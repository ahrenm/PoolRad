#pragma once
#include "gbengine/campaign/economy_service.hpp"
namespace GBEngine {
class IShopInventory {
public:
    virtual ~IShopInventory()=default;
    virtual EconomyResult sell_item(std::size_t,ItemId,bool confirmed=false)=0;
    virtual EconomyResult identify_item(std::size_t,ItemId,bool confirmed=false)=0;
};
enum class CampaignServiceKind {shop,treasure,shrine};
}
