#include "gbengine/rendering/city_assets.hpp"
#include "gbengine/assets/records.hpp"
#include "gbengine/rendering/screen_chrome.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
Image load_party_marker(const IFileSystem& files,std::uint8_t record) {
    const auto bytes=load_record(files,"ICON.DAX",record);
    // Native506E:04FC uses paired separately loaded poses. Sourcezero masks;
    // combat CPIC/COMSPR13 replacement and display0/8 exchange do not apply.
    if(bytes.size()!=17+24*24/2 || read_u16(bytes,0)!=24 || read_u16(bytes,2)!=3 ||
       read_u16(bytes,4)!=0 || read_u16(bytes,6)!=0 || bytes[8]!=1)
        throw DecodeError("Unsupported native overland party icon layout");
    Image result{24,24,0,0,{}};result.pixels.reserve(24*24);
    for(const auto value:std::span(bytes).subspan(17))
        for(const auto color:{static_cast<unsigned>(value>>4),static_cast<unsigned>(value&15)})
            result.pixels.push_back(color?ega_ui_palette[color]:0);
    return result;
}
}
CityAssets CityAssets::load(const IFileSystem& files) {
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    CityAssets result;
    result.overlay=OverlandMapOverlay::load(files);
    constexpr std::size_t river_at=0xc7c0+0x0900,river_size=77;
    if(module.size()<river_at+river_size)throw DecodeError("Original river palette tables are truncated");
    const auto river_data=std::span(module).subspan(river_at,river_size);
    if(sha256(river_data)!="d8e237a20ca71a379832304e5646bf635c723dbcf64bfacf32054de413d5bb18")
        throw DecodeError("Unsupported original river palette tables");
    std::copy_n(river_data.begin(),29,result.river_selectors.begin());
    for(unsigned color=0;color<16;++color) {
        if(river_data[29+color]!=color)throw DecodeError("Invalid original river source palette");
        for(unsigned group=0;group<2;++group) {
            const auto target=river_data[45+group*16+color];
            if(target>15)throw DecodeError("Invalid original river target palette");
            result.river_palettes[group][color]=target;
        }
    }
    constexpr std::size_t windows_at=0xc7c0+0x094d,windows_size=36;
    if(module.size()<windows_at+windows_size)throw DecodeError("Original river viewport tables are truncated");
    const auto windows=std::span(module).subspan(windows_at,windows_size);
    if(sha256(windows)!="29b98b5d6e23a5ee5c8e5523a2a70f3fe8d27f0cb3dd0d751def98806e69064d")
        throw DecodeError("Unsupported original river viewport tables");
    for(std::size_t i=0;i<result.river_windows.size();++i) {
        result.river_windows[i]={read_u16(windows,i*2),read_u16(windows,18+i*2)};
        if(result.river_windows[i][0]>39 || result.river_windows[i][1]>31)
            throw DecodeError("Invalid original river viewport coordinates");
    }
    if(module.size()<0xc7c0+0x3648+result.cells.size())throw DecodeError("Original overland map table is truncated");
    std::copy_n(module.begin()+0xc7c0+0x3648,result.cells.size(),result.cells.begin());
    result.display_cells=result.cells;
    for(const auto id:std::array<std::uint8_t,2>{1,2}) {
        auto bank=decode_ega(load_record(files,"SQRPACI.DAX",id),"SQRPACI.DAX");
        if(bank.size()!=128 || std::any_of(bank.begin(),bank.end(),[](const Image& i){return i.width!=24 || i.height!=24;}))
            throw DecodeError("Invalid city tile bank");
        result.tiles.insert(result.tiles.end(),std::make_move_iterator(bank.begin()),std::make_move_iterator(bank.end()));
    }
    for(unsigned pose=0;pose<result.party_marker.size();++pose)
        result.party_marker[pose]=load_party_marker(files,static_cast<std::uint8_t>(pose/2+(pose%2)*128));
    return result;
}
void CityAssets::load_replacement_tiles(const IFileSystem& files) {
    auto bank=decode_ega(load_record(files,"BACPAC.DAX",1),"BACPAC.DAX");
    if(tiles.size()!=256 || bank.size()!=40 || std::any_of(bank.begin(),bank.end(),[](const Image& i){return i.width!=24 || i.height!=24;}))
        throw DecodeError("Invalid city replacement tiles");
    std::move(bank.begin(),bank.end(),tiles.begin());replacement_tiles_loaded=true;
}
void CityAssets::apply_river_recovery(std::mt19937* random) {
    if(tiles.size()!=256)throw DecodeError("River recovery requires the original city tile banks");
    // Validate every input before changing pixels or consuming the owned RNG.
    for(const auto selector:river_selectors) {
        const auto& image=tiles[selector];
        if(image.width!=24 || image.height!=24 || image.pixels.size()!=24*24)
            throw DecodeError("Invalid native river tile layout");
    }
    for(const auto& palette:river_palettes)for(const auto target:palette)
        if(target>15)throw DecodeError("Invalid native river palette");
    // Source EGA mode1 loops tile, changed source colour, row, then8-pixel
    // bytegroup. Each random bytegroup always consumes two Random(256) calls,
    // even when no matching source-colour pixels remain in that group.
    for(std::size_t entry=0;entry<river_selectors.size();++entry) {
        auto& image=tiles[river_selectors[entry]];
        const auto& palette=river_palettes[entry<11?0:1];
        for(unsigned color=0;color<16;++color) {
            if(palette[color]==color)continue;
            for(unsigned row=0;row<24;++row)for(unsigned group=0;group<3;++group) {
                unsigned mask=255;
                if(random) {
                    const auto first=static_cast<std::uint8_t>((*random)());
                    const auto second=static_cast<std::uint8_t>((*random)());
                    mask=first&second;
                }
                for(unsigned pixel=0;pixel<8;++pixel) {
                    auto& target=image.pixels[row*24+group*8+pixel];
                    if((mask&(128u>>pixel)) && target==ega_ui_palette[color])target=ega_ui_palette[palette[color]];
                }
            }
        }
    }
}
std::uint8_t CityAssets::tile(int x,int y,unsigned mode) const {
    if(mode<2 || mode>4 || x<0 || x>15 || y<0 || y>35) throw DecodeError("Invalid city coordinates");
    return cells.at(static_cast<std::size_t>(y*44+x+static_cast<int>(mode-2)*13));
}
std::uint16_t CityAssets::display_tile(int global_x,int y,const OverlandCampaignState& state) const {
    if(global_x<0 || global_x>=44 || y<0 || y>=36)throw DecodeError("Invalid overland display coordinates");
    const auto index=static_cast<std::size_t>(y*44+global_x);
    if(!cells.at(index))throw DecodeError("Invalid zero city tile");
    return overlay.display_tile(display_cells.at(index),global_x,y,state);
}
void CityAssets::draw_window(Framebuffer& frame,int start_x,int start_y,int left,int top,const OverlandCampaignState* state) const {
    if(start_x<0 || start_x>39 || start_y<0 || start_y>31 || left<0 || top<0 ||
       left>screen_width-120 || top>screen_height-120)
        throw DecodeError("Invalid overland map window");
    if(tiles.size()!=256)throw DecodeError("Overland map tiles are not loaded");
    // Validate the full viewport before modifying any framebuffer pixels.
    for(int row=0;row<5;++row)for(int col=0;col<5;++col) {
        const auto index=static_cast<std::size_t>((start_y+row)*44+start_x+col);
        if(!cells.at(index))throw DecodeError("Invalid zero city tile");
        const auto value=state?display_tile(start_x+col,start_y+row,*state):display_cells.at(index);
        if(!value)throw DecodeError("Invalid zero city tile");
        const auto& image=tiles[value-1];
        if(image.width!=24 || image.height!=24 || image.pixels.size()!=24*24)
            throw DecodeError("Invalid overland tile layout");
    }
    for(int row=0;row<5;++row) for(int col=0;col<5;++col) {
        const auto index=static_cast<std::size_t>((start_y+row)*44+start_x+col);
        if(!cells.at(index))throw DecodeError("Invalid zero city tile");
        const auto value=state?display_tile(start_x+col,start_y+row,*state):display_cells.at(index);
        if(!value) throw DecodeError("Invalid zero city tile");
        const auto& image=tiles.at(value-1);
        for(int py=0;py<24;++py) for(int px=0;px<24;++px)
            frame[static_cast<std::size_t>((top+row*24+py)*320+left+col*24+px)]=image.pixels[static_cast<std::size_t>(py*24+px)];
    }
}
void CityAssets::draw(Framebuffer& frame,int x,int y,unsigned mode,Direction facing,unsigned pose,int left,int top,const OverlandCampaignState* state) const {
    if(mode<2 || mode>4 || x<0 || x>15 || y<0 || y>35 || static_cast<unsigned>(facing)>3 ||
       pose>=party_marker.size() || left<0 || top<0 || left>screen_width-120 || top>screen_height-120)
        throw DecodeError("Invalid overland map rendering request");
    if(tiles.size()!=256)throw DecodeError("Overland map tiles are not loaded");
    const auto& icon=party_marker[pose];
    if(icon.width!=24 || icon.height!=24 || icon.pixels.size()!=24*24)
        throw DecodeError("Overland party marker is not loaded");
    const int start_x=std::clamp(x+static_cast<int>(mode-2)*13-2,0,39),start_y=std::clamp(y-2,0,31);
    draw_window(frame,start_x,start_y,left,top,state);
    const int marker_x=left+(x+static_cast<int>(mode-2)*13-start_x)*24,marker_y=top+(y-start_y)*24;
    const bool mirror=static_cast<unsigned>(facing)>=2;
    for(int py=0;py<24;++py)for(int px=0;px<24;++px) {
        const auto color=icon.pixels[static_cast<std::size_t>(py*24+(mirror?23-px:px))];
        if(color)frame[static_cast<std::size_t>((marker_y+py)*320+marker_x+px)]=color;
    }
}
}
