#pragma once
#include "gbengine/combat/magic_service.hpp"

namespace GBEngine {
enum class RetainedAbilityEventKind { magic, strike };
struct RetainedAbilityEvent {
    RetainedAbilityEventKind kind;
    std::size_t index{};
};
struct RetainedAbilityResult {
    // Native SPC handler code, never a regular spell or item-cast index.
    std::uint8_t effect_code{};
    CombatMagicResult magic;
    std::vector<CombatStrike> strikes;
    std::vector<RetainedAbilityEvent> events;
};
class RetainedAbilityService {
    CombatAttackService attacks_;
    CombatMagicEffects effects_;
    bool loaded_=false;
    static std::size_t find(const DeploymentState&,CombatActorIdentity);
    static std::size_t victim(const DeploymentState&,std::uint8_t native_slot);
    static void remove(DeploymentState&,std::size_t owner,std::size_t effect_index,bool reverse);
public:
    static RetainedAbilityService load(const IFileSystem&);
    // Native0026 exact removal, including flagged4A/4B reversal before unlink.
    // Used by100A's ordered cleanup, which precedes the live phase13 calls.
    static void remove_effect_in_place(DeploymentState&,CombatActorIdentity,std::size_t effect_index);
    // Native phase13's live inactive branch; called after terminal cleanup,
    // independently of the node's reversal flag. No RNG or damage is involved.
    static void release_inactive_in_place(DeploymentState&,CombatActorIdentity,std::uint8_t effect_code);
    // One genuine first-node phase15 callback. The source phase dispatcher
    // calls 4A then 4B even when 4A has already Quit the selected actor.
    // Publication of this candidate, its RNG and ordered immutable feedback
    // belongs to the surrounding round-selection transaction.
    RetainedAbilityResult stage(const DeploymentState&,CombatActorIdentity owner,
        std::uint8_t effect_code,const ItemCatalog&,const CombatAttackContext&,
        std::mt19937&) const;
};
}
