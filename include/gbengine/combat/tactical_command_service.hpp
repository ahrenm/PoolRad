#pragma once
#include "gbengine/combat/round_service.hpp"

namespace GBEngine {
enum class TacticalCommand { guard,delay,quit,bandage,quick,quick_all,manual_control };
struct TacticalCommandAvailability {
    bool guard=false,delay=true,quit=true,bandage=false,quick=true,speed=true;
};
struct TacticalCommandContext {
    // The AI key poll has the additional status1 veto absent in the player
    // command loop. This affects control, never side/allegiance.
    bool automatic_interrupt=false;
};
struct TacticalCommandResult {
    DeploymentState state;
    std::vector<CombatRoundNotice> notices;
    bool resume_selection=false,begin_automatic=false,manual_active=false;
};
class TacticalCommandService {
public:
    static TacticalCommandAvailability available(const DeploymentState&,CombatActorIdentity,const ItemCatalog&);
    // Pure owned staging; the session publishes its token/revision/RNG barrier.
    // Resume performs normal source selection+turn entry, never a second Quit.
    static TacticalCommandResult stage(const DeploymentState&,CombatActorIdentity,TacticalCommand,
                                       const ItemCatalog&,TacticalCommandContext={});
};
}
