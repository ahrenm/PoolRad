#include "gbengine/magic/magic_effect_service.hpp"
#include "gbengine/magic/noncombat_periodic_effects.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
namespace GBEngine {
MagicResult MagicEffectService::resolve_poison_magic(const MagicRequest& request,std::span<Character> members,std::mt19937& random) {
    if(request.spell.value!=26) return {MagicStatus::unresolved,"Unknown poison-recovery handler",{}};
    auto& target=members[request.target.id];
    MagicResult result{MagicStatus::applied,{},{}};
    // Native excludes status1 before even querying the poisoned marker.
    if(target.status()==1 || !has_effect(target,0x37)) return result;
    if(!target.hit_points()) target.set_byte(0x11b,1);
    const auto& definition=catalog_.definition(request.spell);
    const bool accepted=append_marker(target,definition,request,random,255,1);
    result.feedback.push_back({request.target.id,pascal_string_id("GAME.OVR",accepted?0x28867:0x2c532),accepted});
    std::vector<EffectNotice> notices;
    NativeEffectContext context{items_,&random,execution_mode_,&catalog_,&notices,request.target.id};
    const auto bindings=periodic_bindings(context);
    if(!bindings.recover || !bindings.recover(target,target.hit_points()))
        throw DecodeError("Suppression recovery would require native null-node retry context");
    for(const auto& notice:notices) result.feedback.push_back({notice.member,notice.text_id});
    if(target.effects_.size()>=65535) throw DecodeError("Effect record count exceeds native bound");
    CharacterEffect bleed;bleed.payload={0x0f,10,0,255,1};
    bleed.source_file="GAME.OVR";bleed.source_offset=0x28873;
    bleed.spell_id=request.spell.value;bleed.item_origin=request.item_origin;
    target.effects_.push_back(std::move(bleed));
    return result;
}
MagicResult MagicEffectService::resolve_reading_item(const ItemMagicRequest& request,std::span<Character> members,std::mt19937& random) {
    auto& target=members[request.target.id];
    const auto& definition=catalog_.item_definition(request.cast);
    MagicResult result{MagicStatus::applied,{},{}};
    // 4612:07D6 special-cases cast67 to1440. Its metadata duration bytes are
    // zero; its source text and04/07 callbacks describe reading then training.
    constexpr std::uint16_t duration=1440;
    if(application_queries(target,definition.effect_code(),12,random)) {
        std::vector<EffectNotice> notices;
        const auto old=std::find_if(target.effects_.begin(),target.effects_.end(),[&](const auto& effect){return effect.code()==definition.effect_code();});
        if(old!=target.effects_.end() && old->word_1() && old->word_1()<duration)
            remove_effect_at(target,static_cast<std::size_t>(old-target.effects_.begin()),{items_,&random,execution_mode_,&catalog_,&notices,request.target.id});
        for(const auto& notice:notices) result.feedback.push_back({notice.member,notice.text_id});
        if(target.effects_.size()>=65535) throw DecodeError("Effect record count exceeds native bound");
        CharacterEffect reading;reading.payload={definition.effect_code(),0,0,255,1};reading.set_duration(duration);
        reading.source_file="START.EXE:module";reading.source_offset=definition.provenance().module_offset;
        reading.item_origin=request.item_origin;reading.item_cast_id=request.cast.value;
        target.effects_.push_back(std::move(reading));
        result.feedback.push_back({request.target.id,pascal_string_id("GAME.OVR",0x2a0af),true});
    } else result.feedback.push_back({request.target.id,pascal_string_id("GAME.OVR",0x2c532)});
    return result;
}
}
