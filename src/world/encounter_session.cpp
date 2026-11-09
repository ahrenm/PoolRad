#include "gbengine/world/exploration_session.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/app/runtime_timing.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
EclMenu encounter_menu(std::uint16_t distance,bool interior) {
    EclMenu menu;
    const auto& source=overlay_string(interior && distance?0x2bfd:0x2be1);
    for(std::size_t at=0;at<source.size();++at)if(source[at]=='~') {
        const auto end=source.find('~',at+1);
        auto label=source.substr(at+1,end==std::string::npos?std::string::npos:end-at-1);
        while(!label.empty() && label.back()==' ')label.pop_back();
        if(label.empty())throw DecodeError("Invalid extracted encounter menu");
        menu.hotkeys.push_back(static_cast<std::uint8_t>(label.front()));
        for(std::size_t i=1;i<label.size();++i)if(label[i]>='A' && label[i]<='Z')label[i]=static_cast<char>(label[i]+32);
        menu.labels.push_back(StringStore::active().runtime_text(label));
    }
    if(menu.labels.size()!=4)throw DecodeError("Invalid extracted encounter choice count");
    return menu;
}
}
bool ExplorationSession::stage_encounter_dialogue(const EclRequest& request) {
    if(encounter_dialogue_) {
        if(encounter_dialogue_->token!=request.token)throw DecodeError("Encounter continuation changed");
        boundary=picture_token_?"Presenting encounter art":encounter_notice()?"Presenting encounter feedback":"Choose an encounter action";
        return true;
    }
    try {
        if(!request.encounter)throw DecodeError("Missing encounter configuration");
        if(world.encounter.duel_context)throw DecodeError("Duel encounter dialogue requires surrogate participant bindings");
        EncounterInteraction next;next.token=request.token;next.config=*request.encounter;
        next.pace=EncounterDialogueService::pace(world.participants());
        // Validate original labels before publishing art or interaction state.
        (void)encounter_menu(0,true);
        EclRequest art=request;art.kind=EclRequestKind::setup_sprite;
        art.values={next.config.sprite,next.config.maximum_distance,next.config.body};
        if(!stage_sprite(art,true))return false;
        encounter_dialogue_=std::move(next);return true;
    }catch(const DecodeError& error) {boundary="Encounter dialogue unresolved: "+std::string(error.what());return false;}
}
void ExplorationSession::refresh_encounter_menu() {
    if(!encounter_dialogue_ || picture_token_)throw DecodeError("Encounter menu requires presented art");
    auto& state=*encounter_dialogue_;
    state.menu=encounter_menu(state.distance,vm.read_value(0x49e6)!=0);
    const auto index=EncounterDialogueService::text_index(state.config,state.distance);
    display_text=state.config.texts[index];glyph_text_.clear();
    state.ready=true;state.step.reset();state.notice.clear();state.notice_presented=false;state.notice_elapsed=0;
    menu_token_=state.token;menu_selection_=0;boundary="Choose an encounter action";
}
bool ExplorationSession::choose_encounter_menu(std::uint64_t token,std::size_t selection,std::size_t budget) {
    if(!encounter_dialogue_ || !encounter_dialogue_->ready)return false;
    if(encounter_dialogue_->token!=token || !vm.pending_request() || vm.pending_request()->token!=token)
        throw DecodeError("Stale encounter choice");
    if(selection>=4)throw DecodeError("Encounter choice out of range");
    const auto& current=*encounter_dialogue_;
    const auto choices=EncounterDialogueService::choices(current.distance,vm.read_value(0x49e6)!=0);
    const auto step=EncounterDialogueService::choose(current.config,current.pace,current.distance,choices[selection]);
    if(step.approach) {
        EclRequest art=*vm.pending_request();art.kind=EclRequestKind::approach_sprite;
        art.values={current.distance,step.distance};
        if(!stage_sprite(art,true))return false;
    }
    encounter_dialogue_->ready=false;encounter_dialogue_->step=step;
    if(!step.approach)complete_encounter_step(budget);
    return true;
}
void ExplorationSession::complete_encounter_step(std::size_t budget) {
    if(!encounter_dialogue_ || !encounter_dialogue_->step || picture_token_)throw DecodeError("Missing encounter step presentation");
    auto& state=*encounter_dialogue_;const auto step=*state.step;
    // Source policy2 performs its numeric setter before displaying flight.
    // Keep the ECL successor held by this interaction while any setter host
    // effect completes, then present feedback, then permit the successor.
    if(!step.repeat && !state.result_submitted) {
        auto next_vm=vm;next_vm.answer_encounter_dialogue(state.token,step.result);
        vm=std::move(next_vm);state.result_submitted=true;
        if(vm.pending_request()) {pump(budget);return;}
    }
    if(state.result_submitted && vm.pending_request())return;
    if(step.notice!=EncounterNotice::none && state.notice.empty()) {
        state.notice=overlay_string(step.notice==EncounterNotice::both_sides_wait?0x2c1a:0x2c2b);
        state.notice_presented=false;state.notice_elapsed=0;boundary="Presenting encounter feedback";return;
    }
    if(step.repeat) {refresh_encounter_menu();return;}
    if(!state.notice.empty())display_text=state.notice;
    encounter_dialogue_.reset();menu_token_=0;menu_selection_=0;
    pump(budget);
}
void ExplorationSession::advance_encounter_notice(std::uint64_t elapsed) {
    if(!encounter_notice() || !encounter_dialogue_->notice_presented)return;
    auto& state=*encounter_dialogue_;constexpr auto dwell=RuntimeTiming::notification_dwell_ms;
    state.notice_elapsed+=std::min(elapsed,dwell-std::min(dwell,state.notice_elapsed));
    if(state.notice_elapsed<dwell)return;
    complete_encounter_step(10000);
}
}
