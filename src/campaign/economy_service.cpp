#include "gbengine/campaign/economy_service.hpp"
#include <algorithm>
#include <bit>
#include <limits>

namespace GBEngine {
namespace {
EconomyResult result(EconomyStatus status,std::string message={},ItemId item=0) {return {status,std::move(message),{},item};}
EconomyResult reject(std::string message) {return result(EconomyStatus::rejected,std::move(message));}
std::uint16_t capacity(const Character& c) {return static_cast<std::uint16_t>(EquipmentRules::load_allowance(c)+1500);}
std::uint16_t space(const Character& c) {return static_cast<std::uint16_t>(capacity(c)-c.word(0x102));}
bool fits(const Character& c,std::uint16_t amount) {return static_cast<std::uint16_t>(c.word(0x102)+amount)<=capacity(c);}
void add_money(Character& c,std::size_t denomination,std::uint16_t amount) {
    c.set_word(0x88+2*denomination,static_cast<std::uint16_t>(c.word(0x88+2*denomination)+amount));
    c.set_word(0x102,static_cast<std::uint16_t>(c.word(0x102)+amount));
}
void refresh(Character& c,const ItemCatalog& catalog) {EquipmentRules::recalculate(c,catalog);}
bool pay(Character& c,CampaignResources& resources,const ItemCatalog& catalog,std::uint16_t price,bool selected_word) {
    const auto full=WealthRules::gold_value(c,catalog);
    const auto purse=selected_word?static_cast<std::int32_t>(static_cast<std::uint16_t>(full)):full;
    if(purse>=price) {WealthRules::normalize_gold(c,static_cast<std::uint16_t>(purse-price));return true;}
    const auto pool=WealthRules::gold_value(resources.amounts,catalog);
    if(pool<price) return false;
    WealthRules::normalize_gold(resources.amounts,static_cast<std::uint16_t>(pool-price));return true;
}
}
std::int32_t WealthRules::gold_value(const std::array<std::uint32_t,7>& money,const ItemCatalog& catalog) {
    std::uint32_t copper=100;
    for(std::size_t i=0;i<5;++i) copper+=money[i]*catalog.coin_rates()[i];
    return std::bit_cast<std::int32_t>(copper)/200;
}
std::int32_t WealthRules::gold_value(const Character& c,const ItemCatalog& catalog) {
    std::array<std::uint32_t,7> money{};for(std::size_t i=0;i<5;++i) money[i]=c.word(0x88+2*i);
    return gold_value(money,catalog);
}
void WealthRules::normalize_gold(Character& c,std::uint16_t gold) {
    for(std::size_t i=0;i<5;++i) c.set_word(0x88+2*i,0);
    c.set_word(0x8e,static_cast<std::uint16_t>(gold%5));c.set_word(0x90,static_cast<std::uint16_t>(gold/5));
}
void WealthRules::normalize_gold(std::array<std::uint32_t,7>& money,std::uint16_t gold) {
    std::fill_n(money.begin(),5,0);money[3]=gold%5;money[4]=gold/5;
}
std::uint16_t WealthRules::sale_quote(const Item& item) {
    auto value=static_cast<std::uint16_t>(item.value()/2);
    // 1D1D/1D27 requires type 49 AND 1C; the intended ammunition branch is unreachable.
    if(item.quantity()>1) value=static_cast<std::uint16_t>(static_cast<std::uint16_t>(item.quantity()*value)/20);
    return value;
}
std::uint16_t WealthRules::gem_appraisal_value(std::uint8_t d100) {
    if(!d100 || d100>100) throw DecodeError("Gem valuation requires a d100 result");
    return d100<=25?10:d100<=50?50:d100<=70?100:d100<=90?500:d100<=99?1000:5000;
}
std::array<std::uint16_t,2> WealthRules::jewelry_appraisal_range(std::uint8_t d100) {
    if(!d100 || d100>100)throw DecodeError("Jewelry valuation requires a d100 result");
    if(d100<=10)return {100,900};
    if(d100<=20)return {200,1000};
    if(d100<=40)return {300,1500};
    if(d100<=50)return {500,2500};
    if(d100<=70)return {1000,5000};
    return d100<=90?std::array<std::uint16_t,2>{2000,6000}:std::array<std::uint16_t,2>{2000,10000};
}
EconomyResult EconomyService::mutate(const std::function<EconomyResult(Party&,CampaignResources&)>& action) {
    auto staged=party_;auto resources=resources_;const auto revision=party_.revision_;
    try {
        auto outcome=action(staged,resources);
        if(outcome.applied()) {
            if(party_.revision_!=revision) return reject("Party changed during a campaign transaction");
            staged.revision_=revision+1;party_=std::move(staged);resources_=std::move(resources);
        }
        return outcome;
    } catch(const DecodeError& error) {return result(EconomyStatus::unresolved,error.what());}
}
EconomyResult EconomyService::pool() {
    if(!party_.size()) return result(EconomyStatus::unresolved,"No imported party is available");
    return mutate([&](Party& party,CampaignResources& resources) {
        for(auto& c:party.members_) if(c.byte(0x84)==0 || c.byte(0x84)==0xb3) {
            for(std::size_t i=0;i<7;++i) {resources.amounts[i]+=c.word(0x88+2*i);c.set_word(0x88+2*i,0);}
            refresh(c,party.item_catalog());
        }
        return result(EconomyStatus::applied);
    });
}
EconomyResult EconomyService::share() {
    if(!party_.size()) return result(EconomyStatus::unresolved,"No imported party is available");
    return mutate([&](Party& party,CampaignResources& resources) {
        unsigned count=0;for(const auto& c:party.members_) if(c.byte(0x84)==0 || c.byte(0x84)==0xb3) ++count;
        if(!count) return result(EconomyStatus::unresolved,"Native sharing has no eligible divisor");
        std::array<std::uint16_t,7> each{},remaining{};
        for(std::size_t i=0;i<7;++i) if(std::bit_cast<std::int32_t>(resources.amounts[i])>0) {
            each[i]=static_cast<std::uint16_t>(resources.amounts[i]/count);
            remaining[i]=static_cast<std::uint16_t>(resources.amounts[i]%count);
        }
        for(auto& c:party.members_) {
            refresh(c,party.item_catalog());
            if(c.byte(0x84)>=128) continue;
            for(std::size_t reverse=7;reverse>0;--reverse) {
                const auto i=reverse-1;
                if(fits(c,each[i])) {
                    add_money(c,i,each[i]);
                    // Native checks the ENTIRE remainder before adding just one coin.
                    if(remaining[i] && fits(c,remaining[i])) {add_money(c,i,1);--remaining[i];}
                } else {
                    const auto available=space(c);add_money(c,i,available);
                    remaining[i]=static_cast<std::uint16_t>(remaining[i]+each[i]-available);
                }
            }
        }
        // Native overflow pass deliberately traverses all roster entries.
        for(std::size_t reverse=7;reverse>0;--reverse) {
            const auto i=reverse-1;
            if(!remaining[i]) continue;
            for(auto& c:party.members_) {
                const auto amount=std::min(space(c),remaining[i]);add_money(c,i,amount);
                remaining[i]=static_cast<std::uint16_t>(remaining[i]-amount);
            }
        }
        for(std::size_t i=0;i<7;++i) resources.amounts[i]=remaining[i];
        for(auto& c:party.members_) refresh(c,party.item_catalog());
        return result(EconomyStatus::applied);
    });
}
EconomyResult EconomyService::take_money(std::size_t member,std::size_t denomination,std::uint16_t amount) {
    if(member>=party_.size() || denomination>=7) return reject("Choose an actual member and denomination");
    return mutate([&](Party& party,CampaignResources& resources) {
        auto& c=party.members_[member];refresh(c,party.item_catalog());
        if(!fits(c,amount)) return reject("Character cannot carry that amount");
        const auto available=std::bit_cast<std::int32_t>(resources.amounts[denomination]);
        if(available<0) return result(EconomyStatus::unresolved,"Negative native treasure balance");
        const auto transfer=static_cast<std::uint16_t>(std::min(static_cast<std::uint32_t>(amount),resources.amounts[denomination]));
        resources.amounts[denomination]-=transfer;add_money(c,denomination,transfer);refresh(c,party.item_catalog());
        return result(EconomyStatus::applied);
    });
}
EconomyResult EconomyService::trade_money(std::size_t source,std::size_t recipient,std::size_t denomination,std::uint16_t amount,std::uint8_t execution_mode) {
    if(source>=party_.size() || recipient>=party_.size() || denomination>=7) return reject("Choose actual party members and a denomination");
    const auto& owner=party_.member(source);
    if(execution_mode==5 || (owner.byte(0x84)>=128 && owner.active() && owner.status()!=1)) return reject("This party member cannot trade personal money");
    if(amount>owner.word(0x88+2*denomination)) return reject("Amount exceeds the character's currency");
    if(!amount)return fits(party_.member(recipient),0)?result(EconomyStatus::applied):reject("Character cannot carry that amount");
    return mutate([&](Party& party,CampaignResources&) {
        auto& from=party.members_[source];auto& to=party.members_[recipient];
        auto validated=from;refresh(validated,party.item_catalog());
        if(!fits(to,amount)) return reject("Character cannot carry that amount");
        from.set_word(0x88+2*denomination,static_cast<std::uint16_t>(from.word(0x88+2*denomination)-amount));
        from.set_word(0x102,static_cast<std::uint16_t>(from.word(0x102)-amount));
        to.set_word(0x88+2*denomination,static_cast<std::uint16_t>(to.word(0x88+2*denomination)+amount));
        // Raw 044C:049F and04DB both pass bp+0E, the source reference.
        // Preserve this native stale-cache quirk until the View redraw boundary.
        from.set_word(0x102,static_cast<std::uint16_t>(from.word(0x102)+amount));
        return result(EconomyStatus::applied);
    });
}
EconomyResult EconomyService::drop_money(std::size_t member,std::size_t denomination,std::uint16_t amount,std::uint8_t execution_mode) {
    if(member>=party_.size() || denomination>=7) return reject("Choose an actual member and denomination");
    if(amount>party_.member(member).word(0x88+2*denomination)) return reject("Amount exceeds the character's currency");
    if(!amount)return result(EconomyStatus::applied);
    return mutate([&](Party& party,CampaignResources& resources) {
        auto& c=party.members_[member];
        auto validated=c;refresh(validated,party.item_catalog());
        c.set_word(0x88+2*denomination,static_cast<std::uint16_t>(c.word(0x88+2*denomination)-amount));
        c.set_word(0x102,static_cast<std::uint16_t>(c.word(0x102)-amount));
        if(execution_mode==1 || execution_mode==6)resources.amounts[denomination]+=amount;
        return result(EconomyStatus::applied);
    });
}
EconomyResult EconomyService::refresh_money_view(std::size_t member) {
    if(member>=party_.size()) return reject("Choose an actual party member");
    return mutate([&](Party& party,CampaignResources&) {
        refresh(party.members_[member],party.item_catalog());return result(EconomyStatus::applied);
    });
}
EconomyResult EconomyService::acquire(std::size_t member,std::uint64_t stock_id,bool purchase) {
    if(member>=party_.size()) return reject("Choose an actual party member");
    return mutate([&](Party& party,CampaignResources& resources) {
        const auto stock=std::find_if(resources.items.begin(),resources.items.end(),[&](const auto& item){return item.id==stock_id;});
        if(stock==resources.items.end()) return reject("Campaign item is no longer available");
        auto& c=party.members_[member];refresh(c,party.item_catalog());
        const auto& original=stock->item;
        const auto weight=static_cast<std::uint16_t>(original.unit_weight()*std::max(1,static_cast<int>(original.quantity())));
        if(c.inventory_.items_.size()>=16 || !fits(c,weight)) return reject("Character cannot carry this item");
        if(original.readied()) return result(EconomyStatus::unresolved,"Readied campaign templates require an effect binding");
        if(purchase && !pay(c,resources,party.item_catalog(),std::max<std::uint16_t>(1,original.value()),true)) return reject("Insufficient spendable coins");
        if(party.next_item_id_==std::numeric_limits<ItemId>::max()) throw DecodeError("Owned item identity exhausted");
        // 272F:0030 promotes zero-valued stock to one before 0239 copies it.
        if(purchase && !stock->item.value()) {stock->item.bytes_[0x3a]=1;stock->item.bytes_[0x3b]=0;}
        auto item=original;item.id_=party.next_item_id_++;const auto id=item.id();c.inventory_.items_.push_back(std::move(item));
        if(!purchase) resources.items.erase(stock);
        refresh(c,party.item_catalog());return result(EconomyStatus::applied,{},id);
    });
}
EconomyResult EconomyService::buy(std::size_t member,std::uint64_t stock_id) {return acquire(member,stock_id,true);}
EconomyResult EconomyService::acquire_reward(std::size_t member,std::uint64_t reward_id) {return acquire(member,reward_id,false);}
EconomyResult EconomyService::sell(std::size_t member,ItemId id,bool confirmed) {
    if(member>=party_.size()) return reject("Choose an actual party member");
    return mutate([&](Party& party,CampaignResources& resources) {
        auto& c=party.members_[member];auto& items=c.inventory_.items_;
        if(c.byte(0x84)>=128 && c.active() && c.status()!=1) return reject("This party member cannot sell personal equipment");
        const auto item=std::find_if(items.begin(),items.end(),[&](const auto& value){return value.id()==id;});
        if(item==items.end()) return reject("Item is no longer owned by this character");
        if(item->readied()) return reject("Unready this item before selling it");
        const auto quote=WealthRules::sale_quote(*item);
        if(!confirmed) {auto outcome=result(EconomyStatus::confirmation_required,{},id);outcome.gold_quote=quote;return outcome;}
        items.erase(item);refresh(c,party.item_catalog());
        const auto platinum=static_cast<std::uint16_t>(quote/5),gold=static_cast<std::uint16_t>(quote%5);
        if(fits(c,static_cast<std::uint16_t>(platinum+gold))) {add_money(c,4,platinum);add_money(c,3,gold);}
        else {const auto held=std::min(platinum,space(c));add_money(c,4,held);resources.amounts[4]+=platinum-held;add_money(c,3,gold);}
        refresh(c,party.item_catalog());auto outcome=result(EconomyStatus::applied,{},id);outcome.gold_quote=quote;return outcome;
    });
}
EconomyResult EconomyService::identify(std::size_t member,ItemId id,bool confirmed) {
    return mutate([&](Party& party,CampaignResources& resources) {
        std::array<std::uint32_t,5> pool{};std::copy_n(resources.amounts.begin(),5,pool.begin());
        const auto answer=InventoryService(party).identify(member,id,{true,&pool},confirmed);
        const auto status=answer.status==ItemActionStatus::applied?EconomyStatus::applied:
            answer.status==ItemActionStatus::confirmation_required?EconomyStatus::confirmation_required:
            answer.status==ItemActionStatus::unresolved?EconomyStatus::unresolved:EconomyStatus::rejected;
        if(answer.applied()) std::copy(pool.begin(),pool.end(),resources.amounts.begin());
        return result(status,answer.message,answer.item);
    });
}
EconomyResult EconomyService::appraise_gem(std::size_t member,std::mt19937& random,AppraisalQuote& quote) {return appraise(member,false,random,quote);}
EconomyResult EconomyService::appraise_jewelry(std::size_t member,std::mt19937& random,AppraisalQuote& quote) {return appraise(member,true,random,quote);}
EconomyResult EconomyService::appraise(std::size_t member,bool jewelry,std::mt19937& random,AppraisalQuote& quote) {
    if(member>=party_.size()) return reject("Choose an actual party member");
    const std::size_t purse=jewelry?0x94:0x92;
    if(!party_.member(member).word(purse)) return reject("This character has no unappraised items of this kind");
    auto staged_random=random;AppraisalQuote staged_quote;
    const auto outcome=mutate([&](Party& party,CampaignResources&) {
        auto& c=party.members_[member];
        // Use the port's shared unbiased d100 stream. Native 49D5:0DE3 is
        // one die of 100 sides; its DOS PRNG is not replayed by this port.
        const auto bounded=[&](std::uint32_t span) {
            const auto threshold=(std::uint32_t{0}-span)%span;
            std::uint32_t draw;do {draw=static_cast<std::uint32_t>(staged_random());}while(draw<threshold);
            return draw%span;
        };
        const auto d100=static_cast<std::uint8_t>(bounded(100)+1);
        staged_quote.member_=member;staged_quote.revision_=party.revision_+1;
        staged_quote.jewelry_=jewelry;
        if(jewelry) {
            const auto range=WealthRules::jewelry_appraisal_range(d100);
            // Native 11C0 returns integer Random(span), then XOR DX and
            // 119A/119E convert it through Real. 0..9999 roundtrips exactly.
            staged_quote.gold_=static_cast<std::uint16_t>(range[0]+bounded(range[1]));
        } else staged_quote.gold_=WealthRules::gem_appraisal_value(d100);
        staged_quote.keep_=fits(c,1) && c.inventory_.size()<16;
        c.set_word(purse,static_cast<std::uint16_t>(c.word(purse)-1));
        // 19B2 does not refresh cached load until its Sell/Keep decision.
        // Recalculating here would incorrectly make a full purse Keep-able.
        return result(EconomyStatus::applied);
    });
    if(outcome.applied()) {random=std::move(staged_random);quote=staged_quote;}
    return outcome;
}
EconomyResult EconomyService::resolve_appraisal(const AppraisalQuote& quote,bool keep) {
    if(quote.member_>=party_.size() || !quote.gold_ || quote.revision_!=party_.revision())
        return reject("Stale appraisal decision");
    if(keep && !quote.keep_) return reject("Character cannot keep this appraised item");
    return mutate([&](Party& party,CampaignResources& resources) {
        auto& c=party.members_[quote.member_];ItemId id=0;
        if(keep) {
            if(c.inventory_.size()>=16 || !fits(c,1)) return reject("Character cannot keep this appraised item");
            if(party.next_item_id_==std::numeric_limits<ItemId>::max()) throw DecodeError("Owned item identity exhausted");
            std::array<std::uint8_t,63> bytes{};
            bytes[0x2e]=0x46;bytes[0x31]=quote.jewelry_?0xd6:0x65;bytes[0x37]=1;
            bytes[0x3a]=static_cast<std::uint8_t>(quote.gold_);bytes[0x3b]=static_cast<std::uint8_t>(quote.gold_>>8);
            auto item=Item::decode(bytes,{"GAME.OVR",{},quote.jewelry_?std::size_t{0x26db3}:std::size_t{0x26a60}}); // 43EC:212A/1DD7 factories.
            item.id_=party.next_item_id_++;id=item.id_;c.inventory_.items_.push_back(std::move(item));
        } else {
            const auto platinum=static_cast<std::uint16_t>(quote.gold_/5);
            // 43EC:0203 uses cached pre-decrement load and WORD wrapping.
            const auto held=fits(c,platinum)?platinum:space(c);
            add_money(c,4,held);resources.amounts[4]+=static_cast<std::uint32_t>(platinum)-held;
        }
        refresh(c,party.item_catalog());
        auto outcome=result(EconomyStatus::applied,{},id);outcome.gold_quote=quote.gold_;return outcome;
    });
}
}
