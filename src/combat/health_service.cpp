#include "gbengine/combat/health_service.hpp"
#include "gbengine/combat/retained_ability_service.hpp"
#include "gbengine/magic/noncombat_periodic_effects.hpp"
#include "gbengine/magic/spell_catalog.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <set>

namespace GBEngine {
namespace {
std::uint8_t roll(std::mt19937& random,unsigned sides) {
    if(!sides)throw DecodeError("Combat damage die has zero sides");
    const auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    std::uint32_t value;do {value=static_cast<std::uint32_t>(random());}while(value<threshold);
    return static_cast<std::uint8_t>(value%sides+1);
}
bool has(const Character& c,std::uint8_t code) {
    return std::any_of(c.effects().begin(),c.effects().end(),[&](const auto& effect){return effect.code()==code;});
}
bool has_restored(const Character& c,std::uint8_t code) {
    if(!c.effects_restored())throw DecodeError("Combat magic requires restored effect ownership");
    return has(c,code);
}
void stop(DeploymentActor& actor) {for(const auto at:{3u,0u,7u,6u})actor.extension[at]=0;}
std::size_t find_recovery(const DeploymentState& state,CombatActorIdentity id) {
    const auto at=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& actor){return actor.id==id;});
    if(at==state.actors.end() || at->removed)throw DecodeError("Combat magic actor identity is not retained");
    return static_cast<std::size_t>(at-state.actors.begin());
}
}
CombatHealthService CombatHealthService::from_native_module(std::shared_ptr<const Bytes> module,const CombatSpatialService& spatial) {
    if(!module || module->size()<0xd3d4+16)throw DecodeError("Truncated combat health cleanup tables");
    CombatHealthService result;result.spatial_=spatial;
    std::copy_n(module->begin()+0xd3d4,16,result.defeat_cleanup_codes_.begin());
    result.native_module_=std::move(module);result.loaded_=true;return result;
}
std::size_t CombatHealthService::find(const DeploymentState& state,CombatActorIdentity id) {
    const auto it=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return a.id==id;});
    if(it==state.actors.end())throw DecodeError("Combat attack identity is stale");
    return static_cast<std::size_t>(it-state.actors.begin());
}
void CombatHealthService::validate(const DeploymentState& state) const {
    if(!loaded_)throw DecodeError("Combat attack source tables are uninitialized");
    if(state.actors.empty() || state.actors.size()>71)throw DecodeError("Invalid combat attack roster size");
    std::set<std::pair<CombatActorOrigin,std::uint64_t>> ids;std::set<std::uint8_t> slots;
    std::array<std::uint8_t,1250> occupancy{};
    for(const auto& a:state.actors) {
        if(!ids.emplace(a.id.origin,a.id.value).second)throw DecodeError("Duplicate combat attack identity");
        if(a.removed)continue;
        if(!a.native_slot || !slots.insert(a.native_slot).second)throw DecodeError("Duplicate combat attack slot");
        if(a.character.byte(0x10e)>1 || a.extension[9]>7)throw DecodeError("Invalid combat attack side or facing");
        for(const auto p:spatial_.footprint(a)) {
            if(!Battlefield::in_bounds(p.x,p.y))throw DecodeError("Combat attack footprint outside battlefield");
            auto& at=occupancy[static_cast<std::size_t>(p.y*50+p.x)];
            if(at)throw DecodeError("Overlapping combat attack actors");
            at=a.native_slot;
        }
    }
    if(occupancy!=state.occupancy)throw DecodeError("Combat attack occupancy disagrees with actors");
}
void CombatHealthService::disable(DeploymentState& state,std::size_t index,std::optional<std::uint8_t> flags,std::mt19937* random,std::vector<CombatDefeatNotice>* notices) const {
    disable_with_codes(state,index,defeat_cleanup_codes_,flags,random,*native_module_,notices);
}
void CombatHealthService::disable_with_codes(DeploymentState& state,std::size_t index,std::span<const std::uint8_t> codes,
        std::optional<std::uint8_t> flags,std::mt19937* random,std::span<const std::uint8_t> module,std::vector<CombatDefeatNotice>* notices) {
    auto& a=state.actors[index];auto& c=a.character;
    for(const auto code:codes) {
        const auto it=std::find_if(c.effects_.begin(),c.effects_.end(),[&](const auto& e){return e.code()==code;});
        if(it!=c.effects_.end()) {
            if(it->payload[4]) {
                if(code==0x1f || code==0x33 || code==0x34 || code==0x35)stop(a);
                else if(code==0x3a)a.extension[6]=0; //2EE21452 live/reverse movement query.
                else if(code==0x0b) {
                    c.set_byte(0x10e,static_cast<std::uint8_t>((it->payload[3]&0x40)>>6));
                    if(c.byte(0x84)==0xb3)c.set_byte(0x84,0);
                }
                else if(code==0x20) {
                    const auto at=static_cast<std::size_t>(it-c.effects_.begin());
                    if(reverse_animate(state,index,at,codes,flags,random,module,notices))continue;
                }
                else if(code==0x3b) {
                    //2814's delayed regeneration node reverses by appending62.
                    //Later100A table entry62 still removes its first node:
                    //that can be this new one when no earlier62 existed.
                    const auto at=static_cast<std::size_t>(it-c.effects_.begin());
                    CharacterEffect next;next.payload={0x62,0,0,255,0};next.source_file="GAME.OVR:2EE2:1472";
                    c.effects_.push_back(std::move(next));c.effects_.erase(c.effects_.begin()+static_cast<std::ptrdiff_t>(at));
                    continue;
                }
                else if(code==0x07) {
                    if(!notices)throw DecodeError("Combat defeat07 reversal requires its owned recipient feedback");
                    const auto at=static_cast<std::size_t>(it-c.effects_.begin());
                    EffectExpiryEdits edits;
                    if(!NoncombatPeriodicEffects::reverse({index,*it,at},c,edits,{}) || !edits.remove_indices.empty())
                        throw DecodeError("Combat defeat07 requires its exact pure callback edits");
                    if(c.effects_.size()+edits.append.size()>65535)throw DecodeError("Combat defeat07 exceeds native effect capacity");
                    for(const auto& message:edits.notices)notices->push_back({a.id,message.text_id});
                    c.effects_.insert(c.effects_.end(),edits.append.begin(),edits.append.end());
                    c.effects_.erase(c.effects_.begin()+static_cast<std::ptrdiff_t>(at));continue;
                }
                else if(code==0x36) {} //1355 is an empty callback.
                else if(code==0x5f) {
                    if(c.active())throw DecodeError("Combat defeat5F reversal requires its inactive terminal owner");
                }
                else if(code==0x62)c.set_byte(0x11b,std::min(c.byte(0x32),static_cast<std::uint8_t>(c.hit_points()+3)));
                else if(code==0x4a || code==0x4b) {
                    RetainedAbilityService::remove_effect_in_place(state,a.id,static_cast<std::size_t>(it-c.effects_.begin()));
                    continue;
                }
                else throw DecodeError("Combat defeat requires an unsupported effect removal callback");
            }
            c.effects_.erase(it);
        }
    }
    if(has(c,0x63)) {
        //2730 tests only4/5; a genuinely killed6 body does not recover.
        const auto hp=c.status()==4?6u:c.status()==5 && a.extension[0xe]<6?6u-a.extension[0xe]:0u;
        if(hp) {
            if(module.empty() || !random || !notices)throw DecodeError("Combat defeat63 requires source placement, staged RNG and wakeup feedback");
            if(CombatHealthService::revive_from_module(state,a.id,module,static_cast<std::uint8_t>(hp))) {
                notices->push_back({a.id,pascal_string_id("GAME.OVR",c.byte(0x10e)==1?182066:182086)});
                const auto duration=static_cast<std::uint8_t>(roll(*random,4)+1);
                CharacterEffect next;next.payload={0x5f,duration,0,255,1};next.source_file="GAME.OVR:2EE2:2730";
                c.effects_.push_back(std::move(next));
                const auto first=std::find_if(c.effects_.begin(),c.effects_.end(),[](const auto& e){return e.code()==0x63;});
                c.effects_.erase(first); //The source clearsflag4 before exact unlink.
            }
        }
    }
    if(has(c,0x64)) {
        if(!flags)throw DecodeError("Combat defeat64 requires the native damage-flags context");
        if(!(*flags&0x11)) {
            if(!random)throw DecodeError("Combat defeat64 requires staged RNG for native3d6 recovery");
            const auto duration=static_cast<std::uint8_t>(roll(*random,6)+roll(*random,6)+roll(*random,6));
            CharacterEffect next;next.payload={0x66,duration,0,255,1};next.source_file="GAME.OVR:2EE2:27D9";
            c.effects_.push_back(std::move(next));
        }
    }
    // Phase13's first67 ->2EE2:28BF clears its removal flag and redraws.
    // Its49D5:0F06 escape helper requires active10D; damage/terminal handling
    // has already cleared that byte here. Keep the native inactive no-op:
    // no status3, HP recovery, effect unlink, flee notice or reward exclusion.
    const auto mist=std::find_if(c.effects_.begin(),c.effects_.end(),[](const auto& e){return e.code()==0x67;});
    if(mist!=c.effects_.end() && c.active())throw DecodeError("Combat recovered67 requires its active native escape callback");
    if(mist!=c.effects_.end())mist->payload[4]=0;
    //Phase13 calls live4B then4A, even for unflagged nodes; the inactive
    //branch releases first3A and exact-unlinks without double reversal.
    for(const auto code:{std::uint8_t{0x4b},std::uint8_t{0x4a}})
        RetainedAbilityService::release_inactive_in_place(state,a.id,code);
    if(!c.active() && std::none_of(state.bodies.begin(),state.bodies.end(),[&](const auto& body){return body.actor==a.id;})) {
        if(a.extension[0x13]==0) {
            if(state.bodies.size()>=71)throw DecodeError("Combat body capacity exceeded");
            const auto p=*a.position;auto& terrain=state.battlefield.terrain[static_cast<std::size_t>(p.y*50+p.x)];
            state.bodies.push_back({a.id,p,terrain});if(terrain!=0x1e)terrain=0x1f;
        }
        for(auto& slot:state.occupancy)if(slot==a.native_slot)slot=0;
        a.shape=0;
    }
    if(!c.active())stop(a);
}
bool CombatHealthService::reverse_animate(DeploymentState& state,std::size_t index,std::size_t effect_index,
                                         std::span<const std::uint8_t> codes,std::optional<std::uint8_t> flags,std::mt19937* random,
                                         std::span<const std::uint8_t> module,std::vector<CombatDefeatNotice>* notices) {
    auto& actor=state.actors[index];auto& c=actor.character;
    if(effect_index>=c.effects_.size() || c.effects_[effect_index].code()!=0x20)
        throw DecodeError("Control effect20 reversal identity is stale");
    const auto first=std::find_if(c.effects_.begin(),c.effects_.end(),[](const auto& e){return e.code()==0x20;});
    if(static_cast<std::size_t>(first-c.effects_.begin())!=effect_index)
        throw DecodeError("Control effect20 duplicate reversal requires unsafe native successor semantics");
    const auto context=c.effects_[effect_index].payload[3];
    if((context>>4)>1)throw DecodeError("Control effect20 captured allegiance is outside the owned two-side domain");
    c.effects_[effect_index].payload[4]=0;
    const bool killed=c.status()!=7 && c.status()!=6 && c.status()!=8;
    if(killed) {
        c.set_byte(0x10c,6);c.set_byte(0x10d,0);c.set_byte(0x11b,0);
        disable_with_codes(state,index,codes,flags,random,module,notices);
    }
    //2EE20B1B reads the payload after a nonallocating known cleanup could
    //free it. Preserve the original owned value instead of a dangling node.
    c.set_byte(0x10e,static_cast<std::uint8_t>(context>>4));c.set_byte(0x10f,1);
    c.set_byte(0x76,0);c.set_byte(0x6b,c.byte(0x98));c.set_byte(0x72,12);
    if(c.byte(0x84)==0xb3)c.set_byte(0x84,0);
    c.set_byte(0x9f,0);return killed;
}
void CombatHealthService::reverse_animate_dead_in_place(DeploymentState& state,CombatActorIdentity id,
        std::size_t effect_index,const SpellCatalog& spells) {
    const auto module=spells.native_module();
    if(module.size()<0xd3d4+16)throw DecodeError("Control effect20 source cleanup table is truncated");
    (void)reverse_animate(state,find(state,id),effect_index,module.subspan(0xd3d4,16));
}
std::optional<std::uint8_t> CombatHealthService::damage(DeploymentState& state,std::size_t index,std::uint8_t amount,std::size_t resource_index,
        std::optional<std::uint8_t> flags,std::mt19937* random,std::vector<CombatDefeatNotice>* notices) const {
    auto& target=state.actors[index];auto& character=target.character;
    std::optional<std::uint8_t> interrupted;
    const unsigned deficit=amount>character.hit_points()?amount-character.hit_points():0;
    const unsigned hp=amount<=character.hit_points()?character.hit_points()-amount:0;
    if(deficit>=10 || (!hp && character.status()==1))character.set_byte(0x10c,6);
    else if(deficit) {character.set_byte(0x10c,5);target.extension[0xe]=static_cast<std::uint8_t>(deficit);}
    else if(!hp)character.set_byte(0x10c,4);
    if(character.status()!=0 && character.status()!=1) {character.set_byte(0x10d,0);character.set_byte(0x11b,0);target.extension[3]=0;}
    else character.set_byte(0x11b,static_cast<std::uint8_t>(hp));
    if(amount) {
        target.extension[1]=0;
        if(target.extension[0]) {
            interrupted=target.extension[0];
            auto& resources=state.actors[resource_index].character;
            for(std::size_t slot=0;slot<21;++slot)if(resources.byte(0x17+slot)==*interrupted) {
                resources.set_byte(0x17+slot,0);break;
            }
            target.extension[0]=0;
        }
    }
    if(!character.active())disable(state,index,flags,random,notices);
    return interrupted;
}
CombatDamageOutcome CombatHealthService::apply_damage_in_place(DeploymentState& state,CombatActorIdentity target,
                                                               std::uint8_t amount,CombatActorIdentity resource_actor,
                                                               std::optional<std::uint8_t> flags,std::mt19937* random) const {
    validate(state);
    const auto ti=find(state,target),ri=find(state,resource_actor);
    CombatDamageOutcome result;result.amount=amount;result.before_hp=state.actors[ti].character.hit_points();
    result.interrupted_spell=damage(state,ti,amount,ri,flags,random,&result.notices);
    result.after_hp=state.actors[ti].character.hit_points();result.status=state.actors[ti].character.status();
    validate(state);return result;
}
bool CombatHealthService::apply_terminal_status_in_place(DeploymentState& state,CombatActorIdentity target,std::uint8_t status,
        std::optional<std::uint8_t> flags,std::mt19937* random,std::vector<CombatDefeatNotice>* notices) const {
    validate(state);const auto ti=find(state,target);auto& c=state.actors[ti].character;
    if(c.status()==7 || c.status()==6 || c.status()==8)return false;
    c.set_byte(0x10c,status);c.set_byte(0x10d,0);c.set_byte(0x11b,0);
    disable(state,ti,flags,random,notices);validate(state);return true;
}
bool CombatHealthService::apply_terminal_status_from_module_in_place(DeploymentState& state,CombatActorIdentity target,
        std::uint8_t status,std::span<const std::uint8_t> module,std::vector<CombatDefeatNotice>* notices) {
    if(module.size()<0xd3d4+16)throw DecodeError("Combat periodic terminal source cleanup table is truncated");
    const auto ti=find(state,target);auto& c=state.actors[ti].character;
    if(c.status()==7 || c.status()==6 || c.status()==8)return false;
    c.set_byte(0x10c,status);c.set_byte(0x10d,0);c.set_byte(0x11b,0);
    disable_with_codes(state,ti,module.subspan(0xd3d4,16),{},nullptr,module,notices);return true;
}
CombatRecoveryOutcome CombatHealthService::heal(const DeploymentState& state,CombatActorIdentity target,
                                                   std::uint8_t amount,bool force) const {
    CombatRecoveryOutcome result{state,false,false};auto& c=result.state.actors[find_recovery(result.state,target)].character;
    const auto status=c.status();
    if(status!=0 && status!=1 && status!=4 && status!=5)return result;
    if(force?c.hit_points()>=c.byte(0x32):has_restored(c,0x32))return result;
    c.set_byte(0x11b,std::min(c.byte(0x32),static_cast<std::uint8_t>(c.hit_points()+amount)));
    if(!c.active() && status==5)c.set_byte(0x10c,4);
    result.applied=true;return result;
}
CombatRecoveryOutcome CombatHealthService::revive(const DeploymentState& state,CombatActorIdentity target,
                                                     std::uint8_t hp) const {
    CombatRecoveryOutcome result{state,false,false};auto& actor=result.state.actors[find_recovery(result.state,target)];
    if(!actor.position)throw DecodeError("Combat recovery requires its original tactical position");
    const auto old_shape=actor.shape;actor.shape=static_cast<std::uint8_t>(actor.character.byte(0x6c)&127);
    const auto cells=spatial_.footprint(actor);
    if(cells.empty())throw DecodeError("Combat recovery requires a source footprint");
    for(const auto p:cells) {
        if(!Battlefield::in_bounds(p.x,p.y)) {actor.shape=old_shape;return result;}
        const auto slot=result.state.occupancy[static_cast<std::size_t>(p.y*50+p.x)];
        const auto terrain=result.state.battlefield.terrain_at(p.x,p.y);
        if((slot && slot!=actor.native_slot) || !terrain || result.state.battlefield.tables.properties(terrain)[0]==255) {
            actor.shape=old_shape;return result;
        }
    }
    std::optional<std::uint8_t> underlying;
    for(const auto& body:result.state.bodies)if(body.actor==target && body.underlying_terrain!=0x1f)underlying=body.underlying_terrain;
    std::erase_if(result.state.bodies,[&](const auto& body){return body.actor==target;});
    const bool another_body=std::any_of(result.state.bodies.begin(),result.state.bodies.end(),[&](const auto& body){return body.position==*actor.position;});
    if(!another_body && underlying)result.state.battlefield.terrain[static_cast<std::size_t>(actor.position->y*50+actor.position->x)]=*underlying;
    for(const auto p:cells)result.state.occupancy[static_cast<std::size_t>(p.y*50+p.x)]=actor.native_slot;
    result.reactivated=!actor.character.active();actor.character.set_byte(0x10c,0);
    actor.character.set_byte(0x10d,1);actor.character.set_byte(0x11b,hp);result.applied=true;return result;
}
bool CombatHealthService::revive_in_place(DeploymentState& state,CombatActorIdentity id,std::uint8_t hp,const SpellCatalog& spells) {
    return revive_from_module(state,id,spells.native_module(),hp);
}
bool CombatHealthService::revive_from_module(DeploymentState& state,CombatActorIdentity id,std::span<const std::uint8_t> module,std::uint8_t hp) {
    CombatHealthService adapter;adapter.spatial_=CombatSpatialService::from_native_module(module);
    auto result=adapter.revive(state,id,hp);if(!result.applied)return false;
    const auto i=find_recovery(state,id);state.actors[i]=std::move(result.state.actors[i]);
    state.battlefield=std::move(result.state.battlefield);state.occupancy=result.state.occupancy;
    state.bodies=std::move(result.state.bodies);return true;
}
}
