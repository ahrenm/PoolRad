#include "gbengine/rendering/combat_presentation.hpp"
#include "gbengine/assets/decoders.hpp"
#include <limits>
#include <utility>

namespace GBEngine {
CombatPresentation::CombatPresentation(std::uint64_t owner):owner_(owner) {
    if(!owner_)throw DecodeError("Combat presentation requires an owned battle");
}
void CombatPresentation::start(std::vector<CombatVisualStep> steps) {
    if(!complete())throw DecodeError("Combat presentation already has pending visual work");
    if(steps.empty())throw DecodeError("Combat presentation requires at least one visual step");
    if(sequence_==std::numeric_limits<std::uint64_t>::max())
        throw DecodeError("Combat presentation sequence identity exhausted");
    steps_=std::move(steps);++sequence_;step_=0;elapsed_=0;presented_=false;
}
const CombatVisualStep* CombatPresentation::current() const {
    return complete()?nullptr:&steps_[step_];
}
std::optional<CombatPresentationToken> CombatPresentation::token() const {
    if(complete())return std::nullopt;
    return CombatPresentationToken{owner_,sequence_,step_};
}
void CombatPresentation::finish_step() {
    ++step_;elapsed_=0;presented_=false;
}
bool CombatPresentation::presented(CombatPresentationToken supplied) {
    const auto expected=token();
    if(paused_ || !expected || supplied!=*expected || presented_)return false;
    presented_=true;
    if(steps_[step_].duration_ms==0)finish_step();
    return true;
}
void CombatPresentation::advance(std::uint64_t elapsed_ms) {
    if(paused_ || complete() || !presented_)return;
    const auto remaining=steps_[step_].duration_ms-elapsed_;
    if(elapsed_ms>=remaining)finish_step();
    else elapsed_+=elapsed_ms;
}
void CombatPresentation::pause() {paused_=true;}
void CombatPresentation::resume() {
    if(!paused_)return;
    paused_=false;
    // A nested mode may have replaced the pixels. The held frame must be
    // presented again before any retained dwell resumes.
    presented_=false;
}
}
