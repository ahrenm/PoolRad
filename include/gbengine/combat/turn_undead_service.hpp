#pragma once
#include "gbengine/combat/targeting_service.hpp"

namespace GBEngine {
enum class TurnUndeadOutcome { turned,destroyed };
struct TurnUndeadRecipient {
    CombatActorIdentity actor;
    TurnUndeadOutcome outcome=TurnUndeadOutcome::turned;
    std::int8_t threshold{};
};
struct TurnUndeadResult {
    DeploymentState state;
    std::vector<TurnUndeadRecipient> recipients;
    std::uint8_t count_roll{},check_roll{};
    bool failed=false;
};
class TurnUndeadService {
    CombatSpatialService spatial_;
    CombatTargetingService targeting_;
    std::array<std::array<std::int8_t,10>,12> thresholds_{};
    bool loaded_=false;
public:
    static TurnUndeadService load(const IFileSystem&);
    static bool available(const DeploymentActor&);
    // Native326D116A/1351. Owns the complete staged attempt, two ordered rolls,
    // once-per-combat flag, recipients, body changes and source stop fields.
    // A positive result sets extension10 only: the AI flight consumer owns
    // subsequent movement/removal and must not invent an immediate teleport.
    TurnUndeadResult stage(const DeploymentState&,CombatActorIdentity,std::mt19937&) const;
};
}
