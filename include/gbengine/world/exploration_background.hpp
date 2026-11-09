#pragma once
#include "gbengine/scripting/ecl_state.hpp"
#include <optional>
namespace GBEngine {
// Derived native presentation cache. Rebuilt from saved banks/source script,
// never serialized into new save fields or exposed as undefined C059/5F getters.
struct ExplorationBackground {
    std::uint16_t sky=11,alternate_sky=11;
    std::uint8_t horizon=0,lower_horizon=7,floor=6;
    bool operator==(const ExplorationBackground&)const=default;
    static std::uint8_t floor_color(std::uint8_t);
    static std::optional<ExplorationBackground> rebuild(unsigned disk,std::uint8_t record,
        const EclStateBanks&,std::span<const std::uint8_t> script);
};
}
