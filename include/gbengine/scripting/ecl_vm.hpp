#pragma once
#include "gbengine/assets/decoders.hpp"
#include "gbengine/scripting/ecl_state.hpp"
#include "gbengine/combat/encounter_dialogue.hpp"
#include <optional>
#include <bitset>
#include <random>

namespace GBEngine {
enum class EclStatus { ready, waiting, halted, budget_exhausted, faulted };
enum class EclRequestKind { print, picture, load_files, load_pieces, delay, clear_box,
    sprite_off, write_value, write_string, native_call, menu, select_character, load_ecl, damage, input_number, input_string, glyph_table, advance_time,
    clear_treasure,treasure_setup,campaign_service,remove_wealth,select_current,restore_current,add_npc,engine_service,
    setup_sprite,approach_sprite,add_monsters,combat,encounter_dialogue };
struct EclMenu {
    std::uint16_t destination{};
    std::vector<std::string> labels;
    std::vector<std::uint8_t> hotkeys;
    bool allow_enter{};
    bool vertical=false;
    std::string prompt;
    std::vector<std::uint16_t> responses; // PARLAY maps choice indices to script-supplied bytes.
};
struct EclRequest {
    std::uint64_t token{};
    EclRequestKind kind{};
    std::uint16_t address{};
    std::uint8_t opcode{};
    std::vector<std::uint16_t> values;
    std::string text;
    std::optional<EclMenu> menu;
    std::string text_id{};
    std::optional<EncounterDialogueConfig> encounter{};
};
// Reads which depend on selected characters or game-specific special registers.
// Implementations must be read-only; no zero-valued fallback is supplied.
struct IEclBindings {
    virtual ~IEclBindings() = default;
    virtual std::optional<std::uint16_t> read_value(std::uint16_t) const { return std::nullopt; }
    virtual std::optional<std::string> read_character_name() const { return std::nullopt; }
    virtual std::optional<std::uint16_t> party_strength() const { return std::nullopt; }
    virtual std::optional<std::uint16_t> party_field_2f_check() const { return std::nullopt; }
    virtual std::optional<bool> party_has_item_type(std::uint8_t) const { return std::nullopt; }
    virtual std::optional<bool> party_has_effect(std::uint8_t) const { return std::nullopt; }
    virtual std::optional<std::array<std::uint16_t,3>> party_field_summary(std::uint16_t) const { return std::nullopt; }
    virtual std::optional<std::uint8_t> party_selected_identity() const { return std::nullopt; }
};
struct EclTrace {
    std::uint16_t address{};
    std::uint8_t opcode{};
    std::uint16_t next_address{};
    EclStatus status{};
};
struct EclOperand {
    std::uint8_t tag{};
    std::uint16_t raw{};
    std::string text;
    std::string text_id{};
    bool is_string() const { return tag==0x80 || tag==0x81; }
};
struct EclStateUpdate { std::uint16_t address,value; };
class EclVm {
    friend class PortSave;
    friend class ExplorationSession;
    std::array<std::uint16_t,1024> area1_{};
    std::array<std::uint16_t,1024> area2_{};
    std::array<std::uint16_t,512> area3_{};
    Bytes script_;
    std::string text_origin_;
    bool script_modified_=false;
    std::size_t loaded_size_{};
    EclEntryPoints entries_{};
    const IEclBindings* bindings_{};
    std::uint16_t pc_{};
    std::array<bool,6> flags_{};
    std::vector<std::uint16_t> stack_;
    std::mt19937 random_;
    EclStatus status_=EclStatus::ready;
    std::optional<EclRequest> pending_;
    std::uint16_t pending_next_{};
    std::vector<EclStateUpdate> pending_followups_;
    std::uint64_t request_sequence_{};
    std::size_t steps_{};
    bool area1_dirty_{};
    // Invocation-local source numeric-write receipts; never serialized.
    std::bitset<1024> area1_numeric_writes_;
    std::optional<std::uint8_t> invocation_selection_;
    bool restore_selection_=false;
    bool combat_preparation_committed_=false; // Pending combat only; never saveable.
    std::string error_;
    std::vector<EclTrace> trace_;
    std::size_t trace_limit_=256;
    std::uint8_t fetch(std::size_t&) const;
    void valid_target(std::uint16_t) const;
    EclOperand operand(std::size_t&) const;
    std::vector<EclOperand> operands(std::size_t&,std::size_t) const;
    std::uint16_t value(const EclOperand&) const;
    std::string read_string(std::uint16_t) const;
    std::uint16_t cursor_address(std::size_t) const;
    void compare(std::uint16_t left,std::uint16_t right);
    void execute();
    void request(EclRequestKind,std::uint8_t,std::size_t,
                 std::vector<std::uint16_t> values={},std::string text={});
    void write_value(std::uint16_t,std::uint16_t,std::uint8_t,std::size_t);
    void write_values(std::span<const EclStateUpdate>,std::uint8_t,std::size_t);
    void write_string(std::uint16_t,std::string_view,std::uint8_t,std::size_t);
    std::uint16_t selected_damage_roll(std::uint64_t,std::size_t,std::mt19937&) const;
    void complete_host_request(std::uint64_t,std::span<const EclStateUpdate>);
public:
    explicit EclVm(std::span<const std::uint8_t> record,std::uint32_t seed=1,
                   const IEclBindings* bindings=nullptr);
    void start_entry(std::size_t index);
    void start_at(std::uint16_t address);
    EclStatus run(std::size_t instruction_budget);
    // The host must actually perform the request before acknowledging its token.
    // Stale tokens and unsolicited acknowledgements fail without advancing the VM.
    // Optional bank updates are fully validated before any write. They let a host
    // map service commit its verified state changes with the acknowledgement.
    void complete_request(std::uint64_t token,
        std::span<const EclStateUpdate> state_updates={});
    // Menu responses use the native numeric setter, including yielded host effects.
    // Invalid/stale answers leave the pending menu and all VM state unchanged.
    void answer_menu(std::uint64_t token,std::size_t selection);
    void answer_input(std::uint64_t token,std::string_view text);
    void replace_script(std::uint64_t token,std::span<const std::uint8_t> record,std::uint8_t previous_record);
    // Preview is read-only. The typed answer consumes the identical RNG draws,
    // acknowledges once, and optionally halts after native party defeat.
    std::uint16_t preview_selected_damage(std::uint64_t token,std::size_t party_size) const;
    void answer_selected_damage(std::uint64_t token,std::size_t party_size,bool party_defeated);
    void answer_attack_damage(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,bool defeated);
    // Requires all seven resulting clock words, in VM49C6..49CC order. The
    // caller stages party effects before acknowledging this typed service.
    void answer_advance_time(std::uint64_t token,std::span<const EclStateUpdate> clock);
    void answer_advance_time(std::uint64_t token,std::span<const EclStateUpdate> clock,const std::mt19937& expected,const std::mt19937& next);
    // Sprite setup/approach publish bank distances only after successful
    // resource loading and actual presentation of the staged frame.
    void answer_sprite_request(std::uint64_t token,std::uint8_t distance);
    // Idle host operations share the VM's modern RNG. A compare-and-commit
    // checkpoint prevents a stale spell/item transaction overwriting draws.
    std::mt19937 random_state() const {return random_;}
    void commit_random_state(const std::mt19937& expected,const std::mt19937& next);
    // One idle camp tick publishes its seven clock words and random checkpoint.
    void commit_rest_tick(const std::mt19937& expected,const std::mt19937& next,
        const std::array<std::uint16_t,7>& clock);
    void answer_campaign_request(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next);
    // Native NPC operand2 and engine-service operands are evaluated after the
    // staged append/selection restoration, never against the old live member.
    std::uint16_t preview_party_parameter(std::uint64_t token,const IEclBindings&) const;
    const auto& invocation_selection() const {return invocation_selection_;}
    std::optional<std::uint8_t> selection_to_restore() const {return restore_selection_?invocation_selection_:std::nullopt;}
    void answer_party_request(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,
        std::span<const EclStateUpdate> updates={},bool halt=false);
    // ECL0B retains encoded operands; evaluation order is template, source
    // construction, quantity, art. A full63-creature setup evaluates none.
    std::uint16_t preview_encounter_parameter(std::uint64_t token,std::size_t index) const;
    void answer_encounter_setup(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next);
    void answer_special_combat_setup(std::uint64_t token,const std::mt19937& expected);
    // ENCOUNTER remains suspended through all choices and presentation barriers.
    // A disengagement result uses the native setter, including host effects;
    // an unrecognized source policy returns without setting the destination.
    void update_encounter_dialogue(std::uint64_t token,std::uint8_t distance);
    void answer_encounter_dialogue(std::uint64_t token,std::optional<std::uint16_t> result);
    void suspend_for_combat(std::uint64_t token);
    // One spatial-only transaction while24 remains held. This cannot produce
    // an outcome or acknowledge ECL; stale/repeated publication leaves state intact.
    void commit_combat_preparation(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,
                                   std::uint8_t separation);
    void commit_combat_runtime(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,bool clear_surprise=false,bool hostility=false);
    void commit_combat_clock(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,
                             const std::array<std::uint16_t,7>& clock);
    // Tactical cleanup hands the still-held ECL24 to its ordinary collection
    // screen. The script continues only after that screen completes.
    void begin_combat_rewards(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,
                             std::uint16_t defeated_count,std::uint16_t source_result=0,
                             std::optional<std::uint16_t> party_count={});
    void complete_combat_defeat(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,
                               std::uint16_t defeated_count);
    std::uint8_t next_opcode() const;
    // Opening a real shop/treasure modal clears native bank flags while the
    // dispatcher remains suspended. This never advances the pending request.
    void update_campaign_state(std::uint64_t token,std::span<const EclStateUpdate>);
    // A shop decision consumes RNG while its ECL dispatcher remains suspended.
    void commit_campaign_random_state(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next);
    // Mirrors the recovered Portraits preference into its saved bank bit.
    // This host setting is allowed only while the interpreter is quiescent.
    void set_portraits_enabled(bool);
    void set_monsters_enabled(bool);
    std::uint16_t read_value(std::uint16_t address) const;
    // Explicit fixture initialization; character/register overrides remain bound
    // through IEclBindings rather than falling back to the raw seeded banks.
    void seed_value(std::uint16_t address,std::uint16_t value);
    // Load raw saved banks without invoking character/register write effects.
    // The caller must bind the restored party/world and perform native resume setup.
    void restore_state(const EclStateBanks&,std::span<const std::uint8_t> script_image);
    EclStateBanks state_banks() const { return {area1_,area2_,area3_}; }
    EclStatus status() const { return status_; }
    std::uint16_t pc() const { return pc_; }
    std::size_t steps() const { return steps_; }
    std::size_t stack_depth() const { return stack_.size(); }
    bool area1_dirty() const { return area1_dirty_; }
    const auto& comparison_flags() const { return flags_; }
    const auto& trace() const { return trace_; }
    const auto& pending_request() const { return pending_; }
    const auto& error() const { return error_; }
    const auto& entry_points() const { return entries_; }
};
std::string_view ecl_status_name(EclStatus);
std::string_view ecl_request_name(EclRequestKind);
std::string_view ecl_opcode_name(std::uint8_t);
}
