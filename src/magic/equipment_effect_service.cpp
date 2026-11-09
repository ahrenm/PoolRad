#include "gbengine/magic/equipment_effect_service.hpp"
#include "gbengine/magic/magic_effect_service.hpp"
#include "gbengine/magic/noncombat_damage_service.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
bool generic(std::uint8_t handler) {
    return handler==0x80 || handler==0x81 || handler==0x82 || handler==0x85 ||
        handler==0x86 || handler==0x88 || handler==0x8a || handler==0x8b;
}
bool supported_passive(std::uint8_t code) {
    // Authenticated generic equipment markers.38/48 also execute their
    // immediate source callback below; their independent19 is not item-owned.
    return code==0x05 || code==0x10 || code==0x13 || code==0x18 ||
        code==0x03 || code==0x1b || code==0x38 || code==0x48 || code==0x3d || code==0x59;
}
ItemActionResult result(ItemActionStatus status,std::string message,ItemId item) {
    return {status,std::move(message),item,{}};
}
bool native_node(const CharacterEffect& effect,std::uint8_t handler,std::uint8_t code) {
    if(effect.code()!=code || effect.word_1()!=0) return false;
    return handler==0x83?effect.payload[4]==1:
        effect.payload[3]==12 && effect.payload[4]==0;
}
bool native_effect_source(const CharacterEffect& effect,const Item& item) {
    if(effect.source_file.ends_with(".SPC"))return true;
    const auto& source=effect.source_file;
    const auto& origin=item.origin();
    // MON archive nodes use an explicit record suffix. Require the companion
    // item archive/disk/record, rather than accepting arbitrary DAX text.
    if(source.size()!=15 || !source.starts_with("MON") || source[3]<'1' || source[3]>'8' ||
       source.substr(4,9)!="SPC.DAX:0" || !origin.archive_record || effect.source_offset%9)return false;
    constexpr char hex[]="0123456789ABCDEF";
    const auto record=*origin.archive_record;
    return source[13]==hex[record>>4] && source[14]==hex[record&15] &&
        origin.file==source.substr(0,4)+"ITM.DAX";
}
}
EquipmentBindingReport EquipmentEffectService::bind_restored(Party& party) {
    EquipmentBindingReport report;
    for(auto& character:party.members_) {
        const auto next=bind_restored(character);
        report.bound+=next.bound;report.unresolved+=next.unresolved;
    }
    if(report.bound)++party.revision_;
    return report;
}
EquipmentBindingReport EquipmentEffectService::bind_restored(Character& c) {
    EquipmentBindingReport report;
    for(const auto& item:c.inventory_.items_) {
        if(!item.readied() || item.byte(0x3e)<128 || c.bound_equipment_effects_.contains(item.id())) continue;
        const auto handler=item.byte(0x3e);
        if(!c.effects_restored_) {++report.unresolved;continue;}
        if(handler==0x89) {
            // 317A creates no node on ready. Its unready callback removes the
            // first spell17; it does not claim the spell marker as item-owned.
            c.bound_equipment_effects_.insert(item.id());++report.bound;continue;
        }
        if((handler==0x87 && c.byte(0x10)>=19) ||
           (handler==0x84 && (item.byte(0x3d)&15)==c.byte(0xa0))) {
            c.bound_equipment_effects_.insert(item.id());++report.bound;continue;
        }
        if(!(handler==0x83 || (generic(handler) && supported_passive(item.byte(0x3d))))) {
            ++report.unresolved;continue;
        }
        const auto code=handler==0x83?std::uint8_t{0x26}:item.byte(0x3d);
        const auto item_count=std::count_if(c.inventory_.items_.begin(),c.inventory_.items_.end(),[&](const Item& other){
            return other.readied() && ((other.byte(0x3e)==0x83 && code==0x26) ||
                (generic(other.byte(0x3e)) && other.byte(0x3d)==code));
        });
        std::optional<std::size_t> at;std::size_t candidates=0;
        for(std::size_t i=0;i<c.effects_.size();++i) if(native_node(c.effects_[i],handler,code)) {
            ++candidates;at=i;
        }
        // MON loading and original saves can retain readied items after their
        // effect has disappeared (or without ever loading an SPC node). Native
        // refresh0BB8 does not ready them again; unready0026 is inert when no
        // same-code node exists. Preserve that state, without a stat replay or
        // fabricated node. A nonmatching same-code node is NOT absence.
        if(item_count==1 && std::none_of(c.effects_.begin(),c.effects_.end(),
                                      [code](const auto& node){return node.code()==code;})) {
            c.bound_equipment_effects_.insert(item.id());++report.bound;continue;
        }
        // A unique payload is a bounded reconstruction, not serialized native
        // ownership. Runtime spell/item provenance must never be appropriated.
        if(item_count!=1 || candidates!=1 || c.effects_[*at].equipment_item_id ||
           c.effects_[*at].item_origin || c.effects_[*at].spell_id || c.effects_[*at].item_cast_id || !native_effect_source(c.effects_[*at],item) ||
           (handler==0x83 && c.effects_[*at].payload[3]<128 && (c.byte(0x10)!=18 || c.byte(0x16)!=100))) {
            ++report.unresolved;continue;
        }
        c.effects_[*at].equipment_item_id=item.id();
        c.bound_equipment_effects_.insert(item.id());++report.bound;
    }
    return report;
}
ItemActionResult EquipmentEffectService::resolve(const ItemEffectRequest& request,std::span<Character> members) {
    try {
        if(request.trigger==ItemEffectTrigger::use || request.actor>=members.size() || request.target!=request.actor)
            return result(ItemActionStatus::rejected,"Equipment effect requires its owning character",request.item);
        auto& c=members[request.actor];
        const auto found=std::find_if(c.inventory_.items_.begin(),c.inventory_.items_.end(),[&](const Item& item){return item.id()==request.item;});
        if(found==c.inventory_.items_.end() || found->effect_bytes()!=request.raw_effects ||
           found->byte(0x3e)!=request.native_effect || found->origin()!=request.origin)
            return result(ItemActionStatus::rejected,"Equipment effect request no longer matches its item",request.item);
        if(!c.effects_restored_) return result(ItemActionStatus::unresolved,"Equipment requires a restored effect list",request.item);
        const auto handler=found->byte(0x3e);const bool remove=request.trigger==ItemEffectTrigger::unequip;
        if(handler==0x89) {
            if(remove) {
                const auto marker=std::find_if(c.effects_.begin(),c.effects_.end(),[](const auto& effect){return effect.code()==0x17;});
                if(marker!=c.effects_.end()) {
                    if(marker->payload[4] && !items_)return result(ItemActionStatus::unresolved,"Generated weapon reversal requires equipment recalculation",request.item);
                    auto context=context_;context.items=items_;
                    (void)MagicEffectService::remove_effect_at(c,static_cast<std::size_t>(marker-c.effects_.begin()),context);
                }
            }
            return result(ItemActionStatus::applied,{},request.item);
        }
        if(handler==0x87) {
            if(!remove && c.byte(0x10)<19) {found->bytes_[0x34]=0;return result(ItemActionStatus::applied,"This character cannot ready this item",request.item);}
            return result(ItemActionStatus::applied,{},request.item);
        }
        if(handler==0x84) {
            if(remove || (found->byte(0x3d)&15)==c.byte(0xa0)) return result(ItemActionStatus::applied,{},request.item);
            // Damage phase6 includes immunity/resistance callbacks. Until their
            // shared damage context is bound, no partial damage may be applied.
            if(!c.active() || c.status()!=0)
                return result(ItemActionStatus::unresolved,"Alignment damage requires its native damage-query service",request.item);
            const auto amount=static_cast<std::uint8_t>(found->byte(0x3d)>>4);
            found->bytes_[0x34]=0;NoncombatDamageService::apply(c,amount,8);
            return result(ItemActionStatus::applied,"The item harms its wearer and remains unreadied",request.item);
        }
        if(handler!=0x83 && !(generic(handler) && supported_passive(found->byte(0x3d))))
            return result(ItemActionStatus::unresolved,"Equipment handler has no noncombat binding",request.item);
        const auto code=handler==0x83?std::uint8_t{0x26}:found->byte(0x3d);
        if(!remove && code==0x48 && !context_.random)
            return result(ItemActionStatus::unresolved,"Equipment invisibility requires its owned random checkpoint",request.item);
        if(remove) {
            const auto old=std::find_if(c.effects_.begin(),c.effects_.end(),[&](const CharacterEffect& e){return e.code()==code;});
            // Native removes first same-code. Require that exact node to be
            // owned by this item rather than removing a spell/other item node.
            if(old==c.effects_.end() && c.bound_equipment_effects_.contains(request.item))
                return result(ItemActionStatus::applied,{},request.item);
            if(old==c.effects_.end() || old->equipment_item_id!=request.item)
                return result(ItemActionStatus::unresolved,"Equipment effect ownership is ambiguous",request.item);
            const auto at=static_cast<std::size_t>(old-c.effects_.begin());
            auto context=context_;context.items=items_;
            if(old->payload[4] && !MagicEffectService::reverse_effect(c,*old,at,context))
                return result(ItemActionStatus::unresolved,"Equipment reversal is unresolved",request.item);
            c.effects_.erase(c.effects_.begin()+static_cast<std::ptrdiff_t>(at));
        } else {
            if(c.effects_.size()>=65535) throw DecodeError("Effect record count exceeds native bound");
            CharacterEffect effect;effect.payload={code,0,0,12,0};
            if(handler==0x83) {effect.payload[3]=MagicEffectService::install_strength(c,18,100);effect.payload[4]=1;}
            effect.source_file="GAME.OVR";effect.source_offset=handler==0x83?72626u:72501u;
            effect.item_origin=found->origin();effect.equipment_item_id=request.item;
            c.effects_.push_back(std::move(effect));
            //2EE2:2EDA dispatches the generic marker immediately after append.
            //319B/1B is empty;1363/38 always appends19;1730/48 draws first,
            //then appends iff unsigned d100<96. Unequip removes only38/48.
            bool invisible=code==0x38;
            if(code==0x48) {
                constexpr std::uint32_t sides=100,threshold=(std::uint32_t{0}-sides)%sides;
                std::uint32_t sample;do {sample=static_cast<std::uint32_t>((*context_.random)());}while(sample<threshold);
                invisible=sample%sides+1<96;
            }
            if(invisible) {
                if(c.effects_.size()>=65535)throw DecodeError("Equipment invisibility exceeds the native effect bound");
                CharacterEffect marker;marker.payload={0x19,1,0,12,0};
                marker.source_file="GAME.OVR";marker.source_offset=code==0x38?0xffbe:0x1038b;
                c.effects_.push_back(std::move(marker));
            }
        }
        return result(ItemActionStatus::applied,{},request.item);
    } catch(const DecodeError& error) {return result(ItemActionStatus::unresolved,error.what(),request.item);}
}
}
