#include "gbengine/ui/overland_screen.hpp"
#include "gbengine/resources/string_store.hpp"

namespace GBEngine {
namespace {
const std::string& text(std::size_t offset) {
    return StringStore::active().get(pascal_string_id("START.EXE:module",offset));
}
}
OverlandScreen OverlandScreen::load(const IFileSystem& files) {
    OverlandScreen result;result.map_=CityAssets::load(files);
    result.chrome_=ScreenChrome::load(files);result.font_=GameFont::load(files);
    return result;
}
void OverlandScreen::draw(Framebuffer& frame,const OverlandPosition& position,unsigned pose,const Party* party) const {
    if(position.region<2 || position.region>4 || position.x<0 || position.x>15 ||
       position.y<0 || position.y>35 || static_cast<unsigned>(position.facing)>3 || pose>=6)
        throw DecodeError("Invalid overland rendering position or animation frame");
    frame.fill(ega_ui_palette[0]);chrome_.draw(frame,ChromeLayout::exploration(false));
    map_.draw(frame,position.x,position.y,position.region,position.facing,pose,8,8);
    if(party) {
        font_.draw_text(frame,text(0x12fb),136,16,15,1,ega_ui_palette[15]);
        font_.draw_text(frame,text(0x1300),264,16,6,1,ega_ui_palette[15]);
        for(std::size_t i=0;i<party->size();++i) {
            const auto& member=party->member(i);const int y=32+8*static_cast<int>(i);
            const auto color=party->selected_index()==i?15u:!member.active()?12u:member.byte(0x10e)==1?14u:11u;
            font_.draw_text(frame,member.name(),136,y,15,1,ega_ui_palette[color]);
            const auto raw=member.byte(0x111);const int shift=raw<51?1:raw<=60?2:raw<=69?1:0;
            font_.draw_text(frame,std::to_string(60-raw),256+8*shift,y,4-shift,1,ega_ui_palette[10]);
            const auto hp=member.hit_points();const int pad=hp<10?2:hp<100?1:0;
            font_.draw_text(frame,std::to_string(hp),288+8*pad,y,3-pad,1,ega_ui_palette[hp<member.byte(0x32)?14:10]);
        }
    }
    font_.draw_text(frame,"OVERLAND STAGE 1",8,144,38,1,ega_ui_palette[15]);
    font_.draw_text(frame,"REGION "+std::to_string(position.region)+"  X "+std::to_string(position.x)+"  Y "+std::to_string(position.y),8,160,38,1);
    font_.draw_text(frame,"ARROWS MOVE - WATER AND MAP EDGES BLOCK",0,192,40,1,ega_ui_palette[15]);
}
}
