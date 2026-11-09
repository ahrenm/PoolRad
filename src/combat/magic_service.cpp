#include "gbengine/combat/magic_service.hpp"
#include "gbengine/combat/targeting_service.hpp"
#include "gbengine/combat/cloud_service.hpp"
#include "gbengine/magic/saving_throw_service.hpp"
#include "gbengine/party/equipment.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <set>
namespace GBEngine {
namespace {
std::size_t find_actor(const DeploymentState& state,CombatActorIdentity id) {
    const auto at=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return a.id==id;});
    if(at==state.actors.end())throw DecodeError("Combat spell actor identity is stale");
    return static_cast<std::size_t>(at-state.actors.begin());
}
std::uint8_t die(std::mt19937& random,unsigned sides,unsigned count=1) {
    if(!sides)throw DecodeError("Combat spell die has no sides");
    std::uint8_t sum=0;const auto threshold=(std::uint32_t{0}-sides)%sides;
    for(unsigned i=0;i<count;++i){std::uint32_t n;do{n=static_cast<std::uint32_t>(random());}while(n<threshold);sum=static_cast<std::uint8_t>(sum+n%sides+1);}
    return sum;
}
bool has(const Character& c,unsigned code) {return std::any_of(c.effects().begin(),c.effects().end(),[&](const auto& e){return e.code()==code;});}
int signed_byte(std::uint8_t n) {return n<128?n:static_cast<int>(n)-256;}
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
    throw DecodeError("Combat spell has no native direction cone");
}
struct Visible {CombatActorIdentity id;unsigned distance,direction;std::uint8_t slot;};
void source_sort(std::vector<Visible>& entries) {
    std::sort(entries.begin(),entries.end(),[](const auto& x,const auto& y){return x.slot<y.slot;});
    for(std::size_t i=0;i<entries.size();++i)for(std::size_t j=i+1;j<entries.size();++j)
        if(entries[j].distance<entries[i].distance || (entries[j].distance==entries[i].distance && entries[j].direction<entries[i].direction && entries[j].direction%2<=entries[i].direction%2))std::swap(entries[i],entries[j]);
}
unsigned level_for(const Character& caster,const std::array<std::uint8_t,16>& raw,bool item,unsigned override_level) {
    if(override_level)return override_level;
    if(raw[0]==2)return 12;
    if(item)return 6;
    return caster.byte(raw[0]==0?0x96:0x9b);
}
std::size_t message_offset(unsigned id) {
    switch(id) {
    case 1:return 0x1004;case 2:return 0x1036;case 5:case 11:case 18:case 22:case 29:return 0x10dd;
    case 6:case 7:case 16:case 17:case 52:case 53:case 54:return 0x1116;
    case 8:return 0x1150;case 10:return 0x11d2;case 19:return 0x148d;case 21:return 0x150e;
    case 23:case 49:return 0x1650;case 24:return 0x17d7;case 25:return 0x1816;case 26:return 0x184f;
    case 27:return 0x1904;case 30:case 50:return 0x1a09;case 32:return 0x1a7c;case 36:return 0x2063;
    case 33:return 0x1ac0;case 38:return 0x2254;case 40:return 0x2340;
    case 42:return 0x24cb;case 44:return 0x25f3;case 45:return 0x2631;
    case 48:return 0x2888;case 55:return 0x2bff;case 57:return 0x2df8;
    case 61:return 0x2f8c;case 63:return 0x301d;
    default:throw DecodeError("Combat spell source feedback binding is missing");
    }
}
}
CombatMagicService CombatMagicService::load(const IFileSystem& files) {
    CombatMagicService result;result.catalog_=SpellCatalog::load(files);result.spatial_=CombatSpatialService::load(files);result.effects_=CombatMagicEffects::load(files);result.attacks_=CombatAttackService::load(files);return result;
}
std::array<std::uint8_t,16> CombatMagicService::metadata(std::uint8_t id) const {
    return id<=56?catalog_.definition(SpellId{id}).raw_bytes():catalog_.item_definition(ItemCastId{id}).raw_bytes();
}
std::optional<std::string> CombatMagicService::unsupported(std::uint8_t id,bool item_origin) const {
    const auto raw=metadata(id);
    if(!raw[11] && !item_origin)return "This native spell is unavailable from combat Cast";
    return {};
}
CombatMagicTargetPolicy CombatMagicService::target_policy(const DeploymentState& state,CombatActorIdentity caster,std::uint8_t id,bool item,std::uint8_t level) const {
    const auto raw=metadata(id);const auto& c=state.actors[find_actor(state,caster)].character;
    const auto range_level=item?6u:level_for(c,raw,false,level);
    auto range=static_cast<std::uint8_t>(raw[2]+raw[3]*range_level);
    if((!range && raw[6]) || range==255)range=1;
    CombatMagicTargetPolicy policy;policy.raw_mode=raw[6];policy.range=range;policy.bodies=true;
    policy.empty_tiles=(raw[6]&15)>=8;
    const auto mode=raw[6]&15;
    if(!mode){policy.mode=CombatMagicTargetMode::self;policy.maximum_targets=1;}
    else if(mode==15){policy.mode=CombatMagicTargetMode::individual;policy.maximum_targets=1;}
    else if(mode>=8){policy.mode=CombatMagicTargetMode::area;policy.radius=static_cast<std::uint8_t>(raw[6]&7);policy.maximum_targets=71;}
    else {policy.maximum_targets=static_cast<std::uint8_t>((raw[6]&3)+1);policy.mode=policy.maximum_targets==1?CombatMagicTargetMode::individual:CombatMagicTargetMode::multiple;}
    return policy;
}
std::vector<CombatActorIdentity> CombatMagicService::candidates(const DeploymentState& state,CombatActorIdentity caster,std::uint8_t id,bool item,std::uint8_t level) const {
    const auto policy=target_policy(state,caster,id,item,level);const auto& a=state.actors[find_actor(state,caster)];
    (void)attacks_.target_available(state,caster,caster);
    if(a.removed || !a.position || !a.shape)throw DecodeError("Combat caster is not placed");
    if(policy.mode==CombatMagicTargetMode::self)return {caster};
    std::vector<Visible> found;
    for(const auto& target:state.actors) {
        if(target.removed || !target.position || !target.shape)continue;
        unsigned closest=256,direction=0;
        for(const auto to:spatial_.footprint(target))for(const auto from:spatial_.footprint(a)) {
            const auto sight=CombatSpatialService::sight(state.battlefield,from,to,policy.range);
            if(sight.clear && sight.distance<closest){closest=sight.distance;direction=cone(from,to,state.battlefield.tables);}
        }
        if(closest<256 && attacks_.target_available(state,caster,target.id))found.push_back({target.id,closest,direction,target.native_slot});
    }
    source_sort(found);
    std::vector<CombatActorIdentity> result;for(const auto& e:found)result.push_back(e.id);return result;
}
CombatMagicResult CombatMagicService::stage(const DeploymentState& state,const CombatMagicIntent& intent,const ItemCatalog& items,std::mt19937& random) const {
    if(const auto reason=unsupported(intent.cast_index,intent.item_origin))throw DecodeError(*reason);
    const auto raw=metadata(intent.cast_index);const auto ci=find_actor(state,intent.caster);const auto& original_caster=state.actors[ci];
    if(original_caster.removed || !original_caster.position || !original_caster.shape || !original_caster.character.active())throw DecodeError("Combat caster is not an active placed actor");
    const auto level=static_cast<std::uint8_t>(level_for(original_caster.character,raw,intent.item_origin,intent.caster_level));
    const auto policy=target_policy(state,intent.caster,intent.cast_index,intent.item_origin,level);
    CombatMagicResult result;result.state=state;result.next_item_id=intent.next_item_id;auto rng=random;
    auto targets=intent.targets;
    (void)attacks_.target_available(state,intent.caster,intent.caster);
    const auto allowed=policy.mode==CombatMagicTargetMode::area?std::vector<CombatActorIdentity>{}:candidates(state,intent.caster,intent.cast_index,intent.item_origin,level);
    if(policy.mode==CombatMagicTargetMode::self)targets={intent.caster};
    if(policy.mode==CombatMagicTargetMode::area) {
        if(!intent.centre || !Battlefield::in_bounds(intent.centre->x,intent.centre->y))throw DecodeError("Combat area spell needs a battlefield centre");
        if(!CombatSpatialService::sight(state.battlefield,*original_caster.position,*intent.centre,policy.range).clear)throw DecodeError("Combat spell centre is beyond native range or sight");
        const auto centre_slot=state.occupancy[static_cast<std::size_t>(intent.centre->y*50+intent.centre->x)];
        if(centre_slot) {
            const auto selected=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return !a.removed && a.native_slot==centre_slot;});
            if(selected==state.actors.end() || !attacks_.target_available(state,intent.caster,selected->id))throw DecodeError("Combat spell centre target is vetoed by source eligibility");
        }
        auto radius=policy.radius;
        if(intent.cast_index==47 || intent.cast_index==64) {
            if(!intent.dungeon_flag)throw DecodeError("Fireball requires native VM49E6 dungeon policy");
            if(!*intent.dungeon_flag)radius=2;
        }
        targets.clear();std::vector<Visible> entries;
        for(const auto& actor:state.actors)if(!actor.removed && actor.position && actor.shape) {
            unsigned closest=256,direction=0;
            for(const auto point:spatial_.footprint(actor)) {const auto sight=CombatSpatialService::sight(state.battlefield,*intent.centre,point,radius);if(sight.clear && sight.distance<closest){closest=sight.distance;direction=cone(*intent.centre,point,state.battlefield.tables);}}
            if(closest<256)entries.push_back({actor.id,closest,direction,actor.native_slot});
        }
        source_sort(entries);
        for(const auto& entry:entries)targets.push_back(entry.id);
    } else if((raw[6]&15)==15 && intent.centre && targets.empty()) {
        if(!CombatSpatialService::sight(state.battlefield,*original_caster.position,*intent.centre,policy.range).clear)throw DecodeError("Combat spell centre is beyond native range or sight");
        for(const auto& actor:state.actors)if(!actor.removed && actor.position && actor.shape)
            for(const auto point:spatial_.footprint(actor))if(point==*intent.centre){targets.push_back(actor.id);break;}
    } else {
        if(targets.empty())throw DecodeError("Combat spell needs a confirmed target");
        if(targets.size()>policy.maximum_targets)throw DecodeError("Combat spell has too many selected targets");
        std::set<std::pair<CombatActorOrigin,std::uint64_t>> unique;
        for(const auto id:targets) {
            if(!unique.emplace(id.origin,id.value).second)throw DecodeError("Combat spell target was selected twice");
            const auto& a=state.actors[find_actor(state,id)];
            if(std::find(allowed.begin(),allowed.end(),id)==allowed.end()) {
                const auto body=std::find_if(state.bodies.begin(),state.bodies.end(),[&](const auto& b){return b.actor==id;});
                if(body==state.bodies.end() || state.occupancy[static_cast<std::size_t>(body->position.y*50+body->position.x)] ||
                   !CombatSpatialService::sight(state.battlefield,*original_caster.position,body->position,policy.range).clear)
                    throw DecodeError("Combat spell target is beyond native range or sight");
            }
            if(a.removed)throw DecodeError("Combat spell target was removed");
        }
    }
    // 4612:0C20 faces the last Aim choice before the common projectile and
    // before any handler mutates that choice or its target order.
    const auto destination=intent.centre?*intent.centre:!targets.empty()?result.state.actors[find_actor(result.state,targets.back())].position.value_or(*original_caster.position):*original_caster.position;
    result.state.actors[ci].extension[9]=static_cast<std::uint8_t>(cone(*original_caster.position,destination,state.battlefield.tables));
    result.casting_before=std::make_shared<const DeploymentState>(result.state);
    if(intent.cast_index==27) {
        // Native190F scans the complete retained linked roster, not the Aim list.
        targets.clear();unsigned pool=original_caster.character.hit_points();
        for(const auto& actor:state.actors)if(!actor.removed && actor.character.byte(0x9f)==14 && actor.character.hit_points()<=pool) {
            pool-=actor.character.hit_points();targets.push_back(actor.id);
        }
    }
    // Native2385 repeats6C95 for every target counter; it never indexes the
    // selected target list inside that loop. Preserve this selected-build quirk.
    if((intent.cast_index==41 || intent.cast_index==46) && !targets.empty())std::fill(targets.begin(),targets.end(),targets.front());
    for(const auto& a:result.state.actors)if(!a.removed && !a.character.effects_restored())throw DecodeError("Combat spell requires owned effect graphs");
    const auto emit_id=[&](CombatActorIdentity id,const std::string& text_id,bool animated=false) {
        const auto snapshot=std::make_shared<const DeploymentState>(result.state);
        result.events.push_back({CombatMagicEventKind::feedback,result.feedback.size(),snapshot,snapshot});result.feedback.push_back({id,text_id,animated});
    };
    const auto emit=[&](CombatActorIdentity id,std::size_t offset,std::string_view source="GAME.OVR",bool animated=false) {emit_id(id,pascal_string_id(source,offset),animated);};
    const auto emit_handler=[&](CombatActorIdentity id,unsigned cast,bool accepted=true) {
        emit_id(id,pascal_string_id("GAME.OVR",accepted?0x27018+message_offset(cast):0x2c532),accepted);
    };
    MagicEffectService effects{catalog_,&items,&rng,5};
    const auto native=NativeEffectContext{&items,&rng,5,&catalog_};
    const auto source_owns_7a_dice=[&] {
        const auto& source=result.state.actors[ci].character;
        const auto primary=EquipmentRules::inspect(source,items).slots[0];
        return primary && source.inventory().item(*primary).type()==0x56;
    };
    while(MagicEffectService::remove_effect(result.state.actors[ci].character,0x19,native)){}
    const auto remove_at=[&](std::size_t ti,std::size_t index) {
        const auto notices=CombatMagicEffects::remove_effect_in_place(result.state,result.state.actors[ti].id,index,items,catalog_,rng,intent.caster);
        for(const auto& notice:notices)emit_id(result.state.actors[ti].id,notice.text_id);
    };
    if(intent.cast_index==34) {
        if(!intent.centre)throw DecodeError("Combat cloud requires its acquired centre");
        const auto notices=CombatCloudService::create_in_place(result.state,intent.caster,*intent.centre,level,intent.source_item,catalog_,items,rng);
        for(const auto& notice:notices)emit_id(result.state.actors.at(notice.member).id,notice.text_id);
        random=std::move(rng);return result;
    }
    if(intent.cast_index==51 || intent.cast_index==60) {
        // 4612:2BB7/2F4D hit the selected square before295B extends the ray.
        // The extension is radial through the original caster; walls reverse
        // that radial direction rather than reflecting an invented normal.
        const auto origin=*original_caster.position;
        CombatPosition centre=intent.centre.value_or(targets.empty()?origin:*result.state.actors[find_actor(result.state,targets.front())].position);
        const auto initial=intent.cast_index==51?die(rng,6,level):static_cast<std::uint8_t>(die(rng,6)+20);
        const auto continuing=intent.cast_index==51?initial:std::uint8_t{20};
        const auto at=[&](CombatPosition point,std::uint8_t amount,bool absorption_suppressed) {
            if(!Battlefield::in_bounds(point.x,point.y))return false;
            const auto terrain=result.state.battlefield.terrain_at(point.x,point.y);
            const bool wall=terrain && result.state.battlefield.tables.properties(terrain)[0]==255;
            const auto slot=result.state.occupancy[static_cast<std::size_t>(point.y*50+point.x)];
            if(slot) {
                const auto actor=std::find_if(result.state.actors.begin(),result.state.actors.end(),[&](const auto& a){return !a.removed && a.native_slot==slot;});
                if(actor==result.state.actors.end())throw DecodeError("Combat projectile occupancy has no owner");
                const auto identity=actor->id;const auto ti=find_actor(result.state,identity);const auto hp=actor->character.hit_points();
                //2BB7/2F4D use0DE3, which does not initialize681C.7A alone
                //can own that count here when its primary56 branch calls0E2E.
                if(has(actor->character,0x7a) && !source_owns_7a_dice())
                    throw DecodeError("Combat ray resistance7A requires retained native damage-dice scratch");
                const auto before=std::make_shared<const DeploymentState>(result.state);
                const CombatMagicContext context{intent.caster,intent.cast_index,0,level,0,absorption_suppressed};
                const auto saved=CombatMagicEffects::save(result.state,identity,4,0,context,rng,12).saved;
                auto resolved=effects_.damage(result.state,identity,amount,12,saved,2,context,rng);
                const auto outcome=resolved.outcome;const auto notices=std::move(resolved.notices);result.state=std::move(resolved.state);
                for(const auto& notice:notices)emit_id(result.state.actors.at(notice.member).id,notice.text_id);
                result.events.push_back({CombatMagicEventKind::damage,result.damage.size(),before,std::make_shared<const DeploymentState>(result.state)});
                result.damage.push_back({intent.caster,identity,outcome.amount,hp,result.state.actors[ti].character.hit_points(),result.state.actors[ti].character.status(),saved,outcome.interrupted_spell});
            }
            return wall;
        };
        (void)at(centre,initial,intent.cast_index==51);
        if(centre!=origin) {
            std::uint8_t remaining=static_cast<std::uint8_t>((intent.cast_index==51?8:3)*2);
            int direction=1;bool first_long=intent.cast_index==51;
            auto last_slot=result.state.occupancy[static_cast<std::size_t>(centre.y*50+centre.x)];
            unsigned work=0;
            while(remaining) {
                const CombatPosition end{centre.x+(centre.x-origin.x)*direction*remaining,centre.y+(centre.y-origin.y)*direction*remaining};
                const auto line=CombatSpatialService::trace_line(centre,end);std::size_t index=0;std::uint8_t distance=0;
                bool wall=false;
                do {
                    const auto previous=line[index];std::uint8_t terrain=0,slot=0;
                    while(index+1<line.size()) {
                        if(++work>4096)throw DecodeError("Combat lightning traversal exceeded native bounded field");
                        const auto before=line[index++];const auto point=line[index];
                        distance=static_cast<std::uint8_t>(distance+((point.x!=before.x && point.y!=before.y)?3:2));
                        terrain=Battlefield::in_bounds(point.x,point.y)?result.state.battlefield.terrain_at(point.x,point.y):0;
                        slot=Battlefield::in_bounds(point.x,point.y)?result.state.occupancy[static_cast<std::size_t>(point.y*50+point.x)]:0;
                        if((slot && slot!=last_slot) || !terrain || result.state.battlefield.tables.properties(terrain)[0]>1 || remaining<=distance)break;
                    }
                    const auto point=line[index];last_slot=slot;if(!terrain)remaining=0;
                    result.events.push_back({CombatMagicEventKind::projectile,result.projectiles.size(),std::make_shared<const DeploymentState>(result.state)});result.projectiles.push_back({previous,point});
                    wall=at(point,continuing,true);
                    if(wall) {
                        centre=point;
                        if(first_long && CombatSpatialService::range(centre,origin)<9)distance=static_cast<std::uint8_t>(distance+8);
                        direction=-direction;first_long=false;last_slot=0;
                    }
                    remaining=distance<remaining?static_cast<std::uint8_t>(remaining-distance):std::uint8_t{0};
                    if(index+1>=line.size())remaining=0;
                } while(!wall && remaining);
            }
        }
        random=std::move(rng);return result;
    }
    const auto append=[&](std::size_t ti,unsigned cast,std::uint8_t context,std::uint8_t reversal,bool saved=false) {
        auto& target=result.state.actors[ti].character;const auto incoming=raw[10];
        //4612:08C3 calls07D6 per acquired recipient BEFORE1656's phase9
        //queries/save veto.40 and61 override their zero metadata durations.
        const auto duration=cast==40?static_cast<std::uint16_t>(die(rng,6)*10):
            cast==61?static_cast<std::uint16_t>(die(rng,4,5)):
            static_cast<std::uint16_t>(raw[4]+raw[5]*level);
        if(!MagicEffectService::application_queries(target,incoming,level,rng) || (saved && raw[8]==1)){emit_handler(result.state.actors[ti].id,cast,false);return;}
        const auto old=std::find_if(target.effects_.begin(),target.effects_.end(),[&](const auto& e){return e.code()==incoming;});
        if(old!=target.effects_.end() && (!duration || (old->word_1() && old->word_1()<duration)))remove_at(ti,static_cast<std::size_t>(old-target.effects_.begin()));
        CharacterEffect node;node.payload={incoming,0,0,context,reversal};node.set_duration(duration);node.source_file="START.EXE:module";
        node.source_offset=0xf9ba+16*cast;if(cast<=56)node.spell_id=static_cast<std::uint8_t>(cast);else node.item_cast_id=static_cast<std::uint8_t>(cast);node.item_origin=intent.source_item;
        result.state.actors[ti].character.effects_.push_back(std::move(node));emit_handler(result.state.actors[ti].id,cast);
    };
    const auto save=[&](std::size_t ti,std::int8_t modifier=0,std::uint8_t flags=0) {
        if(!raw[8])return false;
        return CombatMagicEffects::save(result.state,result.state.actors[ti].id,raw[9],modifier,{intent.caster,intent.cast_index,raw[10],level,0},rng,flags).saved;
    };
    const auto contact_hit=[&](std::size_t ti) {
        if(raw[2]!=255)return true;
        auto& victim=result.state.actors[ti];auto& target=victim.character;
        auto& caster=result.state.actors[ci].character;
        // 4612:08C3 refreshes and dispatches defense phase0B after the save,
        // then passes the resulting byte111 by value into49D5:0CB7.
        EquipmentRules::recalculate(target,items);
        if(has(target,0x21)){target.set_byte(0x111,static_cast<std::uint8_t>(target.byte(0x111)-4));target.set_byte(0x112,static_cast<std::uint8_t>(target.byte(0x112)-4));}
        if(has(target,0x11)&&target.byte(0x111)<57)target.set_byte(0x111,57);
        if(has(target,0x1e)) {
            if(victim.extension[2])emit(victim.id,0xf6b4);
            victim.extension[2]=0;victim.extension[1]=0;EquipmentRules::recalculate(target,items);
            target.set_byte(0x112,target.byte(0x112)<53?std::uint8_t{50}:static_cast<std::uint8_t>(target.byte(0x112)-2));target.set_byte(0x111,target.byte(0x112));
        }
        const auto defense=target.byte(0x111);const auto rolled=die(rng,20);
        if(rolled==1)return false;
        const auto hit=attacks_.accuracy_in_place(result.state,intent.caster,victim.id,rolled);
        const auto side=caster.byte(0x10e);if(side>1)throw DecodeError("Combat contact attack side is invalid");
        const auto adjusted=signed_byte(static_cast<std::uint8_t>(hit));
        return adjusted>=0 && defense<=adjusted+caster.byte(0x110)+signed_byte(intent.side_attack_bonus[side]);
    };
    if(intent.cast_index==36) {
        unsigned remaining=level;
        for(std::size_t ti=0;ti<result.state.actors.size() && remaining;++ti) {
            const auto& before=result.state.actors[ti];
            if(before.removed || before.character.status()!=6 || before.character.byte(0x9f)!=0)continue;
            const auto identity=before.id;const auto old_side=before.character.byte(0x10e);const auto maximum=before.character.byte(0x32);
            auto placement=effects_.revive(result.state,identity,maximum);if(!placement.applied)continue;
            result.state=std::move(placement.state);auto& actor=result.state.actors[ti];auto& c=actor.character;
            c.set_byte(0x10e,original_caster.character.byte(0x10e));c.set_byte(0x10f,1);c.set_byte(0x76,2);c.set_byte(0x6b,0);c.set_byte(0x72,6);
            for(std::size_t slot=0x17;slot<=0x2b;++slot)c.set_byte(slot,0);
            c.set_byte(0x84,c.byte(0x84)<128?std::uint8_t{0xb3}:std::uint8_t{0xb2});c.set_byte(0x9f,4);
            std::fill(actor.extension.begin()+10,actor.extension.begin()+14,std::uint8_t{0});--remaining;
            auto recovery=effects_.revive(result.state,identity,maximum);if(!recovery.applied)continue;
            result.state=std::move(recovery.state);emit(identity,result.state.actors[ti].character.byte(0x10e)==1?182066:182086);
            append(ti,36,static_cast<std::uint8_t>(old_side*16+level),1);
            result.state.actors[ti].character.set_byte(0x10c,1);
        }
        random=std::move(rng);return result;
    }
    unsigned damage_amount=0,damage_flags=0,damage_dice=0;
    switch(intent.cast_index) {
    case 4:damage_amount=die(rng,8);damage_flags=8;damage_dice=1;break;
    case 9:damage_amount=level;damage_flags=9;break;
    case 15:damage_amount=static_cast<std::uint8_t>((level>>1)+die(rng,4,level>>1));damage_flags=8;damage_dice=level>>1;break;
    case 20:damage_amount=static_cast<std::uint8_t>(level+die(rng,8));damage_flags=12;damage_dice=1;break;
    case 47:damage_amount=die(rng,6,level);damage_flags=9;damage_dice=level;break;
    case 64:damage_dice=die(rng,3)*2+1;damage_amount=die(rng,6,damage_dice);damage_flags=9;break;
    case 65:damage_amount=static_cast<std::uint8_t>(die(rng,4,2)+2);damage_flags=8;damage_dice=2;break;
    default:break;
    }
    // Sleep's one4d4 pool and source-retained order precede all applications.
    unsigned sleep_pool=intent.cast_index==21?die(rng,4,4):0;
    if(intent.cast_index==23 || intent.cast_index==49)std::reverse(targets.begin(),targets.end());
    unsigned remaining=level;std::vector<CombatActorIdentity> activated;
    std::size_t target_ordinal=0;
    for(const auto identity:targets) {
        const auto ti=find_actor(result.state,identity);auto& target=result.state.actors[ti].character;
        const auto cast=intent.cast_index;
        if(cast==31)continue; // Native Knock generic handler has no immediate effect in combat.
        if(cast==10) {
            if(target.byte(0x9f)>1 || target.byte(0x6c)>1){emit(identity,0x27018+0x11c4);continue;}
            const auto saved=save(ti);
            append(ti,cast,static_cast<std::uint8_t>(level+128*original_caster.character.byte(0x10e)),1,saved);
            CombatMagicEffects::apply_charm_in_place(result.state,identity);
            continue;
        }
        if(cast==26) {
            if(target.status()==1 || !has(target,0x37))continue;
            if(!target.hit_points())target.set_byte(0x11b,1);
            append(ti,cast,255,1);
            auto recovery=effects_.revive(result.state,identity,result.state.actors[ti].character.hit_points());
            if(!recovery.applied)throw DecodeError("Suppression recovery requires native null-node retry context");
            result.state=std::move(recovery.state);auto& recovered=result.state.actors[ti].character;
            emit(identity,recovered.byte(0x10e)==1?182066:182086);
            CharacterEffect bleed;bleed.payload={0x0f,10,0,255,1};bleed.source_file="GAME.OVR";bleed.source_offset=0x28873;
            bleed.spell_id=std::uint8_t{26};bleed.item_origin=intent.source_item;recovered.effects_.push_back(std::move(bleed));continue;
        }
        if(cast==1 || cast==2) {
            const auto wanted=cast==1?original_caster.character.byte(0x10e):1-original_caster.character.byte(0x10e);
            if(target.byte(0x10e)!=wanted)continue;
            if(cast==1) {
                bool engaged=false;for(const auto& enemy:result.state.actors)if(!enemy.removed && enemy.position && enemy.shape && enemy.character.byte(0x10e)!=target.byte(0x10e))
                    for(const auto from:spatial_.footprint(result.state.actors[ti]))for(const auto to:spatial_.footprint(enemy))if(CombatSpatialService::sight(result.state.battlefield,from,to,1).clear)engaged=true;
                if(engaged)continue;
            }
            append(ti,cast,level,0);continue;
        }
        if(damage_flags) {
            const auto hp=target.hit_points();bool saved=save(ti,0,static_cast<std::uint8_t>(damage_flags));
            const bool hit=contact_hit(ti);if(!hit)saved=true;
            const auto before=std::make_shared<const DeploymentState>(result.state);
            if(!hit) {
                result.events.push_back({CombatMagicEventKind::damage,result.damage.size(),before,before});
                result.damage.push_back({intent.caster,identity,0,hp,hp,result.state.actors[ti].character.status(),true,{}});continue;
            }
            // CombatMagicEffects applies source phase6 before save zero/half,
            // interrupts queued casts and publishes source HP/body transitions.
            if(cast==9 && (has(result.state.actors[ti].character,0x71) || has(result.state.actors[ti].character,0x3d) ||
                (has(result.state.actors[ti].character,0x7a) && !source_owns_7a_dice())))
                throw DecodeError("Contact fire resistance requires retained native damage-dice scratch");
            const bool absorption_suppressed=policy.mode==CombatMagicTargetMode::area || ((raw[6]&15)==15 && intent.targets.empty());
            const CombatMagicContext context{intent.caster,cast,raw[10],level,static_cast<std::uint8_t>(damage_dice),absorption_suppressed};
            const bool rewrites_dice=has(result.state.actors[ti].character,0x7a) && source_owns_7a_dice();
            auto damage=effects_.damage(result.state,identity,static_cast<std::uint8_t>(damage_amount),static_cast<std::uint8_t>(damage_flags),saved,raw[8],context,rng);
            //7A's0E2E(8,3) changes resident681C for later acquired recipients,
            //including when this recipient's final saved damage becomes zero.
            if(rewrites_dice)damage_dice=3;
            const auto outcome=damage.outcome;const auto notices=std::move(damage.notices);result.state=std::move(damage.state);
            for(const auto& notice:notices)emit_id(result.state.actors.at(notice.member).id,notice.text_id);
            result.events.push_back({CombatMagicEventKind::damage,result.damage.size(),before,std::make_shared<const DeploymentState>(result.state)});
            result.damage.push_back({intent.caster,identity,outcome.amount,hp,result.state.actors[ti].character.hit_points(),result.state.actors[ti].character.status(),saved,outcome.interrupted_spell});
            continue;
        }
        if(cast==21) {
            const auto hd=target.byte(0x73);unsigned cost=hd<=1?1:hd==2?2:hd==3?4:hd==4?6:hd==5?(target.byte(0x2e)?20:10):20;
            if(target.status()==1 || has(target,0x35) || sleep_pool<cost)continue;
            sleep_pool-=cost;append(ti,cast,level,0);continue;
        }
        if(cast==23 || cast==49) {
            if(target_ordinal++) {
                result.events.push_back({CombatMagicEventKind::projectile,result.projectiles.size(),std::make_shared<const DeploymentState>(result.state)});
                result.projectiles.push_back({*original_caster.position,*result.state.actors[ti].position,0x12,4,30});
            }
            const std::int8_t modifier=targets.size()==1?static_cast<std::int8_t>(cast==23?-2:-3):targets.size()==2?-1:0;
            bool saved=save(ti,modifier);const auto& recipient=result.state.actors[ti].character;if(recipient.byte(0x9f)>1 || recipient.byte(0x6c)>1)saved=true;
            append(ti,cast,level,0,saved);continue;
        }
        if(cast==48 || cast==55) {
            const auto wanted=cast==48?original_caster.character.byte(0x10e):1-original_caster.character.byte(0x10e);
            if(target.byte(0x10e)!=wanted || !remaining)continue;
            --remaining;
            const auto opposite=std::find_if(target.effects().begin(),target.effects().end(),[&](const auto& node){return node.code()==(cast==48?0x2a:0x27);});
            if(opposite!=target.effects().end()){remove_at(ti,static_cast<std::size_t>(opposite-target.effects().begin()));continue;}
            activated.push_back(identity);
            append(ti,cast,level,0);continue;
        }
        if(cast==25 || cast==27 || cast==33 || cast==38 || cast==40 || cast==44 || cast==61) {bool saved=save(ti);if(!contact_hit(ti))saved=true;append(ti,cast,level,cast==40?1:0,saved);continue;}
        if(cast==58) {
            const auto remove=[&](unsigned code,bool suppress) {
                const auto node=std::find_if(target.effects_.begin(),target.effects_.end(),[&](const auto& e){return e.code()==code;});
                return node!=target.effects_.end() && MagicEffectService::remove_native(target,static_cast<std::size_t>(node-target.effects_.begin()),suppress,native);
            };
            if(remove(0x37,false)){remove(0x16,true);continue;}
            const bool first=remove(0x22,true),second=remove(0x2b,true);if(second){remove(0x2c,true);remove(0x1f,true);}
            const bool third=remove(0x32,true);if(third)remove(0x39,true);
            if(first || second || third)continue;
        }
        if(cast==41 || cast==46) {
            bool removed=false;
            for(std::size_t i=0;i<result.state.actors[ti].character.effects().size();) {
                const auto node=result.state.actors[ti].character.effects()[i];
                if(node.payload[3]==255){++i;continue;}
                const auto source_level=static_cast<std::uint8_t>(node.payload[3]&15);
                const auto threshold=static_cast<std::uint8_t>(level>source_level?50+(level-source_level)*5:level<source_level?50-(source_level-level)*2:50);
                if(die(rng,100)>threshold){++i;continue;}
                const auto original=result.state.actors[ti].character.effects();
                const bool saved_null_successor=i+1==original.size();
                auto survivor_prefix=original;survivor_prefix.erase(survivor_prefix.begin()+static_cast<std::ptrdiff_t>(i));
                std::vector<std::uint8_t> expected_codes;for(std::size_t n=0;n<result.state.actors[ti].character.effects().size();++n)if(n!=i)expected_codes.push_back(result.state.actors[ti].character.effects()[n].code());
                remove_at(ti,i);const auto& after=result.state.actors[ti].character.effects();
                //2385 captures next BEFORE reversal. Known38/48/22 tail
                //callbacks may append without invalidating that null next;
                //the new tail is retained, never visited by this traversal.
                if(saved_null_successor && (node.code()==0x38 || node.code()==0x48 || node.code()==0x22) &&
                   after.size()>=survivor_prefix.size() && std::equal(survivor_prefix.begin(),survivor_prefix.end(),after.begin())) {
                    removed=true;break;
                }
                if(after.size()!=expected_codes.size() || !std::equal(after.begin(),after.end(),expected_codes.begin(),[](const auto& effect,std::uint8_t code){return effect.code()==code;}))
                    throw DecodeError("Dispel callback changed the native saved-successor traversal");
                removed=true;
            }
            if(removed)emit(identity,0x29391,"GAME.OVR",true);
            continue;
        }
        if(cast==3 || cast==58 || cast==62) {
            const auto amount=cast==3?die(rng,8):cast==58?static_cast<std::uint8_t>(die(rng,4)+8):static_cast<std::uint8_t>(die(rng,4,2)+2);
            const auto old_active=target.active();
            auto recovery=effects_.heal(result.state,identity,amount,false);
            if(recovery.applied) {
                result.state=std::move(recovery.state);const auto& healed=result.state.actors[ti].character;
                if(cast==3)emit(identity,healed.hit_points()==healed.byte(0x32)?0x2ec6:0x2ed6,"START.EXE:module");else emit(identity,0x27018+(cast==58?0x2e43:0x2fc6),"GAME.OVR",true);
                if(!old_active && healed.active())emit(identity,healed.byte(0x10e)==1?182066:182086);
            }
            continue;
        }
        std::vector<Character> members;std::vector<std::size_t> member_actors;
        std::size_t member_caster=0,member_target=0;
        for(std::size_t i=0;i<result.state.actors.size();++i)if(!result.state.actors[i].removed) {
            if(i==ci)member_caster=members.size();
            if(i==ti)member_target=members.size();
            member_actors.push_back(i);members.push_back(result.state.actors[i].character);
        }
        MagicResult resolved;
        if(cast<=56) {
            MagicRequest request{SpellId{cast},intent.item_origin?MagicOrigin::item:MagicOrigin::memorized,{MagicDomain::combat,false},member_caster,{MagicTargetKind::combatant,member_target},level,intent.source_item,&result.next_item_id};
            resolved=effects.resolve_bound(request,members,rng,true);
        } else {
            if(!intent.source_item)throw DecodeError("Combat item cast requires its owned source item");
            ItemMagicRequest request{ItemCastId{cast},{MagicDomain::combat,false},member_caster,{MagicTargetKind::combatant,member_target},*intent.source_item,&result.next_item_id};
            resolved=effects.resolve_item_bound(request,members,rng,true);
        }
        if(!resolved.applied())throw DecodeError(resolved.message);
        for(std::size_t i=0;i<members.size();++i)result.state.actors[member_actors[i]].character=std::move(members[i]);
        for(const auto& feedback:resolved.feedback){const auto snapshot=std::make_shared<const DeploymentState>(result.state);result.events.push_back({CombatMagicEventKind::feedback,result.feedback.size(),snapshot,snapshot});result.feedback.push_back({result.state.actors.at(member_actors.at(feedback.recipient)).id,feedback.text_id,feedback.recipient_animation});}
    }
    if(intent.cast_index==48 || intent.cast_index==55)for(const auto identity:activated) {
        auto& actor=result.state.actors[find_actor(result.state,identity)];auto& character=actor.character;
        //4612:2764 dispatches phase18 for every retained Haste/Slow recipient,
        //even when phase9 vetoed the new marker.27/2A change unused681A;
        //27's first activation and3A's movement write are persistent.
        const auto marker=std::find_if(character.effects_.begin(),character.effects_.end(),[](const auto& node){return node.code()==0x27;});
        if(marker!=character.effects_.end() && !(marker->payload[3]&0x10)) {
            marker->payload[3]=static_cast<std::uint8_t>(marker->payload[3]+0x10);
            character.set_word(0x30,static_cast<std::uint16_t>(character.word(0x30)+1));
            emit(identity,0xf8a1);
        }
        if(has(character,0x3a))actor.extension[6]=0;
    }
    random=std::move(rng);return result;
}
}
