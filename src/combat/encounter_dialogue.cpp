#include "gbengine/combat/encounter_dialogue.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
void validate_distance(std::uint16_t distance) {
    if(distance>2) throw DecodeError("Encounter dialogue presentation distance exceeds native range");
}
}
EncounterPace EncounterDialogueService::pace(std::span<const Character* const> participants) {
    if(participants.empty()||!participants.front())
        throw DecodeError("Encounter pace requires a native participant head");
    const auto seed=participants.front()->byte(0x11c);
    EncounterPace out{seed,seed};
    for(const auto* participant:participants) {
        if(!participant||!participant->effects_restored())
            throw DecodeError("Encounter pace requires restored participant effect graphs");
        auto speed=participant->byte(0x11c);
        const auto has=[&](std::uint8_t code) {
            return std::ranges::any_of(participant->effects(),[&](const auto& effect){return effect.code()==code;});
        };
        if(has(0x27)) speed=static_cast<std::uint8_t>(speed<<1);
        else if(has(0x2a)) speed=static_cast<std::uint8_t>(speed>>1);
        out.minimum=std::min(out.minimum,speed);
        out.maximum=std::max(out.maximum,speed);
    }
    return out;
}
std::array<EncounterChoice,4> EncounterDialogueService::choices(std::uint16_t distance,bool interior) {
    validate_distance(distance);
    return {EncounterChoice::combat,EncounterChoice::wait,EncounterChoice::flee,
            interior&&distance!=0?EncounterChoice::advance:EncounterChoice::parlay};
}
std::size_t EncounterDialogueService::text_index(const EncounterDialogueConfig& config,std::uint16_t distance) {
    validate_distance(distance);
    for(std::size_t count=0;count<config.texts.size();++count) {
        const auto index=(static_cast<std::size_t>(distance)+count)%config.texts.size();
        if(!config.texts[index].empty()) return index;
    }
    return distance;
}
EncounterDialogueStep EncounterDialogueService::choose(const EncounterDialogueConfig& config,
    EncounterPace pace,std::uint16_t distance,EncounterChoice choice) {
    validate_distance(distance);
    const auto index=static_cast<std::size_t>(choice);
    if(index>=config.policies.size()) throw DecodeError("Invalid encounter dialogue choice");
    EncounterDialogueStep out;out.distance=distance;
    const auto wait=[&] {out.repeat=true;out.notice=EncounterNotice::both_sides_wait;};
    const auto approach=[&] {--out.distance;out.approach=true;out.repeat=true;};
    switch(config.policies[index]) {
        case 0:
            out.result=static_cast<std::uint16_t>(choice==EncounterChoice::flee&&pace.minimum>=config.flee_threshold?2:1);
            break;
        case 1:case 3:case 4: {
            const auto policy=config.policies[index];
            if(choice==EncounterChoice::combat) out.result=std::uint16_t{1};
            else if(choice==EncounterChoice::flee) out.result=std::uint16_t{2};
            else if(choice==EncounterChoice::wait&&policy==1) wait();
            else if(distance!=0) approach();
            else if(choice==EncounterChoice::parlay||policy==4) out.result=std::uint16_t{3};
            else wait();
            break;
        }
        case 2:
            if(choice==EncounterChoice::combat&&pace.maximum>=config.monster_flee_threshold) out.result=std::uint16_t{1};
            else {out.result=std::uint16_t{0};out.notice=EncounterNotice::monsters_flee;}
            break;
        default:
            // The native handler returns without touching its destination.
            break;
    }
    return out;
}
}
