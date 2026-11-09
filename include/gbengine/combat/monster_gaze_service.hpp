#pragma once
#include "gbengine/combat/magic_service.hpp"

namespace GBEngine {
// Phase14 borrows genuine resident magic scratch. Ability codes are not cast
// IDs:53/79 retain681B;54 explicitly writes10 before its save/application.
struct MonsterGazeContext {
    CombatMagicContext magic;
    std::uint8_t damage_flags{};
    bool inherited_scratch_known=false;
    std::optional<bool> item_context;
    //Partial native writes do not make the entire ambient bundle authoritative.
    bool damage_flags_known=false,pending_id_known=false;
};
struct MonsterGazeResult {
    CombatMagicResult magic;
    MonsterGazeContext context;
    bool quit=false;
};
class MonsterGazeService {
    CombatMagicEffects effects_;
    CombatAttackService attacks_;
    CombatSpatialService spatial_;
    ItemCatalog items_;
    SpellCatalog spells_;
public:
    static MonsterGazeService load(const IFileSystem&);
    // Exact owned source node, retained victim and selected actor are required
    // at their native read points. Whole candidate/RNG publish only on success.
    MonsterGazeResult stage(const DeploymentState&,CombatActorIdentity actor,
        std::size_t effect_index,const MonsterGazeContext&,std::mt19937&) const;
};
}
