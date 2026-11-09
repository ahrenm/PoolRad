#pragma once
#include "gbengine/party/party.hpp"

namespace GBEngine {
enum class EncounterChoice : std::uint8_t { combat,wait,flee,advance,parlay };
enum class EncounterNotice : std::uint8_t { none,both_sides_wait,monsters_flee };
struct EncounterPace {
    std::uint8_t minimum{},maximum{};
    bool operator==(const EncounterPace&) const =default;
};
struct EncounterDialogueConfig {
    std::uint8_t sprite{};
    std::uint16_t maximum_distance{};
    std::uint8_t body{};
    std::uint16_t destination{};
    std::array<std::uint8_t,5> policies{};
    std::array<std::string,3> texts;
    std::uint8_t flee_threshold{},monster_flee_threshold{};
};
struct EncounterDialogueStep {
    std::uint16_t distance{};
    std::optional<std::uint16_t> result;
    bool repeat=false,approach=false;
    EncounterNotice notice=EncounterNotice::none;
};
// ECL29 is a retained interaction, not a combat result. The caller owns all
// numeric setters, resources and presentation barriers; these queries are pure.
class EncounterDialogueService {
public:
    // Native traversal order includes persistent and temporary participants.
    // SPC graphs must be restored, but no effect execution/binding is needed.
    static EncounterPace pace(std::span<const Character* const>);
    static std::array<EncounterChoice,4> choices(std::uint16_t distance,bool interior);
    static std::size_t text_index(const EncounterDialogueConfig&,std::uint16_t distance);
    static EncounterDialogueStep choose(const EncounterDialogueConfig&,EncounterPace,
                                       std::uint16_t distance,EncounterChoice);
};
}
