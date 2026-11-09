#include "gbengine/party/roster_service.hpp"
#include <algorithm>
#include <limits>
namespace GBEngine {
namespace {
bool valid(const Party& party) {
    return party.size()>0 && party.size()<=8 && party.selected_index() &&
        *party.selected_index()<party.size() && party.revision()!=std::numeric_limits<std::uint64_t>::max();
}
}
RosterResult RosterService::move(Party& party,RosterDirection direction) {
    if(!valid(party)) return {RosterStatus::rejected,"Party order requires a valid selected member"};
    if(direction!=RosterDirection::earlier && direction!=RosterDirection::later)
        return {RosterStatus::rejected,"Invalid party order direction"};
    if(party.size()==1) return {RosterStatus::applied,{}};
    auto staged=party;const auto from=*staged.selected_;
    const auto to=direction==RosterDirection::earlier?(from+staged.size()-1)%staged.size():(from+1)%staged.size();
    auto first=staged.members_.begin();
    if(from<to) std::rotate(first+static_cast<std::ptrdiff_t>(from),first+static_cast<std::ptrdiff_t>(from+1),first+static_cast<std::ptrdiff_t>(to+1));
    else std::rotate(first+static_cast<std::ptrdiff_t>(to),first+static_cast<std::ptrdiff_t>(from),first+static_cast<std::ptrdiff_t>(from+1));
    staged.selected_=to;++staged.revision_;party=std::move(staged);
    return {RosterStatus::applied,{}};
}
RosterResult RosterService::drop(Party& party,bool confirmed) {
    if(!valid(party)) return {RosterStatus::rejected,"Party drop requires a valid selected member"};
    if(!confirmed) return {RosterStatus::rejected,{}};
    if(party.size()==1) return {RosterStatus::quit_required,{}};
    auto staged=party;const auto from=*staged.selected_;
    staged.members_.erase(staged.members_.begin()+static_cast<std::ptrdiff_t>(from));
    staged.selected_=from?from-1:0;++staged.revision_;
    party=std::move(staged);return {RosterStatus::applied,{}};
}
}
