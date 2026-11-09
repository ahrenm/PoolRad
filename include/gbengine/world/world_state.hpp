#pragma once
#include "gbengine/scripting/ecl_vm.hpp"
#include "gbengine/party/party.hpp"
#include "gbengine/combat/encounter.hpp"
#include "gbengine/world/geometry.hpp"
#include "gbengine/world/exploration_background.hpp"
namespace GBEngine {
enum class MoveResult { moved, solid_wall, locked_door, map_boundary };
enum class ExplorationPhase { initializing, idle, before_move, movement_time, movement_notice, look_time, after_move, looking, arriving };
class WorldState final : public IEclBindings {
public:
    std::optional<GeoMap> map;
    std::optional<ExplorationBackground> background;
    // Preview defaults, not a recovered new-game spawn point.
    int x=0,y=0;
    Direction facing=Direction::north;
    Party party;
    // Native 0B appends to the linked roster without extending the saved party.
    // Own its graphs here so VM bindings and staged/checkpoint copies agree.
    EncounterState encounter;
    const Character* selected_encounter_actor() const;
    Character* selected_encounter_actor();
    void select_prepared_participant(std::uint8_t ordinal);
    // Borrowed ordered view; valid until this world's participant storage changes.
    std::vector<const Character*> participants() const;
    unsigned resource_disk=1;
    std::uint16_t tile_query_x{},tile_query_y{},tile_query_value{},tile_query_result{};
    bool tile_query_available=false;
    // Native resident84D2/D3: numeric6C00/6B00 zero-write removal latches.
    // Not character bytes or VM bank words; native map initialization clears.
    bool removal_activity_armed=false,removal_name_armed=false;
    // Resident DS6DDE..E0, reset by native initialization/successful step.
    // They are not VM bank words. Native saves do not persist these gates.
    bool door_bash=true,door_pick=true,door_knock=true;
    // Native write-only CALL parameters: VM03DE/B8/B9 -> resident84CC/CE/D0.
    // Keep opaque words; accepting a write does not implement their consumers.
    std::array<std::optional<std::uint16_t>,3> call_parameters{};
    std::optional<std::uint16_t> read_value(std::uint16_t) const override;
    std::optional<std::string> read_character_name() const override {
        if(const auto* actor=selected_encounter_actor())return actor->name();
        return party.selected_name();
    }
    std::optional<std::uint8_t> party_selected_identity() const override {
        return party.selected_index()?std::optional<std::uint8_t>(party.member(*party.selected_index()).byte(0xbf)):std::nullopt;
    }
    std::optional<std::uint16_t> party_strength() const override;
    std::optional<std::uint16_t> party_field_2f_check() const override;
    std::optional<bool> party_has_item_type(std::uint8_t type) const override;
    std::optional<bool> party_has_effect(std::uint8_t code) const override;
    std::optional<std::array<std::uint16_t,3>> party_field_summary(std::uint16_t field) const override;
    bool write_register(std::uint16_t,std::uint16_t);
    void turn(int quarters);
    MoveResult move(bool backwards=false);
};
}
