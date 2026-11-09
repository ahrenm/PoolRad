#pragma once
#include "gbengine/rendering/presentation.hpp"
#include <algorithm>
namespace GBEngine {
inline constexpr std::uint32_t menu_action_color=0xffffffffu;
// Keys are explicit: ordinary text and unavailable actions are never inferred.
inline void draw_menu_actions(const GameFont& font,Framebuffer& frame,std::string_view text,
                              int left,int top,int columns,std::string_view keys,
                              std::uint32_t color=GameFont::green) {
    font.draw_text(frame,text,left,top,columns,1,color);
    for(std::size_t i=0;i<text.size() && i<static_cast<std::size_t>(std::max(0,columns));++i) {
        if(i && text[i-1]!=' ' && text[i-1]!='/')continue;
        const auto ch=static_cast<unsigned char>(text[i]);
        const auto key=static_cast<char>(ch>='a' && ch<='z'?ch-'a'+'A':ch);
        if(keys.find(key)!=std::string_view::npos)
            font.draw_text(frame,text.substr(i,1),left+8*static_cast<int>(i),top,1,1,menu_action_color);
    }
}
}
