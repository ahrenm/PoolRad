#pragma once
#include <cstdint>

namespace GBEngine {
// Original base-class slots: character bytes96..9D / CharacterStats::levels.
// These eight slots already exist in the retained native records. Naming them
// does not enable creation choices, advancement or class-specific abilities.
// Evidence: research/class-remnants-evidence.json.
enum class CharacterClassSlot : std::uint8_t {
    cleric=0,
    fighter=2,
    magic_user=5,
    thief=6,

    // Player character classes not implemented by SSI, but maybe someday??
    // Reserved in this reconstruction; the shipped XP entries are -1.
    druid=1,
    paladin=3,
    ranger=4,
    monk=7,
};
}
