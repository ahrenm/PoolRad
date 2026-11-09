#include "gbengine/magic/magic_effect_service.hpp"
#include "gbengine/magic/saving_throw_service.hpp"
#include "gbengine/party/equipment.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <limits>

namespace GBEngine {
namespace {
bool native_hammer(const Item& item) {return item.type()==0x14 && item.byte(0x31)==0xf3;}
}
MagicResult MagicEffectService::resolve_weapon_magic(const MagicRequest& request,std::span<Character> members,std::mt19937& random) {
    const auto index=request.target.id;
    auto& target=members[index];
    MagicResult result{MagicStatus::applied,{},{}};
    if(request.spell.value==13) {
        const auto save=SavingThrowService::resolve(members,index,members[request.caster],4,0,random);
        result.rolled_amount=save.die;
        if(!save.saved && remove_effect(target,0x0c,{items_,&random,1})) {
            result.feedback.push_back({index,pascal_string_id("GAME.OVR",0x2bf62)});
            result.feedback.push_back({index,pascal_string_id("GAME.OVR",0x28375)});
        }
        return result;
    }
    if(!items_)return {MagicStatus::unresolved,"Generated weapon requires equipment recalculation",{}};
    // 4612:19C3 applies the marker first, then calls phase 0 unconditionally,
    // including a resolved generic resistance veto.
    if(!append_marker(target,catalog_.definition(request.spell),request,random,request.caster_level,1))
        result.feedback.push_back({index,pascal_string_id("GAME.OVR",0x2c532)});
    auto& inventory=target.inventory_.items_;
    const auto found=std::find_if(inventory.begin(),inventory.end(),native_hammer);
    if(found==inventory.end() && target.byte(0xc7)<16) {
        if(!request.next_item_id)throw DecodeError("Generated spell weapon requires a staged item identity allocator");
        ItemId maximum=0;
        for(const auto& member:members)for(const auto& item:member.inventory_.items_)maximum=std::max(maximum,item.id());
        if(!*request.next_item_id || *request.next_item_id<=maximum || *request.next_item_id==std::numeric_limits<ItemId>::max())
            throw DecodeError("Generated spell weapon identity allocator is invalid or exhausted");
        Item item;item.id_=(*request.next_item_id)++;
        item.origin_={"GAME.OVR:spiritual-hammer",{},62523};
        item.bytes_[0x2e]=0x14;item.bytes_[0x30]=0x14;item.bytes_[0x31]=0xf3;
        item.bytes_[0x32]=1;item.bytes_[0x3d]=0x17;item.bytes_[0x3e]=0x89;
        inventory.push_back(std::move(item));
        result.feedback.push_back({index,pascal_string_id("GAME.OVR",0xf42d)});
    }
    EquipmentRules::recalculate(target,*items_);
    return result;
}
bool MagicEffectService::reverse_hammer(Character& target,const ItemCatalog& items) {
    const auto found=std::find_if(target.inventory_.items_.begin(),target.inventory_.items_.end(),native_hammer);
    if(found!=target.inventory_.items_.end()) {
        target.bound_equipment_effects_.erase(found->id());
        target.inventory_.items_.erase(found);
    }
    // The original callback always refreshes cached equipment, even if there
    // was no matching weapon or the item was already traded/dropped.
    EquipmentRules::recalculate(target,items);return true;
}
}
