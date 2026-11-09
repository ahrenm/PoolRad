#pragma once
#include "gbengine/combat/magic_service.hpp"

namespace GBEngine {
// Native effect58 is a phase14 callback, not an ordinary spell ID. Its target
// acquisition happens through the combat selector with metadata51/automatic1;
// the caller supplies that selector's actual cursor and live damage scratch.
struct MonsterBreathContext {
    CombatActorIdentity caster;
    std::size_t effect_index{};
    CharacterEffect expected_effect;
    CombatPosition acquired_cursor;
    CombatMagicContext damage_context;
    // Phase14 does not initialize pending681B/item6DBF. Resistance69/6A computes
    // a local level through26C2; it does not read681A or dice count681C.
    // AI supplies the approved metadata-based level. Direct callers must bind
    // their own level/pending context where a hit recipient requires it.
    bool pending_id_known=false,level_known=false;
};
struct MonsterBreathResult {
    CombatMagicResult magic;
    // 6DB9/A changes only on source ray reflections after the initial radial
    // reduction. It is owned tactical scratch, never an exploration position.
    CombatPosition cursor;
};
class MonsterBreathService {
    CombatMagicEffects effects_;
public:
    static MonsterBreathService load(const IFileSystem&);
    // Call against the enclosing AI candidate RNG BEFORE automatic targeting.
    // Round0 consumes no die; later rounds consume one native d100.
    static bool should_activate(std::uint8_t round_index,std::mt19937&);
    // Caller has passed the gate and acquired the native cursor. This service
    // stages all removals, saves, damage, charges and Quit atomically, including
    // immutable per-segment scenes. Exceptions preserve the input and RNG.
    MonsterBreathResult stage_after_gate(const DeploymentState&,
        const MonsterBreathContext&,const ItemCatalog&,const SpellCatalog&,
        std::mt19937&) const;
};
}
