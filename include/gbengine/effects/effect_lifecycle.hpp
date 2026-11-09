#pragma once
#include "gbengine/party/party.hpp"
namespace GBEngine {
struct EffectExpiryRequest {
    std::size_t member{};
    CharacterEffect effect;
    std::optional<std::size_t> effect_index={};
};
struct EffectNotice {
    std::size_t member{};
    std::string text_id;
    auto operator<=>(const EffectNotice&) const =default;
};
//Only native first-full-byte spell consumption may cross callback members.
//slot is the absolute saved spellbook byte17..2B, never a general byte offset.
struct EffectSpellSlotConsumption {
    std::size_t member{};
    std::uint8_t slot{},expected_full_byte{};
};
// Explicit callback edits preserve stable identities even for equal duplicate
// nodes. Indices refer to the callback's original list. The expiring node is
// unlinked by the lifecycle itself, after these native callback operations.
struct EffectExpiryEdits {
    std::vector<std::size_t> remove_indices;
    std::vector<CharacterEffect> append;
    std::vector<EffectNotice> notices;
    std::vector<EffectSpellSlotConsumption> spell_slot_consumptions={};
};
// A resolver works on staged character state without external operations.
// Direct structural list edits are forbidden. Verified native callbacks may
// journal exact removals/appends. Strength/disease callbacks can opt into
// context-byte updates; preexisting identity/order/duration remain fixed.
class IEffectExpiryResolver {
public:
    virtual ~IEffectExpiryResolver()=default;
    virtual bool allows_context_updates() const {return false;}
    virtual bool reverse(const EffectExpiryRequest&,Character& staged)=0;
    virtual bool reverse_with_edits(const EffectExpiryRequest& request,Character& staged,EffectExpiryEdits&) {
        return reverse(request,staged);
    }
};
struct EffectAdvanceResult {
    std::size_t decrements{},expired{};
    std::vector<EffectNotice> notices;
};
// Resident DS471E has72 bytes, although imported rosters have at most8.
// Rest retains it across advances; exploration initializes it for each call.
using EffectActivityMask=std::array<std::uint8_t,72>;
class EffectLifecycle {
public:
    // Normal exploration dispatch (native initializes all activity flags).
    // Unknown codes are retained; flag-zero expiry needs no code handler.
    // Missing reversal bindings throw and leave the entire party unchanged.
    static EffectAdvanceResult advance(Party&,std::uint16_t ticks,IEffectExpiryResolver* =nullptr);
    static EffectAdvanceResult advance_retained(Party&,std::uint16_t ticks,EffectActivityMask&,IEffectExpiryResolver* =nullptr);
};
}
