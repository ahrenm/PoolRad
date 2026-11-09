#pragma once
#include "gbengine/combat/deployment.hpp"
#include "gbengine/campaign/treasure_service.hpp"

namespace GBEngine {
struct CombatAftermathContext {
    std::uint16_t native_party_count{};
    // VM6DE3 full-word loot suppression; not resident6DE3 round count.
    std::uint16_t loot_policy{};
    bool surrogate=false;
    ItemId next_item_id{}; // Owned tactical generated-item high-water; zero for legacy pure callers.
    // VM6DE6 is independent of resident84D4 (surrogate). Nonzero selects
    // the duel XP calculation; exactly1 also changes normal defeat eligibility.
    std::uint16_t special_reward_policy{};
    std::optional<CombatActorIdentity> selected_actor{}; // Native5D92 on entry.
};
enum class CombatOutcome {victory,flight,defeat};
struct CombatAftermathResult {
    Party party;
    CampaignResources resources;
    std::mt19937 random;
    TreasureResult collection;
    std::uint16_t defeated_count{};
    bool enemy_rewards=false;
    std::int64_t encounter_experience{};
    CombatOutcome outcome=CombatOutcome::victory;
    std::uint16_t source_result{}; // Final native VM6DC7, including wrapper80.
    bool collection_ready=true;
    bool experience_awarded=false; // Native84DA; false leaves the share display0.
    bool special=false;
    std::vector<std::size_t> removed_persistent;
    // Direct source strings for the native0916 outcome/XP screen.
    std::string result_text_id;
    std::string experience_prefix_id;
};
class CombatAftermathService {
public:
    // Native25C704C9/0000/0355/11AC/12E8 outcome transaction. Returns a fully
    // staged XP/recovery/NPC-share proposal. Collection has ALREADY run;
    // the owner presents its result and treasury without awarding it twice.
    // Does not acknowledge ECL24 or release the combat checkpoint itself.
    static CombatAftermathResult stage(const DeploymentState&,const Party&,
                                      const CampaignResources&,const std::mt19937&,
                                      CombatAftermathContext);
};
}
