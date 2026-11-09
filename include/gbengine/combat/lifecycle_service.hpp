#pragma once
#include "gbengine/combat/round_service.hpp"
#include "gbengine/combat/spatial_service.hpp"
#include "gbengine/time/game_clock.hpp"
#include <memory>

namespace GBEngine {
class SpellCatalog;
enum class CombatRoundEndDisposition { next_round,continue_question,outcome_pending };
struct CombatLifecycleContext {
    std::uint8_t previous_opponent_hp_percent{};
    // Native resident49F4, distinct from any VM bank address.
    bool suppress_continue_question=false;
    const ItemCatalog* items=nullptr;
    const SpellCatalog* spells=nullptr;
    std::mt19937* random=nullptr;
    ItemId* next_item_id=nullptr;
};
struct CombatRoundEndResult {
    std::unique_ptr<DeploymentState> deployment;
    CombatRoundState round;
    GameClock clock;
    std::uint8_t opponent_hp_percent{};
    std::array<std::uint8_t,2> active_sides{};
    std::vector<CombatRoundNotice> notices;
    CombatRoundEndDisposition disposition=CombatRoundEndDisposition::outcome_pending;
    std::uint16_t age_increments{},effect_ticks{};
    std::size_t effect_decrements{},expired_effects{};
    bool bleeding_notice=false;
};
class CombatLifecycleService {
public:
    // Native29CB0880, staged against every retained owned tactical actor.
    // This does not acknowledge ECL24 or publish the returned snapshot. The
    // owner commits once using its held combat token/revision transaction.
    // All unsupported callbacks fail before caller state can change.
    static CombatRoundEndResult end_round(const DeploymentState&,const CombatRoundState&,
                                         const GameClock&,const CombatSpatialService&,
                                         CombatLifecycleContext={});
};
}
