#pragma once
#include "gbengine/party/equipment.hpp"
#include <functional>

namespace GBEngine {
struct EffectNotice;
enum class ItemEffectTrigger { use,equip,unequip };
struct ItemEffectRequest {
    ItemEffectTrigger trigger=ItemEffectTrigger::use;
    std::size_t actor{},target{};
    ItemId item{};
    std::uint64_t revision{};
    std::uint8_t native_effect{},cast_index{};
    std::array<std::uint8_t,3> raw_effects{};
    std::optional<std::size_t> scroll_slot;
    ItemOrigin origin;
};
enum class ItemActionStatus { applied,rejected,confirmation_required,choice_required,unresolved };
struct ItemActionResult {
    ItemActionStatus status=ItemActionStatus::rejected;
    std::string message;
    ItemId item{};
    std::optional<ItemEffectRequest> effect;
    bool applied() const { return status==ItemActionStatus::applied; }
};
// Whole-operation adapter: unlike an effect callback, this owns the final
// commit of inventory, character state and any shared RNG checkpoint.
class IItemActivationService {
public:
    virtual ~IItemActivationService()=default;
    virtual ItemActionResult activate_item(const ItemEffectRequest&)=0;
    virtual const EffectNotice* activation_notice() const {return nullptr;}
    virtual void activation_notice_presented() {}
    virtual void advance_activation_notice(std::uint64_t) {}
    virtual std::optional<std::uint8_t> noncombat_item_target_mode(const ItemEffectRequest&) {return {};}
    virtual ItemActionResult ready_item(std::size_t,ItemId item) {
        return {ItemActionStatus::unresolved,"Equipment effect service is unavailable",item,{}};
    }
};
// Implementations resolve actual game effects against staged characters. An
// unresolved/failed effect rolls back state AND item charge consumption.
class IItemEffectResolver {
public:
    virtual ~IItemEffectResolver()=default;
    virtual ItemActionResult resolve(const ItemEffectRequest&,std::span<Character> staged_party)=0;
};
struct IdentificationContext {
    bool service_available=false;
    // Null means campaign pool not bound, rather than a fabricated empty pool.
    std::array<std::uint32_t,5>* pooled_coins=nullptr;
};
class InventoryService {
    Party& party_;
    ItemActionResult mutate(const std::function<ItemActionResult(std::vector<Character>&,ItemId&)>&);
    static std::optional<std::size_t> locate(const Character&,ItemId);
public:
    explicit InventoryService(Party& party):party_(party) {}
    ItemActionResult ready(std::size_t member,ItemId,IItemEffectResolver* effects=nullptr);
    ItemActionResult trade(std::size_t member,ItemId,std::size_t recipient,bool confirm_special=false);
    ItemActionResult drop(std::size_t member,ItemId,bool confirmed=false);
    ItemActionResult halve(std::size_t member,ItemId);
    ItemActionResult join(std::size_t member,ItemId);
    ItemActionResult identify(std::size_t member,ItemId,IdentificationContext,bool confirmed=false);
    ItemActionResult prepare_use(std::size_t member,ItemId,std::size_t target,std::optional<std::size_t> scroll_slot={}) const;
    ItemActionResult read_scroll(std::size_t member,ItemId);
    ItemActionResult use(const ItemEffectRequest&,IItemEffectResolver&);
    // Used by future treasure/shop services; no UI grants or invented items.
    ItemActionResult grant(std::size_t member,ItemTemplateId);
};
}
