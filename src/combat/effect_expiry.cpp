#include "gbengine/combat/effect_expiry.hpp"
#include "gbengine/magic/magic_effect_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/combat/cloud_service.hpp"
#include "gbengine/combat/health_service.hpp"
#include "gbengine/magic/noncombat_periodic_effects.hpp"
#include "gbengine/magic/noncombat_damage_service.hpp"
#include "gbengine/party/equipment.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
std::uint8_t d100(std::mt19937& random) {
    constexpr std::uint32_t sides=100,threshold=(std::uint32_t{0}-sides)%sides;
    std::uint32_t value;do {value=static_cast<std::uint32_t>(random());}while(value<threshold);
    return static_cast<std::uint8_t>(value%sides+1);
}
}
bool CombatEffectExpiry::reverse(const EffectExpiryRequest& request,Character& target) {
    EffectExpiryEdits edits;
    if(!reverse_with_edits(request,target,edits))return false;
    if(!edits.append.empty() || !edits.remove_indices.empty() || !edits.notices.empty() || !edits.spell_slot_consumptions.empty())
        throw DecodeError("Combat effect expiry requires its callback journal");
    return true;
}
bool CombatEffectExpiry::reverse_with_edits(const EffectExpiryRequest& request,Character& target,EffectExpiryEdits& edits) {
    if(request.member>=indices_.size() || indices_[request.member]>=state_.actors.size() ||
       state_.actors[indices_[request.member]].removed || !request.effect_index ||
       *request.effect_index>=target.effects().size() || target.effects()[*request.effect_index]!=request.effect)
        throw DecodeError("Combat effect expiry actor or effect identity is stale");
    const auto code=request.effect.code();
    const auto recover=[&](std::uint8_t hp) {
        const auto ai=indices_[request.member];auto next=state_;next.actors[ai].character=target;
        const auto original=target.effects_;
        if(!CombatMagicEffects::revive_in_place(next,next.actors[ai].id,hp,spells_))return false;
        target=next.actors[ai].character;target.effects_=original;
        state_.actors[ai]=std::move(next.actors[ai]);state_.actors[ai].character=target;
        state_.battlefield=std::move(next.battlefield);state_.occupancy=next.occupancy;state_.bodies=std::move(next.bodies);
        edits.notices.push_back({request.member,pascal_string_id("GAME.OVR",target.byte(0x10e)==1?182066:182086)});
        return true;
    };
    const auto terminal=[&](std::uint8_t status) {
        const auto original=target.effects_;const auto ai=indices_[request.member];
        // The clock's other Character copies do not own cross-actor edits.
        //Retained links and recursiveAnimate cleanup need a broader journal.
        for(const auto& e:original)if(e.code()==0x4a || e.code()==0x4b || (e.code()==0x20 && e.payload[4]))
            throw DecodeError("Combat periodic terminal cleanup requires cross-actor callback ownership");
        auto next=state_;next.actors[ai].character=target;
        if(code==0x5f)next.actors[ai].character.effects_[*request.effect_index].payload[4]=0;
        const auto active=target.active();
        std::vector<CombatDefeatNotice> terminal_notices;
        CombatHealthService::apply_terminal_status_from_module_in_place(next,next.actors[ai].id,status,spells_.native_module(),&terminal_notices);
        for(const auto& notice:terminal_notices) {
            if(notice.actor!=next.actors[ai].id)throw DecodeError("Combat periodic notice requires another actor's ownership");
            edits.notices.push_back({request.member,notice.text_id});
        }
        if(target.status()!=7 && target.status()!=6 && target.status()!=8) {
            std::vector<bool> removed(original.size());
            for(const auto cleanup:spells_.native_module().subspan(0xd3d4,16))
                for(std::size_t i=0;i<original.size();++i)if(!removed[i] && original[i].code()==cleanup) {removed[i]=true;break;}
            std::size_t kept=0;const auto& after=next.actors[ai].character.effects_;
            for(std::size_t i=0;i<original.size();++i)if(removed[i]) {
                if(i!=*request.effect_index)edits.remove_indices.push_back(i);
            } else {
                auto expected=original[i];if(code==0x5f && i==*request.effect_index)expected.payload[4]=0;
                if(kept>=after.size() || after[kept]!=expected)
                    throw DecodeError("Combat periodic cleanup changed its native saved-node traversal");
                ++kept;
            }
            edits.append.insert(edits.append.end(),after.begin()+static_cast<std::ptrdiff_t>(kept),after.end());
        }
        target=next.actors[ai].character;target.effects_=original;
        state_.actors[ai]=std::move(next.actors[ai]);state_.actors[ai].character=target;
        state_.battlefield=std::move(next.battlefield);state_.occupancy=next.occupancy;state_.bodies=std::move(next.bodies);
        return active && !target.active();
    };
    if(code==0x5f) {
        if(target.active()) {
            edits.notices.push_back({request.member,pascal_string_id("GAME.OVR",60507+0x2605)});
            (void)terminal(6);
        }
        return true;
    }
    if(code==0x3b) {
        //2EE2:1472 appends fresh62 on delayed-regeneration expiry; no RNG.
        CharacterEffect node;node.payload={0x62,0,0,255,0};
        node.source_file="GAME.OVR";node.source_offset=0x100cd;
        edits.append.push_back(std::move(node));return true;
    }
    if(code==0x66) {
        if(!recover(target.byte(0x32))) {
            CharacterEffect node;node.payload={0x66,1,0,request.effect.payload[3],1};
            node.source_file="GAME.OVR:2EE2:2866";node.source_offset=60507+0x2866;
            edits.append.push_back(std::move(node));
        }
        return true;
    }
    if(NoncombatPeriodicEffects::handles(code)) {
        const PeriodicEffectBindings bindings{
            [&](Character& c,std::uint8_t amount) {
                auto& actor=state_.actors[indices_[request.member]];
                const auto interrupted=amount?actor.extension[0]:std::uint8_t{0};
                std::optional<std::size_t> resource_index,resource_member;
                if(interrupted) {
                    if(!resource_actor_)throw DecodeError("Combat periodic interruption requires its selected resource actor");
                    for(std::size_t i=0;i<state_.actors.size();++i)if(!state_.actors[i].removed && state_.actors[i].id==*resource_actor_) {
                        if(resource_index)throw DecodeError("Combat periodic selected resource actor is ambiguous");
                        resource_index=i;
                    }
                    if(!resource_index)throw DecodeError("Combat periodic selected resource actor is stale");
                    for(std::size_t i=0;i<indices_.size();++i)if(indices_[i]==*resource_index) {
                        if(resource_member)throw DecodeError("Combat periodic resource member mapping is ambiguous");
                        resource_member=i;
                    }
                    if(!resource_member)throw DecodeError("Combat periodic selected resource actor is outside the retained roster");
                }
                //Periodic0F/2C call1344 with flags0 and HP>1. The shared
                //bounded resolver guards inherited scratch before any edits.
                NoncombatDamageService::apply(c,amount,0,&random_,&edits);
                if(amount)actor.extension[1]=0;
                if(amount==1)edits.notices.push_back({request.member,pascal_string_id("GAME.OVR",0x2c18b)});
                if(interrupted) {
                    auto& cache=state_.actors[*resource_index].character;
                    auto& resources=*resource_index==indices_[request.member]?c:cache;
                    for(std::uint8_t slot=0x17;slot<=0x2b;++slot)if(resources.byte(slot)==interrupted) {
                        if(*resource_index==indices_[request.member]) {
                            if(cache.byte(slot)!=interrupted)throw DecodeError("Combat periodic resource cache disagrees with current spell slot");
                            c.set_byte(slot,0);
                        } else edits.spell_slot_consumptions.push_back({*resource_member,slot,interrupted});
                        //Only this parent-staged cache is changed here. The
                        //clock validates/applies the exact Party write later.
                        cache.set_byte(slot,0);break;
                    }
                    actor.extension[0]=0;
                    edits.notices.push_back({request.member,pascal_string_id("GAME.OVR",0x2c1de)});
                }
            },
            [&](Character& c,std::uint8_t amount,bool force) {
                const auto status=c.status();if(status!=0 && status!=1 && status!=4 && status!=5)return false;
                if(force?c.hit_points()>=c.byte(0x32):std::any_of(c.effects().begin(),c.effects().end(),[](const auto& e){return e.code()==0x32;}))return false;
                c.set_byte(0x11b,std::min(c.byte(0x32),static_cast<std::uint8_t>(c.hit_points()+amount)));
                if(!c.active() && status==5)c.set_byte(0x10c,4);
                //49D5:175D explicitly does not invoke4E revival in mode5.
                return true;
            },
            [&](Character&,std::uint8_t hp){return recover(hp);},
            [&](Character&,std::uint8_t status,EffectExpiryEdits&){(void)terminal(status);},
            [&](Character& c){EquipmentRules::recalculate(c,items_);}
        };
        return NoncombatPeriodicEffects::reverse(request,target,edits,bindings);
    }
    if(code==0x1c) {
        //Retained quiescent zero-pending projection, not an authenticated
        //reset of resident681B/6820. Cross-action/load ownership is a C07
        //policy/evidence park. Preserve the existing ordered draw here.
        const auto sides=static_cast<std::uint32_t>(request.effect.payload[3])+1;
        const auto threshold=(std::uint32_t{0}-sides)%sides;
        std::uint32_t value;do {value=static_cast<std::uint32_t>(random_());}while(value<threshold);
        return true;
    }
    if(code==0x20) {
        const auto ai=indices_[request.member];
        const auto original=target.effects_;
        const bool killed=target.status()!=7 && target.status()!=6 && target.status()!=8;
        auto next=state_;next.actors[ai].character=target;
        CombatHealthService::reverse_animate_dead_in_place(next,next.actors[ai].id,*request.effect_index,spells_);
        // Terminal cleanup selects the first remaining occurrence of each
        // source table code. Preserve these exact original indices, including
        // duplicates, instead of trying to infer ownership from equal payloads.
        if(killed) {
            std::vector<bool> removed(original.size());
            const auto module=spells_.native_module();
            for(const auto cleanup:module.subspan(0xd3d4,16))
                for(std::size_t i=0;i<original.size();++i)if(!removed[i] && original[i].code()==cleanup) {
                    removed[i]=true;
                    if(i!=*request.effect_index)edits.remove_indices.push_back(i);
                    break;
                }
            edits.notices.push_back({request.member,pascal_string_id("GAME.OVR",0xf76c)});
        }
        target=next.actors[ai].character;target.effects_=original;
        next.actors[ai].character=target;
        // Keep actor/vector addresses stable: explicit removal may supply a
        // Character reference into state_ itself rather than a clock copy.
        state_.actors[ai]=std::move(next.actors[ai]);
        state_.battlefield=std::move(next.battlefield);
        state_.occupancy=next.occupancy;state_.bodies=std::move(next.bodies);
        return true;
    }
    if(code==0x28) {
        const auto& actor=state_.actors[indices_[request.member]];
        if(CombatCloudService::expire_in_place(state_,actor.id,static_cast<std::uint8_t>(request.effect.payload[3]>>4)))
            edits.notices.push_back({request.member,pascal_string_id("GAME.OVR",0xf903)});
        return true;
    }
    if(code==0x33 || code==0x34 || code==0x35 || code==0x1f) {
        auto& actor=state_.actors[indices_[request.member]];
        for(const auto offset:{3u,0u,7u,6u})actor.extension[offset]=0;
        return true;
    }
    if(code==0x3a) {
        //2EE21452 ignores apply/reverse mode. Its persistent combat write is
        //movement6=0; the optional681A query scratch is not active at expiry.
        state_.actors[indices_[request.member]].extension[6]=0;
        return true;
    }
    if(code==0x27) {
        if(!(request.effect.payload[3]&16)) {
            // Native handler0C4B ignores remove/apply mode; its first call
            // still ages once even when that first call is final expiration.
            auto& node=target.effects_[*request.effect_index];
            node.payload[3]=static_cast<std::uint8_t>(node.payload[3]+16);
            target.set_word(0x30,static_cast<std::uint16_t>(target.word(0x30)+1));
            edits.notices.push_back({request.member,pascal_string_id("GAME.OVR",0xf8a1)});
        }
        return true;
    }
    if(code==0x38 || code==0x48) {
        if(code==0x48 && d100(random_)>=96)return true;
        CharacterEffect node;node.payload={0x19,1,0,12,0};node.source_file="GAME.OVR";
        node.source_offset=code==0x38?0xffbe:0x1038b;edits.append.push_back(std::move(node));return true;
    }
    if(code==1 || code==2 || code==3 || code==6 || code==0x1a || code==0x12 || code==0x25 ||
       code==0x2f || code==0x30 || code==0x31 || code==0x3d || code==0x59 || code==0x19 || code==0x36)
        // Verified handlers affect only transient query scratch. This is an
        // expiry callback, with no pending hit/save/damage query to publish.
        return true;
    if(code==0x11) {
        if(target.byte(0x111)<57)target.set_byte(0x111,57);
        return true;
    }
    // Existing reversal helper has exact context-independent stat ownership
    // and generated-hammer reversal paths. Execution5 is explicit, with no
    // fabricated exploration context or generic periodic damage callback.
    return MagicEffectService::reverse_effect(target,request.effect,request.effect_index,
                                              {&items_,&random_,5,&spells_});
}
}
