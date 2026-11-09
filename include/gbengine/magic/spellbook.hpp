#pragma once
#include "gbengine/magic/spell_catalog.hpp"
#include "gbengine/party/party.hpp"
namespace GBEngine {
struct MemorizedSpell {
    std::size_t slot;
    SpellId spell;
    bool pending;
};
// Byte-field views of saved/native character data. These do not synthesize
// spell availability, regenerate class caches or complete memorization/rest.
class Spellbook {
public:
    static std::vector<MemorizedSpell> memorized(const Character&);
    static bool known(const Character&,SpellId);
    static std::uint8_t capacity(const Character&,std::uint8_t school,std::uint8_t level);
    // Native10BA:148B consumes the first full-byte match, not a pending slot.
    // Called on staged characters only after the wider spell service succeeds.
    static std::size_t consume_first(Character&,SpellId);
};
}
