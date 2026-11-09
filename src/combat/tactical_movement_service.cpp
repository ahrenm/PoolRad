#include "gbengine/combat/tactical_movement_service.hpp"
#include "gbengine/assets/records.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/combat/cloud_service.hpp"
#include "gbengine/combat/combat_magic_effects.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
bool has(const Character& c,std::uint8_t code) {
    return std::any_of(c.effects().begin(),c.effects().end(),[&](const auto& e){return e.code()==code;});
}
void quit(DeploymentActor& a) {for(const auto at:{0u,3u,7u,6u})a.extension[at]=0;}
unsigned roll(std::mt19937& random,unsigned sides) {
    const auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    std::uint32_t n;do{n=static_cast<std::uint32_t>(random());}while(n<threshold);
    return n%sides+1;
}
}
TacticalMovementService TacticalMovementService::load(const IFileSystem& files) {
    TacticalMovementService out;out.spatial_=CombatSpatialService::load(files);
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    if(module.size()<0xc7c0+0x2870)throw DecodeError("Truncated tactical movement effect table");
    std::copy_n(module.begin()+0xc7c0+0x286c,4,out.helpless_codes_.begin());
    std::copy_n(module.begin()+0xc7c0+0xc14,16,out.cleanup_codes_.begin());
    out.loaded_=true;return out;
}
std::size_t TacticalMovementService::find(const DeploymentState& state,CombatActorIdentity id) {
    const auto it=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return a.id==id;});
    if(it==state.actors.end())throw DecodeError("Tactical movement actor identity is stale");
    return static_cast<std::size_t>(it-state.actors.begin());
}
bool TacticalMovementService::helpless(const Character& c) const {
    if(!c.effects_restored())throw DecodeError("Tactical movement requires restored effects");
    return std::any_of(helpless_codes_.begin(),helpless_codes_.end(),[&](auto code){return has(c,code);});
}
//Native4E80057B sector, shared by source adjacency ordering and departure arc.
bool TacticalMovementService::sector(const BattlefieldTables& tables,unsigned d,CombatPosition from,CombatPosition to) {
    if(d>8)throw DecodeError("Invalid tactical movement sector");
    if(from==to)return true;
    const int x=from.x+tables.delta_x(d),y=from.y+tables.delta_y(d),tx=to.x,ty=to.y;
    if(x==tx && y==ty)return true;
    switch(d) {
    case 0:return !((tx<x || x-tx+y<ty) && (x<tx || tx-x+y<ty));
    case 1:return !((tx<x || x-tx+y<ty) && (tx<x+y-ty || y<ty));
    case 2:return !((tx<x+y-ty || y<ty) && (tx<x+ty-y || ty<y));
    case 3:return !((tx<x+ty-y || ty<y) && (tx<x || ty<tx-x+y));
    case 4:return !((tx<x || ty<tx-x+y) && (x<tx || ty<x-tx+y));
    case 5:return !((x<tx || ty<x-tx+y) && (x+y-ty<tx || ty<y));
    case 6:return !((x+y-ty<tx || ty<y) && (x+ty-y<tx || y<ty));
    case 7:return !((x+ty-y<tx || y<ty) && (x<tx || tx-x+y<ty));
    default:return true;
    }
}
std::vector<TacticalMovementService::Adjacent> TacticalMovementService::adjacent(const DeploymentState& state,const DeploymentActor& selected) const {
    std::vector<const DeploymentActor*> ordered;
    for(const auto& a:state.actors)if(!a.removed && a.position && a.shape)ordered.push_back(&a);
    std::sort(ordered.begin(),ordered.end(),[](const auto* a,const auto* b){return a->native_slot<b->native_slot;});
    std::vector<Adjacent> out;
    for(const auto* a:ordered) {
        bool clear=false;std::uint8_t distance=255,direction=0;
        for(const auto to:spatial_.footprint(*a))for(const auto from:spatial_.footprint(selected)) {
            const auto sight=CombatSpatialService::sight(state.battlefield,from,to,1);
            if(sight.clear) {
                clear=true;
                if(sight.distance<distance) {
                    distance=sight.distance;
                    for(unsigned d=0;d<8;++d)if(sector(state.battlefield.tables,d,from,to)) {
                        direction=static_cast<std::uint8_t>(d);break;
                    }
                }
            }
        }
        if(clear)out.push_back({a->id,distance,direction});
    }
    //Native002E uses swaps, not a stable comparator: exact ties can reorder.
    for(std::size_t a=0;a<out.size();++a)for(std::size_t b=a+1;b<out.size();++b)
        if(out[b].distance<out[a].distance || (out[b].distance==out[a].distance &&
           out[b].direction<out[a].direction && out[b].direction%2<=out[a].direction%2))std::swap(out[a],out[b]);
    const auto opposing=static_cast<std::uint8_t>(selected.character.byte(0x10e)==0);
    std::erase_if(out,[&](const auto& a){return state.actors[find(state,a.id)].character.byte(0x10e)!=opposing;});
    if(out.size()>12)throw DecodeError("Native departure adjacency exceeds its twelve-entry scratch list");
    return out;
}
TacticalMovementResult TacticalMovementService::stage(const DeploymentState& state,CombatActorIdentity id,unsigned direction,
        const ItemCatalog& catalog,const CombatAttackService& attacks,const CombatAttackContext& context,std::mt19937& random,
        const SpellCatalog* spells) const {
    if(!loaded_)throw DecodeError("Tactical movement tables are uninitialized");
    if(direction>7)throw DecodeError("Tactical movement requires one of eight directions");
    const auto probe=spatial_.probe(state,id,direction);const auto selected_index=find(state,id);
    const auto& selected=state.actors[selected_index];
    //Retain source facing/ordinary block handling and explicit attack/flee
    //boundaries. Departures start only after terrain affordability succeeds.
    if(probe.occupied_slot || !probe.terrain || probe.terrain_cost>selected.extension[6] ||
       !selected.character.active() || helpless(selected.character) || selected.extension[6]<2)
        return {spatial_.stage(state,id,direction),{},false};
    auto candidate=state;auto rng=random;TacticalMovementResult out;
    candidate.actors[selected_index].extension[9]=static_cast<std::uint8_t>(direction);
    const auto before=adjacent(candidate,candidate.actors[selected_index]);
    auto tentative=candidate.actors[selected_index];tentative.position=probe.destination;
    const auto after=adjacent(candidate,tentative);
    for(const auto& entry:before) {
        if(std::any_of(after.begin(),after.end(),[&](const auto& value){return value.id==entry.id;}))continue;
        if(!candidate.actors[selected_index].character.active())break;
        const auto index=find(candidate,entry.id);auto& a=candidate.actors[index];
        if(helpless(a.character) || !attacks.target_available(candidate,a.id,id) || has(a.character,0x4a) || has(a.character,0x4b))continue;
        bool arc=a.extension[3]>0 && a.extension[3]<128;
        if(!a.extension[0xf])arc=true;
        if(!arc)for(unsigned d=a.extension[9]+6;d<=static_cast<unsigned>(a.extension[9])+10;++d)
            if(sector(candidate.battlefield.tables,d%8,*a.position,*candidate.actors[selected_index].position)){arc=true;break;}
        if(!arc)continue;
        unsigned family=a.character.byte(0xa1)?1:2;
        for(unsigned f=1;f<=2;++f)if(a.character.byte(0x112+f))family=f;
        if(!a.character.byte(0x112+family))a.character.set_byte(0x112+family,1);
        a.extension[4]=static_cast<std::uint8_t>(family);
        const auto attacked=attacks.stage(candidate,a.id,id,catalog,context,rng,CombatAttackKind::departure_reaction);
        candidate=attacked.state;out.strikes.insert(out.strikes.end(),attacked.strikes.begin(),attacked.strikes.end());
    }
    if(!candidate.actors[selected_index].character.active() || helpless(candidate.actors[selected_index].character)) {
        quit(candidate.actors[selected_index]);out.step.status=CombatStepStatus::blocked;
        out.step.movement_exhausted=true;out.step.state=std::move(candidate);out.interrupted=true;
        random=std::move(rng);return out;
    }
    out.step=spatial_.stage_after_departure(candidate,id,direction,spells!=nullptr);
    if(!out.step.state)return {out.step,{},false};
    candidate=std::move(*out.step.state);
    if(out.step.moved) {
        const auto arrival=adjacent(candidate,candidate.actors[selected_index]);
        for(const auto& entry:arrival) {
            if(!candidate.actors[selected_index].character.active())break;
            const auto index=find(candidate,entry.id);auto& a=candidate.actors[index];
            if(!a.extension[7] || helpless(a.character))continue;
            a.extension[7]=0;
            auto attacked=attacks.stage(candidate,a.id,id,catalog,context,rng,CombatAttackKind::arrival_guard);
            candidate=std::move(attacked.state);out.strikes.insert(out.strikes.end(),attacked.strikes.begin(),attacked.strikes.end());
        }
        // 29CB09DF / 2AF306F8 invoke contact after0712 has applied movement
        // and arrival guards. The selected mover is the native caster context
        // after the spell/item executor has cleared its pending cast index.
        if(candidate.actors[selected_index].character.active() && spells) {
            const CombatMagicContext contact{id,0,0,candidate.actors[selected_index].character.byte(0x96),0};
            for(const auto& notice:CombatCloudService::contact_in_place(candidate,id,contact,*spells,catalog,rng))
                out.notices.push_back({id,notice.text_id});
        }
        if(!candidate.actors[selected_index].character.active() || helpless(candidate.actors[selected_index].character)) {
            quit(candidate.actors[selected_index]);out.interrupted=true;out.step.movement_exhausted=true;
        }
    }
    out.step.state=std::move(candidate);random=std::move(rng);return out;
}
std::uint8_t TacticalMovementService::movement_budget(DeploymentActor& a,std::uint8_t bonus) {
    auto n=a.character.byte(0x11c);
    if(!a.character.byte(0x10e))n=static_cast<std::uint8_t>(n+bonus);
    if(!n || n>96)n=1;
    n=static_cast<std::uint8_t>(n<<1);
    if(has(a.character,0x27)) {
        const auto it=std::find_if(a.character.effects().begin(),a.character.effects().end(),[](const auto& e){return e.code()==0x27;});
        if(!(it->payload[3]&0x10))throw DecodeError("Combat flight requires a presented Haste age notice");
        n=static_cast<std::uint8_t>(n<<1);
    }
    if(has(a.character,0x2a))n=static_cast<std::uint8_t>(n>>1);
    if(has(a.character,0x3a)){a.extension[6]=0;n=0;}
    return n;
}
TacticalFlightResult TacticalMovementService::stage_flight(const DeploymentState& state,CombatActorIdentity id,
        std::uint8_t bonus,std::mt19937& random,const ItemCatalog* items,const SpellCatalog* spells) const {
    if(!loaded_)throw DecodeError("Tactical flight source tables are uninitialized");
    const auto index=find(state,id);const auto& original=state.actors[index];
    //Probe also validates the complete owned occupancy graph before mutation.
    (void)spatial_.probe(state,id,8);
    if(original.removed || !original.character.active() || helpless(original.character))
        throw DecodeError("Tactical flight requires an active nonhelpless placed actor");
    TacticalFlightResult out{state,false,{}};auto rng=random;auto& a=out.state.actors[index];
    bool visible=false;
    for(const auto& other:out.state.actors) {
        if(other.removed || !other.position || !other.shape || other.character.byte(0x10e)==a.character.byte(0x10e))continue;
        for(const auto from:spatial_.footprint(a))for(const auto to:spatial_.footprint(other))
            if(CombatSpatialService::sight(out.state.battlefield,from,to,255).clear)visible=true;
    }
    if(!visible)out.escaped=true;
    else {
        const auto own=static_cast<std::uint8_t>(movement_budget(a,bonus)>>1);
        std::uint8_t greatest=0;
        for(auto& other:out.state.actors)if(!other.removed && other.character.active() &&
            other.character.byte(0x10e)!=a.character.byte(0x10e)) {
            if(!other.character.effects_restored())throw DecodeError("Tactical flight needs opposing effect graphs");
            greatest=std::max(greatest,static_cast<std::uint8_t>(movement_budget(other,bonus)>>1));
        }
        out.escaped=greatest<own || (greatest==own && roll(rng,2)==1);
    }
    if(out.escaped) {
        //49D50F06 removes the icon without death/body creation and preservesHP.
        a.character.set_byte(0x10d,0);a.character.set_byte(0x10c,3);
        for(const auto p:spatial_.footprint(a))out.state.occupancy[static_cast<std::size_t>(p.y*50+p.x)]=0;
        a.shape=0;quit(a);
        for(const auto code:cleanup_codes_) {
            // Generic reversals can replace the owned actor vector. Reacquire
            // character/indices each time and never retain a borrowed actor
            // across a callback. Only the native first matching node is removed.
            auto& character=out.state.actors[index].character;
            const auto it=std::find_if(character.effects_.begin(),character.effects_.end(),[&](const auto& e){return e.code()==code;});
            if(it==character.effects_.end())continue;
            if(it->payload[4]) {
                if(!items || !spells)throw DecodeError("Tactical escape cleanup requires its actual effect reversal context");
                const auto effect_index=static_cast<std::size_t>(it-character.effects_.begin());
                const auto feedback=CombatMagicEffects::remove_effect_in_place(out.state,id,effect_index,*items,*spells,rng);
                for(const auto& notice:feedback) {
                    if(notice.member>=out.state.actors.size())throw DecodeError("Tactical escape callback notice lost actor ownership");
                    out.notices.push_back({out.state.actors[notice.member].id,notice.text_id});
                }
            } else character.effects_.erase(it);
        }
    }
    quit(out.state.actors[index]);out.notice_id=pascal_string_id("GAME.OVR",out.escaped?0x1344a:0x13453);
    //Validate again via a remaining placed actor when present. Escaped actor
    //itself no longer has a footprint and must never receive a fake body.
    for(const auto& remaining:out.state.actors)if(!remaining.removed && remaining.position && remaining.shape) {
        (void)spatial_.probe(out.state,remaining.id,8);break;
    }
    random=std::move(rng);return out;
}
}
