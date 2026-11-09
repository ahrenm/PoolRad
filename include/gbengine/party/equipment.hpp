#pragma once
#include "gbengine/party/party.hpp"
#include "gbengine/items/item_catalog.hpp"

namespace GBEngine {
// Native groups 0..8 have one slot; group 9 has two. Groups 11/12 are
// scroll families, not wearable slots. Two ammunition types have extra slots.
struct EquippedItems {
    std::array<std::optional<std::size_t>,13> slots{};
    std::uint8_t hands{};
    std::uint16_t total_weight{},readied_weight{};
};
class EquipmentRules {
    static void recalculate_in_place(Character&,const ItemCatalog&);
public:
    static int strength_rank(const Character&);
    static int load_allowance(const Character&);
    static EquippedItems inspect(const Character&,const ItemCatalog&);
    static void recalculate(Character&,const ItemCatalog&);
};
}
