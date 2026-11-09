#include "gbengine/combat/aftermath_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace GBEngine {
namespace {
unsigned roll(std::mt19937& random,unsigned sides) {
    const auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    std::uint32_t value;do {value=static_cast<std::uint32_t>(random());}while(value<threshold);
    return value%sides+1;
}
constexpr auto maximum_reward=static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
std::uint32_t experience_share(const CampaignResources& resources,std::int64_t encounter,
                               std::uint16_t denominator,std::span<const CampaignItem> new_items) {
    if(!denominator)throw DecodeError("Native combat reward divisor is zero");
    if(encounter < -static_cast<std::int64_t>(maximum_reward) || encounter>static_cast<std::int64_t>(maximum_reward))
        throw DecodeError("Encounter experience exceeds the bounded native signed range");
    std::int64_t total=encounter;
    for(const auto amount:resources.amounts)if(amount>maximum_reward)
        throw DecodeError("Negative native reward accumulators are unsupported");
    total+=resources.amounts[0]/200u+resources.amounts[1]/20u+resources.amounts[2]/2u;
    total+=resources.amounts[3];total+=static_cast<std::int64_t>(resources.amounts[4])*5;
    total+=static_cast<std::int64_t>(resources.amounts[5])*250;
    total+=static_cast<std::int64_t>(resources.amounts[6])*2200;
    // The caller supplies the list prefix before native5D9A. With no thrown
    // ground sentinel this is the complete treasury, including scripted items.
    for(const auto& entry:new_items)if(entry.item.bonus()>0) {
        const auto product=entry.item.bonus()*400;
        total+=product<32768?product:product-65536;
    }
    if(total<0 || static_cast<std::uint64_t>(total)>maximum_reward)
        throw DecodeError("Reward experience total exceeds the bounded native signed range");
    return static_cast<std::uint32_t>(total/denominator);
}
void award(Character& member,std::uint32_t base) {
    if(!member.active() || member.status()==1)return;
    auto amount=base;
    const auto native_class=member.byte(0x2f);
    const auto prime=native_class==0?0x12:native_class==2?0x10:native_class==5?0x11:native_class==6?0x13:0;
    if(prime && member.byte(static_cast<std::size_t>(prime))>15)amount+=base/10u;
    else if(native_class==8 || (native_class>=10 && native_class<=14) || native_class==16)amount=base/2u;
    else if(native_class==9 || native_class==15)amount=base/3u;
    const auto before=static_cast<std::uint32_t>(member.word(0xac))|(static_cast<std::uint32_t>(member.word(0xae))<<16);
    const auto after=static_cast<std::uint32_t>(before+amount);
    member.set_word(0xac,static_cast<std::uint16_t>(after));member.set_word(0xae,static_cast<std::uint16_t>(after>>16));
}
}
CombatAftermathResult CombatAftermathService::stage(const DeploymentState& deployment,const Party& party,
                                                   const CampaignResources& resources,const std::mt19937& random,
                                                   CombatAftermathContext context) {
    if(party.size()==0 || party.size()>8 || context.native_party_count!=party.size())
        throw DecodeError("Combat aftermath requires authoritative persistent party count");
    if(deployment.actors.size()<party.size() || deployment.actors.size()>71)
        throw DecodeError("Combat aftermath tactical actor count is invalid");
    CombatAftermathResult result;result.party=party;result.resources=resources;result.random=random;
    result.special=context.surrogate;
    std::set<std::uint64_t> actor_ids;
    for(std::size_t i=0;i<deployment.actors.size();++i) {
        const auto& actor=deployment.actors[i];
        if(i<party.size()) {
            if(actor.id!=CombatActorIdentity{CombatActorOrigin::party,i} || actor.removed || actor.extension[0x13])
                throw DecodeError("Combat aftermath persistent roster identity is invalid");
            result.party.members_[i]=actor.character;
            // Quick is local to this battle, including fled or inactive members.
            result.party.members_[i].set_byte(0x10f,0);
        } else if(actor.id.origin!=CombatActorOrigin::encounter || !actor.extension[0x13] ||
                  !actor.id.value || !actor_ids.insert(actor.id.value).second)
            throw DecodeError("Combat aftermath encounter actor identities are invalid");
        if(actor.character.byte(0x10e)>1)throw DecodeError("Combat aftermath allegiance is outside the native two sides");
    }
    if(context.next_item_id) {
        if(context.next_item_id<party.next_item_id_)throw DecodeError("Combat generated-item allocator moved backwards");
        for(const auto& member:result.party.members_)for(const auto& item:member.inventory_.items_)
            if(!item.id() || item.id()>=context.next_item_id)throw DecodeError("Combat generated item exceeds its owned allocator");
        result.party.next_item_id_=context.next_item_id;
    }
    if(result.resources.next_item_id==0 || result.resources.items.size()>TreasureService::maximum_items)
        throw DecodeError("Combat reward resources have invalid item count or identity allocator");
    std::set<CampaignItemId> item_ids;
    for(const auto& entry:result.resources.items)
        if(!entry.id || entry.id>=result.resources.next_item_id || entry.item.id()!=0 || !item_ids.insert(entry.id).second)
            throw DecodeError("Combat reward resources have invalid campaign item identities");
    auto append_item=[&](const Item& item,std::vector<CampaignItem>& destination) {
        if(result.resources.items.size()+destination.size()>=TreasureService::maximum_items ||
           result.resources.next_item_id==std::numeric_limits<CampaignItemId>::max())
            throw DecodeError("Combat reward item count or identity allocation exhausted");
        auto copy=item;copy.id_=0;copy.bytes_[0x34]=0;
        destination.push_back({result.resources.next_item_id++,std::move(copy)});
    };
    std::vector<CampaignItem> ground;
    for(const auto& item:deployment.ground_items)append_item(item,ground);
    ground.insert(ground.end(),result.resources.items.begin(),result.resources.items.end());
    result.resources.items=std::move(ground);
    std::size_t reward_prefix=deployment.ground_items.empty()?result.resources.items.size():0;
    result.resources.combat_setup=false;
    bool defeat=!context.surrogate,fled=false,won=false;
    std::uint8_t excluded=0;
    for(const auto& member:result.party.members_)if(member.status()==3)fled=true;
    if(!context.surrogate)for(const auto& member:result.party.members_) {
        const auto status=member.status();
        if((status==0 || status==1 || status==3 || context.special_reward_policy==1) &&
           member.byte(0x10e)==0 && member.byte(0x84)<128)defeat=false;
        if(status==0 || status==1) {won=true;fled=false;}
        if(!member.active() || status==1)++excluded;
    }
    const auto collect=[&](bool special_reward) {
        std::vector<CampaignItem> additions;
        std::uint8_t pickup_count=0;
        std::int64_t experience=0;
        if(special_reward) {
            if(!context.selected_actor)throw DecodeError("Duel experience requires the source selected actor");
            const auto found=std::find_if(deployment.actors.begin(),deployment.actors.end(),[&](const auto& actor){return actor.id==*context.selected_actor;});
            if(found==deployment.actors.end() || found->removed)throw DecodeError("Duel experience selected actor is unavailable");
            const auto raw=found->character.byte(0x73);
            experience=(raw<128?static_cast<int>(raw):static_cast<int>(raw)-256)*100;
        } else for(const auto& actor:deployment.actors) {
            if(actor.removed || actor.character.byte(0x10e)!=1 || actor.character.status()==3)continue;
            const auto& character=actor.character;result.enemy_rewards=true;
            for(std::size_t currency=0;currency<7;++currency)
                result.resources.amounts[currency]+=character.word(0x88+currency*2);
            const auto product=static_cast<unsigned>(character.byte(0xba))*character.byte(0xb1);
            experience+=character.word(0xb8)+(product<32768?static_cast<int>(product):static_cast<int>(product)-65536);
            if(context.loot_policy==1)continue;
            for(const auto& item:character.inventory_.items_) {
                const bool accepted=item.value()!=0 || (pickup_count<8 && roll(result.random,10)<4);
                if(!accepted)continue;
                pickup_count=static_cast<std::uint8_t>(pickup_count+1);append_item(item,additions);
            }
        }
        const auto denominator=static_cast<std::uint16_t>(context.native_party_count-excluded);
        auto experience_items=additions;
        experience_items.insert(experience_items.end(),result.resources.items.begin(),result.resources.items.begin()+reward_prefix);
        // Native0000's special branch returns selected signed73*100 directly,
        // bypassing both the treasury conversion and normal party division.
        if(special_reward && (experience<0 || static_cast<std::uint64_t>(experience)>maximum_reward))
            throw DecodeError("Surrogate experience exceeds the bounded native signed range");
        const auto base=special_reward?static_cast<std::uint32_t>(experience):
            experience_share(result.resources,experience,denominator,experience_items);
        result.encounter_experience+=experience;
        result.collection.experience_per_member=base;
        for(auto& member:result.party.members_)award(member,base);
        reward_prefix+=additions.size();
        std::reverse(additions.begin(),additions.end());
        additions.insert(additions.end(),result.resources.items.begin(),result.resources.items.end());
        result.resources.items=std::move(additions);
    };
    if(!context.surrogate) {
        // Native selector reaches aftermath only when a side has no active
        // participants. Pure callers cannot publish a premature victory.
        bool active_party=false,active_enemy=false;
        for(const auto& actor:deployment.actors)if(!actor.removed && actor.character.active()) {
            if(actor.character.byte(0x10e)==1)active_enemy=true;else active_party=true;
        }
        if(active_party && active_enemy)throw DecodeError("Combat aftermath cannot finish while both sides remain active");
        if(won)collect(false);
    } else {
        // Native04C9 repeats reward/award for each qualifying linked actor;
        // selected5D92 is unchanged throughout this scan.
        for(const auto& actor:deployment.actors)if(!actor.removed && actor.character.byte(0x10d)==1 &&
            actor.character.status()==0 && actor.character.byte(0x10e)!=1) {won=true;collect(context.special_reward_policy!=0);}
        for(auto& member:result.party.members_) {
            if(member.status()==0 || member.status()==1)member.set_byte(0x10d,1);
            if(member.status()==5)member.set_byte(0x10c,4);
        }
    }
    result.experience_awarded=won;
    std::vector<bool> removed(party.size(),false);
    if(!context.surrogate) {
        if(defeat) {std::fill(removed.begin(),removed.end(),true);result.outcome=CombatOutcome::defeat;result.source_result=0x80;result.collection_ready=false;}
        else if(fled) {
            result.outcome=CombatOutcome::flight;result.source_result=0x81;
            for(std::size_t i=0;i<party.size();++i) {
                auto& member=result.party.members_[i];
                if(member.status()==3) {member.set_byte(0x10c,0);member.set_byte(0x10d,1);}else removed[i]=true;
            }
        } else for(auto& member:result.party.members_) {
            if(member.status()==3) {member.set_byte(0x10c,0);member.set_byte(0x10d,1);}
            else if(member.status()==5)member.set_byte(0x10c,4);
            else if(member.status()==4 && member.hit_points()!=0) {member.set_byte(0x10c,0);member.set_byte(0x10d,1);}
        }
    }
    // Native11AC: remove temporaries and opposing persistent actors AFTER
    // recovery/flight/defeat and BEFORE NPC shares, then select the head.
    for(std::size_t i=0;i<deployment.actors.size();++i) {
        const auto& actor=deployment.actors[i];
        if(actor.removed || (i<party.size() && removed[i]))continue;
        const auto& character=i<party.size()?result.party.members_[i]:actor.character;
        const auto active=context.surrogate && (character.status()==0 || character.status()==1)?1:character.byte(0x10d);
        if(actor.extension[0x13] || character.byte(0x10e)==1) {
            if(active!=1)++result.defeated_count;
            if(i<party.size())removed[i]=true;
        }
    }
    std::vector<Character> retained;
    for(std::size_t i=0;i<party.size();++i) {
        if(removed[i])result.removed_persistent.push_back(i);else retained.push_back(std::move(result.party.members_[i]));
    }
    result.party.members_=std::move(retained);
    result.party.selected_=result.party.members_.empty()?std::nullopt:std::optional<std::size_t>{0};
    ++result.party.revision_;
    result.collection.status=TreasureStatus::applied;
    if(result.collection_ready) {
        if(result.party.members_.empty())throw DecodeError("Native combat collection has no retained participant");
        std::uint8_t npc_shares=0,total_shares=0;
        for(const auto& member:result.party.members_) {
            const auto weight=static_cast<std::uint8_t>(member.byte(0x85)&7);
            if(member.byte(0x84)>=128 && member.status()==0) {npc_shares=static_cast<std::uint8_t>(npc_shares+weight);total_shares=static_cast<std::uint8_t>(total_shares+weight);}
            else ++total_shares;
        }
        bool deducted=false;
        if(npc_shares) {
            if(!total_shares)throw DecodeError("Native NPC share divisor is zero");
            for(auto& amount:result.resources.amounts)if(amount) {
                const auto per_share=static_cast<std::uint8_t>(amount/total_shares);
                amount-=static_cast<std::uint16_t>(npc_shares*per_share);deducted=true;
            }
        }
        if(deducted)for(std::size_t i=0;i<result.party.size();++i)
            if(result.party.member(i).byte(0x84)>=128 && result.party.member(i).status()==0 && result.party.member(i).byte(0x85)!=0)
                result.collection.npc_members.push_back(i);
    }
    if(context.surrogate) {
        result.result_text_id=pascal_string_id("GAME.OVR",won?0x5DD3:0x5DBB);
        result.experience_prefix_id=pascal_string_id("GAME.OVR",0x5E2F);
    } else {
        const auto text=result.outcome==CombatOutcome::defeat?0x6A4C:!result.enemy_rewards?(fled?0x5DFD:0x5E11):0x5DEA;
        result.result_text_id=pascal_string_id("GAME.OVR",text);
        result.experience_prefix_id=pascal_string_id("GAME.OVR",0x5E45);
    }
    return result;
}
}
