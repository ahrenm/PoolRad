#pragma once
#include "gbengine/assets/decoders.hpp"
#include "gbengine/party/inventory.hpp"
#include <optional>
#include <memory>
#include <set>
namespace GBEngine {
class ItemCatalog;
class InventoryService;
struct CharacterEffect {
    // Native .SPC payload; next far pointer at +5 is replaced by vector order.
    std::array<std::uint8_t,5> payload{};
    std::string source_file;
    std::size_t source_offset{};
    std::optional<std::uint8_t> spell_id;
    std::optional<ItemOrigin> item_origin;
    std::optional<ItemId> equipment_item_id;
    std::optional<std::uint8_t> item_cast_id;
    bool operator==(const CharacterEffect&) const =default;
    std::uint8_t code() const { return payload[0]; }
    std::uint16_t word_1() const { return static_cast<std::uint16_t>(payload[1]|(payload[2]<<8)); }
    void set_duration(std::uint16_t value) {payload[1]=static_cast<std::uint8_t>(value);payload[2]=static_cast<std::uint8_t>(value>>8);}
};
struct CharacterStats {
    std::array<std::uint8_t,6> abilities{};
    std::uint8_t exceptional_strength{},maximum_hp{},current_hp{},movement{};
    std::uint16_t age{},encumbrance{};
    std::uint32_t experience{};
    std::array<std::uint8_t,8> levels{};
    std::array<std::uint16_t,7> money{};
    int armor_class{},thac0{};
    std::uint8_t damage_dice{},damage_sides{};
    int damage_bonus{};
};
class Character {
    friend class SavingThrowService;
    friend class ScriptDamageService;
    friend class CharacterCreationService;
    friend class MonsterGazeService;
    friend class MonsterBreathService;
    friend class RetainedAbilityService;
    friend class OpponentFlightService;
    friend class SpecialCombatService;
    friend class CombatEquipmentService;
    friend class BasicOpponentService;
    friend class CombatAftermathService;
    friend class CombatAttackService;
    friend class CombatHealthService;
    friend class CombatCastingService;
    friend class CombatMagicService;
    friend class CombatMagicEffects;
    friend class CombatCloudService;
    friend class CombatEffectExpiry;
    friend class TacticalMovementService;
    friend class CombatActorPreparation;
    friend class CombatRoundService;
    friend class EncounterService;
    friend class NoncombatDamageService;
    friend class NoncombatPeriodicEffects;
    friend class NpcService;
    friend class ShrineService;
    friend class CampWorkService;
    friend class WealthLossService;
    friend class TreasureService;
    friend class EconomyService;
    friend class PortSave;
    std::array<std::uint8_t,285> bytes_{};
    std::string name_id_;
    Inventory inventory_;
    std::vector<CharacterEffect> effects_;
    bool effects_restored_=false;
    std::set<ItemId> bound_equipment_effects_;
    friend class InventoryService;
    friend class Party;
    friend class EffectLifecycle;
    friend class MagicEffectService;
    friend class EquipmentEffectService;
public:
    static Character decode(std::span<const std::uint8_t>,std::string name_id={});
    std::string name() const;
    CharacterStats stats() const;
    std::string_view race_name() const;
    std::string_view class_name() const;
    std::string_view alignment_name() const;
    std::string_view sex_name() const;
    const Inventory& inventory() const { return inventory_; }
    bool equipment_effects_bound() const;
    const auto& effects() const { return effects_; }
    bool effects_restored() const { return effects_restored_; }
    void load_effects(std::span<const std::uint8_t>,std::string source_file);
    void load_inventory(std::span<const std::uint8_t>);
    std::uint8_t byte(std::size_t offset) const { return bytes_.at(offset); }
    std::uint16_t word(std::size_t offset) const;
    void set_byte(std::size_t offset,std::uint8_t value) { bytes_.at(offset)=value;if(offset<=15) name_id_.clear(); }
    void set_word(std::size_t offset,std::uint16_t value);
    std::uint8_t hit_points() const { return byte(0x11b); }
    std::uint8_t status() const { return byte(0x10c); }
    bool active() const { return byte(0x10d)!=0; }
    void apply_exploration_damage(std::uint8_t amount);
};
// Read-only native roster queries also apply to the transient encounter roster.
// The caller supplies order and ownership; this view never selects or mutates.
struct CharacterQueries {
    static std::optional<std::uint16_t> strength_rating(std::span<const Character* const>);
    static std::optional<std::uint16_t> field_2f_check(std::span<const Character* const>);
    static std::optional<bool> has_item_type(std::span<const Character* const>,std::uint8_t);
    static std::optional<bool> has_effect(std::span<const Character* const>,std::uint8_t);
    static std::optional<std::array<std::uint16_t,3>> field_summary(std::span<const Character* const>,std::uint16_t);
};
std::string_view character_status_name(std::uint8_t);
struct DamageOutcome {
    std::size_t member{};
    std::uint16_t rolled_amount{};
    std::uint8_t before_hp{},after_hp{},status{};
    bool ignored=false,party_defeated=false;
    std::string message;
};
class Party {
    friend class FixLoadout;
    friend class ScriptDamageService;
    friend class TrainingService;
    friend class CharacterCreationService;
    friend class CombatIconService;
    friend class CombatEquipmentService;
    friend class CombatLifecycleService;
    friend class SpecialCombatService;
    friend class CombatAftermathService;
    friend class CombatSession;
    friend class EncounterService;
    friend class NpcService;
    friend class ShrineService;
    friend class RosterService;
    friend class RestService;
    friend class CampWorkService;
    friend class WealthLossService;
    friend class TreasureService;
    friend class EconomyService;
    friend class PortSave;
    std::vector<Character> members_;
    std::optional<std::size_t> selected_;
    std::shared_ptr<const ItemCatalog> item_catalog_;
    ItemId next_item_id_=1;
    std::uint64_t revision_=0;
    friend class InventoryService;
    friend class EffectLifecycle;
    friend class GameTimeService;
    friend class SpellService;
    friend class DoorService;
    friend class MagicEffectService;
    friend class EquipmentEffectService;
    void import_roster(const IFileSystem&,std::span<const std::string> stems,bool restore_effects);
public:
    // Import characters and item sidecars, not campaign progress or spell graphs.
    void import_characters(const IFileSystem&,char save_slot);
    // Campaign roster order is authoritative, including NPCs up to native eight.
    // Preserves effect payloads; execution/expiry still requires the effect subsystem.
    void restore_characters(const IFileSystem&,std::span<const std::string> stems);
    std::size_t size() const { return members_.size(); }
    const Character& member(std::size_t index) const { return members_.at(index); }
    const ItemCatalog& item_catalog() const;
    std::uint64_t revision() const { return revision_; }
    std::optional<std::size_t> selected_index() const { return selected_; }
    void select(std::size_t);
    std::optional<std::string> selected_name() const;
    std::optional<std::uint16_t> strength_rating() const;
    // Native opcode 22 tests class byte 2F for 04/0A; see the native View table.
    std::optional<std::uint16_t> field_2f_check() const;
    // ECL32 searches all owned item records, including inactive members.
    std::optional<bool> has_item_type(std::uint8_t) const;
    // ECL1E mode zero searches owned SPC records across the whole roster.
    // Unrestored graphs cannot establish an absent effect.
    std::optional<bool> has_effect(std::uint8_t) const;
    std::optional<std::array<std::uint16_t,3>> field_summary(std::uint16_t) const;
    std::optional<std::uint16_t> read_value(std::uint16_t) const;
    bool write_value(std::uint16_t address,std::uint16_t value);
    DamageOutcome damage_selected(std::uint16_t amount);
};
}
