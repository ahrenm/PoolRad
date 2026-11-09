#pragma once
#include "gbengine/assets/decoders.hpp"
namespace GBEngine {
using UiPalette=std::array<std::uint32_t,16>;
inline constexpr UiPalette ega_ui_palette{0xff000000u,0xff0000aau,0xff00aa00u,0xff00aaaau,
    0xffaa0000u,0xffaa00aau,0xffaa5500u,0xffaaaaaau,0xff555555u,0xff5555ffu,
    0xff55ff55u,0xff55ffffu,0xffff5555u,0xffff55ffu,0xffffff55u,0xffffffffu};
class UiTiles {
    std::array<std::array<std::uint8_t,64>,31> tiles_{};
    bool loaded_=false;
public:
    static UiTiles load(const IFileSystem&);
    void draw(Framebuffer&,unsigned tile,int left,int top,const UiPalette& palette=ega_ui_palette) const;
};
// Inclusive interior bounds in native 8-pixel cells; the border is outside them.
struct ChromeBox {int left,top,right,bottom;};
struct ChromeLayout {
    std::vector<ChromeBox> boxes;
    static ChromeLayout exploration(bool viewport_border=true);
    static ChromeLayout combat();
    static ChromeLayout character_creation();
    static ChromeLayout merchant();
    static ChromeLayout character_view(bool portrait);
    // Creation and future screens supply their recovered boxes here rather than
    // copying the exploration painter or claiming an unrecovered native layout.
};
class ScreenChrome {
    UiTiles tiles_;
public:
    static ScreenChrome load(const IFileSystem&);
    const UiTiles& tiles() const {return tiles_;}
    void draw(Framebuffer&,const ChromeLayout&,unsigned style=0,const UiPalette& palette=ega_ui_palette) const;
};
}
