#pragma once
#include "gbengine/combat/deployment.hpp"
#include "gbengine/party/inventory_service.hpp"
#include "gbengine/combat/combat_magic_effects.hpp"

namespace GBEngine {
struct CombatMagicResult;
struct CombatEquipmentDamageContext {
    const CombatMagicEffects* effects=nullptr;
    std::mt19937* random=nullptr;
    //84 owns flags8 only. Supply retained ambient query fields explicitly;
    // matching callbacks that need absent inherited scratch stay held.
    std::optional<CombatMagicContext> inherited={};
};
struct CombatEquipmentResult {
    DeploymentState state;
    ItemActionResult action;
    std::shared_ptr<const CombatMagicResult> playback={};
};
// Native405314D0 toggle plus326D0D23 return-to-combat attack refresh.
// Equipment changes own no movement/clock/initiative charge. Actual callbacks
// may alter the actor, and unsupported callbacks retain the whole proposal.
class CombatEquipmentService {
    static std::shared_ptr<const CombatMagicResult> reject_alignment(DeploymentState&,std::size_t,ItemId,
        const CombatEquipmentDamageContext&);
public:
    // The automatic optimizer deliberately carries temporarily adjusted100.
    // This is exactly405314D0: no scalar recalculation or attack refresh.
    // Rejected native toggles are inert; unknown callbacks remain unresolved.
    static CombatEquipmentResult toggle_native(const DeploymentState&,CombatActorIdentity actor,ItemId,const ItemCatalog&,CombatEquipmentDamageContext={});
    static void refresh_primary(DeploymentActor&,const ItemCatalog&,std::uint8_t round_index);
    static CombatEquipmentResult ready(const DeploymentState&,CombatActorIdentity actor,ItemId,
                                       const ItemCatalog&,std::uint8_t round_index,CombatEquipmentDamageContext={});
};
}
