#pragma once
#include "gbengine/party/inventory.hpp"

namespace GBEngine {
using CampaignItemId=std::uint64_t;
struct CampaignItem {
    CampaignItemId id{};
    // Not yet owned by a character. Party ItemIds are assigned on acquisition.
    Item item;
};
struct CampaignResources {
    // Native resident reward/pool words are separate from the VM state banks.
    std::array<std::uint32_t,7> amounts{};
    std::vector<CampaignItem> items;
    CampaignItemId next_item_id=1;
    bool combat_setup=false;
};
}
