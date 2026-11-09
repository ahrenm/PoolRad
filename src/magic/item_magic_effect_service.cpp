#include "gbengine/magic/magic_effect_service.hpp"
#include "gbengine/party/equipment.hpp"
#include "gbengine/magic/noncombat_damage_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
namespace GBEngine {
MagicResult MagicEffectService::resolve_item(const ItemMagicRequest& request,std::span<Character> members,std::mt19937& random) {
    return resolve_item_bound(request,members,random,false);
}
MagicResult MagicEffectService::resolve_item_bound(const ItemMagicRequest& request,std::span<Character> members,std::mt19937& random,bool combat_bound) {
    try {
        const auto& definition=catalog_.item_definition(request.cast);
        if(request.context.casting_blocked) return {MagicStatus::rejected,"Item casting is disabled in this game state",{}};
        if((request.context.domain==MagicDomain::combat && !combat_bound) || (!combat_bound && !definition.noncombat_target_mode()))
            return {MagicStatus::unresolved,"Item casting requires the combat service",{}};
        if(request.caster>=members.size()) return {MagicStatus::rejected,"Item caster is not in the party",{}};
        const auto mode=definition.noncombat_target_mode();
        if(!combat_bound && ((mode==1 && (request.target.kind!=MagicTargetKind::party_member || request.target.id!=request.caster)) ||
           (mode==2 && (request.target.kind!=MagicTargetKind::party_member || request.target.id>=members.size())) ||
           (mode==4 && request.target.kind!=MagicTargetKind::party)))
            return {MagicStatus::rejected,"Item target does not match its native noncombat mode",{}};
        if(!combat_bound && mode!=1 && mode!=2 && mode!=4) return {MagicStatus::unresolved,"Unknown item target mode",{}};
        if(combat_bound && (request.target.kind!=MagicTargetKind::combatant || request.target.id>=members.size()))return {MagicStatus::rejected,"Combat item target is not bound",{}};
        auto& target=members[!combat_bound && mode==4?request.caster:request.target.id];
        if(!target.effects_restored()) return {MagicStatus::unresolved,"Item target effects are not restored",{}};
        const auto id=request.cast.value;
        NativeEffectContext context{items_,&random,execution_mode_,&catalog_};
        MagicResult combat_result{MagicStatus::applied,{},{}};
        const auto report=[&](std::size_t offset,bool recipient_animation=false) {if(combat_bound)combat_result.feedback.push_back({request.target.id,pascal_string_id("GAME.OVR",offset),recipient_animation});};
        if(id==67)return resolve_reading_item(request,members,random);
        if(id==66) return {MagicStatus::applied,{},{}}; // Verified native empty handler.
        if(id==57 || id==63) {
            if(id==57 && remove_effect(target,0x2a,context)) return {MagicStatus::applied,{},{}};
            const auto apply=[&](Character& recipient) {
                if(!recipient.effects_restored()) throw DecodeError("Party item casting requires all owned effect lists");
                // GetDuration is called per acquired target, before the
                // generic applier's phase9 resistance/immunity queries.
                std::uint16_t duration=0;
                if(id==57) for(unsigned die=0;die<5;++die) duration=static_cast<std::uint16_t>(duration+roll(random,4));
                else duration=static_cast<std::uint16_t>((roll(random,10)+(combat_bound?roll(random,10):10))*10);
                if(!application_queries(recipient,definition.effect_code(),12,random)) {report(0x2c532);return;}
                const auto old=std::find_if(recipient.effects_.begin(),recipient.effects_.end(),[&](const CharacterEffect& e){return e.code()==definition.effect_code();});
                if(old!=recipient.effects_.end() && (duration==0 || (old->word_1()!=0 && old->word_1()<duration)))
                    remove_effect_at(recipient,static_cast<std::size_t>(old-recipient.effects_.begin()),context);
                if(recipient.effects_.size()>=65535) throw DecodeError("Effect record count exceeds native bound");
                CharacterEffect effect;effect.payload={definition.effect_code(),0,0,12,0};effect.set_duration(duration);
                effect.source_file="START.EXE:module";effect.source_offset=definition.provenance().module_offset;
                effect.item_origin=request.item_origin;effect.item_cast_id=id;recipient.effects_.push_back(std::move(effect));
                report(0x27018+(id==57?0x2df8:0x301d),true);
            };
            if(id==63 && !combat_bound) for(auto& member:members) apply(member);else apply(target);
            return combat_result;
        }
        if(id==59) {
            if(!items_) return {MagicStatus::unresolved,"Item strength requires equipment recalculation",{}};
            if(target.effects_.size()>=65535) throw DecodeError("Effect record count exceeds native bound");
            const auto strength_context=install_strength(target,21,0);
            EquipmentRules::recalculate(target,*items_);
            const auto duration=static_cast<std::uint16_t>(40+10*roll(random,4));
            CharacterEffect effect;effect.payload={definition.effect_code(),0,0,strength_context,1};effect.set_duration(duration);
            effect.source_file="START.EXE:module";effect.source_offset=definition.provenance().module_offset;
            effect.item_origin=request.item_origin;effect.item_cast_id=id;
            target.effects_.push_back(std::move(effect));if(strength_context<128)report(0x27018+0x2ed9);return combat_result;
        }
        if(id==58 || id==62) {
            if(target.status()!=0 && target.status()!=1 && target.status()!=4 && target.status()!=5)
                return {MagicStatus::unresolved,"Item healing revival requires its native status service",{}};
            if(id==58) {
                const auto remove_first=[&](std::uint8_t code,bool suppressed) {
                    const auto it=std::find_if(target.effects_.begin(),target.effects_.end(),[&](const CharacterEffect& effect){return effect.code()==code;});
                    return it!=target.effects_.end() && remove_native(target,static_cast<std::size_t>(it-target.effects_.begin()),suppressed,context);
                };
                if(remove_first(0x37,false)) {
                    remove_first(0x16,true);return {MagicStatus::applied,{},{}};
                }
                const bool first=remove_first(0x22,true),second=remove_first(0x2b,true);
                if(second) {remove_first(0x2c,true);remove_first(0x1f,true);}
                const bool third=remove_first(0x32,true);
                if(third) remove_first(0x39,true);
                // The native potion cures an existing disease instead of
                // healing on the same use. No dice are drawn in that branch.
                if(first || second || third) return {MagicStatus::applied,{},{}};
            }
            const auto amount=static_cast<std::uint8_t>(id==62?roll(random,4)+roll(random,4)+2:roll(random,4)+8);
            (void)NoncombatRecoveryService::heal(target,amount,false,execution_mode_);
            return {MagicStatus::applied,{},amount};
        }
        // The other item-only rows require actual combat services.
        return {MagicStatus::unresolved,"This item-only handler requires further native consumers",{}};
    } catch(const DecodeError& error) {return {MagicStatus::unresolved,error.what(),{}};}
}
}
