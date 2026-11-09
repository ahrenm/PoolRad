#include "gbengine/combat/combat_session.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>

namespace GBEngine {
const SpellCatalog& CombatSession::spell_catalog() const {
    if(!magic_)throw DecodeError("Combat spell catalog requires an initialized tactical runtime");
    return magic_->catalog();
}
std::vector<MemorizedSpell> CombatSession::castable_spells(bool blocked) const {
    if(!deployment_ || !round_.active || moving() || round_.phase!=CombatRoundPhase::player || !runtime_error_.empty())
        throw DecodeError("Combat Cast requires a current player actor");
    return CombatCastingService::memorized(*deployment_,*round_.active,blocked);
}
void CombatSession::prune_casts() {
    if(!deployment_) {casts_.clear();return;}
    std::erase_if(casts_,[&](const auto& pending){return !CombatCastingService::still_pending(*deployment_,pending);});
}
const CombatCastPending* CombatSession::pending_cast() const {
    if(!deployment_ || !round_.active || round_.phase!=CombatRoundPhase::casting_pending)return nullptr;
    const auto found=std::find_if(casts_.begin(),casts_.end(),[&](const auto& pending){return pending.caster==*round_.active;});
    return found!=casts_.end() && CombatCastingService::still_pending(*deployment_,*found)?&*found:nullptr;
}
CombatMagicTargetPolicy CombatSession::magic_target_policy() const {
    const auto* pending=pending_cast();
    if(!magic_ || !pending)throw DecodeError("Combat target selection lost its owned casting continuation");
    return magic_->target_policy(*deployment_,pending->caster,pending->cast_index,
                                pending->origin!=CombatCastOrigin::memorized,pending->caster_level);
}
std::vector<CombatActorIdentity> CombatSession::magic_candidates() const {
    const auto* pending=pending_cast();
    if(!magic_ || !pending)throw DecodeError("Combat target list lost its owned casting continuation");
    return magic_->candidates(*deployment_,pending->caster,pending->cast_index,
                             pending->origin!=CombatCastOrigin::memorized,pending->caster_level);
}
void CombatSession::schedule_spell(std::size_t slot,const ItemCatalog& items,bool blocked,std::mt19937& random) {
    (void)castable_spells(blocked);
    auto proposal=CombatCastingService::schedule(*deployment_,*round_.active,slot,spell_catalog(),blocked);
    if(const auto reason=magic_->unsupported(proposal.pending.cast_index))throw DecodeError(*reason);
    auto next_round=round_;auto next_random=random;
    if(proposal.resume_selection)CombatRoundService::resume_selection(proposal.state,next_round,items,next_random,&round_context_);
    else {next_round.phase=CombatRoundPhase::casting_pending;next_round.notices.clear();}
    if(proposal.pending.delayed)next_round.notices.insert(next_round.notices.begin(),
        {proposal.pending.caster,pascal_string_id("GAME.OVR",0x14c00)});
    auto pending=casts_;
    std::erase_if(pending,[&](const auto& cast){return cast.caster==proposal.pending.caster || !CombatCastingService::still_pending(proposal.state,cast);});
    pending.push_back(proposal.pending);
    *deployment_=std::move(proposal.state);round_=std::move(next_round);casts_=std::move(pending);
    opponent_turn_.reset();strikes_.clear();standalone_travel_.reset();magic_playback_.reset();action_message_.clear();random=std::move(next_random);++revision_;
}
void CombatSession::schedule_item(ItemId item,std::optional<std::size_t> slot,const ItemCatalog& items,bool blocked) {
    if(!deployment_ || !round_.active || moving() || round_.phase!=CombatRoundPhase::player || !runtime_error_.empty())
        throw DecodeError("Combat Use requires a current player actor");
    auto proposal=CombatCastingService::prepare_item(*deployment_,*round_.active,item,slot,items,spell_catalog(),blocked);
    if(const auto reason=magic_->unsupported(proposal.pending.cast_index,true))throw DecodeError(*reason);
    auto pending=casts_;std::erase_if(pending,[&](const auto& cast){return cast.caster==proposal.pending.caster || !CombatCastingService::still_pending(proposal.state,cast);});
    pending.push_back(proposal.pending);
    *deployment_=std::move(proposal.state);casts_=std::move(pending);round_.phase=CombatRoundPhase::casting_pending;round_.notices.clear();
    strikes_.clear();standalone_travel_.reset();magic_playback_.reset();action_message_.clear();++revision_;
}
void CombatSession::read_scroll(ItemId item,const ItemCatalog& items) {
    if(!deployment_ || !round_.active || moving() || round_.phase!=CombatRoundPhase::player)
        throw DecodeError("Combat scroll reading requires a current player actor");
    auto next=CombatCastingService::read_scroll(*deployment_,*round_.active,item,items);
    *deployment_=std::move(next);strikes_.clear();standalone_travel_.reset();magic_playback_.reset();action_message_.clear();++revision_;
}
void CombatSession::ready_item(ItemId item,const ItemCatalog& items,std::mt19937* random) {
    if(!deployment_ || !round_.active || moving() || round_.phase!=CombatRoundPhase::player)
        throw DecodeError("Combat equipment requires a current player actor");
    const auto wearer=*round_.active;auto next_random=random?*random:std::mt19937{};
    const CombatEquipmentDamageContext context{magic_?&magic_->damage_effects():nullptr,random?&next_random:nullptr,{}};
    auto next=CombatEquipmentService::ready(*deployment_,wearer,item,items,round_.round_index,context);
    if(!next.action.applied())throw DecodeError(next.action.message);
    auto next_round=round_;
    const auto actor=std::find_if(next.state.actors.begin(),next.state.actors.end(),[&](const auto& value){return value.id==wearer;});
    if(actor==next.state.actors.end())throw DecodeError("Combat equipment wearer is no longer owned");
    if(!actor->character.active())CombatRoundService::resume_selection(next.state,next_round,items,next_random,&round_context_);
    std::shared_ptr<const std::vector<MonsterAbilityPlayback>> playback;
    if(next.playback && !next.playback->events.empty()) {
        auto presented=*next.playback;presented.state=next.state;
        presented.events.push_back({CombatMagicEventKind::state,0,{},std::make_shared<const DeploymentState>(next.state)});
        playback=std::make_shared<const std::vector<MonsterAbilityPlayback>>(std::vector<MonsterAbilityPlayback>{{wearer,0x84,std::move(presented)}});
    }
    *deployment_=std::move(next.state);round_=std::move(next_round);monster_playback_=std::move(playback);prune_casts();
    strikes_.clear();standalone_travel_.reset();magic_playback_.reset();action_message_.clear();
    if(random)*random=std::move(next_random);
    ++revision_;
}
void CombatSession::resolve_magic(std::span<const CombatActorIdentity> targets,std::optional<CombatPosition> centre,
                                 const ItemCatalog& items,std::uint16_t dungeon_flag,std::mt19937& random,const CombatAttackContext& attack_context) {
    const auto* retained=pending_cast();
    if(!retained || !magic_ || moving() || !runtime_error_.empty())throw DecodeError("Combat magic requires its owned executor continuation");
    const auto pending=*retained;auto state=*deployment_;auto next_random=random;
    const auto actor=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return a.id==pending.caster;});
    CombatMagicPlayback playback;playback.caster=pending.caster;playback.cast_index=pending.cast_index;
    playback.before=std::make_shared<const DeploymentState>(*deployment_);
    playback.origin=actor->position;playback.destination=centre;
    if(!playback.destination && !targets.empty()) {
        // Target acquisition leaves native6DB9/BA at the last confirmed
        // actor. Hold's reverse handler starts there before drawing the
        // additional recipient projectiles of its own.
        const auto target=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return a.id==targets.back();});
        if(target!=state.actors.end())playback.destination=target->position;
    }
    // 46120C20 consumes memorized slots before the handler. 40531A8A charges
    // item resources after it. The outer transaction retains both on failure.
    if(pending.origin==CombatCastOrigin::memorized)state=CombatCastingService::finish(state,pending,items);
    CombatMagicIntent intent;intent.caster=pending.caster;intent.cast_index=pending.cast_index;
    intent.targets.assign(targets.begin(),targets.end());intent.centre=centre;
    intent.item_origin=pending.origin!=CombatCastOrigin::memorized;intent.caster_level=pending.caster_level;
    intent.source_item=pending.item_origin;intent.next_item_id=next_item_id_;intent.dungeon_flag=dungeon_flag;
    intent.side_attack_bonus=attack_context.side_attack_bonus;
    auto result=magic_->stage(state,intent,items,next_random);
    if(result.casting_before)playback.before=std::move(result.casting_before);
    if(pending.origin!=CombatCastOrigin::memorized)result.state=CombatCastingService::finish(result.state,pending,items);
    auto next_round=round_;
    if(pending.ends_turn)CombatRoundService::finish_cast(result.state,next_round,items,next_random,&round_context_);
    else {next_round.phase=CombatRoundPhase::player;next_round.notices.clear();}
    playback.feedback=std::move(result.feedback);playback.damage=std::move(result.damage);playback.events=std::move(result.events);
    playback.projectiles=std::move(result.projectiles);
    *deployment_=std::move(result.state);round_=std::move(next_round);next_item_id_=result.next_item_id;
    std::erase_if(casts_,[&](const auto& cast){return cast.caster==pending.caster;});prune_casts();
    opponent_turn_.reset();strikes_.clear();standalone_travel_.reset();magic_playback_=std::move(playback);action_message_.clear();random=std::move(next_random);++revision_;
}
void CombatSession::cancel_magic(const ItemCatalog& items,std::mt19937& random) {
    const auto* retained=pending_cast();if(!retained)throw DecodeError("Cancel spell requires an owned executor continuation");
    const auto pending=*retained;auto state=CombatCastingService::finish(*deployment_,pending,items);
    auto next_round=round_;auto next_random=random;
    if(pending.ends_turn)CombatRoundService::finish_cast(state,next_round,items,next_random,&round_context_);
    else {next_round.phase=CombatRoundPhase::player;next_round.notices.clear();}
    next_round.notices.insert(next_round.notices.begin(),{pending.caster,pascal_string_id("GAME.OVR",0x27c2a),false});
    // The source Cancelled string is shared by memorized and item execution.
    *deployment_=std::move(state);round_=std::move(next_round);
    std::erase_if(casts_,[&](const auto& cast){return cast.caster==pending.caster;});prune_casts();
    opponent_turn_.reset();strikes_.clear();standalone_travel_.reset();magic_playback_.reset();action_message_.clear();random=std::move(next_random);++revision_;
}
}
