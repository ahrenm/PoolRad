#include "gbengine/combat/targeting_service.hpp"
#include "gbengine/party/equipment.hpp"
#include <algorithm>
#include <set>

namespace GBEngine {
namespace {
unsigned cone(CombatPosition from,CombatPosition to,const BattlefieldTables& tables) {
    if(from==to)return 0;
    for(unsigned d=0;d<8;++d) {
        const int x=from.x+tables.delta_x(d),y=from.y+tables.delta_y(d);
        if(to==CombatPosition{x,y})return d;
        bool match=false;
        switch(d) {
        case 0:match=!((to.x<x || x-to.x+y<to.y) && (x<to.x || to.x-x+y<to.y));break;
        case 1:match=!((to.x<x || x-to.x+y<to.y) && (to.x<x+y-to.y || y<to.y));break;
        case 2:match=!((to.x<x+y-to.y || y<to.y) && (to.x<x+to.y-y || to.y<y));break;
        case 3:match=!((to.x<x+to.y-y || to.y<y) && (to.x<x || to.y<to.x-x+y));break;
        case 4:match=!((to.x<x || to.y<to.x-x+y) && (x<to.x || to.y<x-to.x+y));break;
        case 5:match=!((x<to.x || to.y<x-to.x+y) && (x+y-to.y<to.x || to.y<y));break;
        case 6:match=!((x+y-to.y<to.x || to.y<y) && (x+to.y-y<to.x || y<to.y));break;
        case 7:match=!((x+to.y-y<to.x || y<to.y) && (x<to.x || to.x-x+y<to.y));break;
        }
        if(match)return d;
    }
    throw DecodeError("Combat target has no native direction cone");
}
struct WeaponRange {unsigned maximum=1;bool ranged=false,dual=false,ammunition=true;};
WeaponRange weapon_range(const Character& c,const ItemCatalog& catalog) {
    WeaponRange result;const auto equipment=EquipmentRules::inspect(c,catalog);
    if(!equipment.slots[0])return result;
    const auto& item=c.inventory().item(*equipment.slots[0]);const auto& def=catalog.definition(item.type());
    const auto n=static_cast<std::uint8_t>(def.byte(12)-1);
    result.maximum=n==0 || n==255?1:n;result.ranged=def.byte(12)>=2;
    const auto flags=def.attack_flags();result.dual=result.ranged && (flags&0x14)==0x14;
    if(result.ranged && !(flags&0x10)) {
        result.ammunition=false;
        if((flags&9)==9 && equipment.slots[11])result.ammunition=true;
        if((flags&0x88)==0x88 && equipment.slots[12])result.ammunition=true;
        if(flags==10)result.ammunition=true;
    }
    return result;
}
}
CombatTargetingService CombatTargetingService::load(const IFileSystem& files) {
    CombatTargetingService result;result.spatial_=CombatSpatialService::load(files);
    result.attacks_=CombatAttackService::load(files);result.loaded_=true;return result;
}
const DeploymentActor& CombatTargetingService::actor(const DeploymentState& state,CombatActorIdentity id) const {
    const auto it=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return a.id==id;});
    if(it==state.actors.end())throw DecodeError("Combat targeting identity is stale");
    return *it;
}
void CombatTargetingService::validate(const DeploymentState& state) const {
    if(!loaded_)throw DecodeError("Combat targeting source tables are uninitialized");
    if(state.actors.empty() || state.actors.size()>71)throw DecodeError("Invalid combat targeting roster");
    std::set<std::pair<CombatActorOrigin,std::uint64_t>> ids;std::set<std::uint8_t> slots;
    std::array<std::uint8_t,1250> occupancy{};
    for(const auto& a:state.actors) {
        if(!ids.emplace(a.id.origin,a.id.value).second)throw DecodeError("Duplicate combat targeting identity");
        if(a.removed)continue;
        if(!a.native_slot || !slots.insert(a.native_slot).second || a.character.byte(0x10e)>1)
            throw DecodeError("Invalid combat targeting slot or allegiance");
        for(const auto p:spatial_.footprint(a)) {
            if(!Battlefield::in_bounds(p.x,p.y))throw DecodeError("Combat target footprint outside battlefield");
            auto& at=occupancy[static_cast<std::size_t>(p.y*50+p.x)];
            if(at)throw DecodeError("Overlapping combat target footprints");
            at=a.native_slot;
        }
    }
    if(occupancy!=state.occupancy)throw DecodeError("Combat targeting occupancy disagrees with actors");
}
CombatTarget CombatTargetingService::describe(const DeploymentState& state,const DeploymentActor& a,
        const DeploymentActor& t,const ItemCatalog& catalog,std::uint8_t distance,unsigned direction) const {
    CombatTarget result;result.actor=t.id;result.position=*t.position;result.distance=distance;
    result.direction=static_cast<std::uint8_t>(direction);result.visible=true;
    result.available=attacks_.target_available(state,a.id,t.id);
    const auto w=weapon_range(a.character,catalog);
    result.weapon_eligible=a.id!=t.id && result.available && (static_cast<unsigned>(distance)>>1)<=w.maximum &&
        a.character.active() && t.character.active();
    if(w.ranged && !w.ammunition)result.weapon_eligible=false;
    else if(w.ranged && !w.dual && std::any_of(state.actors.begin(),state.actors.end(),[&](const auto& other){
        if(other.removed || !other.position || !other.shape || other.character.byte(0x10e)==a.character.byte(0x10e))return false;
        for(const auto from:spatial_.footprint(a))for(const auto to:spatial_.footprint(other))
            if(CombatSpatialService::sight(state.battlefield,from,to,1).clear)return true;
        return false;
    })) {
        result.adjacent_enemy_blocks_ranged=result.weapon_eligible;
        result.weapon_eligible=false;
    }
    //326D2998's predicate is target-side==!attacker-side OR attacker control.
    result.confirmation_required=result.weapon_eligible &&
        t.character.byte(0x10e)==a.character.byte(0x10e) && !a.character.byte(0x10f);
    return result;
}
std::vector<CombatTarget> CombatTargetingService::visible_targets(const DeploymentState& state,CombatActorIdentity id,std::uint16_t maximum) const {
    validate(state);const auto& a=actor(state,id);
    if(a.removed || !a.position || !a.shape)throw DecodeError("Combat aiming actor is not placed");
    std::vector<const DeploymentActor*> slots;
    for(const auto& t:state.actors)if(!t.removed && t.position && t.shape)slots.push_back(&t);
    std::sort(slots.begin(),slots.end(),[](const auto* x,const auto* y){return x->native_slot<y->native_slot;});
    std::vector<CombatTarget> result;
    for(const auto* t:slots) {
        unsigned closest=256,dir=0;
        for(const auto to:spatial_.footprint(*t))for(const auto from:spatial_.footprint(a)) {
            const auto sight=CombatSpatialService::sight(state.battlefield,from,to,maximum);
            if(sight.clear && sight.distance<closest){closest=sight.distance;dir=cone(from,to,state.battlefield.tables);}
        }
        if(closest<256) {
            CombatTarget target;target.actor=t->id;target.position=*t->position;target.visible=true;
            target.distance=static_cast<std::uint8_t>(closest);target.direction=static_cast<std::uint8_t>(dir);
            result.push_back(target);
        }
    }
    // Preserve the native pairwise exchange rather than imposing a different
    // strict ordering when direction parity prevents a tie swap.
    for(std::size_t i=0;i<result.size();++i)for(std::size_t j=i+1;j<result.size();++j)
        if(result[j].distance<result[i].distance || (result[j].distance==result[i].distance &&
            result[j].direction<result[i].direction && result[j].direction%2<=result[i].direction%2))
            std::swap(result[i],result[j]);
    return result;
}
std::vector<CombatTarget> CombatTargetingService::list(const DeploymentState& state,CombatActorIdentity id,const ItemCatalog& catalog) const {
    const auto visible=visible_targets(state,id);const auto& a=actor(state,id);std::vector<CombatTarget> result;
    for(const auto& entry:visible) {
        auto target=describe(state,a,actor(state,*entry.actor),catalog,entry.distance,entry.direction);
        if(target.available)result.push_back(target);
    }
    return result;
}
CombatTarget CombatTargetingService::probe(const DeploymentState& state,CombatActorIdentity id,CombatPosition position,const ItemCatalog& catalog) const {
    validate(state);const auto& a=actor(state,id);
    if(!Battlefield::in_bounds(position.x,position.y))throw DecodeError("Combat manual cursor outside battlefield");
    if(a.removed || !a.position || !a.shape)throw DecodeError("Combat aiming actor is not placed");
    const auto w=weapon_range(a.character,catalog);
    const auto sight=CombatSpatialService::sight(state.battlefield,*a.position,position,255);
    CombatTarget result;result.position=position;result.visible=sight.clear;result.distance=sight.distance;
    result.direction=static_cast<std::uint8_t>(cone(*a.position,position,state.battlefield.tables));
    const auto terrain=state.battlefield.terrain_at(position.x,position.y);
    if(!sight.clear)return result;
    const auto slot=state.occupancy[static_cast<std::size_t>(position.y*50+position.x)];
    if(slot) {
        const auto found=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& t){return t.native_slot==slot && !t.removed;});
        if(found==state.actors.end())throw DecodeError("Combat manual target slot has no owner");
        result=describe(state,a,*found,catalog,sight.distance,result.direction);result.position=position;
        if((static_cast<unsigned>(sight.distance)>>1)>w.maximum || state.battlefield.tables.properties(terrain)[0]==255) {
            result.weapon_eligible=false;
            result.adjacent_enemy_blocks_ranged=false;
        }
    } else if(terrain==0x1f) {
        for(const auto& body:state.bodies)if(body.position==position){result.actor=body.actor;result.body=true;}
        // Source weapon mode forbids bodies. Spell targeting is a separate policy.
    }
    return result;
}
CombatPosition CombatTargetingService::move_cursor(CombatPosition p,const BattlefieldTables& tables,unsigned direction) {
    if(direction>8)throw DecodeError("Invalid combat aiming cursor direction");
    return {std::clamp(p.x+tables.delta_x(direction),0,49),std::clamp(p.y+tables.delta_y(direction),0,24)};
}
}
