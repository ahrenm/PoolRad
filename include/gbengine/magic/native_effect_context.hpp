#pragma once
#include <cstdint>
#include <random>
#include <vector>
namespace GBEngine {
class ItemCatalog;
class SpellCatalog;
struct EffectNotice;
struct EffectExpiryEdits;
// Caller-owned transaction context. No service may retain these pointers or
// mutate live RNG while operating on staged party/effect state.
struct NativeEffectContext {
    const ItemCatalog* items=nullptr;
    std::mt19937* random=nullptr;
    std::uint8_t execution_mode=1;
    const SpellCatalog* spells=nullptr;
    std::vector<EffectNotice>* notices=nullptr;
    std::size_t member=0;
    EffectExpiryEdits* edits=nullptr;
};
}
