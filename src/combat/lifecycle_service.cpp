#include "gbengine/combat/lifecycle_service.hpp"
#include "gbengine/time/game_time_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/combat/effect_expiry.hpp"
#include "gbengine/combat/cloud_service.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
bool has(const Character& character,std::uint8_t code) {
    return std::any_of(character.effects().begin(),character.effects().end(),
                       [code](const auto& effect){return effect.code()==code;});
}
void phase19(DeploymentState& state,DeploymentActor& actor,CombatLifecycleContext context,std::mt19937* random,
             ItemId* next_item_id,std::vector<CombatRoundNotice>& notices) {
    //49D502FA dispatches one first matching node for each of these codes in
    //this exact order.62 is independent of node context and character status.
    auto& character=actor.character;
    if(has(character,0x62)) {
        const auto sum=static_cast<std::uint8_t>(character.hit_points()+3);
        character.set_byte(0x11b,std::min(sum,character.byte(0x32)));
    }
    for(const auto code:std::array<std::uint8_t,4>{0x17,0x48,0x38,0x0b})if(has(character,code)) {
        if(code==0x17) {
            if(!context.items || !next_item_id)throw DecodeError("Combat phase19 hammer requires an owned item allocator");
            if(CombatMagicEffects::maintain_hammer_in_place(state,actor.id,*next_item_id,*context.items))
                notices.push_back({actor.id,pascal_string_id("GAME.OVR",0xf42d)});
        }else if(code==0x38 || code==0x48) {
            if(!random)throw DecodeError("Combat phase19 invisibility requires bound staged RNG");
            CombatMagicEffects::install_invisibility_in_place(character,code,*random);
        }else if(code==0x0b) {
            (void)CombatMagicEffects::apply_charm_in_place(state,actor.id);
        }
    }
}
}
CombatRoundEndResult CombatLifecycleService::end_round(const DeploymentState& state,const CombatRoundState& round,
                                                      const GameClock& clock,const CombatSpatialService& spatial,
                                                      CombatLifecycleContext context) {
    if(round.phase!=CombatRoundPhase::round_end_pending || round.active)
        throw DecodeError("Combat round end requires every selected actor turn to finish");
    if(state.actors.empty() || state.actors.size()>71)
        throw DecodeError("Combat round end requires a bounded tactical roster");
    CombatRoundEndResult result;
    result.deployment=std::make_unique<DeploymentState>(state);result.round=round;result.clock=clock;
    auto& next=*result.deployment;
    // This temporary roster is an adapter for the existing native countdown
    // algorithm. It contains tactical copies from BOTH sides, including the
    // inactive, in exact native traversal order. It is never durable Party.
    Party tactical;
    std::vector<std::size_t> indices;
    for(std::size_t i=0;i<next.actors.size();++i) {
        const auto& actor=next.actors[i];if(actor.removed)continue;
        if(actor.extension[3]>0 && actor.extension[3]<128)
            throw DecodeError("Combat round end cannot skip a retained actor initiative");
        if(actor.character.byte(0x10e)>1)throw DecodeError("Combat round end has an unsupported actor side");
        tactical.members_.push_back(actor.character);indices.push_back(i);
    }
    std::optional<std::mt19937> staged_random;
    if(context.random)staged_random=*context.random;
    std::optional<ItemId> staged_item;
    if(context.next_item_id)staged_item=*context.next_item_id;
    std::optional<CombatEffectExpiry> expiry;
    if(context.items && context.spells && staged_random)expiry.emplace(next,indices,*context.items,*context.spells,*staged_random,round.effect_caster);
    const auto advanced=GameTimeService::advance(result.clock,tactical,{TimeUnit::minute,1},expiry?&*expiry:nullptr);
    result.age_increments=advanced.age_increments;result.effect_ticks=advanced.effect_ticks;
    result.effect_decrements=advanced.effects.decrements;result.expired_effects=advanced.effects.expired;
    for(std::size_t i=0;i<indices.size();++i)next.actors[indices[i]].character=std::move(tactical.members_[i]);
    result.round.round_index=static_cast<std::uint8_t>(round.round_index+1);
    result.round.notices.clear();
    for(const auto& notice:advanced.effects.notices) {
        if(notice.member>=indices.size())throw DecodeError("Combat expiry notice lost its tactical actor");
        result.notices.push_back({next.actors[indices[notice.member]].id,notice.text_id});
    }
    //326D2879 runs BEFORE phase19: active enemy current HP / ALL enemy max.
    std::uint16_t current=0,maximum=0;
    for(const auto& actor:next.actors)if(!actor.removed && actor.character.byte(0x10e)==1) {
        if(actor.character.active())current=static_cast<std::uint16_t>(current+actor.character.hit_points());
        maximum=static_cast<std::uint16_t>(maximum+actor.character.byte(0x32));
    }
    result.opponent_hp_percent=context.previous_opponent_hp_percent;
    if(maximum)result.opponent_hp_percent=static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(current*20u)/maximum)*5u);
    for(std::size_t index=0;index<next.actors.size();++index) {
        if(next.actors[index].removed)continue;
        const auto identity=next.actors[index].id;
        phase19(next,next.actors[index],context,staged_random?&*staged_random:nullptr,staged_item?&*staged_item:nullptr,result.notices);
        const auto& candidate=next.actors[index];
        if(candidate.character.active() && !spatial.footprint(candidate).empty() &&
           spatial.probe(next,identity,8).terrain==0x1e &&
           !has(candidate.character,0x1e) && !has(candidate.character,0x1f) && !has(candidate.character,0x20) &&
           !has(candidate.character,0x6f) && !has(candidate.character,0x7d)) {
            if(!context.items || !context.spells || !staged_random || !round.effect_caster)
                throw DecodeError("Combat round-end cloud requires its retained selected context and staged services");
            const auto source=std::find_if(next.actors.begin(),next.actors.end(),[&](const auto& actor){return !actor.removed && actor.id==*round.effect_caster;});
            if(source==next.actors.end())throw DecodeError("Combat round-end cloud selected context is stale");
            // Dispatcher clears pending681B after the previous cast. Row0 is
            // school0, so10BA26C2 uses selected5D92's cleric96, not cloud owner.
            const CombatMagicContext magic{source->id,0,0,source->character.byte(0x96),0};
            const auto notices=CombatCloudService::contact_in_place(next,identity,magic,*context.spells,*context.items,*staged_random);
            for(const auto& notice:notices) {
                if(notice.member>=next.actors.size())throw DecodeError("Combat cloud notice actor is stale");
                result.notices.push_back({next.actors[notice.member].id,notice.text_id});
            }
        }
        // Contact saves may publish a replaced owned candidate; reacquire the
        // actor after that transaction before native bleeding/count work.
        auto& actor=next.actors[index];
        if(actor.character.status()==5) {
            actor.extension[0x0e]=static_cast<std::uint8_t>(actor.extension[0x0e]+1);
            if(actor.extension[0x0e]>9)actor.character.set_byte(0x10c,6);
        }
    }
    for(const auto& actor:next.actors) {
        if(actor.removed)continue;
        if(!actor.extension[0x13] && actor.character.byte(0x10e)==0 && actor.character.status()==5 && !result.bleeding_notice) {
            result.bleeding_notice=true;
            result.notices.push_back({actor.id,pascal_string_id("GAME.OVR",0xa05f),false});
        }
        if(actor.character.active())++result.active_sides[actor.character.byte(0x10e)];
    }
    result.round.notices=result.notices;
    if(result.active_sides[0] && result.active_sides[1]) {
        result.disposition=CombatRoundEndDisposition::next_round;
        result.round.phase=CombatRoundPhase::not_started;
    } else if(result.active_sides[0] && !result.active_sides[1] && !context.suppress_continue_question)
        result.disposition=CombatRoundEndDisposition::continue_question;
    if(context.random && staged_random)*context.random=std::move(*staged_random);
    if(context.next_item_id && staged_item)*context.next_item_id=*staged_item;
    return result;
}
}
