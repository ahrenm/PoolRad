#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace GBEngine {
enum class TimeUnit : std::uint8_t { round,minute,ten_minutes,hour,day,month,year };
struct TimeIncrement {
    TimeUnit unit=TimeUnit::minute;
    std::uint8_t amount{};
};
struct ClockAdvance;
// A value view of VM49C6..49CC, not a second authoritative clock. Raw words
// survive restore; advancement follows the native single-carry scans exactly.
class GameClock {
    std::array<std::uint16_t,7> words_{};
public:
    explicit GameClock(std::array<std::uint16_t,7> words={}):words_(words) {}
    const auto& words() const {return words_;}
    ClockAdvance advance(TimeIncrement) const;
    std::string hour_text() const;
    std::string minute_text() const;
};
struct ClockAdvance {
    GameClock clock;
    std::uint16_t age_increments{},effect_ticks{};
};
}
