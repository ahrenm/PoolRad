#pragma once
#include "gbengine/party/party.hpp"
#include <random>
#include <functional>

namespace GBEngine {
struct SavingThrowResult {
    bool saved=false;
    std::uint8_t die{},adjusted{};
};
// Borrowed first-node lookup for domain-specific party aura fallback. Own
// effects still win, and raw d20 endpoints never invoke the callback.
using SavingThrowAuraQuery=std::function<const CharacterEffect*(std::uint8_t)>;
// Native 49D5:0D63. The source is the actual selected caster/context owner;
// protection callbacks inspect that member's alignment, not the defender's.
// Caller owns staging for the roster and RNG, including phase-C cache edits.
class SavingThrowService {
public:
    // For callers that consume only the boolean and do not own elemental
    // flags. Publish once only if every native low-bit context agrees on the
    // result, complete roster/cache state and RNG. No adjusted roll is inferred.
    // Aura queries are borrowed read-only lookups; they must not mutate or draw.
    static bool resolve_invariant(std::span<Character> roster,std::size_t target,
        const Character& source,std::uint8_t category,std::int8_t modifier,std::mt19937&,
        const SavingThrowAuraQuery& aura={});
    static SavingThrowResult resolve(std::span<Character> roster,std::size_t target,
        const Character& source,std::uint8_t category,std::int8_t modifier,std::mt19937&);
    static SavingThrowResult resolve(std::span<Character> roster,std::size_t target,
        const Character& source,std::uint8_t category,std::int8_t modifier,std::mt19937&,
        std::uint8_t damage_flags);
    static SavingThrowResult resolve(std::span<Character> roster,std::size_t target,
        const Character& source,std::uint8_t category,std::int8_t modifier,std::mt19937&,
        std::uint8_t damage_flags,const SavingThrowAuraQuery&);
};
}
