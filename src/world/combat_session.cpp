#include "gbengine/world/exploration_session.hpp"
#include "gbengine/combat/aftermath_service.hpp"
#include <algorithm>
#include <limits>

namespace GBEngine {
// In-memory memento of an exploration or committed source-return boundary. VM bindings still point
// to the live session's WorldState, whose address never changes. No disk codec,
// source reads, gameplay replay or pointers into moved party vectors are used.
struct ExplorationCheckpoint {
    JournalTracker journal;
    WorldState world;
    ExplorationAssets assets;
    EclVm vm;
    std::uint8_t record;
    std::uint16_t saved_search;
    bool area;
    std::uint64_t overland_phase;
    std::shared_ptr<const Framebuffer> link_backdrop;
    bool link_backdrop_overland;
    std::optional<PicturePresentation> picture;
    std::optional<ExplorationSpriteSelection> sprite_selection;
    std::optional<ExplorationSpritePresentation> sprite_cache,sprite;
    bool sprite_active,sprite_closeup_shown;
    std::optional<std::uint8_t> sprite_closeup_selector;
    std::optional<CityAssets> city;
    std::optional<GameFont> font;
    std::optional<SpellCatalog> spells;
    std::string glyphs,display,boundary;
    std::vector<std::string> messages;
    CampaignResources campaign;
    RestCampState rest_camp;
    std::optional<ExplorationSession::NestedCampFrame> nested_camp;
    bool nested_camp_return_pending;
    std::optional<MoveResult> last_move;
    std::optional<DamageOutcome> last_damage;
    ExplorationPhase resume_phase;
    bool resume_arriving,resume_link_pending,resume_link_loaded,resume_colour_done;
    explicit ExplorationCheckpoint(const ExplorationSession& s,bool continuation=false)
        :journal(s.journal_),world(s.world),assets(s.assets),vm(s.vm),record(s.record_),saved_search(s.saved_search_),area(s.area_view_),overland_phase(s.overland_phase_ms_),link_backdrop(s.link_backdrop_),link_backdrop_overland(s.link_backdrop_overland_),
         picture(s.picture_),sprite_selection(s.sprite_selection_),sprite_cache(s.sprite_cache_),sprite(s.sprite_),
         sprite_active(s.sprite_active_),sprite_closeup_shown(s.sprite_closeup_shown_),sprite_closeup_selector(s.sprite_closeup_selector_),
         city(s.city_),font(s.font_),spells(s.spell_catalog_),glyphs(s.glyph_text_),display(s.display_text),boundary(s.boundary),
         messages(s.messages),campaign(s.campaign_),rest_camp(s.rest_camp_),nested_camp(s.nested_camp_),nested_camp_return_pending(s.nested_camp_return_pending_),
         last_move(s.last_move),last_damage(s.last_damage),resume_phase(continuation?s.phase_:ExplorationPhase::idle),
         resume_arriving(continuation && s.arriving_),resume_link_pending(continuation && s.link_scene_pending_),
         resume_link_loaded(continuation && s.link_destination_loaded_),resume_colour_done(!continuation || s.overland_entry_colour_done_) {}
    void restore(ExplorationSession& s) && {
        s.journal_=std::move(journal);s.world=std::move(world);s.assets=std::move(assets);s.vm=std::move(vm);
        s.record_=record;s.saved_search_=saved_search;s.area_view_=area;
        s.overland_phase_ms_=overland_phase;s.overland_presented_=false;
        s.link_backdrop_=std::move(link_backdrop);s.link_backdrop_overland_=link_backdrop_overland;
        s.link_scene_pending_=resume_link_pending;s.link_destination_loaded_=resume_link_loaded;
        s.picture_=std::move(picture);s.sprite_selection_=std::move(sprite_selection);
        s.sprite_cache_=std::move(sprite_cache);s.sprite_=std::move(sprite);
        s.sprite_active_=sprite_active;s.sprite_closeup_shown_=sprite_closeup_shown;
        s.sprite_closeup_selector_=sprite_closeup_selector;s.city_=std::move(city);s.font_=std::move(font);
        s.spell_catalog_=std::move(spells);s.glyph_text_=std::move(glyphs);s.display_text=std::move(display);
        s.boundary=std::move(boundary);s.messages=std::move(messages);s.campaign_=std::move(campaign);
        s.rest_camp_=rest_camp;s.nested_camp_=std::move(nested_camp);s.nested_camp_return_pending_=nested_camp_return_pending;
        s.last_move=last_move;s.last_damage=std::move(last_damage);
        // These are proven absent at capture. Nothing from the abandoned
        // invocation/modal/rest activity may survive into restored exploration.
        s.arriving_=resume_arriving;s.phase_=resume_phase;s.party_defeated_=false;s.defeat_acknowledged_=false;
        s.menu_token_=0;s.menu_selection_=0;s.character_token_=0;s.character_selection_=0;
        s.picture_token_.reset();s.pending_sprite_distance_.reset();s.action_time_.reset();
        s.script_sounds_.clear();
        s.command_notice_.reset();s.command_notice_presented_=false;s.command_notice_elapsed_=0;
        s.overland_colour_.reset();s.overland_entry_colour_done_=resume_colour_done;
        s.effect_notices_.clear();s.effect_notice_index_=0;s.effect_notice_presented_=false;s.effect_notice_elapsed_=0;
        s.input_token_=0;s.input_buffer_.clear();s.campaign_token_.reset();s.campaign_kind_.reset();
        s.text_pages_.reset();s.acknowledge_dialogue();
        s.campaign_experience_.reset();s.campaign_npc_members_.clear();s.appraisal_.reset();
        s.campaign_result_text_id_.clear();s.campaign_experience_prefix_id_.clear();s.combat_collection_token_.reset();
        s.rest_={};s.camp_open_=false;s.camp_interrupted_=false;s.camp_encounter_pending_=false;
        s.rest_plan_revision_.reset();s.roster_drop_revision_.reset();s.roster_drop_member_.reset();
        s.combat_.reset();s.combat_checkpoint_.reset();
        s.encounter_dialogue_.reset();
        s.door_pending_=s.door_presented_=s.door_resolved_=s.door_success_=false;s.door_selection_=0;
    }
};
std::shared_ptr<const ExplorationCheckpoint> ExplorationSession::prepare_collection_checkpoint(
        const EclVm& next_vm,const CampaignResources& resources)const {
    // A completed battle's collection commits before its original ECL suffix.
    // That suffix may contain another battle without ever reaching idle. Stage
    // the replacement memento before publishing the collection exit, keeping
    // awards/transfers and the exact PC/flags/GOSUB/host phase together.
    if(!combat_collection_token_ || !combat_checkpoint_ || combat_ || party_defeated_ ||
       next_vm.status()!=EclStatus::ready || next_vm.pending_ || !next_vm.pending_followups_.empty() ||
       world.encounter.setup_prepared || world.encounter.duel_context || world.encounter.special || !world.encounter.groups.empty() ||
       action_time_ || picture_token_ || pending_sprite_distance_ || text_pages_ || command_notice_ || !effect_notices_.empty() ||
       encounter_dialogue_ || overland_colour_ || door_pending_ || camp_open_ || camp_interrupted_ || camp_encounter_pending_ ||
       rest_.running || rest_.awaiting_presentation || rest_plan_revision_ || roster_drop_revision_)
        throw DecodeError("Collection continuation requires a completed battle and closed transient services");
    auto next=std::make_shared<ExplorationCheckpoint>(*this,true);
    next->vm=next_vm;next->campaign=resources;next->display.clear();next->boundary.clear();
    return next;
}
bool ExplorationSession::retain_exploration_checkpoint() {
    const bool nested_encounter=nested_camp_ && nested_camp_->stage==NestedCampStage::encounter && activity_idle();
    if((!can_act() && !nested_encounter) || camp_open_ || action_time_ || arriving_ || picture_token_ || campaign_token_ || appraisal_ ||
       !vm.stack_.empty() || !vm.pending_followups_.empty() || vm.pending_) return false;
    // Preserve the rendered source scene while a typed destination load is
    // pending, including narrative destinations that have no geometry at all.
    try {
        auto backdrop=std::make_shared<Framebuffer>();backdrop->fill(0xff000000u);
        const auto overland=overland_view();draw_view(*backdrop);
        link_backdrop_=std::move(backdrop);link_backdrop_overland_=overland;
    } catch(const DecodeError&) {
        // A logical checkpoint must remain available for failed/unbound art
        // setups. Rendering itself still reports that dependency; no substitute
        // image is published merely to capture a gameplay rollback boundary.
        link_backdrop_.reset();link_backdrop_overland_=false;
    }
    auto next=std::make_shared<ExplorationCheckpoint>(*this);
    combat_checkpoint_=std::move(next);return true;
}
bool ExplorationSession::request_camp(std::size_t budget) {
    if(!can_act() || camp_open_ || nested_camp_)return false;
    if(!budget || budget>1000000)throw DecodeError("Invalid camp entry execution budget");
    if(!retain_exploration_checkpoint())return false;
    vm.start_entry(2);pump(budget);return true;
}
void ExplorationSession::retain_committed_overland_checkpoint() {
    if(!city_mode())return;
    // Coordinates and travel/Look time commit before entry1. F12 abandons
    // that event, not the travel. The copied VM is still halted, with no replay.
    if(vm.status()!=EclStatus::halted || vm.pending_ || !vm.stack_.empty() ||
       !vm.pending_followups_.empty() || action_time_ || combat_ ||
       world.encounter.setup_prepared || world.encounter.duel_context ||
       link_scene_pending_ || campaign_token_ || party_defeated_)
        throw DecodeError("Committed overland checkpoint requires a completed pre-event transaction");
    auto backdrop=std::make_shared<Framebuffer>();backdrop->fill(0xff000000u);draw_view(*backdrop);
    link_backdrop_=std::move(backdrop);link_backdrop_overland_=true;
    combat_checkpoint_=std::make_shared<ExplorationCheckpoint>(*this);
}
bool ExplorationSession::begin_combat() {
    if(combat_)return true;
    const auto& request=vm.pending_request();
    if(!request || request->kind!=EclRequestKind::campaign_service ||
       (!world.encounter.setup_prepared && !world.encounter.duel_context))return false;
    if(!combat_checkpoint_ || (world.encounter.duel_context && !world.encounter.special) || party_defeated_ || camp_open_ || campaign_token_ ||
       !world.party.size() || !active_character()) {
        boundary="Combat entry requires a retained exploration checkpoint and supported party context";return false;
    }
    if(combat_sequence_==std::numeric_limits<std::uint64_t>::max())throw DecodeError("Combat entry token exhausted");
    auto next_vm=vm;
    CombatSession battle({CombatToken{combat_sequence_+1},request->token,disk(),record_,request->address,world.party.size(),CombatEntryKind::launch,{}},world.encounter);
    next_vm.suspend_for_combat(request->token);
    try {
        DeploymentParameters parameters;
        parameters.native_direction=static_cast<std::uint8_t>(static_cast<unsigned>(world.facing)*2);
        parameters.persistent_party_count=vm.read_value(0x6e3e);
        parameters.duel_context=world.encounter.duel_context;
        parameters.identity_parameter=std::min<std::uint16_t>(vm.read_value(0x6dc6),100);
        parameters.world_x=world.x;parameters.world_y=world.y;
        parameters.presentation_mode=city_mode()?static_cast<std::uint8_t>(record_-23):1;
        if(!city_mode() && vm.read_value(0x49e6)!=1)throw DecodeError("Unresolved combat presentation context");
        std::uint8_t separation=2;
        if(!city_mode()) {
            if(!world.map)throw DecodeError("Combat requires source geometry");
            int x=world.x,y=world.y;separation=0;auto geometry=*world.map;geometry.id=record_;
            constexpr std::array dx{0,1,0,-1},dy{-1,0,1,0};
            const auto facing=static_cast<unsigned>(world.facing);
            if(facing>=4)throw DecodeError("Invalid combat exploration facing");
            while(separation<2 && geometry.wall(x,y,world.facing)==0) {++separation;x+=dx[facing];y+=dy[facing];}
            separation=static_cast<std::uint8_t>(std::min<std::uint16_t>(separation,vm.read_value(0x6dc1)));
            parameters.edge=[&](int ex,int ey,std::uint8_t dir) {
                return BattlefieldService::native_edge(*world.map,world.y,ex,ey,dir);
            };
        }
        parameters.separation=separation;
        const auto expected=next_vm.random_state();auto random=expected;
        const std::span<const std::uint8_t> cells=city_?std::span<const std::uint8_t>(city_->cells):std::span<const std::uint8_t>{};
        battle.prepare_spatial(files_,world.party,world.map?&*world.map:nullptr,cells,parameters.presentation_mode,
                               position_x(),position_y(),vm.read_value(0x4ab3),parameters,random);
        next_vm.commit_combat_preparation(request->token,expected,random,separation);
    }catch(const DecodeError& error) {battle.block_preparation(error.what());}
    combat_=std::move(battle);vm=std::move(next_vm);++combat_sequence_;
    boundary=combat_->deployment()?"Combat spatial preparation ready; rounds are not implemented":
                                  "Combat preparation blocked: "+combat_->preparation_error();return true;
}
bool ExplorationSession::block_combat_setup(std::string reason) {
    const auto& request=vm.pending_request();
    if(combat_ || !combat_checkpoint_ || !request || request->kind!=EclRequestKind::add_monsters ||
       !world.encounter.initialized || world.encounter.duel_context || party_defeated_ || !world.party.size())return false;
    if(combat_sequence_==std::numeric_limits<std::uint64_t>::max())throw DecodeError("Combat entry token exhausted");
    CombatSession blocked({CombatToken{combat_sequence_+1},request->token,disk(),record_,request->address,world.party.size(),
                           CombatEntryKind::setup_blocked,std::move(reason)},world.encounter);
    combat_=std::move(blocked);++combat_sequence_;return true;
}
bool ExplorationSession::abort_door() {
    if(!door_pending_ || !combat_checkpoint_)return false;
    auto restored=*combat_checkpoint_;const auto sequence=vm.request_sequence_;
    std::move(restored).restore(*this);vm.request_sequence_=std::max(vm.request_sequence_,sequence);return true;
}
bool ExplorationSession::abort_combat(CombatToken token) {
    if(!combat_checkpoint_)return false;
    const auto& request=vm.pending_request();
    const bool collection=combat_collection_token_ && *combat_collection_token_==token && campaign_token_ &&
        request && request->kind==EclRequestKind::campaign_service && request->token==*campaign_token_;
    if(!collection && (!combat_ || combat_->entry().token!=token))return false;
    const auto expected_kind=collection?EclRequestKind::campaign_service:
        combat_->entry().kind==CombatEntryKind::launch?EclRequestKind::combat:EclRequestKind::add_monsters;
    if(!request || request->kind!=expected_kind || (!collection && request->token!=combat_->entry().ecl_token))
        throw DecodeError("Combat continuation changed before development abort");
    // Allocate/copy everything before publishing. Preserve the request high-water
    // mark so an old modal/VM token can never match after a replayed encounter.
    const auto checkpoint=combat_checkpoint_;
    auto restored=*checkpoint;const auto sequence=vm.request_sequence_;
    std::move(restored).restore(*this);
    // A committed collection boundary resumes live source code. Its next
    // encounter must keep the same ownership through repeated F12 retries.
    if(vm.status()==EclStatus::ready)combat_checkpoint_=checkpoint;
    vm.request_sequence_=std::max(vm.request_sequence_,sequence);
    return true;
}
bool ExplorationSession::current_combat(CombatToken token,std::uint64_t revision) const {
    const auto* battle=combat();const auto& request=vm.pending_request();
    return battle && battle->deployment() && battle->entry().token==token && battle->revision()==revision &&
           request && request->kind==EclRequestKind::combat && request->token==battle->entry().ecl_token;
}
bool ExplorationSession::start_combat_runtime(CombatToken token,std::uint64_t revision) {
    if(!current_combat(token,revision) || combat_->runtime_attempted())return false;
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    try {
        const auto selected=world.party.selected_index();
        const auto entry_selected=selected && !world.encounter.special && !world.encounter.duel_context?
            std::optional<CombatActorIdentity>{{CombatActorOrigin::party,*selected}}:std::nullopt;
        next->start_runtime(files_,world.party.item_catalog(),vm.read_value(0x6dcb),
                            static_cast<std::uint8_t>(vm.read_value(0x6e72)),random,
                            {{static_cast<std::uint8_t>(vm.read_value(0x6e71)),
                              static_cast<std::uint8_t>(vm.read_value(0x6e70))}},entry_selected);
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,random,!next->outcome_pending());
    }catch(const DecodeError& error) {combat_->block_runtime(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::begin_combat_movement(CombatToken token,std::uint64_t revision) {
    return current_combat(token,revision) && combat_->begin_movement();
}
bool ExplorationSession::begin_combat_friendly_bump(CombatToken token,std::uint64_t revision,unsigned direction) {
    if(!current_combat(token,revision))return false;
    try {return combat_->begin_friendly_bump(direction);}
    catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
}
bool ExplorationSession::move_combat_actor(CombatToken token,std::uint64_t revision,unsigned direction) {
    if(!current_combat(token,revision))return false;
    auto next=std::make_unique<CombatSession>(*combat_);
    try {
        if(const auto target=next->bump_target(direction))return attack_combat_actor(token,revision,*target);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    auto next_vm=vm;const auto expected=vm.random_state();auto random=expected;
    try {
        const CombatAttackContext context{{static_cast<std::uint8_t>(vm.read_value(0x6e71)),
                                           static_cast<std::uint8_t>(vm.read_value(0x6e70))}};
        if(!next->move(direction,world.party.item_catalog(),context,random)) {
            combat_->report_action(next->action_message());return false;
        }
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,random);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::command_combat_actor(CombatToken token,std::uint64_t revision,TacticalCommand command) {
    if(!current_combat(token,revision))return false;
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    try {
        next->command(command,world.party.item_catalog(),random);
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,random);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::turn_combat_undead(CombatToken token,std::uint64_t revision) {
    if(!current_combat(token,revision))return false;
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    try {
        next->turn_undead(world.party.item_catalog(),random);
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,random);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::flee_combat_actor(CombatToken token,std::uint64_t revision,unsigned direction) {
    if(!current_combat(token,revision))return false;
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    try {
        next->flee(direction,world.party.item_catalog(),random);
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,random);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::begin_combat_flight(CombatToken token,std::uint64_t revision,unsigned direction) {
    return current_combat(token,revision) && combat_->begin_flight(direction);
}

bool ExplorationSession::attack_combat_actor(CombatToken token,std::uint64_t revision,CombatActorIdentity target,bool confirmed_friendly) {
    if(!current_combat(token,revision))return false;
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    try {
        const CombatAttackContext context{{static_cast<std::uint8_t>(vm.read_value(0x6e71)),
                                           static_cast<std::uint8_t>(vm.read_value(0x6e70))}};
        next->attack(target,world.party.item_catalog(),context,random,confirmed_friendly);
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,random,false,next->hostility_started());
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::toggle_combat_magic(CombatToken token,std::uint64_t revision) {
    if(!current_combat(token,revision))return false;
    try {combat_->toggle_automatic_magic();return true;}
    catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
}
bool ExplorationSession::step_combat_opponent(CombatToken token,std::uint64_t revision) {
    if(!current_combat(token,revision) || combat_->round().phase!=CombatRoundPhase::ai_pending)return false;
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    try {
        BasicOpponentContext context;
        context.attacks.side_attack_bonus={static_cast<std::uint8_t>(vm.read_value(0x6e71)),
                                           static_cast<std::uint8_t>(vm.read_value(0x6e70))};
        context.casualty_percentage=vm.read_value(0x6dc6);context.opponent_hp_percent=next->opponent_hp_percent();
        context.party_movement_bonus=next->round().party_movement_bonus;context.round_index=next->round().round_index;
        context.casting_blocked=vm.read_value(0x49e5)!=0;
        context.formation_facing=static_cast<std::uint8_t>(static_cast<unsigned>(world.facing)*2);
        context.dungeon_flag=vm.read_value(0x49e6);
        next->step_opponent(world.party.item_catalog(),context,random);
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,random);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::finish_combat_movement(CombatToken token,std::uint64_t revision,bool cancel) {
    if(!current_combat(token,revision))return false;
    try {return combat_->finish_movement(cancel);}
    catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
}
bool ExplorationSession::finish_combat_turn(CombatToken token,std::uint64_t revision) {
    if(!current_combat(token,revision) || combat_->round().phase!=CombatRoundPhase::player)return false;
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    try {
        next->finish_turn(world.party.item_catalog(),random);
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,random);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::finish_combat_round(CombatToken token,std::uint64_t revision) {
    if(!current_combat(token,revision) || combat_->round().phase!=CombatRoundPhase::round_end_pending ||
       combat_->continuation_pending() || combat_->outcome_pending())return false;
    const auto time=clock();
    if(!time) {combat_->report_action("Combat round end requires an initialized campaign clock");return false;}
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    try {
        const auto advanced=next->finish_round(*time,world.party.item_catalog(),random);
        next_vm.commit_combat_clock(next->entry().ecl_token,expected,random,advanced.words());
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::answer_combat_continue(CombatToken token,std::uint64_t revision,bool yes) {
    if(!current_combat(token,revision) || !combat_->continuation_pending())return false;
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    try {
        next->continue_battle(yes,world.party.item_catalog(),random);
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,random);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::finish_combat(CombatToken token,std::uint64_t revision) {
    if(!current_combat(token,revision) || !combat_->outcome_pending() || campaign_token_)return false;
    if(combat_->aftermath())return false;
    auto next_vm=vm;const auto expected=vm.random_state();
    try {
        const auto selected=combat_->round().effect_caster?combat_->round().effect_caster:
            world.encounter.special?std::optional<CombatActorIdentity>{{CombatActorOrigin::party,
                world.encounter.special->selected_party_index}}:std::nullopt;
        auto result=CombatAftermathService::stage(*combat_->deployment(),world.party,campaign_,expected,
                         {vm.read_value(0x6e3e),vm.read_value(0x6de3),world.encounter.duel_context,combat_->next_item_id(),
                          vm.read_value(0x6de6),selected});
        if(!result.collection_ready) {
            combat_->hold_aftermath(std::move(result),expected);return false;
        }
        next_vm.begin_combat_rewards(combat_->entry().ecl_token,expected,result.random,result.defeated_count,
            result.source_result,static_cast<std::uint16_t>(result.party.size()));
        auto encounter=world.encounter;encounter.duel_context=false;encounter.special.reset();encounter.clear();
        // The postbattle wrapper has already awarded XP and recovered the
        // roster. Opening collection must not run that transaction again.
        const auto request_token=combat_->entry().ecl_token;
        world.party=std::move(result.party);campaign_=std::move(result.resources);vm=std::move(next_vm);
        world.encounter=std::move(encounter);campaign_kind_=CampaignServiceKind::treasure;campaign_token_=request_token;
        campaign_experience_=result.collection.experience_per_member;campaign_npc_members_=std::move(result.collection.npc_members);
        campaign_result_text_id_=std::move(result.result_text_id);campaign_experience_prefix_id_=std::move(result.experience_prefix_id);
        combat_collection_token_=token;
        combat_.reset();refresh_world_art();display_text.clear();boundary="Postbattle treasure collection";
        return true;
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
}
bool ExplorationSession::present_combat_aftermath(CombatToken token,std::uint64_t revision) {
    if(!current_combat(token,revision) || !combat_->aftermath())return false;
    combat_->aftermath_presented();return true;
}
bool ExplorationSession::acknowledge_combat_defeat(CombatToken token,std::uint64_t revision) {
    if(!current_combat(token,revision) || !combat_->aftermath() || !combat_->aftermath()->presented)return false;
    const auto& pending=*combat_->aftermath();
    if(pending.result.collection_ready || pending.result.outcome!=CombatOutcome::defeat)return false;
    auto next_vm=vm;auto result=pending.result;
    try {
        if(result.source_result!=0x80 || result.party.size()!=0)
            throw DecodeError("True combat defeat must own the native empty persistent roster");
        next_vm.complete_combat_defeat(combat_->entry().ecl_token,pending.expected_random,result.random,result.defeated_count);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    auto encounter=world.encounter;encounter.duel_context=false;encounter.special.reset();encounter.clear();
    world.party=std::move(result.party);campaign_=std::move(result.resources);vm=std::move(next_vm);world.encounter=std::move(encounter);
    combat_.reset();combat_checkpoint_.reset();combat_collection_token_.reset();
    party_defeated_=true;defeat_acknowledged_=true;phase_=ExplorationPhase::idle;refresh_world_art();display_text.clear();
    boundary="Combat defeat: the original party has been removed";
    return true;
}
}
