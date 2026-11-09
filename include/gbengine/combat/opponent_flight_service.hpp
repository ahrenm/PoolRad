#pragma once
#include "gbengine/combat/tactical_movement_service.hpp"

namespace GBEngine {
struct OpponentFlightContext {
    std::uint16_t casualty_percentage{};
    std::uint8_t opponent_hp_percent=100,party_movement_bonus{},formation_facing{};
};
struct OpponentFlightPreparation {
    DeploymentState state;
    std::vector<CombatRoundNotice> notices;
    bool finished=false;
};
struct OpponentFlightProgress {
    CombatActorIdentity actor;
    std::optional<CombatActorIdentity> target{};
    std::uint8_t last_direction=8,reversals{};
    std::optional<std::uint8_t> pending_direction{};
};
struct OpponentFlightResult {
    DeploymentState state;
    OpponentFlightProgress progress;
    std::vector<CombatStrike> strikes;
    std::vector<CombatRoundNotice> notices;
    bool moved=false,finished=false,escaped=false,continue_ordinary=false;
    bool reacquire_target=false;
};
// Source2AF310D7 morale and2AF307F2 outward movement. Every call stages one
// visible move, reaction/escape or source progress boundary; the host publishes
// RNG and snapshot together, then presents before requesting another boundary.
class OpponentFlightService {
    CombatSpatialService spatial_;
    TacticalMovementService movement_;
    std::array<std::uint8_t,36> directions_{};
    std::array<std::uint8_t,16> cleanup_{};
    bool loaded_=false;
    static std::size_t find(const DeploymentState&,CombatActorIdentity);
    static bool has(const Character&,unsigned);
    static void quit(DeploymentActor&);
    static std::uint8_t movement_budget(DeploymentActor&,std::uint8_t);
    static std::uint8_t morale(DeploymentState&,CombatActorIdentity,std::uint8_t);
    static void remove_link(DeploymentState&,CombatActorIdentity,std::uint8_t,
                            const ItemCatalog&,const SpellCatalog*,std::mt19937&);
    void disable(DeploymentState&,CombatActorIdentity,std::uint8_t,const ItemCatalog&,
                 const SpellCatalog*,std::mt19937&,std::vector<CombatRoundNotice>&) const;
public:
    static OpponentFlightService load(const IFileSystem&);
    OpponentFlightPreparation prepare(const DeploymentState&,CombatActorIdentity,
        const ItemCatalog&,const SpellCatalog*,const OpponentFlightContext&,std::mt19937&) const;
    OpponentFlightResult step(const DeploymentState&,const OpponentFlightProgress&,
        const ItemCatalog&,const CombatAttackService&,const CombatAttackContext&,
        const SpellCatalog*,const OpponentFlightContext&,std::mt19937&) const;
    // Phase15 own/visible range1 aura15 has no RNG and only disables source
    // casting/item capability. Positive4A/4B links retain special-action holds.
    static void actor_phase15(DeploymentState&,std::size_t,std::vector<CombatRoundNotice>*);
};
}
