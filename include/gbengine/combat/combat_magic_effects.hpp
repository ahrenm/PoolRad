#pragma once
#include "gbengine/combat/health_service.hpp"
#include "gbengine/magic/saving_throw_service.hpp"
#include "gbengine/items/item_catalog.hpp"
#include "gbengine/effects/effect_lifecycle.hpp"
#include "gbengine/magic/spell_catalog.hpp"

namespace GBEngine {
class CombatAttackService;
struct CombatMagicContext {
    CombatActorIdentity caster;
    // Source pending ID681B, effect code6817 and dice count681C are distinct.
    std::uint8_t pending_id{},pending_effect{},level{},dice_count{};
    bool absorption_suppressed=false; // Native6820, including area acquisition.
};
struct CombatMagicDamageResult {
    DeploymentState state;
    CombatDamageOutcome outcome;
    std::vector<EffectNotice> notices;
};
using CombatMagicRecoveryResult = CombatRecoveryOutcome;
// Explicit combat scratch adapter. All public transactions retain identity,
// ownership, bodies and ordered source callbacks; unknown dependencies throw.
class CombatMagicEffects {
    CombatHealthService health_;
    ItemCatalog items_;
    static std::size_t find(const DeploymentState&,CombatActorIdentity);
    static bool has(const Character&,std::uint8_t);
    std::uint8_t phase6(Character&,const Character& caster,std::uint8_t,std::uint8_t,
        const CombatMagicContext&,std::mt19937&,std::size_t,std::vector<EffectNotice>&) const;
public:
    static CombatMagicEffects load(const IFileSystem&);
    //2EE2:2814 (phase5/6): first62/3B veto, otherwise fresh delayed3B.
    //No weapon query, damage edit or RNG belongs to this callback.
    static bool schedule_regeneration_in_place(Character&);
    // Narrow 2EE2:1A19 adapter: owns copies of already loaded damage tables;
    // no filesystem lifetime or recursive service loading is introduced.
    static CombatMagicDamageResult weapon_callback_damage(const CombatAttackService&,const ItemCatalog&,
        const DeploymentState&,CombatActorIdentity target,std::uint8_t amount,
        const CombatMagicContext&,std::mt19937&);
    static CombatMagicDamageResult on_hit_damage(const CombatAttackService&,const ItemCatalog&,
        const DeploymentState&,CombatActorIdentity target,std::uint8_t amount,std::uint8_t flags,
        const CombatMagicContext&,std::mt19937&);
    // Caller owns whole action staging and shared RNG publication. Raw1/20
    // retain source bypass; elemental phase12 bonuses are explicit flags.
    static SavingThrowResult save(DeploymentState&,CombatActorIdentity target,
        std::uint8_t category,std::int8_t modifier,const CombatMagicContext&,
        std::mt19937&,std::uint8_t damage_flags=0);
    static bool save_invariant(DeploymentState&,CombatActorIdentity target,
        std::uint8_t category,std::int8_t modifier,const CombatMagicContext&,std::mt19937&);
    CombatMagicDamageResult damage(const DeploymentState&,CombatActorIdentity target,
        std::uint8_t amount,std::uint8_t flags,bool save_succeeded,std::uint8_t save_mode,
        const CombatMagicContext&,std::mt19937&) const;
    // 49D5175D: status0/1/4/5, disease veto, byte sum before cap. In combat
    // inactive5 becomes4 but healing alone never reactivates a tactical body.
    CombatMagicRecoveryResult heal(const DeploymentState&,CombatActorIdentity target,
                                   std::uint8_t amount,bool force=false) const;
    CombatMagicRecoveryResult revive(const DeploymentState&,CombatActorIdentity target,
                                     std::uint8_t hp) const;
    //Use the same authenticated footprint tables from an already-owned catalog.
    //Commit in place without invalidating caller-owned actor references.
    static bool revive_in_place(DeploymentState&,CombatActorIdentity,std::uint8_t,const SpellCatalog&);
    static bool revive_from_module(DeploymentState&,CombatActorIdentity,std::span<const std::uint8_t>,std::uint8_t hp);
    // Native first-node phase8/19 invisibility callbacks. Other matching
    // lifecycle callbacks are left to their explicit adapters.
    static void install_invisibility_in_place(Character&,std::uint8_t source_code,std::mt19937&);
    // Native phase19 spiritual-hammer callback ensures one first matching
    // generated weapon, honoring source capacity. Identity allocator is part
    // of the caller's staged combat transaction; true means a source notice.
    static bool maintain_hammer_in_place(DeploymentState&,CombatActorIdentity,ItemId& next_item_id,const ItemCatalog&);
    static bool apply_charm_in_place(DeploymentState&,CombatActorIdentity);
    static std::vector<EffectNotice> remove_effect_in_place(DeploymentState&,CombatActorIdentity,std::size_t effect_index,
        const ItemCatalog&,const SpellCatalog&,std::mt19937&,std::optional<CombatActorIdentity> resource_actor={});
};
}
