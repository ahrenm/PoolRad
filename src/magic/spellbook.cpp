#include "gbengine/magic/spellbook.hpp"
namespace GBEngine {
std::vector<MemorizedSpell> Spellbook::memorized(const Character& character) {
    std::vector<MemorizedSpell> result;
    for(std::size_t slot=0;slot<21;++slot) {
        const auto raw=character.byte(0x17+slot);
        if(raw) result.push_back({slot,SpellId(static_cast<std::uint8_t>(raw&127)),(raw&128)!=0});
    }
    return result;
}
bool Spellbook::known(const Character& character,SpellId id) {
    const SpellId checked(id.value);return character.byte(0x32+checked.value)!=0;
}
std::uint8_t Spellbook::capacity(const Character& character,std::uint8_t school,std::uint8_t level) {
    if(school>1 || level<1 || level>3) throw DecodeError("Unsupported saved spell capacity field");
    return character.byte(0xb1+3u*school+level);
}
std::size_t Spellbook::consume_first(Character& character,SpellId id) {
    const SpellId checked(id.value);
    // Validate the whole bounded slot array before making a scalar change.
    const auto entries=memorized(character);
    for(const auto& entry:entries) if(!entry.pending && entry.spell==checked) {
        character.set_byte(0x17+entry.slot,0);return entry.slot;
    }
    throw DecodeError("Requested spell has no memorized slot");
}
}
