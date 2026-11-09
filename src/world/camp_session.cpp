#include "gbengine/world/exploration_session.hpp"
#include "gbengine/magic/magic_effect_service.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
CampWorkResult unavailable() {return {CampWorkStatus::unresolved,"Camp work requires an idle restored campaign"};}
}
bool ExplorationSession::stage_nested_camp() {
    if(nested_camp_ || camp_open_ || combat_ || campaign_token_ || party_defeated_) return false;
    const auto& request=vm.pending_request();
    if(!request || request->kind!=EclRequestKind::engine_service || vm.selection_to_restore() ||
       static_cast<std::uint8_t>(vm.preview_party_parameter(request->token,world))!=9) return false;
    auto child=vm;
    child.start_entry(2);
    // 2182:3171 changes49ED/442E only: a nested entry retains comparison
    // flags and the resident GOSUB chain until the real child EXIT clears it.
    child.flags_=vm.flags_;child.stack_=vm.stack_;
    child.invocation_selection_=vm.invocation_selection_;
    nested_camp_=NestedCampFrame{vm,phase_,NestedCampStage::entering};
    nested_camp_return_pending_=false;
    vm=std::move(child);phase_=ExplorationPhase::idle;
    boundary.clear();return true;
}
void ExplorationSession::complete_nested_camp() {
    if(!nested_camp_ || vm.status()!=EclStatus::halted || vm.pending_request())
        throw DecodeError("Nested camp return requires a completed child invocation");
    // 2182:2E10 restores resident49ED then calls EXIT(2182:0001).
    // Keep every child bank/script/clock/RNG mutation, but discard both GOSUB
    // chains and the outer38 request. No instruction after38/9 may execute.
    vm.request_sequence_=std::max(vm.request_sequence_,nested_camp_->outer.request_sequence_);
    vm.pc_=nested_camp_->outer.pending_next_;
    vm.pending_.reset();vm.pending_followups_.clear();vm.stack_.clear();
    vm.restore_selection_=false;vm.combat_preparation_committed_=false;
    vm.status_=EclStatus::halted;
    phase_=nested_camp_->phase;nested_camp_.reset();
    // The camp modal is gone and exploration owns the viewport again. Refresh
    // its backdrop here; ordinary ECL EXIT deliberately retains presented art.
    refresh_world_art();sprite_active_=false;
    sprite_cache_.reset();sprite_closeup_shown_=false;
    nested_camp_return_pending_=true;boundary.clear();
}
bool ExplorationSession::update_nested_camp() {
    if(!nested_camp_ || vm.status()!=EclStatus::halted || vm.pending_request()) return false;
    if(nested_camp_->stage==NestedCampStage::entering) {
        nested_camp_->stage=NestedCampStage::ready;
        phase_=ExplorationPhase::idle;boundary="Encamp";return true;
    }
    if(nested_camp_->stage==NestedCampStage::ready || nested_camp_->stage==NestedCampStage::camping) return true;
    complete_nested_camp();nested_camp_return_pending_=false;return false;
}
bool ExplorationSession::begin_camp() {
    if(camp_open_) return true;
    if(!can_act() && !nested_camp_ready()) return false;
    const auto cleanup=CampWorkService::cleanup_camp(world.party);
    if(cleanup.status!=CampWorkStatus::applied) {boundary=cleanup.message;return false;}
    rest_camp_.daily_ticks=0;rest_={};rest_plan_revision_.reset();camp_interrupted_=false;camp_open_=true;
    if(nested_camp_)nested_camp_->stage=NestedCampStage::camping;
    return true;
}
bool ExplorationSession::end_camp() {
    if(!camp_open_ || roster_drop_revision_ || rest_.running || rest_.awaiting_presentation ||
        phase_!=ExplorationPhase::idle || vm.status()!=EclStatus::halted || vm.pending_request()) return false;
    const auto cleanup=CampWorkService::cleanup_camp(world.party);
    if(cleanup.status!=CampWorkStatus::applied) {boundary=cleanup.message;return false;}
    camp_encounter_pending_=camp_interrupted_;camp_interrupted_=false;
    camp_open_=false;rest_={};rest_plan_revision_.reset();
    if(nested_camp_) {
        if(camp_encounter_pending_)nested_camp_->stage=NestedCampStage::encounter;
        else complete_nested_camp();
    }
    return true;
}
void ExplorationSession::resume_after_camp(std::size_t budget) {
    if(nested_camp_return_pending_) {
        nested_camp_return_pending_=false;pump(budget);return;
    }
    if(!camp_encounter_pending_) return;
    if(!budget || budget>1000000) throw DecodeError("Invalid camp encounter execution budget");
    // The interruption tick and camp cleanup are already committed. The abort
    // checkpoint must retain that work, not return to before Rest or reroll it.
    camp_encounter_pending_=false;
    const auto retained=retain_exploration_checkpoint();
    if(!retained) {
        camp_encounter_pending_=true;boundary="Cannot retain interrupted-camp exploration checkpoint";return;
    }
    auto staged=vm;staged.start_entry(3);
    if(nested_camp_) {staged.flags_=vm.flags_;staged.stack_=vm.stack_;}
    // Native4436/4438 belongs to the outer exploration command, not to
    // Encamp's current selection. Preserve that identity for a later0A/EXIT
    // pair in entry3; without0A the current camp selection remains unchanged.
    if(nested_camp_)staged.invocation_selection_=nested_camp_->outer.invocation_selection_;
    vm=std::move(staged);
    display_text.clear();pump(budget);
}
CampWorkChoices ExplorationSession::memorize_choices(std::size_t member) {
    if(!camp_open_ || !campaign_time_bound_ || !can_act()) {CampWorkChoices r;static_cast<CampWorkResult&>(r)=unavailable();return r;}
    return CampWorkService::memorize_choices(world.party,member,spell_catalog());
}
CampScribeChoices ExplorationSession::scribe_choices(std::size_t member) {
    if(!camp_open_ || !campaign_time_bound_ || !can_act()) {CampScribeChoices r;static_cast<CampWorkResult&>(r)=unavailable();return r;}
    return CampWorkService::scribe_choices(world.party,member,spell_catalog());
}
CampWorkResult ExplorationSession::schedule_memorize(std::size_t member,SpellId spell) {
    if(!camp_open_ || !campaign_time_bound_ || !can_act()) return unavailable();
    return CampWorkService::schedule_memorize(world.party,member,spell_catalog(),spell);
}
CampWorkResult ExplorationSession::clear_memorize(std::size_t member) {
    if(!camp_open_ || !campaign_time_bound_ || !can_act()) return unavailable();
    const auto gate=CampWorkService::memorize_choices(world.party,member,spell_catalog());
    if(gate.status!=CampWorkStatus::applied) return {gate.status,gate.message};
    return CampWorkService::clear_memorize(world.party,member);
}
CampWorkResult ExplorationSession::schedule_scribe(std::size_t member,ItemId item,std::size_t slot) {
    if(!camp_open_ || !campaign_time_bound_ || !can_act()) return unavailable();
    return CampWorkService::schedule_scribe(world.party,member,spell_catalog(),item,slot);
}
CampWorkResult ExplorationSession::clear_scribe(std::size_t member) {
    if(!camp_open_ || !campaign_time_bound_ || !can_act()) return unavailable();
    const auto gate=CampWorkService::scribe_choices(world.party,member,spell_catalog());
    if(gate.status!=CampWorkStatus::applied) return {gate.status,gate.message};
    return CampWorkService::clear_scribe(world.party,member);
}
CampWorkPreparation ExplorationSession::prepare_rest() {
    if(!camp_open_ || !campaign_time_bound_ || !can_act()) {CampWorkPreparation r;static_cast<CampWorkResult&>(r)=unavailable();return r;}
    auto result=RestService::prepare(world.party,spell_catalog());
    if(result.status==CampWorkStatus::applied) rest_plan_revision_=world.party.revision();
    else rest_plan_revision_.reset();
    return result;
}
CampWorkResult ExplorationSession::begin_rest(std::uint32_t minutes) {
    if(!camp_open_ || !campaign_time_bound_ || !can_act()) return unavailable();
    if(!rest_plan_revision_ || *rest_plan_revision_!=world.party.revision())
        return {CampWorkStatus::rejected,"Rest preparation changed; reopen the duration editor"};
    auto result=RestService::start(rest_,minutes);
    if(result.status==CampWorkStatus::applied) rest_plan_revision_.reset();
    return result;
}
RestTickResult ExplorationSession::rest_tick() {
    RestTickResult rejected;
    if(!camp_open_ || !campaign_time_bound_ || !rest_.running || party_defeated_ ||
            camp_interrupted_ || phase_!=ExplorationPhase::idle || vm.status()!=EclStatus::halted) {
        static_cast<CampWorkResult&>(rejected)=unavailable();return rejected;
    }
    try {
        auto current=clock();if(!current) throw DecodeError("Rest requires an initialized campaign clock");
        auto party=world.party;auto camp=rest_camp_;auto rest=rest_;auto staged_vm=vm;
        const auto prior=vm.random_state();auto random=prior;
        auto outcome=RestService::tick(camp,rest,*current,party,spell_catalog(),random,
            vm.read_value(0x6dd2),vm.read_value(0x6dd3),expiry_resolver_);
        if(outcome.status!=CampWorkStatus::applied) {boundary=outcome.message;return outcome;}
        staged_vm.commit_rest_tick(prior,random,current->words());
        world.party=std::move(party);rest_camp_=camp;rest_=rest;vm=std::move(staged_vm);
        check_party_defeat();
        if(outcome.outcome==RestOutcome::interrupted) camp_interrupted_=true;
        return outcome;
    } catch(const std::exception& error) {
        rejected.status=CampWorkStatus::unresolved;rejected.message="Rest unresolved: "+std::string(error.what());
        boundary=rejected.message;return rejected;
    }
}
void ExplorationSession::stop_rest() {RestService::stop(rest_);}
void ExplorationSession::rest_notice_presented() {RestService::acknowledge(rest_);}
RosterResult ExplorationSession::move_camp_member(RosterDirection direction) {
    if(!camp_open_ || !campaign_time_bound_ || !can_act())
        return {RosterStatus::rejected,"Party order requires an idle restored camp"};
    const auto result=RosterService::move(world.party,direction);
    if(result.applied()) rest_plan_revision_.reset();
    return result;
}
RosterResult ExplorationSession::drop_camp_member(bool confirmed) {
    if(!camp_open_ || !campaign_time_bound_ || !activity_idle())
        return {RosterStatus::rejected,"Party drop requires an idle restored camp"};
    if(roster_drop_revision_ && (*roster_drop_revision_!=world.party.revision() || roster_drop_member_!=active_character()))
        return {RosterStatus::rejected,"The party changed during drop confirmation"};
    auto party=world.party;auto staged_vm=vm;
    const auto result=RosterService::drop(party,confirmed);
    if(result.applied()) {
        staged_vm.seed_value(0x6e3e,static_cast<std::uint16_t>(vm.read_value(0x6e3e)-1));
        world.party=std::move(party);vm=std::move(staged_vm);rest_plan_revision_.reset();
        check_party_defeat();
    }
    cancel_camp_drop();
    return result;
}
bool ExplorationSession::begin_camp_drop() {
    if(!camp_open_ || !campaign_time_bound_ || !can_act() || !active_character()) return false;
    roster_drop_revision_=world.party.revision();roster_drop_member_=active_character();return true;
}
void ExplorationSession::cancel_camp_drop() {roster_drop_revision_.reset();roster_drop_member_.reset();}
}
