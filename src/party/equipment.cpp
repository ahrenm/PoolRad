#include "gbengine/party/equipment.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
int signed_byte(unsigned n) {n&=255;
return n<128?static_cast<int>(n):static_cast<int>(n)-256;}
int dex_armor(unsigned n) {
    if(n>=1 && n<=3) return -4;
    if(n>=4 && n<=6) return static_cast<int>(n)-7;
    if(n>=15 && n<=18) return static_cast<int>(n)-14;
    if(n>=19 && n<=20) return 4;
    if(n>=21 && n<=23) return 5;
    if(n>=24 && n<=25) return 6;
    return 0;
}
int dex_hit(unsigned n) {
    if(n<3) return -4;
    if(n<=5) return static_cast<int>(n)-6;
    if(n>=16 && n<=18) return static_cast<int>(n)-15;
    if(n>=19 && n<=20) return 3;
    if(n>=21 && n<=23) return 4;
    if(n>=24 && n<=25) return 5;
    return 0;
}
int strength_hit(const Character& c) {
    if(!c.byte(0xaa)) return 0;
    const int n=EquipmentRules::strength_rank(c);
    if(n>=1 && n<=3) return -3;
    if(n>=4 && n<=5) return -2;
    if(n>=6 && n<=7) return -1;
    if(n>=17 && n<=19) return 1;
    if(n>=20 && n<=22) return 2;
    if(n>=23 && n<=25) return 3;
    if(n>=26 && n<=27) return 4;
    if(n>=28 && n<=30) return n-23;
    return 0;
}
int strength_damage(const Character& c) {
    if(!c.byte(0xaa)) return 0;
    const int n=EquipmentRules::strength_rank(c);
    if(n>=1 && n<=2) return -2;
    if(n>=3 && n<=5) return -1;
    if(n==16) return 1;
    if(n>=17 && n<=19) return n-16;
    if(n>=20 && n<=29) return n-17;
    if(n==30) return 14;
    return 0;
}
}
int EquipmentRules::strength_rank(const Character& c) {
    const auto n=c.byte(0x10),exceptional=c.byte(0x16);
    if(n<18) return n;
    if(n==18) {
        if(!exceptional) return 18;
        if(exceptional<=50) return 19;
        if(exceptional<=75) return 20;
        if(exceptional<=90) return 21;
        if(exceptional<=99) return 22;
        if(exceptional==100) return 23;
    } else if(n<=25) return n+5;
    throw DecodeError("Unsupported native strength value");
}
int EquipmentRules::load_allowance(const Character& c) {
    const int n=strength_rank(c);
    if(n>=1 && n<=3) return -350;
    if(n>=4 && n<=5) return -250;
    if(n>=6 && n<=7) return -150;
    if(n>=12 && n<=13) return 100;
    if(n>=14 && n<=15) return 200;
    if(n==16) return 350;
    if(n>=17 && n<=21) return 500+250*(n-17);
    if(n>=22 && n<=26) return 2000+1000*(n-22);
    if(n==27) return 7500;
    if(n>=28 && n<=30) return 9000+3000*(n-28);
    return 0;
}
EquippedItems EquipmentRules::inspect(const Character& c,const ItemCatalog& catalog) {
    EquippedItems result;
    for(std::size_t i=0;i<c.inventory().size();++i) {
        const auto& item=c.inventory().item(i);
        const auto& def=catalog.definition(item.type());
        const auto weight=static_cast<std::uint16_t>(item.unit_weight()*static_cast<unsigned>(std::max(1,static_cast<int>(item.quantity()))));
        result.total_weight=static_cast<std::uint16_t>(result.total_weight+weight);
        if(!item.readied()) continue;
        result.readied_weight=static_cast<std::uint16_t>(result.readied_weight+weight);
        result.hands=static_cast<std::uint8_t>(result.hands+def.hands());
        if(def.category()<9) result.slots[def.category()]=i;
        else if(def.category()==9) result.slots[result.slots[9]?10:9]=i;
        if(item.type()==0x49) result.slots[11]=i;
        if(item.type()==0x1c) result.slots[12]=i;
    }
    for(std::size_t i=0;i<7;++i) result.total_weight=static_cast<std::uint16_t>(result.total_weight+c.word(0x88+2*i));
    return result;
}
void EquipmentRules::recalculate(Character& c,const ItemCatalog& catalog) {
    auto staged=c;recalculate_in_place(staged,catalog);c=std::move(staged);
}
void EquipmentRules::recalculate_in_place(Character& c,const ItemCatalog& catalog) {
    if(!c.equipment_effects_bound()) throw DecodeError("Equipped item effect bindings are not restored");
    const auto equipped=inspect(c,catalog);
    for(std::size_t slot=0;slot<13;++slot) {c.set_word(0xcc+4*slot,0);c.set_word(0xce +4*slot,0);}
    auto put=[&](std::size_t at,int n){c.set_byte(at,static_cast<std::uint8_t>(n));};
    for(std::size_t i=0;i<6;++i) c.set_byte(0x115+i,c.byte(0xa3+i));
    put(0xc7,static_cast<int>(c.inventory().size()));
    put(0x100,equipped.hands);c.set_word(0x102,equipped.total_weight);
    put(0x101,0);
    put(0x110,c.byte(0x2d));
    put(0x11c,c.byte(0x72));
    std::array<int,5> ac{dex_armor(c.byte(0x13)),0,0,0,0};
    bool suppress_ring=false,weight_reduction=false;
    if(equipped.slots[0]) {
        const auto& item=c.inventory().item(*equipped.slots[0]);
        const auto& def=catalog.definition(item.type());
        int attack=c.byte(0x2d),damage=def.damage_bonus(),bonus=item.bonus();
        if(def.attack_flags()&2) attack+=dex_hit(c.byte(0x13));
        if(def.attack_flags()&4) {attack+=strength_hit(c);damage+=strength_damage(c);}
        if((def.attack_flags()&0x80) && equipped.slots[12]) bonus+=c.inventory().item(*equipped.slots[12]).bonus();
        if((def.attack_flags()&1) && equipped.slots[11]) bonus+=c.inventory().item(*equipped.slots[11]).bonus();
        damage+=bonus;
        if(c.byte(0x2e)==2 && ((item.type()>0x28 && item.type()<0x2d) || item.type()==0x25 || item.type()==0x24)) ++bonus;
        put(0x110,attack+bonus);
        put(0x119,damage);
        put(0x115,def.damage_dice());
        put(0x117,def.damage_sides());
    } else {put(0x110,c.byte(0x110)+strength_hit(c));
    put(0x119,c.byte(0x119)+strength_damage(c));}
    for(std::size_t i=0;i<c.inventory().size();++i) {
        const auto& item=c.inventory().item(i);
        if(!item.readied()) continue;
        const auto& def=catalog.definition(item.type());
        if(item.byte(0x2f)==0xba) weight_reduction=true;
        if(def.category()==2) {
            int movement=item.unit_weight()<151?c.byte(0x72):item.unit_weight()<=399?9:6;
            if(item.bonus()!=0 && movement<10) movement+=3;
            put(0x11c,movement);
        }
        if(def.defense()&0x80) {
            const int base=def.defense()&0x7f;
            if(def.category()==1) ac[1]=signed_byte(static_cast<unsigned>(base+item.bonus()));
            else if(base==0) {
                if(def.category()==9) ac[3]=std::max(ac[3],item.bonus());else ac[2]=signed_byte(static_cast<unsigned>(ac[2]+item.bonus()));
                put(0x101,c.byte(0x101)+item.byte(0x33));
            } else if(ac[4]<base+item.bonus()) {ac[4]=signed_byte(static_cast<unsigned>(base+item.bonus()));
            if(item.bonus()>0 && def.category()==2) suppress_ring=true;}
        }
    }
    if(suppress_ring) ac[3]=0;
    if(weight_reduction) c.set_word(0x102,std::max(equipped.readied_weight,static_cast<std::uint16_t>(equipped.total_weight<5000?0:equipped.total_weight-5000)));
    // Native residual load is a signed word, with byte movement caps.
    const int residual=std::max(0,static_cast<int>(static_cast<std::int16_t>(static_cast<std::uint16_t>(c.word(0x102)-load_allowance(c)))));
    const int cap=residual<=512?c.byte(0x11c):residual<=768?9:residual<=1024?6:3;
    put(0x11c,std::min(cap,static_cast<int>(c.byte(0x11c))));
    ac[4]=std::max(ac[4],static_cast<int>(c.byte(0xa9)));
    put(0x111,ac[0]+ac[1]+ac[2]+ac[3]+ac[4]);
    put(0x112,ac[4]+ac[2]+ac[3]-2);
    put(0x6b,c.byte(0x98)>=1 && c.byte(0x2e)>=1?c.byte(0x98):1);
}
}
