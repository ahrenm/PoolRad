#include "gbengine/combat/noninterior_battlefield.hpp"
#include <algorithm>
#include <initializer_list>
#include <optional>
namespace GBEngine {
namespace {
bool range(unsigned v,unsigned low,unsigned high) {return low<=v && v<=high;}
bool one_of(unsigned v,std::initializer_list<unsigned> values) {
    return std::find(values.begin(),values.end(),v)!=values.end();
}
std::uint8_t dice(std::mt19937& random,unsigned count,unsigned sides) {
    std::uint8_t result=0;
    const auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    for(unsigned i=0;i<count;++i) {
        std::uint32_t value;
        do {value=static_cast<std::uint32_t>(random());}while(value<threshold);
        result=static_cast<std::uint8_t>(result+value%sides+1);
    }
    return result;
}
void set(Battlefield& field,int x,int y,unsigned id) {
    // Source river pairs at x49 write their second byte into the next row.
    // Preserve linear source indexing while rejecting writes past the allocation.
    const int offset=y*Battlefield::width+x;
    if(offset<0 || offset>=Battlefield::width*Battlefield::height)
        throw DecodeError("Noninterior battlefield source write outside terrain");
    field.terrain.at(static_cast<std::size_t>(offset))=static_cast<std::uint8_t>(id);
}
bool floor(const Battlefield& field,int x,int y) {return field.properties_at(x,y)[3]==22;}
void river(Battlefield& field,std::uint8_t flags,std::mt19937& random) {
    unsigned chance=0;
    if(flags&0x20) chance=35;
    if(flags&0x10) chance=75;
    if(dice(random,1,100)>chance) return;
    int x=34-dice(random,5,4);
    while((x+2)%7>0) --x;
    const int start=x;
    unsigned crossings=0;
    const auto cross=[&](int cx,int cy) {set(field,cx,cy,52);set(field,cx+1,cy,53);++crossings;};
    for(int y=0;y<Battlefield::height;++y) {
        if(x<Battlefield::width) {
            set(field,x,y,50);set(field,x+1,y,51);
            const auto roll=dice(random,1,20);
            if(roll==1 || (crossings&1)) cross(x,y);
            ++x;
        }
    }
    if(crossings==0) {
        const int y=16-dice(random,1,9);
        x=start+y;
        cross(x,y);cross(x+1,y+1);
    }
}
void trees(Battlefield& field,std::uint8_t flags,std::mt19937& random) {
    unsigned chance=1;
    if(flags&4) chance=3;
    if(flags&0x40) chance=4;
    if(flags&8) chance=7;
    if(flags&0x80) chance=0;
    for(int x=0;x<Battlefield::width;++x) for(int y=1;y<Battlefield::height;++y) {
        if(!floor(field,x,y) || !floor(field,x,y-1) || dice(random,1,100)>chance) continue;
        const auto roll=dice(random,1,10);
        unsigned kind=roll<9?(roll+1)/2:dice(random,1,2)+4;
        if(flags&0x40) kind=dice(random,1,3)+3;
        set(field,x,y,kind+35);
        if(kind<5) set(field,x,y-1,kind+31);
    }
}
void scatter(Battlefield& field,std::uint8_t flags,std::mt19937& random) {
    unsigned first=0,second=15,last=40,third=0;
    if(flags&2) {second=45;last=10;}
    if(flags&4) second=25;
    if(flags&0x40) {last=5;third=30;}
    if(flags&0x80) {first=15;last=25;second=10;}
    for(int x=0;x<Battlefield::width;++x) for(int y=0;y<Battlefield::height;++y) {
        if(!floor(field,x,y)) continue;
        const auto roll=dice(random,1,255);
        if(roll<=first) {
            const auto kind=dice(random,1,4);
            if(kind==4 && y>0 && floor(field,x,y-1)) {set(field,x,y-1,65);set(field,x,y,64);}
            if(kind<4) set(field,x,y,kind+60);
        } else if(roll<=first+6) set(field,x,y,dice(random,1,3)+54);
        else if(roll<=second+first+6) set(field,x,y,dice(random,1,4)+41);
        else if(roll<=third+second+first+6) set(field,x,y,(dice(random,1,10)-1)/3+57);
        else if(roll<=last+third+second+first+6) {
            unsigned kind=dice(random,1,4);
            if(kind==4 && !(flags&0x40)) kind=dice(random,1,3);
            set(field,x,y,kind+45);
        }
    }
}
}
std::uint8_t noninterior_terrain_flags(std::uint8_t v,std::uint16_t region_flag) {
    std::optional<unsigned> result;
    if(range(v,1,2) || range(v,40,41) || range(v,79,86) || range(v,91,92) || one_of(v,{43,241})) result=1;
    else if(range(v,168,177)) result=0x21;
    else if(v==185 || range(v,188,192)) result=0x11;
    else if(one_of(v,{3,4}) || range(v,6,9) || range(v,19,21) || range(v,23,25)) result=3;
    else if(one_of(v,{3,4,7,22}) || range(v,10,18) || range(v,26,36) || range(v,201,208)) result=2;
    else if(range(v,209,213)) result=0x12;
    else if(one_of(v,{37,57,64,164,165,180,239,240,243,250,252}) || range(v,154,158)) result=4;
    else if(range(v,39,44) || one_of(v,{153,159})) result=5;
    else if(range(v,45,46) || range(v,51,56) || range(v,67,77) || range(v,93,97)) result=0x44;
    else if(range(v,181,184) || v==186 || range(v,193,200)) result=0x14;
    else if(range(v,47,50) || range(v,98,101) || v==242) result=0x40;
    else if(v==38) result=0x41;
    else if(range(v,178,179)) result=0x60;
    else if(range(v,58,60) || range(v,65,66)) result=0x48;
    else if(range(v,61,63) || range(v,121,124) || range(v,135,137) || one_of(v,{139,141,149,151,152,238}) || range(v,144,147) || range(v,160,162) || range(v,166,167) || range(v,214,216)) result=8;
    else if(range(v,87,90) || one_of(v,{163,224})) result=0x88;
    else if(one_of(v,{124,125,138,140,142,143,148,150})) result=9;
    else if(range(v,126,133) || range(v,229,231)) result=0x28;
    else if(range(v,217,223)) result=0x18;
    else if(one_of(v,{78,245,232,233})) result=0x20;
    else if(v==109 || range(v,244,246)) result=0x80;
    else if(range(v,110,113) || range(v,115,120)) result=0x81;
    else if(range(v,102,108) || v==114) result=0x90;
    else if(range(v,225,228) || range(v,234,237)) result=0xa0;
    if(!result) throw DecodeError("Noninterior battlefield selector has undefined native flags");
    if((*result&0x80) && region_flag==0xff) *result=(*result+0x80)|1;
    return static_cast<std::uint8_t>(*result);
}
Battlefield build_noninterior_battlefield(const BattlefieldTables& tables,
    std::span<const std::uint8_t> city_cells,std::uint8_t mode,std::uint8_t x,
    std::uint8_t y,std::uint16_t region_flag,std::uint8_t ecl_record,std::mt19937& random) {
    if(mode<2 || mode>4 || city_cells.size()!=44*36 || x>15 || y>35)
        throw DecodeError("Invalid noninterior battlefield source coordinates");
    const auto selector=city_cells[static_cast<std::size_t>(y*44+x+(mode-2)*13)];
    const auto flags=noninterior_terrain_flags(selector,region_flag);
    Battlefield field;field.tables=tables;field.presentation_mode=mode;field.environment_selector=selector;
    field.terrain.fill(23);
    auto staged_random=random;
    river(field,flags,staged_random);trees(field,flags,staged_random);scatter(field,flags,staged_random);
    BattlefieldService::vary_floor(field,ecl_record,staged_random);
    random=staged_random;
    return field;
}
}
