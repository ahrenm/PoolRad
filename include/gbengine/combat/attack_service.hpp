#pragma once
#include "gbengine/combat/health_service.hpp"
#include "gbengine/items/item_catalog.hpp"
#include <random>

namespace GBEngine {
class SpellCatalog;
struct CombatMagicResult;
enum class CombatAttackKind { normal, departure_reaction, arrival_guard, retained_crush, retained_rake };
struct CombatAttackContext {
    // Source VM6E71 for allegiance0; VM6E70 for allegiance1. Low signed bytes.
    std::array<std::uint8_t,2> side_attack_bonus{};
};
struct CombatStrike {
    CombatActorIdentity attacker,target;
    std::uint8_t d20{},damage{},before_hp{},after_hp{},status{};
    bool primary=true,hit=false;
    std::optional<std::uint8_t> interrupted_spell;
    // Ordered presentation must not borrow the already published final battle:
    // later strikes/reactions may have removed this recipient's icon.
    std::shared_ptr<const DeploymentState> before,after;
    // Native phase11 precedes the strike burst, including a complete miss.
    // Keep its notice/cache snapshots separate from successful on-hit work.
    std::shared_ptr<const CombatMagicResult> preparation={};
    // Source phase2 follows the ordinary hit with its own recipient feedback.
    std::shared_ptr<const CombatMagicResult> followup={};
    // Native1884 runs weapon travel once before its strike burst, using
    // ammunition type (plus the separate type2F weapon path), not hit outcome.
    struct Travel {std::uint8_t item_type{},direction{};CombatPosition origin,destination;};
    std::vector<Travel> travel;
};
struct CombatWeaponPlayback {
    CombatActorIdentity attacker;
    std::vector<CombatStrike::Travel> travel;
    std::shared_ptr<const DeploymentState> before;
};
struct CombatAttackResult {
    DeploymentState state;
    std::vector<CombatStrike> strikes;
    bool turn_finished=false;
    // Native1884 can travel before discovering both counters are exhausted.
    // This owns that visual without inventing a hit/miss strike or ammo cost.
    std::optional<CombatWeaponPlayback> standalone_travel={};
    //1406 shared scratch at completion, needed by49's real nested1884.
    std::array<std::uint8_t,2> native_hit_counts{};
    //6E2D/6E2E are reset by the nested1406 too; outer1884 charges its
    //actual projectile using the resulting shared primary spent count.
    std::array<std::uint8_t,2> native_spent_counts{};
};
class CombatAttackService {
    CombatSpatialService spatial_;
    std::array<std::uint8_t,4> helpless_codes_{};
    CombatHealthService health_;
    // DS03F2+16*i includes five names and three following-table byte strings.
    // Keep all eight raw Pascal payloads for the selected-build2F predicate.
    std::array<Bytes,8> retained_defense_names_{};
    std::shared_ptr<const Bytes> native_module_;
    bool loaded_=false;
    static std::size_t find(const DeploymentState&,CombatActorIdentity);
    bool helpless(const Character&) const;
    bool weapon_target_legal(const DeploymentState&,CombatActorIdentity,CombatActorIdentity,const ItemCatalog&,bool allow_same_side) const;
    std::uint8_t distance(const DeploymentActor&,const DeploymentActor&) const;
    bool visible(const DeploymentState&,const DeploymentActor&,const DeploymentActor&,unsigned) const;
    void validate(const DeploymentState&) const;
    static void spend_ammunition(DeploymentState&,std::size_t actor,std::size_t item,
                                 unsigned spent,bool recoverable);
    void drain_levels(DeploymentState&,CombatActorIdentity,CombatActorIdentity,unsigned,
                      const ItemCatalog&,CombatMagicResult&,std::uint8_t,std::mt19937&) const;
    CombatAttackResult stage_attack(const DeploymentState&,CombatActorIdentity,CombatActorIdentity,
                                   const ItemCatalog&,const CombatAttackContext&,std::mt19937&,
                                   CombatAttackKind,bool sweep_strike,bool allow_same_side=false,bool selected_ai_adjacent=false) const;
public:
    static CombatAttackService load(const IFileSystem&);
    const CombatHealthService& health_service() const {return health_;}
    // Native326D1085 eligibility, independent of range/line of sight.
    bool target_available(const DeploymentState&,CombatActorIdentity,CombatActorIdentity) const;
    // Range/terrain/owned actor legality only. No random draws or mutation.
    // Friendly-fire confirmation and targeting bodies remain separate services.
    bool can_target(const DeploymentState&,CombatActorIdentity,CombatActorIdentity,const ItemCatalog&) const;
    //10BA255B: ignore-terrain closest-footprint whole-square distance.
    std::uint8_t native_distance(const DeploymentState&,CombatActorIdentity,CombatActorIdentity) const;
    // Native49D50CB7 phase10/16 query on caller-owned combat staging. Raw1
    // bypasses callbacks;20 becomes100 before ordered byte adjustments. No
    // random draw or budget/resource cost;59 may update its first node context.
    std::uint8_t accuracy_in_place(DeploymentState&,CombatActorIdentity attacker,
                                   CombatActorIdentity target,std::uint8_t raw_d20) const;
    // Native HP/status/body and positive-damage cast interruption. Caller owns
    // the candidate transaction. resource_actor is the native selected5D92:
    // source148B consumes its first full-byte matching slot, even when the
    // interrupted cast belongs to another actor. No matching slot is a no-op.
    CombatDamageOutcome apply_damage_in_place(DeploymentState&,CombatActorIdentity target,
                                              std::uint8_t amount,CombatActorIdentity resource_actor,
                                              std::optional<std::uint8_t> defeat_flags={},std::mt19937* random=nullptr) const;
    // Native2EE20058 terminal application. Status7/6/8 veto; otherwise set
    // terminal status/HP/active,100A cleanup,phase13 and body if still inactive.
    // This is not HP damage and does not interrupt or consume a queued cast.
    bool apply_terminal_status_in_place(DeploymentState&,CombatActorIdentity target,std::uint8_t status,
        std::optional<std::uint8_t> defeat_flags={},std::mt19937* random=nullptr,std::vector<CombatDefeatNotice>* notices=nullptr) const;
    //Periodic16/39/5F use0058 without initializing ambient6819. A copied
    //native module supplies the exact100A cleanup table;64 stays held when
    //that genuine unknown context is relevant, rather than guessing flags0.
    static bool apply_terminal_status_from_module_in_place(DeploymentState&,CombatActorIdentity target,
        std::uint8_t status,std::span<const std::uint8_t> native_module,std::vector<CombatDefeatNotice>* notices=nullptr);
    // Native20 removal captures its original context before terminal cleanup
    // can unlink that same first node. Caller owns the surrounding journal.
    static void reverse_animate_dead_in_place(DeploymentState&,CombatActorIdentity,std::size_t effect_index,const SpellCatalog&);
    // Complete transaction: unsupported matching callbacks throw without RNG,
    // ammo, counts, HP or occupancy publication. Caller owns token/revision fences.
    CombatAttackResult stage(const DeploymentState&,CombatActorIdentity,CombatActorIdentity,
                             const ItemCatalog&,const CombatAttackContext&,std::mt19937&,
                             CombatAttackKind=CombatAttackKind::normal) const;
    //Only the owning friendly-confirmation/automatic transaction may call
    //this route. It relaxes side equality alone; all source weapon/effect,
    //RNG/resource and whole-action rollback contracts remain identical.
    CombatAttackResult stage_confirmed_friendly(const DeploymentState&,CombatActorIdentity,CombatActorIdentity,
                             const ItemCatalog&,const CombatAttackContext&,std::mt19937&) const;
    //0B3C only: a freshly2433-selected distance1 target bypasses1085.
    //The caller retains selection/RNG; this gate cannot admit ranged/retained
    //or manual targets or alter any subsequent hit/effect callback.
    CombatAttackResult stage_selected_adjacent_ai(const DeploymentState&,CombatActorIdentity,CombatActorIdentity,
                             const ItemCatalog&,const CombatAttackContext&,std::mt19937&) const;
};
}
