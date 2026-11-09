#include "gbengine/magic/noncombat_periodic_effects.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
namespace GBEngine {
bool NoncombatPeriodicEffects::handles(std::uint8_t code) {
    return code==0x04 || code==0x07 || code==0x0f || code==0x16 || code==0x22 ||
        code==0x2b || code==0x2c || code==0x32 || code==0x39 || code==0x3b || code==0x3e || code==0x4e;
}
bool NoncombatPeriodicEffects::reverse(const EffectExpiryRequest& request,Character& target,EffectExpiryEdits& edits,const PeriodicEffectBindings& bindings) {
    if(!handles(request.effect.code())) return false;
    if(!target.effects_restored() || !request.effect_index || *request.effect_index>=target.effects().size() ||
       target.effects()[*request.effect_index]!=request.effect)
        throw DecodeError("Periodic expiry requires its exact owned node");
    const auto first=[&](std::uint8_t code)->std::optional<std::size_t> {
        for(std::size_t i=0;i<target.effects().size();++i) if(target.effects()[i].code()==code &&
            std::find(edits.remove_indices.begin(),edits.remove_indices.end(),i)==edits.remove_indices.end()) return i;
        return {};
    };
    const auto has=[&](std::uint8_t code) {
        return first(code).has_value() || std::any_of(edits.append.begin(),edits.append.end(),[&](const auto& node){return node.code()==code;});
    };
    const auto notice=[&](std::size_t offset) {
        edits.notices.push_back({request.member,pascal_string_id("GAME.OVR",offset)});
    };
    const auto append=[&](std::uint8_t code,std::uint16_t duration,std::uint8_t context,std::uint8_t flag,std::size_t offset) {
        auto node=request.effect;node.payload={code,0,0,context,flag};node.set_duration(duration);
        node.source_file="GAME.OVR";node.source_offset=offset;
        // Native callback nodes are fresh records, not equipped-item bindings.
        node.equipment_item_id.reset();edits.append.push_back(std::move(node));
    };
    const auto periodic=[&](std::uint8_t code) {
        const bool weakness=code==0x2b;
        append(code,weakness?60:10,request.effect.payload[3],1,weakness?0xfd39:code==0x0f?0xf246:0xfdc3);
        if(weakness) {
            if(target.byte(0x10)>=4) {
                notice(0xfd2d);target.set_byte(0x10,static_cast<std::uint8_t>(target.byte(0x10)-1));
            } else if(!has(0x1f)) append(0x1f,0,255,0,0xfd39);
        } else if(target.hit_points()>1) {
            if(!bindings.damage) throw DecodeError("Periodic damage requires the native damage-query binding");
            bindings.damage(target,1);
        } else if(code==0x2c && !has(0x1f)) append(0x1f,0,255,0,0xfdc3);
    };
    switch(request.effect.code()) {
    case 0x04:
        notice(0xedca);append(0x07,0xa8c0,255,1,0xedda);break;
    case 0x07: {
        notice(0xee83);
        const auto constitution=static_cast<std::uint8_t>(target.byte(0x14)+1);
        target.set_byte(0x14,constitution);
        if(constitution>=20) append(0x3e,60,255,1,0xee8e);
        else if(constitution>14) {
            std::uint8_t divisor=0;
            for(std::size_t i=0;i<8;++i) {
                const auto level=target.byte(0x96+i);if(!level || level>=128) continue;
                const auto contribution=i==2?(constitution-15)*level:constitution<16?level:2*level;
                divisor=static_cast<std::uint8_t>(divisor+contribution);
            }
            if(!divisor) divisor=1;
            const auto increase=static_cast<std::uint8_t>(target.byte(0x32)-target.byte(0xb1))/divisor;
            if(constitution<17 || (target.byte(0x98)>0 && target.byte(0x98)<128)) {
                target.set_byte(0x32,static_cast<std::uint8_t>(target.byte(0x32)+increase));
                target.set_byte(0x11b,static_cast<std::uint8_t>(target.hit_points()+increase));
            }
        }
        break;
    }
    case 0x0f:case 0x2b:case 0x2c:periodic(request.effect.code());break;
    case 0x22:periodic(0x2b);periodic(0x2c);break;
    case 0x16:
        if(has(0x37)) {
            notice(0xf3bf);
            if(!bindings.set_status) throw DecodeError("Poison expiry requires the native status-query binding");
            bindings.set_status(target,6,edits);
        }
        if(const auto at=first(0x0f)) edits.remove_indices.push_back(*at);
        break;
    case 0x32:case 0x3e:
        append(request.effect.code(),request.effect.code()==0x32?0x3840:60,request.effect.payload[3],1,request.effect.code()==0x32?0xff56:0x10146);
        if(!bindings.heal) throw DecodeError("Periodic regeneration requires the native healing binding");
        if(bindings.heal(target,1,true)) {
            if(!bindings.refresh_inventory) throw DecodeError("Periodic regeneration requires inventory refresh");
            bindings.refresh_inventory(target);
        }
        break;
    case 0x3b:append(0x62,0,255,0,0x100cd);break;
    case 0x39: {
        notice(0xffdd);
        target.set_byte(0x15,target.byte(0x15)<3?3:std::max(std::uint8_t{3},static_cast<std::uint8_t>(target.byte(0x15)-2)));
        const auto context=request.effect.payload[3];
        if((context>>4)<2) {
            notice(0xffe5);
            if(!bindings.set_status) throw DecodeError("Disease expiry requires the native status-query binding");
            bindings.set_status(target,8,edits);
        } else {
            const auto next=static_cast<std::uint8_t>(context-0x10);
            target.effects_[*request.effect_index].payload[3]=next;
            append(0x39,0xa8c0,next,1,0xfff4);
        }
        break;
    }
    case 0x4e:
        if(!bindings.recover) throw DecodeError("Periodic recovery requires the native placement binding");
        if(!bindings.recover(target,target.hit_points())) append(0x4e,1,request.effect.payload[3],1,0x10634);
        break;
    default:return false;
    }
    return true;
}
}
