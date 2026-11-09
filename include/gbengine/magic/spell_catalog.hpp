#pragma once
#include "gbengine/assets/decoders.hpp"
#include <compare>
namespace GBEngine {
// Regular memorized/known spell IDs. Item-only cast indices57..67 and SPC
// effect codes use different domains and must not enter this checked type.
struct SpellId {
    std::uint8_t value;
    explicit SpellId(std::uint8_t);
    auto operator<=>(const SpellId&) const =default;
};
struct SpellProvenance {
    std::string source_file;
    std::size_t module_offset{};
    std::string source_sha256,module_sha256;
};
struct ItemCastId {
    std::uint8_t value;
    explicit ItemCastId(std::uint8_t);
    auto operator<=>(const ItemCastId&) const =default;
};
class ItemCastDefinition {
    ItemCastId id_;
    std::array<std::uint8_t,16> bytes_{};
    SpellProvenance provenance_;
    ItemCastDefinition(ItemCastId,std::span<const std::uint8_t>,SpellProvenance);
    friend class SpellCatalog;
public:
    ItemCastId id() const {return id_;}
    std::uint8_t byte(std::size_t at) const {return bytes_.at(at);}
    const auto& raw_bytes() const {return bytes_;}
    const SpellProvenance& provenance() const {return provenance_;}
    std::uint8_t school() const {return byte(0);}
    std::uint8_t level() const {return byte(1);}
    std::uint8_t duration_base() const {return byte(4);}
    std::uint8_t duration_per_level() const {return byte(5);}
    std::uint8_t noncombat_target_mode() const {return byte(7);}
    std::uint8_t combat_target_mode() const {return byte(6);}
    bool combat_allowed() const {return byte(11)!=0;}
    std::uint8_t casting_time() const {return byte(12);}
    std::uint8_t effect_code() const {return byte(10);}
};
class SpellDefinition {
    SpellId id_;
    std::array<std::uint8_t,16> bytes_{};
    SpellProvenance provenance_;
    std::string name_id_;
    SpellDefinition(SpellId,std::span<const std::uint8_t>,SpellProvenance);
    friend class SpellCatalog;
public:
    SpellId id() const {return id_;}
    std::string_view name() const;
    const std::string& name_id() const {return name_id_;}
    std::uint8_t byte(std::size_t at) const {return bytes_.at(at);}
    const auto& raw_bytes() const {return bytes_;}
    const SpellProvenance& provenance() const {return provenance_;}
    std::uint8_t school() const {return byte(0);}
    std::uint8_t level() const {return byte(1);}
    std::uint8_t range_base() const {return byte(2);}
    std::uint8_t range_per_level() const {return byte(3);}
    std::uint8_t duration_base() const {return byte(4);}
    std::uint8_t duration_per_level() const {return byte(5);}
    std::uint8_t noncombat_target_mode() const {return byte(7);}
    std::uint8_t combat_target_mode() const {return byte(6);}
    bool combat_allowed() const {return byte(11)!=0;}
    std::uint8_t casting_time() const {return byte(12);}
    std::uint8_t effect_code() const {return byte(10);}
};
class SpellCatalog {
    std::vector<SpellDefinition> definitions_;
    std::vector<ItemCastDefinition> item_definitions_;
    Bytes native_module_;
public:
    static SpellCatalog load(const IFileSystem&);
    const SpellDefinition& definition(SpellId) const;
    const auto& definitions() const {return definitions_;}
    const ItemCastDefinition& item_definition(ItemCastId) const;
    const auto& item_definitions() const {return item_definitions_;}
    // Exact selected-build resident rule tables, decoded once at bootstrap.
    std::span<const std::uint8_t> native_module() const {return native_module_;}
};
}
