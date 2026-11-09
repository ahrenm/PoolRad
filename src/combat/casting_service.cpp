#include "gbengine/combat/casting_service.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
std::size_t locate(const DeploymentState& state,CombatActorIdentity id) {
    const auto found=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& actor){return actor.id==id;});
    if(found==state.actors.end() || found->removed)throw DecodeError("Combat caster is not retained");
    return static_cast<std::size_t>(found-state.actors.begin());
}
const DeploymentActor& caster(const DeploymentState& state,CombatActorIdentity id) {return state.actors[locate(state,id)];}
void available(const DeploymentActor& actor,bool blocked,bool item,CombatCastingPolicy policy=CombatCastingPolicy::manual) {
    if(blocked)throw DecodeError("Casting is disabled by the current game state");
    if(!actor.character.active() || (policy==CombatCastingPolicy::manual && actor.character.status()==1) || !actor.character.effects_restored())
        throw DecodeError("Combat casting requires an active actor and restored effect graph");
    if(!actor.extension[3] || actor.extension[3]>=128 || actor.extension[0])
        throw DecodeError("Combat actor cannot select another action while a cast is queued");
    if(!actor.extension[item?2:1])throw DecodeError(item?"Combat item use is unavailable after this actor's prior actions":"Combat casting is unavailable after this actor's prior actions");
}
std::array<std::uint8_t,16> row(const SpellCatalog& catalog,std::uint8_t id) {
    if(id<=56)return catalog.definition(SpellId(id)).raw_bytes();
    return catalog.item_definition(ItemCastId(id)).raw_bytes();
}
std::uint8_t level(const Character& c,const std::array<std::uint8_t,16>& metadata,bool item) {
    if(metadata[0]==2)return 12;
    if(item)return 6;
    return c.byte(metadata[0]==0?0x96:0x9b);
}
const Item& owned(const DeploymentActor& actor,ItemId id) {
    if(!id)throw DecodeError("Combat item requires a stable owned identity");
    const auto& inventory=actor.character.inventory();
    for(std::size_t i=0;i<inventory.size();++i)if(inventory.item(i).id()==id)return inventory.item(i);
    throw DecodeError("Combat item is no longer owned by its caster");
}
}
std::vector<MemorizedSpell> CombatCastingService::memorized(const DeploymentState& state,CombatActorIdentity id,bool blocked) {
    const auto& actor=caster(state,id);available(actor,blocked,false);
    auto entries=Spellbook::memorized(actor.character);
    std::erase_if(entries,[](const auto& entry){return entry.pending;});
    return entries;
}
CombatCastSchedule CombatCastingService::schedule(const DeploymentState& state,CombatActorIdentity id,std::size_t slot,
                                                 const SpellCatalog& catalog,bool blocked,CombatCastingPolicy policy) {
    available(caster(state,id),blocked,false,policy);
    auto entries=Spellbook::memorized(caster(state,id).character);
    std::erase_if(entries,[](const auto& entry){return entry.pending;});
    const auto found=std::find_if(entries.begin(),entries.end(),[slot](const auto& entry){return entry.slot==slot;});
    if(found==entries.end())throw DecodeError("Selected combat spell is no longer memorized");
    const auto metadata=row(catalog,found->spell.value);
    if(!metadata[11])throw DecodeError("Selected spell cannot be cast during combat");
    CombatCastSchedule result;result.state=state;
    auto& actor=result.state.actors[locate(state,id)];
    result.pending.caster=id;result.pending.cast_index=found->spell.value;result.pending.source_slot=slot;
    result.pending.caster_level=0; //10BA:26C2 is evaluated by the executor, late.
    result.pending.priority_delay=static_cast<std::uint8_t>(metadata[12]/3);
    result.pending.delayed=result.pending.priority_delay!=0;
    result.resume_selection=result.pending.delayed;
    if(result.pending.delayed) {
        actor.extension[0]=found->spell.value;
        const auto priority=actor.extension[3],delay=result.pending.priority_delay;
        actor.extension[3]=priority>delay?static_cast<std::uint8_t>(priority-delay):std::uint8_t{1};
    }
    return result;
}
CombatCastSchedule CombatCastingService::prepare_item(const DeploymentState& state,CombatActorIdentity id,ItemId item_id,
                                                     std::optional<std::size_t> scroll_slot,const ItemCatalog& items,
                                                     const SpellCatalog& spells,bool blocked,CombatCastingPolicy policy) {
    const auto& actor=caster(state,id);available(actor,blocked,true,policy);
    const auto& item=owned(actor,item_id);
    if(!item.readied())throw DecodeError("Combat item must be readied before use");
    const auto& definition=items.definition(item.type());
    CombatCastSchedule result;result.state=state;
    auto& pending=result.pending;pending.caster=id;pending.item=item_id;pending.item_effects=item.effect_bytes();
    pending.item_origin=item.origin();pending.scroll_slot=scroll_slot;
    if(definition.scroll()) {
        if(!scroll_slot || *scroll_slot>=3)throw DecodeError("Choose a spell on this combat scroll");
        for(const auto raw:item.effect_bytes())if(raw && (!(raw&127) || (raw&127)>56))
            throw DecodeError("Unknown combat scroll spell identifier");
        const auto raw=item.effect_bytes()[*scroll_slot];
        if(!raw)throw DecodeError("Selected combat scroll slot is empty");
        const bool cleric=actor.character.byte(0x96)>0 && actor.character.byte(0x96)<128 && definition.category()==12;
        const bool reading=std::any_of(actor.character.effects().begin(),actor.character.effects().end(),[](const auto& effect){return effect.code()==0x10;});
        if(item.hidden_components() && !cleric && !reading)throw DecodeError("Combat scroll requires its user's magic-reading effect");
        pending.origin=CombatCastOrigin::scroll;pending.cast_index=static_cast<std::uint8_t>(raw&127);
    } else {
        if(scroll_slot)throw DecodeError("A non-scroll combat item has no spell slot");
        if(!item.byte(0x3d) || item.byte(0x3e)>=128)throw DecodeError("Combat item has no usable activation");
        const auto raw=item.byte(0x3d);
        pending.cast_index=raw>56?static_cast<std::uint8_t>(raw-23):raw;
        pending.origin=CombatCastOrigin::item;
    }
    const auto metadata=row(spells,pending.cast_index);
    pending.ends_turn=metadata[11]!=0;
    pending.caster_level=level(actor.character,metadata,true);
    //4053:1A8A bypasses326D:2414 entirely: all item casts execute immediately.
    return result;
}
DeploymentState CombatCastingService::read_scroll(const DeploymentState& state,CombatActorIdentity id,ItemId item_id,const ItemCatalog& catalog) {
    const auto& actor=caster(state,id);const auto& item=owned(actor,item_id);
    const auto& definition=catalog.definition(item.type());
    if(!definition.scroll() || !item.readied())throw DecodeError("Reading requires an owned readied combat scroll");
    for(const auto raw:item.effect_bytes())if(raw && (!(raw&127) || (raw&127)>56))
        throw DecodeError("Unknown combat scroll spell identifier");
    const bool cleric=actor.character.byte(0x96)>0 && actor.character.byte(0x96)<128 && definition.category()==12;
    const bool reading=actor.character.effects_restored() && std::any_of(actor.character.effects().begin(),actor.character.effects().end(),[](const auto& effect){return effect.code()==0x10;});
    if(item.hidden_components() && !cleric && !reading)throw DecodeError("Combat scroll requires its user's magic-reading effect");
    auto result=state;auto& inventory=result.actors[locate(result,id)].character.inventory_.items_;
    const auto found=std::find_if(inventory.begin(),inventory.end(),[item_id](const auto& entry){return entry.id()==item_id;});
    found->bytes_[0x35]=0;return result;
}
bool CombatCastingService::still_pending(const DeploymentState& state,const CombatCastPending& pending) {
    try {
        const auto& actor=caster(state,pending.caster);
        if(pending.origin!=CombatCastOrigin::memorized) {
            if(!pending.item)return false;
            const auto& item=owned(actor,*pending.item);
            return item.readied() && item.effect_bytes()==pending.item_effects && pending.item_origin==item.origin();
        }
        if(!actor.character.active())return false;
        if(pending.delayed && actor.extension[0]!=pending.cast_index)return false;
        const auto entries=Spellbook::memorized(actor.character);
        return std::any_of(entries.begin(),entries.end(),[&](const auto& entry){return !entry.pending && entry.spell.value==pending.cast_index;});
    } catch(const DecodeError&) {return false;}
}
DeploymentState CombatCastingService::finish(const DeploymentState& state,const CombatCastPending& pending,const ItemCatalog& catalog) {
    if(!still_pending(state,pending))throw DecodeError("Combat casting continuation no longer owns its source resource");
    auto result=state;auto& actor=result.actors[locate(result,pending.caster)];
    if(pending.origin==CombatCastOrigin::memorized) {
        (void)Spellbook::consume_first(actor.character,SpellId(pending.cast_index));
        actor.extension[0]=0;
        return result;
    }
    auto& inventory=actor.character.inventory_.items_;
    const auto found=std::find_if(inventory.begin(),inventory.end(),[&](const auto& item){return pending.item==item.id();});
    if(found==inventory.end())throw DecodeError("Combat item source disappeared while applying its effect");
    if(pending.origin==CombatCastOrigin::scroll) {
        std::size_t consumed=3;
        for(std::size_t i=0;i<3;++i)if((found->effect_bytes()[i]&127)==pending.cast_index)consumed=i;
        if(consumed==3)throw DecodeError("Combat scroll no longer contains its selected spell");
        found->bytes_[0x3c+consumed]=0;--found->bytes_[0x30];found->bytes_[0x35]=0;
        if(found->byte(0x30)<0xd2)inventory.erase(found);
    } else if(found->charges()) {
        if(found->quantity()>=2)--found->bytes_[0x39];
        else if(--found->bytes_[0x3c]==0)inventory.erase(found);
    }
    // Native inventory return recalculates scalar caches, without rerunning
    // actor phase7/15 or restoring spent attack/movement allowances.
    EquipmentRules::recalculate(actor.character,catalog);
    return result;
}
}
