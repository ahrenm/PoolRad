#pragma once
#include "gbengine/combat/attack_service.hpp"
#include "gbengine/combat/attack_service.hpp"
#include "gbengine/magic/spell_catalog.hpp"
#include "gbengine/magic/magic_effect_service.hpp"
#include "gbengine/combat/combat_magic_effects.hpp"
namespace GBEngine {
// A native cast index is validated against the separate regular/item catalogs.
// Resource ownership and queued cast timing belong to CombatCastingService.
struct CombatMagicIntent {
    CombatActorIdentity caster;
    std::uint8_t cast_index{};
    std::vector<CombatActorIdentity> targets;
    std::optional<CombatPosition> centre;
    bool item_origin=false;
    std::uint8_t caster_level{};
    std::optional<ItemOrigin> source_item;
    ItemId next_item_id{};
    std::optional<std::uint16_t> dungeon_flag;
    std::array<std::uint8_t,2> side_attack_bonus{};
};
enum class CombatMagicTargetMode { self,individual,multiple,area,all_visible };
struct CombatMagicTargetPolicy {
    CombatMagicTargetMode mode=CombatMagicTargetMode::individual;
    std::uint8_t raw_mode{},range{},maximum_targets=1,radius{};
    bool bodies=false;
    bool empty_tiles=false;
};
struct CombatMagicFeedback {
    CombatActorIdentity actor;std::string text_id;
    // 10BA:1FEB(...,1,recipient), distinct from a plain 16EC notice/veto.
    bool recipient_animation=false;
    std::uint8_t color=10;
    bool include_name=true;
    // Native composed notices retain their extracted prefix and suffix.
    std::string suffix={};
};
struct CombatMagicDamage {
    CombatActorIdentity caster,target;
    std::uint8_t amount{},before_hp{},after_hp{},status{};
    bool saved=false;
    std::optional<std::uint8_t> interrupted_spell;
    std::uint8_t flags{};
};
struct CombatMagicProjectile {CombatPosition from,to;std::uint8_t effect_slot=0x13,frame_count=4,source_wait=50;};
enum class CombatMagicEventKind { feedback,damage,projectile,terminal,state };
struct CombatMagicEvent {
    CombatMagicEventKind kind;
    std::size_t index;
    // Presentation borrows an immutable pre-impact state, never the final
    // battle whose later recipients may already be bodies or removed actors.
    std::shared_ptr<const DeploymentState> before;
    std::shared_ptr<const DeploymentState> after={};
};
struct CombatMagicResult {
    DeploymentState state;
    std::vector<CombatMagicFeedback> feedback;
    std::vector<CombatMagicDamage> damage;
    std::vector<CombatMagicEvent> events;
    std::vector<CombatMagicProjectile> projectiles;
    ItemId next_item_id{};
    std::shared_ptr<const DeploymentState> casting_before;
};
class CombatMagicService {
    SpellCatalog catalog_;
    CombatSpatialService spatial_;
    CombatMagicEffects effects_;
    CombatAttackService attacks_;
    std::array<std::uint8_t,16> metadata(std::uint8_t) const;
public:
    static CombatMagicService load(const IFileSystem&);
    const SpellCatalog& catalog() const {return catalog_;}
    const CombatMagicEffects& damage_effects() const {return effects_;}
    std::optional<std::string> unsupported(std::uint8_t cast_index,bool item_origin=false) const;
    CombatMagicTargetPolicy target_policy(const DeploymentState&,CombatActorIdentity,std::uint8_t,
                                          bool item_origin=false,std::uint8_t level=0) const;
    // Pure source range/LOS/placed-actor targeting; source eligibility callbacks
    // are checked again in stage. Does not spend slots, charges or RNG.
    std::vector<CombatActorIdentity> candidates(const DeploymentState&,CombatActorIdentity,
                                               std::uint8_t,bool item_origin=false,std::uint8_t level=0) const;
    CombatMagicResult stage(const DeploymentState&,const CombatMagicIntent&,const ItemCatalog&,std::mt19937&) const;
};
}
