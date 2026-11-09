#include "gbengine/rendering/screen_chrome.hpp"
#include "gbengine/assets/records.hpp"
#include <algorithm>
namespace GBEngine {
UiTiles UiTiles::load(const IFileSystem& files) {
    const auto images=decode_ega(load_record(files,"8X8D1.DAX",202),"8X8D1.DAX");
    if(images.size()!=31) throw DecodeError("Invalid native UI tile count");
    UiTiles result;
    for(std::size_t i=0;i<images.size();++i) {
        if(images[i].width!=8 || images[i].height!=8) throw DecodeError("Invalid native UI tile dimensions");
        for(std::size_t p=0;p<64;++p) {
            const auto at=std::find(ega_ui_palette.begin(),ega_ui_palette.end(),images[i].pixels[p]);
            if(at==ega_ui_palette.end()) throw DecodeError("Invalid native UI palette index");
            result.tiles_[i][p]=static_cast<std::uint8_t>(at-ega_ui_palette.begin());
        }
    }
    result.loaded_=true;return result;
}
void UiTiles::draw(Framebuffer& frame,unsigned tile,int left,int top,const UiPalette& palette) const {
    if(!loaded_ || tile>=tiles_.size() || left<0 || top<0 || left>312 || top>192) throw DecodeError("UI tile outside screen or resource bank");
    for(int y=0;y<8;++y) for(int x=0;x<8;++x)
        frame[static_cast<std::size_t>((top+y)*320+left+x)]=palette[tiles_[tile][static_cast<std::size_t>(y*8+x)]];
}
ChromeLayout ChromeLayout::exploration(bool viewport) {
    ChromeLayout result{{{1,1,38,22},{1,17,38,22},{1,1,15,15}}};
    if(viewport) result.boxes.push_back({3,3,13,13});
    return result;
}
ChromeLayout ChromeLayout::combat() {return {{{1,1,21,21},{23,1,38,21}}};}
ChromeLayout ChromeLayout::character_creation() {return {{{1,1,38,22}}};}
ChromeLayout ChromeLayout::merchant() {return {{{1,1,38,22}}};}
ChromeLayout ChromeLayout::character_view(bool portrait) {
    ChromeLayout result{{{1,1,38,22}}};
    if(portrait)result.boxes.push_back({28,1,38,11});
    return result;
}
ScreenChrome ScreenChrome::load(const IFileSystem& files) {ScreenChrome result;result.tiles_=UiTiles::load(files);return result;}
void ScreenChrome::draw(Framebuffer& frame,const ChromeLayout& layout,unsigned style,const UiPalette& palette) const {
    if(style>2) throw DecodeError("Unsupported native chrome style");
    for(const auto box:layout.boxes) {
        if(box.left<1 || box.top<1 || box.right>38 || box.bottom>23 || box.left>box.right || box.top>box.bottom)
            throw DecodeError("Invalid native chrome interior bounds");
    }
    const auto corner=20+3*style,vertical=corner+1,horizontal=corner+2;
    for(const auto box:layout.boxes) {
        for(int x=box.left;x<=box.right;++x) {
            tiles_.draw(frame,horizontal,x*8,(box.top-1)*8,palette);
            tiles_.draw(frame,horizontal,x*8,(box.bottom+1)*8,palette);
        }
        for(int y=box.top;y<=box.bottom;++y) {
            tiles_.draw(frame,vertical,(box.left-1)*8,y*8,palette);
            tiles_.draw(frame,vertical,(box.right+1)*8,y*8,palette);
        }
        for(const int x:{box.left-1,box.right+1}) for(const int y:{box.top-1,box.bottom+1})
            tiles_.draw(frame,corner,x*8,y*8,palette);
    }
}
}
