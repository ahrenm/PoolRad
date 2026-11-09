#include "gbengine/combat/attack_service.hpp"
#include "gbengine/assets/records.hpp"
#include "gbengine/party/equipment.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/magic/spell_catalog.hpp"
#include "gbengine/combat/retained_ability_service.hpp"
#include "gbengine/combat/magic_service.hpp"
#include "gbengine/combat/cloud_service.hpp"
#include "gbengine/party/character_recovery_service.hpp"
#include <algorithm>
#include <bit>
#include <limits>

namespace GBEngine {
namespace {
int signed_byte(std::uint8_t n) {return n<128?n:static_cast<int>(n)-256;}
std::uint8_t roll(std::mt19937& random,unsigned sides) {
    if(!sides)throw DecodeError("Combat damage die has zero sides");
    const auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    std::uint32_t value;do {value=static_cast<std::uint32_t>(random());}while(value<threshold);
    return static_cast<std::uint8_t>(value%sides+1);
}
bool has(const Character& c,std::uint8_t code) {
    return std::any_of(c.effects().begin(),c.effects().end(),[&](const auto& effect){return effect.code()==code;});
}
void stop(DeploymentActor& actor) {for(const auto at:{3u,0u,7u,6u})actor.extension[at]=0;}
// 326D:263D takes the first inclusive 4E80:057B cone. Adjacent cardinal and
// diagonal positions are exact matches before testing the larger wedges.
unsigned direction(CombatPosition from,CombatPosition to,const BattlefieldTables& tables) {
    if(from==to)return 0;
    for(unsigned d=0;d<8;++d) {
        const int x=from.x+tables.delta_x(d),y=from.y+tables.delta_y(d);
        if(to==CombatPosition{x,y})return d;
        bool match=false;
        switch(d) {
        case 0:match=!((to.x<x || x-to.x+y<to.y) && (x<to.x || to.x-x+y<to.y));break;
        case 1:match=!((to.x<x || x-to.x+y<to.y) && (to.x<x+y-to.y || y<to.y));break;
        case 2:match=!((to.x<x+y-to.y || y<to.y) && (to.x<x+to.y-y || to.y<y));break;
        case 3:match=!((to.x<x+to.y-y || to.y<y) && (to.x<x || to.y<to.x-x+y));break;
        case 4:match=!((to.x<x || to.y<to.x-x+y) && (x<to.x || to.y<x-to.x+y));break;
        case 5:match=!((x<to.x || to.y<x-to.x+y) && (x+y-to.y<to.x || to.y<y));break;
        case 6:match=!((x+y-to.y<to.x || to.y<y) && (x+to.y-y<to.x || y<to.y));break;
        case 7:match=!((x+to.y-y<to.x || y<to.y) && (x<to.x || to.x-x+y<to.y));break;
        }
        if(match)return d;
    }
    throw DecodeError("Combat target has no native facing cone");
}
struct Weapon {
    std::optional<std::size_t> item,ammunition;
    bool ranged=false,dual_use=false;
    unsigned maximum=1;
};
Weapon weapon(const Character& c,const ItemCatalog& catalog) {
    Weapon result;const auto equipped=EquipmentRules::inspect(c,catalog);result.item=equipped.slots[0];
    if(!result.item)return result;
    const auto& item=c.inventory().item(*result.item);const auto& def=catalog.definition(item.type());
    const auto raw=static_cast<std::uint8_t>(def.byte(12)-1);
    result.maximum=raw==0 || raw==255?1:raw;
    result.ranged=def.byte(12)>=2;const auto flags=def.attack_flags();
    result.dual_use=result.ranged && (flags&0x14)==0x14;
    if(flags&0x10)result.ammunition=result.item;
    if(flags&8) {
        if(flags&1)result.ammunition=equipped.slots[11];
        if(flags&0x80)result.ammunition=equipped.slots[12];
    }
    return result;
}
}
CombatAttackService CombatAttackService::load(const IFileSystem& files) {
    CombatAttackService result;result.spatial_=CombatSpatialService::load(files);
    auto module=decode_start_data(files.read_asset("START.EXE"));
    if(module.size()<0xc7c0+0x2870)throw DecodeError("Truncated combat attack tables");
    std::copy_n(module.begin()+0xc7c0+0x286c,4,result.helpless_codes_.begin());
    for(std::size_t i=0;i<result.retained_defense_names_.size();++i) {
        const auto at=0xc7c0+0x3f2+16*(i+1),size=static_cast<std::size_t>(module[at]);
        if(at+size+1>module.size())throw DecodeError("Truncated combat retained-target predicate table");
        result.retained_defense_names_[i]=Bytes(module.begin()+static_cast<std::ptrdiff_t>(at+1),module.begin()+static_cast<std::ptrdiff_t>(at+size+1));
    }
    result.native_module_=std::make_shared<const Bytes>(std::move(module));
    result.health_=CombatHealthService::from_native_module(result.native_module_,result.spatial_);
    result.loaded_=true;return result;
}
void CombatAttackService::drain_levels(DeploymentState& state,CombatActorIdentity attacker,
        CombatActorIdentity target,unsigned count,const ItemCatalog& catalog,CombatMagicResult& playback,
        std::uint8_t flags,std::mt19937& random) const {
    // 2EE2:23A4/23C2 ->2122. The owner extension, not a monster ID or SPC
    // payload, supplies the victim. The surrounding attack owns atomicity.
    const auto& owner=state.actors[find(state,attacker)];
    if(!owner.retained_target || *owner.retained_target!=target || !native_module_)
        throw DecodeError("Combat level drain requires its retained on-hit victim and source tables");
    auto& c=state.actors[find(state,target)].character;
    const auto snapshot=[&]{return std::make_shared<const DeploymentState>(state);};
    const auto notice=[&](std::size_t offset,std::string suffix={}) {
        playback.feedback.push_back({target,pascal_string_id("GAME.OVR",offset),false,10,true,std::move(suffix)});
        playback.events.push_back({CombatMagicEventKind::feedback,playback.feedback.size()-1,snapshot(),{}});
    };
    notice(60507+0x20ef,count==1?overlay_string(60507+0x20e7):std::to_string(count)+overlay_string(60507+0x20df));
    // Native2002 uses signed levels/XP, later ties and the FFFFFFFF sentinel.
    // All-negative levels leave its local selection undefined; retain a hold.
    const auto choose=[&]() {
        int best_level=0;std::int32_t best_xp=0;std::optional<unsigned> selected;
        for(unsigned cls=0;cls<8;++cls) {
            const auto level=signed_byte(c.byte(0x96+cls));std::uint32_t bits=0;
            if(level>1)for(unsigned i=0;i<4;++i) {
                const auto at=0xc7c0u+static_cast<std::uint16_t>(0x409b+cls*0x38+static_cast<unsigned>(level)*4+i);
                if(at>=native_module_->size())throw DecodeError("Combat level drain source table is truncated");
                bits|=static_cast<std::uint32_t>((*native_module_)[at])<<(8*i);
            }
            const auto xp=std::bit_cast<std::int32_t>(bits);
            if(level>=best_level && (xp>=best_xp || bits==0xffffffffu)) {
                selected=cls;best_level=level;best_xp=xp;
            }
        }
        if(!selected)throw DecodeError("Combat level drain has no initialized native class selection");
        return std::pair{*selected,std::bit_cast<std::uint32_t>(best_xp)};
    };
    const auto terminal=[&](std::uint8_t status,std::size_t text) {
        const auto before=snapshot();const auto hp=c.hit_points();
        std::vector<CombatDefeatNotice> cleanup_notes;
        if(apply_terminal_status_in_place(state,target,status,flags,&random,&cleanup_notes)) {
            notice(text);
            for(const auto& defeat_note:cleanup_notes) {
                playback.feedback.push_back({defeat_note.actor,defeat_note.text_id});
                playback.events.push_back({CombatMagicEventKind::feedback,playback.feedback.size()-1,before,snapshot()});
            }
            playback.damage.push_back({attacker,target,0,hp,c.hit_points(),c.status(),false,{},0});
            playback.events.push_back({CombatMagicEventKind::terminal,playback.damage.size()-1,before,snapshot()});
        }
    };
    if(c.status()==1) {
        terminal(8,60507+0x20fb);c.set_byte(0x32,0);
        playback.events.push_back({CombatMagicEventKind::state,0,{},snapshot()});return;
    }
    for(unsigned n=0;n<count;++n) {
        if(c.status()==8)continue;
        c.set_byte(0x74,static_cast<std::uint8_t>(c.byte(0x74)+1));
        std::uint8_t sum=0;
        for(unsigned cls=0;cls<8;++cls)sum=static_cast<std::uint8_t>(sum+c.byte(0x96+cls));
        if(!sum)throw DecodeError("Combat level drain has a native zero level divisor");
        const auto loss=static_cast<unsigned>(c.byte(0x32))/sum;
        for(unsigned hp=0;hp<loss;++hp) {
            for(const auto at:{0x32u,0xb1u,0x11bu})if(c.byte(at))c.set_byte(at,static_cast<std::uint8_t>(c.byte(at)-1));
            c.set_byte(0x75,static_cast<std::uint8_t>(c.byte(0x75)+1));
        }
        if(signed_byte(c.byte(0x73))<2) {
            terminal(8,60507+0x20fb);c.set_byte(0x32,0);
            const auto [cls,xp]=choose();(void)xp;
            if(signed_byte(c.byte(0x96+cls))>0 && count==2)c.set_byte(0x96+cls,0);
        }else {
            const auto [cls,xp]=choose();(void)xp;
            c.set_byte(0x96+cls,static_cast<std::uint8_t>(c.byte(0x96+cls)-1));
            if(!c.hit_points() && c.status()!=6)terminal(6,60507+0x2118);
            CharacterRecoveryService::rebuild_class_cache(c,*native_module_);
        }
        const auto [cls,xp]=choose();c.set_byte(0x73,c.byte(0x96+cls));
        c.set_word(0xac,static_cast<std::uint16_t>(xp));c.set_word(0xae,static_cast<std::uint16_t>(xp>>16));
        EquipmentRules::recalculate(c,catalog);
        // This is the native HP/cache update, not a second HP damage callback:
        // no damage animation, spell interruption, save or random draw.
        playback.events.push_back({CombatMagicEventKind::state,0,{},snapshot()});
    }
}
void CombatAttackService::spend_ammunition(DeploymentState& state,std::size_t actor,std::size_t item,
                                          unsigned spent,bool recoverable) {
    auto& c=state.actors.at(actor).character;auto& ammunition=c.inventory_.items_.at(item);
    if(ammunition.quantity())ammunition.bytes_[0x39]=static_cast<std::uint8_t>(ammunition.quantity()-spent);
    if(ammunition.quantity())return;
    //326D1884 calls unlink1509 directly, not the equipment-unready callback.
    //317A/89 has no item-owned SPC node: its spell17 marker remains until the
    //genuine spell lifecycle removes it.3134/87 is only a strength refusal:
    //accepted87 has no node or inverse scalar edits. Direct1509 may unlink
    //its host binding too; actual item-owned effects still hold below.
    if(c.bound_equipment_effects_.contains(ammunition.id()) && ammunition.byte(0x3e)!=0x89 && ammunition.byte(0x3e)!=0x87)
        throw DecodeError("Combat spent ammunition requires equipment reversal");
    if(std::any_of(c.effects_.begin(),c.effects_.end(),[&](const auto& effect){return effect.equipment_item_id==ammunition.id();}))
        throw DecodeError("Combat spent ammunition has a retained owned effect");
    c.bound_equipment_effects_.erase(ammunition.id());
    if(recoverable && ammunition.byte(0x3e)!=0x89) {
        ammunition.bytes_[0x34]=0;
        state.ground_items.insert(state.ground_items.begin(),std::move(ammunition));
    }
    c.inventory_.items_.erase(c.inventory_.items_.begin()+static_cast<std::ptrdiff_t>(item));
}
std::size_t CombatAttackService::find(const DeploymentState& state,CombatActorIdentity id) {
    const auto it=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return a.id==id;});
    if(it==state.actors.end())throw DecodeError("Combat attack identity is stale");
    return static_cast<std::size_t>(it-state.actors.begin());
}
bool CombatAttackService::helpless(const Character& c) const {
    if(!c.effects_restored())throw DecodeError("Combat attack requires restored effect ownership");
    return std::any_of(helpless_codes_.begin(),helpless_codes_.end(),[&](const auto code){return has(c,code);});
}
std::uint8_t CombatAttackService::distance(const DeploymentActor& a,const DeploymentActor& b) const {
    unsigned smallest=256;
    for(const auto from:spatial_.footprint(a))for(const auto to:spatial_.footprint(b))
        smallest=std::min(smallest,static_cast<unsigned>(CombatSpatialService::range(from,to)));
    if(smallest==256)throw DecodeError("Combat attack requires two placed footprints");
    return static_cast<std::uint8_t>(smallest>>1);
}
bool CombatAttackService::visible(const DeploymentState& state,const DeploymentActor& a,const DeploymentActor& b,unsigned maximum) const {
    for(const auto from:spatial_.footprint(a))for(const auto to:spatial_.footprint(b))
        if(CombatSpatialService::sight(state.battlefield,from,to,static_cast<std::uint16_t>(maximum)).clear)return true;
    return false;
}
void CombatAttackService::validate(const DeploymentState& state) const {health_.validate(state);}
bool CombatAttackService::can_target(const DeploymentState& state,CombatActorIdentity attacker,CombatActorIdentity target,const ItemCatalog& catalog) const {
    return weapon_target_legal(state,attacker,target,catalog,false);
}
std::uint8_t CombatAttackService::native_distance(const DeploymentState& state,CombatActorIdentity attacker,CombatActorIdentity target) const {
    validate(state);return distance(state.actors[find(state,attacker)],state.actors[find(state,target)]);
}
bool CombatAttackService::weapon_target_legal(const DeploymentState& state,CombatActorIdentity attacker,CombatActorIdentity target,const ItemCatalog& catalog,bool allow_same_side) const {
    validate(state);const auto& a=state.actors[find(state,attacker)];const auto& t=state.actors[find(state,target)];
    if(attacker==target || a.removed || t.removed || !a.position || !t.position || !a.shape || !t.shape ||
       !a.character.active() || !t.character.active() || (!allow_same_side && a.character.byte(0x10e)==t.character.byte(0x10e)))return false;
    if(helpless(a.character))return false;
    const auto w=weapon(a.character,catalog);
    if(w.ranged && !w.ammunition && catalog.definition(a.character.inventory().item(*w.item).type()).attack_flags()!=10)return false;
    if(distance(a,t)>w.maximum || !visible(state,a,t,w.maximum))return false;
    if(w.ranged && !w.dual_use)for(const auto& other:state.actors)
        if(!other.removed && other.character.active() && other.position && other.shape &&
           other.character.byte(0x10e)!=a.character.byte(0x10e) && visible(state,a,other,1))return false;
    return true;
}
bool CombatAttackService::target_available(const DeploymentState& state,CombatActorIdentity attacker,CombatActorIdentity target) const {
    validate(state);const auto& a=state.actors[find(state,attacker)];const auto& t=state.actors[find(state,target)];
    if(attacker==target)return true;
    if(!a.character.effects_restored() || !t.character.effects_restored())throw DecodeError("Combat target requires restored effects");
    //326D1085 invokes target phase1 then actor phase0 with a temporary link.
    if(has(t.character,0x47) || (has(t.character,0x25) && signed_byte(t.extension[3])>0) ||
       (has(t.character,0x19) && !has(a.character,0x18)))return false;
    // Native2EE2:2E37, actor phase0/7E, inspects the proposed victim's
    // inventory through the temporary ext0A/C link. A readied item with a
    // 76 name component, or98 plus finalFC, sets the common eligibility veto.
    // These are raw item name bytes, not item types or spell/effect IDs.
    if(has(a.character,0x7e))for(std::size_t i=0;i<t.character.inventory().size();++i) {
        const auto& item=t.character.inventory().item(i);
        if(!item.byte(0x34))continue;
        for(std::size_t part=1;part<=3;++part) {
            const auto name=item.byte(0x2e + part);
            if(name==0x76 || (name==0x98 && item.byte(0x31)==0xfc))return false;
        }
    }
    return true;
}
std::uint8_t CombatAttackService::accuracy_in_place(DeploymentState& state,CombatActorIdentity attacker,
                                                  CombatActorIdentity target,std::uint8_t raw) const {
    if(!loaded_ || raw<1 || raw>20)throw DecodeError("Combat accuracy requires loaded source tables and a native d20 result");
    const auto ai=find(state,attacker),ti=find(state,target);
    if(raw==1)return 1;
    validate(state);
    auto& a=state.actors[ai];auto& t=state.actors[ti];auto& ac=a.character;auto& tc=t.character;
    if(a.removed || t.removed || !ac.effects_restored() || !tc.effects_restored())
        throw DecodeError("Combat accuracy requires retained actor effect graphs");
    auto adjusted=raw==20?std::uint8_t{100}:raw;
    const auto add=[&](int value){adjusted=static_cast<std::uint8_t>(adjusted+value);};
    // Native phase10 order1/2/21/24/31/3/6/12/1A. Query31 prefers own
    // first node, then the first source-roster owner whose icon sees this actor.
    if(has(ac,1))add(1);
    if(has(ac,2))add(-1);
    if(has(ac,0x21))add(-4);
    if(has(ac,0x24))add(-4);
    const CharacterEffect* prayer=nullptr;
    const auto own=std::find_if(ac.effects().begin(),ac.effects().end(),[](const auto& effect){return effect.code()==0x31;});
    if(own!=ac.effects().end())prayer=&*own;
    else for(std::size_t i=0;i<state.actors.size();++i)if(i!=ai && !state.actors[i].removed) {
        const auto& owner=state.actors[i];
        if(!owner.character.effects_restored())throw DecodeError("Combat accuracy aura requires restored effect ownership");
        const auto node=std::find_if(owner.character.effects().begin(),owner.character.effects().end(),[](const auto& effect){return effect.code()==0x31;});
        if(node!=owner.character.effects().end() && visible(state,owner,a,6)) {prayer=&*node;break;}
    }
    if(prayer)add(ac.byte(0x10e)==((prayer->payload[3]&0x10)>>4)?1:-1);
    if(has(ac,3) && tc.byte(0x9f)==4)add(2);
    if(has(ac,6))add(tc.byte(0x9f)==10?1:tc.byte(0x9f)==9 || tc.byte(0x9f)==12?2:tc.byte(0x9f)==4?3:0);
    if(tc.byte(0x9f)==1 && tc.byte(0x6c)==1) {
        const auto name=tc.name();
        for(const auto code:{0x12u,0x1au})if(has(ac,static_cast<std::uint8_t>(code))) {
            const auto base=code==0x12?0x332u:0x372u,count=code==0x12?4u:8u;
            for(unsigned i=1;i<=count;++i)
                if(name==StringStore::active().get(pascal_string_id("START.EXE:module",0xc7c0+base+16*i)))add(1);
        }
    }
    // Native phase16 is19/47/25/2F/30/59.25's FF assignment precedes
    // retained-name reductions, so it cannot be moved after those callbacks.
    for(const auto code:{0x19u,0x47u})if(has(tc,static_cast<std::uint8_t>(code)))add(-4);
    if(has(tc,0x25) && signed_byte(t.extension[3])>0)adjusted=255;
    if(has(tc,0x2f) && ac.byte(0x9f)==1 && (ac.byte(0x6c)&127)==2) {
        if(!t.retained_target)throw DecodeError("Combat defense2F has a native null retained-target dependency");
        const auto& retained=state.actors[find(state,*t.retained_target)].character;
        const auto length=retained.byte(0);
        if(length>15)throw DecodeError("Combat retained target name exceeds its native Pascal bound");
        for(const auto& name:retained_defense_names_) {
            bool equal=name.size()==length;
            for(std::size_t i=0;i<name.size() && equal;++i)equal=name[i]==retained.byte(i+1);
            if(equal)add(-4);
        }
    }
    if(has(tc,0x30) && ac.byte(0x9f)==1) {
        const auto name=ac.name();
        if(name==StringStore::active().get(pascal_string_id("GAME.OVR",0xfec3)) ||
           name==StringStore::active().get(pascal_string_id("GAME.OVR",0xfecb)))add(-4);
    }
    const auto ward=std::find_if(tc.effects_.begin(),tc.effects_.end(),[](const auto& effect){return effect.code()==0x59;});
    if(ward!=tc.effects_.end() && !(ward->payload[3]&0x10)) {ward->payload[3]|=0x10;adjusted=255;}
    return adjusted;
}
CombatDamageOutcome CombatAttackService::apply_damage_in_place(DeploymentState& state,CombatActorIdentity target,
        std::uint8_t amount,CombatActorIdentity resource_actor,std::optional<std::uint8_t> flags,std::mt19937* random) const {
    return health_.apply_damage_in_place(state,target,amount,resource_actor,flags,random);
}
bool CombatAttackService::apply_terminal_status_in_place(DeploymentState& state,CombatActorIdentity target,std::uint8_t status,
        std::optional<std::uint8_t> flags,std::mt19937* random,std::vector<CombatDefeatNotice>* notices) const {
    return health_.apply_terminal_status_in_place(state,target,status,flags,random,notices);
}
bool CombatAttackService::apply_terminal_status_from_module_in_place(DeploymentState& state,CombatActorIdentity target,
        std::uint8_t status,std::span<const std::uint8_t> module,std::vector<CombatDefeatNotice>* notices) {
    return CombatHealthService::apply_terminal_status_from_module_in_place(state,target,status,module,notices);
}
void CombatAttackService::reverse_animate_dead_in_place(DeploymentState& state,CombatActorIdentity target,
        std::size_t effect_index,const SpellCatalog& spells) {
    CombatHealthService::reverse_animate_dead_in_place(state,target,effect_index,spells);
}
CombatAttackResult CombatAttackService::stage(const DeploymentState& state,CombatActorIdentity attacker,CombatActorIdentity target,
                                             const ItemCatalog& catalog,const CombatAttackContext& context,std::mt19937& random,
                                             CombatAttackKind kind) const {
    return stage_attack(state,attacker,target,catalog,context,random,kind,false);
}
CombatAttackResult CombatAttackService::stage_confirmed_friendly(const DeploymentState& state,CombatActorIdentity attacker,CombatActorIdentity target,
        const ItemCatalog& catalog,const CombatAttackContext& context,std::mt19937& random) const {
    return stage_attack(state,attacker,target,catalog,context,random,CombatAttackKind::normal,false,true);
}
CombatAttackResult CombatAttackService::stage_selected_adjacent_ai(const DeploymentState& state,CombatActorIdentity attacker,CombatActorIdentity target,
        const ItemCatalog& catalog,const CombatAttackContext& context,std::mt19937& random) const {
    validate(state);const auto& actor=state.actors[find(state,attacker)];
    if(!actor.character.byte(0x10f) || native_distance(state,attacker,target)!=1)
        throw DecodeError("Native selected adjacent AI attack requires its automatic distance1 caller");
    return stage_attack(state,attacker,target,catalog,context,random,CombatAttackKind::normal,false,false,true);
}
CombatAttackResult CombatAttackService::stage_attack(const DeploymentState& state,CombatActorIdentity attacker,CombatActorIdentity target,
                                             const ItemCatalog& catalog,const CombatAttackContext& context,std::mt19937& random,
                                             CombatAttackKind kind,bool sweep_strike,bool allow_same_side,bool selected_ai_adjacent) const {
    if(kind==CombatAttackKind::retained_crush || kind==CombatAttackKind::retained_rake) {
        validate(state);const auto& a=state.actors[find(state,attacker)];const auto& t=state.actors[find(state,target)];
        if(a.removed || t.removed || !a.position || !t.position || !a.shape || !t.shape || !a.character.active() || !t.character.active())
            throw DecodeError("Retained crushing attack requires active placed native-slot actors");
        if(kind==CombatAttackKind::retained_rake && a.retained_target!=target)
            throw DecodeError("Native raking attack requires its retained on-hit victim");
    } else if(kind==CombatAttackKind::departure_reaction || kind==CombatAttackKind::arrival_guard) {
        validate(state);const auto& a=state.actors[find(state,attacker)];const auto& t=state.actors[find(state,target)];
        //08B2/0627 call1884 directly with no projectile, using the current
        //cached weapon profile. They do not optimize/switch weapons or apply
        //manual Aim's ammunition and nondual-adjacency restrictions.
        if(a.removed || t.removed || !a.position || !t.position || !a.shape || !t.shape || !a.character.active() ||
           !t.character.active() || a.character.byte(0x10e)==t.character.byte(0x10e) || helpless(a.character) ||
           distance(a,t)!=1 || !visible(state,a,t,1))throw DecodeError("Combat reaction requires native opposing adjacent footprints");
    } else if(!weapon_target_legal(state,attacker,target,catalog,allow_same_side))throw DecodeError("Combat target is not legal for this weapon");
    const auto ai=find(state,attacker),ti=find(state,target);
    CombatAttackResult result;result.state=state;auto rng=random;
    auto& a=result.state.actors[ai];auto& t=result.state.actors[ti];auto& ac=a.character;auto& tc=t.character;
    if(!ac.equipment_effects_bound() || !tc.equipment_effects_bound())throw DecodeError("Combat attack requires equipment effect bindings");
    // 326D:1085 source eligibility precedes facing, expenditure and rolls.
    if(!sweep_strike && !selected_ai_adjacent && kind!=CombatAttackKind::arrival_guard && kind!=CombatAttackKind::retained_crush && kind!=CombatAttackKind::retained_rake && !target_available(state,attacker,target))
        throw DecodeError("Combat attack eligibility vetoed by source effect");
    auto w=weapon(ac,catalog);const auto range=distance(a,t);
    if(w.dual_use && range==1)w.ammunition.reset();
    if(kind!=CombatAttackKind::normal)w.ammunition.reset();
    std::vector<CombatStrike::Travel> travel;
    const auto retain_travel=[&](std::size_t item) {
        travel.push_back({ac.inventory().item(item).type(),static_cast<std::uint8_t>(direction(*a.position,*t.position,state.battlefield.tables)),*a.position,*t.position});
    };
    if(w.ammunition)retain_travel(*w.ammunition);
    if(w.item && ac.inventory().item(*w.item).type()==0x2f)retain_travel(*w.item);
    // A ranged attack's primary budget must already have been capped by the
    // round service. A malformed stale stack cannot wrap during consumption.
    if(w.ammunition && std::max(1u,static_cast<unsigned>(ac.inventory().item(*w.ammunition).quantity()))<ac.byte(0x113))
        throw DecodeError("Combat ammunition no longer covers prepared attack budget");
    //326D0E82/10BA2433: freeze the native adjacent roster before any hit.
    //Only when weak opponents outnumber the ordinary primary budget does
    //the source substitute one primary attempt per weak target, capped by5.
    if(!sweep_strike && kind==CombatAttackKind::normal && tc.byte(0x73)==0 && range==1 && ac.byte(0x113)<a.extension[5]) {
        struct Candidate {std::size_t actor;std::uint8_t distance,direction;};
        std::vector<std::size_t> slots;
        for(std::size_t i=0;i<state.actors.size();++i) {
            const auto& actor=state.actors[i];if(!actor.removed && actor.position && actor.shape)slots.push_back(i);
        }
        std::sort(slots.begin(),slots.end(),[&](auto l,auto r){return state.actors[l].native_slot<state.actors[r].native_slot;});
        std::vector<Candidate> adjacent;
        for(const auto i:slots) {
            std::optional<Candidate> closest;
            for(const auto to:spatial_.footprint(state.actors[i]))for(const auto from:spatial_.footprint(a)) {
                const auto sight=CombatSpatialService::sight(state.battlefield,from,to,1);
                if(sight.clear && (!closest || sight.distance<closest->distance))
                    closest=Candidate{i,sight.distance,static_cast<std::uint8_t>(direction(from,to,state.battlefield.tables))};
            }
            if(closest)adjacent.push_back(*closest);
        }
        //4E80002E swaps, including their effect on tied entries, precede
        //2433's side filter. Do not use a stable sort or an actor-vector order.
        for(std::size_t i=0;i<adjacent.size();++i)for(std::size_t j=i+1;j<adjacent.size();++j)
            if(adjacent[j].distance<adjacent[i].distance || (adjacent[j].distance==adjacent[i].distance &&
               adjacent[j].direction<adjacent[i].direction && adjacent[j].direction%2<=adjacent[i].direction%2))
                std::swap(adjacent[i],adjacent[j]);
        std::erase_if(adjacent,[&](const auto& candidate){return state.actors[candidate.actor].character.byte(0x10e)==ac.byte(0x10e);});
        const auto weak=static_cast<unsigned>(std::count_if(adjacent.begin(),adjacent.end(),[&](const auto& candidate){return state.actors[candidate.actor].character.byte(0x73)==0;}));
        if(weak>ac.byte(0x113)) {
            const auto selected=std::find_if(adjacent.begin(),adjacent.end(),[&](const auto& candidate){return candidate.actor==ti;});
            if(selected==adjacent.end())throw DecodeError("Combat sweep selected target is absent from the native adjacent roster");
            std::iter_swap(adjacent.begin(),selected);
            const auto limit=std::min(weak,static_cast<unsigned>(a.extension[5]));
            CombatAttackResult sweep;sweep.state=state;unsigned attempted=0;
            for(const auto candidate:adjacent) {
                if(attempted==limit)break;
                if(sweep.state.actors[candidate.actor].character.byte(0x73)!=0)continue;
                sweep.state.actors[ai].character.set_byte(0x113,1);
                const auto id=sweep.state.actors[candidate.actor].id;
                auto burst=stage_attack(sweep.state,attacker,id,catalog,context,rng,kind,true);
                sweep.state=std::move(burst.state);
                sweep.native_hit_counts=burst.native_hit_counts;
                sweep.native_spent_counts=burst.native_spent_counts;
                sweep.strikes.insert(sweep.strikes.end(),burst.strikes.begin(),burst.strikes.end());
                if(burst.standalone_travel)throw DecodeError("Combat sweep lost a native strike burst");
                ++attempted;
            }
            if(sweep.strikes.empty())throw DecodeError("Combat sweep produced no owned strikes");
            auto& first=sweep.strikes.front();
            auto notice=first.preparation?std::make_shared<CombatMagicResult>(*first.preparation):std::make_shared<CombatMagicResult>();
            notice->feedback.push_back({attacker,pascal_string_id("GAME.OVR",0x127fb+0x0e7b),false,10,true,{}});
            notice->events.insert(notice->events.begin(),{CombatMagicEventKind::feedback,notice->feedback.size()-1,std::make_shared<const DeploymentState>(state),{}});
            notice->state=first.before?*first.before:sweep.state;first.preparation=std::move(notice);
            //Manual2C3D and automatic2AF3 callers both Quit after a sweep,
            //even when the last victim died before consuming every family.
            stop(sweep.state.actors[ai]);sweep.turn_finished=true;
            validate(sweep.state);random=std::move(rng);return sweep;
        }
    }
    if(kind!=CombatAttackKind::departure_reaction && kind!=CombatAttackKind::retained_crush && kind!=CombatAttackKind::retained_rake) {
        const auto target_to_actor=direction(*t.position,*a.position,state.battlefield.tables);
        const auto difference=(target_to_actor+8-t.extension[9])%8;
        const auto turn=std::min(difference,8-difference);
        t.extension[0x12]=static_cast<std::uint8_t>((t.extension[0x12]+turn)%8);
        t.extension[0xf]=static_cast<std::uint8_t>(t.extension[0xf]+1);
        if(t.extension[0xf]<3)t.extension[9]=static_cast<std::uint8_t>(target_to_actor);
    }
    if(kind==CombatAttackKind::retained_rake && t.extension[0xf]<3)
        t.extension[9]=static_cast<std::uint8_t>(direction(*t.position,*a.position,state.battlefield.tables));
    a.extension[9]=static_cast<std::uint8_t>(direction(*a.position,*t.position,state.battlefield.tables));
    a.retained_target=target; //326D1884 writes the selected actor's ext0A/C link.
    //1884 retains target/exposure/facing even when both native counters are
    //zero, then Quits without1406, hit/damage draws or ammunition expenditure.
    //This includes0B3C's >20 fallback tail after its earlier Quit.
    if(!ac.byte(0x113) && !ac.byte(0x114)) {
        if(!travel.empty())result.standalone_travel=CombatWeaponPlayback{attacker,travel,std::make_shared<const DeploymentState>(result.state)};
        stop(a);result.turn_finished=true;validate(result.state);return result;
    }
    a.extension[8]=1;
    const bool auto_hit=helpless(tc);
    if(auto_hit) {
        unsigned family=a.extension[4];
        if(family<1 || family>2)throw DecodeError("Combat attack family cursor is invalid");
        while(!ac.byte(0x112+family)) {
            if(family==1)throw DecodeError("Combat helpless execution has no remaining attack family");
            --family;
        }
        a.extension[4]=static_cast<std::uint8_t>(family);
        CombatStrike strike;strike.attacker=attacker;strike.target=target;strike.primary=family==1;strike.hit=true;
        strike.before=std::make_shared<const DeploymentState>(result.state);strike.travel=travel;
        strike.before_hp=tc.hit_points();strike.damage=static_cast<std::uint8_t>(tc.hit_points()+5);
        std::vector<CombatDefeatNotice> wakeups;
        strike.interrupted_spell=health_.damage(result.state,ti,strike.damage,ai,std::uint8_t{0},&rng,&wakeups);strike.after_hp=tc.hit_points();strike.status=tc.status();
        strike.after=std::make_shared<const DeploymentState>(result.state);result.strikes.push_back(strike);
        if(!wakeups.empty()) {
            auto playback=std::make_shared<CombatMagicResult>();playback->state=result.state;
            for(const auto& notice:wakeups) {
                playback->feedback.push_back({notice.actor,notice.text_id});
                playback->events.push_back({CombatMagicEventKind::feedback,playback->feedback.size()-1,strike.before,strike.after});
            }
            result.strikes.back().followup=std::move(playback);
        }
        while(true) {
            const auto invis=std::find_if(ac.effects_.begin(),ac.effects_.end(),[](const auto& e){return e.code()==0x19;});
            if(invis==ac.effects_.end())break;
            //0918's removal only edits eligibility681E and old6822 scratch;
            //the next0CB7 resets6822, and no caller consumes681E here.
            ac.effects_.erase(invis);
        }
        ac.set_byte(0x113,0);ac.set_byte(0x114,0);
        result.native_spent_counts[family-1]=1;
        if(w.ammunition)spend_ammunition(result.state,ai,*w.ammunition,result.native_spent_counts[0],w.dual_use);
        EquipmentRules::recalculate(ac,catalog);stop(a);result.turn_finished=true;
        validate(result.state);random=std::move(rng);return result;
    }
    if(w.item && (tc.byte(0x6c)>0x80 || (tc.byte(0x6c)&7)>1)) {
        const auto& def=catalog.definition(ac.inventory().item(*w.item).type());
        ac.set_byte(0x115,def.byte(2));ac.set_byte(0x117,def.byte(3));
        ac.set_byte(0x119,static_cast<std::uint8_t>(ac.byte(0x119)-def.byte(11)+def.byte(4)));
    }
    EquipmentRules::recalculate(tc,catalog);
    // Phase11 runs21 then11. The08/09/2D/2E alignment handlers alter only
    // save scratch6816 and prior hit scratch6822 here; 0CB7 overwrites6822
    // before consuming it. They do not patch the target's armor bytes.
    if(has(tc,0x21)) {
        tc.set_byte(0x111,static_cast<std::uint8_t>(tc.byte(0x111)-4));
        tc.set_byte(0x112,static_cast<std::uint8_t>(tc.byte(0x112)-4));
    }
    if(has(tc,0x11) && tc.byte(0x111)<57)tc.set_byte(0x111,57);
    std::shared_ptr<CombatMagicResult> preparation;
    if(has(tc,0x1e)) {
        const auto before=t.extension[2]?std::make_shared<const DeploymentState>(result.state):nullptr;
        // Native phase11 runs1E last.0A65 rebuilds equipment again, replacing
        // earlier21/11 armor edits, then copies its adjusted rear cache to111.
        if(CombatCloudService::nausea_in_place(t,catalog)) {
            preparation=std::make_shared<CombatMagicResult>();preparation->state=result.state;
            preparation->feedback.push_back({target,pascal_string_id("GAME.OVR",0xf6b4)});
            preparation->events.push_back({CombatMagicEventKind::feedback,0,before,{}});
            preparation->events.push_back({CombatMagicEventKind::state,0,{},std::make_shared<const DeploymentState>(result.state)});
        }
    }
    const auto equipped=EquipmentRules::inspect(ac,catalog);
    bool backstab=signed_byte(ac.byte(0x9c))>0 &&
        (!equipped.slots[2] || ac.inventory().item(*equipped.slots[2]).type()==0x32) &&
        (!w.item || [&]{const auto type=ac.inventory().item(*w.item).type();return type==7 || type==8 || (type>0x22 && type<0x26);}()) &&
        t.extension[0xf]>1 && direction(*a.position,*t.position,state.battlefield.tables)==t.extension[9];
    bool rear=kind==CombatAttackKind::departure_reaction || kind==CombatAttackKind::retained_crush || (t.extension[0xf]>1 && t.extension[0x12]>4 && direction(*a.position,*t.position,state.battlefield.tables)==t.extension[9]);
    std::uint8_t defense=backstab?static_cast<std::uint8_t>(tc.byte(0x112)-2):tc.byte(rear?0x112:0x111);
    unsigned short_range=range;
    if(w.ranged)short_range=(catalog.definition(ac.inventory().item(*w.item).type()).byte(12)-1)/3;
    unsigned remaining_range=range;
    if(remaining_range!=short_range && remaining_range>=short_range) {remaining_range-=short_range;defense=static_cast<std::uint8_t>(defense+2);}
    if(remaining_range!=short_range && remaining_range>=short_range)defense=static_cast<std::uint8_t>(defense+3);
    std::array<std::uint8_t,2> spent_counts{};bool forced_quit=false;
    //1406 owns byte hit counters6823/6824 for this burst, distinct from
    //remaining113/114 and family cursor4. Deflection decrements6823.
    std::array<std::uint8_t,2> hit_counts{};
    const unsigned first=a.extension[4];
    if(first<1 || first>2)throw DecodeError("Combat attack family cursor is invalid");
    for(unsigned family=first;family>=1;--family) {
        while(ac.byte(0x112+family) && tc.active()) {
            ac.set_byte(0x112+family,static_cast<std::uint8_t>(ac.byte(0x112+family)-1));
            a.extension[4]=static_cast<std::uint8_t>(family);++spent_counts[family-1];
            // Attack removes every first19 successively; nonzero expiry flags
            // require their real removal service rather than erase-and-guess.
            while(true) {
                const auto invis=std::find_if(ac.effects_.begin(),ac.effects_.end(),[](const auto& e){return e.code()==0x19;});
                if(invis==ac.effects_.end())break;
                //Flagged0918 has only scratch writes discarded before hit.
                ac.effects_.erase(invis);
            }
            CombatStrike strike;std::vector<CombatStrike> nested_strikes;
            strike.attacker=attacker;strike.target=target;strike.primary=family==1;
            if(result.strikes.empty()) {strike.travel=travel;strike.preparation=preparation;}
            strike.before=std::make_shared<const DeploymentState>(result.state);
            strike.before_hp=tc.hit_points();strike.d20=roll(rng,20);
            const auto hit_roll=accuracy_in_place(result.state,attacker,target,strike.d20);
            if(strike.d20>1) {
                strike.hit=signed_byte(static_cast<std::uint8_t>(hit_roll))>=0 && defense<=
                    signed_byte(static_cast<std::uint8_t>(hit_roll))+ac.byte(0x110)+signed_byte(context.side_attack_bonus[ac.byte(0x10e)]);
            }
            if(strike.hit) {
                ++hit_counts[family-1];
                std::uint8_t damage=0;const auto count=ac.byte(0x114+family),sides=ac.byte(0x116+family);
                for(unsigned die=0;die<count;++die)damage=static_cast<std::uint8_t>(damage+roll(rng,sides));
                damage=static_cast<std::uint8_t>(damage+ac.byte(0x118+family));
                if(backstab)damage=static_cast<std::uint8_t>(damage*((signed_byte(ac.byte(0x9c))>>2)+2));
                //2EE20A3D: unsigned byte amount minus its logical quarter.
                if(has(ac,0x1d))damage=static_cast<std::uint8_t>(damage-(damage>>2));
                if(has(ac,3) && tc.byte(0x9f)==4)damage=static_cast<std::uint8_t>(damage+2);
                if(has(ac,6))damage=static_cast<std::uint8_t>(damage+(tc.byte(0x9f)==10?1:tc.byte(0x9f)==9 || tc.byte(0x9f)==12?2:tc.byte(0x9f)==4?3:0));
                //Retained legacy projection:09C0 draws, then treats pending
                //681B as zero. This is not a proven native action invariant:
                //54 can write10 and1884/1406 do not clear resident scratch.
                //C07 tracks full cross-action/load ownership as unresolved;
                //keep the existing projection and exact first-node draw.
                const auto absorption=std::find_if(tc.effects_.begin(),tc.effects_.end(),[](const auto& e){return e.code()==0x1c;});
                if(absorption!=tc.effects_.end())(void)roll(rng,static_cast<unsigned>(absorption->payload[3])+1);
                // 2EE2:101D returns the selected primary/projectile with a
                // genuine primary fallback. Its callers use ITEMS byte7 for
                // damage classification, not projectile-selection byte14.
                const auto damage_equipped=EquipmentRules::inspect(ac,catalog);auto selected=damage_equipped.slots[0];
                if(selected) {
                    const auto primary=selected;
                    const auto f=catalog.definition(ac.inventory().item(*selected).type()).attack_flags();
                    if(f&8) {
                        if(f&1)selected=damage_equipped.slots[11];
                        if(f&0x80)selected=damage_equipped.slots[12];
                    }
                    if(!selected)selected=primary;
                }
                const Item* selected_weapon=selected?&ac.inventory().item(*selected):nullptr;
                const auto classification=selected_weapon?catalog.definition(selected_weapon->type()).byte(7):std::uint8_t{};
                //2EE20FAC is a projectile avoidance callback, not immunity.
                //It still draws on amount0, and repeated phase5 codes do not
                //short circuit after a preceding successful avoidance.
                const auto avoid=[&](unsigned chance) {
                    if(!w.item || range<=1 || roll(rng,100)>chance)return;
                    auto before=std::make_shared<const DeploymentState>(result.state);
                    auto notice=strike.preparation?std::make_shared<CombatMagicResult>(*strike.preparation):std::make_shared<CombatMagicResult>();
                    notice->feedback.push_back({target,pascal_string_id("GAME.OVR",60507+0x0fa2)});
                    notice->events.push_back({CombatMagicEventKind::feedback,notice->feedback.size()-1,before,{}});
                    notice->state=result.state;strike.preparation=std::move(notice);
                    damage=0;--hit_counts[0]; //Native also sets last hit scratch6822=FF.
                };
                if(has(tc,0x29) && selected_weapon && !selected_weapon->byte(0x32))avoid(100);
                if(has(tc,0x68))avoid(60);
                if(has(tc,0x78) && w.item && (ac.inventory().item(*w.item).type()==0x57 || ac.inventory().item(*w.item).type()==0x58))avoid(50);
                if(has(tc,0x65))(void)CombatMagicEffects::schedule_regeneration_in_place(tc);
                // Native phase5 fixed code order, first matching node per code.
                // The pure weapon callbacks add no RNG and do not unlink nodes.
                if(has(tc,0x73) && selected_weapon && (!classification || (classification&1)))damage=static_cast<std::uint8_t>(damage>>1);
                if(has(tc,0x74) && selected_weapon && signed_byte(selected_weapon->byte(0x32))>0)damage=static_cast<std::uint8_t>(damage>>1);
                if(has(tc,0x77) && (!selected_weapon || !selected_weapon->byte(0x32)) &&
                   (signed_byte(ac.byte(0x2e))>0 || signed_byte(ac.byte(0x73))<4))damage=0;
                if(has(tc,0x7b)) {
                    if(!selected_weapon)damage=0;
                    else if(!selected_weapon->byte(0x32))damage=selected_weapon->byte(0x31)==0xb1?static_cast<std::uint8_t>(damage>>1):0;
                }
                if(has(tc,0x60) && (!selected_weapon || (!selected_weapon->byte(0x32) && selected_weapon->byte(0x31)!=0xb1)))damage=0;
                if(has(tc,0x5e) && selected_weapon && (classification&0x81))damage=static_cast<std::uint8_t>(damage>>1);
                //2EE2:2D38 reads the actual primary56, not selected ammo.
                //Ordinary1406 sets flags6819=0, so no inherited681C add here.
                if(has(tc,0x7a) && w.item && ac.inventory().item(*w.item).type()==0x56)
                    damage=static_cast<std::uint8_t>(roll(rng,8)+roll(rng,8)+roll(rng,8));
                // 2EE2:2B20 /75 runs last: a selected primary55 rewrites even
                // an earlier zero result. It is independent of ammunition.
                if(has(tc,0x75) && w.item && ac.inventory().item(*w.item).type()==0x55)
                    damage=static_cast<std::uint8_t>(roll(rng,6)+1);
                strike.damage=damage;
                std::vector<CombatDefeatNotice> wakeups;
                strike.interrupted_spell=health_.damage(result.state,ti,damage,ai,std::uint8_t{0},&rng,&wakeups);
                strike.after_hp=tc.hit_points();strike.status=tc.status();
                strike.after=std::make_shared<const DeploymentState>(result.state);
                if(!tc.active() && !wakeups.empty()) {
                    auto playback=std::make_shared<CombatMagicResult>();playback->state=result.state;
                    for(const auto& notice:wakeups) {
                        playback->feedback.push_back({notice.actor,notice.text_id});
                        playback->events.push_back({CombatMagicEventKind::feedback,playback->feedback.size()-1,strike.before,strike.after});
                    }
                    strike.followup=std::move(playback);
                }
                if(tc.active()) {
                    auto playback=std::make_shared<CombatMagicResult>();
                    for(const auto& notice:wakeups) {
                        playback->feedback.push_back({notice.actor,notice.text_id});
                        playback->events.push_back({CombatMagicEventKind::feedback,playback->feedback.size()-1,strike.before,strike.after});
                    }
                    std::uint8_t callback_flags=0; //0191 clears6819 before phase4/5.
                    const auto drain=[&] {
                        for(const auto code:{0x55u,0x56u})if(has(ac,static_cast<std::uint8_t>(code)))
                            drain_levels(result.state,attacker,target,code==0x55?1:2,catalog,*playback,callback_flags,rng);
                    };
                    const auto snapshot=[&]{return std::make_shared<const DeploymentState>(result.state);};
                    const auto notice=[&](std::size_t text,bool animation=false) {
                        playback->feedback.push_back({target,pascal_string_id("GAME.OVR",60507+text),animation});
                        playback->events.push_back({CombatMagicEventKind::feedback,playback->feedback.size()-1,snapshot(),{}});
                    };
                    const auto append=[&](Character& c,std::uint8_t code,std::uint16_t duration,std::uint8_t context_byte,std::uint8_t flag) {
                        CharacterEffect e;e.payload={code,static_cast<std::uint8_t>(duration),static_cast<std::uint8_t>(duration>>8),context_byte,flag};
                        e.source_file="GAME.OVR:attack callback";c.effects_.push_back(std::move(e));
                    };
                    const auto adopt=[&](DeploymentState candidate) {
                        //SavingThrowService stages a whole candidate. Preserve
                        //this burst's borrowed actor/Character storage.
                        if(candidate.actors.size()!=result.state.actors.size())throw DecodeError("Combat on-hit callback changed the native roster");
                        for(std::size_t i=0;i<candidate.actors.size();++i)result.state.actors[i]=std::move(candidate.actors[i]);
                        result.state.battlefield=std::move(candidate.battlefield);result.state.occupancy=candidate.occupancy;
                        result.state.bodies=std::move(candidate.bodies);result.state.clouds=std::move(candidate.clouds);
                        result.state.ground_items=std::move(candidate.ground_items);
                    };
                    const auto save=[&](std::int8_t modifier) {
                        auto candidate=result.state;
                        const CombatMagicContext native{attacker,0,0,0,0,false};
                        const auto saved=CombatMagicEffects::save(candidate,target,0,modifier,native,rng,0).saved;
                        adopt(std::move(candidate));return saved;
                    };
                    const auto require_pending_context=[&] {
                        //2556/064E inspect681B;09C0 can consume a shield
                        //using681B/6820 after its draw. Neither4F nor50 owns
                        //those fields. A zero-count1C alwaysdraws1, so its
                        //native <=1 short circuit never observes the context.
                        const auto shield=std::find_if(tc.effects().begin(),tc.effects().end(),[](const auto& e){return e.code()==0x1c;});
                        if(has(tc,0x5b) || has(tc,0x11) || (shield!=tc.effects().end() && shield->payload[3]!=0))
                            throw DecodeError("Combat on-hit damage requires native ambient pending-cast/absorption context for5B/11/1C");
                    };
                    const auto poison=[&](std::int8_t modifier) {
                        if(save(modifier))return;
                        notice(0x152f);append(tc,0x37,0,255,0);
                        const auto before=snapshot();const auto hp=tc.hit_points();
                        notice(0x153b);
                        std::vector<CombatDefeatNotice> cleanup_notes;
                        if(apply_terminal_status_in_place(result.state,target,6,callback_flags,&rng,&cleanup_notes)) {
                            for(const auto& defeat_note:cleanup_notes) {
                                playback->feedback.push_back({defeat_note.actor,defeat_note.text_id});
                                playback->events.push_back({CombatMagicEventKind::feedback,playback->feedback.size()-1,before,snapshot()});
                            }
                            playback->damage.push_back({attacker,target,0,hp,tc.hit_points(),tc.status(),false,{},0});
                            playback->events.push_back({CombatMagicEventKind::terminal,playback->damage.size()-1,before,snapshot()});
                        }
                    };
                    const auto paralyze=[&](std::uint16_t duration) {
                        //15EB ignores its first argument, including45's FE.
                        if(save(0))return;
                        notice(0x15de,true);append(tc,0x34,duration,12,0);
                        playback->events.push_back({CombatMagicEventKind::state,0,{},snapshot()});
                    };
                    const auto disease=[&] {
                        if(has(tc,0x32))return;
                        append(tc,0x32,0x3840,255,1);
                        const auto progress=static_cast<std::uint8_t>(roll(rng,6)*16+15);
                        append(tc,0x39,0xa8c0,progress,1);notice(0x23e0);
                    };
                    if(family==1) {
                        drain();
                        if(has(ac,0x57))disease();
                        if(has(ac,0x44) && tc.byte(0x2e)!=2)paralyze(63);
                    } else {
                        for(const auto code:{0x40u,0x41u,0x42u})if(has(ac,static_cast<std::uint8_t>(code)))
                            poison(static_cast<std::int8_t>(code==0x40?0:code==0x41?4:2));
                        if(has(ac,0x43))paralyze(static_cast<std::uint16_t>(roll(rng,8)+roll(rng,8)));
                        if(has(ac,0x44) && tc.byte(0x2e)!=2)paralyze(63);
                        if(has(ac,0x45))paralyze(static_cast<std::uint16_t>(roll(rng,9)+10));
                        if(has(ac,0x46))poison(-2);
                    }
                    if(has(ac,0x4f)) {
                            callback_flags=9; //2EE21A19 writes9 before49D51344.
                            // 2EE2:1A19 uses the actual selected attacker and
                            // its retained ext0A/C victim, not effect ownership.
                            if(!a.retained_target || *a.retained_target!=target)
                                throw DecodeError("Combat4F requires its retained on-hit victim");
                            require_pending_context();
                            //1A19 owns flags9 and0E2E owns dicecount2, but
                            //neither binds681B/6817/6820 or26C2 casterlevel.
                            if(has(tc,0x69) || has(tc,0x6a))
                                throw DecodeError("Combat on-hit4F requires native ambient cast-level context for69/6A");
                            const auto amount=static_cast<std::uint8_t>(roll(rng,10)+roll(rng,10));
                            const CombatMagicContext native{attacker,0,0,0,2,false};
                            auto extra=CombatMagicEffects::weapon_callback_damage(*this,catalog,result.state,target,amount,native,rng);
                            const auto before=std::make_shared<const DeploymentState>(result.state);
                            for(const auto& effect_notice:extra.notices) {
                                playback->feedback.push_back({extra.state.actors.at(effect_notice.member).id,effect_notice.text_id});
                                playback->events.push_back({CombatMagicEventKind::feedback,playback->feedback.size()-1,before,{}});
                            }
                            const auto& outcome=extra.outcome;
                            playback->damage.push_back({attacker,target,outcome.amount,outcome.before_hp,outcome.after_hp,outcome.status,false,outcome.interrupted_spell,9});
                            playback->events.push_back({CombatMagicEventKind::damage,playback->damage.size()-1,before,std::make_shared<const DeploymentState>(extra.state)});
                            // Keep actor/Character references used by this burst
                            // stable while adopting the complete staged result.
                            for(std::size_t i=0;i<result.state.actors.size();++i)result.state.actors[i]=std::move(extra.state.actors[i]);
                            result.state.battlefield=std::move(extra.state.battlefield);result.state.occupancy=extra.state.occupancy;
                            result.state.bodies=std::move(extra.state.bodies);result.state.clouds=std::move(extra.state.clouds);
                            result.state.ground_items=std::move(extra.state.ground_items);
                        }
                    if(family==1) {
                        if(has(ac,0x50)) {
                            //1A4C initializes flags10 and dicecount1, but does
                            //not initialize ambient6817 observed by69/6A.
                            if(has(tc,0x69) || has(tc,0x6a))throw DecodeError("Combat on-hit50 requires native ambient pending-effect context for69/6A");
                            require_pending_context();
                            callback_flags=0x10;
                            const auto amount=roll(rng,4);const auto before=snapshot();
                            const CombatMagicContext native{attacker,0,0,0,1,false};
                            auto extra=CombatMagicEffects::on_hit_damage(*this,catalog,result.state,target,amount,callback_flags,native,rng);
                            for(const auto& message:extra.notices) {
                                playback->feedback.push_back({extra.state.actors.at(message.member).id,message.text_id});
                                playback->events.push_back({CombatMagicEventKind::feedback,playback->feedback.size()-1,before,{}});
                            }
                            const auto& outcome=extra.outcome;
                            playback->damage.push_back({attacker,target,outcome.amount,outcome.before_hp,outcome.after_hp,outcome.status,false,outcome.interrupted_spell,callback_flags});
                            if(outcome.amount)playback->events.push_back({CombatMagicEventKind::damage,playback->damage.size()-1,before,std::make_shared<const DeploymentState>(extra.state)});
                            adopt(std::move(extra.state));
                        }
                        const auto retain=[&](std::uint8_t code,std::uint16_t duration) {
                            if(!t.native_slot)throw DecodeError("Combat on-hit binding requires the native victim slot");
                            append(ac,code,duration,t.native_slot,1);
                            append(tc,0x3a,0,255,0);t.extension[6]=0;
                            if(code==0x4a)notice(0x1928);
                            else playback->events.push_back({CombatMagicEventKind::state,0,{},snapshot()});
                        };
                        if(has(ac,0x4c))retain(0x4b,0xcf);
                        if(has(ac,0x49) && hit_counts[0]==2 && tc.active()) {
                            //39D2 re-enters real1884, not a flat damage bonus.
                            //Its two secondary strikes reset shared1406 hit
                            //counters and preserve secondary damage bonus11A.
                            a.extension[4]=2;ac.set_byte(0x114,2);ac.set_byte(0x116,2);ac.set_byte(0x118,4);ac.set_byte(0x113,0);
                            playback->feedback.push_back({attacker,pascal_string_id("GAME.OVR",0x127fb+0x39c9)});
                            playback->events.push_back({CombatMagicEventKind::feedback,playback->feedback.size()-1,snapshot(),{}});
                            playback->state=result.state;strike.followup=std::move(playback);playback=std::make_shared<CombatMagicResult>();
                            auto nested=stage_attack(result.state,attacker,target,catalog,context,rng,CombatAttackKind::retained_rake,false);
                            if(nested.strikes.empty() || nested.standalone_travel)throw DecodeError("Native raking attack lost its owned nested strike burst");
                            hit_counts=nested.native_hit_counts;spent_counts=nested.native_spent_counts;
                            nested_strikes=std::move(nested.strikes);adopt(std::move(nested.state));
                            stop(a);forced_quit=true; //39D2 calls2637 even after early victim defeat.
                        }
                        if(has(ac,0x4d) && tc.byte(0x10)<19)retain(0x4a,0);
                    } else {
                        drain();if(has(ac,0x57))disease();
                    }
                    if(!playback->events.empty()) {
                        playback->state=result.state;
                        if(nested_strikes.empty())strike.followup=std::move(playback);
                        else {
                            //Only later phase2/4D feedback/state follows49.
                            //Place it after the final nested strike and its own
                            //followups, retaining every native before/after.
                            auto& last=nested_strikes.back();
                            auto merged=last.followup?std::make_shared<CombatMagicResult>(*last.followup):std::make_shared<CombatMagicResult>();
                            const auto offset=merged->feedback.size();
                            merged->feedback.insert(merged->feedback.end(),playback->feedback.begin(),playback->feedback.end());
                            for(auto event:playback->events) {
                                if(event.kind==CombatMagicEventKind::feedback)event.index+=offset;
                                else if(event.kind!=CombatMagicEventKind::state)throw DecodeError("Native post-rake callback lost ordered event ownership");
                                merged->events.push_back(std::move(event));
                            }
                            merged->state=result.state;last.followup=std::move(merged);
                        }
                    }
                }
            }
            if(!strike.after) {
                strike.after_hp=tc.hit_points();strike.status=tc.status();
                strike.after=std::make_shared<const DeploymentState>(result.state);
            }
            result.strikes.push_back(strike);
            result.strikes.insert(result.strikes.end(),nested_strikes.begin(),nested_strikes.end());
        }
    }
    a.extension[5]=0;
    if(w.ammunition)spend_ammunition(result.state,ai,*w.ammunition,spent_counts[0],w.dual_use);
    EquipmentRules::recalculate(ac,catalog);
    result.native_hit_counts=hit_counts;result.native_spent_counts=spent_counts;
    result.turn_finished=forced_quit || (ac.byte(0x113)==0 && ac.byte(0x114)==0);
    if(result.turn_finished)stop(a);
    validate(result.state);random=std::move(rng);return result;
}
}
