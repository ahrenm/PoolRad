#include "gbengine/combat/combat_magic_effects.hpp"
#include "gbengine/combat/attack_service.hpp"
#include <algorithm>
#include <limits>
#include "gbengine/party/equipment.hpp"
#include "gbengine/combat/effect_expiry.hpp"
#include "gbengine/resources/string_store.hpp"

namespace GBEngine {
namespace {
std::uint8_t roll(std::mt19937& random,std::uint32_t sides) {
    const auto threshold=(std::uint32_t{0}-sides)%sides;
    std::uint32_t value;do {value=static_cast<std::uint32_t>(random());}while(value<threshold);
    return static_cast<std::uint8_t>(value%sides+1);
}
}
std::size_t CombatMagicEffects::find(const DeploymentState& state,CombatActorIdentity id) {
    const auto at=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& actor){return actor.id==id;});
    if(at==state.actors.end() || at->removed)throw DecodeError("Combat magic actor identity is not retained");
    return static_cast<std::size_t>(at-state.actors.begin());
}
bool CombatMagicEffects::has(const Character& c,std::uint8_t code) {
    if(!c.effects_restored())throw DecodeError("Combat magic requires restored effect ownership");
    return std::any_of(c.effects().begin(),c.effects().end(),[&](const auto& node){return node.code()==code;});
}
CombatMagicEffects CombatMagicEffects::load(const IFileSystem& files) {
    CombatMagicEffects result;result.health_=CombatAttackService::load(files).health_service();
    result.items_=ItemCatalog::load(files);return result;
}
bool CombatMagicEffects::schedule_regeneration_in_place(Character& target) {
    if(has(target,0x62) || has(target,0x3b))return false;
    if(target.effects_.size()>=65535)throw DecodeError("Combat regeneration effect count exceeds native bound");
    CharacterEffect node;node.payload={0x3b,3,0,255,1};
    node.source_file="GAME.OVR:2EE2:2814";node.source_offset=60507+0x2814;
    target.effects_.push_back(std::move(node));return true;
}
namespace {
template<class Resolve>
auto tactical_save(DeploymentState& state,std::size_t ti,std::size_t ci,std::mt19937& random,Resolve resolve) {
    auto staged=state;auto rng=random;
    // 49D50160: own first node wins. Only fallback scans the retained linked
    // roster, using4E800909 footprint visibility at range1 (2D/2E) or6 (31).
    // Owned occupancy is the validated native icon footprint, including large
    // actors; bodies with no live icon cannot receive a fallback aura.
    const SavingThrowAuraQuery aura=[&](std::uint8_t code)->const CharacterEffect* {
        const auto cells=[&](const DeploymentActor& actor) {
            std::vector<CombatPosition> result;
            if(actor.removed || !actor.position || !actor.shape)return result;
            for(int y=0;y<Battlefield::height;++y)for(int x=0;x<Battlefield::width;++x)
                if(staged.occupancy[static_cast<std::size_t>(y*50+x)]==actor.native_slot)result.push_back({x,y});
            if(result.empty() || result.size()>4)throw DecodeError("Combat aura icon footprint disagrees with owned occupancy");
            return result;
        };
        const auto recipient=cells(staged.actors[ti]);if(recipient.empty())return nullptr;
        for(std::size_t i=0;i<staged.actors.size();++i)if(i!=ti && !staged.actors[i].removed) {
            const auto& actor=staged.actors[i];
            if(!actor.character.effects_restored())throw DecodeError("Combat magic requires restored effect ownership");
            if(!std::any_of(actor.character.effects().begin(),actor.character.effects().end(),[&](const auto& e){return e.code()==code;}))continue;
            bool visible=false;
            for(const auto from:cells(actor))for(const auto to:recipient)
                if(CombatSpatialService::sight(staged.battlefield,from,to,code==0x31?6:1).clear)visible=true;
            if(visible) {
                const auto node=std::find_if(actor.character.effects().begin(),actor.character.effects().end(),
                                            [&](const auto& effect){return effect.code()==code;});
                return &*node;
            }
        }
        return nullptr;
    };
    std::array<Character,1> roster{staged.actors[ti].character};
    const auto result=resolve(roster,staged.actors[ci].character,rng,aura);
    staged.actors[ti].character=std::move(roster[0]);state=std::move(staged);random=std::move(rng);return result;
}
}
SavingThrowResult CombatMagicEffects::save(DeploymentState& state,CombatActorIdentity target,
        std::uint8_t category,std::int8_t modifier,const CombatMagicContext& context,
        std::mt19937& random,std::uint8_t flags) {
    return tactical_save(state,find(state,target),find(state,context.caster),random,
        [&](std::span<Character> roster,const Character& source,std::mt19937& rng,const SavingThrowAuraQuery& aura) {
            return SavingThrowService::resolve(roster,0,source,category,modifier,rng,flags,aura);
        });
}
bool CombatMagicEffects::save_invariant(DeploymentState& state,CombatActorIdentity target,
        std::uint8_t category,std::int8_t modifier,const CombatMagicContext& context,std::mt19937& random) {
    return tactical_save(state,find(state,target),find(state,context.caster),random,
        [&](std::span<Character> roster,const Character& source,std::mt19937& rng,const SavingThrowAuraQuery& aura) {
            return SavingThrowService::resolve_invariant(roster,0,source,category,modifier,rng,aura);
        });
}
std::uint8_t CombatMagicEffects::phase6(Character& target,const Character& caster,std::uint8_t amount,std::uint8_t flags,
                                       const CombatMagicContext& context,std::mt19937& random,
                                       std::size_t member,std::vector<EffectNotice>& notices) const {
    std::uint8_t pending=context.pending_effect;
    constexpr std::array<std::uint8_t,16> order{0x71,0x3d,0x7a,0x3c,0x5b,0x0a,0x14,0x69,
                                              0x6a,0x70,0x72,0x76,0x11,0x5d,0x65,0x1c};
    const auto cancel=[&] {amount=0;pending=0;};
    for(const auto code:order)if(has(target,code)) {
        if(code==0x71 || code==0x3d) {
            if(flags&1) {
                for(unsigned die=0;die<context.dice_count;++die) {
                    amount=static_cast<std::uint8_t>(amount-(code==0x71?1:2));
                    if(amount<context.dice_count)amount=context.dice_count;
                }
                if(code==0x3d && !(flags&8))cancel();
            }
        } else if(code==0x7a) {
            const auto equipped=EquipmentRules::inspect(caster,items_);
            const auto primary=equipped.slots[0];
            auto dice_count=context.dice_count;
            if(primary && caster.inventory().item(*primary).type()==0x56) {
                amount=static_cast<std::uint8_t>(roll(random,8)+roll(random,8)+roll(random,8));
                dice_count=3; //0E2E writes681C before invoking0DE3.
            }
            if(flags&9)amount=static_cast<std::uint8_t>(amount+dice_count);
        } else if(code==0x5d) {
            if(flags&1)amount=static_cast<std::uint8_t>(amount>>1);
        } else if(code==0x65) {
            (void)schedule_regeneration_in_place(target);
        } else if(code==0x3c) {
            // Native319B is an empty handler.
        } else if(code==0x5b) {
            if(context.pending_id==15 || (flags&4))cancel();
        } else if(code==0x0a) {if(flags&2)amount=static_cast<std::uint8_t>(amount>>1);}
        else if(code==0x14) {if(flags&1)amount=static_cast<std::uint8_t>(amount>>1);}
        else if(code==0x69 || code==0x6a) {
            const auto threshold=static_cast<std::uint8_t>((code==0x69?50:15)-(11-static_cast<int>(context.level))*5);
            if((pending || (flags&8)) && roll(random,100)<=threshold)cancel();
        } else if(code==0x70) {if(flags&1)cancel();}
        else if(code==0x72) {if(flags&4)amount=static_cast<std::uint8_t>(amount>>1);}
        else if(code==0x76) {if(flags&2)amount=static_cast<std::uint8_t>(amount>>1);}
        else if(code==0x11) {
            if(target.byte(0x111)<0x39)target.set_byte(0x111,0x39);
            if(context.pending_id==15)amount=0;
        } else if(code==0x1c) {
            auto node=std::find_if(target.effects_.begin(),target.effects_.end(),[](const auto& e){return e.code()==0x1c;});
            //09C0 rolls before checking pending681B or suppression6820. In
            //particular ordinary weapon hits and area spells still draw.
            const auto die=roll(random,static_cast<std::uint32_t>(node->payload[3])+1);
            if(die>1 && context.pending_id && !context.absorption_suppressed) {
                cancel();notices.push_back({member,pascal_string_id("GAME.OVR",0xf60d)});
                --node->payload[3];
                if(!node->payload[3]) {
                    //0026 invokes this same callback before unlink if flag4;
                    //now context0 means1d1, whose result cannot recurse.
                    if(node->payload[4])(void)roll(random,1);
                    target.effects_.erase(node);
                }
            }
        }
    }
    return amount;
}
CombatMagicDamageResult CombatMagicEffects::weapon_callback_damage(const CombatAttackService& attacks,const ItemCatalog& items,
        const DeploymentState& state,CombatActorIdentity target,std::uint8_t amount,
        const CombatMagicContext& context,std::mt19937& random) {
    return on_hit_damage(attacks,items,state,target,amount,9,context,random);
}
CombatMagicDamageResult CombatMagicEffects::on_hit_damage(const CombatAttackService& attacks,const ItemCatalog& items,
        const DeploymentState& state,CombatActorIdentity target,std::uint8_t amount,std::uint8_t flags,
        const CombatMagicContext& context,std::mt19937& random) {
    CombatMagicEffects adapter;adapter.health_=attacks.health_service();adapter.items_=items;
    return adapter.damage(state,target,amount,flags,false,0,context,random);
}
CombatMagicDamageResult CombatMagicEffects::damage(const DeploymentState& state,CombatActorIdentity target,
        std::uint8_t amount,std::uint8_t flags,bool saved,std::uint8_t save_mode,
        const CombatMagicContext& context,std::mt19937& random) const {
    if(save_mode>2)throw DecodeError("Combat damage has an unsupported saving mode");
    auto rng=random;CombatMagicDamageResult result{state,{},{}};const auto ti=find(result.state,target);
    const auto ci=find(result.state,context.caster);
    amount=phase6(result.state.actors[ti].character,result.state.actors[ci].character,amount,flags,context,rng,ti,result.notices);
    // Native phase6 always runs before the saved zero/half adjustment.
    if(saved && save_mode==1)amount=0;
    else if(saved && save_mode==2)amount=static_cast<std::uint8_t>(amount>>1);
    if(amount) {
        result.outcome=health_.apply_damage_in_place(result.state,target,amount,context.caster,flags,&rng);
        for(const auto& notice:result.outcome.notices)
            result.notices.push_back({find(result.state,notice.actor),notice.text_id});
    }
    else {
        const auto& c=result.state.actors[ti].character;
        result.outcome={0,c.hit_points(),c.hit_points(),c.status(),{}};
    }
    random=std::move(rng);return result;
}
CombatMagicRecoveryResult CombatMagicEffects::heal(const DeploymentState& state,CombatActorIdentity target,
        std::uint8_t amount,bool force) const {return health_.heal(state,target,amount,force);}
