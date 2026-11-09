#pragma once
#include "gbengine/assets/decoders.hpp"
namespace GBEngine {
// Native virtual word banks, independent of host layout and selected-character
// overrides. These include unnamed words; preserving them does not name them.
struct EclStateBanks {
    std::array<std::uint16_t,1024> area1{},area2{};
    std::array<std::uint16_t,512> area3{};
    std::uint16_t raw_value(std::uint16_t address) const;
};
}
