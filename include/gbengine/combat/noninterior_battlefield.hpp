#pragma once
#include "gbengine/combat/battlefield.hpp"
#include <span>
namespace GBEngine {
// Native 2C86:08A4 selector classification. Undefined native cases fail.
std::uint8_t noninterior_terrain_flags(std::uint8_t selector,std::uint16_t region_flag);
// Resident city table3648; coordinates are low bytes of VM49C3/49C4,
// region_flag is the full VM4AB3 word, not a presentation asset selector.
Battlefield build_noninterior_battlefield(const BattlefieldTables&,
    std::span<const std::uint8_t> city_cells,std::uint8_t presentation_mode,
    std::uint8_t city_x,std::uint8_t city_y,std::uint16_t region_flag,
    std::uint8_t ecl_record,std::mt19937&);
}
