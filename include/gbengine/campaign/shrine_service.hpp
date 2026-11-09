#pragma once
#include "gbengine/campaign/economy_service.hpp"
#include "gbengine/magic/magic_effect_service.hpp"

namespace GBEngine {
enum class ShrineAction {cure_blindness,cure_disease,cure_light,cure_serious,cure_critical,neutralize_poison,raise_dead,remove_curse,stone_to_flesh};
struct ShrineInspection {
    EconomyStatus status=EconomyStatus::unresolved;
    std::uint16_t fee{};
    bool needed=false;
    std::string question_id,service_id,message;
};
// Native24D3 healing services. Confirmation never charges; a confirmed action
// stages purse/pool, owned effects/character state and shared RNG together.
class ShrineService {
    Party& party_;
    CampaignResources& resources_;
    const SpellCatalog& spells_;
    const ItemCatalog& items_;
    static void remove(Character&,std::uint8_t,bool suppressed);
public:
    ShrineService(Party& party,CampaignResources& resources,const SpellCatalog& spells,const ItemCatalog& items)
        :party_(party),resources_(resources),spells_(spells),items_(items) {}
    static ShrineInspection inspect(const Party&,std::size_t,ShrineAction);
    EconomyResult apply(std::size_t,ShrineAction,std::mt19937&,bool confirmed_unneeded=false,bool confirmed_fee=false);
};
}
