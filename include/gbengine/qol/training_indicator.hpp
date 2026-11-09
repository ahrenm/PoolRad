#pragma once
#include "gbengine/party/training_service.hpp"
namespace GBEngine {
struct TrainingIndicatorColours {unsigned name,armour_class;};
// Screen-only QoL: source eligibility is queried without preparing/clipping XP.
// Status colours own the name ahead of training; white AC retains selection.
inline TrainingIndicatorColours training_indicator_colours(const TrainingService& rules,
        const Character& member,bool selected,unsigned native_name_colour) {
    unsigned name=native_name_colour;
    if(!member.active())name=12;
    else if(member.byte(0x10e)==1)name=14;
    else if(member.status()==0 && rules.eligible_classes(member))name=9; // EGA light blue.
    return {name,selected?15u:10u};
}
}
