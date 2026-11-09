#include "gbengine/campaign/treasure_service.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace GBEngine {
namespace {
std::uint8_t treasure_roll(std::mt19937& random,std::uint32_t sides) {
    const auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    std::uint32_t value;do {value=static_cast<std::uint32_t>(random());}while(value<threshold);
    return static_cast<std::uint8_t>(value%sides+1);
}
std::uint8_t random_type(std::mt19937& random,std::optional<std::uint8_t> previous) {
    const auto group=treasure_roll(random,100);
    if(group<=60) {
        const auto type=treasure_roll(random,100);
        if(type<=47 || (type>=50 && type<=59))return type==45?59:type;
        if(type>=60 && type<=90) {
            const auto weapon=treasure_roll(random,10);
            return weapon<=4?36:weapon<=7?35:weapon==8?34:weapon==9?37:38;
        }
        if(type>=91 && type<=94)return 73;
        if(type>=95 && type<=97)return 93;
        if(type>=98)return 77;
        // 48/49 fall through every native branch with an uninitialized
        // selector. Never turn that source defect into an invented item.
        if(previous)return *previous;
        throw DecodeError("Native random treasure selector is uninitialized for first roll48/49");
    }
    if(group<=85)return 61;
    if(group<=92)return 62; // 91/92 reach this branch before nominal91..98.
    if(group<=98) {
        const auto type=treasure_roll(random,16);
        return type<=7?71:type<=9?70:type==10?84:type<=12?78:type<=14?79:type==15?92:67;
    }
    return 69;
}
Item generate_item(const ItemCatalog& catalog,std::mt19937& random,std::optional<std::uint8_t>& previous) {
    const auto type=random_type(random,previous);previous=type;
    std::array<std::uint8_t,63> bytes{};
    bytes[0x2e]=type;bytes[0x35]=6;
    auto word=[&](std::size_t at,unsigned value){bytes[at]=static_cast<std::uint8_t>(value);bytes[at+1]=static_cast<std::uint8_t>(value>>8);};
    unsigned table=0;
    if((type>=1 && type<=59) || type==73 || type==77 || type==93) {
        const auto modifier=treasure_roll(random,20);
        bytes[0x32]=modifier<=14?1:modifier<=19?2:3;
        const auto bonus=bytes[0x32];
        if(type==21) {if(treasure_roll(random,5)==5)table=144;bytes[0x31]=21;bytes[0x30]=static_cast<std::uint8_t>(bonus+161);}
        else if(type==28) {bytes[0x31]=28;bytes[0x30]=static_cast<std::uint8_t>(bonus+161);}
        else if(type>=50 && type<=58) {bytes[0x31]=type;bytes[0x30]=type<=51?49:type==52?50:48;bytes[0x2f]=static_cast<std::uint8_t>(bonus+161);bytes[0x35]=4;}
        else if(type==73) {bytes[0x31]=61;bytes[0x30]=static_cast<std::uint8_t>(bonus+161);}
        else if(type==77) {bytes[0x31]=79;bytes[0x30]=167;bytes[0x32]=static_cast<std::uint8_t>(bonus*2+2);bytes[0x2f]=bytes[0x32]==4?221:bytes[0x32]==6?222:220;}
        else if(type==93) {bytes[0x31]=66;bytes[0x30]=224;bytes[0x2f]=static_cast<std::uint8_t>(bonus+161);}
        else {bytes[0x31]=type;bytes[0x30]=static_cast<std::uint8_t>(bonus+161);}
        constexpr std::array<unsigned,60> weights{40,75,50,125,100,150,15,30,10,25,60,80,150,75,75,100,80,150,175,150,50,20,40,100,125,80,60,80,40,50,40,50,50,50,100,75,60,35,250,50,125,80,50,100,50,100,50,80,40,40,150,100,200,250,400,300,400,350,450,50};
        unsigned weight=type<weights.size()?weights[type]:type==77?10:type==93?1:40;
        // The native fallback gives otherwise-unlisted types quantity10.
        if(type==48 || type==49 || type==73)bytes[0x39]=10;
        if(type==9)bytes[0x39]=5;
        word(0x37,weight);
        unsigned factor=type==59?2500:(type==73 || type==28)?150:(type==53 || type==54 || type==77)?3000:(type==55 || type==56)?3500:type==57?4000:type==58?5000:2000;
        word(0x3a,bytes[0x32]*factor);
    } else if(type==61 || type==62) {
        const auto count=treasure_roll(random,3);
        bytes[0x31]=type==61?209:208;bytes[0x30]=static_cast<std::uint8_t>(count-47);bytes[0x32]=1;word(0x37,25);
        unsigned value=0;
        for(unsigned i=0;i<count;++i) {
            const auto level=treasure_roll(random,3);
            const auto sides=type==61?(level==1?13u:level==2?7u:11u):(level==1?8u:level==2?7u:9u);
            const auto base=type==61?(level==1?8u:level==2?28u:44u):(level==1?0u:level==2?21u:35u);
            bytes[0x3c+i]=static_cast<std::uint8_t>(base+treasure_roll(random,sides));value+=level*300;
        }
        word(0x3a,value);
    } else {
        switch(type) {
        case 63:table=73;break;
        case 67:table=145;break;
        case 69: {const auto roll=treasure_roll(random,10);table=roll==1?41:roll<=6?121:129;break;}
        case 78: {const auto roll=treasure_roll(random,3);table=roll==1?65:roll==2?113:0;break;}
        case 79:table=57;break;
        case 84:table=9;break;
        case 92:table=97;break;
        case 71: {const auto roll=treasure_roll(random,11);table=roll<=5?17:roll<=8?1:roll==9?25:roll==10?81:0;break;}
        case 70:table=treasure_roll(random,10)<=7?89:137;break;
        default:throw DecodeError("Unknown native random treasure type");
        }
    }
    if(table) {
        for(unsigned i=0;i<3;++i)bytes[0x2f+i]=static_cast<std::uint8_t>(catalog.generated_item_word(table+i));
        bytes[0x32]=bytes[0x33]=table==97?2:1;
        word(0x37,catalog.generated_item_word(table+3));word(0x3a,catalog.generated_item_word(table+4));
        bytes[0x39]=0;
        for(unsigned i=0;i<3;++i)bytes[0x3c+i]=static_cast<std::uint8_t>(catalog.generated_item_word(table+5+i));
    }
    (void)catalog.definition(type);
    return Item::decode(bytes,{"START.EXE:random-treasure",{},type});
}
}
TreasureResult TreasureService::prepare(CampaignResources& resources,const ItemCatalog& catalog,
                                        unsigned disk,std::span<const std::uint16_t,8> operands,
                                        std::mt19937& random) {
    const auto selector=static_cast<std::uint8_t>(operands[7]);
    if(disk<1 || disk>8)
        return {TreasureStatus::rejected,"Treasure resource disk is outside the supported range"};
    if(resources.next_item_id==0 || resources.items.size()>maximum_items)
        return {TreasureStatus::rejected,"Campaign treasure identity or count is invalid"};
    std::set<CampaignItemId> identities;
    for(const auto& entry:resources.items) {
        if(entry.id==0 || entry.id>=resources.next_item_id || entry.item.id()!=0 || !identities.insert(entry.id).second)
            return {TreasureStatus::rejected,"Campaign treasure item identity is invalid"};
    }
    CampaignResources staged=resources;
    auto staged_random=random;
    for(std::size_t i=0;i<staged.amounts.size();++i)staged.amounts[i]=operands[i];
    if(selector<0x80) {
        std::vector<Item> additions;
        for(const auto& [key,item]:catalog.templates()) {
            if(key.disk==disk && key.record==selector) {
                if(key.ordinal!=additions.size())
                    return {TreasureStatus::rejected,"Treasure template ordinals are not consecutive"};
                (void)catalog.definition(item.type());
                if(item.id()!=0)
                    return {TreasureStatus::rejected,"Treasure template already has a party identity"};
                additions.push_back(item);
            }
        }
        if(additions.empty())
            return {TreasureStatus::rejected,"Treasure template archive record is missing or empty"};
        if(additions.size()>maximum_items-staged.items.size())
            return {TreasureStatus::rejected,"Campaign treasure item count exceeds the bounded limit"};
        if(additions.size()>std::numeric_limits<CampaignItemId>::max()-staged.next_item_id)
            return {TreasureStatus::rejected,"Campaign treasure identity allocation is exhausted"};
        std::vector<CampaignItem> prepended;
        prepended.reserve(additions.size()+staged.items.size());
        for(const auto& item:additions)prepended.push_back({staged.next_item_id++,item});
        std::reverse(prepended.begin(),prepended.end());
        prepended.insert(prepended.end(),staged.items.begin(),staged.items.end());
        staged.items=std::move(prepended);
    } else if(selector<0xff) {
        const auto count=static_cast<std::size_t>(selector-0x80);
        if(count>maximum_items-staged.items.size() || count>std::numeric_limits<CampaignItemId>::max()-staged.next_item_id)
            return {TreasureStatus::rejected,"Campaign random treasure count or identity limit is exhausted"};
        std::vector<CampaignItem> additions;additions.reserve(count+staged.items.size());
        std::optional<std::uint8_t> previous;
        try {
            for(std::size_t i=0;i<count;++i)additions.push_back({staged.next_item_id++,generate_item(catalog,staged_random,previous)});
        } catch(const DecodeError& error) {return {TreasureStatus::unresolved,error.what()};}
        std::reverse(additions.begin(),additions.end());
        additions.insert(additions.end(),staged.items.begin(),staged.items.end());staged.items=std::move(additions);
    }
    resources=std::move(staged);
    random=std::move(staged_random);
    return {TreasureStatus::applied,{}};
}
void TreasureService::clear(CampaignResources& resources) {
    resources.amounts.fill(0);
    resources.items.clear();
    resources.combat_setup=false;
}
TreasureResult TreasureService::begin_collection(Party& party,CampaignResources& resources,
                                                 std::uint16_t native_party_count,std::int64_t encounter_experience) {
    if(resources.combat_setup)
        return {TreasureStatus::unresolved,"Combat reward participants are unavailable"};
    if(party.members_.empty() || native_party_count!=party.members_.size())
        return {TreasureStatus::unresolved,"Reward party count does not match the imported roster"};
    bool player_survivor=false,clears_fled=false;
    std::uint8_t excluded=0;
    for(const auto& member:party.members_) {
        if(member.byte(0x10e)!=0)
            return {TreasureStatus::unresolved,"Combat reward participant ownership is unavailable"};
        if(member.byte(0x84)<128 && (member.status()==0 || member.status()==1 || member.status()==3))player_survivor=true;
        if(member.status()==0 || member.status()==1)clears_fled=true;
        if(!member.active() || member.status()==1)++excluded;
    }
    if(!player_survivor || !clears_fled)
        return {TreasureStatus::unresolved,"Native defeat or fled-only participant removal is unavailable"};
    const auto denominator=static_cast<std::uint16_t>(native_party_count-excluded);
    if(!denominator)return {TreasureStatus::unresolved,"Native reward divisor is zero"};
    constexpr auto maximum=static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    for(const auto amount:resources.amounts)if(amount>maximum)
        return {TreasureStatus::unresolved,"Negative native reward accumulators are unsupported"};
    if(encounter_experience < -static_cast<std::int64_t>(maximum) || encounter_experience>static_cast<std::int64_t>(maximum))
        return {TreasureStatus::unresolved,"Encounter experience exceeds the bounded native signed range"};
    std::int64_t total=encounter_experience;
    total+=resources.amounts[0]/200u+resources.amounts[1]/20u+resources.amounts[2]/2u;
    total+=resources.amounts[3];
    total+=static_cast<std::int64_t>(resources.amounts[4])*5;
    total+=static_cast<std::int64_t>(resources.amounts[5])*250;
    total+=static_cast<std::int64_t>(resources.amounts[6])*2200;
    for(const auto& entry:resources.items) {
        const auto modifier=entry.item.bonus();
        if(modifier>0) {
            // Native MUL keeps AX then CWD sign-extends that word before
            // adding to the DWORD; very large positive modifiers can wrap.
            const auto product=modifier*400;
            total+=product<32768?product:product-65536;
        }
    }
    if(total<0 || static_cast<std::uint64_t>(total)>maximum)
        return {TreasureStatus::unresolved,"Reward experience total exceeds the bounded native signed range"};
    const auto base=static_cast<std::uint32_t>(total/denominator);
    auto staged=party.members_;
    for(auto& member:staged) {
        if(!member.active() || member.status()==1)continue;
        auto award=base;
        const auto native_class=member.byte(0x2f);
        const auto prime=native_class==0?0x12:native_class==2?0x10:native_class==5?0x11:native_class==6?0x13:0;
        if(prime!=0 && member.byte(static_cast<std::size_t>(prime))>15)award+=base/10u;
        else if(native_class==8 || (native_class>=10 && native_class<=14) || native_class==16)award=base/2u;
        else if(native_class==9 || native_class==15)award=base/3u;
        const auto before=static_cast<std::uint32_t>(member.word(0xac)) |
                          (static_cast<std::uint32_t>(member.word(0xae))<<16);
        const auto after=static_cast<std::uint32_t>(before+award);
        member.set_word(0xac,static_cast<std::uint16_t>(after));
        member.set_word(0xae,static_cast<std::uint16_t>(after>>16));
    }
    // Recovery follows XP. A recovered member does not retroactively change
    // the share denominator or receive the award it was ineligible for.
    for(auto& member:staged) {
        if(member.status()==3) {member.set_byte(0x10c,0);member.set_byte(0x10d,1);}
        else if(member.status()==5)member.set_byte(0x10c,4);
        else if(member.status()==4 && member.hit_points()!=0) {member.set_byte(0x10c,0);member.set_byte(0x10d,1);}
    }
    auto staged_resources=resources;
    std::uint8_t npc_shares=0,total_shares=0;
    for(const auto& member:staged) {
        const auto weight=static_cast<std::uint8_t>(member.byte(0x85)&7);
        if(member.byte(0x84)>=128 && member.status()==0) {npc_shares=static_cast<std::uint8_t>(npc_shares+weight);total_shares=static_cast<std::uint8_t>(total_shares+weight);}
        else ++total_shares;
    }
    bool deducted=false;
    if(npc_shares) {
        if(!total_shares)return {TreasureStatus::unresolved,"Native NPC share divisor is zero"};
        for(auto& amount:staged_resources.amounts)if(amount) {
            const auto per_share=static_cast<std::uint8_t>(amount/total_shares);
            const auto deduction=static_cast<std::uint16_t>(npc_shares*per_share);
            amount-=deduction;deducted=true;
        }
    }
    std::vector<std::size_t> recipients;
    if(deducted)for(std::size_t i=0;i<staged.size();++i)
        if(staged[i].byte(0x84)>=128 && staged[i].status()==0 && staged[i].byte(0x85)!=0)recipients.push_back(i);
    party.members_=std::move(staged);
    resources=std::move(staged_resources);
    ++party.revision_;
    return {TreasureStatus::applied,{},base,std::move(recipients)};
}
}
