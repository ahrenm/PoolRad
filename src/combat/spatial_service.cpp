#include "gbengine/combat/spatial_service.hpp"
#include "gbengine/assets/records.hpp"
#include <algorithm>
#include <cstdlib>
#include <set>

namespace GBEngine {
namespace {
int signed_byte(std::uint8_t value) {return value<128?value:static_cast<int>(value)-256;}
// Native line traversal: ties take the diagonal. Its distance accumulator is
// a BYTE, independent of terrain and the caller's maximum whole-square range.
class NativeLine {
    CombatPosition end_;
    int dx_,dy_,sx_,sy_,error_,diagonal_,straight_;
public:
    CombatPosition current;
    std::uint8_t distance=0;
    NativeLine(CombatPosition from,CombatPosition to):end_(to),dx_(std::abs(to.x-from.x)),
        dy_(std::abs(to.y-from.y)),sx_((to.x>from.x)-(to.x<from.x)),sy_((to.y>from.y)-(to.y<from.y)),
        error_(dx_>dy_?2*dy_-dx_:2*dx_-dy_),diagonal_(2*(std::min(dx_,dy_)-std::max(dx_,dy_))),
        straight_(2*std::min(dx_,dy_)),current(from) {}
    bool advance() {
        if(current==end_)return false;
        if(dx_>dy_) {
            current.x+=sx_;
            if(error_<0){error_+=straight_;distance=static_cast<std::uint8_t>(distance+2);}
            else {current.y+=sy_;error_+=diagonal_;distance=static_cast<std::uint8_t>(distance+3);}
        } else {
            current.y+=sy_;
            if(error_<0){error_+=straight_;distance=static_cast<std::uint8_t>(distance+2);}
            else {current.x+=sx_;error_+=diagonal_;distance=static_cast<std::uint8_t>(distance+3);}
        }
        return true;
    }
};
void require_position(CombatPosition value) {
    if(!Battlefield::in_bounds(value.x,value.y))throw DecodeError("Combat spatial position outside battlefield");
}
}
CombatSpatialService CombatSpatialService::load(const IFileSystem& files) {
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    return from_native_module(module);
}
CombatSpatialService CombatSpatialService::from_native_module(std::span<const std::uint8_t> module) {
    if(module.size()<0xc7c0+0x2870)throw DecodeError("Truncated combat spatial tables");
    CombatSpatialService result;
    for(unsigned shape=1;shape<=4;++shape)for(unsigned cell=0;cell<4;++cell) {
        const auto at=0xc7c0+0x2844+shape*8+cell*2;
        const CombatPosition offset{signed_byte(module[at]),signed_byte(module[at+1])};
        if(offset.x< -1 || offset.x>1 || offset.y< -1 || offset.y>1)
            throw DecodeError("Invalid native combat spatial footprint");
        result.footprints_[shape][cell]=offset;
    }
    std::copy_n(module.begin()+0xc7c0+0x286c,4,result.helpless_codes_.begin());
    result.loaded_=true;return result;
}
const DeploymentActor& CombatSpatialService::actor(const DeploymentState& state,CombatActorIdentity id) const {
    if(!loaded_)throw DecodeError("Combat spatial tables are uninitialized");
    const auto found=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& value){return value.id==id;});
    if(found==state.actors.end())throw DecodeError("Combat spatial actor identity is stale");
    return *found;
}
std::vector<CombatPosition> CombatSpatialService::footprint(const DeploymentActor& actor_value) const {
    if(!loaded_)throw DecodeError("Combat spatial tables are uninitialized");
    if(actor_value.shape>4)throw DecodeError("Unsupported combat spatial footprint");
    std::vector<CombatPosition> result;
    if(actor_value.removed || !actor_value.position || !actor_value.shape)return result;
    for(const auto offset:footprints_[actor_value.shape])if(offset.x>=0)
        result.push_back({actor_value.position->x+offset.x,actor_value.position->y+offset.y});
    return result;
}
void CombatSpatialService::validate(const DeploymentState& state) const {
    if(!loaded_)throw DecodeError("Combat spatial tables are uninitialized");
    if(state.actors.empty() || state.actors.size()>71)throw DecodeError("Invalid combat spatial participant count");
    std::array<std::uint8_t,1250> expected{};
    std::set<std::pair<CombatActorOrigin,std::uint64_t>> ids;
    std::set<std::uint8_t> slots;
    for(const auto& value:state.actors) {
        if(!ids.emplace(value.id.origin,value.id.value).second)throw DecodeError("Duplicate combat spatial actor identity");
        if(value.removed)continue;
        if(!value.native_slot || !slots.insert(value.native_slot).second)throw DecodeError("Duplicate or missing combat spatial slot");
        if(value.character.byte(0x10e)>1)throw DecodeError("Unsupported combat spatial side");
        for(const auto position:footprint(value)) {
            require_position(position);
            auto& cell=expected[static_cast<std::size_t>(position.y*50+position.x)];
            if(cell)throw DecodeError("Overlapping combat spatial footprints");
            cell=value.native_slot;
        }
    }
    if(expected!=state.occupancy)throw DecodeError("Combat spatial occupancy disagrees with owned actors");
}
CombatStepProbe CombatSpatialService::probe(const DeploymentState& state,CombatActorIdentity id,unsigned direction) const {
    if(direction>8)throw DecodeError("Invalid native combat movement direction");
    validate(state);const auto& selected=actor(state,id);
    if(selected.removed || !selected.position || !selected.shape)throw DecodeError("Combat actor has no movement footprint");
    const auto dx=state.battlefield.tables.delta_x(direction),dy=state.battlefield.tables.delta_y(direction);
    CombatStepProbe result;result.destination={selected.position->x+dx,selected.position->y+dy};
    result.step_cost=static_cast<std::uint8_t>((direction&1)?3:2);
    std::uint8_t largest=1;
    for(auto position:footprint(selected)) {
        position.x+=dx;position.y+=dy;
        std::uint8_t terrain=0;
        if(Battlefield::in_bounds(position.x,position.y)) {
            terrain=state.battlefield.terrain_at(position.x,position.y);
            const auto slot=state.occupancy[static_cast<std::size_t>(position.y*50+position.x)];
            if(slot && slot!=selected.native_slot)result.occupied_slot=slot;
        }
        if(!terrain || !result.terrain)result.terrain=0;
        else if(terrain==0x1e || result.terrain==0x1e)result.terrain=0x1e;
        else {
            const auto cost=state.battlefield.tables.properties(terrain)[0];
            if(cost>=largest){largest=cost;result.terrain=terrain;}
        }
    }
    result.terrain_cost=state.battlefield.tables.properties(result.terrain)[0];
    return result;
}
bool CombatSpatialService::helpless(const Character& character) const {
    if(!character.effects_restored())throw DecodeError("Combat movement requires bound effect records");
    return std::any_of(character.effects().begin(),character.effects().end(),[&](const auto& effect){
        return std::find(helpless_codes_.begin(),helpless_codes_.end(),effect.code())!=helpless_codes_.end();
    });
}
std::uint8_t CombatSpatialService::range(CombatPosition from,CombatPosition to) {
    require_position(from);require_position(to);
    NativeLine line(from,to);while(line.advance()){}
    return line.distance;
}
std::vector<CombatPosition> CombatSpatialService::trace_line(CombatPosition from,CombatPosition to) {
    NativeLine line(from,to);std::vector<CombatPosition> result{from};
    while(line.advance()) {if(result.size()>=4096)throw DecodeError("Combat ray exceeded native bounded traversal");result.push_back(line.current);}
    return result;
}
CombatSightResult CombatSpatialService::sight(const Battlefield& field,CombatPosition from,CombatPosition to,
                                             std::uint16_t maximum_range,bool ignore_terrain) {
    require_position(from);require_position(to);
    // Native arithmetic multiplies a WORD range by2 then adds1.
    const auto limit=static_cast<std::uint16_t>(maximum_range*2u+1u);
    NativeLine line(from,to);
    NativeLine elevation({0,field.properties_at(from.x,from.y)[1]},
                         {std::max(std::abs(to.x-from.x),std::abs(to.y-from.y)),field.properties_at(to.x,to.y)[1]});
    for(unsigned work=0;work<256;++work) {
        if((!ignore_terrain && field.properties_at(line.current.x,line.current.y)[2]>elevation.current.y) ||
           line.distance>limit)return {false,line.distance,line.current};
        (void)elevation.advance();
        if(!line.advance())return {true,line.distance,line.current};
    }
    throw DecodeError("Combat sight traversal exceeded native field bounds");
}
bool CombatSpatialService::adjacent(const DeploymentState& state,const DeploymentActor& left,const DeploymentActor& right) const {
    for(const auto from:footprint(left))for(const auto to:footprint(right))
        if(sight(state.battlefield,from,to,1).clear)return true;
    return false;
}
CombatStepResult CombatSpatialService::stage(const DeploymentState& state,CombatActorIdentity id,unsigned direction) const {
    return stage_step(state,id,direction,true);
}
CombatStepResult CombatSpatialService::stage_after_departure(const DeploymentState& state,CombatActorIdentity id,unsigned direction,bool resolve_hazard) const {
    return stage_step(state,id,direction,false,resolve_hazard);
}
CombatStepResult CombatSpatialService::stage_step(const DeploymentState& state,CombatActorIdentity id,unsigned direction,bool hold_reactions,bool resolve_hazard) const {
    if(direction>7)throw DecodeError("A combat step requires one of eight native directions");
    const auto destination=probe(state,id,direction);const auto& selected=actor(state,id);
    const auto unresolved=[](std::string message){CombatStepResult value;value.message=std::move(message);return value;};
    if(!selected.character.active() || helpless(selected.character))return unresolved("Inactive or helpless actor cannot move");
    if(selected.extension[6]<2)return unresolved("Actor has no remaining movement budget");
    auto candidate=state;
    auto& next=*std::find_if(candidate.actors.begin(),candidate.actors.end(),[&](const auto& value){return value.id==id;});
    next.extension[9]=static_cast<std::uint8_t>(direction);
    const auto blocked=[&](std::string message) {
        CombatStepResult value;value.status=CombatStepStatus::blocked;value.state=std::move(candidate);value.message=std::move(message);return value;
    };
    if(destination.occupied_slot) {
        const auto target=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& value){return !value.removed && value.native_slot==destination.occupied_slot;});
        if(target==state.actors.end())throw DecodeError("Combat occupied slot has no owned actor");
        if(target->character.byte(0x10e)!=selected.character.byte(0x10e))return unresolved("Attacking an occupied hostile square requires combat attack resolution");
        return unresolved("An occupied allied square requires friendly-fire confirmation and attack resolution");
    }
    if(!destination.terrain)return unresolved("Leaving the battlefield requires combat flight resolution");
    if(selected.extension[6]<destination.terrain_cost)return blocked("Insufficient movement budget for this terrain");
    // 08B2 finds opponents adjacent before departure but absent after the
    // tentative move. It may execute attacks before0712 spends movement.
    next.position=destination.destination;
    if(hold_reactions)for(const auto& other:state.actors) {
        if(other.removed || !other.position || !other.shape || !other.character.active() ||
           other.character.byte(0x10e)==selected.character.byte(0x10e))continue;
        const bool before=adjacent(state,selected,other),after=adjacent(state,next,other);
        if(before && !after && !helpless(other.character))return unresolved("Leaving hostile reach requires reaction attack resolution");
        if(selected.extension[6]>=destination.step_cost && after && other.extension[7] && !helpless(other.character))
            return unresolved("Entering guarded hostile reach requires guard attack resolution");
    }
    CombatStepResult result;
    if(next.extension[6]<destination.step_cost) {
        next.position=selected.position;next.extension[6]=0;
        result.status=CombatStepStatus::budget_exhausted;result.movement_exhausted=true;
        result.state=std::move(candidate);result.message="Movement budget exhausted before the diagonal step";return result;
    }
    if(destination.terrain==0x1e) {
        const bool protected_from_hazard=std::any_of(selected.character.effects().begin(),selected.character.effects().end(),[](const auto& effect){
            const auto code=effect.code();return code==0x1e || code==0x1f || code==0x20 || code==0x6f || code==0x7d;
        });
        if(!protected_from_hazard && !resolve_hazard)return unresolved("Entering hazardous terrain requires saving throw and effect resolution");
    }
    next.extension[6]=static_cast<std::uint8_t>(next.extension[6]-destination.step_cost);
    if(next.extension[6]<2)next.extension[6]=0;
    next.extension[0xf]=0;next.extension[0x12]=0;
    candidate.occupancy.fill(0);
    for(const auto& value:candidate.actors)for(const auto position:footprint(value)) {
        require_position(position);
        auto& cell=candidate.occupancy[static_cast<std::size_t>(position.y*50+position.x)];
        if(cell)throw DecodeError("Combat movement produced overlapping actor footprints");
        cell=value.native_slot;
    }
    validate(candidate);result.status=CombatStepStatus::moved;result.moved=true;result.movement_exhausted=next.extension[6]==0;
    result.state=std::move(candidate);return result;
}
}
