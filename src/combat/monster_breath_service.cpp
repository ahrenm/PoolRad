#include "gbengine/combat/monster_breath_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/party/equipment.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
std::size_t find_actor(const DeploymentState& state,CombatActorIdentity id) {
    const auto at=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return a.id==id;});
    if(at==state.actors.end() || at->removed)throw DecodeError("Monster breath actor identity is stale");
    return static_cast<std::size_t>(at-state.actors.begin());
}
int sign(int value) {return (value>0)-(value<0);}
}
MonsterBreathService MonsterBreathService::load(const IFileSystem& files) {
    MonsterBreathService result;result.effects_=CombatMagicEffects::load(files);return result;
}
bool MonsterBreathService::should_activate(std::uint8_t round,std::mt19937& random) {
    if(!round)return true;
    constexpr std::uint32_t sides=100,threshold=(std::uint32_t{0}-sides)%sides;
    std::uint32_t value;do{value=static_cast<std::uint32_t>(random());}while(value<threshold);
    return value%sides+1>50;
}
MonsterBreathResult MonsterBreathService::stage_after_gate(const DeploymentState& state,
        const MonsterBreathContext& request,const ItemCatalog& items,const SpellCatalog& spells,
        std::mt19937& random) const {
    const auto ci=find_actor(state,request.caster);const auto& caster=state.actors[ci];
    if(!caster.position || !caster.shape || !caster.character.effects_restored())
        throw DecodeError("Monster breath requires a placed actor and owned effects");
    if(request.damage_context.caster!=request.caster)
        throw DecodeError("Monster breath damage scratch belongs to another actor");
    if(request.effect_index>=caster.character.effects().size() ||
       caster.character.effects()[request.effect_index]!=request.expected_effect || request.expected_effect.code()!=0x58)
        throw DecodeError("Monster breath source node identity is stale");
    if(!Battlefield::in_bounds(request.acquired_cursor.x,request.acquired_cursor.y))
        throw DecodeError("Monster breath target cursor was not acquired on the battlefield");
    auto rng=random;MonsterBreathResult output;auto& result=output.magic;result.state=state;
    const auto origin=*caster.position;
    output.cursor={origin.x+sign(request.acquired_cursor.x-origin.x),origin.y+sign(request.acquired_cursor.y-origin.y)};
    // The original doubles positive components only, including large icons.
    // Neither footprint size nor a symmetric direction vector replaces it.
    if(output.cursor.x==origin.x+1)++output.cursor.x;
    if(output.cursor.y==origin.y+1)++output.cursor.y;
    auto source_index=request.effect_index;
    const auto emit=[&](CombatActorIdentity id,const std::string& text) {
        const auto scene=std::make_shared<const DeploymentState>(result.state);
        result.events.push_back({CombatMagicEventKind::feedback,result.feedback.size(),scene,scene});
        result.feedback.push_back({id,text,false});
    };
    emit(request.caster,pascal_string_id("GAME.OVR",0x2a0e7));
    // 49D5:0FD2 repeatedly removes the FIRST19 before the first projectile.
    // Track the original58 node by index, retaining duplicate-node identity.
    while(true) {
        const auto& nodes=result.state.actors[ci].character.effects();
        const auto node=std::find_if(nodes.begin(),nodes.end(),[](const auto& e){return e.code()==0x19;});
        if(node==nodes.end())break;
        const auto index=static_cast<std::size_t>(node-nodes.begin());
        const auto notices=CombatMagicEffects::remove_effect_in_place(result.state,request.caster,index,items,spells,rng);
        if(index<source_index)--source_index;
        for(const auto& notice:notices)emit(result.state.actors.at(notice.member).id,notice.text_id);
    }
    result.casting_before=std::make_shared<const DeploymentState>(result.state);
    const auto projectile=[&](CombatPosition from,CombatPosition to) {
        result.events.push_back({CombatMagicEventKind::projectile,result.projectiles.size(),std::make_shared<const DeploymentState>(result.state)});
        result.projectiles.push_back({from,to,0x13,4,50});
    };
    const auto at=[&](CombatPosition point,std::uint8_t amount,bool suppressed) {
        if(!Battlefield::in_bounds(point.x,point.y))return false;
        const auto terrain=result.state.battlefield.terrain_at(point.x,point.y);
        const bool wall=terrain && result.state.battlefield.tables.properties(terrain)[0]==255;
        const auto slot=result.state.occupancy[static_cast<std::size_t>(point.y*50+point.x)];
        if(slot) {
            const auto actor=std::find_if(result.state.actors.begin(),result.state.actors.end(),[&](const auto& a){return !a.removed && a.native_slot==slot;});
            if(actor==result.state.actors.end())throw DecodeError("Monster breath occupancy has no retained owner");
            const auto identity=actor->id;const auto ti=find_actor(result.state,identity);const auto hp=actor->character.hit_points();
            for(const auto& effect:actor->character.effects()) {
                if((effect.code()==0x69 || effect.code()==0x6a) && !request.level_known)
                    throw DecodeError("Monster breath recipient requires a bound resistance level");
                if((effect.code()==0x11 || (effect.code()==0x1c && !suppressed)) && !request.pending_id_known)
                    throw DecodeError("Monster breath recipient requires unowned native pending-ID scratch");
                if(effect.code()==0x7a) {
                    //30D9 passes HP directly; it never establishes681C. The
                    //7A primary56 branch alone sets count3 before reading it.
                    const auto& source=result.state.actors[ci].character;
                    const auto primary=EquipmentRules::inspect(source,items).slots[0];
                    if(!primary || source.inventory().item(*primary).type()!=0x56)
                        throw DecodeError("Monster breath recipient requires unowned native damage-dice scratch");
                }
            }
            const auto before=std::make_shared<const DeploymentState>(result.state);
            auto context=request.damage_context;context.absorption_suppressed=suppressed;
            const auto saved=CombatMagicEffects::save(result.state,identity,3,0,context,rng,0x0c).saved;
            auto resolved=effects_.damage(result.state,identity,amount,0x0c,saved,2,context,rng);
            const auto outcome=resolved.outcome;const auto notices=std::move(resolved.notices);result.state=std::move(resolved.state);
            for(const auto& notice:notices)emit(result.state.actors.at(notice.member).id,notice.text_id);
            result.events.push_back({CombatMagicEventKind::damage,result.damage.size(),before,std::make_shared<const DeploymentState>(result.state)});
            result.damage.push_back({request.caster,identity,outcome.amount,hp,result.state.actors[ti].character.hit_points(),result.state.actors[ti].character.status(),saved,outcome.interrupted_spell,0x0c});
        }
        return wall;
    };
    projectile(origin,output.cursor);
    (void)at(output.cursor,result.state.actors[ci].character.byte(0x32),request.damage_context.absorption_suppressed);
    // 30D9 re-reads32 after the first cell, then295B holds this BYTE for the
    // whole ray. This is base HP, not the current HP at11B.
    const auto continuing=result.state.actors[ci].character.byte(0x32);
    auto& centre=output.cursor;
    if(centre!=origin) {
        std::uint8_t remaining=20;int direction=1;
        auto last_slot=Battlefield::in_bounds(centre.x,centre.y)?result.state.occupancy[static_cast<std::size_t>(centre.y*50+centre.x)]:std::uint8_t{0};
        unsigned work=0;
        while(remaining) {
            const CombatPosition end{centre.x+(centre.x-origin.x)*direction*remaining,centre.y+(centre.y-origin.y)*direction*remaining};
            const auto line=CombatSpatialService::trace_line(centre,end);std::size_t index=0;std::uint8_t distance=0;bool wall=false;
            do {
                const auto previous=line[index];std::uint8_t terrain=0,slot=0;
                while(index+1<line.size()) {
                    if(++work>4096)throw DecodeError("Monster breath traversal exceeded the native bounded field");
                    const auto before=line[index++];const auto point=line[index];
                    distance=static_cast<std::uint8_t>(distance+((point.x!=before.x && point.y!=before.y)?3:2));
                    terrain=0;slot=0;
                    if(Battlefield::in_bounds(point.x,point.y)) {
                        terrain=result.state.battlefield.terrain_at(point.x,point.y);
                        slot=result.state.occupancy[static_cast<std::size_t>(point.y*50+point.x)];
                    }
                    if((slot && slot!=last_slot) || !terrain || result.state.battlefield.tables.properties(terrain)[0]>1 || remaining<=distance)break;
                }
                const auto point=line[index];last_slot=slot;if(!terrain)remaining=0;
                projectile(previous,point);wall=at(point,continuing,true);
                if(wall) {centre=point;direction=-direction;last_slot=0;}
                remaining=distance<remaining?static_cast<std::uint8_t>(remaining-distance):std::uint8_t{0};
                if(index+1>=line.size())remaining=0;
            } while(!wall && remaining);
        }
    }
    const auto& nodes=result.state.actors[ci].character.effects();
    if(source_index>=nodes.size() || nodes[source_index]!=request.expected_effect)
        throw DecodeError("Monster breath source node changed during its native callbacks");
    if(nodes[source_index].payload[3]>=0xfe)--result.state.actors[ci].character.effects_[source_index].payload[3];
    else {
        const auto notices=CombatMagicEffects::remove_effect_in_place(result.state,request.caster,source_index,items,spells,rng);
        for(const auto& notice:notices)emit(result.state.actors.at(notice.member).id,notice.text_id);
    }
    for(const auto offset:{3u,0u,7u,6u})result.state.actors[ci].extension[offset]=0;
    random=std::move(rng);return output;
}
}
