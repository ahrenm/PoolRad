#include "gbengine/magic/magic_effect_service.hpp"
#include "gbengine/magic/noncombat_periodic_effects.hpp"
#include "gbengine/magic/noncombat_damage_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/party/equipment.hpp"
#include <algorithm>
namespace GBEngine {
PeriodicEffectBindings MagicEffectService::periodic_bindings(NativeEffectContext context) {
    const auto woke=[context](const Character& c,bool was_active) {
        if(!was_active && c.active() && context.notices)
            context.notices->push_back({context.member,pascal_string_id("GAME.OVR",c.byte(0x10e)==1?182066:182086)});
    };
    return {
        [context](Character& c,std::uint8_t amount){
            NoncombatDamageService::apply(c,amount,0,context.random,context.edits);
            if(amount==1 && context.notices)context.notices->push_back({context.member,pascal_string_id("GAME.OVR",0x2c18b)});
        },
        [context,woke](Character& c,std::uint8_t amount,bool force){const auto active=c.active();const auto done=NoncombatRecoveryService::heal(c,amount,force,context.execution_mode);woke(c,active);return done;},
        [context](Character& c,std::uint8_t hp){const auto done=NoncombatRecoveryService::revive(c,hp,context.execution_mode);
            if(done && context.notices)context.notices->push_back({context.member,pascal_string_id("GAME.OVR",c.byte(0x10e)==1?182066:182086)});
            return done;},
        [context](Character& c,std::uint8_t status,EffectExpiryEdits& edits){set_native_status(c,status,edits,context);},
        [context](Character& c){if(!context.items)throw DecodeError("Periodic healing requires inventory recalculation");EquipmentRules::recalculate(c,*context.items);}
    };
}
void MagicEffectService::set_native_status(Character& target,std::uint8_t status,EffectExpiryEdits& edits,NativeEffectContext context) {
    if(context.execution_mode!=1 && context.execution_mode!=2)throw DecodeError("Status callback requires noncombat execution");
    if(target.status()==7 || target.status()==6 || target.status()==8)return;
    if(status!=6 && status!=8)throw DecodeError("Unsupported noncombat terminal status");
    // Native phase0D rescue/removal consumers need real combat/party context.
    static constexpr std::array<std::uint8_t,5> rescue{0x63,0x64,0x67,0x4b,0x4a};
    if(std::any_of(target.effects_.begin(),target.effects_.end(),[](const auto& e){return std::find(rescue.begin(),rescue.end(),e.code())!=rescue.end();}))
        throw DecodeError("Terminal status requires an unresolved native rescue callback");
    if(!context.spells)throw DecodeError("Terminal status requires source effect cleanup definitions");
    const auto module=context.spells->native_module();
    constexpr std::size_t start=0xd3d4;
    if(module.size()<start+16)throw DecodeError("Terminal status cleanup table is truncated");
    target.set_byte(0x10c,status);target.set_byte(0x10d,0);target.set_byte(0x11b,0);
    for(std::size_t i=0;i<16;++i) {
        const auto code=module[start+i];
        std::optional<std::size_t> at;
        for(std::size_t j=0;j<target.effects_.size();++j)
            if(target.effects_[j].code()==code && std::find(edits.remove_indices.begin(),edits.remove_indices.end(),j)==edits.remove_indices.end()) {at=j;break;}
        if(!at) {
            const auto appended=std::find_if(edits.append.begin(),edits.append.end(),[&](const auto& node){return node.code()==code;});
            if(appended!=edits.append.end()) {
                if(appended->payload[4])throw DecodeError("Terminal cleanup needs an appended effect reversal");
                edits.append.erase(appended);
            }
            continue;
        }
        const auto node=target.effects_[*at];
        if(node.payload[4]) {
            if(NoncombatPeriodicEffects::handles(code)) {
                if(!NoncombatPeriodicEffects::reverse({context.member,node,*at},target,edits,periodic_bindings(context)))
                    throw DecodeError("Terminal periodic cleanup unresolved");
            } else if(!reverse_native(target,node,*at,false,context))throw DecodeError("Terminal effect cleanup unresolved");
        }
        edits.remove_indices.push_back(*at);
    }
}
bool MagicEffectService::reverse_with_edits(const EffectExpiryRequest& request,Character& target,EffectExpiryEdits& edits) {
    if(!NoncombatPeriodicEffects::handles(request.effect.code()))return reverse(request,target);
    NativeEffectContext context{items_,random_,execution_mode_,&catalog_,&edits.notices,request.member,&edits};
    return NoncombatPeriodicEffects::reverse(request,target,edits,periodic_bindings(context));
}
}
