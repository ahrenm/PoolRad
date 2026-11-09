#include "gbengine/time/rest_service.hpp"
#include "gbengine/magic/magic_effect_service.hpp"
#include "gbengine/magic/noncombat_damage_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
RestTickResult failed(CampWorkStatus status,std::string message) {
    RestTickResult result;result.status=status;result.message=std::move(message);return result;
}
std::uint8_t d100(std::mt19937& random) {
    // Same rejection sampling used by the VM and magic services; one native
    // d100 operation need not consume exactly one underlying host RNG word.
    constexpr auto threshold=(std::uint32_t{0}-100u)%100u;
    std::uint32_t value;
    do {value=static_cast<std::uint32_t>(random());} while(value<threshold);
    return static_cast<std::uint8_t>(value%100+1);
}
}
CampWorkPreparation RestService::prepare(Party& party,const SpellCatalog& catalog) {
    return CampWorkService::prepare_rest(party,catalog);
}
CampWorkResult RestService::start(RestState& state,std::uint32_t minutes) {
    if(state.running || state.awaiting_presentation)
        return {CampWorkStatus::rejected,"Rest is already active or awaiting presentation"};
    if(minutes>maximum_minutes)
        return {CampWorkStatus::rejected,"Rest duration exceeds native 99-day limit"};
    RestState staged;staged.remaining_minutes=minutes;staged.running=minutes!=0;staged.effects.fill(1);
    state=std::move(staged);return {};
}
RestTickResult RestService::tick(RestCampState& camp,RestState& state,GameClock& clock,Party& party,
    const SpellCatalog& catalog,std::mt19937& random,std::uint16_t interval,std::uint16_t chance,
    IEffectExpiryResolver* resolver) {
    if(state.awaiting_presentation)
        return failed(CampWorkStatus::rejected,"Rest feedback must be presented before another tick");
    if(!state.running || !state.remaining_minutes)
        return failed(CampWorkStatus::rejected,"Rest is not running");
    if(!party.size() || party.size()>8)
        return failed(CampWorkStatus::unresolved,"Rest requires a restored native roster");
    if(camp.daily_ticks>=288 || state.work.hourly_ticks>=12)
        return failed(CampWorkStatus::rejected,"Invalid resident rest counter");
    auto next_camp=camp;auto next_state=state;auto next_clock=clock;auto next_party=party;auto next_random=random;
    RestTickResult result;
    try {
        next_state.remaining_minutes-=std::min(next_state.remaining_minutes,5u);
        MagicEffectService native_expiry(catalog,&next_party.item_catalog(),&next_random,2);
        const auto time=GameTimeService::advance_rest(next_clock,next_party,{TimeUnit::minute,5},next_state.effects,resolver?resolver:&native_expiry);
        result.effect_notices=time.effects.notices;
        if(++next_camp.daily_ticks==288) {
            bool changed=false;
            for(std::size_t i=0;i<next_party.size();++i) {
                auto& member=next_party.members_[i];
                const auto status=member.status();
                if(status!=0 && status!=1 && status!=4 && status!=5) continue;
                if(std::any_of(member.effects().begin(),member.effects().end(),[](const CharacterEffect& effect){return effect.code()==0x32;})) continue;
                // Native force0 adds even at maximum, wrapping the byte before
                // capping. Do not replace this with a saturated increment.
                const auto active=member.active();
                (void)NoncombatRecoveryService::heal(member,1,false,2);changed=true;
                if(!active && member.active())result.effect_notices.push_back({i,pascal_string_id("GAME.OVR",member.byte(0x10e)==1?182066:182086)});
                result.recovered_members.push_back(i);
            }
            if(changed) ++next_party.revision_;
            next_camp.daily_ticks=0;result.daily_recovery=true;
        }
        auto work=CampWorkService::tick_work(next_party,catalog,next_state.work);
        if(work.status!=CampWorkStatus::applied) return failed(work.status,std::move(work.message));
        result.work_notices=std::move(work.notices);
        if(interval) {
            next_camp.encounter_ticks=static_cast<std::uint16_t>(next_camp.encounter_ticks+1);
            if(next_camp.encounter_ticks>=interval) {
                next_camp.encounter_ticks=0;result.encounter_roll=d100(next_random);
                if(*result.encounter_roll<=chance) result.outcome=RestOutcome::interrupted;
            }
        }
        if(result.outcome==RestOutcome::interrupted || !next_state.remaining_minutes) {
            next_state.running=false;
            if(result.outcome!=RestOutcome::interrupted) result.outcome=RestOutcome::completed;
        }
        next_state.awaiting_presentation=result.daily_recovery || !result.work_notices.empty() || !result.effect_notices.empty() || result.outcome==RestOutcome::interrupted;
    } catch(const DecodeError& error) {return failed(CampWorkStatus::unresolved,error.what());}
    camp=std::move(next_camp);state=std::move(next_state);clock=std::move(next_clock);party=std::move(next_party);random=std::move(next_random);
    return result;
}
}
