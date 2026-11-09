#include "gbengine/ui/game_over_screen.hpp"
#include "gbengine/resources/string_store.hpp"
namespace GBEngine {
void draw_game_over(Framebuffer& frame,const GameFont& font,const ScreenChrome& chrome) {
    frame.fill(ega_ui_palette[0]);chrome.draw(frame,ChromeLayout::merchant());
    const auto& title=overlay_string(0x6a4c);
    font.draw_text(frame,title,8*((39-static_cast<int>(title.size()))/2),8,38,1,ega_ui_palette[15]);
    font.draw_text(frame,overlay_string(0x6a55),40,16,33,21,GameFont::green,true);
    font.draw_text(frame,overlay_string(0x6a8b),0,192,40,1,ega_ui_palette[13]);
}
}
