#include "gbengine/combat/opponent_equipment_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <set>

namespace GBEngine {
std::uint8_t OpponentEquipmentService::weapon_score(const Item& item,const ItemCatalog& catalog,const Character& owner,
                                                   const Character* retained_target) {
    const auto& d=catalog.definition(item.type());
    auto score=static_cast<std::uint8_t>(d.damage_dice()*d.damage_sides());
    if(item.bonus()>0)score=static_cast<std::uint8_t>(score+item.bonus()*8);
    if(d.damage_bonus()>0)score=static_cast<std::uint8_t>(score+d.damage_bonus()*2);
    // Target-dependent55 overwrites all preceding damage/bonus arithmetic.
    if(item.type()==0x55 && retained_target && retained_target->byte(0x76)>0 && retained_target->byte(0x76)<128)score=8;
    if(d.attack_flags()&8)score=static_cast<std::uint8_t>(score+(d.byte(12)-1)*2);
    if(d.hands()<2)score=static_cast<std::uint8_t>(score+3);
    if(static_cast<unsigned>(d.hands())+owner.byte(0x100)>3 ||
       (item.byte(0x3e)==0x84 && (item.byte(0x3d)&15)!=owner.byte(0xa0)) ||
       item.byte(0x3d)==0x53 || item.cursed())score=0;
    return score;
}
OpponentEquipmentResult OpponentEquipmentService::stage(const DeploymentState& state,CombatActorIdentity id,
                                                        const ItemCatalog& catalog,OpponentEquipmentContext context) {
    OpponentEquipmentResult out{state,false,{}};
    const auto found=std::find_if(out.state.actors.begin(),out.state.actors.end(),[&](const auto& actor){return actor.id==id;});
    if(found==out.state.actors.end() || found->removed)throw DecodeError("Opponent equipment actor identity is stale");
    const auto index=static_cast<std::size_t>(found-out.state.actors.begin());
    std::set<ItemId> identities;
    for(std::size_t i=0;i<found->character.inventory().size();++i) {
        const auto item_id=found->character.inventory().item(i).id();
        if(!item_id || !identities.insert(item_id).second)
            throw DecodeError("Opponent equipment requires unique owned item identities");
    }
    // Reacquire after every native toggle: each returns an owned candidate.
    const auto character=[&]() -> Character& {return out.state.actors[index].character;};
    auto slots=EquipmentRules::inspect(character(),catalog);
    for(unsigned slot=0;slot<2;++slot)if(slots.slots[slot])
        character().set_byte(0x100,static_cast<std::uint8_t>(character().byte(0x100)-
            catalog.definition(character().inventory().item(*slots.slots[slot]).type()).hands()));
    std::optional<std::size_t> weapon,shield,ranged;
    std::uint8_t best_ranged=1,best_shield=0;
    auto best=static_cast<std::uint8_t>(character().byte(0xa5)*character().byte(0xa3));
    if(character().byte(0xa7)>0 && character().byte(0xa7)<128)
        best=static_cast<std::uint8_t>(best+character().byte(0xa7)*2);
    for(std::size_t i=0;i<character().inventory().size();++i) {
        const auto& item=character().inventory().item(i);const auto& d=catalog.definition(item.type());
        if(!(d.class_mask()&character().byte(0xb0)))continue;
        if(d.category()==0) {
            const Character* target=nullptr;
            if(item.type()==0x55 && context.retained_target) {
                const auto at=std::find_if(out.state.actors.begin(),out.state.actors.end(),[&](const auto& actor){return actor.id==*context.retained_target;});
                if(at==out.state.actors.end())throw DecodeError("Opponent retained equipment target identity is stale");
                target=&at->character;
            }
            const auto score=weapon_score(item,catalog,character(),target);
            if((d.attack_flags()&0x18) && score>best_ranged){ranged=i;best_ranged=score;}
            if(!(d.attack_flags()&8) && score>best){weapon=i;best=score;}
        } else if(d.category()==1) {
            const auto score=static_cast<std::uint8_t>(item.bonus()<0?0:item.bonus()+1);
            if(score>best_shield){shield=i;best_shield=score;}
        }
    }
    if(ranged) {
        const auto& d=catalog.definition(character().inventory().item(*ranged).type());const auto flags=d.attack_flags();
        std::optional<std::size_t> ammunition;
        if(flags&16)ammunition=ranged;
        if(flags&8){if(flags&1)ammunition=slots.slots[11];if(flags&128)ammunition=slots.slots[12];}
        const bool legal=ammunition.has_value() || flags==10;
        const bool close=d.byte(12)>=2 && (flags&0x14)==0x14;
        if(best_ranged>(best>>1) && legal && (close || !context.adjacent_opponent))weapon=ranged;
    }
    const auto owned_id=[&](std::optional<std::size_t> item_index) -> std::optional<ItemId> {
        if(!item_index)return {};
        return character().inventory().item(*item_index).id();
    };
    const auto chosen_weapon=owned_id(weapon),chosen_shield=owned_id(shield);
    const auto toggle=[&](std::optional<ItemId> item_id) {
        if(!item_id)return;
        const Item* source=nullptr;
        for(std::size_t i=0;i<character().inventory().size();++i)
            if(character().inventory().item(i).id()==*item_id)source=&character().inventory().item(i);
        std::optional<std::size_t> notice;
        if(source && !source->readied()) {
            if(source->byte(0x3e)==0x83 && character().byte(0x10)<=18 &&
               (character().byte(0x10)!=18 || character().byte(0x16)<=100))notice=0x11ba6;
            if(source->byte(0x3e)==0x87 && character().byte(0x10)<19)notice=0x11d76;
        }
        auto candidate=CombatEquipmentService::toggle_native(out.state,id,*item_id,catalog);
        if(candidate.action.status==ItemActionStatus::unresolved)
            throw DecodeError("Opponent equipment callback is unresolved: "+candidate.action.message);
        if(candidate.action.applied()) {
            out.state=std::move(candidate.state);
            if(notice)out.notices.push_back({id,pascal_string_id("GAME.OVR",*notice),*notice!=0x11d76});
        }
    };
    bool change=!(slots.slots[0] && (owned_id(slots.slots[0])==chosen_weapon || character().inventory().item(*slots.slots[0]).cursed()));
    if(change) {
        toggle(owned_id(slots.slots[0]));EquipmentRules::recalculate(character(),catalog);
        slots=EquipmentRules::inspect(character(),catalog);
        if(slots.slots[1] && !character().inventory().item(*slots.slots[1]).cursed())
            character().set_byte(0x100,static_cast<std::uint8_t>(character().byte(0x100)-
                catalog.definition(character().inventory().item(*slots.slots[1]).type()).hands()));
        toggle(chosen_weapon);out.changed=true;
    }
    EquipmentRules::recalculate(character(),catalog);
    CombatEquipmentService::refresh_primary(out.state.actors[index],catalog,context.round_index);
    slots=EquipmentRules::inspect(character(),catalog);
    change=!(slots.slots[1] && (owned_id(slots.slots[1])==chosen_shield || character().inventory().item(*slots.slots[1]).cursed()));
    if(character().byte(0x100)<3) {
        if(character().byte(0x100)<2 && change) {
            toggle(owned_id(slots.slots[1]));EquipmentRules::recalculate(character(),catalog);toggle(chosen_shield);out.changed=true;
        }
    } else {
        // The selected build removes the candidate weapon if the shield is
        // absent OR cursed; otherwise it removes the existing shield.
        if(!slots.slots[1] || character().inventory().item(*slots.slots[1]).cursed())toggle(chosen_weapon);
        else toggle(owned_id(slots.slots[1]));
        out.changed=true;
    }
    EquipmentRules::recalculate(character(),catalog);
    return out;
}
}
