#pragma once
#include "gbengine/combat/deployment.hpp"
#include "gbengine/items/item_catalog.hpp"
#include "gbengine/combat/retained_ability_service.hpp"
#include <random>

namespace GBEngine {
enum class CombatRoundPhase { not_started,player,ai_pending,round_end_pending,casting_pending };
struct CombatRoundNotice {
    CombatActorIdentity actor;
    std::string text_id;
    bool include_name=true;
    bool operator==(const CombatRoundNotice&) const=default;
};
struct CombatRoundContext {
    const RetainedAbilityService* retained=nullptr;
    CombatAttackContext attacks;
};
struct CombatSelectionStep {
    std::optional<CombatRoundNotice> notice;
    std::shared_ptr<const RetainedAbilityResult> ability;
    std::shared_ptr<const DeploymentState> before;
    bool operator==(const CombatSelectionStep&) const=default;
};
struct CombatRoundState {
    // Native resident6DE3 starts at zero. It is independent of campaign time.
    std::uint8_t round_index{};
    // VM6DCB full word; source tests its low byte and clears the full word
    // only after preparing every actor. This is not a symmetric surprise flag.
    std::uint16_t surprise_mask{};
    std::uint8_t party_movement_bonus{};
    CombatRoundPhase phase=CombatRoundPhase::not_started;
    std::optional<CombatActorIdentity> active;
    std::vector<CombatRoundNotice> notices;
    // Native selected5D92 remains bound after initiative selection ends. It
    // supplies caster context for round-end terrain/effect callbacks.
    std::optional<CombatActorIdentity> effect_caster{};
    // Exact notice/phase15 action order across native selection rescans.
    // Immutable results outlive publication of the final tactical snapshot.
    std::vector<CombatSelectionStep> selection_playback{};
    std::uint64_t selection_sequence{};
    bool operator==(const CombatRoundState&) const=default;
};
class CombatRoundService {
    static std::uint8_t phase18(DeploymentActor&,std::uint8_t,bool,std::vector<CombatRoundNotice>&);
    static void prepare_actor(DeploymentActor&,const ItemCatalog&,CombatRoundState&,std::mt19937&);
    static void advance_in_place(DeploymentState&,CombatRoundState&,const ItemCatalog&,std::mt19937&,const CombatRoundContext*);
    static void finish_actor(DeploymentState&,CombatRoundState&,const ItemCatalog&,std::mt19937&,const CombatRoundContext*);
public:
    // Each operation stages complete owned state and RNG before publication.
    // No VM, campaign clock, rendering or outcome is acknowledged here.
    static void begin_round(DeploymentState&,CombatRoundState&,const ItemCatalog&,std::mt19937&,const CombatRoundContext* =nullptr);
    static void finish_turn(DeploymentState&,CombatRoundState&,const ItemCatalog&,std::mt19937&,const CombatRoundContext* =nullptr);
    static void finish_cast(DeploymentState&,CombatRoundState&,const ItemCatalog&,std::mt19937&,const CombatRoundContext* =nullptr);
    static void finish_opponent_turn(DeploymentState&,CombatRoundState&,const ItemCatalog&,std::mt19937&,const CombatRoundContext* =nullptr);
    // Commands own their priority/Guard edits; rescan without repeating Quit.
    static void resume_selection(DeploymentState&,CombatRoundState&,const ItemCatalog&,std::mt19937&,const CombatRoundContext* =nullptr);
    // Exposed source primitive: every nonremoved actor consumes one d100,
    // including inactive/zero-priority actors; later equal rolls win ties.
    static std::optional<std::size_t> select_next(const DeploymentState&,std::mt19937&);
    static int dexterity_modifier(std::uint8_t);
};
}
