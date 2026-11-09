#include "gbengine/world/script_damage_service.hpp"
#include <algorithm>
#include "gbengine/magic/saving_throw_service.hpp"
#include "gbengine/scripting/ecl_damage_mode.hpp"
#include "gbengine/resources/string_store.hpp"
namespace GBEngine {
ScriptDamageSaveContext ScriptDamageService::save_context(unsigned disk,std::uint8_t ecl,std::uint16_t instruction,
        std::span<const std::uint8_t> script) {
    // User-approved compatibility policy for the shared castle thorn handler.
    // It supplies a poison save, not inherited elemental combat scratch. The
    // authenticated leaf covers every castle map/hedge using this handler.
    constexpr std::size_t begin=0x9b99-0x9900,length=0x40;
    if(disk==5 && ecl==5 && instruction==0x9bc3 && script.size()>=begin+length &&
       sha256(script.subspan(begin,length))=="edd8a1edbbcc90106f942ae0da838ac4c47e1f5b7314112b4070c5a285c28a48")
        return ScriptDamageSaveContext::ordinary_poison;
    return ScriptDamageSaveContext::inherited;
}
std::vector<DamageOutcome> ScriptDamageService::attacks(Party& party,std::span<const std::uint16_t> v,std::mt19937& random) {
    return attacks(party,v,random,ScriptDamageSaveContext::inherited);
}
std::vector<DamageOutcome> ScriptDamageService::attacks(Party& party,std::span<const std::uint16_t> v,std::mt19937& random,
        ScriptDamageSaveContext context) {
    const auto decoded=decode_ecl_damage_mode(v);
    if(!decoded)throw DecodeError("Unsupported scripted damage category or operands");
    const auto mode=*decoded;
    if(context!=ScriptDamageSaveContext::inherited && context!=ScriptDamageSaveContext::ordinary_poison)
        throw DecodeError("Unknown scripted saving-throw context");
    if(context==ScriptDamageSaveContext::ordinary_poison && (!mode.save_category || *mode.save_category!=0))
        throw DecodeError("Ordinary poison context requires a poison-category save");
    if(!party.size() || party.size()>255 || !party.selected_index())throw DecodeError("Scripted attacks require a selected imported roster");
    if(v[1] && !v[2])throw DecodeError("Scripted damage dice require nonzero sides");
    auto staged=party;auto rng=random;
    const auto sample=[&](std::uint32_t bound) {
        const auto threshold=(std::uint32_t{0}-bound)%bound;
        std::uint32_t n;do{n=static_cast<std::uint32_t>(rng());}while(n<threshold);
        return n%bound;
    };
    const auto damage=[&]() {
        unsigned amount=0;
        for(unsigned i=0;i<v[1];++i)amount=(amount+sample(v[2])+1)&255;
        return static_cast<std::uint16_t>(amount+v[3]);
    };
    auto amount=damage();
    // Native consumes the initial ordinal even on selected/no-save paths,
    // but skips it whenever bit40 is set (including attack-count modes64..127).
    const auto initial_target=mode.initial_ordinal?sample(static_cast<std::uint32_t>(staged.size())):0u;
    if(mode.target!=EclDamageTarget::attacks) {
        std::vector<DamageOutcome> result;
        const std::size_t first=mode.target==EclDamageTarget::whole_roster?0:
            mode.target==EclDamageTarget::selected_member?*staged.selected_:initial_target;
        const auto end=mode.target==EclDamageTarget::whole_roster?staged.size():first+1;
        for(std::size_t index=first;index<end;++index) {
            bool saved=false;
            if(mode.save_category) {
                const bool elemental=std::any_of(staged.members_[index].effects().begin(),staged.members_[index].effects().end(),
                    [](const auto& e){return e.code()==0x0a || e.code()==0x14 || e.code()==0x3d;});
                if(!elemental || context==ScriptDamageSaveContext::ordinary_poison) {
                    saved=SavingThrowService::resolve(staged.members_,index,staged.members_[*staged.selected_],
                        *mode.save_category,static_cast<std::int8_t>(mode.modifier),rng,0).saved;
                } else {
                    saved=SavingThrowService::resolve_invariant(staged.members_,index,staged.members_[*staged.selected_],
                        *mode.save_category,static_cast<std::int8_t>(mode.modifier),rng);
                }
            }
            if(!saved) {
                const auto selected=staged.selected_;staged.selected_=index;
                result.push_back(staged.damage_selected(amount));staged.selected_=selected;
            }
        }
        ++staged.revision_;party=std::move(staged);random=std::move(rng);return result;
    }
    std::vector<DamageOutcome> outcomes;
    for(unsigned attack=0;attack<mode.attack_count;++attack) {
        const auto target=sample(static_cast<std::uint32_t>(staged.size()));
        auto& c=staged.members_[target];
        if(!c.effects_restored())throw DecodeError("Scripted accuracy requires restored effects");
        const auto die=sample(20)+1;
        auto adjusted=static_cast<std::uint8_t>(die==20?100:die);
        if(die>1) {
            // 49D5:0C4F calls phase16 only, not attacker/save callbacks.
            const auto first=[&](unsigned code)->CharacterEffect* {
                const auto at=std::find_if(c.effects_.begin(),c.effects_.end(),[&](const auto& e){return e.code()==code;});
                return at==c.effects_.end()?nullptr:&*at;
            };
            const auto& source=staged.members_[*staged.selected_]; // Native global5D92 never changes to the random victim.
            if(first(0x25) || (first(0x2f) && source.byte(0x9f)==1 && (source.byte(0x6c)&127)==2) ||
               (first(0x30) && source.byte(0x9f)==1))
                throw DecodeError("Scripted defense requires an unresolved native attacker/extension context");
            if(first(0x19))adjusted=static_cast<std::uint8_t>(adjusted-4);
            if(first(0x47))adjusted=static_cast<std::uint8_t>(adjusted-4);
            if(auto* ward=first(0x59);ward && !(ward->payload[3]&0x10)) {ward->payload[3]|=0x10;adjusted=255;}
            // Signed result must be nonnegative; strict > defense, not >=.
            if(adjusted<128 && static_cast<unsigned>(adjusted)+v[4]>c.byte(0x111)) {
                const auto selected=staged.selected_;staged.selected_=target;
                outcomes.push_back(staged.damage_selected(amount));staged.selected_=selected;
            }
        }
        amount=damage(); // Includes the unused roll after the final arrow.
    }
    ++staged.revision_;party=std::move(staged);random=std::move(rng);return outcomes;
}
}
