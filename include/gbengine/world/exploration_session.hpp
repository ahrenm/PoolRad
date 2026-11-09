#pragma once
#include "gbengine/world/world_state.hpp"
#include "gbengine/qol/journal.hpp"
#include "gbengine/rendering/exploration_renderer.hpp"
#include "gbengine/rendering/city_assets.hpp"
#include "gbengine/rendering/presentation.hpp"
#include "gbengine/rendering/exploration_sprite.hpp"
#include "gbengine/assets/records.hpp"
#include "gbengine/saves/original_save.hpp"
#include "gbengine/time/game_time_service.hpp"
#include "gbengine/magic/spell_service.hpp"
#include "gbengine/campaign/shop_inventory.hpp"
#include "gbengine/campaign/shrine_service.hpp"
#include "gbengine/time/rest_service.hpp"
#include "gbengine/party/roster_service.hpp"
#include "gbengine/combat/combat_session.hpp"
namespace GBEngine {
namespace Debug {class Access;} 
class PortSave;
struct ExplorationCheckpoint;
struct ExplorationSpriteSelection {std::uint8_t record{},body{};};
struct ExplorationCommandNotice {
    std::string text_id;
    std::uint8_t color=14;
};
struct OverlandColourPresentation {
    std::vector<Framebuffer> frames;
    std::size_t frame=0;
    bool presented=false;
    std::uint64_t elapsed=0;
};
struct EncounterInteraction {
    std::uint64_t token{};
    EncounterPace pace;
    EncounterDialogueConfig config;
    std::uint8_t distance{};
    EclMenu menu;
    bool ready=false,notice_presented=false,result_submitted=false;
    std::uint64_t notice_elapsed{};
    std::optional<EncounterDialogueStep> step;
    std::string notice;
};
// Services only the recovered exploration subset. Unsupported requests remain pending.
// Owns its bindings; deliberately noncopyable so EclVm cannot retain a stale pointer.
class ExplorationSession : public IItemActivationService,public IShopInventory {
    friend class Debug::Access;
    friend class PortSave;
    friend struct ExplorationCheckpoint;
    const IFileSystem& files_;
    std::uint8_t record_;
    bool arriving_=false;
    ExplorationPhase phase_=ExplorationPhase::initializing;
    std::uint16_t saved_search_{};
    std::uint64_t menu_token_{};
    std::size_t menu_selection_{};
    std::uint64_t character_token_{};
    std::size_t character_selection_{};
    std::optional<PicturePresentation> picture_;
    std::optional<std::uint64_t> picture_token_;
    std::optional<ExplorationSpriteSelection> sprite_selection_;
    std::optional<ExplorationSpritePresentation> sprite_cache_,sprite_;
    bool sprite_active_=false,sprite_closeup_shown_=false;
    std::optional<std::uint8_t> sprite_closeup_selector_;
    std::optional<std::uint8_t> pending_sprite_distance_;
    std::optional<CityAssets> city_;
    std::uint64_t overland_phase_ms_=0;
    bool overland_presented_=false;
    bool overland_entry_colour_done_=false;
    std::optional<OverlandColourPresentation> overland_colour_;
    std::shared_ptr<const Framebuffer> link_backdrop_;
    bool link_scene_pending_=false,link_destination_loaded_=false,link_backdrop_overland_=false;
    bool party_defeated_=false;
    bool defeat_acknowledged_=false;
    bool area_view_=false;
    bool campaign_time_bound_=false;
    JournalTracker journal_;
    bool journal_supported_=false;
    std::shared_ptr<const JournalRules> journal_rules_;
    std::filesystem::path journal_directory_;
    std::string journal_rules_error_;
    bool qol_upgrades_=false; // Launch policy, neither save nor checkpoint state.
    IEffectExpiryResolver* expiry_resolver_=nullptr; // Borrowed; owner must outlive the session.
    std::optional<TimeIncrement> action_time_;
    bool door_pending_=false,door_presented_=false,door_resolved_=false,door_success_=false;
    std::size_t door_selection_=0;
    std::vector<std::pair<std::uint64_t,std::uint8_t>> script_sounds_;
    std::optional<ExplorationCommandNotice> command_notice_;
    bool command_notice_presented_=false;
    std::uint64_t command_notice_elapsed_=0;
    std::vector<EffectNotice> effect_notices_;
    std::size_t effect_notice_index_=0;
    bool effect_notice_presented_=false;
    std::uint64_t effect_notice_elapsed_=0;
    std::optional<SpellCatalog> spell_catalog_;
    std::uint64_t input_token_{};
    std::string input_buffer_;
    std::optional<GameFont> font_;
    std::string glyph_text_;
    struct TextPages {
        std::uint64_t token{};
        std::vector<std::string> pages;
        std::size_t page=0;
        bool presented=false;
        bool replacement=false; // Hold old text before consuming the next PRINTCLEAR.
    };
    std::optional<TextPages> text_pages_;
    std::uint64_t dialogue_revision_=0; // Transient presentation identity, never saved.
    std::uint64_t acknowledged_dialogue_revision_=0;
    bool pause_text_replacement_=false; // Interactive host policy, never saved.
    bool stage_text(const EclRequest&);
    CampaignResources campaign_;
    std::optional<std::uint64_t> campaign_token_;
    std::optional<CampaignServiceKind> campaign_kind_;
    std::optional<std::uint32_t> campaign_experience_;
    std::string campaign_result_text_id_,campaign_experience_prefix_id_;
    std::vector<std::size_t> campaign_npc_members_;
    std::optional<AppraisalQuote> appraisal_;
    RestCampState rest_camp_;
    RestState rest_;
    bool camp_open_=false,camp_interrupted_=false,camp_encounter_pending_=false;
    enum class NestedCampStage { entering,ready,camping,encounter };
    struct NestedCampFrame {
        EclVm outer;
        ExplorationPhase phase;
        NestedCampStage stage=NestedCampStage::entering;
    };
    std::optional<NestedCampFrame> nested_camp_;
    bool nested_camp_return_pending_=false;
    void complete_nested_camp();
    std::optional<std::uint64_t> rest_plan_revision_;
    std::optional<std::uint64_t> roster_drop_revision_;
    std::optional<std::size_t> roster_drop_member_;
    std::optional<CombatSession> combat_;
    std::optional<EncounterInteraction> encounter_dialogue_;
    std::shared_ptr<const ExplorationCheckpoint> combat_checkpoint_;
    std::uint64_t combat_sequence_=0;
    std::optional<CombatToken> combat_collection_token_;
    bool activity_idle() const { return !text_pages_ && !overland_colour_ && !command_notice_ && effect_notices_.empty() && !party_defeated_ && !rest_.running && !rest_.awaiting_presentation &&
        !combat_ && !encounter_dialogue_ && !world.encounter.setup_prepared && !world.encounter.duel_context && !link_scene_pending_ &&
        !camp_interrupted_ && !camp_encounter_pending_ && phase_==ExplorationPhase::idle && vm.status()==EclStatus::halted && (world.map.has_value() || city_mode()); }
    bool advance_time(TimeIncrement,std::optional<std::uint64_t> token={});
    void retain_committed_overland_checkpoint();
    std::shared_ptr<const ExplorationCheckpoint> prepare_collection_checkpoint(const EclVm&,const CampaignResources&) const;
    void prepare_overland_colour();
    void refresh_world_art();
    bool stage_sprite(const EclRequest&,bool encounter_context=false);
    void check_party_defeat();
    bool block_combat_setup(std::string);
    bool current_combat(CombatToken,std::uint64_t revision) const;
    bool stage_encounter_dialogue(const EclRequest&);
    void refresh_encounter_menu();
    bool choose_encounter_menu(std::uint64_t,std::size_t,std::size_t);
    void complete_encounter_step(std::size_t);
    MagicContext magic_context() const {return {MagicDomain::exploration,vm.read_value(0x49e5)!=0};}
public:
    WorldState world;
    ExplorationAssets assets;
    EclVm vm;
    std::vector<std::string> messages;
    std::string boundary;
    std::string display_text;
    std::optional<MoveResult> last_move;
    std::optional<DamageOutcome> last_damage;
    ExplorationSession(const IFileSystem&,unsigned disk,std::uint8_t record,IEffectExpiryResolver* =nullptr,
                       std::optional<GameClock> initial_clock={});
    ExplorationSession(const IFileSystem&,const OriginalSave&,IEffectExpiryResolver* =nullptr);
    ExplorationSession(const IFileSystem&,const PortSave&,IEffectExpiryResolver* =nullptr);
    // Genuine fresh campaign: native zero banks, Phlan15,1 west, created party.
    ExplorationSession(const IFileSystem&,Party,std::mt19937);
    ExplorationSession(const ExplorationSession&)=delete;
    ExplorationSession& operator=(const ExplorationSession&)=delete;
    void set_qol_upgrades(bool enabled,const std::filesystem::path& journal_directory="data/journal");
    bool qol_upgrades()const{return qol_upgrades_;}
    const JournalTracker& journal()const{return journal_;}
    void prepare_journal();
    void complete_text_request(std::uint64_t);
    EclStatus pump(std::size_t budget=10000);
    bool can_act() const { return !roster_drop_revision_ &&
        (!nested_camp_ || nested_camp_->stage==NestedCampStage::camping) && activity_idle(); }
    bool retain_exploration_checkpoint();
    bool door_pending() const {return door_pending_;}
    std::vector<char> door_choices() const;
    std::size_t door_selection() const {return door_selection_;}
    void door_presented() {if(door_pending_)door_presented_=true;}
    void hold_door() {door_presented_=false;}
    bool door_input(InputEvent);
    bool abort_door();
    bool character_services_pending() const;
    bool ending_pending() const;
    void finish_ending(std::uint64_t token,std::uint64_t expected_revision,const std::mt19937& expected_random);
    void finish_character_services(std::uint64_t token,std::uint64_t expected_revision,Party,std::mt19937);
    bool request_camp(std::size_t budget=10000);
    bool stage_nested_camp();
    bool nested_camp_active() const {return nested_camp_.has_value();}
    bool nested_camp_ready() const {return nested_camp_ && nested_camp_->stage==NestedCampStage::ready && activity_idle();}
    // Called before ordinary halted-phase processing. True consumes the child
    // entry2 halt; false permits processing a completed outer terminal service.
    bool update_nested_camp();
    bool begin_combat();
    const CombatSession* combat() const {return combat_?&*combat_:nullptr;}
    const EncounterState& encounter_setup() const {return world.encounter;}
    bool abort_combat(CombatToken);
    bool start_combat_runtime(CombatToken,std::uint64_t revision);
    bool begin_combat_movement(CombatToken,std::uint64_t revision);
    bool begin_combat_friendly_bump(CombatToken,std::uint64_t revision,unsigned direction);
    bool move_combat_actor(CombatToken,std::uint64_t revision,unsigned direction);
    bool attack_combat_actor(CombatToken,std::uint64_t revision,CombatActorIdentity target,bool confirmed_friendly=false);
    bool schedule_combat_spell(CombatToken,std::uint64_t revision,std::size_t slot);
    bool schedule_combat_item(CombatToken,std::uint64_t revision,ItemId,std::optional<std::size_t> scroll_slot={});
    bool read_combat_scroll(CombatToken,std::uint64_t revision,ItemId);
    bool ready_combat_item(CombatToken,std::uint64_t revision,ItemId);
    bool resolve_combat_magic(CombatToken,std::uint64_t revision,std::span<const CombatActorIdentity>,std::optional<CombatPosition> centre={});
    bool cancel_combat_magic(CombatToken,std::uint64_t revision);
    bool command_combat_actor(CombatToken,std::uint64_t revision,TacticalCommand);
    bool turn_combat_undead(CombatToken,std::uint64_t revision);
    bool flee_combat_actor(CombatToken,std::uint64_t revision,unsigned direction);
    bool begin_combat_flight(CombatToken,std::uint64_t revision,unsigned direction);
    bool step_combat_opponent(CombatToken,std::uint64_t revision);
    bool toggle_combat_magic(CombatToken,std::uint64_t revision);
    bool finish_combat_movement(CombatToken,std::uint64_t revision,bool cancel);
    bool finish_combat_turn(CombatToken,std::uint64_t revision);
    bool finish_combat_round(CombatToken,std::uint64_t revision);
    bool answer_combat_continue(CombatToken,std::uint64_t revision,bool yes);
    bool finish_combat(CombatToken,std::uint64_t revision);
    bool present_combat_aftermath(CombatToken,std::uint64_t revision);
    bool acknowledge_combat_defeat(CombatToken,std::uint64_t revision);
    const auto& combat_collection_token() const {return combat_collection_token_;}
    const std::string* encounter_notice() const {return encounter_dialogue_ && !encounter_dialogue_->notice.empty()?&encounter_dialogue_->notice:nullptr;}
    void encounter_notice_presented() {if(encounter_notice())encounter_dialogue_->notice_presented=true;}
    void advance_encounter_notice(std::uint64_t);
    bool party_defeated() const { return party_defeated_; }
    bool defeat_acknowledged()const{return defeat_acknowledged_;}
    std::optional<GameClock> clock() const;
    bool time_blocked() const {return action_time_.has_value();}
    const ExplorationCommandNotice* command_notice() const {return command_notice_?&*command_notice_:nullptr;}
    void command_notice_presented() {if(command_notice_)command_notice_presented_=true;}
    void hold_command_notice() {command_notice_presented_=false;}
    void advance_command_notice(std::uint64_t milliseconds);
    bool presenting_overland_colour() const {return overland_colour_.has_value();}
    void overland_colour_presented() {if(overland_colour_)overland_colour_->presented=true;}
    void hold_overland_colour() {if(overland_colour_)overland_colour_->presented=false;}
    void advance_overland_colour(std::uint64_t milliseconds);
    const EffectNotice* effect_notice() const {return effect_notices_.empty()?nullptr:&effect_notices_.at(effect_notice_index_);}
    void effect_notice_presented() {if(effect_notice())effect_notice_presented_=true;}
    void advance_effect_notice(std::uint64_t milliseconds);
    const SpellCatalog& spell_catalog();
    const IFileSystem& asset_files() const {return files_;}
    std::optional<std::pair<std::uint64_t,std::uint8_t>> take_script_sound() {
        if(script_sounds_.empty())return {};
        const auto result=script_sounds_.front();script_sounds_.erase(script_sounds_.begin());return result;
    }
    std::uint8_t game_speed() const {return static_cast<std::uint8_t>(vm.read_value(0x49fc));}
    std::uint64_t script_delay_ms() const {return std::uint64_t{game_speed()}*225;}
    bool set_game_speed(std::uint8_t speed) {
        if(speed>9 || !camp_open_ || !can_act())return false;
        vm.seed_value(0x49fc,static_cast<std::uint16_t>((vm.read_value(0x49fc)&0xff00)|speed));return true;
    }
    bool character_portraits_enabled() const {return !campaign_time_bound_ || (vm.read_value(0x49ff)&1)!=0;}
    bool monster_pictures_enabled() const {return static_cast<std::uint8_t>(vm.read_value(0x49ff)>>1)!=0;}
    bool set_monster_pictures_enabled(bool enabled) {
        if(!campaign_time_bound_ || !can_act()) return false;
        vm.set_monsters_enabled(enabled);return true;
    }
    bool set_character_portraits_enabled(bool enabled) {
        if(!campaign_time_bound_ || !can_act()) return false;
        vm.set_portraits_enabled(enabled);return true;
    }
    std::vector<MemorizedSpell> castable_spells();
    SpellCastPlan prepare_spell(std::size_t slot,MagicTarget);
    MagicResult cast_spell(const SpellCastPlan&);
    ItemActionResult activate_item(const ItemEffectRequest&) override;
    const EffectNotice* activation_notice() const override {return effect_notice();}
    void activation_notice_presented() override {effect_notice_presented();}
    void advance_activation_notice(std::uint64_t elapsed) override {advance_effect_notice(elapsed);}
    std::optional<std::uint8_t> noncombat_item_target_mode(const ItemEffectRequest&) override;
    ItemActionResult ready_item(std::size_t,ItemId) override;
    std::optional<std::size_t> active_character() const {return world.party.selected_index();}
    bool cycle_character(int delta);
    bool toggle_area_view();
    bool area_view() const {return area_view_ && !city_mode() && vm.read_value(0x49fb)==0;}
    bool toggle_search();
    ExplorationPhase phase() const { return phase_; }
    unsigned disk() const { return world.resource_disk; }
    std::uint8_t record() const { return record_; }
    bool city_mode() const { return city_.has_value() && record_>=25 && record_<=27 && vm.read_value(0x49e6)==0; }
    // Visible source scene can outlive its logical mode during a held transfer.
    bool overland_view() const {
        if(overland_colour_)return true;
        return link_backdrop_ && (link_scene_pending_ || (!world.map && !city_mode()))?link_backdrop_overland_:city_mode();
    }
    std::uint8_t overland_tile(int x,int y) const {
        if(!city_mode())throw DecodeError("Overland tile query requires an active overland map");
        return city_->tile(x,y,record_-23);
    }
    int position_x() const { return city_mode()?vm.read_value(0x49c3):world.x; }
    int position_y() const { return city_mode()?vm.read_value(0x49c4):world.y; }
    bool city_input(InputEvent,std::size_t budget=10000);
    void advance_overland_animation(std::uint64_t);
    void hold_overland_animation() {overland_presented_=false;}
    void overland_presented() {if(city_mode() && can_act() && !picture_ && !showing_glyphs())overland_presented_=true;}
    bool forward(std::size_t budget=10000);
    bool turn(int quarters);
    bool look(std::size_t budget=10000);
    bool continue_dialogue(std::size_t budget=10000);
    const EclMenu* menu() const {
        const auto& r=vm.pending_request();
        if(encounter_dialogue_)return encounter_dialogue_->ready?&encounter_dialogue_->menu:nullptr;
        return r && r->kind==EclRequestKind::menu && r->menu ? &*r->menu : nullptr;
    }
    std::size_t menu_selection() const { return menu_selection_; }
    bool choose_menu(std::uint64_t token,std::size_t selection,std::size_t budget=10000);
    bool menu_input(InputEvent,std::size_t budget=10000);
    bool entering_input() const {return vm.pending_request() && (vm.pending_request()->kind==EclRequestKind::input_number || vm.pending_request()->kind==EclRequestKind::input_string);}
    bool text_page_pending() const {return text_pages_.has_value();}
    // Interactive hosts keep the final page until Enter; headless source drivers
    // may retain completion on actual presentation.
    void text_page_presented(bool wait_for_final_input=false);
    std::uint64_t dialogue_revision() const {return dialogue_revision_;}
    void acknowledge_dialogue() {acknowledged_dialogue_revision_=dialogue_revision_;}
    void pause_text_replacement(bool enabled) {pause_text_replacement_=enabled;acknowledge_dialogue();}
    bool text_page_has_more() const {return text_pages_ && text_pages_->page+1<text_pages_->pages.size();}
    void hold_text_page() {if(text_pages_)text_pages_->presented=false;}
    bool text_page_input(InputEvent,std::size_t budget=10000);
    const std::string& input_buffer() const {return input_buffer_;}
    bool entry_input(InputEvent,std::size_t budget=10000);
    bool selecting_character() const { return vm.pending_request() && vm.pending_request()->kind==EclRequestKind::select_character; }
    std::size_t character_selection() const { return character_selection_; }
    bool choose_character(std::uint64_t token,std::size_t index,std::size_t budget=10000);
    bool character_input(InputEvent,std::size_t budget=10000);
    const auto& picture() const { return picture_; }
    const auto& picture_token() const { return picture_token_; }
    const auto& sprite() const {return sprite_;}
    void draw_view(Framebuffer& frame) const;
    bool showing_glyphs() const;
    const std::string& glyph_text() const {return glyph_text_;}
    void advance_picture(std::uint64_t milliseconds) { if(picture_ && !picture_token_) picture_->advance(milliseconds); }
    bool picture_presented(std::uint64_t token,std::size_t budget=10000);
    bool campaign_requested() const;
    bool begin_campaign();
    bool finish_campaign(std::uint64_t token,std::size_t budget=10000);
    const CampaignResources& campaign_resources() const {return campaign_;}
    CampaignResources* money_pool_for_view() {return campaign_token_ && !appraisal_?&campaign_:nullptr;}
    const auto& campaign_kind() const {return campaign_kind_;}
    const auto& campaign_token() const {return campaign_token_;}
    const auto& campaign_experience() const {return campaign_experience_;}
    const auto& campaign_result_text_id() const {return campaign_result_text_id_;}
    const auto& campaign_experience_prefix_id() const {return campaign_experience_prefix_id_;}
    const auto& campaign_npc_members() const {return campaign_npc_members_;}
    ShrineInspection shrine_inspection(std::size_t,ShrineAction) const;
    EconomyResult apply_shrine(std::size_t,ShrineAction,bool confirmed_unneeded=false,bool confirmed_fee=false);
    EconomyResult pool_money();
    EconomyResult share_money();
    EconomyResult take_money(std::size_t,std::uint16_t);
    EconomyResult take_campaign_item(CampaignItemId);
    MagicResult detect_treasure();
    EconomyResult buy_item(CampaignItemId);
    EconomyResult sell_item(std::size_t,ItemId,bool confirmed=false) override;
    EconomyResult identify_item(std::size_t,ItemId,bool confirmed=false) override;
    const auto& appraisal_quote() const {return appraisal_;}
    EconomyResult appraise_gem();
    EconomyResult appraise_jewelry();
    EconomyResult resolve_appraisal(bool keep);
    bool select_campaign_member(int delta);
    bool begin_camp();
    bool end_camp();
    bool camp_open() const {return camp_open_;}
    bool camp_interrupted() const {return camp_interrupted_;}
    RosterResult move_camp_member(RosterDirection);
    bool begin_camp_drop();
    void cancel_camp_drop();
    RosterResult drop_camp_member(bool confirmed);
    void resume_after_camp(std::size_t budget=10000);
    CampWorkChoices memorize_choices(std::size_t);
    CampScribeChoices scribe_choices(std::size_t);
    CampWorkResult schedule_memorize(std::size_t,SpellId);
    CampWorkResult clear_memorize(std::size_t);
    CampWorkResult schedule_scribe(std::size_t,ItemId,std::size_t);
    CampWorkResult clear_scribe(std::size_t);
    CampWorkPreparation prepare_rest();
    CampWorkResult begin_rest(std::uint32_t minutes);
    RestTickResult rest_tick();
    void stop_rest();
    void rest_notice_presented();
    const RestState* rest_state() const {return camp_open_?&rest_:nullptr;}
};}
