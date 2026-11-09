#include "gbengine/magic/saving_throw_service.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
std::uint8_t d20(std::mt19937& random) {
    constexpr std::uint32_t sides=20,threshold=(std::uint32_t{0}-sides)%sides;
    std::uint32_t n;do {n=static_cast<std::uint32_t>(random());} while(n<threshold);
    return static_cast<std::uint8_t>(n%sides+1);
}
const CharacterEffect* query(std::span<Character> roster,std::size_t target,std::uint8_t code,const SavingThrowAuraQuery& aura) {
    const auto find=[&](const Character& c)->const CharacterEffect* {
        if(!c.effects_restored()) throw DecodeError("Saving throws require restored effect lists");
        const auto at=std::find_if(c.effects().begin(),c.effects().end(),[&](const auto& e){return e.code()==code;});
        return at==c.effects().end()?nullptr:&*at;
    };
    if(const auto* own=find(roster[target]))return own;
    // Family C's 2D/2E/31 are in the native party-aura fallback set.
    if(code==0x2d || code==0x2e || code==0x31) {
        if(aura)return aura(code);
        for(std::size_t i=0;i<roster.size();++i) if(i!=target)
            if(const auto* other=find(roster[i]))return other;
    }
    return nullptr;
}
int signed_byte(std::uint8_t n) {return n<128?n:static_cast<int>(n)-256;}
}
SavingThrowResult SavingThrowService::resolve(std::span<Character> roster,std::size_t target,
        const Character& source,std::uint8_t category,std::int8_t modifier,std::mt19937& random) {
    return resolve(roster,target,source,category,modifier,random,0);
}
bool SavingThrowService::resolve_invariant(std::span<Character> roster,std::size_t target,
        const Character& source,std::uint8_t category,std::int8_t modifier,std::mt19937& random,
        const SavingThrowAuraQuery& aura) {
    // All save callbacks read only bits0/1 of6819. Other resident scratch is
    // neither recovered nor published here. The source remains the genuine
    // selected actor even when it aliases the destination roster.
    const auto owner=source;
    auto agreed=std::vector<Character>(roster.begin(),roster.end());auto agreed_rng=random;
    const bool saved=resolve(agreed,target,owner,category,modifier,agreed_rng,0,aura).saved;
    for(std::uint8_t flags=1;flags<4;++flags) {
        auto trial=std::vector<Character>(roster.begin(),roster.end());auto trial_rng=random;
        const bool result=resolve(trial,target,owner,category,modifier,trial_rng,flags,aura).saved;
        bool same=result==saved && trial_rng==agreed_rng;
        for(std::size_t i=0;i<trial.size();++i)
            same=same && trial[i].bytes_==agreed[i].bytes_ && trial[i].effects_==agreed[i].effects_ &&
                trial[i].effects_restored_==agreed[i].effects_restored_ &&
                trial[i].bound_equipment_effects_==agreed[i].bound_equipment_effects_;
        if(!same)throw DecodeError("Saving throw requires inherited elemental damage flags");
    }
    // Keep caller-owned vector elements/references intact; only their values
    // change. The resolver never mutates inventory or identity ownership.
    std::move(agreed.begin(),agreed.end(),roster.begin());random=std::move(agreed_rng);
    return saved;
}
SavingThrowResult SavingThrowService::resolve(std::span<Character> roster,std::size_t target,
        const Character& source,std::uint8_t category,std::int8_t modifier,std::mt19937& random,std::uint8_t damage_flags) {
    return resolve(roster,target,source,category,modifier,random,damage_flags,{});
}
SavingThrowResult SavingThrowService::resolve(std::span<Character> roster,std::size_t target,
        const Character& source,std::uint8_t category,std::int8_t modifier,std::mt19937& random,std::uint8_t damage_flags,
        const SavingThrowAuraQuery& aura) {
    if(target>=roster.size() || category>4)throw DecodeError("Saving throw needs an actual member and native category");
    if(!roster[target].effects_restored())throw DecodeError("Saving throw target effect graph is not restored");
    const auto die=d20(random);
    // Raw 1 and 20 bypass bonuses, thresholds and the complete callback family.
    if(die==1 || die==20)return {die==20,die,die};
    auto adjusted=static_cast<std::uint8_t>(die+signed_byte(roster[target].byte(0x101))+modifier);
    const auto add=[&](int n){adjusted=static_cast<std::uint8_t>(adjusted+n);};
    constexpr std::array<std::uint8_t,15> order{8,9,10,0x11,0x14,0x21,0x24,0x2d,0x2e,0x31,0x3d,0x6f,0x7d,0x5a,0x61};
    for(const auto code:order) if(const auto* effect=query(roster,target,code,aura)) {
        const auto alignment=source.byte(0xa0);
        if(code==8 || code==0x2d) {if(alignment==2 || alignment==5 || alignment==8)add(2);}
        else if(code==9 || code==0x2e) {if(alignment==0 || alignment==3 || alignment==6)add(2);}
        else if(code==0x0a) {if(damage_flags&2)add(3);}
        else if(code==0x14) {if(damage_flags&1)add(3);}
        else if(code==0x3d) {if(damage_flags&1)add(4);}
        else if(code==0x11) {
            if(roster[target].byte(0x111)<0x39)roster[target].set_byte(0x111,0x39);
            add(1);
        } else if(code==0x21) {
            roster[target].set_byte(0x111,static_cast<std::uint8_t>(roster[target].byte(0x111)-4));
            roster[target].set_byte(0x112,static_cast<std::uint8_t>(roster[target].byte(0x112)-4));add(-4);
        } else if(code==0x24)add(-4);
        else if(code==0x31)add(roster[target].byte(0x10e)==((effect->payload[3]&0x10)>>4)?1:-1);
        else if((code==0x6f || code==0x7d) && category==0)adjusted=100;
        else if((code==0x5a && category==0) || (code==0x61 && (category==4 || category==2))) {
            const auto constitution=roster[target].byte(0x14);
            // Below 4 or above 20 leaves a native stack byte uninitialized.
            if(constitution<4 || constitution>20)throw DecodeError("Native saving throw ability bonus is uninitialized for this value");
            add(constitution<=6?1:constitution<=10?2:constitution<=13?3:constitution<=17?4:5);
        }
    }
    return {adjusted>=roster[target].byte(0x6d+category),die,adjusted};
}
}
