#pragma once
#include "gbengine/platform/hal.hpp"
#include <array>
#include <optional>

namespace GBEngine {
using ItemId=std::uint64_t;
class InventoryService;
class Party;
struct ItemOrigin {
    std::string file;
    std::optional<std::uint8_t> archive_record;
    std::size_t record_offset{};
    bool operator==(const ItemOrigin&) const =default;
};
// Native .ITM records contain a screen-text scratch buffer and a saved far
// pointer. Neither is a persistent item identity or a replacement pointer.
class Item {
    friend class SpecialCombatService;
    friend class CombatEquipmentService;
    friend class BasicOpponentService;
    friend class CombatAftermathService;
    friend class CombatAttackService;
    friend class CombatCastingService;
    friend class CombatMagicService;
    friend class CombatMagicEffects;
    friend class EncounterService;
    friend class NpcService;
    friend class CampWorkService;
    friend class WealthLossService;
    friend class EconomyService;
    friend class PortSave;
    std::array<std::uint8_t,63> bytes_{};
    ItemId id_{};
    ItemOrigin origin_;
    friend class InventoryService;
    friend class Party;
    friend class EquipmentEffectService;
    friend class MagicEffectService;
public:
    static Item decode(std::span<const std::uint8_t>,ItemOrigin origin={});
    ItemId id() const { return id_; }
    const ItemOrigin& origin() const { return origin_; }
    std::uint8_t byte(std::size_t offset) const { return bytes_.at(offset); }
    bool readied() const { return byte(0x34)!=0; }
    std::uint8_t quantity() const { return byte(0x39); }
    std::uint8_t type() const { return byte(0x2e); }
    int bonus() const { return byte(0x32)<128?byte(0x32):static_cast<int>(byte(0x32))-256; }
    bool cursed() const { return byte(0x36)!=0; }
    std::uint8_t hidden_components() const { return byte(0x35); }
    std::uint8_t charges() const { return byte(0x3c); }
    std::array<std::uint8_t,3> effect_bytes() const { return {byte(0x3c),byte(0x3d),byte(0x3e)}; }
    std::uint16_t unit_weight() const { return static_cast<std::uint16_t>(byte(0x37)|(static_cast<unsigned>(byte(0x38))<<8)); }
    std::uint16_t value() const { return static_cast<std::uint16_t>(byte(0x3a)|(static_cast<unsigned>(byte(0x3b))<<8)); }
    std::string name(bool detects_magic=false) const;
    std::string ready_label() const;
};
class Inventory {
    friend class SpecialCombatService;
    friend class CombatEquipmentService;
    friend class BasicOpponentService;
    friend class CombatAftermathService;
    friend class CombatAttackService;
    friend class CombatCastingService;
    friend class CombatMagicService;
    friend class CombatMagicEffects;
    friend class EncounterService;
    friend class NpcService;
    friend class CampWorkService;
    friend class WealthLossService;
    friend class EconomyService;
    friend class PortSave;
    std::vector<Item> items_;
    friend class InventoryService;
    friend class Party;
    friend class EquipmentEffectService;
    friend class MagicEffectService;
public:
    static Inventory decode(std::span<const std::uint8_t>);
    std::size_t size() const { return items_.size(); }
    const Item& item(std::size_t index) const { return items_.at(index); }
};
}
