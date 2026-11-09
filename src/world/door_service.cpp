#include "gbengine/world/door_service.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
unsigned roll(std::mt19937& random,unsigned sides) {
    const auto threshold=(std::uint32_t{0}-sides)%sides;
    std::uint32_t n;do{n=static_cast<std::uint32_t>(random());}while(n<threshold);
    return n%sides+1;
}
void unlock(GeoMap& map,int x,int y,unsigned d) {
    if(!GeoMap::contains(x,y))return;
    auto& b=map.planes[768+static_cast<std::size_t>(y*16+x)];
    b=static_cast<std::uint8_t>((b&~(3u<<(d*2)))|(1u<<(d*2)));
}
}
std::vector<char> DoorService::choices(const WorldState& world) {
    std::vector<char> out;
    if(world.door_bash)out.push_back('B');
    bool thief=false,knock=false,mage=false;
    for(std::size_t i=0;i<world.party.size();++i) {
        const auto& c=world.party.member(i);
        thief|=static_cast<std::int8_t>(c.byte(0x9c))>0;
        mage|=static_cast<std::int8_t>(c.byte(0x9b))>0;
        for(unsigned s=0;s<21;++s)knock|=c.byte(0x17+s)==31;
    }
    if(world.door_pick && thief)out.push_back('P');
    const bool strong=world.map && world.map->door(world.x,world.y,world.facing)==3;
    if(world.door_knock && (strong?mage:knock))out.push_back('K');
    out.push_back('E');return out;
}
bool DoorService::apply(WorldState& world,std::mt19937& random,char action) {
    const auto allowed=choices(world);
    if(std::find(allowed.begin(),allowed.end(),action)==allowed.end())throw DecodeError("Unavailable door action");
    if(!world.map || !GeoMap::contains(world.x,world.y))throw DecodeError("Door action requires interior geometry");
    const auto type=world.map->door(world.x,world.y,world.facing);
    if(type<2)throw DecodeError("Door is no longer locked");
    bool success=false;
    if(action=='K') {
        // Native0648 consumes first exact full-byte Knock, then steps without
        // unlocking either edge. No ordinary spell-service targeting/costs.
        for(auto& c:world.party.members_) {
            for(unsigned s=0;s<21;++s)if(c.byte(0x17+s)==31) {
                c.set_byte(0x17+s,0);++world.party.revision_;return true;
            }
        }
        return false;
    }
    if(action=='P') {
        if(type==2)for(std::size_t i=0;i<world.party.size() && !success;++i) {
            const auto& c=world.party.member(i);
            const auto die=roll(random,100); // Draw precedes status test, even non-thieves.
            success=die<=c.byte(0x78) && c.status()==0;
        }
        world.door_pick=false;
    }
    if(action=='B')for(std::size_t i=0;i<world.party.size() && !success;++i) {
        const auto& c=world.party.member(i);const auto str=c.byte(0x10),ex=c.byte(0x16);
        unsigned sides=6,limit=0;
        if(type==3) {
            if(str==18) {if(ex>=91 && ex<=99)limit=1;else if(ex==100)limit=2;else world.door_bash=false;}
            else if(str==19 || str==20)limit=3;
            else if(str==21 || str==22)limit=4;
            else if(str==23)limit=5;
            else if(str==24){sides=8;limit=7;}
            else if(str==25){success=true;}
            else world.door_bash=false;
        }else {
            if(str>=3 && str<=7)limit=1;
            else if(str>=8 && str<=15)limit=2;
            else if(str==16 || str==17)limit=3;
            else if(str==18){if(ex<=50)limit=3;else if(ex<=99)limit=4;else if(ex==100)limit=5;}
            // Pool0294 has no ordinary-door branches for STR19..25.
        }
        if(limit)success=roll(random,sides)<=limit;
    }
    if(success) {
        const auto d=static_cast<unsigned>(world.facing);
        constexpr int dx[]{0,1,0,-1},dy[]{-1,0,1,0};
        unlock(*world.map,world.x,world.y,d);
        unlock(*world.map,world.x+dx[d],world.y+dy[d],(d+2)%4);
    }
    return success;
}
}
