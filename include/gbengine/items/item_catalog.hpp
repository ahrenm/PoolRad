#pragma once
#include "gbengine/party/inventory.hpp"
#include <map>

namespace GBEngine {
struct ItemTemplateId {
    unsigned disk{};
    std::uint8_t record{};
    std::size_t ordinal{};
    auto operator<=>(const ItemTemplateId&) const = default;
};
// ITEMS entries are data, not C++ object layouts. Category is not a slot:
// only the native groups 0..9 and two special ammunition types map to slots.
// Other categories (including scrolls and miscellaneous items) stay distinct.
class ItemDefinition {
    std::array<std::uint8_t,16> bytes_{};
public:
    static ItemDefinition decode(std::span<const std::uint8_t>);
    std::uint8_t byte(std::size_t at) const { return bytes_.at(at); }
    std::uint8_t category() const { return byte(0); }
    std::uint8_t hands() const { return byte(1); }
    std::uint8_t defense() const { return byte(6); }
    std::uint8_t damage_dice() const { return byte(9); }
    std::uint8_t damage_sides() const { return byte(10); }
    int damage_bonus() const { return byte(11)<128?byte(11):static_cast<int>(byte(11))-256; }
    std::uint8_t class_mask() const { return byte(13); }
    std::uint8_t attack_flags() const { return byte(14); }
    bool scroll() const { return category()>=11 && category()<=13; }
};
class ItemCatalog {
    std::array<ItemDefinition,128> definitions_{};
    std::map<ItemTemplateId,Item> templates_;
    std::array<std::uint32_t,5> coin_rates_{};
    // Resident DS0970: random magical-item component/effect templates. Words
    // retain native layout; display strings continue through StringStore.
    std::array<std::uint16_t,153> generated_item_words_{};
public:
    static ItemCatalog load(const IFileSystem&);
    static std::array<ItemDefinition,128> decode_definitions(std::span<const std::uint8_t>);
    const ItemDefinition& definition(std::uint8_t type) const;
    const std::map<ItemTemplateId,Item>& templates() const { return templates_; }
    const Item& item_template(ItemTemplateId id) const { return templates_.at(id); }
    const std::array<std::uint32_t,5>& coin_rates() const { return coin_rates_; }
    std::uint16_t generated_item_word(std::size_t index) const {return generated_item_words_.at(index);}
    static std::string_view spell_name(std::uint8_t spell);
};
}
