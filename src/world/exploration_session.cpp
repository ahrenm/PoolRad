#include "gbengine/world/exploration_session.hpp"
#include "gbengine/qol/travel.hpp"
#include "gbengine/world/door_service.hpp"
#include "gbengine/world/script_damage_service.hpp"
#include "gbengine/combat/special_combat_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/magic/equipment_effect_service.hpp"
#include "gbengine/campaign/treasure_service.hpp"
#include "gbengine/campaign/wealth_loss_service.hpp"
#include "gbengine/party/roster_service.hpp"
#include "gbengine/campaign/npc_service.hpp"
#include "gbengine/ui/glyph_text.hpp"
#include "gbengine/app/runtime_timing.hpp"
#include "gbengine/world/overland.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
constexpr std::array dx{0,1,0,-1},dy{-1,0,1,0};
unsigned index(Direction d) { return static_cast<unsigned>(d); }
bool city_gate(int x,int y) {
    // ECL7/26's actual entry1 city-menu coordinates (B053/B06A). These
    // entrances have no quest prerequisite; their source menus own admission.
    return (y==26 && (x==12 || x==13)) || (x==13 && y==27) || (x==11 && y==28);
}
bool shared_region_veto(const IFileSystem& files,unsigned region,int local_x,std::uint8_t tile,std::uint16_t pollution) {
    // The three ECL regions overlap by three columns of one physical map.
    // Their legacy tile lists differ. Preserve both restrictions in an overlap
    // so changing regional representation cannot open a one-way terrain hole.
    // This extends the host C01B compatibility policy, not native DOS dispatch.
    struct Table {unsigned disk;std::uint8_t record;std::size_t address,count;const char* hash;};
    constexpr std::array tables{
        Table{6,25,0xadce,9,"dfeaaaa6ba539e26a6ec413c5bcaa611b0657c73424b1fa76ad9f1af4ade5a4e"},
        Table{7,26,0xb019,29,"c3a1a667e768b48bdfe249a5e23f0cac31204822bf708d74f98bda97d15a6d88"},
        Table{8,27,0xab9e,9,"763d8f8af0c88d28f2b78a01d872b5134f6d19d617bd92f1517ecd10e4c2a2fe"}};
    constexpr Table recovered{7,26,0xb036,20,"df3838b6637702d71c72f1c07e53584b18ba05d69a533242937ce168ae0b528b"};
    const int global_x=local_x+static_cast<int>(region-2)*13;
    for(unsigned other=2;other<=4;++other) {
        const int x=global_x-static_cast<int>(other-2)*13;
        if(other==region || x<0 || x>15)continue;
        const auto& table=other==3 && pollution>=254?recovered:tables[other-2];
        const auto script=load_record(files,"ECL"+std::to_string(table.disk)+".DAX",table.record);
        const auto offset=table.address-0x9900+2;
        if(script.size()<offset+table.count)throw DecodeError("Truncated shared overland tile table");
        const auto values=std::span(script).subspan(offset,table.count);
        if(sha256(values)!=table.hash)throw DecodeError("Unsupported shared overland tile table");
        // Source loops use inclusive final indices8/28/19, including the ninth
        // byte of the west/east lists. Do not reinterpret these as lengths.
        if(std::find(values.begin(),values.end(),tile)!=values.end())return true;
    }
    return false;
}
bool native_inert_call(std::uint16_t address) {
    // Selected-build2182:2CCF has no matching branch for these legacy calls.
    // In particular,2C51/C003/C009 do not consume a bitmap buffer or audio.
    constexpr std::array calls{0x2c4e,0x0806,0x2c51,0xc003,0xc009};
    return std::find(calls.begin(),calls.end(),address)!=calls.end();
}
bool native_inert_tile_write(std::uint16_t address) {
    // ECL7/26's legacy tile tables address base+(byte row)*8+column0..7.
    // Native27A9:06FB classifies both complete domains as special, and
    // 27A9:0C77 has no matching numeric setter: the writes are inert. Reads
    // remain unresolved (0E0C's local is uninitialized), never zero-filled.
    return (address>=0xd000 && address<=0xd7ff) || (address>=0x6300 && address<=0x6aff);
}
}
ExplorationSession::ExplorationSession(const IFileSystem& f,unsigned disk,std::uint8_t record,IEffectExpiryResolver* expiry,
        std::optional<GameClock> initial_clock)
    :files_(f),record_(record),expiry_resolver_(expiry),vm(load_record(f,"ECL"+std::to_string(disk)+".DAX",record),1,&world) {
    vm.seed_value(0x49e6,1); // Exploration mode, native initializer 27a9:01b6.
    vm.seed_value(0x49fc,2); // User-selected450ms default; saved resident Speed mirror.
    world.resource_disk=disk;
    vm.seed_value(0x6de1,255); // Native area2 + 5c2: normal PIC mode until changed by ECL.
    if(initial_clock) {
        for(std::size_t i=0;i<initial_clock->words().size();++i)
            vm.seed_value(static_cast<std::uint16_t>(0x49c6+i),initial_clock->words()[i]);
        campaign_time_bound_=true;
    }
    vm.start_entry(4);
}
ExplorationSession::ExplorationSession(const IFileSystem& files,Party party,std::mt19937 random)
    :ExplorationSession(files,3,0,nullptr,GameClock({0,0,0,6,0,0,0})) {
    if(party.size()==0 || party.size()>6)throw DecodeError("A new campaign requires one to six party members");
    for(std::size_t i=0;i<party.size();++i)if(!party.member(i).effects_restored())throw DecodeError("New campaign requires owned character effects");
    // Native2E80:002D initializes zero banks and6AAD/F/E to15/1/6;
    // CALE393E:0161 selects resource disk3 before entering ECL0.
    world.party=std::move(party);world.x=15;world.y=1;world.facing=Direction::west;
    vm.seed_value(0x6e12,3); // Publish CALE's selected disk into native area2 too.
    vm.seed_value(0x6e3e,static_cast<std::uint16_t>(world.party.size()));
    vm.random_=std::move(random);
}
ExplorationSession::ExplorationSession(const IFileSystem& files,const OriginalSave& save,IEffectExpiryResolver* expiry)
    :files_(files),record_(static_cast<std::uint8_t>(save.ecl_record())),
     expiry_resolver_(expiry),vm(load_record(files,"ECL"+std::to_string(save.resource_disk())+".DAX",record_),1,&world) {
    const bool overland=save.banks().raw_value(0x49e6)==0;
    if(overland) {
        if(record_<25 || record_>27 || save.resource_disk()!=record_-19u ||
           save.presentation_mode()!=record_-23u || save.banks().raw_value(0x49c3)>15 ||
           save.banks().raw_value(0x49c4)>35)
            throw DecodeError("Unsupported or inconsistent original overland layout");
        // This adapted intermediate cannot prove whether the native cache has
        // already been recoloured. Schema6 intentionally stores no cache-phase
        // field; never import it as a quiescent, reproducible campaign.
        if(record_==26 && save.banks().raw_value(0x4ab3)==254 && save.banks().raw_value(0x4aa2)==0)
            throw DecodeError("Original overland recovery intermediate requires completion in DOS before import");
    } else if(save.presentation_mode()>=2 || save.banks().raw_value(0x49e6)!=1)
        throw DecodeError("Unsupported original exploration presentation");
    if(!GeoMap::contains(save.x(),save.y()) || save.native_direction()>6 || save.native_direction()%2)
        throw DecodeError("Invalid saved exploration position or direction");
    world.resource_disk=save.resource_disk();world.x=save.x();world.y=save.y();
    world.facing=static_cast<Direction>(save.native_direction()/2);
    world.party.restore_characters(files,save.roster());
    // Adopt only independently verified, unambiguous saved equipment records.
    // Unsupported or ambiguous bindings remain explicit on the character.
    (void)EquipmentEffectService::bind_restored(world.party);
    if(overland) {
        city_=CityAssets::load(files);city_->load_replacement_tiles(files);
        world.tile_query_available=true;
        // Native3D84:1602 restores the final cache only for nonzero permission.
        // A DOS load never replays the map-entry animated prelude.
        if(save.banks().raw_value(0x4aa2)!=0)city_->apply_river_recovery();
        overland_entry_colour_done_=true;
    } else world.map=GeoMap::decode(load_record(files,"GEO"+std::to_string(world.resource_disk)+".DAX",
        static_cast<std::uint8_t>(save.geometry_record())),static_cast<std::uint8_t>(save.geometry_record()));
    // 3D84:1602 reloads the wall cache pairs serialized by 3D84:1D58.
    for(unsigned i=0;!overland && i<3;++i) {
        const auto id=save.banks().raw_value(static_cast<std::uint16_t>(0x4afa+i));
        const auto slot=save.banks().raw_value(static_cast<std::uint16_t>(0x4afd+i));
        if(id>=0x8000) continue; // Native signed-word sentinel test, not just FFFF.
        if(id>255 || slot<1 || slot>3) throw DecodeError("Invalid saved wall cache pair");
        assets.load_walls(files,world.resource_disk,slot,static_cast<std::uint8_t>(id));
    }
    vm.restore_state(save.banks(),save.script_image());
    // Native saved-game flag DS:49F8 skips archive reload and local-bank clears
    // in 27A9:01B6. Its other resets still run, followed by entry 4 only.
    vm.seed_value(0x6de1,255);vm.seed_value(0x6dd2,0);vm.seed_value(0x6dd3,0);
    vm.seed_value(0x49e5,0);vm.seed_value(0x49e6,overland?0:1);
    vm.seed_value(0x6e3e,static_cast<std::uint16_t>(world.party.size()));
    campaign_time_bound_=true;
    vm.start_entry(4);
}
const SpellCatalog& ExplorationSession::spell_catalog() {
    if(!spell_catalog_) spell_catalog_=SpellCatalog::load(files_);
    return *spell_catalog_;
}
std::vector<MemorizedSpell> ExplorationSession::castable_spells() {
    if(!can_act() || !active_character()) throw DecodeError("Casting requires idle exploration and a selected caster");
    // ECL8/16 entry4 explicitly announces its shell and sets49E5. Keep the
    // general service guard for other contexts; explain this known obstruction.
    if(disk()==8 && record_==16 && vm.read_value(0x49e5)!=0)
        throw DecodeError("An anti-magic field surrounds you.");
    return SpellService(world.party,spell_catalog()).castable(*active_character(),magic_context());
}
SpellCastPlan ExplorationSession::prepare_spell(std::size_t slot,MagicTarget target) {
    if(!can_act() || !active_character()) throw DecodeError("Casting requires idle exploration and a selected caster");
    return SpellService(world.party,spell_catalog()).prepare(*active_character(),slot,target,magic_context());
}
MagicResult ExplorationSession::cast_spell(const SpellCastPlan& plan) {
    if(!can_act() || active_character()!=plan.caster) return {MagicStatus::rejected,"Caster/session changed since spell selection",{}};
    auto staged_party=world.party;auto staged_vm=vm;
    const auto prior=vm.random_state();auto random=prior;
    MagicEffectService effects(spell_catalog(),&world.party.item_catalog());
    const auto result=SpellService(staged_party,spell_catalog()).cast(plan,magic_context(),random,effects);
    if(result.applied()) {
        staged_vm.commit_random_state(prior,random);
        world.party=std::move(staged_party);vm=std::move(staged_vm);check_party_defeat();
    }
    return result;
}
ItemActionResult ExplorationSession::activate_item(const ItemEffectRequest& request) {
    if(!can_act()) return {ItemActionStatus::rejected,"Item activation requires idle exploration",request.item,{}};
    auto staged_party=world.party;auto staged_vm=vm;
    const auto prior=vm.random_state();auto random=prior;
    MagicEffectService effects(spell_catalog(),&world.party.item_catalog());
    const auto result=SpellService(staged_party,spell_catalog()).use_item(request,magic_context(),random,effects);
    if(result.applied()) {
        staged_vm.commit_random_state(prior,random);
        world.party=std::move(staged_party);vm=std::move(staged_vm);check_party_defeat();
        effect_notices_.clear();for(const auto& notice:result.feedback)effect_notices_.push_back({notice.recipient,notice.text_id});
        effect_notice_index_=0;effect_notice_elapsed_=0;effect_notice_presented_=false;
    }
    return {result.applied()?ItemActionStatus::applied:result.status==MagicStatus::rejected?ItemActionStatus::rejected:ItemActionStatus::unresolved,
        result.message,request.item,{}};
}
std::optional<std::uint8_t> ExplorationSession::noncombat_item_target_mode(const ItemEffectRequest& request) {
    if(request.cast_index<1 || request.cast_index>67) return {};
    if(request.cast_index<=56) return spell_catalog().definition(SpellId(request.cast_index)).noncombat_target_mode();
    return spell_catalog().item_definition(ItemCastId(request.cast_index)).noncombat_target_mode();
}
bool ExplorationSession::character_services_pending()const {
    const auto& request=vm.pending_request();
    if(!request || request->kind!=EclRequestKind::engine_service)return false;
    try{return static_cast<std::uint8_t>(vm.preview_party_parameter(request->token,world))==0;}catch(const DecodeError&){return false;}
}
bool ExplorationSession::ending_pending()const {
    const auto& request=vm.pending_request();
    if(!request || request->kind!=EclRequestKind::engine_service)return false;
    try{return static_cast<std::uint8_t>(vm.preview_party_parameter(request->token,world))==8;}catch(const DecodeError&){return false;}
}
void ExplorationSession::finish_ending(std::uint64_t token,std::uint64_t revision,const std::mt19937& expected) {
    if(!ending_pending() || vm.pending_request()->token!=token || world.party.revision()!=revision || vm.random_state()!=expected)
        throw DecodeError("Stale ending continuation");
    auto staged=vm;staged.answer_party_request(token,expected,expected);vm=std::move(staged);pump();
}
void ExplorationSession::finish_character_services(std::uint64_t token,std::uint64_t revision,Party party,std::mt19937 random) {
    if(!character_services_pending() || vm.pending_request()->token!=token || world.party.revision()!=revision || !party.size() || party.size()>8)
        throw DecodeError("Stale character-service continuation");
    auto next=vm;
    next.answer_party_request(token,vm.random_state(),random,std::array<EclStateUpdate,1>{{{0x6e3e,static_cast<std::uint16_t>(party.size())}}});
    world.party=std::move(party);vm=std::move(next);pump();
}
ItemActionResult ExplorationSession::ready_item(std::size_t member,ItemId item) {
    if((!can_act() && !campaign_token_) || party_defeated_) return {ItemActionStatus::rejected,"Equipment changes require an available party service",item,{}};
    auto staged=world.party;auto next_vm=vm;const auto expected=vm.random_state();auto random=expected;
    EquipmentEffectService effects(&staged.item_catalog(),{&staged.item_catalog(),&random,static_cast<std::uint8_t>(camp_open_?2:1)});
    const auto result=InventoryService(staged).ready(member,item,&effects);
    if(result.applied()) {
        try {
            if(campaign_token_)next_vm.commit_campaign_random_state(*campaign_token_,expected,random);
            else next_vm.commit_random_state(expected,random);
        }catch(const DecodeError& error) {return {ItemActionStatus::unresolved,error.what(),item,{}};}
        world.party=std::move(staged);vm=std::move(next_vm);check_party_defeat();
    }
    return result;
}
void ExplorationSession::check_party_defeat() {
    bool active=false;
    for(std::size_t i=0;i<world.party.size();++i) active=active || world.party.member(i).active();
    if(world.party.size() && !active) {
        party_defeated_=true;phase_=ExplorationPhase::idle;
        display_text=overlay_string(0x347c);boundary.clear();
    }
}
std::optional<GameClock> ExplorationSession::clock() const {
    if(!campaign_time_bound_) return std::nullopt;
    std::array<std::uint16_t,7> words{};
    for(std::size_t i=0;i<words.size();++i) words[i]=vm.read_value(static_cast<std::uint16_t>(0x49c6+i));
    return GameClock(words);
}
bool ExplorationSession::advance_time(TimeIncrement increment,std::optional<std::uint64_t> token) {
    try {
        auto current=clock();
        if(!current) throw DecodeError("Campaign clock is not initialized in this preview fixture");
        auto staged_party=world.party;
        const auto prior_random=vm.random_state();auto random=prior_random;
        MagicEffectService native_expiry(spell_catalog(),&world.party.item_catalog(),&random);
        const auto result=GameTimeService::advance(*current,staged_party,increment,expiry_resolver_?expiry_resolver_:&native_expiry);
        std::array<EclStateUpdate,7> updates{};
        for(std::size_t i=0;i<updates.size();++i)
            updates[i]={static_cast<std::uint16_t>(0x49c6+i),current->words()[i]};
        auto staged_vm=vm;
        if(token) staged_vm.answer_advance_time(*token,updates,prior_random,random);
        else staged_vm.commit_rest_tick(prior_random,random,current->words());
        vm=std::move(staged_vm);world.party=std::move(staged_party);
        effect_notices_=result.effects.notices;effect_notice_index_=0;effect_notice_presented_=false;effect_notice_elapsed_=0;
        check_party_defeat();return true;
    } catch(const std::exception& error) {
        boundary="Time advancement unresolved: "+std::string(error.what());return false;
    }
}
void ExplorationSession::prepare_overland_colour() {
    if(!city_mode()) {overland_entry_colour_done_=true;return;}
    auto next=*city_;
    if(vm.read_value(0x4aa2)!=0) {
        next.apply_river_recovery();city_=std::move(next);overland_entry_colour_done_=true;return;
    }
    if(record_!=26 || vm.read_value(0x4ab3)!=254) {overland_entry_colour_done_=true;return;}
    const auto expected=vm.random_state();auto random=expected;
    const OverlandCampaignState state{vm.read_value(0x4a8c),vm.read_value(0x4ad2),
        vm.read_value(0x4a7c),vm.read_value(0x4a9f),vm.read_value(0x4aa0),vm.read_value(0x4ab3)};
    OverlandColourPresentation presentation;presentation.frames.reserve(11);
    const auto append=[&](int left,int top) {
        auto& frame=presentation.frames.emplace_back();frame.fill(0xff000000u);
        next.draw_window(frame,left,top,16,0,&state);
    };
    append(17,15);
    for(const auto& window:next.river_windows) {
        next.apply_river_recovery(&random);append(window[0],window[1]);
    }
    next.apply_river_recovery();append(23,25);
    auto next_vm=vm;next_vm.commit_random_state(expected,random);
    city_=std::move(next);vm=std::move(next_vm);overland_colour_=std::move(presentation);
    overland_entry_colour_done_=true;overland_presented_=false;
}
void ExplorationSession::advance_overland_colour(std::uint64_t milliseconds) {
    if(!overland_colour_ || !overland_colour_->presented)return;
    auto& presentation=*overland_colour_;
    if(presentation.frame+1<presentation.frames.size()) {
        ++presentation.frame;presentation.presented=false;return; // Never skip an unseen native view.
    }
    // Native3FC3:06B3 ends with two1709:0DBE timer waits. The host fixed
    // policy gives each900ms after actual presentation; campaign time is separate.
    constexpr auto dwell=2*RuntimeTiming::notification_dwell_ms;
    presentation.elapsed+=std::min(milliseconds,dwell-presentation.elapsed);
    if(presentation.elapsed<dwell)return;
    overland_colour_.reset();overland_presented_=false;pump();
}
EclStatus ExplorationSession::pump(std::size_t budget) {
    if(door_pending_)return EclStatus::waiting;
    if(!budget || budget>1000000) throw DecodeError("Invalid exploration execution budget");
    if(party_defeated_)return EclStatus::halted;
    boundary.clear();
    const auto sync_background=[&]() {
        const bool native_defaults=disk()==8 && (record_==16 || record_==13 ||
            (record_==27 && vm.read_value(0x4a9e)==255));
        const bool graveyard=disk()==4 && record_==10 && world.background.has_value();
        if(vm.status()==EclStatus::faulted || (!native_defaults && !graveyard) || !world.map)return true;
        try {
            auto background=ExplorationBackground::rebuild(disk(),record_,vm.state_banks(),vm.script_);
            // During entry/exit retain the resident values set so far; only the
            // sky bank writes need synchronization between source instructions.
            if(graveyard) {
                background->horizon=world.background->horizon;
                background->lower_horizon=world.background->lower_horizon;
                background->floor=world.background->floor;
            }
            if(world.background!=background) {
                auto staged_assets=assets;staged_assets.load_background(files_);
                assets=std::move(staged_assets);world.background=background;
            }
        } catch(const std::exception& error) {
            boundary="Exploration background unresolved: "+std::string(error.what());return false;
        }
        return true;
    };
    std::size_t used=0;
    while(used<budget) {
        if(overland_colour_) {boundary="Presenting overland recovery";return EclStatus::waiting;}
        if(command_notice_) {boundary="Presenting movement feedback";return EclStatus::waiting;}
        if(combat_) {boundary="Combat scaffold: tactical initialization is not implemented";return EclStatus::waiting;}
        if(encounter_notice()) {boundary="Presenting encounter feedback";return EclStatus::waiting;}
        if(encounter_dialogue_ && encounter_dialogue_->result_submitted && !vm.pending_request()) {
            complete_encounter_step(budget-used);return encounter_dialogue_?EclStatus::waiting:vm.status();
        }
        if(effect_notice()) {boundary="Presenting effect feedback";return EclStatus::waiting;}
        if(party_defeated_)return EclStatus::halted;
        if(action_time_) {
            if(!advance_time(*action_time_)) return EclStatus::waiting;
            action_time_.reset();
            if(party_defeated_)return EclStatus::halted;
            retain_committed_overland_checkpoint();
            if(phase_==ExplorationPhase::look_time) {
                vm.seed_value(0x6dca,1);phase_=ExplorationPhase::looking;
            } else phase_=ExplorationPhase::after_move;
            vm.start_entry(1);
            if(effect_notice())return EclStatus::waiting;
        }
        const auto prior=vm.steps();
        // Read-only party queries use the merged participant view. Services
        // which mutate/traverse actor ownership still need tactical bindings.
        if((world.encounter.setup_prepared || world.encounter.duel_context) && !vm.pending_request() &&
           vm.status()!=EclStatus::faulted && vm.status()!=EclStatus::halted &&
           (vm.next_opcode()==0x00 ||
            (vm.next_opcode()==0x0a && (world.encounter.duel_context || world.encounter.special || world.removal_activity_armed || world.removal_name_armed)) ||
            vm.next_opcode()==0x20 || vm.next_opcode()==0x2e ||
            vm.next_opcode()==0x34 || vm.next_opcode()==0x36 || vm.next_opcode()==0x38 || vm.next_opcode()==0x39 || vm.next_opcode()==0x1c)) {
            boundary="Prepared encounter actor mutation requires unimplemented tactical bindings";return EclStatus::waiting;
        }
        // Resolve a previously held palette before allowing another source
        // instruction, including after an instruction-budget yield.
        if(!sync_background())return EclStatus::waiting;
        const bool graveyard=disk()==4 && record_==10;
        const bool single_step=world.encounter.setup_prepared || world.encounter.duel_context || graveyard;
        EclStatus status;
        if(graveyard && !vm.pending_request() && vm.pc()==0x9a99) {
            // Explicit host compatibility for one SAVE C059 -> 4A16. The DOS
            // special getter has no defined return. Preserve the reset resident
            // floor41 instead of inventing a general getter or skipping source.
            // Validate both source helpers and require the first-visit context.
            try {
                (void)ExplorationBackground::rebuild(disk(),record_,vm.state_banks(),vm.script_);
                if(vm.read_value(0x4a00)!=0 || !world.background || world.background->floor!=6)
                    throw DecodeError("Graveyard background preservation context is unavailable");
                struct FloorRead final:IEclBindings {
                    std::optional<std::uint16_t> read_value(std::uint16_t address)const override {
                        return address==0xc059?std::optional<std::uint16_t>(std::uint16_t{0x41}):std::nullopt;
                    }
                } binding;
                auto candidate=vm;candidate.bindings_=&binding;status=candidate.run(1);
                candidate.bindings_=&world;vm=std::move(candidate);
            } catch(const std::exception& error) {
                boundary="Graveyard background unresolved: "+std::string(error.what());return EclStatus::waiting;
            }
        } else status=vm.run(single_step?1:budget-used);
        if(vm.read_value(0x49fb)!=0 || city_mode()) area_view_=false;
        used+=vm.steps()-prior;
        if(vm.steps()!=prior && !sync_background())return EclStatus::waiting;
        // Single stepping here is an ownership fence, not the caller's budget.
        // Continue pure instructions automatically until a real host boundary.
        if(status==EclStatus::budget_exhausted && single_step && used<budget)continue;
        if(status==EclStatus::halted) {
            {auto glyphs=GlyphText(glyph_text_);glyphs.exit(vm.read_value(0x49e6)!=0);glyph_text_=glyphs.encode();}
            // A nested child also completed native EXIT before Encamp opens.
            // Preserve drawn pixels/selectors, but never carry its C8/C9 cache.
            if(nested_camp_active()){sprite_cache_.reset();sprite_closeup_shown_=false;}
            if(update_nested_camp())return status;
            if(phase_==ExplorationPhase::initializing && !overland_entry_colour_done_) {
                try {prepare_overland_colour();}
                catch(const std::exception& error) {
                    boundary="Overland colour setup unresolved: "+std::string(error.what());return EclStatus::waiting;
                }
                if(overland_colour_) {boundary="Presenting overland recovery";return EclStatus::waiting;}
            }
            // Native scripts may switch event handlers while retaining the
            // loaded geometry. Require a fresh source view-mode selection and
            // coherent owned scene/cache provenance, never an inherited flag.
            if(phase_==ExplorationPhase::initializing && link_scene_pending_ && !link_destination_loaded_ &&
               vm.area1_numeric_writes_.test(0xfb) && vm.read_value(0x49fb)==1 && vm.read_value(0x49e6)==1 &&
               link_backdrop_ && !link_backdrop_overland_ && world.map &&
               vm.read_value(0x49c5)==world.map->id && GeoMap::contains(world.x,world.y) &&
               assets.resident_walls_from_disk(world.resource_disk))link_destination_loaded_=true;
            if(link_scene_pending_ && link_destination_loaded_) {
                link_scene_pending_=false;overland_presented_=false;
            }
            // Native EXIT resets C8/C9 cache flags, retaining selectors and
            // the pixels already presented until the next world refresh.
            sprite_cache_.reset();sprite_closeup_shown_=false;
            if(arriving_ && phase_==ExplorationPhase::initializing) {
                arriving_=false;vm.seed_value(0x6dd5,0);
                phase_=ExplorationPhase::arriving;vm.start_entry(0);continue;
            }
            if(phase_==ExplorationPhase::arriving) {
                retain_committed_overland_checkpoint();
                phase_=ExplorationPhase::after_move;vm.start_entry(1);continue;
            }
            if(phase_==ExplorationPhase::before_move) {
                // Original 2182:32f8 records the origin AFTER entry 0 returns.
                vm.seed_value(0x49f0,static_cast<std::uint16_t>(world.x));
                vm.seed_value(0x49f1,static_cast<std::uint16_t>(world.y));
                const int next_x=position_x()+dx[index(world.facing)],next_y=position_y()+dy[index(world.facing)];
                // The legacy C01B/035F bridge is inferred compatibility, not
                // native DOS dispatch. Its raw-tile veto incorrectly seals
                // every Phlan gate before river recovery. At the source's city
                // entrances only, let the independently recovered DOS terrain
                // predicate decide. Retain all other ECL vetoes, raw cells,
                // river permission and the genuine entry1/menu/LOAD sequence.
                const bool gate_approach=city_mode() && record_==26 && city_gate(next_x,next_y) &&
                    world.tile_query_x==next_x && world.tile_query_y==next_y &&
                    world.tile_query_result==city_->tile(next_x,next_y,3) &&
                    OverlandTravel::classify_tile(city_->tile(next_x,next_y,3))==OverlandTerrain::land;
                if(vm.read_value(0x6dc9)>=255 && !gate_approach) { vm.seed_value(0x6dc9,0);last_move.reset(); }
                else if(city_mode()) {
                    if(gate_approach)vm.seed_value(0x6dc9,0);
                    const auto mode=static_cast<unsigned>(record_-23);
                    const int nx=position_x()+dx[index(world.facing)],ny=position_y()+dy[index(world.facing)];
                    // Source regional transfers already ran in entry0. Remaining
                    // true outer edges are blocked, never successful clamp moves.
                    if(nx<0 || nx>15 || ny<0 || ny>35)last_move=MoveResult::map_boundary;
                    else {
                        const auto tile=city_->tile(nx,ny,mode);
                        const auto terrain=OverlandTravel::classify_tile(tile);
                        const bool native_allowed=terrain==OverlandTerrain::land || (terrain==OverlandTerrain::water && vm.read_value(0x4aa2)!=0);
                        bool shared_blocked=false;
                        if(native_allowed && !gate_approach && position_y()>2 && vm.read_value(0x4a9e)!=255) {
                            try {shared_blocked=shared_region_veto(files_,mode,nx,tile,vm.read_value(0x4ab3));}
                            catch(const std::exception& error) {
                                boundary="Shared overland terrain unresolved: "+std::string(error.what());return EclStatus::waiting;
                            }
                        }
                        if(native_allowed && !shared_blocked) {
                            vm.seed_value(0x49c3,static_cast<std::uint16_t>(nx));vm.seed_value(0x49c4,static_cast<std::uint16_t>(ny));last_move=MoveResult::moved;
                        } else {
                            last_move=MoveResult::solid_wall;
                            if(terrain==OverlandTerrain::water && !native_allowed) {
                                // Native3635:0818 presents1709:0D68 before
                                // returning to the outer redraw/entry1 path.
                                command_notice_=ExplorationCommandNotice{pascal_string_id("GAME.OVR",0x16ebf),14};
                                command_notice_presented_=false;command_notice_elapsed_=0;
                                phase_=ExplorationPhase::movement_notice;
                                boundary="Presenting movement feedback";return EclStatus::waiting;
                            }
                        }
                    }
                } else {
                    if(!door_resolved_ && world.map->door(world.x,world.y,world.facing)>=2) {
                        if(door_choices().size()>1) {
                            door_pending_=true;door_presented_=false;door_selection_=0;
                            boundary="Locked door";return EclStatus::waiting;
                        }
                        // Native0D6D's Exit-only branch clears this edge's door
                        // flags (leaving its wall), without a choice or step.
                        auto& bits=world.map->planes[768+static_cast<std::size_t>(world.y*16+world.x)];
                        bits=static_cast<std::uint8_t>(bits&~(3u<<(index(world.facing)*2)));
                        door_resolved_=true;door_success_=false;
                    }
                    if(door_resolved_) {
                        last_move=MoveResult::locked_door;
                        if(door_success_) {
                            // Knock permits this step but does not rewrite door bits.
                            const int nx=world.x+dx[index(world.facing)],ny=world.y+dy[index(world.facing)];
                            if(GeoMap::contains(nx,ny)){world.x=nx;world.y=ny;last_move=MoveResult::moved;}
                            else last_move=MoveResult::map_boundary;
                        }
                        door_resolved_=false;door_success_=false;
                    }else last_move=world.move();
                    if(last_move==MoveResult::moved)world.door_bash=world.door_pick=world.door_knock=true;
                }
                refresh_world_art(); // Scene refresh between the step and entry 1.
                if(campaign_time_bound_ && last_move==MoveResult::moved) {
                    // Native movement updates coordinates, then time, before entry 1.
                    phase_=ExplorationPhase::movement_time;
                    action_time_=city_mode()?wilderness_step_time(qol_upgrades_):
                        TimeIncrement{vm.read_value(0x6dca)&1?TimeUnit::ten_minutes:TimeUnit::minute,1};
                    continue;
                }
                retain_committed_overland_checkpoint();
                phase_=ExplorationPhase::after_move;vm.start_entry(1);
                continue;
            }
            if(phase_==ExplorationPhase::looking) vm.seed_value(0x6dca,saved_search_);
            phase_=ExplorationPhase::idle;
            if(link_scene_pending_)boundary="Destination has no completed world presentation; source scene retained";
            // Host proof of a setup reset in this event, not a recovered
            // persistent flag. Idle save/resume must not depend on this proof.
            if(!world.encounter.setup_prepared)world.encounter.initialized=false;
            combat_checkpoint_.reset();
            return status;
        }
        if(status!=EclStatus::waiting) {
            if(status==EclStatus::faulted) boundary=vm.error();
            else if(status==EclStatus::budget_exhausted) boundary="Script budget exhausted: Enter to resume";
            return status;
        }
        const auto r=*vm.pending_request();
        if(r.kind==EclRequestKind::encounter_dialogue) {
            stage_encounter_dialogue(r);return status;
        }
        if(r.kind==EclRequestKind::add_monsters) {
            try {
                if(!combat_checkpoint_)throw DecodeError("Encounter setup requires a retained exploration checkpoint");
                auto staged=world.encounter;auto party=world.party;auto next_vm=vm;
                const auto expected=vm.random_state();auto random=expected;
                if(staged.count<63) {
                    const auto id=static_cast<std::uint8_t>(vm.preview_encounter_parameter(r.token,0));
                    EncounterService::add_group(staged,party,files_,disk(),id,random,[&] {
                        const auto quantity=static_cast<std::uint8_t>(vm.preview_encounter_parameter(r.token,1));
                        const auto art=static_cast<std::uint8_t>(vm.preview_encounter_parameter(r.token,2));
                        return std::array<std::uint8_t,2>{quantity,art};
                    });
                }
                next_vm.answer_encounter_setup(r.token,expected,random);
                world.encounter=std::move(staged);world.party=std::move(party);vm=std::move(next_vm);continue;
            } catch(const DecodeError& error) {
                boundary="Encounter setup unresolved: "+std::string(error.what());
                block_combat_setup(boundary);return status;
            }
        }
        if(r.kind==EclRequestKind::setup_sprite || r.kind==EclRequestKind::approach_sprite) {
            stage_sprite(r);return status;
        }
        if(r.kind==EclRequestKind::sprite_off && sprite_active_) {
            if(!picture_token_) {refresh_world_art();sprite_active_=false;picture_token_=r.token;}
            boundary="Presenting exploration sprite removal";return status;
        }
        if(r.kind==EclRequestKind::add_npc) {
            try {
                auto staged=world;auto next_vm=vm;const auto expected=vm.random_state();auto random=expected;
                if(vm.read_value(0x6e3e)!=staged.party.size()) throw DecodeError("NPC roster count disagrees with native state");
                const auto template_id=static_cast<std::uint8_t>(r.values.at(0));
                const auto loaded=NpcService::append(staged.party,files_,disk(),template_id,random);
                if(!loaded.applied()) {boundary=loaded.message;return status;}
                const auto parameter=static_cast<std::uint8_t>(vm.preview_party_parameter(r.token,staged));
                const auto finished=NpcService::finish(staged.party,template_id,parameter);
                if(!finished.applied()) {boundary=finished.message;return status;}
                const std::array<EclStateUpdate,1> updates{{{0x6e3e,static_cast<std::uint16_t>(staged.party.size())}}};
                next_vm.answer_party_request(r.token,expected,random,updates);
                world=std::move(staged);vm=std::move(next_vm);continue;
            } catch(const DecodeError& error) {boundary="NPC service unresolved: "+std::string(error.what());return status;}
        }
        if(r.kind==EclRequestKind::select_current || r.kind==EclRequestKind::restore_current || r.kind==EclRequestKind::engine_service) {
            try {
                auto staged=world;auto next_vm=vm;const auto random=vm.random_state();
                if((r.kind!=EclRequestKind::engine_service || vm.selection_to_restore()) &&
                   (!staged.party.size() || !staged.party.selected_index())) throw DecodeError("Party service requires a selected imported member");
                const auto restore=[&] {
                    staged.encounter.selected_actor.reset();
                    if(const auto identity=vm.selection_to_restore()) {
                        std::optional<std::size_t> found;
                        for(std::size_t i=0;i<staged.party.size();++i) if(staged.party.member(i).byte(0xbf)==*identity) {
                            if(found) throw DecodeError("Ambiguous original character cache identity");
                            found=i;
                        }
                        if(!found) throw DecodeError("Original character no longer belongs to the party");
                        staged.party.select(*found);
                    }
                };
                if(r.kind==EclRequestKind::select_current) {
                    auto value=r.values.at(0);
                    if(staged.encounter.setup_prepared) {
                        staged.select_prepared_participant(static_cast<std::uint8_t>(value&0x7f));
                        next_vm.answer_party_request(r.token,random,random);
                        world=std::move(staged);vm=std::move(next_vm);continue;
                    }
                    if(value&0x80) {
                        if(staged.encounter.setup_prepared || staged.encounter.duel_context || staged.encounter.special ||
                           !staged.encounter.groups.empty())
                            throw DecodeError("SELECT_CURRENT removal in an encounter participant context remains unresolved");
                        value&=0x7f;
                    }
                    if(!vm.invocation_selection()) throw DecodeError("Original selection is unavailable");
                    if(value<staged.party.size()) staged.party.select(value);
                    if((r.values.at(0)&0x80) && staged.removal_activity_armed && staged.removal_name_armed) {
                        if(staged.party.size()==1)throw DecodeError("Native last-member removal refreshes an empty selection; undefined context remains held");
                        bool active=false;for(std::size_t i=0;i<staged.party.size();++i)if(i!=*staged.party.selected_index() && staged.party.member(i).active())active=true;
                        if(!active)throw DecodeError("Last-active-member scripted removal requires terminal ownership");
                        const auto removed=staged.party.member(*staged.party.selected_index()).byte(0xbf);
                        const auto result=RosterService::drop(staged.party,true);
                        if(!result.applied())throw DecodeError(result.message);
                        staged.removal_activity_armed=staged.removal_name_armed=false;
                        const auto expected=vm.random_state();
                        const std::array<EclStateUpdate,1> updates{{{0x6e3e,static_cast<std::uint16_t>(staged.party.size())}}};
                        next_vm.answer_party_request(r.token,expected,expected,updates);
                        if(vm.invocation_selection()==removed){next_vm.restore_selection_=false;next_vm.invocation_selection_.reset();}
                        world=std::move(staged);vm=std::move(next_vm);continue;
                    }
                } else restore();
                if(r.kind==EclRequestKind::engine_service) {
                    const auto mode=static_cast<std::uint8_t>(vm.preview_party_parameter(r.token,staged));
                    if(mode==0) {
                        // Native38 restores invocation selection before CALE.
                        // Keep the typed request throughout the owned menu.
                        next_vm.restore_selection_=false;
                        world=std::move(staged);vm=std::move(next_vm);
                        boundary="Character services";return status;
                    }
                    if(mode==8 || mode==9) {
                        next_vm.restore_selection_=false;world=std::move(staged);vm=std::move(next_vm);
                        if(mode==8){boundary="Ending presentation";return status;}
                        if(!stage_nested_camp())throw DecodeError("Nested camp requires a supported suspended invocation");
                        continue;
                    }
                    // All other native selector bytes return without a service.
                }
                next_vm.answer_party_request(r.token,random,random);
                world=std::move(staged);vm=std::move(next_vm);continue;
            } catch(const DecodeError& error) {boundary="Party service unresolved: "+std::string(error.what());return status;}
        }
        if(r.kind==EclRequestKind::clear_treasure || r.kind==EclRequestKind::treasure_setup) {
            auto staged=campaign_;auto staged_vm=vm;auto random=vm.random_state();const auto prior_random=random;
            auto encounter=world.encounter;
            if(r.kind==EclRequestKind::clear_treasure) {TreasureService::clear(staged);encounter.clear();}
            else {
                if(r.values.size()!=8) throw DecodeError("Invalid treasure setup request");
                // Preview fixtures also have decoded item catalogs after import.
                if(!world.party.size()) {boundary="Treasure requires an imported item catalog";return status;}
                const auto result=TreasureService::prepare(staged,world.party.item_catalog(),disk(),std::span<const std::uint16_t,8>(r.values.data(),8),random);
                if(!result.applied()) {boundary=result.message;return status;}
            }
            staged_vm.answer_campaign_request(r.token,prior_random,random);
            campaign_=std::move(staged);world.encounter=std::move(encounter);vm=std::move(staged_vm);continue;
        }
        if(r.kind==EclRequestKind::remove_wealth) {
            if(r.values.size()!=2) throw DecodeError("Invalid wealth loss request");
            auto staged_party=world.party;auto staged_vm=vm;auto random=vm.random_state();const auto prior_random=random;
            const auto result=WealthLossService::apply(staged_party,static_cast<std::uint8_t>(r.values[0]),static_cast<std::uint8_t>(r.values[1]),random);
            if(!result.applied()) {boundary=result.message;return status;}
            staged_vm.answer_campaign_request(r.token,prior_random,random);
            world.party=std::move(staged_party);vm=std::move(staged_vm);
            check_party_defeat();if(party_defeated_)return EclStatus::halted;
            continue;
        }
        if(r.kind==EclRequestKind::campaign_service) {
            if(world.encounter.setup_prepared || world.encounter.duel_context)begin_combat();
            else boundary.clear(); // Normal modal handoff is not a gameplay error.
            return status;
        }
        if(r.kind==EclRequestKind::advance_time) {
            if(!advance_time({TimeUnit::minute,static_cast<std::uint8_t>(r.values.at(0))},r.token)) return status;
            continue;
        }
        if(r.kind==EclRequestKind::damage) {
            try {
                auto staged_party=world.party;
                auto staged_vm=vm;
                std::vector<DamageOutcome> outcomes;
                const auto defeated=[&]{for(std::size_t i=0;i<staged_party.size();++i)if(staged_party.member(i).active())return false;return true;};
                const auto expected=vm.random_state();auto random=expected;
                outcomes=ScriptDamageService::attacks(staged_party,r.values,random,
                    ScriptDamageService::save_context(disk(),record_,r.address,vm.script_));
                staged_vm.answer_attack_damage(r.token,expected,random,defeated());
                const bool party_defeated=defeated();
                world.party=std::move(staged_party);vm=std::move(staged_vm);
                last_damage.reset();
                for(auto& outcome:outcomes) {
                    last_damage=std::move(outcome);
                    if(last_damage->message.empty())continue;
                    if(messages.size()==256) messages.erase(messages.begin());
                    messages.push_back(last_damage->message);
                    display_text+='\n';display_text+=last_damage->message;
                    if(display_text.size()>4096) display_text.erase(0,display_text.size()-4096);
                }
                if(party_defeated) {
                    check_party_defeat();return EclStatus::halted;
                }
            } catch(const std::exception& error) {
                boundary="Damage unresolved: "+std::string(error.what());return status;
            }
            continue;
        }
        if(r.kind==EclRequestKind::load_ecl) {
            if(nested_camp_active()){boundary="Nested camp script transfer requires owned child lifecycle";return status;}
            // Prior source coordinate/resource writes are already committed.
            // Hold the owned source pixels even if destination validation fails;
            // retry keeps this exact typed request rather than replaying entry0.
            if(!link_scene_pending_) {link_scene_pending_=true;link_destination_loaded_=false;}
            try {
                const auto id=static_cast<std::uint8_t>(r.values.at(0));
                const auto script=load_record(files_,"ECL"+std::to_string(world.resource_disk)+".DAX",id);
                auto staged=vm;staged.replace_script(r.token,script,record_);
                vm=std::move(staged);world.removal_activity_armed=world.removal_name_armed=false;world.background.reset();
                record_=id;display_text.clear();link_scene_pending_=true;link_destination_loaded_=false;
                overland_entry_colour_done_=false;
                refresh_world_art();sprite_cache_.reset();sprite_closeup_shown_=false;
                picture_token_.reset();menu_token_=0;character_token_=0;
                last_move.reset();phase_=ExplorationPhase::initializing;arriving_=true;
            } catch(const std::exception& error) {
                boundary="Map transition failed: "+std::string(error.what());return status;
            }
            continue;
        }
        if(r.kind==EclRequestKind::picture) {
            if(picture_token_) {boundary="Presenting picture";return status;}
            const auto id=static_cast<std::uint8_t>(r.values.at(0));
            glyph_text_.clear();
            if(id==255) {refresh_world_art();sprite_active_=false;sprite_cache_.reset();sprite_closeup_shown_=false;}
            else {
                try {
                    auto staged=PicturePresentation::load(files_,world.resource_disk,id,static_cast<std::uint8_t>(vm.read_value(0x6de1)));
                    picture_=std::move(staged);
                    sprite_.reset();sprite_closeup_shown_=true;
                } catch(const std::exception& error) {
                    boundary="Picture load failed: "+std::string(error.what());return status;
                }
            }
            picture_token_=r.token;boundary="Presenting picture";return status;
        }
        if(r.kind==EclRequestKind::glyph_table) {
            try {
                auto staged_font=font_?*font_:GameFont::load(files_);
                auto glyphs=GlyphText(glyph_text_);glyphs.append(r.values,display_text);
                font_=std::move(staged_font);
                glyph_text_=glyphs.encode();
                vm.complete_request(r.token);continue;
            } catch(const DecodeError& e) {boundary=std::string("Character table unresolved: ")+e.what();return status;}
        }
        if(r.kind==EclRequestKind::input_number || r.kind==EclRequestKind::input_string) {
            if(input_token_!=r.token) {input_token_=r.token;input_buffer_.clear();}
            boundary=r.kind==EclRequestKind::input_number?"Enter number (6 chars), Backspace, Enter":"Enter text (40 chars), Backspace, Enter";
            return status;
        }
        if(r.kind==EclRequestKind::menu) {
            if(menu_token_!=r.token) {
                menu_token_=r.token;menu_selection_=0;
                if(r.menu->vertical) display_text=r.menu->prompt;
            }
            boundary=r.menu->vertical?"Choose a line: arrows / Page Up/Down / Enter":"Choose an option: arrows / hotkey / Enter";return status;
        }
        if(r.kind==EclRequestKind::select_character) {
            display_text=r.text;
            if(character_token_!=r.token) {
                character_token_=r.token;character_selection_=world.party.selected_index().value_or(0);
            }
            boundary=world.party.size()?"Choose a party member: arrows / Enter / 1-6":"No party imported: restart with --party A or J";
            return status;
        }
        try {
        auto staged_world=world;auto staged_assets=assets;
        bool redraw_world=false,scene_loaded=false;
        std::vector<EclStateUpdate> patches;
        if(r.kind==EclRequestKind::print) {
            if(!stage_text(r)) {boundary="Presenting dialogue page";return EclStatus::waiting;}
        }
        else if(r.kind==EclRequestKind::clear_box) {
            // Native2182:2B14 clears cells(1,1)..(15,15), retaining the
            // dialogue cursor/text. A nonprinting owned marker also preserves
            // this viewport-clear state through schema6's existing glyph field.
            auto glyphs=GlyphText(glyph_text_);glyphs.clear_box(display_text);glyph_text_=glyphs.encode();
        }
        else if(r.kind==EclRequestKind::sprite_off) {
            // Native31 is inert without the active SPRIT flag, even if an
            // unrelated PICTURE remains visible. Active removal yields above.
        }
        else if(r.kind==EclRequestKind::native_call && (r.values.at(0)==0x8000 || r.values.at(0)==0x8001)) {
            if(!combat_checkpoint_ || !world.party.selected_index()) {
                boundary="Special combat requires a retained exploration checkpoint and selected character";return status;
            }
            auto proposal=SpecialCombatService::stage(files_,world.resource_disk,world.party,world.encounter,
                *world.party.selected_index(),r.values.at(0)==0x8000);
            auto next_vm=vm;next_vm.answer_special_combat_setup(r.token,vm.random_state());
            world.party=std::move(proposal.party);world.encounter=std::move(proposal.encounter);vm=std::move(next_vm);
            continue;
        }
        else if(r.kind==EclRequestKind::native_call && r.values.at(0)==0xc01e) {
            const auto facing=index(staged_world.facing);
            staged_world.x=(staged_world.x+dx[facing]+16)%16;
            staged_world.y=(staged_world.y+dy[facing]+16)%16;
        }
        else if(r.kind==EclRequestKind::native_call && native_inert_call(r.values.at(0))) {
            // No matching case in this build's native CALL dispatcher.
        }
        else if(r.kind==EclRequestKind::native_call && r.values.at(0)==0xba03) {
            // Native2182:2CCF examines resident84CC (our call_parameters[0]):
            // 8/10 select DS2628/262A through18AE:00DC, otherwise return.
            // Queue cosmetic source metadata; the owning mode submits it only
            // after the resulting scene has actually presented. No VM wait,
            // campaign time or RNG is introduced by unavailable/muted audio.
            const auto selector=staged_world.call_parameters[0];
            if(selector && (*selector==8 || *selector==10)) {
                if(script_sounds_.size()==256)script_sounds_.erase(script_sounds_.begin());
                script_sounds_.emplace_back(r.token,static_cast<std::uint8_t>(*selector));
            }
        }
        else if(r.kind==EclRequestKind::native_call && r.values.at(0)==0xc01b && city_mode()) {
            // Deliberate host compatibility policy: original ECL queries this
            // interface, but this build's CALL dispatcher has no C01B branch.
            // Bind its script intent to owned map data; do not claim DOS parity.
            const auto signed_word=[](std::uint16_t v){return v<32768?static_cast<int>(v):static_cast<int>(v)-65536;};
            staged_world.tile_query_result=city_->tile(std::clamp(signed_word(staged_world.tile_query_x),0,15),std::clamp(signed_word(staged_world.tile_query_y),0,35),record_-23);
        }
        else if(r.kind==EclRequestKind::native_call && r.values.at(0)==0xc018 && city_mode()) {
            const auto x=staged_world.tile_query_x,y=staged_world.tile_query_y;
            if(x>15 || y>35 || staged_world.tile_query_value==0 || staged_world.tile_query_value>255) {
                boundary="Unsupported city tile update";return status;
            }
            auto next_city=*city_;auto next_vm=vm;
            next_city.cells.at(static_cast<std::size_t>(y*44+x+(record_-25)*13))=static_cast<std::uint8_t>(staged_world.tile_query_value);
            next_vm.complete_request(r.token);
            city_=std::move(next_city);vm=std::move(next_vm);overland_presented_=false;continue;
        }
        else if(r.kind==EclRequestKind::native_call && r.values.at(0)==0x2c90 && city_mode()) {redraw_world=true;}
        else if(r.kind==EclRequestKind::native_call && staged_world.map &&
                (r.values.at(0)==0x2c90 || r.values.at(0)==0xc018)) {
            // Recovered scene/front-wall refresh. Queries are derived from the
            // current geometry and every SDL frame renders the current scene.
            (void)staged_world.map->wall(staged_world.x,staged_world.y,staged_world.facing);
            redraw_world=r.values.at(0)==0x2c90;
        }
        else if(r.kind==EclRequestKind::write_value &&
                (r.values.at(0)==0x6e22 || r.values.at(0)==0x6e24 || r.values.at(0)==0x6e26)) {
            const auto value=r.values.at(1);
            if(value>128) staged_assets.load_walls(files_,world.resource_disk,(r.values.at(0)-0x6e22)/2+1,static_cast<std::uint8_t>(value&127));
        }
        else if(r.kind==EclRequestKind::write_value && disk()==4 && record_==10 &&
                (r.values.at(0)==0xc059 || r.values.at(0)==0xc05f)) {
            const auto background=ExplorationBackground::rebuild(disk(),record_,vm.state_banks(),vm.script_);
            const auto value=r.values.at(1);
            const bool horizon=r.values[0]==0xc05f;
            if(value>255 || (horizon && (r.address!=0x9a8c || value!=8)) ||
               (!horizon && !((r.address==0x9aa0 && value==11) ||
                              (r.address==0x992b && value==vm.read_value(0x4a16)))))
                throw DecodeError("Background write is outside the recovered Graveyard source helpers");
            staged_assets.load_background(files_);
            auto current=staged_world.background.value_or(ExplorationBackground{});
            current.sky=background->sky;current.alternate_sky=background->alternate_sky;
            if(horizon){current.horizon=0;current.lower_horizon=8;}
            else current.floor=ExplorationBackground::floor_color(static_cast<std::uint8_t>(value));
            staged_world.background=current;
        }
        else if(r.kind==EclRequestKind::write_value && world.resource_disk==8 && record_==29 &&
                (r.values.at(0)==0xc059 || r.values.at(0)==0xc05f)) {
            const auto background=ExplorationBackground::rebuild(disk(),record_,vm.state_banks(),vm.script_);
            const auto value=static_cast<std::uint8_t>(r.values.at(1));
            const bool horizon=r.values[0]==0xc05f;
            if((horizon && ((r.address!=0xaf52 && r.address!=0xaf59) || value!=(vm.read_value(0x4a10)==1?8:65))) ||
               (!horizon && (r.address!=0xaf66 || value!=vm.read_value(static_cast<std::uint16_t>(0xafed+static_cast<std::uint8_t>(vm.read_value(0x4adb)))))))
                throw DecodeError("Background write is outside the recovered Kuto source helper");
            staged_assets.load_background(files_);
            auto current=staged_world.background.value_or(ExplorationBackground{});
            current.sky=background->sky;current.alternate_sky=background->alternate_sky;
            if(horizon){current.horizon=0;current.lower_horizon=value==8?8:7;}
            else current.floor=ExplorationBackground::floor_color(value);
            staged_world.background=current;
        }
        else if(r.kind==EclRequestKind::write_value && native_inert_tile_write(r.values.at(0))) {
            // Preserve each typed request/continuation; no state owns these
            // ignored writes in the selected native build.
        }
        else if(r.kind==EclRequestKind::write_value && staged_world.write_register(r.values.at(0),r.values.at(1))) {}
        else if(r.kind==EclRequestKind::load_files) {
            const auto id=static_cast<std::uint8_t>(r.values.at(0));
            // A real subsequent scene request revokes any retained-scene proof.
            // Keep the source backdrop through failures and incomplete loaders.
            if(vm.read_value(0x49e6)==0 || (id!=255 && id!=127)) {
                link_scene_pending_=true;link_destination_loaded_=false;
            }
            if(vm.read_value(0x49e6)==0) {
                if(record_<25 || record_>27 || r.values.at(2)!=0) {boundary="Unsupported city LOAD_FILES";return status;}
                try {
                    auto staged_city=city_?*city_:CityAssets::load(files_);
                    staged_city.load_replacement_tiles(files_);
                    auto next_vm=vm;next_vm.complete_request(r.token);
                    city_=std::move(staged_city);world.tile_query_available=true;vm=std::move(next_vm);
                    overland_presented_=false;link_destination_loaded_=true;
                }
                catch(const std::exception& error) {boundary="City load failed: "+std::string(error.what());return status;}
                continue;
            }
            if(id!=255 && id!=127) {
                staged_world.map=GeoMap::decode(load_record(files_,"GEO"+std::to_string(world.resource_disk)+".DAX",id),id);
                patches={{0x49c5,id},{0x6dc9,0}};
                scene_loaded=true;
            }
        } else if(r.kind==EclRequestKind::load_pieces) {
            if(r.values.at(0)==127) staged_assets.load_walls(files_,world.resource_disk,1,0);
            else {
                for(unsigned slot=1;slot<=3;++slot) {
                    // Native2182:0C74 skips operand2 only when both flags are
                    // set. Operand1's two-bank WALLDEF fills slots1+2 itself.
                    if(slot==2 && vm.read_value(0x49e7)!=0 && vm.read_value(0x49e8)!=0)continue;
                    const auto id=static_cast<std::uint8_t>(r.values.at(slot-1));
                    // Native 255 invalidates the cache key; it does not erase retained tiles.
                    if(id!=255) staged_assets.load_walls(files_,world.resource_disk,slot,id);
                }
            }
        } else {
            boundary=r.kind==EclRequestKind::delay?"Timed script pause":
                "Unimplemented exploration service: "+std::string(ecl_request_name(r.kind));
            return status;
        }
        if(r.kind==EclRequestKind::print)complete_text_request(r.token);
        else vm.complete_request(r.token,patches);
        if(redraw_world) {refresh_world_art();sprite_active_=false;}
        world=std::move(staged_world);assets=std::move(staged_assets);
        if(scene_loaded)link_destination_loaded_=true;
        } catch(const std::exception& error) {
            boundary="Exploration service failed: "+std::string(error.what());return status;
        }
    }
    boundary="Script budget exhausted: Enter to resume";
    return EclStatus::budget_exhausted;
}
bool ExplorationSession::cycle_character(int delta) {
    if(!can_act() || !world.party.size()) return false;
    const auto count=static_cast<int>(world.party.size());
    const auto selected=static_cast<int>(active_character().value_or(0));
    world.party.select(static_cast<std::size_t>((selected+delta%count+count)%count));return true;
}
bool ExplorationSession::toggle_area_view() {
    if(!can_act()) return false;
    if(city_mode() || !world.map || !GeoMap::contains(world.x,world.y) || vm.read_value(0x49fb)!=0) {area_view_=false;return false;}
    area_view_=!area_view_;
    refresh_world_art(); // Native A redraws the world, replacing prior idle art.
    return true;
}
bool ExplorationSession::toggle_search() {
    if(!can_act()) return false;
    vm.seed_value(0x6dca,static_cast<std::uint16_t>(vm.read_value(0x6dca)^1));return true;
}
bool ExplorationSession::forward(std::size_t budget) {
    if(!can_act()) return false;
    if(!budget || budget>1000000) throw DecodeError("Invalid exploration execution budget");
    const auto d=index(world.facing);
    if(!city_mode() && !GeoMap::contains(world.x,world.y)) return false;
    if(!retain_exploration_checkpoint())return false;
    // Dismiss the previous step before entry0 can append new narration.
    display_text.clear();acknowledge_dialogue();
    vm.seed_value(0x49f2,record_); // Native outer input loop updates the active ECL ID.
    vm.seed_value(0x6dc9,0);vm.seed_value(0x6dd5,0);
    if(!city_mode() && world.map->door(world.x,world.y,world.facing)!=0 &&
       !GeoMap::contains(world.x+dx[d],world.y+dy[d])) vm.seed_value(0x6dd5,1);
    last_move.reset();phase_=ExplorationPhase::before_move;
    vm.start_entry(0);pump(budget);return true;
}
std::vector<char> ExplorationSession::door_choices()const {return DoorService::choices(world);}
bool ExplorationSession::door_input(InputEvent event) {
    if(!door_pending_)return false;
    if(!event.pressed || !door_presented_)return true;
    if(event.key==Key::combat_abort){abort_door();return true;}
    const auto choices=door_choices();
    if(event.key==Key::left || event.key==Key::up) {door_selection_=(door_selection_+choices.size()-1)%choices.size();return true;}
    if(event.key==Key::right || event.key==Key::down) {door_selection_=(door_selection_+1)%choices.size();return true;}
    char action=static_cast<char>(event.character);
    if(action>='a' && action<='z')action=static_cast<char>(action-'a'+'A');
    if(event.key==Key::cancel)action='E';
    if(event.key==Key::accept)action=choices.at(door_selection_);
    if(std::find(choices.begin(),choices.end(),action)==choices.end())return true;
    auto candidate=world;const auto expected=vm.random_state();auto random=expected;
    const bool success=DoorService::apply(candidate,random,action);
    vm.commit_random_state(expected,random);world=std::move(candidate);
    door_pending_=false;door_presented_=false;door_resolved_=true;door_success_=success;
    pump();return true;
}
bool ExplorationSession::turn(int quarters) {
    if(!can_act()) return false;
    world.turn(quarters);last_move.reset();refresh_world_art();return true;
}
bool ExplorationSession::look(std::size_t budget) {
    if(!can_act()) return false;
    if(!budget || budget>1000000) throw DecodeError("Invalid exploration execution budget");
    if(!retain_exploration_checkpoint())return false;
    saved_search_=vm.read_value(0x6dca)&1;
    vm.seed_value(0x49f2,record_);
    last_move.reset();
    refresh_world_art();
    vm.seed_value(0x6dc9,0);vm.seed_value(0x6dca,1);
    if(campaign_time_bound_) {
        // Timing precedes entry 1; failed expiry cannot start it or charge twice.
        vm.seed_value(0x6dca,saved_search_);
        phase_=ExplorationPhase::look_time;action_time_=TimeIncrement{TimeUnit::ten_minutes,1};pump(budget);return true;
    }
    phase_=ExplorationPhase::looking;vm.start_entry(1);pump(budget);return true;
}
bool ExplorationSession::continue_dialogue(std::size_t budget) {
    if(command_notice_ || overland_colour_)return false;
    if(effect_notice())return false;
    if(party_defeated_) return false;
    if(action_time_) {pump(budget);return true;}
    if(selecting_character()) return false;
    if(menu()) return choose_menu(vm.pending_request()->token,menu_selection_,budget);
    if(vm.status()==EclStatus::budget_exhausted ||
       vm.status()==EclStatus::ready) {
        pump(budget);return true;
    }
    if(!vm.pending_request() || vm.pending_request()->kind!=EclRequestKind::delay) return false;
    if(!budget || budget>1000000) throw DecodeError("Invalid exploration execution budget");
    vm.complete_request(vm.pending_request()->token);pump(budget);return true;
}
void ExplorationSession::advance_command_notice(std::uint64_t milliseconds) {
    if(!command_notice_ || !command_notice_presented_)return;
    constexpr auto dwell=RuntimeTiming::notification_dwell_ms;
    command_notice_elapsed_+=std::min(milliseconds,dwell-command_notice_elapsed_);
    if(command_notice_elapsed_<dwell)return;
    if(phase_!=ExplorationPhase::movement_notice || vm.status()!=EclStatus::halted || vm.pending_request())
        throw DecodeError("Movement feedback lost its source continuation");
    command_notice_.reset();command_notice_elapsed_=0;command_notice_presented_=false;
    refresh_world_art();retain_committed_overland_checkpoint();
    phase_=ExplorationPhase::after_move;vm.start_entry(1);pump();
}
void ExplorationSession::advance_effect_notice(std::uint64_t milliseconds) {
    if(!effect_notice() || !effect_notice_presented_)return;
    constexpr auto dwell=RuntimeTiming::notification_dwell_ms;
    effect_notice_elapsed_+=std::min(milliseconds,dwell-std::min(dwell,effect_notice_elapsed_));
    if(effect_notice_elapsed_<dwell)return;
    effect_notice_elapsed_=0;effect_notice_presented_=false;
    if(++effect_notice_index_<effect_notices_.size())return;
    effect_notices_.clear();effect_notice_index_=0;pump();
}
bool ExplorationSession::choose_menu(std::uint64_t token,std::size_t selection,std::size_t budget) {
    if(!menu()) return false;
    if(!budget || budget>1000000) throw DecodeError("Invalid exploration execution budget");
    if(encounter_dialogue_)return choose_encounter_menu(token,selection,budget);
    const bool vertical=menu()->vertical;
    vm.answer_menu(token,selection);acknowledge_dialogue();
    if(vertical) display_text.clear(); // Native 2182:0f08 clears its text box on return.
    pump(budget);return true;
}
bool ExplorationSession::entry_input(InputEvent event,std::size_t budget) {
    if(!entering_input() || !event.pressed) return false;
    if(!budget || budget>1000000) throw DecodeError("Invalid exploration execution budget");
    if(event.key==Key::backspace) {if(!input_buffer_.empty()) input_buffer_.pop_back();return true;}
    if(event.key==Key::accept) {
        try {vm.answer_input(vm.pending_request()->token,input_buffer_);}
        catch(const DecodeError& e) {boundary=e.what();return true;}
        acknowledge_dialogue();input_buffer_.clear();pump(budget);return true;
    }
    const auto limit=vm.pending_request()->kind==EclRequestKind::input_number?6u:40u;
    const auto c=event.character;
    if(c>=32 && c<=122 && input_buffer_.size()<limit) input_buffer_.push_back(static_cast<char>(c));
    return true;
}
bool ExplorationSession::menu_input(InputEvent event,std::size_t budget) {
    const auto* current=menu();if(!current || !event.pressed) return false;
    const auto count=current->labels.size();
    switch(event.key) {
    case Key::up:case Key::left:menu_selection_=(menu_selection_+count-1)%count;return true;
    case Key::down:case Key::right:menu_selection_=(menu_selection_+1)%count;return true;
    case Key::accept:choose_menu(vm.pending_request()->token,menu_selection_,budget);return true;
    case Key::previous:if(current->vertical) menu_selection_=menu_selection_>=3?menu_selection_-3:0;return true;
    case Key::next:if(current->vertical) menu_selection_=std::min(menu_selection_+3,count-1);return true;
    default:break;
    }
    auto key=event.character;if(key>='a' && key<='z') key=static_cast<std::uint8_t>(key-'a'+'A');
    if(current->vertical) {
        if(key=='P') menu_selection_=menu_selection_>=3?menu_selection_-3:0;
        else if(key=='N') menu_selection_=std::min(menu_selection_+3,count-1);
        return true;
    }
    const auto it=std::find(current->hotkeys.begin(),current->hotkeys.end(),key);
    if(it!=current->hotkeys.end()) choose_menu(vm.pending_request()->token,static_cast<std::size_t>(it-current->hotkeys.begin()),budget);
    return true; // Unrecognized menu input must not become exploration input.
}
bool ExplorationSession::choose_character(std::uint64_t token,std::size_t index,std::size_t budget) {
    if(!selecting_character()) return false;
    if(!budget || budget>1000000) throw DecodeError("Invalid exploration execution budget");
    if(vm.pending_request()->token!=token) throw DecodeError("Stale character-selection answer");
    auto staged=world.party;staged.select(index);
    vm.complete_request(token);acknowledge_dialogue();world.party=std::move(staged);pump(budget);return true;
}
bool ExplorationSession::character_input(InputEvent event,std::size_t budget) {
    if(!selecting_character() || !event.pressed) return false;
    const auto count=world.party.size();if(!count) return true;
    switch(event.key) {
    case Key::home:case Key::up:case Key::left:character_selection_=(character_selection_+count-1)%count;return true;
    case Key::end:case Key::down:case Key::right:character_selection_=(character_selection_+1)%count;return true;
    case Key::accept:choose_character(vm.pending_request()->token,character_selection_,budget);return true;
    default:break;
    }
    if(event.character>='1' && event.character<='6' && static_cast<std::size_t>(event.character-'1')<count)
        choose_character(vm.pending_request()->token,event.character-'1',budget);
    return true;
}
void ExplorationSession::draw_view(Framebuffer& frame) const {
    if(overland_colour_)frame=overland_colour_->frames.at(overland_colour_->frame);
    else if(picture_) picture_->draw(frame);
    else if(link_backdrop_ && (link_scene_pending_ || (!world.map && !city_mode())))frame=*link_backdrop_;
    else if(city_mode()) {
        // Native4CB9 applies campaign substitutions only while drawing. The
        // owned raw cells remain authoritative for3635 collision and ECL queries.
        const OverlandCampaignState state{vm.read_value(0x4a8c),vm.read_value(0x4ad2),
            vm.read_value(0x4a7c),vm.read_value(0x4a9f),vm.read_value(0x4aa0),vm.read_value(0x4ab3)};
        city_->draw(frame,position_x(),position_y(),record_-23,world.facing,
                    static_cast<unsigned>(overland_phase_ms_/RuntimeTiming::ambient_frame_ms),16,0,&state);
        if(sprite_)sprite_->draw(frame);
    }
    else {render_exploration(frame,world,assets);if(sprite_)sprite_->draw(frame);}
    if(font_ && showing_glyphs()) {
        const auto glyphs=GlyphText(glyph_text_);
        if(!glyphs.native) {
            for(int y=16;y<104;++y)for(int x=8;x<160;++x)frame[static_cast<std::size_t>(y*320+x)]=0xff000000u;
            font_->draw_text(frame,glyphs.legacy,16,24,18,10,0xffffffffu);
        }
    }
}
bool ExplorationSession::city_input(InputEvent event,std::size_t budget) {
    if(!city_mode() || !event.pressed) return false;
    std::optional<Direction> direction;
    if(event.character=='8')direction=Direction::north;
    else if(event.character=='6')direction=Direction::east;
    else if(event.character=='2')direction=Direction::south;
    else if(event.character=='4')direction=Direction::west;
    else switch(event.key) {
    case Key::up:direction=Direction::north;break;
    case Key::right:direction=Direction::east;break;
    case Key::down:direction=Direction::south;break;
    case Key::left:direction=Direction::west;break;
    default:return false;
    }
    if(can_act()) {world.facing=*direction;forward(budget);}return true;
}
void ExplorationSession::advance_overland_animation(std::uint64_t elapsed) {
    if(!city_mode() || !can_act() || picture_ || showing_glyphs() || !overland_presented_)return;
    constexpr auto cycle=std::uint64_t{RuntimeTiming::ambient_frame_ms}*6;
    overland_phase_ms_=(overland_phase_ms_+elapsed%cycle)%cycle;
}
bool ExplorationSession::picture_presented(std::uint64_t token,std::size_t budget) {
    if(!picture_token_) return false;
    if(*picture_token_!=token || !vm.pending_request() || vm.pending_request()->token!=token)
        throw DecodeError("Stale picture presentation acknowledgement");
    if(!budget || budget>1000000) throw DecodeError("Invalid exploration execution budget");
    const auto kind=vm.pending_request()->kind;auto staged=vm;
    if(kind==EclRequestKind::encounter_dialogue) {
        if(!encounter_dialogue_ || !pending_sprite_distance_)throw DecodeError("Missing encounter presentation state");
        staged.update_encounter_dialogue(token,*pending_sprite_distance_);
        encounter_dialogue_->distance=*pending_sprite_distance_;vm=std::move(staged);
        picture_token_.reset();pending_sprite_distance_.reset();
        if(encounter_dialogue_->step)complete_encounter_step(budget);else refresh_encounter_menu();
        return true;
    }
    if(kind==EclRequestKind::setup_sprite || kind==EclRequestKind::approach_sprite) {
        if(!pending_sprite_distance_)throw DecodeError("Missing staged sprite presentation distance");
        staged.answer_sprite_request(token,*pending_sprite_distance_);
    } else if(kind==EclRequestKind::picture || kind==EclRequestKind::sprite_off)staged.complete_request(token);
    else throw DecodeError("Invalid presentation request kind");
    vm=std::move(staged);picture_token_.reset();pending_sprite_distance_.reset();pump(budget);return true;
}}
