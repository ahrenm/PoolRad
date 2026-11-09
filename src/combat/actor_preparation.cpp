#include "gbengine/combat/actor_preparation.hpp"
#include "gbengine/party/equipment.hpp"
#include "gbengine/combat/combat_magic_effects.hpp"
#include "gbengine/combat/round_service.hpp"
#include "gbengine/combat/opponent_flight_service.hpp"
#include "gbengine/combat/cloud_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
bool has(const Character& character,std::uint8_t code) {
    return std::any_of(character.effects().begin(),character.effects().end(),
                       [code](const auto& effect){return effect.code()==code;});
}
void graph(const Character& character) {
    if(!character.effects_restored())throw DecodeError("Combat actor effect graph is not restored");
}
bool positive(std::uint8_t value) {return value>0 && value<128;}
bool fear_queries(const Character& recipient,std::uint8_t incoming,std::mt19937& random) {
    // 49D5:02FA phase9, bounded incoming34/02.69/6A have been rejected
    // because their inherited level is unavailable.6E/70 modify only6819;
    // neither it nor their elemental result is consumed by this application.
    for(const auto code:{0x6bu,0x6cu,0x6du,0x6eu,0x6fu,0x70u,0x7cu,0x7du})
        if(has(recipient,static_cast<std::uint8_t>(code))) {
            if(code==0x6b || code==0x7c) {
                constexpr auto threshold=(std::uint32_t{0}-100u)%100u;
                std::uint32_t draw;do{draw=static_cast<std::uint32_t>(random());}while(draw<threshold);
                // Both handlers draw even though incoming34/02 never matches
                // their0B/35 cancellation predicate.
            } else if((code==0x6d || code==0x6f || code==0x7d) && incoming==0x34)incoming=0;
        }
    return incoming!=0;
}
void stop(DeploymentActor& actor) {
    // 10BA:2637 shared phase7 handler for33/34/35/1F.
    for(const auto at:{0u,3u,7u,6u})actor.extension[at]=0;
}
}
void CombatActorPreparation::entry_fear(DeploymentState& state,std::size_t owner,std::optional<CombatActorIdentity> selected,std::mt19937* random,std::vector<CombatSelectionStep>* playback) {
    if(!selected || !random)throw DecodeError("Combat entry effect51 requires the original selected actor and staged RNG");
    // Failed supplementary placement can change global5D92 during2C86:19F5.
    // This binding proves its preservation only for the all-placed profile.
    for(const auto& actor:state.actors)if(actor.removed && actor.id.origin==CombatActorOrigin::encounter)
        throw DecodeError("Combat entry effect51 selection after supplementary removal is unresolved");
    std::vector<Character> roster;std::vector<std::size_t> indices;std::optional<std::size_t> source;
    for(std::size_t i=0;i<state.actors.size();++i)if(!state.actors[i].removed) {
        if(state.actors[i].id==*selected)source=roster.size();
        indices.push_back(i);roster.push_back(state.actors[i].character);
    }
    if(!source || selected->origin!=CombatActorOrigin::party)
        throw DecodeError("Combat entry effect51 source selection is not retained");
    const auto side=state.actors[owner].character.byte(0x10e)==0?1u:0u;
    for(const auto& recipient:roster)if(recipient.byte(0x10d) && recipient.byte(0x10e)==side) {
        const auto level=recipient.byte(0x73);
        if(level<=5) {
            if(!playback)throw DecodeError("Combat entry effect51 low-level fear requires owned presentation");
            // Phase9's69/6A consume inherited681C, not the dragon's level.
            if(has(recipient,0x69) || has(recipient,0x6a))
                throw DecodeError("Combat entry effect51 phase9 requires inherited level scratch");
        }

    }
    // User-approved fear saves are non-elemental: no inherited fire/cold bonus.
    // Phase8 precedes resident49F3=5. The default save adapter deliberately
    // uses own-first/unrestricted linked-roster aura lookup, without tactical LOS.
    for(std::size_t i=0;i<roster.size();++i)if(roster[i].byte(0x10d) && roster[i].byte(0x10e)==side) {
        const auto saved=SavingThrowService::resolve(roster,i,roster[*source],4,0,*random,0).saved;
        const auto level=roster[i].byte(0x73);
        if(level>5)continue; // signed negative also follows the native no-application branch.
        auto& recipient=roster[i];const auto code=static_cast<std::uint8_t>(level<=3?0x34:2);
        // All phase9 callbacks execute even after a successful save/cancellation.
        const bool accepted=fear_queries(recipient,code,*random);
        auto text=pascal_string_id("GAME.OVR",0x2c532);
        if(accepted && !saved) {
            auto& effects=recipient.effects_;
            const auto old=std::find_if(effects.begin(),effects.end(),[&](const auto& effect){return effect.code()==code;});
            if(old!=effects.end()) {
                // New duration0 replaces only the first matching node. A
                // reversal flag invokes a distinct callback before unlinking.
                if(old->payload[4])throw DecodeError("Combat entry fear replacement requires a bound removal callback");
                effects.erase(old);
            }
            if(effects.size()>=65535)throw DecodeError("Combat entry fear effect count exceeds native bound");
            CharacterEffect effect;effect.payload={code,0,0,12,0};
            effect.source_file="GAME.OVR:2EE2:1AAA";effect.source_offset=60507+0x1aaa;
            effects.push_back(std::move(effect));
            text=pascal_string_id("GAME.OVR",level<=3?0x106da:0x106f1);
        }
        // Entry phase8 precedes resident49F3=5: native1FEB is plain16EC.
        for(std::size_t n=0;n<roster.size();++n)state.actors[indices[n]].character=roster[n];
        playback->push_back({CombatRoundNotice{state.actors[indices[i]].id,text},{},std::make_shared<const DeploymentState>(state)});
    }
    for(std::size_t i=0;i<roster.size();++i)state.actors[indices[i]].character=std::move(roster[i]);
}
void CombatActorPreparation::initial(DeploymentState& state,const ItemCatalog& catalog) {
    auto staged=state;initial_in_place(staged,catalog,nullptr,{},nullptr,nullptr);state=std::move(staged);
}
void CombatActorPreparation::entry_mummy_fear(DeploymentState& state,std::size_t owner,
        std::optional<CombatActorIdentity> selected,std::mt19937* random,std::vector<CombatSelectionStep>* playback) {
    if(!selected || !random || !playback)
        throw DecodeError("Combat entry effect52 requires retained selection, staged RNG and presentation");
    for(const auto& actor:state.actors)if(actor.removed && actor.id.origin==CombatActorOrigin::encounter)
        throw DecodeError("Combat entry effect52 selection after supplementary removal is unresolved");
    const auto side=state.actors[owner].character.byte(0x10e)==0?1u:0u;
    std::vector<Character> roster;std::vector<std::size_t> indices;std::optional<std::size_t> source;
    std::array<unsigned,2> counts{};
    for(std::size_t i=0;i<state.actors.size();++i)if(!state.actors[i].removed) {
        if(state.actors[i].id==*selected)source=roster.size();
        indices.push_back(i);roster.push_back(state.actors[i].character);
        if(roster.back().active()) {
            const auto team=roster.back().byte(0x10e);
            if(team>1)throw DecodeError("Combat entry effect52 has an invalid side");
            ++counts[team];
        }
    }
    if(!source || selected->origin!=CombatActorOrigin::party)
        throw DecodeError("Combat entry effect52 source selection is not retained");
    //2EE2:1BA8 removes the first52 on EVERY retained actor before its active/
    //opposite-side test. Ordinary multiple mummies consequently trigger once.
    //A flagged removal recursively dispatches52 and needs separate ownership.
    for(const auto& c:roster) {
        const auto node=std::find_if(c.effects_.begin(),c.effects_.end(),[](const auto& e){return e.code()==0x52;});
        if(node!=c.effects_.end() && node->payload[4])
            throw DecodeError("Combat entry effect52 flagged removal requires recursive callback ownership");
        if(c.active() && c.byte(0x10e)==side) {
            if(has(c,0x69) || has(c,0x6a))
                throw DecodeError("Combat entry effect52 phase9 requires inherited level scratch");

        }
    }
    const auto publish=[&]{for(std::size_t n=0;n<roster.size();++n)state.actors[indices[n]].character=roster[n];};
    for(std::size_t i=0;i<roster.size();++i) {
        auto& recipient=roster[i];auto& nodes=recipient.effects_;
        const auto first=std::find_if(nodes.begin(),nodes.end(),[](const auto& e){return e.code()==0x52;});
        if(first!=nodes.end())nodes.erase(first);
        if(!recipient.active() || recipient.byte(0x10e)!=side)continue;
        //Native6814 counts active retained actors. Race/class raw2E==7 adds2.
        const auto bonus=static_cast<std::int8_t>((counts[side]>=6?1:0)+(recipient.byte(0x2e)==7?2:0));
        // Same approved non-elemental policy as dragon fear; ordinary poison
        // protections and native count/race bonuses still apply.
        const auto saved=SavingThrowService::resolve(roster,i,roster[*source],0,bonus,*random,0).saved;
        //Duration is always rolled AFTER save, including successful saves.
        const auto duration=static_cast<std::uint8_t>((*random)()%4+1);
        const auto accepted=fear_queries(recipient,0x34,*random);
        auto text=pascal_string_id("GAME.OVR",0x2c532);
        if(accepted && !saved) {
            const auto old=std::find_if(nodes.begin(),nodes.end(),[](const auto& e){return e.code()==0x34;});
            if(old!=nodes.end() && old->word_1()!=0 && old->word_1()<duration) {
                if(old->payload[4])throw DecodeError("Combat entry effect52 replacement requires a bound removal callback");
                nodes.erase(old);
            }
            //1656 may remove a shorter first node, then0E52 ALWAYS appends,
            //including when an equal/longer/indefinite node remains.
            if(nodes.size()>=65535)throw DecodeError("Combat entry fear effect count exceeds native bound");
            CharacterEffect e;e.payload={0x34,duration,0,12,0};e.source_file="GAME.OVR:2EE2:1BA8";
            e.source_offset=60507+0x1ba8;nodes.push_back(std::move(e));
            text=pascal_string_id("GAME.OVR",60507+0x1b91);
        }
        publish();playback->push_back({CombatRoundNotice{state.actors[indices[i]].id,text},{},std::make_shared<const DeploymentState>(state)});
    }
    publish();
}
void CombatActorPreparation::initial(DeploymentState& state,const ItemCatalog& catalog,std::mt19937& random,std::optional<CombatActorIdentity> selected,std::vector<CombatSelectionStep>* playback,const SpellCatalog* spells) {
    auto staged=state;auto rng=random;auto steps=playback?*playback:std::vector<CombatSelectionStep>{};
    initial_in_place(staged,catalog,&rng,selected,playback?&steps:nullptr,spells);state=std::move(staged);random=std::move(rng);if(playback)*playback=std::move(steps);
}
void CombatActorPreparation::initial_in_place(DeploymentState& state,const ItemCatalog& catalog,std::mt19937* random,std::optional<CombatActorIdentity> selected,std::vector<CombatSelectionStep>* playback,const SpellCatalog* spells) {
    // Native scalar refresh precedes extension allocation and placement. The
    // owned spatial snapshot retains removed supplementary actors for audit;
    // they were refreshed too, but no longer participate in phase8 traversal.
    for(std::size_t actor_index=0;actor_index<state.actors.size();++actor_index) {
        auto& actor=state.actors[actor_index];
        graph(actor.character);EquipmentRules::recalculate(actor.character,catalog);
    }
    for(std::size_t actor_index=0;actor_index<state.actors.size();++actor_index) {
        auto& actor=state.actors[actor_index];
        if(actor.removed)continue;
        auto& character=actor.character;
        // 49D5:02FA selects these exact codes, first matching node per code.
        // Missing code is a proven no-call, not a generic unknown-effect skip.
        const auto recovery=std::find_if(character.effects_.begin(),character.effects_.end(),[](const auto& e){return e.code()==0x63;});
        if(recovery!=character.effects_.end()) {
            const auto hp=character.status()==4?6u:character.status()==5 && actor.extension[0xe]<6?6u-actor.extension[0xe]:0u;
            if(hp) {
                if(!spells || !random || !playback)throw DecodeError("Combat entry recovery requires staged placement, RNG and presentation");
                const auto index=static_cast<std::size_t>(recovery-character.effects_.begin());
                if(CombatMagicEffects::revive_in_place(state,actor.id,static_cast<std::uint8_t>(hp),*spells)) {
                    if(character.effects_.size()>=65535)throw DecodeError("Combat entry recovery effect capacity exceeded");
                    std::uint32_t draw;do{draw=static_cast<std::uint32_t>((*random)());}while(draw<(std::uint32_t{0}-4u)%4u);
                    CharacterEffect node;node.payload={0x5f,static_cast<std::uint8_t>(draw%4+2),0,255,1};
                    node.source_file="GAME.OVR:2EE2:2730";node.source_offset=60507+0x2730;
                    character.effects_.push_back(std::move(node));character.effects_.erase(character.effects_.begin()+static_cast<std::ptrdiff_t>(index));
                    playback->push_back({CombatRoundNotice{actor.id,pascal_string_id("GAME.OVR",character.byte(0x10e)==1?182066:182086)},{},std::make_shared<const DeploymentState>(state)});
                }
            }
        }
        if(has(character,0x51))entry_fear(state,actor_index,selected,random,playback);
        if(has(character,0x52))entry_mummy_fear(state,actor_index,selected,random,playback);
        const auto marker=std::find_if(character.effects_.begin(),character.effects_.end(),
                                       [](const auto& effect){return effect.code()==0x59;});
        // 2EE2:2490: entry resets resident round6DE3 and scratch6822 to zero.
        if(marker!=character.effects_.end())marker->payload[3]&=0x0f;
        for(const auto code:{0x48u,0x38u})if(has(character,static_cast<std::uint8_t>(code))) {
            if(!random)throw DecodeError("Combat entry invisibility requires bound staged RNG");
            CombatMagicEffects::install_invisibility_in_place(character,static_cast<std::uint8_t>(code),*random);
        }
    }
}
void CombatActorPreparation::actor_turn(DeploymentState& state,std::size_t index,const ItemCatalog& catalog) {
    actor_turn(state,index,catalog,nullptr);
}
void CombatActorPreparation::actor_turn(DeploymentState& state,std::size_t index,const ItemCatalog& catalog,std::vector<CombatRoundNotice>* notices) {
    auto staged=state;auto feedback=notices?*notices:std::vector<CombatRoundNotice>{};
    actor_turn_in_place(staged,index,catalog,notices?&feedback:nullptr,nullptr,nullptr,nullptr);state=std::move(staged);
    if(notices)*notices=std::move(feedback);
}
void CombatActorPreparation::actor_turn(DeploymentState& state,std::size_t index,const ItemCatalog& catalog,
        std::vector<CombatRoundNotice>* notices,const CombatRoundContext* context,std::mt19937& random,
        std::vector<CombatSelectionStep>& playback) {
    auto staged=state;auto feedback=notices?*notices:std::vector<CombatRoundNotice>{};auto steps=playback;auto rng=random;
    actor_turn_in_place(staged,index,catalog,notices?&feedback:nullptr,context,&rng,&steps);
    state=std::move(staged);random=std::move(rng);playback=std::move(steps);if(notices)*notices=std::move(feedback);
}
void CombatActorPreparation::actor_turn_in_place(DeploymentState& state,std::size_t index,const ItemCatalog& catalog,
        std::vector<CombatRoundNotice>* notices,const CombatRoundContext* context,std::mt19937* random,
        std::vector<CombatSelectionStep>* playback) {
    if(index>=state.actors.size() || state.actors[index].removed)
        throw DecodeError("Combat actor-selection cache requires a retained actor");
    auto& actor=state.actors[index];auto& character=actor.character;graph(character);
    actor.extension[0x0f]=0;actor.extension[0x12]=0;actor.extension[7]=0;
    for(const auto code:{0x33u,0x34u,0x35u,0x1fu})
        if(has(character,static_cast<std::uint8_t>(code)))stop(actor);
    if(!positive(actor.extension[3]))return;
    if(actor.extension[3]==20)actor.extension[3]=19;
    EquipmentRules::recalculate(character,catalog);
    // Source15 is Silence capability suppression, including the native
    // own-first/visible range1 aura path; it does not mark outward flight.
    const auto notice_start=notices?notices->size():0;
    OpponentFlightService::actor_phase15(state,index,notices);
    if(has(character,0x1e)) {
        if(actor.extension[2] && !notices)throw DecodeError("Combat cloud capability notice requires a staged presentation binding");
        if(CombatCloudService::nausea_in_place(actor,catalog)) {
            notices->push_back({actor.id,pascal_string_id("GAME.OVR",0xf6b4)});
        }
    }
    if(playback && notices)for(auto n=notice_start;n<notices->size();++n)
        playback->push_back({(*notices)[n],{},std::make_shared<const DeploymentState>(state)});
    // A retained action may replace the entire state. Reacquire actor and
    // character by index after each callback instead of retaining references.
    for(const auto code:{std::uint8_t{0x4a},std::uint8_t{0x4b}})
        if(has(state.actors[index].character,code)) {
            if(!context || !context->retained || !random || !playback)
                throw DecodeError("Combat phase15 retained ability requires bound selection RNG and presentation context");
            auto result=std::make_shared<RetainedAbilityResult>(context->retained->stage(state,state.actors[index].id,code,catalog,context->attacks,*random));
            state=result->magic.state;
            if(!result->events.empty())playback->push_back({{},std::move(result),{}});
        }
    if(has(state.actors[index].character,0x0b))(void)CombatMagicEffects::apply_charm_in_place(state,state.actors[index].id);
}
}
