#pragma once
#include "gbengine/time/game_time_service.hpp"
#include "gbengine/magic/camp_work_service.hpp"
#include <random>

namespace GBEngine {
// These are native resident counters, not the similarly numbered VM fields.
// Camp entry resets daily_ticks. Encounter ticks persist across camp visits;
// neither counter resets merely because another Rest starts.
struct RestCampState {
    std::uint16_t daily_ticks{},encounter_ticks{};
    bool operator==(const RestCampState&) const =default;
};
struct RestState {
    std::uint32_t remaining_minutes{};
    CampWorkState work;
    EffectActivityMask effects{};
    bool running{},awaiting_presentation{};
    bool operator==(const RestState&) const =default;
};
enum class RestOutcome { running,completed,interrupted };
struct RestTickResult : CampWorkResult {
    RestOutcome outcome=RestOutcome::running;
    bool daily_recovery{};
    std::vector<std::size_t> recovered_members;
    std::vector<CampWorkNotice> work_notices;
    std::vector<EffectNotice> effect_notices;
    std::optional<std::uint8_t> encounter_roll;
};
class RestService {
public:
    static constexpr std::uint32_t maximum_minutes=99u*24u*60u+23u*60u+59u;
    static CampWorkPreparation prepare(Party&,const SpellCatalog&);
    static CampWorkResult start(RestState&,std::uint32_t minutes);
    // Exactly one native five-minute step. All state is staged together; an
    // unresolved reversal/recovery/work task leaves the step uncommitted.
    static RestTickResult tick(RestCampState&,RestState&,GameClock&,Party&,
        const SpellCatalog&,std::mt19937&,std::uint16_t interval,std::uint16_t chance,
        IEffectExpiryResolver* =nullptr);
    static void acknowledge(RestState& state) {state.awaiting_presentation=false;}
    static void stop(RestState& state) {state.running=false;}
};
}
