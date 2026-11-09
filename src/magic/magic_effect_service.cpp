#include "gbengine/magic/magic_effect_service.hpp"
#include "gbengine/party/equipment.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/magic/noncombat_periodic_effects.hpp"
#include "gbengine/magic/noncombat_damage_service.hpp"
#include "gbengine/party/character_recovery_service.hpp"
#include <algorithm>
namespace GBEngine {
bool MagicEffectService::has_effect(const Character& character,std::uint8_t code) {
    if(!character.effects_restored()) throw DecodeError("Effect query requires a bound effect list");
    return std::any_of(character.effects().begin(),character.effects().end(),[&](const CharacterEffect& e){return e.code()==code;});
}
std::optional<bool> MagicEffectService::party_detects_magic(const Party& party) {
    if(!party.size()) return std::nullopt;
    bool found=false,unbound=false;
    for(std::size_t i=0;i<party.size();++i) {
        const auto& member=party.member(i);
        if(!member.effects_restored()) unbound=true;
        else found=found || has_effect(member,5);
    }
    if(found) return true;
    return unbound?std::nullopt:std::optional<bool>{false};
}
std::uint32_t MagicEffectService::roll(std::mt19937& random,std::uint32_t sides) {
    const auto threshold=(std::uint32_t{0}-sides)%sides;
    std::uint32_t value;do {value=static_cast<std::uint32_t>(random());} while(value<threshold);
    return value%sides+1;
}
bool MagicEffectService::application_queries(Character& target,std::uint8_t incoming,std::uint8_t level,std::mt19937& random) {
    // 49D5:02FA phase9 runs the entire ordered family, even after cancellation.
    // 69/6A skip their draw once pending code and damage flags are both zero;
    // 6B/7C always draw. This exploration path has no elemental damage flags.
    constexpr std::array<std::uint8_t,10> order{0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,0x70,0x7c,0x7d};
    for(const auto code:order) if(has_effect(target,code)) {
        if(code==0x69 || code==0x6a) {
            const auto threshold=static_cast<std::uint8_t>((code==0x69?50:15)-(11-static_cast<int>(level))*5);
            if(incoming && roll(random,100)<=threshold) incoming=0;
        } else if(code==0x6b || code==0x7c) {
            const auto result=roll(random,100);
            if(result<=(code==0x6b?90u:30u) && (incoming==0x0b || incoming==0x35)) incoming=0;
        } else if(code==0x6c) {if(incoming==0x0b || incoming==0x35) incoming=0;}
        else if(code==0x6d) {if(incoming==0x34) incoming=0;}
        else if(code==0x6f) {if(incoming==0x37 || incoming==0x34) incoming=0;}
        else if(code==0x7d) {if(incoming==0x0b || incoming==0x35 || incoming==0x34 || incoming==0x37) incoming=0;}
        // 6E/70 affect only elemental damage flags, absent in these spells.
    }
    return incoming!=0;
}
bool MagicEffectService::append_marker(Character& target,const SpellDefinition& definition,const MagicRequest& request,std::mt19937& random,std::uint8_t context,std::uint8_t reversal) {
    const auto code=definition.effect_code();
    if(!application_queries(target,code,request.caster_level,random)) return false;
    const auto duration=static_cast<std::uint16_t>(definition.duration_base()+definition.duration_per_level()*request.caster_level);
    auto& effects=target.effects_;
    const auto old=std::find_if(effects.begin(),effects.end(),[&](const CharacterEffect& e){return e.code()==code;});
    if(old!=effects.end() && ((!duration) || (old->word_1()!=0 && old->word_1()<duration))) {
        remove_effect_at(target,static_cast<std::size_t>(old-effects.begin()),{items_,&random,execution_mode_,&catalog_});
    }
    if(effects.size()>=65535) throw DecodeError("Effect record count exceeds native bound");
    CharacterEffect effect;effect.payload={code,0,0,context,reversal};effect.set_duration(duration);
    effect.source_file="START.EXE:module";
    effect.source_offset=definition.provenance().module_offset;
    effect.spell_id=definition.id().value;effect.item_origin=request.item_origin;
    // Runtime provenance points to the native definition, never to a fabricated SPC save.
    effects.push_back(std::move(effect));
    return true;
}
namespace {
std::uint8_t encode_strength(std::uint8_t strength,std::uint8_t exceptional) {
    return static_cast<std::uint8_t>(strength==18?exceptional+1:strength+100);
}
std::pair<std::uint8_t,std::uint8_t> decode_strength(std::uint8_t context) {
    const auto value=static_cast<std::uint8_t>(context&127);
    return value<102?std::pair<std::uint8_t,std::uint8_t>{std::uint8_t{18},static_cast<std::uint8_t>(value-1)}:
        std::pair<std::uint8_t,std::uint8_t>{static_cast<std::uint8_t>(value-100),std::uint8_t{0}};
}
}
std::uint8_t MagicEffectService::install_strength(Character& target,std::uint8_t strength,std::uint8_t exceptional) {
    if(!target.effects_restored()) throw DecodeError("Strength installation requires a bound effect list");
    const auto old_strength=target.byte(0x10),old_exceptional=target.byte(0x16);
    if(strength<old_strength || (strength==18 && exceptional<old_exceptional))
        return static_cast<std::uint8_t>(encode_strength(strength,exceptional)+128);
    auto saved_strength=old_strength,saved_exceptional=old_exceptional;
    const auto existing=std::find_if(target.effects_.begin(),target.effects_.end(),[](const auto& e){return (e.code()==0x0c || e.code()==0x26) && e.payload[3]<128;});
    if(existing!=target.effects_.end()) {
        const auto original=decode_strength(existing->payload[3]);
        saved_strength=original.first;saved_exceptional=original.second;
        existing->payload[3]=static_cast<std::uint8_t>(encode_strength(old_strength,old_exceptional)+128);
    }
    target.set_byte(0x10,strength);target.set_byte(0x16,exceptional);
    return encode_strength(saved_strength,saved_exceptional);
}
bool MagicEffectService::reverse_native(Character& target,const CharacterEffect& effect,std::optional<std::size_t> index,bool suppress_periodic,NativeEffectContext context) {
    const auto code=effect.code();
    if(code==0x0b) {
        //2EE20400 removal restores the captured side and PC84 marker, while
        // deliberately retaining automatic/manual control10F.
        target.set_byte(0x10e,static_cast<std::uint8_t>((effect.payload[3]&0x40)>>6));
        if(target.byte(0x84)==0xb3)target.set_byte(0x84,0);
        return true;
    }
    if(code==0x17)return context.items && reverse_hammer(target,*context.items);
    if(code==0x0e) {target.set_byte(0x15,effect.payload[3]);return true;}
    if(code==0x0c || code==0x26) {
        if(effect.payload[3]>=128) return true;
        const auto original=decode_strength(effect.payload[3]);
        target.set_byte(0x10,original.first);target.set_byte(0x16,original.second);
        std::optional<std::size_t> winner;std::uint8_t best=0,exceptional=0;
        for(std::size_t i=0;i<target.effects_.size();++i) {
            if(index && i==*index) continue;
            const auto& other=target.effects_[i];if(other.code()!=0x0c && other.code()!=0x26) continue;
            const auto value=decode_strength(other.payload[3]);
            if(best<value.first || (best==18 && exceptional<value.second)) {
                best=value.first;exceptional=value.second;winner=i;
            }
        }
        if(winner) {target.effects_[*winner].payload[3]=encode_strength(original.first,original.second);target.set_byte(0x10,best);target.set_byte(0x16,exceptional);}
        return true;
    }
    if(code==0x21) {target.set_byte(0x111,static_cast<std::uint8_t>(target.byte(0x111)-4));target.set_byte(0x112,static_cast<std::uint8_t>(target.byte(0x112)-4));return true;}
    // The native disease-cure handler sets681F to suppress periodic scheduling/damage and
    // disease charisma loss while removing these exact nodes.
    if(suppress_periodic && (code==0x04 || code==0x0f || code==0x16 || code==0x22 || code==0x2b || code==0x2c || code==0x32 || code==0x39)) return true;
    switch(code) {
        case 0x28:
            // Native29CB0000 empties the cloud list before quiescent return;
            // 2EE20CC3 then finds no owned cloud record outside combat. Its
            // tactical terrain callback stays bound only in execution5.
            return context.execution_mode==1 || context.execution_mode==2;
        case 1:case 2:case 3:case 6:case 0x12:case 0x1a:case 0x25:
        case 0x2f:case 0x30:case 0x31:case 0x3d:case 0x59:
            // Verified query scratch handlers have no persistent callback
            // outside combat; retained native target/aura scratch is not a
            // noncombat actor state. Combat must bind its own phase context.
            return context.execution_mode==1 || context.execution_mode==2;
        case 5:case 0x0d:case 0x10:case 0x13:case 0x18:case 0x37: // Verified empty handlers.
        case 8:case 9:case 0x0a:case 0x14:case 0x19:case 0x24:case 0x2a:case 0x2d:case 0x2e:
            // These callbacks modify only per-query combat scratch in this
            // domain; no owned character/effect mutation occurs on reversal.
            return true;
        default:return false;
    }
}
bool MagicEffectService::reverse_effect(Character& target,const CharacterEffect& effect,std::optional<std::size_t> index,NativeEffectContext context) {
    if(!target.effects_restored()) return false;
    if(!index) {
        const auto it=std::find(target.effects_.begin(),target.effects_.end(),effect);
        if(it!=target.effects_.end()) index=static_cast<std::size_t>(it-target.effects_.begin());
    }
    if(!index || *index>=target.effects_.size() || target.effects_[*index]!=effect) return false;
    return reverse_native(target,effect,index,false,context);
}
bool MagicEffectService::reverse(const EffectExpiryRequest& request,Character& target) {
    return reverse_effect(target,request.effect,request.effect_index,{items_,random_,execution_mode_,&catalog_});
}
bool MagicEffectService::remove_native(Character& target,std::size_t index,bool suppress_periodic,NativeEffectContext context) {
    if(index>=target.effects_.size()) throw DecodeError("Effect removal requires an owned node");
    const auto effect=target.effects_[index];
    if(suppress_periodic && effect.code()==0x16 && effect.payload[4]) {
        // 16 still removes the first bleeding0F while681F suppresses its
        // terminal poison callback and the removed0F's recurrence.
        const auto bleed=std::find_if(target.effects_.begin(),target.effects_.end(),[](const auto& node){return node.code()==0x0f;});
        if(bleed!=target.effects_.end()) {
            const auto at=static_cast<std::size_t>(bleed-target.effects_.begin());
            target.effects_.erase(bleed);if(at<index)--index;
        }
    }
    if(effect.payload[4] && NoncombatPeriodicEffects::handles(effect.code()) && !suppress_periodic) {
        EffectExpiryEdits edits;
        context.edits=&edits;
        if(!NoncombatPeriodicEffects::reverse({context.member,effect,index},target,edits,periodic_bindings(context)))
            throw DecodeError("Periodic effect removal is unresolved");
        edits.remove_indices.push_back(index);
        std::sort(edits.remove_indices.begin(),edits.remove_indices.end());
        edits.remove_indices.erase(std::unique(edits.remove_indices.begin(),edits.remove_indices.end()),edits.remove_indices.end());
        for(auto it=edits.remove_indices.rbegin();it!=edits.remove_indices.rend();++it) {
            if(*it>=target.effects_.size())throw DecodeError("Invalid periodic removal index");
            target.effects_.erase(target.effects_.begin()+static_cast<std::ptrdiff_t>(*it));
        }
        target.effects_.insert(target.effects_.end(),edits.append.begin(),edits.append.end());
        if(context.notices)context.notices->insert(context.notices->end(),edits.notices.begin(),edits.notices.end());
    } else {
        if(effect.payload[4] && !reverse_native(target,effect,index,suppress_periodic,context))
            throw DecodeError("Effect removal reversal unresolved for code "+std::to_string(effect.code()));
        target.effects_.erase(target.effects_.begin()+static_cast<std::ptrdiff_t>(index));
    }
    return true;
}
bool MagicEffectService::remove_effect_at(Character& target,std::size_t index,NativeEffectContext context) {return remove_native(target,index,false,context);}
bool MagicEffectService::remove_effect(Character& target,std::uint8_t code,NativeEffectContext context) {
    if(!target.effects_restored()) throw DecodeError("Effect removal requires a bound effect list");
    const auto it=std::find_if(target.effects_.begin(),target.effects_.end(),[&](const auto& e){return e.code()==code;});
    return it!=target.effects_.end() && remove_native(target,static_cast<std::size_t>(it-target.effects_.begin()),false,context);
}
void MagicEffectService::haste_activation_queries(Character& target) {
    // This is native query family hex12 (decimal18), not decimal12. Order is
    // haste27, slow2A, combat-context3A. Attack-rate scratch is not a persistent
    // character field. Haste's one-time age/context mutation is persistent.
    const auto haste=std::find_if(target.effects_.begin(),target.effects_.end(),[](const auto& e){return e.code()==0x27;});
    if(haste!=target.effects_.end() && !(haste->payload[3]&0x10)) {
        haste->payload[3]=static_cast<std::uint8_t>(haste->payload[3]+0x10);
        target.set_word(0x30,static_cast<std::uint16_t>(target.word(0x30)+1));
    }
    if(has_effect(target,0x3a)) throw DecodeError("Haste activation requires a restored combat-context callback");
}
MagicResult MagicEffectService::resolve(const MagicRequest& request,std::span<Character> members,std::mt19937& random) {
    return resolve_bound(request,members,random,false);
}
MagicResult MagicEffectService::resolve_bound(const MagicRequest& request,std::span<Character> members,std::mt19937& random,bool combat_bound) {
    try {
        const auto& definition=catalog_.definition(request.spell);
        if((request.context.domain==MagicDomain::combat && !combat_bound) || (request.target.kind!=MagicTargetKind::party_member && request.target.kind!=MagicTargetKind::party && !(combat_bound && request.target.kind==MagicTargetKind::combatant)))
            return {MagicStatus::unresolved,"Combat and area spell targets need their game-mode service",{}};
        if(request.caster>=members.size() || ((request.target.kind==MagicTargetKind::party_member || request.target.kind==MagicTargetKind::combatant) && request.target.id>=members.size()))
            return {MagicStatus::rejected,"Spell requires actual caster and target members",{}};
        const bool party_target=request.target.kind==MagicTargetKind::party;
        if(!combat_bound && party_target && definition.noncombat_target_mode()!=4) return {MagicStatus::rejected,"This spell requires a confirmed single member",{}};
        if(!combat_bound && !party_target && definition.noncombat_target_mode()==4) return {MagicStatus::rejected,"This spell requires the roster target",{}};
        if(!combat_bound && definition.noncombat_target_mode()==1 && request.target.id!=request.caster) return {MagicStatus::rejected,"This spell targets its caster",{}};
        auto& target=members[party_target?request.caster:request.target.id];
        if(!target.effects_restored()) return {MagicStatus::unresolved,"Spell target effect list is not restored",{}};
        const auto id=request.spell.value;
        MagicResult result{MagicStatus::applied,{},{}};
        const auto emit=[&](std::size_t recipient,std::size_t offset,std::string_view source="GAME.OVR",bool recipient_animation=false) {
            result.feedback.push_back({recipient,pascal_string_id(source,offset),recipient_animation});
        };
        const auto target_index=party_target?request.caster:request.target.id;
        NativeEffectContext native_context{items_,&random,execution_mode_,&catalog_};
        const auto application_message=[&](std::size_t recipient,bool accepted) {
            std::size_t offset=0x2c532;
            if(accepted) switch(id) {
                case 1:offset=0x2801c;break;
                case 5:case 11:case 18:case 22:case 29:offset=0x280f5;break;
                case 6:case 7:case 16:case 17:case 52:case 53:case 54:offset=0x2812e;break;
                case 8:offset=0x28168;break;
                case 14:offset=0x283e4;break;
                case 19:offset=0x284a5;break;
                case 24:offset=0x287ef;break;
                case 30:case 50:offset=0x28a21;break;
                case 32:offset=0x28a94;break;
                case 42:offset=0x294e3;break;
                case 45:offset=0x29649;break;
                case 48:offset=0x298a0;break;
                default:throw DecodeError("Native application feedback binding is missing");
            }
            emit(recipient,offset,"GAME.OVR",accepted);
        };
        if(id==13 || id==28)return resolve_weapon_magic(request,members,random);
        if(id==26)return resolve_poison_magic(request,members,random);
        if(id==56) {
            if(!items_)return {MagicStatus::unresolved,"Level recovery requires source class and equipment caches",{}};
            if(CharacterRecoveryService::restore_level(target,catalog_.native_module())) {
                EquipmentRules::recalculate(target,*items_);emit(target_index,0x29c4f);
            }
            return result;
        }
        if(id==48) {
            auto remaining=request.caster_level;std::vector<bool> retained(members.size(),false);
            for(std::size_t i=0;i<members.size();++i) {
                auto& member=members[i];
                if(!member.effects_restored()) throw DecodeError("Haste requires all owned effect lists");
                if(member.byte(0x10e)!=members[request.caster].byte(0x10e) || !remaining) continue;
                --remaining;
                if(!remove_effect(member,0x2a,native_context)) retained[i]=true;
            }
            // Native applies all markers before dispatching activation queries.
            for(std::size_t i=0;i<members.size();++i) if(retained[i])
                application_message(i,append_marker(members[i],definition,request,random,request.caster_level));
            for(std::size_t i=0;i<members.size();++i) if(retained[i]) haste_activation_queries(members[i]);
            return result;
        }
        if(id==3) {
            if(target.status()!=0 && target.status()!=1 && target.status()!=4 && target.status()!=5)
                return {MagicStatus::unresolved,"Healing revival and this status need their native status service",{}};
            const auto amount=static_cast<std::uint8_t>(roll(random,8)); // Native rolls even at full HP.
            const auto was_active=target.active();
            if(NoncombatRecoveryService::heal(target,amount,false,execution_mode_)) {
                emit(target_index,target.hit_points()==target.byte(0x32)?0x2ec6:0x2ed6,"START.EXE:module");
                if(!was_active && target.active())emit(target_index,target.byte(0x10e)==1?182066:182086);
            }
            result.rolled_amount=amount;return result;
        }
        if(id==1 || id==5 || id==6 || id==7 || id==8 || id==11 || id==16 || id==17 || id==18 || id==19 || id==22 || id==24 || id==29 || id==30 || id==45 || id==50 || id==52 || id==53 || id==54) {
            if(party_target) for(std::size_t i=0;i<members.size();++i) {
                auto& member=members[i];
                if(!member.effects_restored()) throw DecodeError("Whole-party spell requires all owned effect lists");
                // Bless filters the acquired roster by native allegiance10E.
                if(id==1 && member.byte(0x10e)!=members[request.caster].byte(0x10e)) continue;
                application_message(i,append_marker(member,definition,request,random,request.caster_level));
            } else application_message(target_index,append_marker(target,definition,request,random,request.caster_level));
            return result;
        }
        if(id==14) {
            const auto before=target.byte(0x15);
            const auto amount=static_cast<std::uint8_t>(roll(random,4)+roll(random,4));
            // Native byte addition happens before the unsigned25 cap.
            target.set_byte(0x15,std::min(std::uint8_t{25},static_cast<std::uint8_t>(before+amount)));
            application_message(target_index,append_marker(target,definition,request,random,before,1));
            result.rolled_amount=amount;return result;
        }
        if(id==32 || id==42) {
            const auto context=id==32?static_cast<std::uint8_t>(roll(random,4)):
                static_cast<std::uint8_t>(members[request.caster].byte(0x10e)*16+request.caster_level);
            application_message(target_index,append_marker(target,definition,request,random,context));return result;
        }
        if(id==12 || id==35) {
            if(!items_) return {MagicStatus::unresolved,"Strength changes require the native equipment recalculation service",{}};
            std::uint8_t strength=18,exceptional=0;
            if(id==12) {
                constexpr std::array<std::uint8_t,6> values{0,1,51,76,91,100};
                if(request.caster_level<1 || request.caster_level>values.size()) return {MagicStatus::unresolved,"Enlarge caster level has no native assignment in the verified branch",{}};
                exceptional=values[request.caster_level-1];
            } else {
                std::optional<std::uint8_t> increase;
                // These are sequential draws; multiclass members can draw all
                // three dice, with the last applicable family determining gain.
                const auto positive=[&](std::size_t at){const auto n=target.byte(at);return n>0 && n<128;};
                if(positive(0x9b)) increase=static_cast<std::uint8_t>(roll(random,4));
                if(positive(0x96) || positive(0x9c)) increase=static_cast<std::uint8_t>(roll(random,6));
                if(positive(0x98)) increase=static_cast<std::uint8_t>(roll(random,8));
                if(!increase) return {MagicStatus::unresolved,"Strength target has no recovered native class branch",{}};
                strength=static_cast<std::uint8_t>(target.byte(0x10)+*increase);
                if(strength>18) {
                    if(!positive(0x98)) strength=18;
                    else {exceptional=std::min(std::uint8_t{100},static_cast<std::uint8_t>(target.byte(0x16)+(strength-18)*10));strength=18;}
                }
            }
            const auto effective=strength>=target.byte(0x10) && (strength!=18 || exceptional>=target.byte(0x16));
            const auto context=install_strength(target,strength,exceptional);
            if(effective) EquipmentRules::recalculate(target,*items_);
            // These two native handlers append directly, without phase9.
            CharacterEffect effect;effect.payload={definition.effect_code(),0,0,context,1};
            effect.set_duration(static_cast<std::uint16_t>(definition.duration_base()+definition.duration_per_level()*request.caster_level));
            effect.source_file="START.EXE:module";effect.source_offset=definition.provenance().module_offset;
            effect.spell_id=id;effect.item_origin=request.item_origin;
            if(target.effects_.size()>=65535) throw DecodeError("Effect record count exceeds native bound");
            target.effects_.push_back(std::move(effect));
            if(effective) emit(target_index,id==12?0x282b1:0x28f4d);
            return result;
        }
        if(id==37) {if(remove_effect(target,0x21,native_context)) emit(target_index,0x29227,"GAME.OVR",true);return result;}
        if(id==39) {
            auto cure_remove=[&](std::uint8_t code) {
                const auto it=std::find_if(target.effects_.begin(),target.effects_.end(),[&](const auto& e){return e.code()==code;});
                return it!=target.effects_.end() && remove_native(target,static_cast<std::size_t>(it-target.effects_.begin()),true,native_context);
            };
            cure_remove(0x22);
            if(cure_remove(0x2b)) {cure_remove(0x2c);cure_remove(0x1f);}
            if(cure_remove(0x32)) cure_remove(0x39);
            return result;
        }
        if(id==43) {
            if(remove_effect(target,0x24,native_context)) emit(target_index,0x29536,"GAME.OVR",true);
            else for(auto& item:target.inventory_.items_) if(item.cursed()) {item.bytes_[0x36]=0;emit(target_index,0x29543,"GAME.OVR",true);break;}
            return result;
        }
        if(id==41 || id==46) {
            bool removed=false;
            for(std::size_t i=0;i<target.effects_.size();) {
                const auto raw_context=target.effects_[i].payload[3];
                if(raw_context==255) {++i;continue;}
                const auto level=static_cast<std::uint8_t>(raw_context&15);
                const auto threshold=static_cast<std::uint8_t>(request.caster_level>level?50+(request.caster_level-level)*5:
                    request.caster_level<level?50-(level-request.caster_level)*2:50);
                if(roll(random,100)<=threshold) {remove_effect_at(target,i,native_context);removed=true;}else ++i;
            }
            if(removed) emit(target_index,0x29391,"GAME.OVR",true);
            return result;
        }
        return {MagicStatus::unresolved,"This spell's native handler is not implemented",{}};
    } catch(const DecodeError& error) {return {MagicStatus::unresolved,error.what(),{}};}
}
}
