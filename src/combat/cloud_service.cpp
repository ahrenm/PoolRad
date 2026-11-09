#include "gbengine/combat/cloud_service.hpp"
#include "gbengine/magic/magic_effect_service.hpp"
#include "gbengine/party/equipment.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
std::size_t actor_index(const DeploymentState& state,CombatActorIdentity id) {
    const auto at=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return a.id==id && !a.removed;});
    if(at==state.actors.end())throw DecodeError("Combat cloud actor identity is not retained");
    return static_cast<std::size_t>(at-state.actors.begin());
}
std::uint8_t die(std::mt19937& random,unsigned sides) {
    const auto threshold=(std::uint32_t{0}-sides)%sides;std::uint32_t value;
    do{value=static_cast<std::uint32_t>(random());}while(value<threshold);return static_cast<std::uint8_t>(value%sides+1);
}
bool has(const Character& c,unsigned code) {return std::any_of(c.effects().begin(),c.effects().end(),[&](const auto& n){return n.code()==code;});}
}
bool CombatCloudService::nausea_in_place(DeploymentActor& actor,const ItemCatalog& items) {
    const bool coughing=actor.extension[2]!=0;
    actor.extension[2]=0;actor.extension[1]=0;
    EquipmentRules::recalculate(actor.character,items);
    const auto rear=actor.character.byte(0x112);
    actor.character.set_byte(0x112,rear<53?std::uint8_t{50}:static_cast<std::uint8_t>(rear-2));
    actor.character.set_byte(0x111,actor.character.byte(0x112));
    return coughing;
}
std::vector<EffectNotice> CombatCloudService::contact_in_place(DeploymentState& state,CombatActorIdentity target,
        const CombatMagicContext& context,const SpellCatalog& spells,const ItemCatalog& items,std::mt19937& random) {
    auto candidate=state;auto rng=random;const auto ti=actor_index(candidate,target);std::vector<EffectNotice> notices;
    const auto& c=candidate.actors[ti].character;
    if(!c.active())return notices;
    if(!c.effects_restored())throw DecodeError("Combat cloud requires owned effect graphs");
    // Every retained source footprint containing terrain1E qualifies.
    const auto shape=static_cast<unsigned>(candidate.actors[ti].shape);
    if(shape>4 || !candidate.actors[ti].position)throw DecodeError("Combat cloud target has no source footprint");
    bool inside=false;const auto module=spells.native_module();
    for(unsigned n=0;n<4;++n) {
        const auto at=0xc7c0+0x2844+shape*8+n*2;
        if(at+1>=module.size())throw DecodeError("Truncated source cloud footprint table");
        const auto x=module[at]<128?module[at]:static_cast<int>(module[at])-256;
        const auto y=module[at+1]<128?module[at+1]:static_cast<int>(module[at+1])-256;
        if(x<0)continue;
        const auto p=CombatPosition{candidate.actors[ti].position->x+x,candidate.actors[ti].position->y+y};
        if(Battlefield::in_bounds(p.x,p.y) && candidate.battlefield.terrain_at(p.x,p.y)==0x1e)inside=true;
    }
    if(!inside || has(c,0x1e) || has(c,0x1f) || has(c,0x20) || has(c,0x6f) || has(c,0x7d))return notices;
    const auto saved=CombatMagicEffects::save(candidate,target,0,0,context,rng).saved;
    const std::uint8_t incoming=saved?0x1e:0x1f;
    const auto duration=saved?std::uint16_t{1}:static_cast<std::uint16_t>(die(rng,4)+1);
    auto& recipient=candidate.actors[ti].character;
    const auto accepted=MagicEffectService::application_queries(recipient,incoming,context.level,rng);
    if(accepted) {
        if(recipient.effects_.size()>=65535)throw DecodeError("Combat cloud effect capacity exceeded");
        CharacterEffect marker;marker.payload={incoming,0,0,255,0};marker.set_duration(duration);marker.source_file="GAME.OVR";marker.source_offset=0x2aeea+0x0ae7;
        recipient.effects_.push_back(std::move(marker));
    }
    notices.push_back({ti,pascal_string_id("GAME.OVR",accepted?(saved?0x2aeea+0x0abb:0x2aeea+0x0acb):0x2c532)});
    auto& actor=candidate.actors[ti];
    if(saved) {
        if(nausea_in_place(actor,items))notices.push_back({ti,pascal_string_id("GAME.OVR",0xf6b4)});
    } else for(const auto at:{3u,0u,7u,6u})actor.extension[at]=0;
    state=std::move(candidate);random=std::move(rng);return notices;
}
std::vector<EffectNotice> CombatCloudService::create_in_place(DeploymentState& state,CombatActorIdentity caster,
        CombatPosition centre,std::uint8_t level,std::optional<ItemOrigin> item,const SpellCatalog& spells,
        const ItemCatalog& items,std::mt19937& random) {
    if(!Battlefield::in_bounds(centre.x,centre.y))throw DecodeError("Combat cloud centre is outside battlefield");
    auto candidate=state;auto rng=random;const auto ci=actor_index(candidate,caster);std::vector<EffectNotice> notices;
    if(!candidate.actors[ci].character.effects_restored())throw DecodeError("Combat cloud caster effects are not owned");
    if(candidate.clouds.size()>=65535)throw DecodeError("Combat cloud record capacity exceeded");
    std::uint8_t ordinal=0;
    // Native1B11 tests next==null before counting the current owner. Its
    // existing tail is excluded, retaining duplicate owner ordinal0 on recast.
    for(std::size_t n=0;n+1<candidate.clouds.size();++n)if(candidate.clouds[n].caster==caster)++ordinal;
    CombatCloud cloud;cloud.caster=caster;cloud.ordinal=ordinal;cloud.centre=centre;
    const auto module=spells.native_module();std::array<std::optional<CombatActorIdentity>,4> occupants;
    for(unsigned n=0;n<4;++n) {
        const auto direction=module[0xc7c0+0x290e +n];if(direction>8)throw DecodeError("Invalid source cloud direction table");
        auto& cell=cloud.cells[n];cell.position={centre.x+candidate.battlefield.tables.delta_x(direction),centre.y+candidate.battlefield.tables.delta_y(direction)};
        if(!Battlefield::in_bounds(cell.position.x,cell.position.y))continue;
        auto terrain=candidate.battlefield.terrain_at(cell.position.x,cell.position.y);
        cell.active=terrain && candidate.battlefield.tables.properties(terrain)[0]!=255;
        const auto slot=candidate.occupancy[static_cast<std::size_t>(cell.position.y*50+cell.position.x)];
        if(slot){const auto found=std::find_if(candidate.actors.begin(),candidate.actors.end(),[&](const auto& a){return !a.removed && a.native_slot==slot;});if(found==candidate.actors.end())throw DecodeError("Combat cloud occupant has no owner");occupants[n]=found->id;}
        if(terrain==0x1e) {
            for(const auto& prior:candidate.clouds)for(const auto& old:prior.cells)if(old.active && old.position==cell.position && old.underlying_terrain!=0x1e)terrain=old.underlying_terrain;
        } else if(terrain==0x1f) {
            for(const auto& body:candidate.bodies)if(body.position==cell.position)terrain=body.underlying_terrain;
        }
        cell.underlying_terrain=terrain;
    }
    candidate.clouds.push_back(cloud);
    auto& c=candidate.actors[ci].character;if(c.effects_.size()>=65535)throw DecodeError("Combat cloud effect capacity exceeded");
    CharacterEffect marker;marker.payload={0x28,level,0,static_cast<std::uint8_t>(level+16*ordinal),1};marker.source_file="GAME.OVR";marker.source_offset=0x27018+0x1b11;marker.spell_id=std::uint8_t{34};marker.item_origin=std::move(item);c.effects_.push_back(std::move(marker));
    for(const auto& cell:cloud.cells)if(cell.active)candidate.battlefield.terrain[static_cast<std::size_t>(cell.position.y*50+cell.position.x)]=0x1e;
    notices.push_back({ci,pascal_string_id("GAME.OVR",0x27018+0x1af9)});
    for(const auto occupant:occupants)if(occupant) {
        const auto next=contact_in_place(candidate,*occupant,{caster,34,0,level,0},spells,items,rng);notices.insert(notices.end(),next.begin(),next.end());
    }
    state=std::move(candidate);random=std::move(rng);return notices;
}
bool CombatCloudService::expire_in_place(DeploymentState& state,CombatActorIdentity caster,std::uint8_t ordinal) {
    const auto at=std::find_if(state.clouds.begin(),state.clouds.end(),[&](const auto& cloud){return cloud.caster==caster && cloud.ordinal==ordinal;});
    if(at==state.clouds.end())return false;
    for(const auto& cell:at->cells)if(cell.active) {
        const auto body=std::any_of(state.bodies.begin(),state.bodies.end(),[&](const auto& value){return value.position==cell.position;});
        state.battlefield.terrain[static_cast<std::size_t>(cell.position.y*50+cell.position.x)]=body?0x1f:cell.underlying_terrain;
    }
    state.clouds.erase(at);
    for(const auto& cloud:state.clouds)for(const auto& cell:cloud.cells)if(cell.active)state.battlefield.terrain[static_cast<std::size_t>(cell.position.y*50+cell.position.x)]=0x1e;
    return true;
}
}
