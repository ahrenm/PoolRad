#include "gbengine/time/game_time_service.hpp"
namespace GBEngine {
TimeAdvanceResult GameTimeService::advance(GameClock& clock,Party& party,TimeIncrement increment,IEffectExpiryResolver* resolver) {
    EffectActivityMask mask;mask.fill(1);
    return advance_rest(clock,party,increment,mask,resolver);
}
TimeAdvanceResult GameTimeService::advance_rest(GameClock& clock,Party& party,TimeIncrement increment,EffectActivityMask& mask,IEffectExpiryResolver* resolver) {
    const auto next=clock.advance(increment);
    auto staged=party;
    if(next.age_increments) {
        for(auto& member:staged.members_)
            member.set_word(0x30,static_cast<std::uint16_t>(member.word(0x30)+next.age_increments));
        ++staged.revision_;
    }
    const auto effects=EffectLifecycle::advance_retained(staged,next.effect_ticks,mask,resolver);
    clock=next.clock;party=std::move(staged);
    return {next.age_increments,next.effect_ticks,effects};
}
}
