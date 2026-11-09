#pragma once
#include "gbengine/combat/equipment_service.hpp"
#include "gbengine/combat/round_service.hpp"

namespace GBEngine {
struct OpponentEquipmentContext {
    // Native extension0A's retained actor, independently of a new AI choice.
    std::optional<CombatActorIdentity> retained_target;
    bool adjacent_opponent=false;
    std::uint8_t round_index{};
};
struct OpponentEquipmentResult {
    DeploymentState state;
    // Native13CA redraw flag: attempted changes, not necessarily applied ones.
    bool changed=false;
    std::vector<CombatRoundNotice> notices;
};
// Source2AF3128D/13CA. All changes are staged, including equipped callbacks;
// unknown callback dependencies throw and leave the caller-owned state intact.
class OpponentEquipmentService {
public:
    static std::uint8_t weapon_score(const Item&,const ItemCatalog&,const Character& owner,
                                     const Character* retained_target=nullptr);
    static OpponentEquipmentResult stage(const DeploymentState&,CombatActorIdentity,
                                        const ItemCatalog&,OpponentEquipmentContext={});
};
}
