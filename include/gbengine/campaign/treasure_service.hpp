#pragma once
#include "gbengine/campaign/campaign_state.hpp"
#include "gbengine/items/item_catalog.hpp"
#include "gbengine/party/party.hpp"
#include <random>

namespace GBEngine {
enum class TreasureStatus { applied,rejected,unresolved };
struct TreasureResult {
    TreasureStatus status=TreasureStatus::rejected;
    std::string message;
    std::optional<std::uint32_t> experience_per_member={};
    std::vector<std::size_t> npc_members={};
    bool applied() const {return status==TreasureStatus::applied;}
};
class TreasureService {
public:
    static constexpr std::size_t maximum_items=4096;
    // ECL27 stages all seven words and the low-byte item selector. Failure
    // preserves resources, identity allocation and the shared VM generator.
    static TreasureResult prepare(CampaignResources&,const ItemCatalog&,unsigned disk,
                                  std::span<const std::uint16_t,8>,std::mt19937&);
    // ECL1C also clears native encounter setup; identities are never reused.
    static void clear(CampaignResources&);
    // Scripted rewards and an explicitly prepared normal-combat contribution:
    // native eligibility/divisor, status recovery and NPC monetary deductions.
    // The combat owner must already reduce ownership to the persistent roster.
    // No implicit generated participants or duel. Adds native XP once;
    // caller owns the modal entry token and must not replay this on input.
    static TreasureResult begin_collection(Party&,CampaignResources&,std::uint16_t native_party_count,
                                           std::int64_t encounter_experience=0);
};
}
