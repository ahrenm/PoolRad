#pragma once
#include "gbengine/combat/deployment.hpp"

namespace GBEngine {
struct CombatStepProbe {
    CombatPosition destination;
    std::uint8_t terrain=0x17,occupied_slot{},terrain_cost{},step_cost{};
};
enum class CombatStepStatus { moved, blocked, budget_exhausted, unresolved };
struct CombatStepResult {
    CombatStepStatus status=CombatStepStatus::unresolved;
    bool moved=false,movement_exhausted=false;
    // Only a complete supported resolution publishes a candidate. A blocked
    // ordinary step may still publish the native facing change.
    std::optional<DeploymentState> state;
    std::string message;
};
struct CombatSightResult {
    bool clear=false;
    std::uint8_t distance{}; // Native half-square metric, cardinal2/diagonal3.
    CombatPosition reached;
};
class CombatSpatialService {
    std::array<std::array<CombatPosition,4>,5> footprints_{};
    std::array<std::uint8_t,4> helpless_codes_{};
    bool loaded_=false;
    const DeploymentActor& actor(const DeploymentState&,CombatActorIdentity) const;
    bool helpless(const Character&) const;
    bool adjacent(const DeploymentState&,const DeploymentActor&,const DeploymentActor&) const;
    void validate(const DeploymentState&) const;
public:
    static CombatSpatialService load(const IFileSystem&);
    static CombatSpatialService from_native_module(std::span<const std::uint8_t>);
    std::vector<CombatPosition> footprint(const DeploymentActor&) const;
    // 4F3E:0CB9 probes destination footprints without sweeping orthogonal
    // corners. Self occupancy is ignored; the last foreign slot wins.
    CombatStepProbe probe(const DeploymentState&,CombatActorIdentity,unsigned direction) const;
    CombatStepResult stage(const DeploymentState&,CombatActorIdentity,unsigned direction) const;
    // Compositional primitive for TacticalMovementService only: caller has
    // staged departure attacks and must stage guard attacks after this step.
    CombatStepResult stage_after_departure(const DeploymentState&,CombatActorIdentity,unsigned direction,
                                           bool resolve_hazard=false) const;
    // 4E80:01A0/02CC metric and 0422 height interpolation are deliberately
    // separate from movement/occupancy. Native callers can disable terrain
    // occlusion with descriptor byte6; ordinary movement keeps it enabled.
    static std::uint8_t range(CombatPosition,CombatPosition);
    // Native DDA samples, including both endpoints. Ray construction can use
    // an endpoint outside the field; terrain probing owns the edge decision.
    static std::vector<CombatPosition> trace_line(CombatPosition,CombatPosition);
    static CombatSightResult sight(const Battlefield&,CombatPosition,CombatPosition,
                                   std::uint16_t maximum_range=255,bool ignore_terrain=false);
private:
    CombatStepResult stage_step(const DeploymentState&,CombatActorIdentity,unsigned,bool,bool=false) const;
};
}
