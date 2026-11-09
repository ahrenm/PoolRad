#include "gbengine/campaign/shrine_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <bit>

namespace GBEngine {
namespace {
constexpr std::array<std::uint8_t,6> disease_codes{0x1f,0x22,0x2b,0x2c,0x32,0x39};
constexpr std::array<std::uint16_t,9> fees{1000,1000,100,350,600,1000,5500,3500,2000};
constexpr std::array<std::size_t,9> names{18353,18519,18749,18767,18787,19638,19068,19876,20112};
constexpr std::array<std::size_t,9> questions{18339,18502,0,0,0,19621,19055,19861,20097};
EconomyResult outcome(EconomyStatus status,std::string message={}) {return {status,std::move(message),{},0};}
std::uint8_t roll(std::mt19937& random,unsigned count) {
    std::uint8_t sum=0;for(unsigned i=0;i<count;++i)sum=static_cast<std::uint8_t>(sum+random()%8+1);return sum;
}
bool healing(ShrineAction action) {return action>=ShrineAction::cure_light && action<=ShrineAction::cure_critical;}
}
ShrineInspection ShrineService::inspect(const Party& party,std::size_t member,ShrineAction action) {
    const auto index=static_cast<std::size_t>(action);
    if(index>=fees.size() || member>=party.size())return {EconomyStatus::rejected,0,false,{}, {},"Choose an actual shrine service and party member"};
    const auto& c=party.member(member);
    ShrineInspection result{EconomyStatus::applied,fees[index],true,{},pascal_string_id("GAME.OVR",names[index]),{}};
    if(!c.effects_restored()) {result.status=EconomyStatus::unresolved;result.message="Shrine service requires a restored owned effect list";return result;}
    const auto has=[&](std::uint8_t code){return MagicEffectService::has_effect(c,code);};
    switch(action) {
        case ShrineAction::cure_blindness:result.needed=has(0x21);break;
        case ShrineAction::cure_disease:result.needed=std::any_of(disease_codes.begin(),disease_codes.end(),has);break;
        case ShrineAction::neutralize_poison:result.needed=has(0x37);break;
        case ShrineAction::raise_dead:result.needed=c.status()==6 || c.status()==1;break;
        case ShrineAction::remove_curse:
            result.needed=has(0x24);
            for(std::size_t i=0;i<c.inventory().size();++i)result.needed=result.needed || c.inventory().item(i).cursed();
            break;
        case ShrineAction::stone_to_flesh:result.needed=c.status()==7;break;
        default:break;
    }
    if(!result.needed && questions[index])result.question_id=pascal_string_id("GAME.OVR",questions[index]);
    return result;
}
void ShrineService::remove(Character& c,std::uint8_t code,bool suppressed) {
    const auto it=std::find_if(c.effects_.begin(),c.effects_.end(),[&](const auto& effect){return effect.code()==code;});
    if(it==c.effects_.end())return;
    const auto index=static_cast<std::size_t>(it-c.effects_.begin());
    if(suppressed && it->payload[4]) {
        if(code==0x20) {
            // 2EE2:0B1B still restores allegiance/cache bytes under681F.
            c.set_byte(0x10e,it->payload[3]>>4);c.set_byte(0x10f,1);c.set_byte(0x76,0);
            c.set_byte(0x6b,c.byte(0x98));c.set_byte(0x72,12);
            if(c.byte(0x84)==0xb3)c.set_byte(0x84,0);
            c.set_byte(0x9f,0);
        } else if(code==0x16) {
            // 2EE2:0775 removes the first0F itself before the caller's0F.
            remove(c,0x0f,true);
        } else if(code==0x0f || code==0x37 || code==0x1f) {
            // 001F is vetoed by681F;37 is empty;1F clears combat scratch.
        } else {MagicEffectService::remove_native(c,index,true);return;}
        // A nested16 removal may shift this node; retain its owned index.
        const auto node=std::find_if(c.effects_.begin(),c.effects_.end(),[&](const auto& effect){return effect.code()==code;});
        if(node!=c.effects_.end())c.effects_.erase(node);
    } else MagicEffectService::remove_native(c,index,suppressed);
}
EconomyResult ShrineService::apply(std::size_t member,ShrineAction action,std::mt19937& random,bool confirmed_unneeded,bool confirmed_fee) {
    const auto inspection=inspect(party_,member,action);
    if(inspection.status!=EconomyStatus::applied)return outcome(inspection.status,inspection.message);
    if(!inspection.needed && !confirmed_unneeded)return outcome(EconomyStatus::confirmation_required);
    if(!confirmed_fee)return outcome(EconomyStatus::confirmation_required);
    auto party=party_;auto resources=resources_;auto next_random=random;const auto revision=party_.revision_;
    std::string recovery_notice;
    try {
        auto& c=party.members_[member];const auto fee=inspection.fee;
        const auto purse=static_cast<std::uint16_t>(WealthRules::gold_value(c,items_));
        if(purse>=fee)WealthRules::normalize_gold(c,static_cast<std::uint16_t>(purse-fee));
        else {
            const auto pool=WealthRules::gold_value(resources.amounts,items_);
            if(pool<fee)return outcome(EconomyStatus::rejected,overlay_string(17946));
            WealthRules::normalize_gold(resources.amounts,static_cast<std::uint16_t>(pool-fee));
        }
        // 43EC:0130/0185 only normalize money. The shrine HUD redraw does not
        // rebuild equipment: retain cached load and cure-reversal byte changes.
        if(healing(action)) {
            const unsigned count=static_cast<unsigned>(action)-static_cast<unsigned>(ShrineAction::cure_light)+1;
            const auto amount=static_cast<std::uint8_t>(roll(next_random,count)+(count==2?1:count==3?3:0));
            const auto status=c.status();
            if((status==0 || status==1 || status==4 || status==5) && !MagicEffectService::has_effect(c,0x32)) {
                c.set_byte(0x11b,std::min(c.byte(0x32),static_cast<std::uint8_t>(c.byte(0x11b)+amount)));
                if(!c.active() && (status==4 || status==5)) {
                    // 175d demotes5->4, then dispatches4E. Its1869 placement
                    // helper10a7 succeeds unconditionally outside combat
                    // (execution1 here), retaining the healed HP byte. The
                    // failed-placement retry node is unreachable in shrines.
                    c.set_byte(0x10c,0);c.set_byte(0x10d,1);
                    recovery_notice=c.name()+" "+overlay_string(c.byte(0x10e)==1?182066:182086);
                }
            }
        } else if(action==ShrineAction::cure_blindness)remove(c,0x21,false);
        else if(action==ShrineAction::cure_disease)for(const auto code:disease_codes)remove(c,code,true);
        else if(action==ShrineAction::neutralize_poison) {remove(c,0x37,true);remove(c,0x16,true);remove(c,0x0f,true);}
        else if(action==ShrineAction::raise_dead && inspection.needed) {
            remove(c,0x20,true);remove(c,0x37,true);c.set_byte(0x11b,1);c.set_byte(0x10c,0);c.set_byte(0x10d,1);
            if(c.byte(0x14))c.set_byte(0x14,static_cast<std::uint8_t>(c.byte(0x14)-1));
            auto reduction=static_cast<std::uint8_t>(c.byte(0xb1)<c.byte(0x32)?c.byte(0x32)-c.byte(0xb1):0);
            const auto con=c.byte(0x14);
            if(con>13) {
                std::uint8_t levels=0;
                for(std::size_t i=0;i<8;++i)if(std::bit_cast<std::int8_t>(c.byte(0x96+i))>0) {
                    const auto level=c.byte(0x96+i);levels=static_cast<std::uint8_t>(levels+level*(i==2?con-14:con<16?1:2));
                }
                if(levels)reduction=static_cast<std::uint8_t>(reduction/levels);
                if(con<17 || std::bit_cast<std::int8_t>(c.byte(0x98))>0)c.set_byte(0x32,static_cast<std::uint8_t>(c.byte(0x32)-reduction));
            }
        } else if(action==ShrineAction::stone_to_flesh && inspection.needed) {c.set_byte(0x10c,0);c.set_byte(0x10d,1);c.set_byte(0x11b,1);}
        else if(action==ShrineAction::remove_curse) {
            MagicEffectService resolver(spells_,&items_);
            const auto result=resolver.resolve({SpellId{43},MagicOrigin::script,{MagicDomain::exploration,false},member,{MagicTargetKind::party_member,member},12},party.members_,next_random);
            if(!result.applied())return outcome(result.status==MagicStatus::rejected?EconomyStatus::rejected:EconomyStatus::unresolved,result.message);
        }
        if(party_.revision_!=revision)return outcome(EconomyStatus::rejected,"Party changed during a shrine transaction");
        party.revision_=revision+1;party_=std::move(party);resources_=std::move(resources);random=next_random;
        return outcome(EconomyStatus::applied,std::move(recovery_notice));
    } catch(const DecodeError& error) {return outcome(EconomyStatus::unresolved,error.what());}
}
}
