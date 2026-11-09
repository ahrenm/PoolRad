#pragma once
#include "gbengine/party/party.hpp"
#include <functional>
#include <random>

namespace GBEngine {
struct CombatantId {
    std::uint64_t value{};
    bool operator==(const CombatantId&) const =default;
};
struct EncounterCreature {
    CombatantId id;
    Character character;
};
struct EncounterGroup {
    std::uint8_t template_id{},quantity{},art_selector{},native_group{};
    // The paired source CPIC records are validated and owned at ECL0B time.
    // Groups with equal native_group alias one native art slot; a later0B
    // replaces every such binding together. Selector+80 is consumed as a byte.
    // CombatSession exposes groups read-only; tactical drawing is a later step.
    std::string art_source_file;
    std::array<std::vector<Image>,2> art_frames;
    std::vector<EncounterCreature> creatures;
    // CALL8000 appends its own selected-character copy, outside ECL0B's
    // quantity/group counters. Its native CPIC record is11, not a MON template.
    bool surrogate=false;
};
struct SpecialCombatContext {
    std::size_t selected_party_index{};
    bool surrogate=false; // CALL8000/VM6DE6=1; CALL8001/VM6DE6=0.
    std::array<std::uint8_t,16> selected_name{};
    // Native setup compares Pascal names rather than identity. These copies
    // hold its active-byte edits without altering the durable exploration Party.
    std::vector<Character> tactical_party;
    std::vector<std::uint8_t> original_active;
};
// Native encounter state is resident context, separate from ECL bank words.
// Monsters retain independent ownership until tactical participant binding is
// implemented; they are never appended to the exploration Party.
struct EncounterState {
    bool initialized=false;
    bool setup_prepared=false; // resident84E7
    bool duel_context=false;   // resident84D4; NOT VM6DE6
    std::uint8_t count=0,next_group=8;
    std::uint64_t next_combatant_id=1;
    std::vector<EncounterGroup> groups;
    // Native DS5D92 may select an appended temporary actor before ECL24.
    // Own an identity, never a pointer into copied/reallocated group vectors.
    std::optional<CombatantId> selected_actor;
    std::optional<SpecialCombatContext> special;
    void clear() {
        const auto retained_duel=duel_context;
        auto retained_special=std::move(special);
        *this=EncounterState{};initialized=true;duel_context=retained_duel;special=std::move(retained_special);
    }
};
class EncounterService {
public:
    // ECL0B evaluates template first, loads its graph and draws five icon
    // colors, then evaluates quantity/art. The caller must stage any VM side
    // effects of late_parameters in the same outer transaction.
    static void add_group(EncounterState&,Party&,const IFileSystem&,unsigned disk,
                          std::uint8_t template_id,std::mt19937&,
                          const std::function<std::array<std::uint8_t,2>()>& late_parameters);
};
}
