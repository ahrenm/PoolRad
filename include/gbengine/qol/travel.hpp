#pragma once
#include "gbengine/time/game_clock.hpp"
namespace GBEngine {
// Launch-only QoL policy; source travel still owns movement, encounters and
// the shared clock/effect transaction. No saved time or ECL data is rewritten.
constexpr TimeIncrement wilderness_step_time(bool qol_enabled) {
    return {TimeUnit::hour,static_cast<std::uint8_t>(qol_enabled?2:12)};
}
}
