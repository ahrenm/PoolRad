#pragma once
#include "gbengine/combat/battlefield.hpp"
#include "gbengine/party/party.hpp"
#include <functional>

namespace GBEngine {
enum class CombatActorOrigin { party,encounter };
struct CombatActorIdentity {
    CombatActorOrigin origin=CombatActorOrigin::party;
    std::uint64_t value{};
    bool operator==(const CombatActorIdentity&) const=default;
};
struct CombatPosition {
    int x{},y{};
    bool operator==(const CombatPosition&) const=default;
};
struct DeploymentActor {
    CombatActorIdentity id;
    Character character;
    // Native allocated +108 graph: source zeroes exactly22 bytes. Addresses
    // and pointers never enter this owned replacement.
    std::array<std::uint8_t,22> extension{};
    std::uint8_t native_slot{},shape{};
    std::optional<CombatPosition> position;
    bool removed=false;
    // Owned counterpart of extension0A..0D's native target link. The raw
    // extension bytes remain opaque pointer storage, never host addresses.
    std::optional<CombatActorIdentity> retained_target={};
};
struct CombatBody {
    CombatActorIdentity actor;
    CombatPosition position;
    std::uint8_t underlying_terrain{};
};
struct CombatCloudCell {CombatPosition position;std::uint8_t underlying_terrain{};bool active=false;};
struct CombatCloud {
    CombatActorIdentity caster;
    std::uint8_t ordinal{};
    CombatPosition centre;
    std::array<CombatCloudCell,4> cells;
};
struct DeploymentParameters {
    std::uint8_t native_direction{};
    std::uint16_t separation{},persistent_party_count{},identity_parameter{};
    int world_x{},world_y{};
    std::uint8_t presentation_mode{};
    bool duel_context=false;
    // Native2C86:01AD bidirectional edge classification:1 is a solid edge;
    // 0 and3 permit region switching. Called only for interior presentation.
    std::function<std::uint8_t(int,int,std::uint8_t)> edge;
};
struct DeploymentState {
    Battlefield battlefield;
    std::vector<DeploymentActor> actors;
    // Native slot numbers, zero for empty. Body markers belong to terrain.
    std::array<std::uint8_t,1250> occupancy{};
    std::vector<CombatBody> bodies;
    // Owned source19-byte cloud layers. Active battles cannot be serialized.
    std::vector<CombatCloud> clouds{};
    // Native resident6810 thrown-item prefix, newest exhausted stack first.
    // Transfer owned ItemIds/provenance here; the quantity remains native0.
    // Aftermath converts these to campaign ownership without awarding item XP.
    std::vector<Item> ground_items{};
};
class DeploymentService {
public:
    // Source-derived formation scan. Complete candidate state is returned;
    // party, encounter ownership, campaign RNG and VM remain untouched.
    static DeploymentState build(const IFileSystem&,const Battlefield&,
                                 std::vector<DeploymentActor>,const DeploymentParameters&);
};
}
