#include "gbengine/combat/combat_session.hpp"
#include "gbengine/combat/noninterior_battlefield.hpp"
#include "gbengine/combat/actor_preparation.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <limits>
namespace GBEngine {
CombatSession::CombatSession(CombatEntry entry,EncounterState encounter)
    :entry_(entry),encounter_(std::move(encounter)) {
    if(!entry_.token.value || !entry_.ecl_token || !entry_.party_size ||
       (encounter_.duel_context!=encounter_.special.has_value()) ||
       (entry_.kind==CombatEntryKind::launch && !encounter_.duel_context && (!encounter_.setup_prepared || !encounter_.count)) ||
       (entry_.kind==CombatEntryKind::setup_blocked && entry_.setup_error.empty()))
        throw DecodeError("Combat scaffold requires a prepared source encounter and party");
}
CombatSession::CombatSession(const CombatSession& other)
    :entry_(other.entry_),encounter_(other.encounter_),
     deployment_(other.deployment_?std::make_unique<DeploymentState>(*other.deployment_):nullptr),
     preparation_error_(other.preparation_error_),round_(other.round_),spatial_(other.spatial_),attacks_(other.attacks_),strikes_(other.strikes_),standalone_travel_(other.standalone_travel_),
     opponents_(other.opponents_),opponent_turn_(other.opponent_turn_),
     movement_checkpoint_(other.movement_checkpoint_),movement_(other.movement_),targeting_(other.targeting_),turning_(other.turning_),
     magic_(other.magic_),casts_(other.casts_),magic_playback_(other.magic_playback_),monster_playback_(other.monster_playback_),
     retained_(other.retained_),round_context_(other.round_context_),next_item_id_(other.next_item_id_),
     revision_(other.revision_),runtime_attempted_(other.runtime_attempted_),runtime_error_(other.runtime_error_),action_message_(other.action_message_),
     round_end_(other.round_end_),opponent_hp_percent_(other.opponent_hp_percent_),automatic_magic_(other.automatic_magic_),hostility_started_(other.hostility_started_),
     aftermath_(other.aftermath_?std::make_unique<CombatAftermathPending>(*other.aftermath_):nullptr) {}
void CombatSession::hold_aftermath(CombatAftermathResult result,const std::mt19937& expected) {
    if(!outcome_pending() || aftermath_ || result.collection_ready)
        throw DecodeError("Terminal combat outcome requires its unconsumed aftermath boundary");
    aftermath_=std::make_unique<CombatAftermathPending>(CombatAftermathPending{std::move(result),expected,false});++revision_;
}
void CombatSession::toggle_automatic_magic() {
    if(!runtime_attempted_ || !runtime_error_.empty() || moving() ||
       (round_.phase!=CombatRoundPhase::player && round_.phase!=CombatRoundPhase::ai_pending))
        throw DecodeError("Automatic magic toggle requires the native combat command boundary");
    automatic_magic_=!automatic_magic_;round_.notices.clear();
    if(round_.active)round_.notices.push_back({*round_.active,
        pascal_string_id("GAME.OVR",automatic_magic_?0xbb6e:0xbb77),false});
    ++revision_;
}
std::optional<std::size_t> CombatSession::active_index() const {
    if(!deployment_ || !round_.active)return {};
    for(std::size_t i=0;i<deployment_->actors.size();++i)
        if(!deployment_->actors[i].removed && deployment_->actors[i].id==*round_.active)return i;
    throw DecodeError("Combat active actor no longer belongs to its tactical snapshot");
}
void CombatSession::start_runtime(const IFileSystem& files,const ItemCatalog& catalog,std::uint16_t surprise,
                                  std::uint8_t movement_bonus,std::mt19937& random,const CombatAttackContext& attack_context,std::optional<CombatActorIdentity> entry_selected) {
    if(runtime_attempted_ || !deployment_)throw DecodeError("Combat runtime requires unstarted spatial preparation");
    auto state=std::make_unique<DeploymentState>(*deployment_);auto next_random=random;
    auto next_round=round_;next_round.surprise_mask=surprise;next_round.party_movement_bonus=movement_bonus;
    auto spatial=CombatSpatialService::load(files);
    auto attacks=CombatAttackService::load(files);
    auto opponents=BasicOpponentService::load(files);
    auto movement=TacticalMovementService::load(files);
    auto targeting=CombatTargetingService::load(files);auto turning=TurnUndeadService::load(files);
    auto magic=std::make_shared<const CombatMagicService>(CombatMagicService::load(files));
    auto retained=std::make_shared<const RetainedAbilityService>(RetainedAbilityService::load(files));
    const CombatRoundContext round_context{retained.get(),attack_context};
    std::vector<CombatSelectionStep> entry_playback;
    CombatActorPreparation::initial(*state,catalog,next_random,entry_selected,&entry_playback,&magic->catalog());
    std::uint16_t current=0,maximum=0;
    for(const auto& actor:state->actors)if(!actor.removed && actor.character.byte(0x10e)==1) {
        if(actor.character.active())current=static_cast<std::uint16_t>(current+actor.character.hit_points());
        maximum=static_cast<std::uint16_t>(maximum+actor.character.byte(0x32));
    }
    const auto hp_percent=maximum?static_cast<std::uint8_t>((static_cast<std::uint16_t>(current*20u)/maximum)*5u):opponent_hp_percent_;
    std::array<std::uint8_t,2> active_sides{};
    for(const auto& actor:state->actors)if(!actor.removed && actor.character.active()) {
        const auto side=actor.character.byte(0x10e);
        if(side>1)throw DecodeError("Initial combat count has an unsupported actor side");
        ++active_sides[side];
    }
    // 29CB:007A checks the prepared side counts immediately after2C86:1E31.
    // An already empty side skips the complete first-round/selector/minute
    // path and enters native cleanup directly, retaining VM6DCB untouched.
    const bool initially_finished=!active_sides[0] || !active_sides[1];
    if(!initially_finished)CombatRoundService::begin_round(*state,next_round,catalog,next_random,&round_context);
    next_round.selection_playback.insert(next_round.selection_playback.begin(),entry_playback.begin(),entry_playback.end());
    for(const auto& step:entry_playback)if(step.notice)next_round.notices.push_back(*step.notice);
    deployment_=std::move(state);round_=std::move(next_round);spatial_=std::move(spatial);attacks_=std::move(attacks);
    opponents_=std::move(opponents);movement_=std::move(movement);targeting_=std::move(targeting);turning_=std::move(turning);random=std::move(next_random);
    magic_=std::move(magic);retained_=std::move(retained);round_context_=round_context;
    if(initially_finished)round_end_=CombatRoundEndDisposition::outcome_pending;
    opponent_hp_percent_=hp_percent;runtime_attempted_=true;++revision_;
}
void CombatSession::block_runtime(std::string error) {
    runtime_attempted_=true;runtime_error_=std::move(error);++revision_;
}
bool CombatSession::begin_movement() {
    if(!runtime_error_.empty() || moving() || round_.phase!=CombatRoundPhase::player)return false;
    const auto index=active_index();if(!index || !deployment_->actors[*index].extension[6])return false;
    if(deployment_->actors[*index].extension[6]<2) {
        deployment_->actors[*index].extension[6]=0;action_message_.clear();++revision_;return true;
    }
    const auto& actor=deployment_->actors[*index];
    movement_checkpoint_=MoveCheckpoint{actor.id,actor.position,actor.extension[6],actor.extension[9]};strikes_.clear();standalone_travel_.reset();action_message_.clear();++revision_;return true;
}
bool CombatSession::move(unsigned direction,const ItemCatalog& catalog,const CombatAttackContext& context,std::mt19937& random) {
    if(!moving() || !movement_ || !attacks_ || !round_.active)return false;
    auto next_random=random;
    auto result=movement_->stage(*deployment_,*round_.active,direction,catalog,*attacks_,context,next_random,
                                 magic_?&magic_->catalog():nullptr);
    action_message_=std::move(result.step.message);
    if(!result.step.state)return false;
    auto next_round=round_;
    if(result.interrupted)CombatRoundService::resume_selection(*result.step.state,next_round,catalog,next_random,&round_context_);
    else next_round.notices.clear();
    next_round.notices.insert(next_round.notices.begin(),result.notices.begin(),result.notices.end());
    *deployment_=std::move(*result.step.state);round_=std::move(next_round);strikes_=std::move(result.strikes);
    standalone_travel_.reset();
    if(result.interrupted)movement_checkpoint_.reset();
    random=std::move(next_random);++revision_;return true;
}
void CombatSession::command(TacticalCommand command,const ItemCatalog& catalog,std::mt19937& random) {
    const bool interrupt=round_.phase==CombatRoundPhase::ai_pending && command==TacticalCommand::manual_control;
    if(!round_.active || !runtime_error_.empty() || moving() ||
       (!interrupt && round_.phase!=CombatRoundPhase::player))
        throw DecodeError("Tactical command requires a current controlled actor");
    auto result=TacticalCommandService::stage(*deployment_,*round_.active,command,catalog,{interrupt});
    auto next_round=round_;next_round.notices.clear();auto next_random=random;
    if(result.resume_selection)CombatRoundService::resume_selection(result.state,next_round,catalog,next_random,&round_context_);
    else if(result.begin_automatic)next_round.phase=CombatRoundPhase::ai_pending;
    next_round.notices.insert(next_round.notices.begin(),result.notices.begin(),result.notices.end());
    *deployment_=std::move(result.state);round_=std::move(next_round);
    if(result.resume_selection || result.begin_automatic)opponent_turn_.reset();
    strikes_.clear();standalone_travel_.reset();action_message_.clear();random=std::move(next_random);++revision_;
}
std::vector<CombatTarget> CombatSession::targets(const ItemCatalog& catalog) const {
    if(!targeting_ || !round_.active)throw DecodeError("Aim requires a selected actor");
    return targeting_->list(*deployment_,*round_.active,catalog);
}
CombatTarget CombatSession::probe_target(CombatPosition position,const ItemCatalog& catalog) const {
    if(!targeting_ || !round_.active)throw DecodeError("Aim requires a selected actor");
    return targeting_->probe(*deployment_,*round_.active,position,catalog);
}
void CombatSession::turn_undead(const ItemCatalog& catalog,std::mt19937& random) {
    if(!turning_ || !round_.active || moving() || round_.phase!=CombatRoundPhase::player || !runtime_error_.empty())
        throw DecodeError("Turn Undead requires a current player actor");
    auto next_random=random;
    auto result=turning_->stage(*deployment_,*round_.active,next_random);
    auto next_round=round_;
    CombatRoundService::resume_selection(result.state,next_round,catalog,next_random,&round_context_);
    std::vector<CombatRoundNotice> notices{{*round_.active,pascal_string_id("GAME.OVR",0x1392b)}};
    for(const auto& recipient:result.recipients)
        notices.push_back({recipient.actor,pascal_string_id("GAME.OVR",
            recipient.outcome==TurnUndeadOutcome::turned?0x1393b:0x13945)});
    if(result.failed)notices.push_back({*round_.active,pascal_string_id("GAME.OVR",0x13952),false});
    next_round.notices.insert(next_round.notices.begin(),notices.begin(),notices.end());
    *deployment_=std::move(result.state);round_=std::move(next_round);opponent_turn_.reset();
    strikes_.clear();standalone_travel_.reset();action_message_.clear();random=std::move(next_random);++revision_;
}
std::optional<CombatActorIdentity> CombatSession::bump_target(unsigned direction,bool include_allies) const {
    if(!moving() || !spatial_ || !round_.active)return {};
    const auto destination=spatial_->probe(*deployment_,*round_.active,direction);
    if(!destination.occupied_slot)return {};
    const auto index=active_index();
    for(const auto& actor:deployment_->actors)
        if(!actor.removed && actor.id!=*round_.active && actor.native_slot==destination.occupied_slot &&
           (include_allies || actor.character.byte(0x10e)!=deployment_->actors[*index].character.byte(0x10e)))return actor.id;
    return {};
}
bool CombatSession::begin_friendly_bump(unsigned direction) {
    if(direction>=8 || round_.phase!=CombatRoundPhase::player || !runtime_error_.empty())return false;
    const auto target=bump_target(direction,true);const auto index=active_index();if(!target || !index)return false;
    const auto victim=std::find_if(deployment_->actors.begin(),deployment_->actors.end(),[&](const auto& actor){return actor.id==*target;});
    if(victim==deployment_->actors.end() || !victim->character.active() ||
       victim->character.byte(0x10e)!=deployment_->actors[*index].character.byte(0x10e))return false;
    //29CB09DF calls4F3E0AF8 before probing the occupied destination. NoN
    //retains this attempted facing while position and Move budget stay owned.
    deployment_->actors[*index].extension[9]=static_cast<std::uint8_t>(direction);
    strikes_.clear();standalone_travel_.reset();action_message_.clear();++revision_;return true;
}
bool CombatSession::flight_prompt(unsigned direction) const {
    if(!moving() || !spatial_ || !round_.active)return false;
    const auto probe=spatial_->probe(*deployment_,*round_.active,direction);
    return probe.terrain==0 && !probe.occupied_slot;
}
bool CombatSession::begin_flight(unsigned direction) {
    if(!flight_prompt(direction))return false;
    deployment_->actors[*active_index()].extension[9]=static_cast<std::uint8_t>(direction);
    strikes_.clear();standalone_travel_.reset();action_message_.clear();++revision_;return true;
}
void CombatSession::flee(unsigned direction,const ItemCatalog& catalog,std::mt19937& random) {
    if(!flight_prompt(direction) || !movement_ || !round_.active || round_.phase!=CombatRoundPhase::player)
        throw DecodeError("Flight requires a current player Move group");
    auto next_random=random;
    auto result=movement_->stage_flight(*deployment_,*round_.active,round_.party_movement_bonus,next_random,
                                       &catalog,magic_?&magic_->catalog():nullptr);
    auto next_round=round_;CombatRoundService::resume_selection(result.state,next_round,catalog,next_random,&round_context_);
    result.notices.insert(result.notices.begin(),{*round_.active,result.notice_id});
    next_round.notices.insert(next_round.notices.begin(),result.notices.begin(),result.notices.end());
    *deployment_=std::move(result.state);round_=std::move(next_round);movement_checkpoint_.reset();
    opponent_turn_.reset();strikes_.clear();standalone_travel_.reset();action_message_.clear();random=std::move(next_random);++revision_;
}
bool CombatSession::can_attack(CombatActorIdentity target,bool confirmed_friendly) const {
    if(!attacks_ || !round_.active || round_.phase!=CombatRoundPhase::player || !runtime_error_.empty())return false;
    const auto index=active_index();if(!index || target==*round_.active)return false;
    const auto& selected=deployment_->actors[*index].character;
    return std::any_of(deployment_->actors.begin(),deployment_->actors.end(),[&](const auto& actor) {
        return !actor.removed && actor.id==target && actor.character.active() && actor.position &&
               (actor.character.byte(0x10e)!=selected.byte(0x10e) || confirmed_friendly || selected.byte(0x10f)!=0);
    });
}
void CombatSession::attack(CombatActorIdentity target,const ItemCatalog& catalog,const CombatAttackContext& context,
        std::mt19937& random,bool confirmed_friendly) {
    if(!can_attack(target,confirmed_friendly))throw DecodeError("Combat attack requires the current player and an opposing or confirmed allied target");
    auto state=*deployment_;const auto selected=*active_index();
    const auto victim=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& actor){return actor.id==target;});
    const bool same_side=victim->character.byte(0x10e)==state.actors[selected].character.byte(0x10e);
    const bool manual_hostility=same_side && state.actors[selected].character.byte(0x10f)==0;
    if(manual_hostility && !confirmed_friendly)throw DecodeError("An allied weapon target requires presented confirmation");
    if(manual_hostility) {
        //326D:2998 writes every linked status0 NPC, not only the clicked ally.
        //Side counts are derived from this retained roster by their consumers.
        for(auto& actor:state.actors)if(!actor.removed && actor.character.status()==0 && actor.character.byte(0x84)>127) {
            actor.character.set_byte(0x10e,1);actor.retained_target.reset();
            std::fill(actor.extension.begin()+10,actor.extension.begin()+14,std::uint8_t{});
        }
    }
    auto next_random=random;
    auto result=same_side?attacks_->stage_confirmed_friendly(state,*round_.active,target,catalog,context,next_random):
        attacks_->stage(state,*round_.active,target,catalog,context,next_random);
    auto next_round=round_;
    if(result.turn_finished)CombatRoundService::finish_turn(result.state,next_round,catalog,next_random,&round_context_);
    *deployment_=std::move(result.state);round_=std::move(next_round);strikes_=std::move(result.strikes);standalone_travel_=std::move(result.standalone_travel);
    hostility_started_=hostility_started_ || manual_hostility;
    movement_checkpoint_.reset();action_message_.clear();random=std::move(next_random);++revision_;
}
void CombatSession::step_opponent(const ItemCatalog& catalog,const BasicOpponentContext& context,std::mt19937& random) {
    if(!opponents_ || !attacks_ || !runtime_error_.empty() || moving() || round_.phase!=CombatRoundPhase::ai_pending || !round_.active)
        throw DecodeError("Opponent action requires the current source-selected AI actor");
    const auto progress=opponent_turn_.value_or(BasicOpponentState{*round_.active,false,false,{},8,0,0});
    if(progress.actor!=*round_.active)throw DecodeError("Opponent continuation belongs to another actor");
    auto next_random=random;
    auto next_context=round_context_;next_context.attacks=context.attacks;
    auto bound=context;bound.spells=magic_?&magic_->catalog():nullptr;bound.automatic_party_casting=automatic_magic_;
    auto result=opponents_->stage(*deployment_,progress,catalog,*attacks_,bound,next_random);
    if(result.casting || result.execute_queued_cast) {
        // Source000F resolves an immediate selection in the same transaction as
        // its policy/priority draws. A failed handler cannot publish a reroll.
        CombatSession staged(*this);staged.magic_playback_.reset();staged.strikes_.clear();staged.standalone_travel_.reset();
        CombatCastPending pending;
        if(result.casting) {
            pending=result.casting->pending;
            *staged.deployment_=std::move(result.casting->state);
        } else {
            *staged.deployment_=std::move(result.state);
            const auto found=std::find_if(staged.casts_.begin(),staged.casts_.end(),[&](const auto& cast){return cast.caster==*round_.active;});
            if(found==staged.casts_.end() || !CombatCastingService::still_pending(*staged.deployment_,*found))
                throw DecodeError("Automatic queued spell lost its owned casting continuation");
            pending=*found;
        }
        if(const auto reason=magic_->unsupported(pending.cast_index,pending.origin!=CombatCastOrigin::memorized))throw DecodeError(*reason);
        std::erase_if(staged.casts_,[&](const auto& cast){return cast.caster==pending.caster;});
        // AI000F always calls Quit after the executor, including item raw11=0.
        pending.ends_turn=true;staged.casts_.push_back(pending);
        if(result.casting && result.casting->resume_selection) {
            CombatRoundService::resume_selection(*staged.deployment_,staged.round_,catalog,next_random,&round_context_);
            staged.round_.notices.insert(staged.round_.notices.begin(),
                {pending.caster,pascal_string_id("GAME.OVR",0x14c00)});
            staged.opponent_turn_.reset();staged.action_message_.clear();
        } else {
            auto targets=opponents_->magic_targets(*staged.deployment_,pending,spell_catalog(),next_random);
            const auto selected=std::find_if(staged.deployment_->actors.begin(),staged.deployment_->actors.end(),
                [&](const auto& actor){return actor.id==pending.caster;});
            selected->retained_target=targets.retained_target;
            staged.round_.phase=CombatRoundPhase::casting_pending;
            if(targets.intent)staged.resolve_magic(targets.intent->targets,targets.intent->centre,catalog,
                context.dungeon_flag,next_random,context.attacks);
            else staged.cancel_magic(catalog,next_random);
        }
        staged.round_.notices.insert(staged.round_.notices.begin(),result.notices.begin(),result.notices.end());
        staged.prune_casts();staged.revision_=revision_+1;
        *this=std::move(staged);random=std::move(next_random);return;
    }
    if(result.turn_undead) {
        if(!turning_)throw DecodeError("Automatic Turn Undead service is unavailable");
        auto turned=turning_->stage(result.state,*round_.active,next_random);
        auto next_round=round_;
        CombatRoundService::resume_selection(turned.state,next_round,catalog,next_random,&round_context_);
        result.notices.push_back({*round_.active,pascal_string_id("GAME.OVR",0x1392b)});
        for(const auto& recipient:turned.recipients)result.notices.push_back({recipient.actor,
            pascal_string_id("GAME.OVR",recipient.outcome==TurnUndeadOutcome::turned?0x1393b:0x13945)});
        if(turned.failed)result.notices.push_back({*round_.active,pascal_string_id("GAME.OVR",0x13952),false});
        next_round.notices.insert(next_round.notices.begin(),result.notices.begin(),result.notices.end());
        *deployment_=std::move(turned.state);round_=std::move(next_round);opponent_turn_.reset();
        strikes_.clear();standalone_travel_.reset();magic_playback_.reset();action_message_.clear();random=std::move(next_random);++revision_;return;
    }
    auto next_round=round_;
    if(result.finished)CombatRoundService::finish_opponent_turn(result.state,next_round,catalog,next_random,&next_context);
    else next_round.notices.clear();
    next_round.notices.insert(next_round.notices.begin(),result.notices.begin(),result.notices.end());
    *deployment_=std::move(result.state);round_=std::move(next_round);strikes_=std::move(result.strikes);standalone_travel_=std::move(result.standalone_travel);
    if(result.finished)opponent_turn_.reset();else opponent_turn_=result.progress;
    monster_playback_=result.abilities.empty()?nullptr:
        std::make_shared<const std::vector<MonsterAbilityPlayback>>(std::move(result.abilities));
    round_context_=next_context;
    magic_playback_.reset();action_message_.clear();random=std::move(next_random);++revision_;
}
bool CombatSession::finish_movement(bool cancel) {
    if(!moving())return false;
    if(cancel) {
        // Native29CB:09DF restores only this Move group's scalar checkpoint.
        // Reaction damage, effects, counts, bodies, other actors and RNG survive.
        auto staged=std::make_unique<DeploymentState>(*deployment_);
        const auto found=std::find_if(staged->actors.begin(),staged->actors.end(),[&](const auto& actor) {
            return actor.id==movement_checkpoint_->actor && !actor.removed;
        });
        if(found==staged->actors.end() || !found->character.active())
            throw DecodeError("Move checkpoint no longer has an active actor");
        for(auto& cell:staged->occupancy)if(cell==found->native_slot)cell=0;
        found->position=movement_checkpoint_->position;found->extension[6]=movement_checkpoint_->budget;
        found->extension[9]=movement_checkpoint_->facing;
        for(const auto position:spatial_->footprint(*found)) {
            if(!Battlefield::in_bounds(position.x,position.y))throw DecodeError("Move checkpoint is outside battlefield");
            auto& cell=staged->occupancy[static_cast<std::size_t>(position.y*50+position.x)];
            if(cell)throw DecodeError("Move checkpoint is occupied by another actor");
            cell=found->native_slot;
        }
        const auto probe=spatial_->probe(*staged,found->id,8);
        if(probe.occupied_slot || !probe.terrain || probe.terrain_cost==0xff)
            throw DecodeError("Move checkpoint terrain does not permit reinsertion");
        deployment_=std::move(staged);
    } else if(const auto index=active_index();index && deployment_->actors[*index].extension[6]<2)
        deployment_->actors[*index].extension[6]=0;
    movement_checkpoint_.reset();strikes_.clear();standalone_travel_.reset();action_message_.clear();++revision_;return true;
}
void CombatSession::finish_turn(const ItemCatalog& catalog,std::mt19937& random) {
    if(moving() || !runtime_error_.empty())throw DecodeError("Finish Move before ending this combat turn");
    CombatRoundService::finish_turn(*deployment_,round_,catalog,random,&round_context_);strikes_.clear();standalone_travel_.reset();action_message_.clear();++revision_;
}
GameClock CombatSession::finish_round(const GameClock& clock,const ItemCatalog& catalog,std::mt19937& random) {
    if(moving() || !runtime_error_.empty() || !spatial_ || round_end_)
        throw DecodeError("Combat round lifecycle is unavailable or already committed");
    auto next_random=random;
    auto next_item=next_item_id_;
    auto result=CombatLifecycleService::end_round(*deployment_,round_,clock,*spatial_,
        {opponent_hp_percent_,false,&catalog,&spell_catalog(),&next_random,&next_item});
    if(result.disposition==CombatRoundEndDisposition::next_round) {
        CombatRoundService::begin_round(*result.deployment,result.round,catalog,next_random,&round_context_);
        result.round.notices.insert(result.round.notices.begin(),result.notices.begin(),result.notices.end());
    }
    deployment_=std::move(result.deployment);round_=std::move(result.round);
    next_item_id_=next_item;
    opponent_hp_percent_=result.opponent_hp_percent;
    if(result.disposition!=CombatRoundEndDisposition::next_round)round_end_=result.disposition;
    strikes_.clear();standalone_travel_.reset();action_message_.clear();random=std::move(next_random);++revision_;return result.clock;
}
void CombatSession::continue_battle(bool yes,const ItemCatalog& catalog,std::mt19937& random) {
    if(!continuation_pending())throw DecodeError("Combat has no pending continuation question");
    round_.notices.clear();
    if(yes) {
        round_.phase=CombatRoundPhase::not_started;
        CombatRoundService::begin_round(*deployment_,round_,catalog,random,&round_context_);round_end_.reset();
    } else round_end_=CombatRoundEndDisposition::outcome_pending;
    strikes_.clear();standalone_travel_.reset();action_message_.clear();++revision_;
}
Party CombatSession::inspection_party(const Party& resources) const {
    if(!deployment_ || !active_index())throw DecodeError("Combat View requires an active tactical actor");
    Party result=resources;result.members_.clear();result.selected_.reset();
    for(const auto& actor:deployment_->actors)if(!actor.removed) {
        if(actor.id==*round_.active)result.selected_=result.members_.size();
        result.members_.push_back(actor.character);
    }
    return result;
}
void CombatSession::prepare_spatial(const IFileSystem& files,const Party& party,const GeoMap* map,
        std::span<const std::uint8_t> city_cells,std::uint8_t mode,int x,int y,std::uint16_t region_flag,
        const DeploymentParameters& parameters,std::mt19937& random) {
    if(deployment_ || entry_.kind!=CombatEntryKind::launch || parameters.persistent_party_count!=party.size())
        throw DecodeError("Combat preparation requires the authoritative persistent roster count");
    auto staged=random;const auto tables=BattlefieldTables::load(files);
    Battlefield field;
    if(mode==1) {
        if(!map)throw DecodeError("Interior combat requires source geometry");
        field=BattlefieldService::build_interior(tables,*map,x,y,entry_.script_record,staged);
    } else field=build_noninterior_battlefield(tables,city_cells,mode,static_cast<std::uint8_t>(x),
                     static_cast<std::uint8_t>(y),region_flag,entry_.script_record,staged);
    std::vector<DeploymentActor> actors;
    for(std::size_t i=0;i<party.size();++i) {
        if(encounter_.special && encounter_.special->tactical_party.size()!=party.size())
            throw DecodeError("Special combat lost its persistent tactical roster");
        DeploymentActor actor;actor.id={CombatActorOrigin::party,i};
        actor.character=encounter_.special?encounter_.special->tactical_party[i]:party.member(i);
        actors.push_back(std::move(actor));
    }
    for(const auto& group:encounter_.groups)for(const auto& creature:group.creatures) {
        DeploymentActor actor;actor.id={CombatActorOrigin::encounter,creature.id.value};actor.character=creature.character;
        actors.push_back(std::move(actor));
    }
    auto deployed=std::make_unique<DeploymentState>(DeploymentService::build(files,field,std::move(actors),parameters));
    auto next_item=party.next_item_id_;
    for(const auto& actor:deployed->actors)for(std::size_t i=0;i<actor.character.inventory().size();++i) {
        const auto id=actor.character.inventory().item(i).id();
        if(id==std::numeric_limits<ItemId>::max())throw DecodeError("Combat generated item identities are exhausted");
        next_item=std::max(next_item,id+1);
    }
    next_item_id_=next_item;
    deployment_=std::move(deployed);random=staged;preparation_error_.clear();
}
}
