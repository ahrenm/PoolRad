#include "gbengine/combat/opponent_magic_service.hpp"
#include <algorithm>
#include <limits>

namespace GBEngine {
namespace {
unsigned roll(std::mt19937& random,unsigned sides) {
    if(!sides)throw DecodeError("Automatic combat choice has no candidates");
    const auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    std::uint32_t n;do {n=static_cast<std::uint32_t>(random());}while(n<threshold);
    return n%sides+1;
}
int signed_byte(std::uint8_t n){return n<128?n:static_cast<int>(n)-256;}
bool has(const Character& c,unsigned code) {
    if(!c.effects_restored())throw DecodeError("Automatic magic requires restored effect ownership");
    return std::any_of(c.effects().begin(),c.effects().end(),[&](const auto& e){return e.code()==code;});
}
std::array<std::uint8_t,16> row(const SpellCatalog& catalog,std::uint8_t id) {
    if(!id || id>67)throw DecodeError("Automatic magic encountered an unknown raw cast identifier");
    return id<=56?catalog.definition(SpellId{id}).raw_bytes():catalog.item_definition(ItemCastId{id}).raw_bytes();
}
unsigned range(const Character& caster,const std::array<std::uint8_t,16>& raw,bool item) {
    const auto level=item?6u:raw[0]==2?12u:caster.byte(raw[0]==0?0x96:0x9b);
    auto n=static_cast<std::uint8_t>(raw[2]+raw[3]*level);
    if((!n && raw[6]) || n==255)n=1;
    return n;
}
bool opposing_active(const DeploymentState& state,const DeploymentActor& actor) {
    const auto side=static_cast<std::uint8_t>(actor.character.byte(0x10e)==0);
    return std::any_of(state.actors.begin(),state.actors.end(),[side](const auto& a){
        return !a.removed && a.character.active() && a.character.byte(0x10e)==side;
    });
}
unsigned cone(CombatPosition from,CombatPosition to,const BattlefieldTables& tables) {
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
    throw DecodeError("Automatic magic target has no source direction cone");
}
void sort_visible(std::vector<CombatTarget>& entries) {
    // Callers insert native-slot order. Native pairwise exchanges retain their
    // non-stable tie behavior rather than a modern strict sort predicate.
    for(std::size_t i=0;i<entries.size();++i)for(std::size_t j=i+1;j<entries.size();++j)
        if(entries[j].distance<entries[i].distance || (entries[j].distance==entries[i].distance &&
           entries[j].direction<entries[i].direction && entries[j].direction%2<=entries[i].direction%2))
            std::swap(entries[i],entries[j]);
}
}
OpponentMagicService OpponentMagicService::load(const IFileSystem& files) {
    OpponentMagicService result;result.spatial_=CombatSpatialService::load(files);
    result.targeting_=CombatTargetingService::load(files);result.attacks_=CombatAttackService::load(files);
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    constexpr std::size_t base=0xc7c0;
    if(module.size()<base+0x2870)throw DecodeError("Truncated automatic combat magic source tables");
    for(unsigned i=0;i<9;++i) {
        result.near_x_[i]=static_cast<std::int8_t>(signed_byte(module[base+0x2736+i]));
        result.near_y_[i]=static_cast<std::int8_t>(signed_byte(module[base+0x273f+i]));
    }
    std::copy_n(module.begin()+base+0x286c,4,result.helpless_.begin());
    const auto overlay=files.read_asset("GAME.OVR");constexpr std::size_t set_at=0x143d3;
    if(overlay.size()<set_at+32)throw DecodeError("Truncated automatic Cure body status set");
    for(unsigned status=0;status<256;++status)
        result.cure_body_status_[status]=(overlay[set_at+status/8]&(1u<<(status%8)))==0;
    result.loaded_=true;return result;
}
std::size_t OpponentMagicService::find(const DeploymentState& state,CombatActorIdentity id) {
    const auto found=std::find_if(state.actors.begin(),state.actors.end(),[id](const auto& actor){return actor.id==id;});
    if(found==state.actors.end() || found->removed)throw DecodeError("Automatic magic actor identity is stale");
    return static_cast<std::size_t>(found-state.actors.begin());
}
std::vector<CombatTarget> OpponentMagicService::visible_at(const DeploymentState& state,CombatPosition centre,unsigned maximum) const {
    std::vector<const DeploymentActor*> slots;
    for(const auto& actor:state.actors)if(!actor.removed && actor.position && actor.shape)slots.push_back(&actor);
    std::sort(slots.begin(),slots.end(),[](const auto* a,const auto* b){return a->native_slot<b->native_slot;});
    std::vector<CombatTarget> result;
    for(const auto* actor:slots) {
        unsigned distance=256,direction=0;
        for(const auto to:spatial_.footprint(*actor)) {
            const auto sight=CombatSpatialService::sight(state.battlefield,centre,to,static_cast<std::uint16_t>(maximum));
            if(sight.clear && sight.distance<distance){distance=sight.distance;direction=cone(centre,to,state.battlefield.tables);}
        }
        if(distance<256) {
            CombatTarget entry;entry.actor=actor->id;entry.position=*actor->position;
            entry.distance=static_cast<std::uint8_t>(distance);entry.direction=static_cast<std::uint8_t>(direction);entry.visible=true;
            result.push_back(entry);
        }
    }
    sort_visible(result);return result;
}
std::vector<CombatTarget> OpponentMagicService::opposing(const DeploymentState& state,CombatActorIdentity id,unsigned maximum) const {
    auto result=targeting_.visible_targets(state,id,static_cast<std::uint16_t>(maximum));
    const auto side=static_cast<std::uint8_t>(state.actors[find(state,id)].character.byte(0x10e)==0);
    std::erase_if(result,[&](const auto& entry){return state.actors[find(state,*entry.actor)].character.byte(0x10e)!=side;});
    return result;
}
std::optional<CombatActorIdentity> OpponentMagicService::cure_target(const DeploymentState& state,CombatActorIdentity id) const {
    const auto& caster=state.actors[find(state,id)];
    if(!caster.position)throw DecodeError("Automatic Cure caster has no position");
    std::optional<CombatActorIdentity> best,body;unsigned hp=255;
    // This is nine source coordinates around the actor origin, not around
    // every footprint cell, and never a percentage-based healing policy.
    for(unsigned i=0;i<9;++i) {
        const CombatPosition point{caster.position->x+near_x_[i],caster.position->y+near_y_[i]};
        if(!Battlefield::in_bounds(point.x,point.y))continue;
        const auto slot=state.occupancy[static_cast<std::size_t>(point.y*50+point.x)];
        if(slot) {
            const auto target=std::find_if(state.actors.begin(),state.actors.end(),[slot](const auto& a){return !a.removed && a.native_slot==slot;});
            if(target==state.actors.end())throw DecodeError("Automatic Cure occupancy has no owner");
            const auto current=target->character.hit_points(),maximum=target->character.byte(0x32);
            if(target->character.byte(0x10e)==caster.character.byte(0x10e) && current<maximum &&
               (current<hp || (target->id==id && current<(maximum>>1)))){best=target->id;hp=current;}
        } else if(state.battlefield.terrain_at(point.x,point.y)==0x1f) {
            for(const auto& record:state.bodies) {
                const auto& target=state.actors[find(state,record.actor)];
                if(record.position==point && cure_body_status_[target.character.status()])body=target.id;
            }
        }
    }
    return hp<8 || !body?best:body;
}
bool OpponentMagicService::friendly_fire(const DeploymentState& state,CombatActorIdentity caster,CombatPosition point,unsigned radius) const {
    const auto enemy=static_cast<std::uint8_t>(state.actors[find(state,caster)].character.byte(0x10e)==0);
    const auto list=visible_at(state,point,radius);
    return std::any_of(list.begin(),list.end(),[&](const auto& entry){return state.actors[find(state,*entry.actor)].character.byte(0x10e)!=enemy;});
}
bool OpponentMagicService::eligible(const DeploymentState& state,CombatActorIdentity id,std::uint8_t cast,unsigned priority,
        const SpellCatalog& spells,bool item) const {
    const auto metadata=row(spells,cast);
    if(priority>metadata[13])return false;
    if(cast!=3 && !metadata[14])return true;
    if(cast==3 && cure_target(state,id))return true;
    const auto& actor=state.actors[find(state,id)];
    // Item03E4 has not entered4053:1A8A or set6DBF yet. Its preflight range
    // therefore uses the ordinary school level; actual item targeting uses6.
    (void)item;
    const auto list=opposing(state,id,range(actor.character,metadata,false));
    if(list.empty())return false;
    if(!metadata[15])return true;
    // Native02EB rejects immediately on the FIRST unsafe centre. All centres
    // must be safe, even if another legal enemy could have been selected.
    return std::none_of(list.begin(),list.end(),[&](const auto& target){return friendly_fire(state,id,target.position,metadata[15]);});
}
std::optional<CombatActorIdentity> OpponentMagicService::acquire(const DeploymentState& state,CombatActorIdentity id,unsigned maximum,std::mt19937& rng) const {
    //326D37E4(force=1,allowfallback=0): two identical visible lists, twenty
    // random choices each. Rejections zero slots without compacting or changing
    // the die's sides. Force1 never turns on the terrain-ignore descriptor.
    for(unsigned pass=0;pass<2;++pass) {
        const auto list=opposing(state,id,maximum);std::vector<bool> rejected(list.size());
        for(unsigned attempt=0;attempt<20 && !list.empty();++attempt) {
            const auto selected=roll(rng,static_cast<unsigned>(list.size()))-1;
            if(rejected[selected])continue;
            const auto candidate=*list[selected].actor;
            if(attacks_.target_available(state,id,candidate))return candidate;
            rejected[selected]=true;
            if(std::all_of(rejected.begin(),rejected.end(),[](bool value){return value;}))break;
        }
    }
    return {};
}
std::optional<CombatCastSchedule> OpponentMagicService::select_item(const DeploymentState& state,CombatActorIdentity id,
        const ItemCatalog& items,const SpellCatalog& spells,const OpponentMagicContext& context,std::mt19937& random) const {
    if(!loaded_)throw DecodeError("Automatic magic source tables are uninitialized");
    const auto& actor=state.actors[find(state,id)];auto rng=random;const auto passes=roll(rng,7);
    std::optional<CombatCastSchedule> result;
    if(actor.extension[2] && opposing_active(state,actor) && !context.casting_blocked) {
        for(unsigned pass=0;pass<passes && !result;++pass)for(std::size_t i=0;i<actor.character.inventory().size();++i) {
            const auto& item=actor.character.inventory().item(i);
            if(items.definition(item.type()).scroll() || item.byte(0x3e)>=128 || !item.readied() || !item.byte(0x3d))continue;
            const auto raw=item.byte(0x3d);const auto cast=raw>56?static_cast<std::uint8_t>(raw-23):raw;
            if(!eligible(state,id,cast,7-pass,spells,true))continue;
            auto temporary=state;temporary.actors[find(state,id)].extension[0]=0;
            result=CombatCastingService::prepare_item(temporary,id,item.id(),{},items,spells,false,CombatCastingPolicy::automatic);
            result->state.actors[find(state,id)].extension[0]=actor.extension[0];break;
        }
    }
    random=rng;return result;
}
bool OpponentMagicService::wants_turn(const DeploymentState& state,CombatActorIdentity id) const {
    if(!loaded_)throw DecodeError("Automatic magic source tables are uninitialized");
    const auto& actor=state.actors[find(state,id)];
    if(actor.extension[0x11] || signed_byte(actor.character.byte(0x96))<=0)return false;
    for(const auto& entry:opposing(state,id,255)) {
        const auto& target=state.actors[find(state,*entry.actor)];const auto type=signed_byte(target.character.byte(0x76));
        if(!target.extension[0x10] && type>0 && type<13)return true;
    }
    return false;
}
std::optional<CombatCastSchedule> OpponentMagicService::select_spell(const DeploymentState& state,CombatActorIdentity id,
        const ItemCatalog& items,const SpellCatalog& spells,const OpponentMagicContext& context,std::mt19937& random) const {
    if(!loaded_)throw DecodeError("Automatic magic source tables are uninitialized");
    const auto& actor=state.actors[find(state,id)];auto rng=random;
    std::vector<std::pair<std::size_t,std::uint8_t>> choices;
    if(actor.extension[1])for(std::size_t slot=0;slot<21;++slot)if(const auto raw=actor.character.byte(0x17+slot))choices.emplace_back(slot,raw);
    const auto passes=roll(rng,7);std::optional<CombatCastSchedule> result;
    (void)items;
    if(!choices.empty() && (actor.character.byte(0x84)>=128 || context.automatic_party_casting) && opposing_active(state,actor)) {
        for(unsigned pass=0;pass<passes && !result;++pass)for(unsigned attempt=0;attempt<3;++attempt) {
            const auto [slot,cast]=choices[roll(rng,static_cast<unsigned>(choices.size()))-1];
            // Native0545 retains raw high bits. Indexing outside the proved67
            // rows is an explicit atomic boundary, not a silently masked slot.
            if(!eligible(state,id,cast,7-pass,spells,false))continue;
            result=CombatCastingService::schedule(state,id,slot,spells,false,CombatCastingPolicy::automatic);break;
        }
    }
    random=rng;return result;
}
OpponentMagicTargets OpponentMagicService::targets(const DeploymentState& state,const CombatCastPending& pending,
        const SpellCatalog& spells,std::mt19937& random) const {
    return targets_metadata(state,pending.caster,pending.cast_index,pending.origin!=CombatCastOrigin::memorized,
        pending.caster_level,pending.item_origin,spells,random);
}
OpponentMagicTargets OpponentMagicService::targets_metadata(const DeploymentState& state,CombatActorIdentity caster,
        std::uint8_t cast_index,bool item_origin,std::uint8_t caster_level,std::optional<ItemOrigin> source_item,
        const SpellCatalog& spells,std::mt19937& random) const {
    if(!loaded_)throw DecodeError("Automatic magic source tables are uninitialized");
    const auto& actor=state.actors[find(state,caster)];
    if(!actor.position || !actor.shape)throw DecodeError("Automatic caster is not placed");
    // Validate owned footprints/slot identities before any acquisition draws.
    (void)targeting_.visible_targets(state,caster,255);
    const auto raw=row(spells,cast_index);const auto mode=raw[6]&15;
    auto rng=random;OpponentMagicTargets result;result.retained_target=actor.retained_target;
    CombatMagicIntent intent;intent.caster=caster;intent.cast_index=cast_index;
    intent.item_origin=item_origin;intent.caster_level=caster_level;intent.source_item=source_item;
    const auto maximum=range(actor.character,raw,intent.item_origin);
    const auto single=[&]() -> std::optional<CombatActorIdentity> {
        if(!raw[14])return cast_index==3?cure_target(state,caster):std::optional{caster};
        for(unsigned attempt=0;attempt<20;++attempt) {
            //Force1 clears an old retained link even when acquisition fails.
            result.retained_target=acquire(state,caster,maximum,rng);
            if(!result.retained_target)continue;
            const auto& target=state.actors[find(state,*result.retained_target)];
            if(raw[15] && friendly_fire(state,caster,*target.position,raw[15]))continue;
            const bool helpless=std::any_of(helpless_.begin(),helpless_.end(),[&](auto code){return has(target.character,code);});
            if(helpless && std::find(helpless_.begin(),helpless_.end(),raw[10])!=helpless_.end())continue;
            return result.retained_target;
        }
        return {};
    };
    if(mode==0)intent.targets={caster};
    else if(mode>=8 && mode<=14) {
        if(const auto target=single())intent.centre=state.actors[find(state,*target)].position;
        else {random=rng;return result;}
    } else if(mode==15) {
        if(const auto target=single()) {
            intent.targets={*target};intent.centre=state.actors[find(state,*target)].position;
        } else {random=rng;return result;}
    } else {
        const unsigned count=(raw[6]&3)+1;
        for(unsigned i=0;i<count;++i)if(const auto target=single()) {
            // 1E16 writes6DB9/BA even for an automatic duplicate. 20C3's
            // duplicate omission does not restore the prior cursor, so the
            // common projectile may end on an earlier unique recipient.
            intent.centre=state.actors[find(state,*target)].position;
            if(std::find(intent.targets.begin(),intent.targets.end(),*target)==intent.targets.end())intent.targets.push_back(*target);
        }
        if(intent.targets.empty()){random=rng;return result;}
    }
    result.intent=std::move(intent);random=rng;return result;
}
}
