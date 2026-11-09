#include "gbengine/party/combat_icon_service.hpp"
#include <limits>

namespace GBEngine {
namespace {
void validate(const CombatIconAppearance& value) {
    // Imported style0 uses the same source pair as1. Editing only offers1/2.
    if(value.head>=64 || value.weapon>=64 || value.style>2)
        throw DecodeError("Unsupported saved combat icon appearance");
}
void write(Character& character,const CombatIconAppearance& value) {
    validate(value);
    character.set_byte(0xbd,value.head);character.set_byte(0xbe,value.weapon);character.set_byte(0xc0,value.style);
    for(std::size_t i=0;i<value.colors.size();++i)character.set_byte(0xc1+i,value.colors[i]);
}
}
CombatIconAppearance CombatIconService::read(const Character& character) {
    CombatIconAppearance result{character.byte(0xbd),character.byte(0xbe),character.byte(0xc0),{}};
    for(std::size_t i=0;i<result.colors.size();++i)result.colors[i]=character.byte(0xc1+i);
    validate(result);return result;
}
CombatIconPlan CombatIconService::prepare(const Party& party,std::size_t member) {
    if(member>=party.size() || party.selected_index()!=member ||
       party.revision()==std::numeric_limits<std::uint64_t>::max())
        throw DecodeError("Combat icon editing requires the selected owned character");
    return {party.revision(),member,party.member(member).byte(0xbf),read(party.member(member))};
}
CombatIconResult CombatIconService::keep(Party& party,const CombatIconPlan& plan,const CombatIconAppearance& value) {
    try {
        validate(value);
        if(plan.revision!=party.revision() || plan.member>=party.size() || party.selected_index()!=plan.member ||
           party.member(plan.member).byte(0xbf)!=plan.native_identity || read(party.member(plan.member))!=plan.appearance)
            return {false,"Combat icon selection changed; reopen the editor"};
        if(value==plan.appearance)return {true,{}};
        if(party.revision()==std::numeric_limits<std::uint64_t>::max())return {false,"Combat icon revision exhausted"};
        auto staged=party;write(staged.members_[plan.member],value);++staged.revision_;party=std::move(staged);
        return {true,{}};
    }catch(const DecodeError& error) {return {false,error.what()};}
}
void CombatIconService::cycle_part(CombatIconAppearance& value,CombatIconPart part,bool next) {
    validate(value);
    std::uint8_t* selected;
    unsigned maximum;
    if(part==CombatIconPart::head) {selected=&value.head;maximum=13;}
    else if(part==CombatIconPart::weapon) {selected=&value.weapon;maximum=31;}
    else throw DecodeError("Unknown combat icon part");
    if(next)*selected=*selected<maximum?static_cast<std::uint8_t>(*selected+1):0;
    else *selected=*selected?static_cast<std::uint8_t>(*selected-1):static_cast<std::uint8_t>(maximum);
}
void CombatIconService::cycle_color(CombatIconAppearance& value,std::size_t component,bool secondary,bool next) {
    validate(value);
    if(component>=value.colors.size())throw DecodeError("Unknown combat icon color component");
    auto& color=value.colors[component];const unsigned shift=secondary?4u:0u;
    const auto changed=static_cast<unsigned>(((color>>shift)+(next?1u:15u))&15u);
    color=static_cast<std::uint8_t>((color&~(15u<<shift))|(changed<<shift));
}
void CombatIconService::set_style(CombatIconAppearance& value,std::uint8_t style) {
    if(style!=1 && style!=2)throw DecodeError("Combat icon editing supports native Small/Large styles");
    value.style=style;
}
Character CombatIconService::preview(const Character& source,const CombatIconAppearance& value) {
    auto result=source;write(result,value);return result;
}
}
