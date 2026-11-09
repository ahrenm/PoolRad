#include "gbengine/combat/opponent_flight_service.hpp"
#include "gbengine/combat/combat_magic_effects.hpp"
#include "gbengine/assets/records.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/party/equipment.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
unsigned roll(std::mt19937& random,unsigned sides) {
    const auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    std::uint32_t n;do {n=static_cast<std::uint32_t>(random());}while(n<threshold);
    return n%sides+1;
}
bool positive(std::uint8_t n) {return n && n<128;}
}
std::size_t OpponentFlightService::find(const DeploymentState& state,CombatActorIdentity id) {
    const auto it=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return a.id==id;});
    if(it==state.actors.end() || it->removed)throw DecodeError("Opponent flight identity is stale");
    return static_cast<std::size_t>(it-state.actors.begin());
}
bool OpponentFlightService::has(const Character& c,unsigned code) {
    if(!c.effects_restored())throw DecodeError("Opponent flight requires restored effect ownership");
    return std::any_of(c.effects().begin(),c.effects().end(),[&](const auto& e){return e.code()==code;});
}
void OpponentFlightService::quit(DeploymentActor& a) {for(const auto at:{0u,3u,7u,6u})a.extension[at]=0;}
OpponentFlightService OpponentFlightService::load(const IFileSystem& files) {
    OpponentFlightService out;out.spatial_=CombatSpatialService::load(files);out.movement_=TacticalMovementService::load(files);
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    if(module.size()<0xc7c0+0xc26)throw DecodeError("Truncated native opponent flight tables");
    std::copy_n(module.begin()+0xc7c0+0x2ae,36,out.directions_.begin());
    for(unsigned policy=1;policy<=6;++policy)for(unsigned attempt=1;attempt<=5;++attempt)
        if(out.directions_[policy*5+attempt]>8)throw DecodeError("Invalid opponent flight direction priority");
    std::copy_n(module.begin()+0xc7c0+0xc14,16,out.cleanup_.begin());
    if(module[0xc7c0+0xc24]!=0x4a || module[0xc7c0+0xc25]!=0x4b)
        throw DecodeError("Unexpected selected-build opponent link cleanup order");
    out.loaded_=true;return out;
}
std::uint8_t OpponentFlightService::movement_budget(DeploymentActor& actor,std::uint8_t bonus) {
    auto n=actor.character.byte(0x11c);
    if(!actor.character.byte(0x10e))n=static_cast<std::uint8_t>(n+bonus);
    if(!n || n>96)n=1;
    n=static_cast<std::uint8_t>(n<<1);
    if(has(actor.character,0x27)) {
        const auto at=std::find_if(actor.character.effects().begin(),actor.character.effects().end(),[](const auto& e){return e.code()==0x27;});
        if(!(at->payload[3]&16))throw DecodeError("Opponent flight requires a presented Haste age notice");
        n=static_cast<std::uint8_t>(n<<1);
    }
    if(has(actor.character,0x2a))n=static_cast<std::uint8_t>(n>>1);
    if(has(actor.character,0x3a)){actor.extension[6]=0;n=0;}
    return n;
}
std::uint8_t OpponentFlightService::morale(DeploymentState& state,CombatActorIdentity id,std::uint8_t value) {
    auto& c=state.actors[find(state,id)].character;
    if(has(c,1))value=static_cast<std::uint8_t>(value+5);
    if(has(c,2))value=value<5?0:static_cast<std::uint8_t>(value-5);
    if(has(c,0x0b)){(void)CombatMagicEffects::apply_charm_in_place(state,id);value=100;}
    return value;
}
void OpponentFlightService::remove_link(DeploymentState& state,CombatActorIdentity id,std::uint8_t code,
        const ItemCatalog&,const SpellCatalog*,std::mt19937&) {
    const auto ai=find(state,id);auto& c=state.actors[ai].character;
    const auto node=std::find_if(c.effects_.begin(),c.effects_.end(),[&](const auto& e){return e.code()==code;});
    if(node==c.effects_.end())return;
    auto index=static_cast<std::size_t>(node-c.effects_.begin());
    if(node->payload[4]) {
        const auto slot=node->payload[3];
        const auto target=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return !a.removed && a.native_slot==slot;});
        if(!slot || target==state.actors.end())throw DecodeError("Opponent retained-link cleanup references an unavailable native actor slot");
        const auto ti=static_cast<std::size_t>(target-state.actors.begin());
        auto& victim=state.actors[ti];
        const auto held=std::find_if(victim.character.effects_.begin(),victim.character.effects_.end(),[](const auto& e){return e.code()==0x3a;});
        if(held!=victim.character.effects_.end()) {
            //2EE21452 ignores apply/reverse mode: query scratch0, ext6=0
            //when called by either the live query or removal callback.
            if(held->payload[4])victim.extension[6]=0;
            if(ai==ti && static_cast<std::size_t>(held-victim.character.effects_.begin())<index)--index;
            victim.character.effects_.erase(held);
        }
        if(code==0x4b && c.active() && victim.character.active())state.actors[ai].extension[0x10]=1;
    }
    c.effects_.erase(c.effects_.begin()+static_cast<std::ptrdiff_t>(index));
}
void OpponentFlightService::disable(DeploymentState& state,CombatActorIdentity id,std::uint8_t status,
        const ItemCatalog& items,const SpellCatalog* spells,std::mt19937& random,std::vector<CombatRoundNotice>& notices) const {
    const auto ai=find(state,id);auto& actor=state.actors[ai];
    if(!actor.character.active())return;
    for(const auto p:spatial_.footprint(actor))state.occupancy.at(static_cast<std::size_t>(p.y*50+p.x))=0;
    actor.character.set_byte(0x10d,0);actor.character.set_byte(0x10c,status);
    if(status!=3)actor.character.set_byte(0x11b,0);
    actor.shape=0;quit(actor);
    //49D5100A selects only the first remaining occurrence of each of16 codes.
    for(const auto code:cleanup_) {
        auto& character=state.actors[ai].character;
        const auto at=std::find_if(character.effects_.begin(),character.effects_.end(),[&](const auto& e){return e.code()==code;});
        if(at==character.effects_.end())continue;
        const auto index=static_cast<std::size_t>(at-character.effects_.begin());
        if(code==0x4a || code==0x4b)remove_link(state,id,code,items,spells,random);
        else if(!at->payload[4])character.effects_.erase(at);
        else {
            if(!spells)throw DecodeError("Opponent surrender cleanup requires its source effect catalog");
            const auto feedback=CombatMagicEffects::remove_effect_in_place(state,id,index,items,*spells,random);
            for(const auto& notice:feedback) {
                if(notice.member>=state.actors.size())throw DecodeError("Opponent surrender callback notice has stale ownership");
                notices.push_back({state.actors[notice.member].id,notice.text_id});
            }
        }
    }
}
OpponentFlightPreparation OpponentFlightService::prepare(const DeploymentState& state,CombatActorIdentity id,
        const ItemCatalog& items,const SpellCatalog* spells,const OpponentFlightContext& context,std::mt19937& random) const {
    if(!loaded_)throw DecodeError("Opponent flight tables are uninitialized");
    OpponentFlightPreparation out{state,{}};auto rng=random;const auto ai=find(out.state,id);
    (void)has(out.state.actors[ai].character,0);
    if(out.state.actors[ai].character.byte(0x10e)>1)throw DecodeError("Opponent morale allegiance is not binary");
    out.state.actors[ai].extension[0x14]=0;
    for(const auto code:{std::uint8_t{0x4a},std::uint8_t{0x4b}})remove_link(out.state,id,code,items,spells,rng);
    if(out.state.actors[ai].extension[0x10]) {
        out.state.actors[ai].extension[0x14]=1;
        out.notices.push_back({id,pascal_string_id("GAME.OVR",0xbc7a)});
    } else if(out.state.actors[ai].character.byte(0x84)>=128) {
        auto threshold=static_cast<std::uint8_t>(out.state.actors[ai].character.byte(0x84)<<1);
        if(threshold>102)threshold=0;
        threshold=morale(out.state,id,threshold);
        const auto& c=out.state.actors[ai].character;
        if(!c.byte(0x32))throw DecodeError("Opponent morale has zero source maximum HP");
        const auto missing=100-static_cast<int>(static_cast<unsigned>(c.hit_points())*100/c.byte(0x32));
        if(!threshold || static_cast<int>(threshold)<missing) {
            const auto group=morale(out.state,id,context.opponent_hp_percent);
            const auto casualties=static_cast<std::uint16_t>(100-context.casualty_percentage);
            if(group<casualties || !group || !out.state.actors[ai].character.byte(0x10e)) {
                std::uint8_t greatest=0;
                const auto opposing=static_cast<std::uint8_t>(!out.state.actors[ai].character.byte(0x10e));
                for(auto& other:out.state.actors)if(!other.removed && other.character.active() && other.character.byte(0x10e)==opposing)
                    greatest=std::max(greatest,static_cast<std::uint8_t>(movement_budget(other,context.party_movement_bonus)>>1));
                const auto own=static_cast<std::uint8_t>(movement_budget(out.state.actors[ai],context.party_movement_bonus)>>1);
                if(own<greatest) {
                    if(out.state.actors[ai].character.byte(0x11)>5) {
                        out.notices.push_back({id,pascal_string_id("GAME.OVR",0xbc8c)});
                        disable(out.state,id,4,items,spells,rng,out.notices);out.finished=true;
                    }
                } else out.state.actors[ai].extension[0x14]=1;
            }
        }
        if(out.state.actors[ai].extension[0x14] && !out.state.actors[ai].extension[0x10])
            out.notices.push_back({id,pascal_string_id("GAME.OVR",0xabc0)});
    }
    random=std::move(rng);return out;
}
OpponentFlightResult OpponentFlightService::step(const DeploymentState& state,const OpponentFlightProgress& progress,
        const ItemCatalog& items,const CombatAttackService& attacks,const CombatAttackContext& attack_context,
        const SpellCatalog* spells,const OpponentFlightContext& context,std::mt19937& random) const {
    if(!loaded_)throw DecodeError("Opponent flight tables are uninitialized");
    if(context.formation_facing>6 || (context.formation_facing&1))throw DecodeError("Opponent flight needs its native even exploration facing");
    OpponentFlightResult out{state,progress,{},{}};auto rng=random;const auto ai=find(out.state,progress.actor);
    auto& actor=out.state.actors[ai];auto& c=actor.character;
    if(!actor.extension[0x14])throw DecodeError("Opponent outward continuation requires source flight mode");
    if(!c.active() || !positive(actor.extension[3]) || actor.extension[3]==20) {
        out.finished=!positive(actor.extension[3]) || !c.active();out.continue_ordinary=!out.finished;return out;
    }
    if(!actor.extension[6]) {out.continue_ordinary=true;return out;}
    const auto finish=[&](bool guard) {
        quit(actor);if(guard)actor.extension[7]=1;out.finished=true;random=rng;return out;
    };
    if((actor.extension[6]>>1)==0) {
        const auto gear=EquipmentRules::inspect(c,items);
        const bool projectile=gear.slots[0] && items.definition(c.inventory().item(*gear.slots[0]).type()).byte(12)>=2;
        bool helpless=false;for(const auto code:{0x33u,0x34u,0x35u,0x1fu})helpless=helpless||has(c,code);
        return finish(!helpless && !projectile && actor.extension[3]);
    }
    unsigned selected=8;
    if(progress.pending_direction) {
        if(*progress.pending_direction>8)throw DecodeError("Opponent flight retained direction is malformed");
        selected=*progress.pending_direction;out.progress.pending_direction.reset();
        if(selected==8) {random=rng;return out;}
    } else {
        actor.extension[0x15]=static_cast<std::uint8_t>(roll(rng,2));
        auto direction=static_cast<std::uint8_t>(context.formation_facing-(((context.formation_facing+2)%4)>>1));
        if(!c.byte(0x10e))direction=static_cast<std::uint8_t>(direction+4);
        direction=static_cast<std::uint8_t>(direction%8);
        for(unsigned attempt=1;attempt<=5;++attempt) {
            const auto d=(direction+directions_[actor.extension[0x15]*5+attempt])%8;
            const auto probe=spatial_.probe(out.state,actor.id,d);
            if(!probe.terrain) {
                auto fled=movement_.stage_flight(out.state,actor.id,context.party_movement_bonus,rng,&items,spells);
                out.state=std::move(fled.state);out.escaped=fled.escaped;out.finished=true;
                auto& escaped=out.state.actors[ai];escaped.extension[6]=0;escaped.extension[0x14]=0;quit(escaped);
                out.notices.push_back({escaped.id,std::move(fled.notice_id)});
                out.notices.insert(out.notices.end(),fled.notices.begin(),fled.notices.end());random=std::move(rng);return out;
            }
            if(probe.occupied_slot || probe.terrain_cost>=actor.extension[6])continue;
            const auto cost=static_cast<std::uint8_t>(probe.step_cost*probe.terrain_cost);
            if(cost>actor.extension[6])continue;
            if(probe.terrain==30 && !(has(c,0x20)||has(c,0x1e)||has(c,0x6f)||has(c,0x7d)))continue;
            selected=d;break;
        }
        if(selected==8 || out.progress.last_direction==(selected+4)%8) {
            ++out.progress.reversals;actor.extension[0x15]=static_cast<std::uint8_t>(actor.extension[0x15]%6+1);
            if(out.progress.reversals>1) {
                actor.retained_target.reset();out.progress.target.reset();
                if(out.progress.reversals>=3) {actor.extension[6]=0;out.continue_ordinary=true;random=rng;return out;}
                //37E4 is shared with the ordinary opponent driver. Retain the
                //already chosen direction across that exact acquisition boundary:
                //resumption must not draw another d2 or increment reversal twice.
                out.reacquire_target=true;out.progress.pending_direction=static_cast<std::uint8_t>(selected);
                random=rng;return out;
            }
        }
    }
    if(selected==8) {random=rng;return out;}
    out.progress.last_direction=static_cast<std::uint8_t>(selected);
    auto moved=movement_.stage(out.state,actor.id,selected,items,attacks,attack_context,rng,spells);
    if(!moved.step.state || (!moved.step.moved && !moved.interrupted))
        throw DecodeError("Opponent flight movement callback is unresolved: "+moved.step.message);
    out.state=std::move(*moved.step.state);out.strikes=std::move(moved.strikes);out.notices=std::move(moved.notices);
    out.moved=moved.step.moved;out.finished=moved.interrupted || !positive(out.state.actors[ai].extension[3]);
    out.continue_ordinary=!out.finished && !out.state.actors[ai].extension[6];
    random=std::move(rng);return out;
}
void OpponentFlightService::actor_phase15(DeploymentState& state,std::size_t index,std::vector<CombatRoundNotice>* notices) {
    if(index>=state.actors.size() || state.actors[index].removed)throw DecodeError("Opponent phase15 actor is stale");
    auto& selected=state.actors[index];bool silenced=has(selected.character,0x15);
    const auto cells=[&](const DeploymentActor& actor) {
        std::vector<CombatPosition> out;
        if(!actor.position || !actor.shape)return out;
        for(int y=0;y<25;++y)for(int x=0;x<50;++x)
            if(state.occupancy[static_cast<std::size_t>(y*50+x)]==actor.native_slot)out.push_back({x,y});
        if(out.empty() || out.size()>4)throw DecodeError("Opponent phase15 aura has an invalid owned icon footprint");
        return out;
    };
    if(!silenced)for(const auto& other:state.actors)if(!other.removed && has(other.character,0x15)) {
        for(const auto from:cells(other))for(const auto to:cells(selected))
            if(CombatSpatialService::sight(state.battlefield,from,to,1).clear)silenced=true;
        if(silenced)break;
    }
    if(silenced) {
        if(selected.extension[2]) {
            if(!notices)throw DecodeError("Combat Silence capability notice requires a staged presentation binding");
            notices->push_back({selected.id,pascal_string_id("GAME.OVR",0xf35e)});
        }
        selected.extension[2]=0;selected.extension[1]=0;
    }
}
}
