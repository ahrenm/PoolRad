#pragma once
#include <array>
#include <cstddef>
#include <string_view>

namespace GBEngine {
// Explicit development launch fixtures, never defaults for restored campaigns.
struct ExplorationScenario {
    std::string_view name;
    std::size_t disk,record;
};
inline constexpr std::array exploration_scenarios{
    ExplorationScenario{"overland",7,26},
    ExplorationScenario{"overland-west",6,25},
    ExplorationScenario{"overland-east",8,27},
    ExplorationScenario{"overland-links",7,26},
    ExplorationScenario{"overland-links-west",6,25},
    ExplorationScenario{"overland-links-east",8,27},
    ExplorationScenario{"overland-travel",7,26},
    ExplorationScenario{"overland-recovery",7,26},
    ExplorationScenario{"combat-entry",3,8},
    ExplorationScenario{"combat-movement",3,8},
    ExplorationScenario{"combat-weapons",3,8},
    ExplorationScenario{"combat-duel",3,11},
    ExplorationScenario{"encounter-dialogue",4,2},
    ExplorationScenario{"plaza",1,18},
    ExplorationScenario{"parlay",4,2},
    ExplorationScenario{"text-input",8,27},
    ExplorationScenario{"number-input",2,20},
    ExplorationScenario{"glyphs",4,21},
    ExplorationScenario{"party-strength",1,18}
};
constexpr bool isolated_overland_scenario(std::string_view name) noexcept {
    return name=="overland" || name=="overland-west" || name=="overland-east";
}
constexpr bool linked_overland_scenario(std::string_view name) noexcept {
    return name=="overland-links" || name=="overland-links-west" || name=="overland-links-east";
}
constexpr bool timed_overland_scenario(std::string_view name) noexcept {
    return name=="overland-travel" || name=="overland-recovery";
}
constexpr const ExplorationScenario* find_exploration_scenario(std::string_view name) noexcept {
    for(const auto& scenario:exploration_scenarios)if(scenario.name==name)return &scenario;
    return nullptr;
}
}
