#pragma once
#include "gbengine/party/inventory_service.hpp"
#include "gbengine/magic/native_effect_context.hpp"
namespace GBEngine {
struct EquipmentBindingReport {std::size_t bound{},unresolved{};};
// Native equipment handlers are a distinct namespace from spell/cast IDs.
// The caller owns staging; unsupported bindings preserve their original graph.
class EquipmentEffectService final : public IItemEffectResolver {
    const ItemCatalog* items_{};
    NativeEffectContext context_{};
public:
    explicit EquipmentEffectService(const ItemCatalog* items=nullptr,NativeEffectContext context={}):items_(items),context_(context) {}
    ItemActionResult resolve(const ItemEffectRequest&,std::span<Character>) override;
    static EquipmentBindingReport bind_restored(Party&);
    // Reconstruct metadata after a loaded actor receives final item identities.
    // Never readies items again, changes stats, creates nodes or consumes RNG.
    static EquipmentBindingReport bind_restored(Character&);
};
}
