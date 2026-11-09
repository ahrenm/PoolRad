#include "gbengine/ui/dialogue_text.hpp"
#include "gbengine/debug/runtime.hpp"
#include "gbengine/modes/modes.hpp"
#include "gbengine/app/exploration_scenario.hpp"
#include "gbengine/world/exploration_session.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/saves/port_save.hpp"
#include "gbengine/saves/save_repository.hpp"
#include "gbengine/magic/equipment_effect_service.hpp"
#include <iostream>
#include <atomic>
namespace GBEngine {
namespace {
std::atomic<std::uint64_t> next_script_audio_owner{1};
std::unique_ptr<ExplorationSession> create_session(const IFileSystem& files,const LaunchOptions& options,ISaveRepository* saves,std::ostream& out) {
    if(!options.resume_slot.empty()) {
        if(!saves) throw DecodeError("Port save storage is not available");
        return std::make_unique<ExplorationSession>(files,PortSave::decode(saves->load(options.resume_slot.front())));
    }
    if(!options.save_slot.empty()) {
        const auto save=OriginalSave::load(files,options.save_slot.front());
        auto cache=options.strings_file;cache+=".original-"+options.save_slot+".toml";
        StringStore::active().prepare_original_roster(files,save.roster(),cache,out);
        return std::make_unique<ExplorationSession>(files,save);
    }
    if(options.scenario_fixture=="combat-weapons" || options.scenario_fixture=="combat-duel" || timed_overland_scenario(options.scenario_fixture)) {
        if(options.party_fixture.empty())throw DecodeError("The timed campaign fixture requires --party A or J");
        const auto save=OriginalSave::load(files,options.party_fixture.front());
        std::array<std::uint16_t,7> words{};
        for(std::size_t i=0;i<words.size();++i)words[i]=save.banks().raw_value(static_cast<std::uint16_t>(0x49c6+i));
        return std::make_unique<ExplorationSession>(files,static_cast<unsigned>(options.disk),
                   static_cast<std::uint8_t>(options.record),nullptr,GameClock(words));
    }
    return std::make_unique<ExplorationSession>(files,static_cast<unsigned>(options.disk),static_cast<std::uint8_t>(options.record));
}
class ExplorationMode final : public IGameMode {
    std::unique_ptr<ExplorationSession> session_;
    Debug::SessionAttachment debug_session_;
    ExplorationScreen screen_;
    ExplorationUiState ui_;
    const IFileSystem& files_;
    ISaveRepository* saves_;
    ISaveRepository* characters_;
    bool character_open_=false;
    bool ending_open_=false;
    bool camp_requested_=false;
    bool campaign_open_=false;
    bool combat_open_=false;
    struct Narrative {
        std::uint64_t revision{};
        std::string text;
        bool operator==(const Narrative&) const =default;
    };
    enum class HandoffKind {combat,campaign,encamp,character,ending};
    struct Handoff {
        HandoffKind kind;
        std::uint64_t token{};
        Narrative narrative;
        bool operator==(const Handoff&) const =default;
    };
    Narrative acknowledged_narrative_;
    std::optional<Handoff> handoff_;
    bool handoff_presented_=false,handoff_accepted_=false,handoff_abort_=false;
    mutable std::optional<Handoff> rendered_handoff_;
    mutable std::optional<std::pair<std::uint64_t,Narrative>> rendered_text_page_;
    Narrative narrative() const {return {session_->dialogue_revision(),std::string(DialogueText::visible(session_->display_text))};}
    std::optional<Handoff> requested_handoff() const {
        const auto text=narrative();
        if(const auto* battle=session_->combat();battle && !combat_open_) {
            if(battle->deployment())return Handoff{HandoffKind::combat,battle->entry().token.value,text};
            return {}; // Genuine setup errors retain their existing combat UI.
        }
        const auto& request=session_->vm.pending_request();
        const auto token=request?request->token:0;
        if(!campaign_open_ && session_->campaign_requested() && session_->boundary.empty())return Handoff{HandoffKind::campaign,token,text};
        if(session_->nested_camp_ready())return Handoff{HandoffKind::encamp,token,text};
        if(!ending_open_ && session_->ending_pending())return Handoff{HandoffKind::ending,token,text};
        if(!character_open_ && session_->character_services_pending())return Handoff{HandoffKind::character,token,text};
        return {};
    }
    void sync_handoff() {
        auto next=requested_handoff();
        if(next==handoff_)return;
        handoff_=std::move(next);handoff_presented_=false;handoff_abort_=false;
        handoff_accepted_=!handoff_ || handoff_->narrative.text.empty() || handoff_->narrative==acknowledged_narrative_;
    }
    bool handoff_pending() const {return handoff_ && !handoff_accepted_;}

