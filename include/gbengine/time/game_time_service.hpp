#pragma once
#include "gbengine/time/game_clock.hpp"
#include "gbengine/effects/effect_lifecycle.hpp"
namespace GBEngine {
struct TimeAdvanceResult {
    std::uint16_t age_increments{},effect_ticks{};
    EffectAdvanceResult effects;
};
class GameTimeService {
public:
    // Exploration context only. Stages the complete clock/party transaction;
    // failure, including an unknown expiry reversal, commits neither.
    static TimeAdvanceResult advance(GameClock&,Party&,TimeIncrement,IEffectExpiryResolver* =nullptr);
    static TimeAdvanceResult advance_rest(GameClock&,Party&,TimeIncrement,EffectActivityMask&,IEffectExpiryResolver* =nullptr);
};
}
