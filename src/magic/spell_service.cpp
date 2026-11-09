#include "gbengine/magic/spell_service.hpp"
#include <algorithm>
namespace GBEngine {
void SpellService::validate_caster(const Character& caster,MagicContext context) {
    if(context.casting_blocked) throw DecodeError("Casting is disabled by the current game state");
    if(caster.status()==1 || !caster.active()) throw DecodeError("This character cannot cast in the current state");
    if(context.domain==MagicDomain::exploration && (caster.status()!=0 || !caster.active()))
        throw DecodeError("Exploration casting requires a normal active character");
    if(!caster.effects_restored()) throw DecodeError("Casting requires restored character effect lists");
}
std::vector<MemorizedSpell> SpellService::castable(std::size_t caster,MagicContext context) const {
    if(caster>=party_.size()) throw DecodeError("Casting requires an imported caster");
    validate_caster(party_.member(caster),context);
    auto entries=Spellbook::memorized(party_.member(caster));
    std::erase_if(entries,[](const MemorizedSpell& entry){return entry.pending;});
    return entries;
}
SpellCastPlan SpellService::prepare(std::size_t caster,std::size_t slot,MagicTarget target,MagicContext context) const {
    const auto entries=castable(caster,context);
    const auto entry=std::find_if(entries.begin(),entries.end(),[&](const MemorizedSpell& e){return e.slot==slot;});
    if(entry==entries.end()) throw DecodeError("Selected memorized spell is no longer available");
    (void)catalog_.definition(entry->spell);
    if(target.kind==MagicTargetKind::none) throw DecodeError("A confirmed spell target is required");
    if(target.kind==MagicTargetKind::party_member && target.id>=party_.size()) throw DecodeError("Spell target is not in the party");
    return {party_.revision(),caster,slot,entry->spell,target,context.domain};
}
MagicResult SpellService::cast(const SpellCastPlan& plan,MagicContext context,std::mt19937& random,IMagicEffectResolver& resolver) {
    try {
        if(plan.revision!=party_.revision() || plan.domain!=context.domain)
            return {MagicStatus::rejected,"Spell selection is stale",{}};
        const auto checked=prepare(plan.caster,plan.slot,plan.target,context);
        if(checked.spell!=plan.spell) return {MagicStatus::rejected,"Memorized slot changed since selection",{}};
        const auto& definition=catalog_.definition(plan.spell);
        if(context.domain!=MagicDomain::combat && !definition.noncombat_target_mode())
            return {MagicStatus::unresolved,"This spell requires the combat service",{}};
        if(context.domain!=MagicDomain::combat) {
            const auto mode=definition.noncombat_target_mode();
            if((mode==1 && (plan.target.kind!=MagicTargetKind::party_member || plan.target.id!=plan.caster)) ||
               (mode==2 && plan.target.kind!=MagicTargetKind::party_member) || (mode==4 && plan.target.kind!=MagicTargetKind::party))
                return {MagicStatus::rejected,"Target does not match the native noncombat spell mode",{}};
            if(mode!=1 && mode!=2 && mode!=4) return {MagicStatus::unresolved,"Unknown noncombat spell target mode",{}};
        }
        auto staged=party_.members_;auto staged_random=random;auto next_item_id=party_.next_item_id_;
        const auto level=staged[plan.caster].byte(definition.school()==0?0x96:0x9b);
        // Native first-ID consumption precedes the handler, but remains staged
        // until a successful effect. Failure consumes neither slot nor RNG.
        (void)Spellbook::consume_first(staged[plan.caster],plan.spell);
        auto result=resolver.resolve({plan.spell,MagicOrigin::memorized,context,plan.caster,plan.target,level,{},&next_item_id},staged,staged_random);
        if(result.applied()) {party_.members_=std::move(staged);party_.next_item_id_=next_item_id;++party_.revision_;random=std::move(staged_random);}
        return result;
    } catch(const DecodeError& error) {return {MagicStatus::unresolved,error.what(),{}};}
}
MagicResult SpellService::apply_external(const MagicRequest& request,std::uint64_t expected_revision,std::mt19937& random,IMagicEffectResolver& resolver) {
    if(expected_revision!=party_.revision()) return {MagicStatus::rejected,"External magic request is stale",{}};
    if(request.origin!=MagicOrigin::script && request.origin!=MagicOrigin::innate)
        return {MagicStatus::rejected,"External magic requires an explicit script or innate origin",{}};
    if(request.caster>=party_.size() || request.target.kind==MagicTargetKind::none)
        return {MagicStatus::rejected,"External magic requires actual caster and target bindings",{}};
    (void)catalog_.definition(request.spell);
    auto staged=party_.members_;auto staged_random=random;auto next_item_id=party_.next_item_id_;
    auto owned=request;owned.next_item_id=&next_item_id;
    auto result=resolver.resolve(owned,staged,staged_random);
    if(result.applied()) {party_.members_=std::move(staged);party_.next_item_id_=next_item_id;++party_.revision_;random=std::move(staged_random);}
    return result;
}
MagicResult SpellService::cast_treasury_detect(std::size_t caster,std::uint64_t expected_revision,
                                              std::mt19937& random,IMagicEffectResolver& resolver) {
    try {
        if(expected_revision!=party_.revision())return {MagicStatus::rejected,"Treasury spell selection is stale",{}};
        if(caster>=party_.size())return {MagicStatus::rejected,"Treasury spell requires the selected party member",{}};
        std::optional<SpellId> spell;
        for(std::size_t slot=0;slot<21;++slot) {
            const auto raw=party_.member(caster).byte(0x17+slot);
            if(raw==5 || raw==11) {spell=SpellId(raw);break;}
        }
        if(!spell)return {MagicStatus::rejected,"Selected character has no Treasury detection spell",{}};
        const auto& definition=catalog_.definition(*spell);
        if(definition.noncombat_target_mode()!=1)return {MagicStatus::unresolved,"Treasury detection source target mode changed",{}};
        auto staged=party_.members_;auto staged_random=random;auto next_item_id=party_.next_item_id_;
        const auto level=staged[caster].byte(definition.school()==0?0x96:0x9b);
        // Native49D5:0FD2 removes every first matching Invisible19 node before
        //148B consumes the spell. Execution6 does not bypass this cleanup.
        const NativeEffectContext native{&party_.item_catalog(),&staged_random,6,&catalog_};
        while(MagicEffectService::remove_effect(staged[caster],0x19,native)){}
        (void)Spellbook::consume_first(staged[caster],*spell);
        const MagicContext context{MagicDomain::exploration,false};
        auto result=resolver.resolve({*spell,MagicOrigin::memorized,context,caster,
            {MagicTargetKind::party_member,caster,0,0},level,{},&next_item_id},staged,staged_random);
        if(result.applied()) {party_.members_=std::move(staged);party_.next_item_id_=next_item_id;++party_.revision_;random=std::move(staged_random);}
        return result;
    }catch(const DecodeError& error) {return {MagicStatus::unresolved,error.what(),{}};}
}
MagicResult SpellService::use_item(const ItemEffectRequest& request,MagicContext context,std::mt19937& random,IMagicEffectResolver& resolver) {
    try {
        if(request.cast_index<1 || request.cast_index>67)
            return {MagicStatus::unresolved,"Unknown native item casting index",{}};
        if(request.actor>=party_.size()) return {MagicStatus::rejected,"Item caster is not in the party",{}};
        validate_caster(party_.member(request.actor),context);
        const auto target_mode=request.cast_index<=56?catalog_.definition(SpellId(request.cast_index)).noncombat_target_mode():
            catalog_.item_definition(ItemCastId(request.cast_index)).noncombat_target_mode();
        if(context.domain!=MagicDomain::combat && !target_mode)
            return {MagicStatus::unresolved,"Item spell requires the combat service",{}};
        if(context.domain!=MagicDomain::combat && target_mode==1 && request.target!=request.actor)
            return {MagicStatus::rejected,"This item spell targets its user",{}};
        if(context.domain!=MagicDomain::combat && target_mode!=1 && target_mode!=2 && target_mode!=4)
            return {MagicStatus::unresolved,"Unknown noncombat item target mode",{}};
        auto staged=party_;auto staged_random=random;auto next_item_id=staged.next_item_id_;
        struct Adapter final : IItemEffectResolver {
            IMagicEffectResolver& resolver;std::mt19937& random;MagicContext context;bool whole_party;ItemId& next_item_id;MagicResult result;
            Adapter(IMagicEffectResolver& r,std::mt19937& rng,MagicContext c,bool all,ItemId& next):resolver(r),random(rng),context(c),whole_party(all),next_item_id(next) {}
            ItemActionResult resolve(const ItemEffectRequest& request,std::span<Character> members) override {
                const MagicTarget target{whole_party?MagicTargetKind::party:MagicTargetKind::party_member,whole_party?0:request.target,0,0};
                if(request.cast_index<=56) result=resolver.resolve({SpellId(request.cast_index),MagicOrigin::item,context,request.actor,target,6,request.origin,&next_item_id},members,random);
                else result=resolver.resolve_item({ItemCastId(request.cast_index),context,request.actor,target,request.origin,&next_item_id},members,random);
                return {result.applied()?ItemActionStatus::applied:result.status==MagicStatus::rejected?ItemActionStatus::rejected:ItemActionStatus::unresolved,
                    result.message,request.item,{}};
            }
        } adapter(resolver,staged_random,context,context.domain!=MagicDomain::combat && target_mode==4,next_item_id);
        const auto action=InventoryService(staged).use(request,adapter);
        if(!action.applied()) return {action.status==ItemActionStatus::rejected?MagicStatus::rejected:MagicStatus::unresolved,action.message,{}};
        staged.next_item_id_=next_item_id;party_=std::move(staged);random=std::move(staged_random);return adapter.result;
    } catch(const DecodeError& error) {return {MagicStatus::unresolved,error.what(),{}};}
}
}