    bool recovery_requested_=false;
    std::unique_ptr<IGameMode> load_browser_;
    bool load_cancelled_=false;
    LaunchOptions load_options_;
    std::ostream& output_;
    std::optional<ExplorationUiState> combat_ui_checkpoint_;
    NativeSoundCues sound_commands_;
    std::uint64_t sound_owner_=next_script_audio_owner.fetch_add(1);
    std::uint64_t sound_sequence_=0,sound_wait_=0;
    std::vector<std::uint16_t> pending_sounds_;
    struct MotionSound {int x,y;unsigned disk;std::uint8_t record;bool city;};
    std::optional<MotionSound> motion_sound_;
    void queue_motion_sound(NativeSoundCue cue,unsigned count=1) {
        while(count-- && pending_sounds_.size()<32)pending_sounds_.push_back(sound_commands_.command(cue));
    }
    void begin_motion_sound() {
        if(session_->can_act())motion_sound_=MotionSound{session_->position_x(),session_->position_y(),session_->disk(),session_->record(),session_->city_mode()};
    }
    void observe_motion_sound() {
        if(!motion_sound_)return;
        const auto prior=*motion_sound_;
        if(session_->last_move) {
            if(*session_->last_move==MoveResult::moved)queue_motion_sound(NativeSoundCue::ecl_call8,prior.city?1:2);
            else if(!prior.city && (*session_->last_move==MoveResult::solid_wall || *session_->last_move==MoveResult::locked_door))
                queue_motion_sound(NativeSoundCue::weapon_hit);
            motion_sound_.reset();
        }else if(session_->can_act()) {
            // Native2182:32F8's outer coordinate-change cue is independent
            // of the inner interior073B step; transfers have no inner step.
            if(session_->position_x()!=prior.x || session_->position_y()!=prior.y)
                queue_motion_sound(NativeSoundCue::ecl_call8);
            motion_sound_.reset();
        }
    }
    std::optional<std::uint64_t> delay_token_;
    std::uint64_t delay_elapsed_=0;
    std::uint64_t delay_duration_=450;
    bool delay_presented_=false;
    bool sync_delay() {
        const auto& request=session_->vm.pending_request();
        if(!request || request->kind!=EclRequestKind::delay) {
            delay_token_.reset();delay_elapsed_=0;delay_presented_=false;return false;
        }
        if(delay_token_!=request->token) {
            delay_token_=request->token;delay_elapsed_=0;delay_presented_=false;
            delay_duration_=session_->script_delay_ms();
        }
        return true;
    }
    void open_ready_camp(IGameFlow& flow) {
        if(!session_->nested_camp_ready() && (!camp_requested_ || !session_->can_act())) return;
        sync_handoff();if(handoff_pending())return;
        camp_requested_=false;
        flow.push_mode(make_encamp_mode(*session_,screen_,files_,saves_,load_options_.qol_upgrades));
    }
    void begin_requested_recovery() {
        if(!recovery_requested_ || !session_->can_act())return;
        recovery_requested_=false;
        if(!session_->look())throw DecodeError("Pollution recovery fixture lost its source continuation");
    }
    std::string status_text() const {
        const auto& session=*session_;
        std::string activity=session.can_act()?"READY":session.boundary;
        if(session.last_move && session.can_act()) {
            const auto result=*session.last_move;
            activity=result==MoveResult::moved?"READY":result==MoveResult::solid_wall?"WALL":result==MoveResult::locked_door?"LOCKED DOOR": "MAP BOUNDARY";
        }
        return activity;
    }
    void command(ExplorationCommand command,IGameFlow& flow) {
        auto& session=*session_;ui_.focus=command;
        switch(command) {
        case ExplorationCommand::area:
            if(!session.toggle_area_view()) ui_.notice=overlay_string(0x17046);
            break;
        case ExplorationCommand::view:
            if(const auto selected=session.active_character()) flow.push_mode(make_character_view_mode(session.world.party,*selected,screen_.font(),files_,CharacterViewServices::for_exploration(session,session.character_portraits_enabled())));
            else ui_.notice="Load a party to view a character.";
            break;
        case ExplorationCommand::search:session.toggle_search();break;
        case ExplorationCommand::look:session.look();break;
        case ExplorationCommand::cast:
            try {flow.push_mode(make_spell_mode(session,screen_));}
            catch(const DecodeError& error) {ui_.notice=error.what();}
            break;
        case ExplorationCommand::encamp:
            camp_requested_=true;
            if(!session.request_camp())camp_requested_=false;
            open_ready_camp(flow);break;
        }
    }
public:
    ExplorationMode(const IFileSystem& files,const LaunchOptions& options,std::ostream& out,ISaveRepository* saves,std::unique_ptr<ExplorationSession> supplied={},ISaveRepository* characters=nullptr)
        :session_(supplied?std::move(supplied):create_session(files,options,saves,out)),debug_session_(*session_),screen_(ExplorationScreen::load(files,options.qol_upgrades)),files_(files),saves_(saves),characters_(characters),load_options_(options),output_(out),sound_commands_(NativeSoundCues::load(files)) {
        session_->set_qol_upgrades(options.qol_upgrades,options.journal_directory);
        auto& session=*session_;const auto disk=options.disk,record=options.record;
        const auto scenario=std::string_view(options.scenario_fixture),party_slot=std::string_view(options.party_fixture);
        if(!scenario.empty()) {
            const auto* fixture=find_exploration_scenario(scenario);
            if(!fixture || fixture->disk!=disk || fixture->record!=record)throw DecodeError("Invalid exploration scenario");
        }
        if(scenario=="party-strength" && party_slot.empty()) throw DecodeError("The party-strength fixture requires --party A or J");
        if(linked_overland_scenario(scenario) && party_slot.empty())throw DecodeError("The linked overland fixture requires --party A or J");
        if((scenario=="combat-entry" || scenario=="combat-movement" || scenario=="combat-weapons" || scenario=="combat-duel") && party_slot.empty()) throw DecodeError("The combat fixture requires --party A or J");
        if(scenario=="encounter-dialogue" && party_slot.empty()) throw DecodeError("The encounter-dialogue fixture requires --party A or J");
        if(scenario=="plaza") session.vm.seed_value(0x4ab0,1); // Explicit fixture: enable native introduction initialization.
        if(!party_slot.empty() && !timed_overland_scenario(scenario)) {
            session.world.party.import_characters(files,party_slot.front());
            out<<"Imported six characters and their item sidecars from slot "<<party_slot<<" as a preview fixture; original saves remain read-only. Campaign and spells are not restored.\n";
        }
        if(scenario=="encounter-dialogue" || scenario=="combat-entry" || scenario=="combat-movement" || scenario=="combat-weapons" || scenario=="combat-duel") {
            session.world.party.restore_characters(files,OriginalSave::load(files,party_slot.front()).roster());
            (void)EquipmentEffectService::bind_restored(session.world.party);
        }
        if(linked_overland_scenario(scenario)) {
            session.world.party.restore_characters(files,OriginalSave::load(files,party_slot.front()).roster());
            (void)EquipmentEffectService::bind_restored(session.world.party);
            session.vm.seed_value(0x6e3e,static_cast<std::uint16_t>(session.world.party.size()));
            session.vm.seed_value(0x49c3,scenario=="overland-links-east"?2:15);
            session.vm.seed_value(0x49c4,20);
            session.world.facing=scenario=="overland-links-east"?Direction::west:Direction::east;
            out<<"Linked overland fixture: source region"<<(record-23)<<" at "<<session.vm.read_value(0x49c3)
               <<",20. Actual ECL initialization, link and arrival events run; campaign clock is unbound.\n";
        }
        if(timed_overland_scenario(scenario)) {
            const auto imported=OriginalSave::load(files,party_slot.front());
            session.world.party.restore_characters(files,imported.roster());
            (void)EquipmentEffectService::bind_restored(session.world.party);
            session.vm.seed_value(0x6e3e,static_cast<std::uint16_t>(session.world.party.size()));
            session.vm.seed_value(0x6e12,static_cast<std::uint16_t>(disk));
            const bool recovery=scenario=="overland-recovery";
            session.vm.seed_value(0x49c3,recovery?7:10);session.vm.seed_value(0x49c4,recovery?18:26);
            session.world.facing=Direction::north;
            if(recovery) {
                session.vm.seed_value(0x4ab3,254);session.vm.seed_value(0x4aa2,0);
                out<<"Pollution recovery fixture: region3 at7,18, restored party/effects and source-save clock; "
                      "quest progress FE and river permission0 are explicit test state, not a saved campaign location.\n";
            } else {
                // A native fresh-map load preserves persistent quest words,
                // clearing the old map's4A00..4A1F locals. Relocation remains explicit.
                for(unsigned address=0x4a20;address<=0x4cff;++address)
                    session.vm.seed_value(static_cast<std::uint16_t>(address),imported.banks().raw_value(static_cast<std::uint16_t>(address)));
                session.vm.seed_value(0x49ff,imported.banks().raw_value(0x49ff));
                out<<"Timed overland fixture: region3 at10,26, with restored party/effects, source-save clock and persistent quest flags. "
                      "This is not a saved campaign location.\n";
            }
        }
        if(!options.save_slot.empty() || !options.resume_slot.empty()) out<<"Restored campaign "<<(options.resume_slot.empty()?options.save_slot:options.resume_slot)<<": "<<session.world.party.size()
            <<" characters, item/effect records, three state banks and saved ECL image. "<<(session.city_mode()?"CITY":"GEO")<<session.disk()<<"/"<<static_cast<unsigned>(session.city_mode()?session.record():session.world.map->id)
            <<" at ("<<session.position_x()<<","<<session.position_y()<<"), facing "<<static_cast<unsigned>(session.world.facing)
            <<". Source saves remain read-only; exploration time and bounded effect countdown are enabled. Spells and combat remain incomplete.\n";
        const auto status=options.resume_slot.empty()?session.pump():session.vm.status();
        out<<"ECL initialization: "<<ecl_status_name(status)<<" "<<session.boundary<<'\n';
        for(const auto& text:session.messages) out<<text<<'\n';
        if(!session.world.map && !session.city_mode()) throw DecodeError("ECL initialization did not load an exploration map");
        if(scenario=="overland-recovery") {
            recovery_requested_=true;begin_requested_recovery();
            out<<"Original entry1 recovery event: answer the continue menu, then the three distinct key waits; "
                  "source picture and pyramid menu follow. Look charges its normal ten minutes.\n";
        }
        if(scenario=="plaza") {
            session.world.x=0;session.world.y=4;session.world.facing=Direction::east;
            session.forward();
            out<<"Plaza preview fixture: GEO1/18 (0,4), facing east; VM 4AB0=1 before initialization. No saved campaign is restored.\n";
        }
        if(scenario=="parlay") {
            session.vm.start_at(0xa315);session.pump();
            session.display_text="PARLAY PREVIEW FIXTURE - CHOOSE AN ATTITUDE";
            out<<"Parlay preview fixture: GEO4/2 initialized, direct ECL start at A315. No saved campaign or natural encounter context is restored.\n";
        }
        if(scenario=="text-input" || scenario=="number-input") {
            session.vm.start_at(scenario=="text-input"?0x9c0a:0xb3bd);session.pump();
            session.display_text=scenario=="text-input"?"TEXT ENTRY PREVIEW FIXTURE - ENTER AN ANSWER":"NUMBER ENTRY PREVIEW FIXTURE - ENTER A VALUE";
            out<<"Input preview fixture: direct original instruction in "<<scenario<<"; no saved campaign or natural event context is restored.\n";
        }
        if(scenario=="glyphs") {
            session.vm.start_at(0x9bd8);session.pump(5);
            session.display_text="SYMBOL TABLE PREVIEW FIXTURE - ENTER RESUMES ORIGINAL ECL";
            out<<"Symbol preview fixture: direct ECL4/21 start at 9BD8, five-instruction inspection stop. No saved campaign or natural event context is restored.\n";
        }
        if(scenario=="party-strength") {
            session.vm.start_at(0xa00d);
            if(session.pump(1)!=EclStatus::budget_exhausted || session.vm.pc()!=0xa011)
                throw DecodeError("Original party-strength fixture did not complete its instruction");
            session.display_text="PARTY STRENGTH RATING: "+std::to_string(session.vm.read_value(0x6e79))+"\nENTER RESUMES ORIGINAL ECL";
            out<<"Party-strength preview fixture: original ECL1/18 at A00D; rating "<<session.vm.read_value(0x6e79)<<". No saved campaign or natural encounter context is restored.\n";
        }
        if(scenario=="combat-entry" || scenario=="combat-movement" || scenario=="combat-weapons") {
            session.vm.seed_value(0x6e3e,static_cast<std::uint16_t>(session.world.party.size())); // Explicit fixture roster binding.
            if(scenario=="combat-weapons") {session.vm.seed_value(0x6e12,3);session.vm.seed_value(0x49f2,8);}
            if(!session.retain_exploration_checkpoint())throw DecodeError("Combat fixture requires idle initialized exploration");
            combat_ui_checkpoint_=ui_;
            if(scenario=="combat-weapons") {
                // Explicit small fight: the original B481 Corporal profile and
                // CPIC pair, quantity1, followed by the real ECL24 at B489.
                // The ordinary guard script retains all four original groups.
                auto encounter=session.world.encounter;encounter.clear();auto party=session.world.party;
                const auto expected=session.vm.random_state();auto random=expected;
                EncounterService::add_group(encounter,party,files,3,54,random,[]{return std::array<std::uint8_t,2>{1,15};});
                session.vm.commit_random_state(expected,random);
                session.world.encounter=std::move(encounter);session.world.party=std::move(party);
                session.vm.seed_value(0x6dc1,2);session.vm.seed_value(0x6dc6,100);
                session.vm.seed_value(0x6dcb,0);session.vm.start_at(0xb489);
            } else session.vm.start_at(0xb45b);
            if(scenario=="combat-movement") {
                for(unsigned work=0;work<128 && session.vm.pc()!=0xb489;++work)session.pump(1);
                if(session.vm.pc()!=0xb489 || session.vm.pending_request())throw DecodeError("Movement fixture did not reach source combat instruction");
                session.vm.seed_value(0x6dcb,2);
            }
            session.pump();
            if(!session.combat())throw DecodeError("Original guard combat fixture failed: "+session.boundary);
            out<<"Combat entry fixture: original ECL3/8 guard refusal tail at B45B. No saved campaign or natural encounter context is restored. F12 rolls back to initialized exploration.\n";
            if(scenario=="combat-movement")out<<"Movement test fixture: enemy surprise VM6DCB=2 selects a first-round player without skipping AI. Restored character graphs are used; this is not a natural encounter result.\n";
            if(scenario=="combat-weapons")out<<"Weapon test fixture: one original MON3/54 Corporal with its source CPIC3/15 pair, no surprise, restored party graphs and source-save clock. This is a controlled small fight, not the original four-group guard encounter. Campaign map/quests are a preview; F12 restores the checkpoint.\n";
        }
        if(scenario=="encounter-dialogue") {
            session.vm.seed_value(0x6e3e,static_cast<std::uint16_t>(session.world.party.size()));
            session.world.x=5;session.world.y=9;session.world.facing=Direction::north;
            session.vm.seed_value(0x4ae1,0);combat_ui_checkpoint_=ui_;
            if(!session.look())throw DecodeError("Encounter fixture requires idle initialized exploration");
            out<<"Encounter dialogue fixture: GEO4/2 event2 at (5,9), Look dispatches original entry1. Party graphs restored for pace queries; campaign is a preview. F12 rolls back after combat entry.\n";
        }
        if(scenario=="combat-duel") {
            session.world.party.select(0);
            session.vm.seed_value(0x6e3e,static_cast<std::uint16_t>(session.world.party.size()));
            session.vm.seed_value(0x6e12,3);session.vm.seed_value(0x49f2,11);
            session.vm.seed_value(0x6dc1,2);session.vm.seed_value(0x6dc6,100);session.vm.seed_value(0x6de1,255);
            if(!session.retain_exploration_checkpoint())throw DecodeError("Duel fixture requires idle initialized exploration");
            combat_ui_checkpoint_=ui_;session.vm.start_at(0x9ca5);session.pump();
            if(!session.combat())throw DecodeError("Original mirror duel fixture failed: "+session.boundary);
            out<<"Mirror duel fixture: original ECL3/11 at9CA5 executes1C/CALL8000/24 with the selected roster head and owned mirror opponent. Restored party and clock, preview campaign; F12 restores exploration.\n";
        }
        acknowledged_narrative_=narrative();session.pause_text_replacement(true);
        out<<"Event preview: Up steps; Left/Right turn; Down turns around. On city maps arrows move north/east/south/west. L looks, Enter continues, F5 scales. Full party/time/combat services remain incomplete.\n";
        if(session.world.party.size()) out<<"Idle party views: 1-6 opens a member; I opens Inventory; L or Backspace leaves each screen.\n";
        if(session.city_mode()) ui_.focus=ExplorationCommand::cast;
        out<<"Exploration UI: Home/End selects the active character; A toggles Area; V opens the active character; C opens restored memorized spells; E opens camp. Save or cancel the slot choice to reach the quit query. Rest, Memorize and Scribe use restored camp services; unsupported combat/recovery remains explicit.\n";

    }
    std::unique_ptr<IGameMode> terminal_mode()override {
        if(!session_ || !session_->party_defeated())return {};
        if(session_->defeat_acknowledged())return make_cale_mode(files_,load_options_,output_,saves_,characters_);
        return make_game_over_mode(std::move(session_),screen_,files_,load_options_,output_,saves_,characters_);
    }
    bool text_input_active() const override {return session_->entering_input();}
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed) return;
        if(load_browser_) {
            load_browser_->handle_input(event,flow);
            if(load_cancelled_) {load_browser_.reset();load_cancelled_=false;session_->hold_overland_animation();}
            return;
        }
        auto& session=*session_;
        sync_handoff();
        if(handoff_) {
            if(handoff_pending() && !handoff_abort_) {
                if(event.key==Key::combat_abort && handoff_->kind==HandoffKind::combat)handoff_abort_=true;
                else if(event.key==Key::accept && handoff_presented_) {
                    handoff_accepted_=true;acknowledged_narrative_=handoff_->narrative;session.acknowledge_dialogue();
                }
            }
            return; // Repeated input cannot leak into the child before it opens.
        }
        if(session.combat() && !combat_open_)return;
        if(session.door_input(event))return;
        const auto page=narrative();
        if(session.text_page_input(event)) {
            if(!session.text_page_pending())acknowledged_narrative_=page;
            return;
        }
        // ECL3A is a calibrated timed wait, not a keyboard acknowledgement.
        if(sync_delay())return;
        if(event.key==Key::load) {
            if(session.can_act() && !camp_requested_ && !campaign_open_ && !combat_open_) {
                load_browser_=make_load_mode(files_,load_options_,output_,saves_,[this]{load_cancelled_=true;},characters_);
                session.hold_overland_animation();
            }
            return;
        }
        if(session.can_act()) {
            combat_ui_checkpoint_=ui_;ui_.notice.clear();
            // A new idle action dismisses the previous scene's text. Only
            // automatic replacement within its source continuation must pause.
            acknowledged_narrative_=narrative();session.acknowledge_dialogue();
        }
        const auto menu_token=session.menu()?std::optional<std::uint64_t>{session.vm.pending_request()->token}:std::nullopt;
        const auto menu_text=narrative();
        if(session.entry_input(event) || session.character_input(event) || session.menu_input(event)) {
            if(menu_token && (!session.vm.pending_request() || session.vm.pending_request()->token!=*menu_token))acknowledged_narrative_=menu_text;
            open_ready_camp(flow);return;
        }
        if(session.city_mode() && session.can_act() &&
           (event.key==Key::up || event.key==Key::right || event.key==Key::down || event.key==Key::left ||
            event.character=='8' || event.character=='6' || event.character=='2' || event.character=='4'))begin_motion_sound();
        if(session.city_input(event)){observe_motion_sound();open_ready_camp(flow);return;}
        if(camp_requested_) {open_ready_camp(flow);return;}
        if(session.can_act() && event.character>='1' && event.character<='6') {
            const auto index=static_cast<std::size_t>(event.character-'1');
            if(index<session.world.party.size()) flow.push_mode(make_character_view_mode(session.world.party,index,screen_.font(),files_,CharacterViewServices::for_exploration(session,session.character_portraits_enabled())));
            return;
        }
        if(session.can_act()) {
            if(event.key==Key::home || event.key==Key::end) {session.cycle_character(event.key==Key::home?-1:1);return;}
            auto key=event.character;if(key>='a' && key<='z') key=static_cast<std::uint8_t>(key-'a'+'A');
            if(key==',' || key=='.') {
                const int first=session.city_mode()?1:0,count=6-first;
                const int focus=static_cast<int>(ui_.focus);
                ui_.focus=static_cast<ExplorationCommand>(first+(focus-first+(key==','?count-1:1))%count);return;
            }
            if(event.key==Key::accept) return; // Native exploration ignores idle CR; letters activate commands.
            switch(key) {
            case 'J':if(load_options_.qol_upgrades){try{flow.push_mode(make_journal_mode(session,screen_.font(),load_options_.journal_directory));}catch(const std::exception& error){ui_.notice=std::string("Journal: ")+error.what();}}return;
            case 'A':if(!session.city_mode()) command(ExplorationCommand::area,flow);return;
            case 'C':command(ExplorationCommand::cast,flow);return;
            case 'V':command(ExplorationCommand::view,flow);return;
            case 'E':command(ExplorationCommand::encamp,flow);return;
            case 'S':command(ExplorationCommand::search,flow);return;
            case 'L':command(ExplorationCommand::look,flow);return;
            default:break;
            }
        }
        switch(event.key) {
        case Key::left:if(session.turn(-1))queue_motion_sound(NativeSoundCue::ecl_call8);break;
        case Key::right:if(session.turn(1))queue_motion_sound(NativeSoundCue::ecl_call8);break;
        case Key::down:session.turn(2);break;
        case Key::up:begin_motion_sound();session.forward();observe_motion_sound();break;
        case Key::look:session.look();break;
        case Key::accept:session.continue_dialogue();break;
        default:break;
        }
    }
    void update(std::uint64_t elapsed,IGameFlow& flow) override {
        if(load_browser_) {load_browser_->update(elapsed,flow);return;}
        sound_wait_-=std::min(sound_wait_,elapsed);
        observe_motion_sound();
        if(sync_delay() && delay_presented_) {
            // Native2182:271F ->1709:0DBE waits225*resident Speed.
            // Saved Speed controls only this source delay, never the host Hz.
            const auto dwell=delay_duration_;
            delay_elapsed_+=std::min(elapsed,dwell-delay_elapsed_);
            if(delay_elapsed_==dwell) {
                session_->continue_dialogue();sync_delay();
            }
        }
        if(session_->combat()) {
            if(!combat_open_) {
                sync_handoff();
                if(handoff_pending())return;
                combat_open_=true;flow.push_mode(make_combat_mode(*session_,screen_));
            }
            return;
        }
        session_->advance_picture(elapsed);
        session_->advance_overland_colour(elapsed);
        session_->advance_command_notice(elapsed);
        session_->advance_overland_animation(elapsed);
        session_->advance_effect_notice(elapsed);
        session_->advance_encounter_notice(elapsed);
        begin_requested_recovery();
        if(session_->city_mode() && ui_.focus==ExplorationCommand::area) ui_.focus=ExplorationCommand::cast;
        sync_handoff();if(handoff_pending())return;
        open_ready_camp(flow);
        if(!ending_open_ && session_->ending_pending()) {
            try {auto ending=make_ending_mode(*session_,files_,screen_);ending_open_=true;flow.push_mode(std::move(ending));}
            catch(const DecodeError& error){ui_.notice="Ending presentation: "+std::string(error.what());}
            return;
        }
        if(!character_open_ && session_->character_services_pending()) {
            character_open_=true;flow.push_mode(make_character_services_mode(*session_,files_,load_options_,output_,saves_,characters_));
        }
        if(!campaign_open_ && session_->campaign_requested() && session_->begin_campaign()) {
            campaign_open_=true;flow.push_mode(make_campaign_mode(*session_,screen_));
        }
    }
    void on_suspend() override {
        handoff_presented_=false;rendered_handoff_.reset();rendered_text_page_.reset();
        session_->hold_door();
        delay_presented_=false;session_->hold_text_page();pending_sounds_.clear();sound_wait_=0;
        while(session_->take_script_sound()){} // Abandoned cosmetic audio never replays on resume.
    }
    void on_resume() override {
        handoff_presented_=false;rendered_handoff_.reset();rendered_text_page_.reset();
        delay_presented_=false;
        session_->hold_text_page();
        ending_open_=false;
        character_open_=false;
        if(combat_open_) {
            combat_open_=false;camp_requested_=false;
            if(combat_ui_checkpoint_)ui_=*combat_ui_checkpoint_;
        }
        campaign_open_=false;session_->resume_after_camp();
        session_->hold_overland_animation();
        session_->hold_command_notice();
        session_->hold_overland_colour();
    }
    void render(Framebuffer& frame) const override {
        if(load_browser_) {load_browser_->render(frame);return;}
        auto ui=ui_;
        const auto next=requested_handoff();
        const bool pending=next && (next==handoff_?handoff_pending():
            !next->narrative.text.empty() && next->narrative!=acknowledged_narrative_);
        ui.combat_intro=pending && next->kind==HandoffKind::combat;
        ui.narration_handoff=pending;
        rendered_handoff_=pending?next:std::nullopt;
        rendered_text_page_=session_->text_page_pending() && session_->vm.pending_request()?
            std::optional{std::pair{session_->vm.pending_request()->token,narrative()}}:std::nullopt;
        screen_.draw(frame,*session_,ui);
    }
    std::string title() const override {
        if(load_browser_)return load_browser_->title();
        const auto& session=*session_;const auto activity=status_text();
        const auto location=session.city_mode()?session.record():session.world.map?session.world.map->id:session.record();
        return "PoolRad | "+std::string(session.city_mode()?"OVERLAND":session.world.map?"GEO":"ECL")+std::to_string(session.disk())+"/"+std::to_string(location)+
            " | x="+std::to_string(session.position_x())+" y="+std::to_string(session.position_y())+
            " facing="+std::to_string(static_cast<unsigned>(session.world.facing))+" event="+
            std::to_string(session.city_mode() || !session.world.map?0:session.world.map->event(session.world.x,session.world.y))+" | "+activity+
            " | active="+(session.active_character()?std::to_string(*session.active_character()+1):"none")+" | "+(session.area_view()?"Area":"Perspective");
    }
    void after_present(IGameFlow& flow) override {
        if(load_browser_) {load_browser_->after_present(flow);return;}
        const auto picture=session_->picture_token();
        session_->door_presented();
        const auto page_request=session_->vm.pending_request();
        const auto text=session_->text_page_pending() && page_request && rendered_text_page_==
            std::optional{std::pair{page_request->token,narrative()}};
        while(const auto sound=session_->take_script_sound()) {
            const auto cue=sound_commands_.command(sound->second==8?NativeSoundCue::ecl_call8:NativeSoundCue::ecl_call10);
            if(pending_sounds_.size()<32)pending_sounds_.push_back(cue);
        }
        if(!sound_wait_ && !pending_sounds_.empty()) {
            flow.audio_cue({{sound_owner_,++sound_sequence_,0},pending_sounds_.front(),0,true,AudioCueDomain::script});
            pending_sounds_.erase(pending_sounds_.begin());sound_wait_=100;
        }
        sync_handoff();
        if(handoff_pending()) {
            if(rendered_handoff_==handoff_) {
                handoff_presented_=true;
                if(handoff_abort_ && handoff_->kind==HandoffKind::combat) {
                    if(!session_->abort_combat(CombatToken{handoff_->token}))throw DecodeError("Combat introduction lost its rollback checkpoint");
                    pending_sounds_.clear();sound_wait_=0;
                    sync_handoff();if(combat_ui_checkpoint_)ui_=*combat_ui_checkpoint_;
                }
            }
            return;
        }
        session_->overland_colour_presented();
        session_->command_notice_presented();
        session_->effect_notice_presented();
        session_->encounter_notice_presented();
        session_->overland_presented();
        if(picture && session_->picture_token()==picture)session_->picture_presented(*picture);
        if(text && session_->text_page_pending() && session_->vm.pending_request() &&
           rendered_text_page_==std::optional{std::pair{session_->vm.pending_request()->token,narrative()}})session_->text_page_presented(true);
        if(sync_delay())delay_presented_=true;
    }
};
}
std::unique_ptr<IGameMode> make_exploration_mode(const IFileSystem& files,const LaunchOptions& options,std::ostream& out,ISaveRepository* saves,ISaveRepository* characters) {
    if(isolated_overland_scenario(options.scenario_fixture))return make_overland_mode(files,options,out);
    return std::make_unique<ExplorationMode>(files,options,out,saves,nullptr,characters);
}
std::unique_ptr<IGameMode> make_exploration_mode(const IFileSystem& files,const LaunchOptions& options,std::ostream& out,ISaveRepository* saves,std::unique_ptr<ExplorationSession> session,ISaveRepository* characters) {
    if(!session)throw DecodeError("Missing newly created campaign");
    return std::make_unique<ExplorationMode>(files,options,out,saves,std::move(session),characters);
}
}
