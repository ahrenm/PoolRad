#include "gbengine/combat/tactical_command_service.hpp"
#include "gbengine/party/equipment.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
std::size_t find(const DeploymentState& state,CombatActorIdentity id) {
    const auto it=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& actor){return actor.id==id && !actor.removed;});
    if(it==state.actors.end())throw DecodeError("Tactical command actor identity is stale");
    return static_cast<std::size_t>(it-state.actors.begin());
}
bool bandageable(const DeploymentActor& actor) {
    return !actor.removed && !actor.extension[0x13] && !actor.character.byte(0x10e) && actor.character.byte(0x10c)==5;
}
void quit(DeploymentActor& actor) {
    for(const auto offset:{0u,3u,7u,6u})actor.extension[offset]=0;
}
void automatic(DeploymentState& state,std::size_t index) {
    auto& actor=state.actors[index];
    actor.character.set_byte(0x10f,1);
    if(!actor.retained_target && std::any_of(actor.extension.begin()+10,actor.extension.begin()+14,[](auto n){return n!=0;}))
        throw DecodeError("Quick has an unresolved native target link");
    //29CB:122B clears a same-side retained target, preserving hostile links.
    if(actor.retained_target && state.actors[find(state,*actor.retained_target)].character.byte(0x10e)==actor.character.byte(0x10e))
        actor.retained_target.reset();
}
}
TacticalCommandAvailability TacticalCommandService::available(const DeploymentState& state,CombatActorIdentity id,const ItemCatalog& catalog) {
    const auto& actor=state.actors[find(state,id)];
    TacticalCommandAvailability result;
    const auto slots=EquipmentRules::inspect(actor.character,catalog);
    result.guard=true;
    if(slots.slots[0]) {
        const auto& weapon=actor.character.inventory().item(*slots.slots[0]);
        const auto& definition=catalog.definition(weapon.type());
        result.guard=definition.byte(12)<2 || (definition.attack_flags()&0x14)==0x14;
    }
    result.bandage=std::any_of(state.actors.begin(),state.actors.end(),bandageable);
    return result;
}
TacticalCommandResult TacticalCommandService::stage(const DeploymentState& state,CombatActorIdentity id,TacticalCommand command,
                                                   const ItemCatalog& catalog,TacticalCommandContext context) {
    const auto index=find(state,id);
    const auto& current=state.actors[index];
    if(!current.character.active() || !current.extension[3] || current.extension[3]>=128)
        throw DecodeError("Tactical command requires an active actor with positive priority");
    const bool manual=command==TacticalCommand::manual_control;
    if(context.automatic_interrupt && !manual)throw DecodeError("Automatic interruption only accepts manual control");
    if(!manual && current.character.byte(0x10f))throw DecodeError("Player tactical command requires manual control");
    TacticalCommandResult result{state,{},false,false,false};
    auto& actor=result.state.actors[index];
    switch(command) {
    case TacticalCommand::guard:
        if(!available(state,id,catalog).guard)throw DecodeError("Guard is absent for the source readied weapon");
        quit(actor);actor.extension[7]=1;
        result.notices.push_back({id,pascal_string_id("START.EXE:module",0x321d),false});
        result.resume_selection=true;break;
    case TacticalCommand::delay:
        actor.extension[3]=1;result.resume_selection=true;break;
    case TacticalCommand::quit:
        quit(actor);result.resume_selection=true;break;
    case TacticalCommand::bandage: {
        const auto target=std::find_if(result.state.actors.begin(),result.state.actors.end(),bandageable);
        if(target==result.state.actors.end())throw DecodeError("Bandage has no native eligible recipient");
        target->character.set_byte(0x10c,4);target->extension[0xe]=0;
        result.notices.push_back({target->id,pascal_string_id("GAME.OVR",0xa7fa),true});
        quit(actor);result.resume_selection=true;break;
    }
    case TacticalCommand::quick:
        automatic(result.state,index);result.begin_automatic=true;break;
    case TacticalCommand::quick_all:
        actor.extension[3]=20;
        for(std::size_t i=0;i<result.state.actors.size();++i)if(!result.state.actors[i].removed)automatic(result.state,i);
        result.resume_selection=true;break;
    case TacticalCommand::manual_control:
        for(auto& other:result.state.actors)if(!other.removed && other.character.byte(0x84)<128 &&
            (!context.automatic_interrupt || other.character.byte(0x10c)!=1))other.character.set_byte(0x10f,0);
        if(context.automatic_interrupt && !actor.character.byte(0x10f)) {
            actor.extension[3]=20;result.resume_selection=true;result.manual_active=true;
        }
        break;
    }
    return result;
}
}
