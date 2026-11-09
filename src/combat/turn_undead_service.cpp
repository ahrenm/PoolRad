#include "gbengine/combat/turn_undead_service.hpp"
#include "gbengine/assets/records.hpp"
#include <algorithm>
#include <set>

namespace GBEngine {
namespace {
int signed_byte(std::uint8_t n){return n<128?n:static_cast<int>(n)-256;}
std::uint8_t roll(std::mt19937& random,unsigned sides) {
    const auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    std::uint32_t value;do{value=static_cast<std::uint32_t>(random());}while(value<threshold);
    return static_cast<std::uint8_t>(value%sides+1);
}
void stop(DeploymentActor& a){for(const auto at:{3u,0u,7u,6u})a.extension[at]=0;}
}
TurnUndeadService TurnUndeadService::load(const IFileSystem& files) {
    TurnUndeadService result;result.spatial_=CombatSpatialService::load(files);result.targeting_=CombatTargetingService::load(files);
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    constexpr std::size_t base=0xc7c0+0x447;
    if(module.size()<base+131)throw DecodeError("Truncated source turning table");
    for(unsigned type=1;type<=12;++type)for(unsigned level=1;level<=10;++level)
        result.thresholds_[type-1][level-1]=static_cast<std::int8_t>(signed_byte(module[base+type*10+level]));
    result.loaded_=true;return result;
}
bool TurnUndeadService::available(const DeploymentActor& a) {
    return !a.removed && a.position && a.shape && a.character.active() &&
        signed_byte(a.character.byte(0x96))>0 && !a.extension[0x11];
}
TurnUndeadResult TurnUndeadService::stage(const DeploymentState& state,CombatActorIdentity id,std::mt19937& random) const {
    if(!loaded_)throw DecodeError("Turn Undead source tables are uninitialized");
    const auto it=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return a.id==id;});
    if(it==state.actors.end())throw DecodeError("Turn Undead actor identity is stale");
    if(!available(*it))throw DecodeError("Turn Undead is unavailable for this actor");
    if(state.actors.empty() || state.actors.size()>71)throw DecodeError("Invalid Turn Undead roster");
    std::set<std::pair<CombatActorOrigin,std::uint64_t>> ids;std::set<std::uint8_t> slots;
    std::array<std::uint8_t,1250> expected{};
    for(const auto& a:state.actors) {
        if(!ids.emplace(a.id.origin,a.id.value).second)throw DecodeError("Duplicate Turn Undead actor identity");
        if(a.removed)continue;
        if(!a.native_slot || !slots.insert(a.native_slot).second || a.character.byte(0x10e)>1)
            throw DecodeError("Invalid Turn Undead slot or allegiance");
        for(const auto p:spatial_.footprint(a)) {
            if(!Battlefield::in_bounds(p.x,p.y))throw DecodeError("Turn Undead footprint outside battlefield");
            auto& cell=expected[static_cast<std::size_t>(p.y*50+p.x)];
            if(cell)throw DecodeError("Overlapping Turn Undead footprints");
            cell=a.native_slot;
        }
    }
    if(expected!=state.occupancy)throw DecodeError("Turn Undead occupancy disagrees with actors");
    TurnUndeadResult result;result.state=state;auto rng=random;
    const auto index=static_cast<std::size_t>(it-state.actors.begin());auto& caster=result.state.actors[index];
    caster.extension[0x11]=1;
    result.count_roll=roll(rng,12);result.check_roll=roll(rng,20);
    const auto level=caster.character.byte(0x96);
    const unsigned band=level<=8?level:level<=13?9:10;
    unsigned remaining=result.count_roll,minimum=6;
    // Native targeting is rebuilt after each recipient. Smallest type wins;
    // equal types retain the source distance/direction/native-slot order.
    while(remaining) {
        std::optional<std::size_t> best;unsigned type=13;
        for(const auto& visible:targeting_.visible_targets(result.state,id,255)) {
            const auto owner=std::find_if(result.state.actors.begin(),result.state.actors.end(),[&](const auto& a){return a.id==*visible.actor;});
            const auto i=static_cast<std::size_t>(owner-result.state.actors.begin());
            const auto& a=result.state.actors[i];const auto kind=signed_byte(a.character.byte(0x76));
            if(a.removed || !a.shape || !a.position || a.extension[0x10] || kind<=0 || kind>=13 ||
               a.character.byte(0x10e)==caster.character.byte(0x10e))continue;
            if(static_cast<unsigned>(kind)<type) {
                best=i;type=static_cast<unsigned>(kind);
            }
        }
        if(!best)break;
        const auto threshold=thresholds_[type-1][band-1];const int magnitude=threshold<0?-threshold:threshold;
        if(result.check_roll<magnitude)break;
        auto& target=result.state.actors[*best];
        TurnUndeadRecipient recipient;recipient.actor=target.id;recipient.threshold=threshold;
        if(threshold<1) {
            recipient.outcome=TurnUndeadOutcome::destroyed;
            if(target.extension[0x13]==0) {
                if(result.state.bodies.size()>=71)throw DecodeError("Turn Undead body capacity exceeded");
                const auto p=*target.position;auto& terrain=result.state.battlefield.terrain[static_cast<std::size_t>(p.y*50+p.x)];
                result.state.bodies.push_back({target.id,p,terrain});if(terrain!=0x1e)terrain=0x1f;
            }
            for(const auto p:spatial_.footprint(target))result.state.occupancy[static_cast<std::size_t>(p.y*50+p.x)]=0;
            target.shape=0;stop(target);target.character.set_byte(0x10c,8);target.character.set_byte(0x10d,0);
        } else {recipient.outcome=TurnUndeadOutcome::turned;target.extension[0x10]=1;}
        result.recipients.push_back(recipient);
        if(minimum)--minimum;
        const auto next=remaining-1;
        if(!(next==0 && minimum && threshold<0))remaining=next;
    }
    result.failed=result.recipients.empty();stop(caster);random=rng;return result;
}
}
