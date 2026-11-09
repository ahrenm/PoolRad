#include "gbengine/combat/retained_ability_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
std::uint8_t roll4(std::mt19937& random) {return static_cast<std::uint8_t>(random()%4+1);}
void quit(DeploymentActor& actor) {for(const auto at:{0u,3u,7u,6u})actor.extension[at]=0;}
}
std::size_t RetainedAbilityService::find(const DeploymentState& state,CombatActorIdentity id) {
    const auto at=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return a.id==id && !a.removed;});
    if(at==state.actors.end())throw DecodeError("Retained ability owner is unavailable");
    return static_cast<std::size_t>(at-state.actors.begin());
}
std::size_t RetainedAbilityService::victim(const DeploymentState& state,std::uint8_t slot) {
    if(!slot)throw DecodeError("Retained ability references the null native slot");
    std::optional<std::size_t> result;
    for(std::size_t i=0;i<state.actors.size();++i)if(!state.actors[i].removed && state.actors[i].native_slot==slot) {
        if(result)throw DecodeError("Retained ability native slot is ambiguous");
        result=i;
    }
    if(!result)throw DecodeError("Retained ability victim native slot is unavailable");
    return *result;
}
void RetainedAbilityService::remove(DeploymentState& state,std::size_t ai,std::size_t index,bool reverse) {
    auto& owner=state.actors[ai];auto& nodes=owner.character.effects_;
    if(!owner.character.effects_restored())throw DecodeError("Retained ability removal requires owned SPC nodes");
    if(index>=nodes.size())throw DecodeError("Retained ability exact node was removed by a nested callback");
    const auto node=nodes[index];
    if(reverse && node.payload[4]) {
        const auto ti=victim(state,node.payload[3]);auto& target=state.actors[ti];
        if(!target.character.effects_restored())throw DecodeError("Retained ability reversal requires owned victim SPC nodes");
        auto& held=target.character.effects_;
        const auto at=std::find_if(held.begin(),held.end(),[](const auto& effect){return effect.code()==0x3a;});
        if(at!=held.end()) {
            if(at->payload[4])target.extension[6]=0;
            if(ai==ti && static_cast<std::size_t>(at-held.begin())<index)--index;
            held.erase(at);
        }
        if(node.code()==0x4b && owner.character.active() && target.character.active())owner.extension[0x10]=1;
    }
    nodes.erase(nodes.begin()+static_cast<std::ptrdiff_t>(index));
}
RetainedAbilityService RetainedAbilityService::load(const IFileSystem& files) {
    RetainedAbilityService result;result.attacks_=CombatAttackService::load(files);
    result.effects_=CombatMagicEffects::load(files);result.loaded_=true;return result;
}
void RetainedAbilityService::remove_effect_in_place(DeploymentState& state,CombatActorIdentity id,std::size_t index) {
    const auto ai=find(state,id);const auto& nodes=state.actors[ai].character.effects_;
    if(index>=nodes.size() || (nodes[index].code()!=0x4a && nodes[index].code()!=0x4b))
        throw DecodeError("Retained ability exact removal has stale effect ownership");
    remove(state,ai,index,true);
}
void RetainedAbilityService::release_inactive_in_place(DeploymentState& state,CombatActorIdentity id,std::uint8_t code) {
    if(code!=0x4a && code!=0x4b)throw DecodeError("Unknown inactive retained-victim ability");
    const auto ai=find(state,id);auto& owner=state.actors[ai];
    const auto at=std::find_if(owner.character.effects_.begin(),owner.character.effects_.end(),[&](const auto& e){return e.code()==code;});
    if(at==owner.character.effects_.end())return;
    auto index=static_cast<std::size_t>(at-owner.character.effects_.begin());
    const auto ti=victim(state,at->payload[3]);auto& target=state.actors[ti];
    if(!owner.character.effects_restored() || !target.character.effects_restored())
        throw DecodeError("Inactive retained release requires owned SPC nodes");
    if(owner.character.active() && target.character.active())throw DecodeError("Inactive retained release requires its genuine live callback context");
    auto& held=target.character.effects_;
    const auto bind=std::find_if(held.begin(),held.end(),[](const auto& e){return e.code()==0x3a;});
    if(bind!=held.end()) {
        if(bind->payload[4])target.extension[6]=0;
        if(ai==ti && static_cast<std::size_t>(bind-held.begin())<index)--index;
        held.erase(bind);
    }
    owner.character.effects_.at(index).payload[4]=0;remove(state,ai,index,false);
}
RetainedAbilityResult RetainedAbilityService::stage(const DeploymentState& state,CombatActorIdentity id,
        std::uint8_t code,const ItemCatalog& items,const CombatAttackContext& context,std::mt19937& random) const {
    if(!loaded_)throw DecodeError("Retained ability source services are uninitialized");
    if(code!=0x4a && code!=0x4b)throw DecodeError("Unknown retained-victim ability");
    RetainedAbilityResult result;result.effect_code=code;result.magic.state=state;
    auto rng=random;auto& candidate=result.magic.state;const auto ai=find(candidate,id);
    auto& owner=candidate.actors[ai];
    if(!owner.character.effects_restored())throw DecodeError("Retained ability requires owned SPC nodes");
    const auto at=std::find_if(owner.character.effects_.begin(),owner.character.effects_.end(),[&](const auto& e){return e.code()==code;});
    if(at==owner.character.effects_.end())return result;
    const auto index=static_cast<std::size_t>(at-owner.character.effects_.begin());
    const auto original=*at;const auto ti=victim(candidate,original.payload[3]);const auto target_id=candidate.actors[ti].id;
    if(!candidate.actors[ti].character.effects_restored())throw DecodeError("Retained ability victim requires owned SPC nodes");
    if(!owner.character.active() || !candidate.actors[ti].character.active()) {
        // Live inactive branch first releases3A, then zeros flag4 before the
        // exact unlink; it must not call the reversal a second time.
        release_inactive_in_place(candidate,id,code);
        return result;
    }
    if(code==0x4a) {
        owner.character.set_byte(0x113,1);owner.character.set_byte(0x114,0);
        owner.character.set_byte(0x115,3);owner.character.set_byte(0x117,4);
        auto attack=attacks_.stage(candidate,id,target_id,items,context,rng,CombatAttackKind::retained_crush);
        candidate=std::move(attack.state);result.strikes=std::move(attack.strikes);
        for(std::size_t i=0;i<result.strikes.size();++i)result.events.push_back({RetainedAbilityEventKind::strike,i});
        quit(candidate.actors[ai]);
        if(!candidate.actors[ti].character.active()) {
            if(index>=candidate.actors[ai].character.effects_.size() || candidate.actors[ai].character.effects_[index]!=original)
                throw DecodeError("Retained crushing exact node changed during its nested attack");
            remove(candidate,ai,index,true);
        }
    } else {
        //1344 does not initialize ambient6817.69/6A can observe a retained
        //prior effect code even though the outer cast executor reset681B.
        //The owned runtime has no authoritative ambient6817 binding yet.
        if(std::any_of(candidate.actors[ti].character.effects().begin(),candidate.actors[ti].character.effects().end(),
            [](const auto& effect){return effect.code()==0x69 || effect.code()==0x6a;}))
            throw DecodeError("Retained blood drain requires the native ambient pending-effect context for resistance69/6A");
        const auto before=std::make_shared<const DeploymentState>(candidate);
        result.magic.feedback.push_back({id,pascal_string_id("GAME.OVR",0x103b9),false});
        result.magic.events.push_back({CombatMagicEventKind::feedback,0,before,{}});
        result.events.push_back({RetainedAbilityEventKind::magic,0});
        const auto damage_roll=roll4(rng);
        CombatMagicContext damage_context;damage_context.caster=id;
        const auto damage_before=std::make_shared<const DeploymentState>(candidate);
        auto damage=effects_.damage(candidate,target_id,damage_roll,0,false,0,damage_context,rng);
        candidate=std::move(damage.state);
        // A phase6 notice is ordered before the damage notice it modifies.
        for(const auto& notice:damage.notices) {
            if(notice.member>=candidate.actors.size())throw DecodeError("Retained drain callback notice has stale ownership");
            result.magic.feedback.push_back({candidate.actors[notice.member].id,notice.text_id,false});
            result.magic.events.push_back({CombatMagicEventKind::feedback,result.magic.feedback.size()-1,damage_before,{}});
            result.events.push_back({RetainedAbilityEventKind::magic,result.magic.events.size()-1});
        }
        result.magic.damage.push_back({id,target_id,damage.outcome.amount,damage.outcome.before_hp,
            damage.outcome.after_hp,damage.outcome.status,false,damage.outcome.interrupted_spell});
        if(damage.outcome.amount) {
            result.magic.events.push_back({CombatMagicEventKind::damage,0,damage_before,std::make_shared<const DeploymentState>(candidate)});
            result.events.push_back({RetainedAbilityEventKind::magic,result.magic.events.size()-1});
        }
        quit(candidate.actors[ai]);
        if(index>=candidate.actors[ai].character.effects_.size() || candidate.actors[ai].character.effects_[index]!=original)
            throw DecodeError("Retained drain exact node changed during its nested damage callback");
        if(!candidate.actors[ti].character.active() || (original.word_1()>>4)<=damage_roll)remove(candidate,ai,index,true);
        else candidate.actors[ai].character.effects_[index].set_duration(static_cast<std::uint16_t>(original.word_1()-damage_roll*16));
    }
    random=std::move(rng);return result;
}
}
