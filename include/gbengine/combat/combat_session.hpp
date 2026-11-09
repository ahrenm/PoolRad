#pragma once
#include "gbengine/combat/encounter.hpp"
#include "gbengine/combat/deployment.hpp"
#include "gbengine/combat/round_service.hpp"
#include "gbengine/combat/spatial_service.hpp"
#include "gbengine/combat/lifecycle_service.hpp"
#include "gbengine/combat/attack_service.hpp"
#include "gbengine/combat/basic_opponent_service.hpp"
#include "gbengine/combat/tactical_command_service.hpp"
#include "gbengine/combat/tactical_movement_service.hpp"
#include "gbengine/combat/turn_undead_service.hpp"
#include "gbengine/combat/casting_service.hpp"
#include "gbengine/combat/magic_service.hpp"
#include "gbengine/combat/equipment_service.hpp"
#include "gbengine/combat/aftermath_service.hpp"
#include <memory>
namespace GBEngine {
namespace Debug {class Access;} 
struct CombatToken {
    std::uint64_t value{};
    bool operator==(const CombatToken&) const =default;
};
enum class CombatEntryKind {launch,setup_blocked};
struct CombatEntry {
    CombatToken token;
    std::uint64_t ecl_token{};
    unsigned disk{};
    std::uint8_t script_record{};
    std::uint16_t instruction{};
    std::size_t party_size{};
    CombatEntryKind kind=CombatEntryKind::launch;
    std::string setup_error;
};
struct CombatMagicPlayback {
    CombatActorIdentity caster;
    std::uint8_t cast_index{};
    std::optional<CombatPosition> origin,destination;
    std::shared_ptr<const DeploymentState> before;
    std::vector<CombatMagicFeedback> feedback;
    std::vector<CombatMagicDamage> damage;
    std::vector<CombatMagicEvent> events;
    std::vector<CombatMagicProjectile> projectiles;
};
struct CombatAftermathPending {
    CombatAftermathResult result;
    std::mt19937 expected_random;
    bool presented=false;
};
// Spatial state is independent of persistent characters and source encounter
// templates. Round setup, effect phase8, actions and outcomes remain separate.
class CombatSession {
    friend class Debug::Access;
    CombatEntry entry_;
    EncounterState encounter_;
    std::unique_ptr<DeploymentState> deployment_;
    std::string preparation_error_;
    CombatRoundState round_;
    std::optional<CombatSpatialService> spatial_;
    std::optional<CombatAttackService> attacks_;
    std::vector<CombatStrike> strikes_;
    std::optional<CombatWeaponPlayback> standalone_travel_;
    std::optional<BasicOpponentService> opponents_;
    std::optional<BasicOpponentState> opponent_turn_;
    struct MoveCheckpoint {
        CombatActorIdentity actor;
        std::optional<CombatPosition> position;
        std::uint8_t budget{},facing{};
    };
    std::optional<MoveCheckpoint> movement_checkpoint_;
    std::optional<TacticalMovementService> movement_;
    std::optional<CombatTargetingService> targeting_;
    std::optional<TurnUndeadService> turning_;
    std::shared_ptr<const CombatMagicService> magic_;
    std::vector<CombatCastPending> casts_;
    std::optional<CombatMagicPlayback> magic_playback_;
    std::shared_ptr<const std::vector<MonsterAbilityPlayback>> monster_playback_;
    std::shared_ptr<const RetainedAbilityService> retained_;
    CombatRoundContext round_context_;
    ItemId next_item_id_=1;
    void prune_casts();
    std::uint64_t revision_=0;
    bool runtime_attempted_=false;
    std::string runtime_error_,action_message_;
    std::optional<CombatRoundEndDisposition> round_end_;
    std::uint8_t opponent_hp_percent_=100;
    bool automatic_magic_=false; // Native resident6E30, independent of VM6E30.
    bool hostility_started_=false; // Native resident6DE2; checkpoint-owned combat state.
    std::unique_ptr<CombatAftermathPending> aftermath_;
public:
    CombatSession(CombatEntry entry,EncounterState encounter);
    CombatSession(const CombatSession&);
    CombatSession(CombatSession&&) noexcept=default;
    CombatSession& operator=(CombatSession&&) noexcept=default;
    const CombatEntry& entry() const {return entry_;}
    const EncounterState& encounter() const {return encounter_;}
    const DeploymentState* deployment() const {return deployment_.get();}
    const std::string& preparation_error() const {return preparation_error_;}
    const CombatRoundState& round() const {return round_;}
    std::uint64_t revision() const {return revision_;}
    bool runtime_attempted() const {return runtime_attempted_;}
    const std::string& runtime_error() const {return runtime_error_;}
    const std::string& action_message() const {return action_message_;}
    const auto& strikes() const {return strikes_;}
    const auto& standalone_travel() const {return standalone_travel_;}
    const auto& magic_playback() const {return magic_playback_;}
    const auto& monster_playback() const {return monster_playback_;}
    ItemId next_item_id() const {return next_item_id_;}
    bool automatic_magic() const {return automatic_magic_;}
    bool hostility_started() const {return hostility_started_;}
    const CombatAftermathPending* aftermath() const {return aftermath_.get();}
    void hold_aftermath(CombatAftermathResult,const std::mt19937&);
    void aftermath_presented() {if(aftermath_)aftermath_->presented=true;}
    void toggle_automatic_magic();
    const SpellCatalog& spell_catalog() const;
    std::vector<MemorizedSpell> castable_spells(bool casting_blocked=false) const;
    const CombatCastPending* pending_cast() const;
    CombatMagicTargetPolicy magic_target_policy() const;
    std::vector<CombatActorIdentity> magic_candidates() const;
    void schedule_spell(std::size_t slot,const ItemCatalog&,bool casting_blocked,std::mt19937&);
    void schedule_item(ItemId,std::optional<std::size_t>,const ItemCatalog&,bool casting_blocked);
    void read_scroll(ItemId,const ItemCatalog&);
    void ready_item(ItemId,const ItemCatalog&,std::mt19937* =nullptr);
    void resolve_magic(std::span<const CombatActorIdentity>,std::optional<CombatPosition>,const ItemCatalog&,
                       std::uint16_t dungeon_flag,std::mt19937&,const CombatAttackContext& = {});
    void cancel_magic(const ItemCatalog&,std::mt19937&);
    bool moving() const {return movement_checkpoint_.has_value();}
    bool continuation_pending() const {return round_end_==CombatRoundEndDisposition::continue_question;}
    bool outcome_pending() const {return round_end_==CombatRoundEndDisposition::outcome_pending;}
    std::uint8_t opponent_hp_percent() const {return opponent_hp_percent_;}
    std::optional<std::size_t> active_index() const;
    void start_runtime(const IFileSystem&,const ItemCatalog&,std::uint16_t surprise,std::uint8_t movement_bonus,std::mt19937&,const CombatAttackContext& = {},std::optional<CombatActorIdentity> entry_selected={});
    void block_runtime(std::string);
    void report_action(std::string message) {action_message_=std::move(message);}
    bool begin_movement();
    bool move(unsigned direction,const ItemCatalog&,const CombatAttackContext&,std::mt19937&);
    void command(TacticalCommand,const ItemCatalog&,std::mt19937&);
    void turn_undead(const ItemCatalog&,std::mt19937&);
    bool flight_prompt(unsigned direction) const;
    bool begin_flight(unsigned direction);
    void flee(unsigned direction,const ItemCatalog&,std::mt19937&);
    std::vector<CombatTarget> targets(const ItemCatalog&) const;
    CombatTarget probe_target(CombatPosition,const ItemCatalog&) const;
    std::optional<CombatActorIdentity> bump_target(unsigned direction,bool include_allies=false) const;
    bool begin_friendly_bump(unsigned direction);
    bool can_attack(CombatActorIdentity,bool confirmed_friendly=false) const;
    void attack(CombatActorIdentity,const ItemCatalog&,const CombatAttackContext&,std::mt19937&,bool confirmed_friendly=false);
    void step_opponent(const ItemCatalog&,const BasicOpponentContext&,std::mt19937&);
    bool finish_movement(bool cancel);
    void finish_turn(const ItemCatalog&,std::mt19937&);
    GameClock finish_round(const GameClock&,const ItemCatalog&,std::mt19937&);
    void continue_battle(bool,const ItemCatalog&,std::mt19937&);
    Party inspection_party(const Party& resources) const;
    void prepare_spatial(const IFileSystem&,const Party&,const GeoMap*,std::span<const std::uint8_t>,
                         std::uint8_t mode,int x,int y,std::uint16_t region_flag,
                         const DeploymentParameters&,std::mt19937&);
    void block_preparation(std::string error) {deployment_.reset();preparation_error_=std::move(error);}
};
}
