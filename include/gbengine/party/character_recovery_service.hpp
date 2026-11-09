#pragma once
#include "gbengine/party/party.hpp"

namespace GBEngine {
// Native character caches are decoded byte/table operations, never a C++
// reinterpretation of the saved record or an AD&D rules approximation.
class CharacterRecoveryService {
public:
    // START.EXE's decoded load module supplies the resident DS tables. These
    // calls stage their record so malformed source/undefined native selections
    // cannot leave a partially rebuilt character.
    static void rebuild_class_cache(Character&,std::span<const std::uint8_t> module);
    static bool restore_level(Character&,std::span<const std::uint8_t> module);
    static bool advancement_capped(const Character&,std::uint8_t class_index);
};
}