CombatMagicRecoveryResult CombatMagicEffects::revive(const DeploymentState& state,CombatActorIdentity target,
        std::uint8_t hp) const {return health_.revive(state,target,hp);}
bool CombatMagicEffects::revive_in_place(DeploymentState& state,CombatActorIdentity target,std::uint8_t hp,const SpellCatalog& spells) {
    return CombatHealthService::revive_in_place(state,target,hp,spells);
}
bool CombatMagicEffects::revive_from_module(DeploymentState& state,CombatActorIdentity target,std::span<const std::uint8_t> module,std::uint8_t hp) {
    return CombatHealthService::revive_from_module(state,target,module,hp);
}
void CombatMagicEffects::install_invisibility_in_place(Character& target,std::uint8_t source_code,std::mt19937& random) {
    if(source_code!=0x38 && source_code!=0x48)throw DecodeError("Unsupported combat invisibility lifecycle source");
    if(!has(target,source_code))return;
    if(source_code==0x48 && roll(random,100)>=96)return;
    if(target.effects_.size()>=65535)throw DecodeError("Combat invisibility exceeds native effect capacity");
    CharacterEffect node;node.payload={0x19,1,0,12,0};node.source_file="GAME.OVR";
    // Source handler-relative static provenance, mapped by retained evidence.
    node.source_offset=source_code==0x38?0xffbe:0x1038b;
    target.effects_.push_back(std::move(node));
}
bool CombatMagicEffects::maintain_hammer_in_place(DeploymentState& state,CombatActorIdentity id,ItemId& next,const ItemCatalog& catalog) {
    auto& target=state.actors[find(state,id)].character;
    if(!has(target,0x17))return false;
    const auto found=std::find_if(target.inventory_.items_.begin(),target.inventory_.items_.end(),
                                [](const auto& item){return item.type()==0x14 && item.byte(0x31)==0xf3;});
    bool created=false;
    if(found==target.inventory_.items_.end() && target.byte(0xc7)<16) {
        ItemId maximum=0;
        for(const auto& actor:state.actors)for(std::size_t i=0;i<actor.character.inventory().size();++i)
            maximum=std::max(maximum,actor.character.inventory().item(i).id());
        if(!next || next<=maximum || next==std::numeric_limits<ItemId>::max())
            throw DecodeError("Combat generated weapon identity allocator is invalid or exhausted");
        Item item;item.id_=next++;item.origin_={"GAME.OVR:spiritual-hammer",{},62523};
        item.bytes_[0x2e]=0x14;item.bytes_[0x30]=0x14;item.bytes_[0x31]=0xf3;item.bytes_[0x32]=1;
        item.bytes_[0x3d]=0x17;item.bytes_[0x3e]=0x89;target.inventory_.items_.push_back(std::move(item));created=true;
    }
    EquipmentRules::recalculate(target,catalog);return created;
}
bool CombatMagicEffects::apply_charm_in_place(DeploymentState& state,CombatActorIdentity id) {
    auto& actor=state.actors[find(state,id)];auto& c=actor.character;
    if(!c.effects_restored())throw DecodeError("Combat Charm requires restored effect ownership");
    const auto at=std::find_if(c.effects_.begin(),c.effects_.end(),[](const auto& node){return node.code()==0x0b;});
    if(at==c.effects_.end() || (at->payload[3]&0x20))return false;
    const auto old=c.byte(0x10e);
    at->payload[3]=static_cast<std::uint8_t>(at->payload[3]+0x20+old*0x40);
    c.set_byte(0x10e,static_cast<std::uint8_t>(at->payload[3]>>7));c.set_byte(0x10f,1);
    if(c.byte(0x84)<128)c.set_byte(0x84,0xb3);
    for(std::size_t i=0x0a;i<=0x0d;++i)actor.extension[i]=0;
    actor.retained_target.reset();
    return c.byte(0x10e)!=old;
}
std::vector<EffectNotice> CombatMagicEffects::remove_effect_in_place(DeploymentState& state,CombatActorIdentity id,
        std::size_t index,const ItemCatalog& items,const SpellCatalog& spells,std::mt19937& random,
        std::optional<CombatActorIdentity> resource_actor) {
    auto staged=state;auto rng=random;const auto ti=find(staged,id);auto& c=staged.actors[ti].character;
    if(!c.effects_restored() || index>=c.effects_.size())throw DecodeError("Combat removal requires an exact owned effect");
    const auto effect=c.effects_[index];EffectExpiryEdits edits;
    if(effect.payload[4]) {
        std::vector<std::size_t> indices(staged.actors.size());
        for(std::size_t i=0;i<indices.size();++i)indices[i]=i;
        CombatEffectExpiry resolver(staged,indices,items,spells,rng,resource_actor);
        if(!resolver.reverse_with_edits({ti,effect,index},c,edits))
            throw DecodeError("Combat effect removal callback unresolved for code "+std::to_string(effect.code()));
    }
    std::vector<std::pair<std::size_t,std::uint8_t>> consumed;
    for(const auto& consumption:edits.spell_slot_consumptions) {
        const auto key=std::pair{consumption.member,consumption.slot};
        if(consumption.member>=state.actors.size() || consumption.slot<0x17 || consumption.slot>0x2b ||
           !consumption.expected_full_byte || !resource_actor || state.actors[consumption.member].id!=*resource_actor ||
           state.actors[consumption.member].character.byte(consumption.slot)!=consumption.expected_full_byte ||
           staged.actors[consumption.member].character.byte(consumption.slot)!=0 ||
           std::find(consumed.begin(),consumed.end(),key)!=consumed.end())
            throw DecodeError("Combat effect removal spell consumption lost its exact source/slot/full-byte ownership");
        //No temporary Party exists here: the validated write is already in
        //this method's unpublished tactical candidate cache.
        consumed.push_back(key);
    }
    edits.remove_indices.push_back(index);std::sort(edits.remove_indices.begin(),edits.remove_indices.end());
    edits.remove_indices.erase(std::unique(edits.remove_indices.begin(),edits.remove_indices.end()),edits.remove_indices.end());
    for(auto at=edits.remove_indices.rbegin();at!=edits.remove_indices.rend();++at) {
        if(*at>=c.effects_.size())throw DecodeError("Combat effect removal journal index is stale");
        c.effects_.erase(c.effects_.begin()+static_cast<std::ptrdiff_t>(*at));
    }
    if(c.effects_.size()+edits.append.size()>65535)throw DecodeError("Combat effect removal exceeds native effect count");
    c.effects_.insert(c.effects_.end(),edits.append.begin(),edits.append.end());
    state=std::move(staged);random=std::move(rng);return edits.notices;
}
}
