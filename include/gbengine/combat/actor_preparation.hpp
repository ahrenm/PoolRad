#pragma once
#include "gbengine/combat/deployment.hpp"
#include "gbengine/items/item_catalog.hpp"

namespace GBEngine {
struct CombatRoundNotice;
struct CombatRoundContext;
struct CombatSelectionStep;
class SpellCatalog;
// Native combat entry and actor-selection caches operate on the owned tactical
// copies. No durable Party state, shared RNG or ECL completion is implicit.
class CombatActorPreparation {
    static void entry_mummy_fear(DeploymentState&,std::size_t,std::optional<CombatActorIdentity>,std::mt19937*,std::vector<CombatSelectionStep>*);
    static void entry_fear(DeploymentState&,std::size_t,std::optional<CombatActorIdentity>,std::mt19937*,std::vector<CombatSelectionStep>*);
    static void initial_in_place(DeploymentState&,const ItemCatalog&,std::mt19937*,std::optional<CombatActorIdentity>,std::vector<CombatSelectionStep>*,const SpellCatalog*);
    static void actor_turn_in_place(DeploymentState&,std::size_t,const ItemCatalog&,std::vector<CombatRoundNotice>*,
        const CombatRoundContext*,std::mt19937*,std::vector<CombatSelectionStep>*);
public:
    // 2C86:12E1 scalar refresh, then 2C86:1E31 ordered phase8 callbacks.
    static void initial(DeploymentState&,const ItemCatalog&);
    static void initial(DeploymentState&,const ItemCatalog&,std::mt19937&,std::optional<CombatActorIdentity> entry_selected={},
        std::vector<CombatSelectionStep>* entry_playback=nullptr,const SpellCatalog* spells=nullptr);
    // 29CB:01F6 phase7, positive-initiative scalar refresh, then phase15.
    // Unsupported present callbacks fail with the entire candidate untouched.
    static void actor_turn(DeploymentState&,std::size_t,const ItemCatalog&);
    static void actor_turn(DeploymentState&,std::size_t,const ItemCatalog&,std::vector<CombatRoundNotice>* notices);
    static void actor_turn(DeploymentState&,std::size_t,const ItemCatalog&,std::vector<CombatRoundNotice>*,
        const CombatRoundContext*,std::mt19937&,std::vector<CombatSelectionStep>&);
};
}
