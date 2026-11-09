#pragma once
#include "gbengine/combat/combat_magic_effects.hpp"
#include "gbengine/effects/effect_lifecycle.hpp"
#include "gbengine/magic/spell_catalog.hpp"

namespace GBEngine {
// Adapter for the countdown's temporary Party roster. Actor indices map that
// source-order copy back to the same staged tactical state. Character scalar
// edits go through the supplied staged character; extension edits address the
// corresponding staged actor. Caller publishes both after the whole tick.
class CombatEffectExpiry final : public IEffectExpiryResolver {
    DeploymentState& state_;
    std::span<const std::size_t> indices_;
    const ItemCatalog& items_;
    const SpellCatalog& spells_;
    std::mt19937& random_;
    std::optional<CombatActorIdentity> resource_actor_;
public:
    CombatEffectExpiry(DeploymentState& state,std::span<const std::size_t> indices,
        const ItemCatalog& items,const SpellCatalog& spells,std::mt19937& random,
        std::optional<CombatActorIdentity> resource_actor={})
        :state_(state),indices_(indices),items_(items),spells_(spells),random_(random),resource_actor_(resource_actor) {}
    bool allows_context_updates() const override {return true;}
    bool reverse(const EffectExpiryRequest&,Character&) override;
    bool reverse_with_edits(const EffectExpiryRequest&,Character&,EffectExpiryEdits&) override;
};
}
