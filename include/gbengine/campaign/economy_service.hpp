#pragma once
#include "gbengine/campaign/campaign_state.hpp"
#include "gbengine/party/inventory_service.hpp"
#include <functional>
#include <random>

namespace GBEngine {
enum class EconomyStatus { applied,rejected,confirmation_required,unresolved };
struct EconomyResult {
    EconomyStatus status=EconomyStatus::rejected;
    std::string message;
    std::optional<std::uint16_t> gold_quote;
    ItemId item{};
    bool applied() const {return status==EconomyStatus::applied;}
};
// A consumed gem/jewelry awaiting exactly one Sell/Keep decision. Construction and
// validity are owned by the transaction service, not the screen.
class AppraisalQuote {
    friend class EconomyService;
    std::size_t member_{};
    std::uint64_t revision_{};
    std::uint16_t gold_{};
    bool keep_{};
    bool jewelry_{};
public:
    std::size_t member() const {return member_;}
    std::uint16_t gold() const {return gold_;}
    bool can_keep() const {return keep_;}
    bool jewelry() const {return jewelry_;}
};
// Width and rounding rules from 43EC:00A8, 4053:28BE and 0130/0185.
// Gems/jewelry remain counts and never contribute to spendable coin value.
class WealthRules {
public:
    static std::int32_t gold_value(const std::array<std::uint32_t,7>&,const ItemCatalog&);
    static std::int32_t gold_value(const Character&,const ItemCatalog&);
    static void normalize_gold(Character&,std::uint16_t);
    static void normalize_gold(std::array<std::uint32_t,7>&,std::uint16_t);
    static std::uint16_t sale_quote(const Item&);
    static std::uint16_t gem_appraisal_value(std::uint8_t d100);
    static std::array<std::uint16_t,2> jewelry_appraisal_range(std::uint8_t d100);
};
// All operations stage both party and resident-style campaign resources.
// Shop stock is repeatable; taking a treasure item consumes that resource.
class EconomyService {
    Party& party_;
    CampaignResources& resources_;
    EconomyResult mutate(const std::function<EconomyResult(Party&,CampaignResources&)>&);
    EconomyResult acquire(std::size_t,std::uint64_t,bool purchase);
    EconomyResult appraise(std::size_t,bool jewelry,std::mt19937&,AppraisalQuote&);
public:
    EconomyService(Party& party,CampaignResources& resources):party_(party),resources_(resources) {}
    EconomyResult pool();
    EconomyResult share();
    EconomyResult take_money(std::size_t member,std::size_t denomination,std::uint16_t amount);
    // View Trade/Drop are physical currency operations, separate from item actions.
    EconomyResult trade_money(std::size_t source,std::size_t recipient,std::size_t denomination,std::uint16_t amount,std::uint8_t execution_mode=0);
    EconomyResult drop_money(std::size_t member,std::size_t denomination,std::uint16_t amount,std::uint8_t execution_mode=0);
    // Native View redraw recalculates only its viewed/source member after the loop.
    EconomyResult refresh_money_view(std::size_t member);
    EconomyResult buy(std::size_t member,std::uint64_t stock_id);
    EconomyResult acquire_reward(std::size_t member,std::uint64_t reward_id);
    EconomyResult sell(std::size_t member,ItemId,bool confirmed=false);
    EconomyResult identify(std::size_t member,ItemId,bool confirmed=false);
    EconomyResult appraise_gem(std::size_t member,std::mt19937&,AppraisalQuote&);
    EconomyResult appraise_jewelry(std::size_t member,std::mt19937&,AppraisalQuote&);
    EconomyResult resolve_appraisal(const AppraisalQuote&,bool keep);
};
}
