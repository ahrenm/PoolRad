#include "gbengine/debug/runtime.hpp"
#include "gbengine/world/exploration_session.hpp"

namespace GBEngine::Debug {
bool Access::quick_win(ExplorationSession& s,std::uint64_t token,std::uint64_t revision) {
    auto* runtime=Runtime::current();if(!runtime || !runtime->healthy())return false;
    const CombatToken ticket{token};
    if(!s.current_combat(ticket,revision))return false;
    const auto& b=*s.combat_;
    // Never convert a failed preparation, terminal defeat or already rewarded
    // encounter. The mode additionally fences actual-present/action playback.
    if(!b.runtime_attempted_ || !b.runtime_error_.empty() || b.aftermath_ || b.outcome_pending() || b.moving() || b.pending_cast())return false;
    bool party=false,enemy=false;
    for(const auto& a:b.deployment_->actors)if(!a.removed && a.character.active()) {
        if(a.character.byte(0x10e)==1)enemy=true;else party=true;
    }
    if(!party || !enemy)return false;
    auto next=std::make_unique<CombatSession>(b);
    for(auto& a:next->deployment_->actors)if(!a.removed && a.character.byte(0x10e)==1) {
        a.character.set_byte(0x10c,6);a.character.set_byte(0x10d,0);a.character.set_byte(0x11b,0);a.extension[3]=0;
    }
    next->casts_.clear();next->opponent_turn_.reset();next->strikes_.clear();next->standalone_travel_.reset();
    next->magic_playback_.reset();next->monster_playback_.reset();next->round_.notices.clear();next->round_.selection_playback.clear();
    next->round_end_=CombatRoundEndDisposition::outcome_pending;next->action_message_.clear();++next->revision_;
    if(!runtime->event("debug_quick_win","{\"combat_token\":"+std::to_string(token)+",\"revision\":"+std::to_string(revision)+",\"assisted\":true}"))return false;
    s.combat_=std::move(*next);
    // Normal CombatMode finish_combat owns rewards/Treasury/ECL continuation.
    // No direct XP, purse, quest, RNG or clock write is made here.
    return true;
}
}

