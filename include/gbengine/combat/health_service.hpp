#pragma once
#include "gbengine/combat/spatial_service.hpp"
#include <random>

namespace GBEngine {
class SpellCatalog;
class CombatAttackService;
struct CombatDefeatNotice {CombatActorIdentity actor;std::string text_id;};
struct CombatDamageOutcome {
    std::uint8_t amount{},before_hp{},after_hp{},status{};
    std::optional<std::uint8_t> interrupted_spell;
    std::vector<CombatDefeatNotice> notices={};
};
struct CombatRecoveryOutcome {
    DeploymentState state;
    bool applied=false,reactivated=false;
};
// Shared tactical HP, status, recovery and body ownership. Weapon attacks,
// spells and periodic effects all retain their existing staged transactions.
// This service neither selects attacks nor owns campaign/VM publication.
class CombatHealthService {
    CombatSpatialService spatial_;
    std::array<std::uint8_t,16> defeat_cleanup_codes_{};
    std::shared_ptr<const Bytes> native_module_;
    bool loaded_=false;
    // A strike burst has already validated its candidate; preserve the exact
    // old validation points when it applies indexed damage between callbacks.
    friend class CombatAttackService;
    static std::size_t find(const DeploymentState&,CombatActorIdentity);
    void validate(const DeploymentState&) const;
    void disable(DeploymentState&,std::size_t,std::optional<std::uint8_t>,std::mt19937*,std::vector<CombatDefeatNotice>*) const;
    static void disable_with_codes(DeploymentState&,std::size_t,std::span<const std::uint8_t>,std::optional<std::uint8_t>,std::mt19937*,std::span<const std::uint8_t>,std::vector<CombatDefeatNotice>*);
    static bool reverse_animate(DeploymentState&,std::size_t,std::size_t,std::span<const std::uint8_t>,std::optional<std::uint8_t> ={},std::mt19937* =nullptr,std::span<const std::uint8_t> ={},std::vector<CombatDefeatNotice>* =nullptr);
    std::optional<std::uint8_t> damage(DeploymentState&,std::size_t,std::uint8_t,std::size_t,std::optional<std::uint8_t>,std::mt19937*,std::vector<CombatDefeatNotice>*) const;
public:
    // Reuse the already authenticated module and footprints, without loading
    // assets or drawing RNG inside a health operation.
    static CombatHealthService from_native_module(std::shared_ptr<const Bytes>,const CombatSpatialService&);
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
    CombatRecoveryOutcome heal(const DeploymentState&,CombatActorIdentity target,
                               std::uint8_t amount,bool force=false) const;
    CombatRecoveryOutcome revive(const DeploymentState&,CombatActorIdentity target,std::uint8_t hp) const;
    static bool revive_in_place(DeploymentState&,CombatActorIdentity,std::uint8_t,const SpellCatalog&);
    static bool revive_from_module(DeploymentState&,CombatActorIdentity,std::span<const std::uint8_t>,std::uint8_t hp);
};
}
