#pragma once
#include "gbengine/combat/deployment.hpp"
#include "gbengine/magic/spellbook.hpp"
#include "gbengine/party/equipment.hpp"

namespace GBEngine {
enum class CombatCastOrigin { memorized,item,scroll };
enum class CombatCastingPolicy { manual,automatic };
// Session-owned continuation: native ext0 stores only the spell byte. Host
// identities retain its actual source without borrowing item/actor pointers.
struct CombatCastPending {
    CombatActorIdentity caster;
    std::uint8_t cast_index{};
    CombatCastOrigin origin=CombatCastOrigin::memorized;
    std::size_t source_slot{};
    std::optional<ItemId> item;
    std::array<std::uint8_t,3> item_effects{};
    std::optional<std::size_t> scroll_slot;
    std::optional<ItemOrigin> item_origin;
    // Zero means resolve the memorized caster's live native school level when
    // execution begins; queued casts must not freeze a cached level.
    std::uint8_t caster_level{},priority_delay{};
    bool delayed=false;
    //4053:1A8A permits noncombat-only item rows during battle but calls Quit
    // only when their combat-allowed byte is nonzero.
    bool ends_turn=true;
};
struct CombatCastSchedule {
    DeploymentState state;
    CombatCastPending pending;
    // Delayed memorized casts rescan once; immediate/item casting enters target
    // selection directly. Neither path consumes a slot, charge or RNG here.
    bool resume_selection=false;
};
class CombatCastingService {
public:
    static std::vector<MemorizedSpell> memorized(const DeploymentState&,CombatActorIdentity,bool casting_blocked=false);
    static CombatCastSchedule schedule(const DeploymentState&,CombatActorIdentity,std::size_t slot,
                                      const SpellCatalog&,bool casting_blocked=false,
                                      CombatCastingPolicy=CombatCastingPolicy::manual);
    static CombatCastSchedule prepare_item(const DeploymentState&,CombatActorIdentity,ItemId,
                                          std::optional<std::size_t> scroll_slot,
                                          const ItemCatalog&,const SpellCatalog&,bool casting_blocked=false,
                                          CombatCastingPolicy=CombatCastingPolicy::manual);
    // Source effect16 reveals the scroll before its spell list; this change
    // survives leaving that list without selecting a spell.
    static DeploymentState read_scroll(const DeploymentState&,CombatActorIdentity,ItemId,const ItemCatalog&);
    // In one outer transaction, memorized resources are consumed BEFORE effect
    // resolution, item resources AFTER it. Explicit Cancel-spell Y also charges.
    // The caller then performs source Quit/rescan. Failed transactions publish
    // neither effects nor resources. Initial selection cancellation is inert.
    static DeploymentState finish(const DeploymentState&,const CombatCastPending&,const ItemCatalog&);
    static bool still_pending(const DeploymentState&,const CombatCastPending&);
};
}
