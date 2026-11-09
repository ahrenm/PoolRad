#include "gbengine/ui/menu_hotkeys.hpp"
#include "gbengine/ui/combat_magic_menu.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
const std::string& module(std::size_t at) {return StringStore::active().get(pascal_string_id("START.EXE:module",at));}
const Item& owned(const Character& actor,ItemId id) {
    for(std::size_t i=0;i<actor.inventory().size();++i)if(actor.inventory().item(i).id()==id)return actor.inventory().item(i);
    throw DecodeError("Combat item list lost an owned identity");
}
}
std::size_t CombatMagicMenu::bottom() const {return std::min(rows_.size(),top_+visible());}
void CombatMagicMenu::normalize(int direction) {
    if(empty())return;
    const auto count=bottom()-top_;
    for(std::size_t work=0;work<count && rows_[selected_].heading;++work)
        selected_=top_+(selected_-top_+(direction<0?count-1:1))%count;
}
void CombatMagicMenu::move(int direction) {
    if(empty())return;
    const auto count=bottom()-top_;
    selected_=top_+(selected_-top_+(direction<0?count-1:1))%count;normalize(direction);
}
void CombatMagicMenu::page(int direction) {
    if(empty())return;
    const auto offset=selected_-top_,count=visible();
    if(direction>0) {
        if(top_+count>=rows_.size())return;
        top_=std::min(top_+count,rows_.size()-count);
    }else {if(!top_)return;top_=top_>count?top_-count:0;}
    selected_=std::min(top_+offset,bottom()-1);normalize(direction);
}
CombatMagicMenu CombatMagicMenu::spells(std::span<const MemorizedSpell> spells,const SpellCatalog& catalog) {
    CombatMagicMenu result;std::optional<std::uint8_t> previous;
    for(const auto& entry:spells)if(!entry.pending) {
        const auto level=catalog.definition(entry.spell).level();
        if(previous!=level){result.rows_.push_back({{},level,{},true});previous=level;}
        CombatMagicChoice choice;choice.spell_slot=entry.slot;
        result.rows_.push_back({choice,level,entry.spell,false});
    }
    result.normalize(-1);return result;
}
CombatMagicMenu CombatMagicMenu::items(const Character& actor) {
    CombatMagicMenu result;result.kind_=Kind::items;
    for(std::size_t i=0;i<actor.inventory().size();++i) {
        CombatMagicChoice choice;choice.item=actor.inventory().item(i).id();result.rows_.push_back({choice,0,{},false});
    }
    return result;
}
CombatMagicMenu CombatMagicMenu::scroll(const Character& actor,ItemId id,const SpellCatalog& catalog) {
    CombatMagicMenu result;result.kind_=Kind::scroll;const auto& item=owned(actor,id);
    std::optional<std::uint8_t> previous;
    for(std::size_t slot=0;slot<3;++slot)if(const auto raw=item.effect_bytes()[slot];raw) {
        const SpellId spell(static_cast<std::uint8_t>(raw&127));const auto level=catalog.definition(spell).level();
        if(previous!=level){result.rows_.push_back({{},level,{},true});previous=level;}
        CombatMagicChoice choice;choice.item=id;choice.scroll_slot=slot;
        result.rows_.push_back({choice,level,spell,false});
    }
    result.normalize(-1);return result;
}
CombatMagicChoice CombatMagicMenu::choice() const {
    if(empty() || rows_.at(selected_).heading)throw DecodeError("Combat magic list has no selected resource");
    return rows_[selected_].choice;
}
CombatMagicMenuAction CombatMagicMenu::input(InputEvent event) {
    if(!event.pressed)return CombatMagicMenuAction::none;
    const auto letter=event.character>='a' && event.character<='z'?event.character-'a'+'A':event.character;
    if(letter=='E' || event.key==Key::cancel || event.key==Key::backspace)return CombatMagicMenuAction::leave;
    if(empty())return CombatMagicMenuAction::none;
    if(event.key==Key::home || event.key==Key::up)move(-1);
    else if(event.key==Key::end || event.key==Key::down)move(1);
    else if(event.key==Key::next || letter=='N')page(1);
    else if(event.key==Key::previous || letter=='P')page(-1);
    else if(letter=='R' && item_list())return CombatMagicMenuAction::ready;
    else if(event.key==Key::accept || letter==(item_list()?'U':'C'))return CombatMagicMenuAction::select;
    return CombatMagicMenuAction::none;
}
void CombatMagicMenu::draw(Framebuffer& frame,const GameFont& font,const ScreenChrome& chrome,
        const Character& actor,const SpellCatalog& catalog,std::string_view diagnostic) const {
    frame.fill(ega_ui_palette[0]);chrome.draw(frame,{{{1,1,38,1},{1,3,38,22}}});
    const auto color=!actor.active()?12u:actor.byte(0x10e)==1?14u:11u;
    font.draw_text(frame,actor.name()+module(0x23b3),8,8,38,1,ega_ui_palette[color]);
    if(item_list())font.draw_text(frame,overlay_string(0x21ef2),16,24,36,1,GameFont::green);
    else {
        const auto x=8*static_cast<int>(actor.name().size()+4);
        font.draw_text(frame,overlay_string(0x239ce)+overlay_string(0x23973),x,8,std::max(0,(312-x)/8),1,GameFont::green);
    }
    for(std::size_t i=top_;i<bottom();++i) {
        const auto& row=rows_[i];const int y=40+static_cast<int>(i-top_)*(item_list()?16:8);
        if(row.heading)font.draw_text(frame,module(0xd239+41*row.level),8,y,38,1,ega_ui_palette[13]);
        else if(row.spell)font.draw_text(frame,overlay_string(0x272a2)+std::string(catalog.definition(*row.spell).name()),8,y,38,1,ega_ui_palette[i==selected_?15:10]);
        else {
            const auto& item=owned(actor,*row.choice.item);
            font.draw_text(frame,item.ready_label(),8,y,6,1,ega_ui_palette[i==selected_?15:10]);
            font.draw_text(frame,item.name(),64,y,31,2,ega_ui_palette[i==selected_?15:10],true);
        }
    }
    if(empty())font.draw_text(frame,overlay_string(0x17cba),8,144,38,4,GameFont::green,true);
    std::string commands=item_list()?overlay_string(0x21ebd)+" "+overlay_string(0x21ec3)+" ":overlay_string(0x27053)+overlay_string(0x27018);
    if(top_+visible()<rows_.size())commands+=overlay_string(0x2d657);
    if(top_)commands+=overlay_string(0x2d65d);
    commands+=overlay_string(0x2d663);font.draw_text(frame,commands,0,192,40,1,GameFont::green);
    bool first=true;
    for(std::size_t i=0;i<commands.size();++i) {
        if(first && commands[i]!=' ')font.draw_text(frame,std::string_view(commands).substr(i,1),8*static_cast<int>(i),192,1,1,menu_action_color);
        first=commands[i]==' ' || commands[i]==':';
    }
    if(!diagnostic.empty()) {
        for(int y=136;y<184;++y)std::fill_n(frame.begin()+y*320+8,304,ega_ui_palette[0]);
        font.draw_text(frame,diagnostic,8,136,38,6,ega_ui_palette[12],true);
    }
}
}
