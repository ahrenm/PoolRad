#pragma once
#include "gbengine/magic/magic_effect_service.hpp"
#include "gbengine/magic/spellbook.hpp"
#include "gbengine/party/inventory_service.hpp"
namespace GBEngine {
struct SpellCastPlan {
    std::uint64_t revision;
    std::size_t caster,slot;
    SpellId spell;
    MagicTarget target;
    MagicDomain domain;
};
class SpellService {
    Party& party_;
    const SpellCatalog& catalog_;
    static void validate_caster(const Character&,MagicContext);
public:
    SpellService(Party& party,const SpellCatalog& catalog):party_(party),catalog_(catalog) {}
    std::vector<MemorizedSpell> castable(std::size_t caster,MagicContext) const;
    SpellCastPlan prepare(std::size_t caster,std::size_t slot,MagicTarget,MagicContext) const;
    // Clock charging is deliberately absent: the recovered exploration Cast
    // path adds no time. Future combat action costs belong to the combat host.
    MagicResult cast(const SpellCastPlan&,MagicContext,std::mt19937&,IMagicEffectResolver&);
    // Native Treasury D scans raw slots for the first exact5/11, consumes it
    // before the self handler and uses execution6 supplied by its host. This
    // entry deliberately has none of the ordinary Cast UI eligibility gates.
    MagicResult cast_treasury_detect(std::size_t caster,std::uint64_t expected_revision,
                                     std::mt19937&,IMagicEffectResolver&);
    MagicResult use_item(const ItemEffectRequest&,MagicContext,std::mt19937&,IMagicEffectResolver&);
    // Quest/innate hosts must identify their origin and expected party revision.
    // This is not a memorized-spell bypass exposed through the exploration UI.
    MagicResult apply_external(const MagicRequest&,std::uint64_t expected_revision,std::mt19937&,IMagicEffectResolver&);
};
}
