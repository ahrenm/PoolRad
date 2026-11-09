#pragma once
#include "gbengine/combat/attack_service.hpp"

namespace GBEngine {
struct CombatTarget {
    std::optional<CombatActorIdentity> actor;
    CombatPosition position;
    std::uint8_t distance{},direction{}; // Native half-points and first cone.
    bool visible=false,body=false,available=false,weapon_eligible=false;
    bool confirmation_required=false;
    // Read-only reason for an otherwise valid ranged target. Native adjacency
    // uses occupied footprints, including diagonal cells of large opponents.
    bool adjacent_enemy_blocks_ranged=false;
};
class CombatTargetingService {
    CombatSpatialService spatial_;
    CombatAttackService attacks_;
    bool loaded_=false;
    const DeploymentActor& actor(const DeploymentState&,CombatActorIdentity) const;
    void validate(const DeploymentState&) const;
    CombatTarget describe(const DeploymentState&,const DeploymentActor&,const DeploymentActor&,
                          const ItemCatalog&,std::uint8_t,unsigned) const;
public:
    static CombatTargetingService load(const IFileSystem&);
    // Unfiltered4E800909 list. Turning uses this directly and does not invoke
    // weapon/phase0/1 eligibility callbacks. maximum is in whole squares.
    std::vector<CombatTarget> visible_targets(const DeploymentState&,CombatActorIdentity,
                                             std::uint16_t maximum=127) const;
    // Native326D32E6/4E800909: visible placed actors, including self/allies,
    // sorted by distance then the source's direction/parity exchange rule.
    // No RNG, actor mutation or action expenditure.
    std::vector<CombatTarget> list(const DeploymentState&,CombatActorIdentity,const ItemCatalog&) const;
    // Native326D2DFA manual cursor. Bodies remain distinct from live occupancy;
    // weapon targeting never silently treats a body or empty cell as an enemy.
    CombatTarget probe(const DeploymentState&,CombatActorIdentity,CombatPosition,const ItemCatalog&) const;
    static CombatPosition move_cursor(CombatPosition,const BattlefieldTables&,unsigned);
};
}
