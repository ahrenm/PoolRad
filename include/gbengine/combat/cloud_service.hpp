#pragma once
#include "gbengine/combat/combat_magic_effects.hpp"
#include "gbengine/magic/spell_catalog.hpp"
#include "gbengine/effects/effect_lifecycle.hpp"
namespace GBEngine {
class CombatCloudService {
public:
    // Native2EE2:0A65, shared by cloud contact and phases11/15. Caller owns
    // staging and first-node dispatch. Returns whether a cough notice is due.
    static bool nausea_in_place(DeploymentActor&,const ItemCatalog&);
    static std::vector<EffectNotice> create_in_place(DeploymentState&,CombatActorIdentity,
        CombatPosition,std::uint8_t level,std::optional<ItemOrigin>,const SpellCatalog&,
        const ItemCatalog&,std::mt19937&);
    static std::vector<EffectNotice> contact_in_place(DeploymentState&,CombatActorIdentity,
        const CombatMagicContext&,const SpellCatalog&,const ItemCatalog&,std::mt19937&);
    // Source28 lookup uses owner + high nibble of marker context; collisions
    // retain native first-match behavior. No independent host hazard timer.
    static bool expire_in_place(DeploymentState&,CombatActorIdentity,std::uint8_t ordinal);
};
}
