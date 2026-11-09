#include "gbengine/campaign/wealth_loss_service.hpp"
#include "gbengine/magic/equipment_effect_service.hpp"
#include "gbengine/numerics/real48_wealth.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
WealthLossResult unresolved(std::string message) {return {WealthLossStatus::unresolved,std::move(message),0};}
unsigned d100(std::mt19937& random) {
    // Same modern unbiased dice mapping as VM DAMAGE and magic services.
    constexpr auto bound=std::uint32_t{100};
    constexpr auto threshold=(std::uint32_t{0}-bound)%bound;
    std::uint32_t sample;do {sample=static_cast<std::uint32_t>(random());} while(sample<threshold);
    return sample%bound+1;
}
}
WealthLossResult WealthLossService::apply(Party& party,std::uint8_t target,std::uint8_t percent,std::mt19937& random) {
    if(!party.size() || (target==0 && !party.selected_index()))
        return unresolved("Wealth loss requires a restored party and actual selected member");
    try {
        auto staged=party;
        auto staged_random=random;
        const auto revision=party.revision();
        const auto first=target==0?*party.selected_index():0;
        const auto end=target==0?first+1:party.size();
        std::size_t lost=0;
        EquipmentEffectService equipment(&staged.item_catalog());
        const NativeReal48::WealthRetention retention(percent);
        for(std::size_t member=first;member<end;++member) {
            for(std::size_t money=0;money<7;++money) {
                auto& character=staged.members_[member];
                character.set_word(0x88+2*money,retention.apply(character.word(0x88+2*money)));
            }
            unsigned chance=percent;
            for(std::size_t item_index=0;item_index<staged.member(member).inventory().size();) {
                const auto& item=staged.member(member).inventory().item(item_index);
                const auto weight=item.unit_weight();
                const auto penalty=weight>255?90u:weight>24?50u:0u;
                chance=chance>penalty?chance-penalty:0;
                const auto roll=d100(staged_random); // Always draw, including zero chance.
                if(roll>chance) {++item_index;continue;}
                const auto id=item.id();
                if(item.readied()) {
                    if(item.cursed()) return unresolved("Forced removal of cursed equipment is not implemented");
                    const auto unready=InventoryService(staged).ready(member,id,&equipment);
                    if(!unready.applied()) return unresolved(unready.message);
                }
                // A stale owned effect on an unreadied record must not be
                // silently orphaned by direct record removal.
                auto& character=staged.members_[member];
                const auto owned=std::find_if(character.inventory_.items_.begin(),character.inventory_.items_.end(),[&](const auto& entry){return entry.id()==id;});
                if(owned==character.inventory_.items_.end()) {++lost;continue;}
                if(character.bound_equipment_effects_.contains(id) ||
                   std::any_of(character.effects().begin(),character.effects().end(),[&](const auto& effect){return effect.equipment_item_id==id;}))
                    return unresolved("Lost item retains an unresolved equipment-effect binding");
                character.inventory_.items_.erase(owned);
                ++lost;
            }
            EquipmentRules::recalculate(staged.members_[member],staged.item_catalog());
        }
        if(party.revision()!=revision) return unresolved("Party changed while wealth loss was staged");
        staged.revision_=revision+1;
        party=std::move(staged);random=std::move(staged_random);
        return {WealthLossStatus::applied,{},lost};
    } catch(const DecodeError& error) {return unresolved(error.what());}
}
}
