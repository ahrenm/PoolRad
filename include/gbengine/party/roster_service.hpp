#pragma once
#include "gbengine/party/party.hpp"
namespace GBEngine {
enum class RosterStatus {applied,rejected,quit_required};
enum class RosterDirection {earlier,later};
struct RosterResult {
    RosterStatus status=RosterStatus::rejected;
    std::string message;
    bool applied() const {return status==RosterStatus::applied;}
};
// Moves complete owned characters. Item/effect identities and presentation
// ordinals belong to the member, never to its current position in the roster.
class RosterService {
public:
    static RosterResult move(Party&,RosterDirection);
    static RosterResult drop(Party&,bool confirmed);
};
}
