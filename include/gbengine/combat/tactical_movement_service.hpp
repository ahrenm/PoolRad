#pragma once
#include "gbengine/combat/attack_service.hpp"
#include "gbengine/combat/round_service.hpp"
#include "gbengine/magic/spell_catalog.hpp"

namespace GBEngine {
struct TacticalMovementResult {
    CombatStepResult step;
    std::vector<CombatStrike> strikes;
    bool interrupted=false;
    std::vector<CombatRoundNotice> notices{};
};
struct TacticalFlightResult {
    DeploymentState state;
    bool escaped=false;
    std::string notice_id;
    std::vector<CombatRoundNotice> notices{};
};
class TacticalMovementService {
    CombatSpatialService spatial_;
    std::array<std::uint8_t,4> helpless_codes_{};
    std::array<std::uint8_t,16> cleanup_codes_{};
    bool loaded_=false;
    struct Adjacent {CombatActorIdentity id;std::uint8_t distance{},direction{};};
    static std::size_t find(const DeploymentState&,CombatActorIdentity);
    bool helpless(const Character&) const;
    static bool sector(const BattlefieldTables&,unsigned,CombatPosition,CombatPosition);
    std::vector<Adjacent> adjacent(const DeploymentState&,const DeploymentActor&) const;
    static std::uint8_t movement_budget(DeploymentActor&,std::uint8_t);
public:
    static TacticalMovementService load(const IFileSystem&);
    //Complete source08B2 ->0712 ->0627 transaction. Unsupported callbacks
    //publish no candidate and consume no caller RNG. Flight stays explicit.
    TacticalMovementResult stage(const DeploymentState&,CombatActorIdentity,unsigned,
        const ItemCatalog&,const CombatAttackService&,const CombatAttackContext&,std::mt19937&,
        const SpellCatalog* spells=nullptr) const;
    //After explicit native flee confirmation (or source AI flee decision).
    //Failure consumes the turn too. An escaped actor retains HP, owns status3,
    //and leaves no tactical body. Final party/encounter aftermath is separate.
    TacticalFlightResult stage_flight(const DeploymentState&,CombatActorIdentity,
        std::uint8_t party_movement_bonus,std::mt19937&,const ItemCatalog* items=nullptr,
        const SpellCatalog* spells=nullptr) const;
};
}
