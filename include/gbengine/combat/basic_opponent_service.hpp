#pragma once
#include "gbengine/combat/tactical_movement_service.hpp"
#include "gbengine/combat/opponent_magic_service.hpp"
#include "gbengine/combat/opponent_flight_service.hpp"
#include "gbengine/combat/opponent_equipment_service.hpp"
#include "gbengine/combat/monster_gaze_service.hpp"
#include "gbengine/combat/monster_breath_service.hpp"

namespace GBEngine {
enum class OpponentDecisionPhase {items,queued_cast,turning,spells,weapons};
struct BasicOpponentState {
    CombatActorIdentity actor;
    bool initialized=false,finished=false;
    std::optional<CombatActorIdentity> target;
    std::uint8_t last_direction=8,reversals{},weapon_iterations{};
    OpponentDecisionPhase decision=OpponentDecisionPhase::weapons;
    std::optional<OpponentFlightProgress> flight={};
    bool attack_loop_started=false;
};
struct BasicOpponentContext {
    CombatAttackContext attacks;
    std::uint16_t casualty_percentage{}; // VM6DC6, not resident6DC6.
    std::uint8_t opponent_hp_percent=100; // Resident6E2F from326D:2879.
    std::uint8_t party_movement_bonus{},round_index{};
    const SpellCatalog* spells=nullptr;
    bool casting_blocked=false,automatic_party_casting=false;
    std::uint8_t formation_facing{};
    std::uint16_t dungeon_flag{};
};
struct MonsterAbilityPlayback {
    CombatActorIdentity caster;
    std::uint8_t effect_code{};
    CombatMagicResult magic;
};
struct BasicOpponentResult {
    DeploymentState state;
    BasicOpponentState progress;
    std::vector<CombatStrike> strikes;
    bool moved=false,attacked=false,finished=false;
    std::vector<CombatRoundNotice> notices{};
    std::optional<CombatCastSchedule> casting={};
    bool execute_queued_cast=false,turn_undead=false;
    std::vector<MonsterAbilityPlayback> abilities{};
    std::optional<CombatWeaponPlayback> standalone_travel={};
};
class BasicOpponentService {
    CombatSpatialService spatial_;
    TacticalMovementService movement_;
    OpponentMagicService magic_;
    OpponentFlightService flight_;
    SpellCatalog spells_;
    MonsterGazeService gaze_;
    MonsterBreathService breath_;
    std::array<std::uint8_t,36> directions_{};
    std::array<std::uint8_t,4> helpless_codes_{};
    bool loaded_=false;
    struct Candidate {CombatActorIdentity actor;std::uint8_t distance{},direction{};};
    static std::size_t find(const DeploymentState&,CombatActorIdentity);
    static bool sector(const BattlefieldTables&,unsigned,CombatPosition,CombatPosition);
    std::uint8_t facing(const DeploymentState&,const DeploymentActor&,const DeploymentActor&) const;
    std::vector<Candidate> candidates(const DeploymentState&,const DeploymentActor&,unsigned,bool) const;
    bool helpless(const Character&) const;
    static std::uint8_t phase18(DeploymentActor&,std::uint8_t,bool);
    static std::uint8_t movement_budget(DeploymentActor&,std::uint8_t);
    void optimize(DeploymentState&,std::size_t,const ItemCatalog&,std::uint8_t,std::vector<CombatRoundNotice>&) const;
    static void quit(DeploymentActor&);
public:
    static BasicOpponentService load(const IFileSystem&);
    OpponentMagicTargets magic_targets(const DeploymentState& state,const CombatCastPending& pending,
            const SpellCatalog& spells,std::mt19937& random) const {return magic_.targets(state,pending,spells,random);}
    // One source movement/attack boundary per transaction. Presentation belongs
    // to the host; unsupported conditional callbacks publish nothing.
    BasicOpponentResult stage(const DeploymentState&,const BasicOpponentState&,
        const ItemCatalog&,const CombatAttackService&,const BasicOpponentContext&,std::mt19937&) const;
};
}
