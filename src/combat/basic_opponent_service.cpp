#include "gbengine/combat/basic_opponent_service.hpp"
#include "gbengine/assets/records.hpp"
#include "gbengine/party/equipment.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
unsigned roll(std::mt19937& random,unsigned sides) {
    const auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    std::uint32_t n;do {n=static_cast<std::uint32_t>(random());}while(n<threshold);
    return n%sides+1;
}
bool has(const Character& c,unsigned code) {
    return std::any_of(c.effects().begin(),c.effects().end(),[&](const auto& e){return e.code()==code;});
}
bool positive(std::uint8_t n) {return n>0 && n<128;}
}
BasicOpponentService BasicOpponentService::load(const IFileSystem& files) {
    BasicOpponentService service;service.spatial_=CombatSpatialService::load(files);
    service.movement_=TacticalMovementService::load(files);
    service.magic_=OpponentMagicService::load(files);service.flight_=OpponentFlightService::load(files);
    service.spells_=SpellCatalog::load(files);
    service.gaze_=MonsterGazeService::load(files);service.breath_=MonsterBreathService::load(files);
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    if(module.size()<0xc7c0+0x2870)throw DecodeError("Truncated opponent direction tables");
    std::copy_n(module.begin()+0xc7c0+0x2ae,36,service.directions_.begin());
    for(unsigned policy=1;policy<=6;++policy)for(unsigned attempt=1;attempt<=5;++attempt)
        if(service.directions_[policy*5+attempt]>8)throw DecodeError("Invalid native opponent direction priority");
    std::copy_n(module.begin()+0xc7c0+0x286c,4,service.helpless_codes_.begin());
    service.loaded_=true;return service;
}
std::size_t BasicOpponentService::find(const DeploymentState& state,CombatActorIdentity id) {
    const auto it=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& actor){return actor.id==id;});
    if(it==state.actors.end() || it->removed)throw DecodeError("Opponent actor identity is stale");
    return static_cast<std::size_t>(it-state.actors.begin());
}
bool BasicOpponentService::sector(const BattlefieldTables& tables,unsigned d,CombatPosition from,CombatPosition to) {
    if(d>8)throw DecodeError("Invalid opponent direction sector");
    if(from==to)return true;
    const int x=from.x+tables.delta_x(d),y=from.y+tables.delta_y(d),tx=to.x,ty=to.y;
    if(x==tx && y==ty)return true;
    switch(d) {
    case 0:return !((tx<x || x-tx+y<ty) && (x<tx || tx-x+y<ty));
    case 1:return !((tx<x || x-tx+y<ty) && (tx<x+y-ty || y<ty));
    case 2:return !((tx<x+y-ty || y<ty) && (tx<x+ty-y || ty<y));
    case 3:return !((tx<x+ty-y || ty<y) && (tx<x || ty<tx-x+y));
    case 4:return !((tx<x || ty<tx-x+y) && (x<tx || ty<x-tx+y));
    case 5:return !((x<tx || ty<x-tx+y) && (x+y-ty<tx || ty<y));
    case 6:return !((x+y-ty<tx || ty<y) && (x+ty-y<tx || y<ty));
    case 7:return !((x+ty-y<tx || y<ty) && (x<tx || tx-x+y<ty));
    default:return true;
    }
}
std::uint8_t BasicOpponentService::facing(const DeploymentState& state,const DeploymentActor& actor,const DeploymentActor& target) const {
    if(!actor.position || !target.position)throw DecodeError("Opponent target has no tactical position");
    for(unsigned d=0;d<8;++d)if(sector(state.battlefield.tables,d,*actor.position,*target.position))return static_cast<std::uint8_t>(d);
    throw DecodeError("Opponent direction classification failed");
}
std::vector<BasicOpponentService::Candidate> BasicOpponentService::candidates(const DeploymentState& state,const DeploymentActor& selected,
                                                                          unsigned range,bool ignore_terrain) const {
    //4E80:0909 scans native slots, target footprint before actor footprint;
    //strictly smaller distance retains the first footprint pair on a tie.
    std::vector<const DeploymentActor*> ordered;
    for(const auto& actor:state.actors)if(!actor.removed && actor.position && actor.shape)ordered.push_back(&actor);
    std::sort(ordered.begin(),ordered.end(),[](const auto* a,const auto* b){return a->native_slot<b->native_slot;});
    std::vector<Candidate> result;
    for(const auto* actor:ordered) {
        bool clear=false;std::uint8_t distance=255,direction=0;
        for(const auto to:spatial_.footprint(*actor))for(const auto from:spatial_.footprint(selected)) {
            const auto sight=CombatSpatialService::sight(state.battlefield,from,to,static_cast<std::uint16_t>(range),ignore_terrain);
            if(sight.clear) {
                clear=true;
                if(sight.distance<distance) {
                    distance=sight.distance;
                    for(unsigned d=0;d<8;++d)if(sector(state.battlefield.tables,d,from,to)){direction=static_cast<std::uint8_t>(d);break;}
                }
            }
        }
        if(clear)result.push_back({actor->id,distance,direction});
    }
    //Native selection-sort swaps also affect exact-equal entries. Preserve it.
    for(std::size_t a=0;a<result.size();++a)for(std::size_t b=a+1;b<result.size();++b)
        if(result[b].distance<result[a].distance || (result[b].distance==result[a].distance &&
           result[b].direction<result[a].direction && result[b].direction%2<=result[a].direction%2))std::swap(result[a],result[b]);
    const auto opposing=static_cast<std::uint8_t>(selected.character.byte(0x10e)==0);
    std::erase_if(result,[&](const auto& c){return state.actors[find(state,c.actor)].character.byte(0x10e)!=opposing;});
    return result;
}
bool BasicOpponentService::helpless(const Character& c) const {
    if(!c.effects_restored())throw DecodeError("Opponent effect ownership is unavailable");
    return std::any_of(helpless_codes_.begin(),helpless_codes_.end(),[&](auto code){return has(c,code);});
}
std::uint8_t BasicOpponentService::phase18(DeploymentActor& actor,std::uint8_t value,bool movement) {
    const auto& c=actor.character;
    if(has(c,0x27)) {
        const auto node=std::find_if(c.effects().begin(),c.effects().end(),[](const auto& e){return e.code()==0x27;});
        if(!(node->payload[3]&16))throw DecodeError("Opponent movement requires a presented Haste age notice");
        value=static_cast<std::uint8_t>(value<<1);
    }
    if(has(c,0x2a))value=static_cast<std::uint8_t>(value>>1);
    if(has(c,0x3a)){actor.extension[6]=0;if(movement)value=0;}
    return value;
}
std::uint8_t BasicOpponentService::movement_budget(DeploymentActor& actor,std::uint8_t party_bonus) {
    auto n=actor.character.byte(0x11c);
    if(!actor.character.byte(0x10e))n=static_cast<std::uint8_t>(n+party_bonus);
    if(!n || n>96)n=1;
    return phase18(actor,static_cast<std::uint8_t>(n<<1),true);
}
void BasicOpponentService::quit(DeploymentActor& actor) {
    for(const auto offset:{0u,3u,7u,6u})actor.extension[offset]=0;
}
void BasicOpponentService::optimize(DeploymentState& state,std::size_t index,const ItemCatalog& catalog,std::uint8_t round,std::vector<CombatRoundNotice>& notices) const {
    const auto id=state.actors[index].id;
    const OpponentEquipmentContext context{state.actors[index].retained_target,
        !candidates(state,state.actors[index],1,false).empty(),round};
    auto result=OpponentEquipmentService::stage(state,id,catalog,context);
    // Retain vector storage: the driver's actor/character references borrow it.
    for(std::size_t i=0;i<state.actors.size();++i)state.actors[i]=std::move(result.state.actors[i]);
    notices.insert(notices.end(),result.notices.begin(),result.notices.end());
}
BasicOpponentResult BasicOpponentService::stage(const DeploymentState& state,const BasicOpponentState& progress,
        const ItemCatalog& catalog,const CombatAttackService& attacks,const BasicOpponentContext& context,std::mt19937& random) const {
    if(!loaded_)throw DecodeError("Opponent direction service is not initialized");
    if(progress.finished)throw DecodeError("Opponent turn has already finished");
    auto rng=random;BasicOpponentResult out{state,progress,{},false,false,false};
    const auto index=find(out.state,progress.actor);auto& actor=out.state.actors[index];auto& c=actor.character;
    const auto& spells=context.spells?*context.spells:spells_;
    const OpponentMagicContext magic_context{context.casting_blocked,context.automatic_party_casting};
    const OpponentFlightContext flight_context{context.casualty_percentage,context.opponent_hp_percent,
        context.party_movement_bonus,context.formation_facing};
    const auto adopt=[&](DeploymentState next) {
        if(next.actors.size()!=out.state.actors.size())throw DecodeError("Opponent action changed the retained tactical roster");
        out.state.battlefield=std::move(next.battlefield);out.state.occupancy=std::move(next.occupancy);
        out.state.bodies=std::move(next.bodies);out.state.clouds=std::move(next.clouds);
        out.state.ground_items=std::move(next.ground_items);
        for(std::size_t i=0;i<next.actors.size();++i)out.state.actors[i]=std::move(next.actors[i]);
    };
    if(c.byte(0x10f)==0)throw DecodeError("Opponent service requires source AI control");
    if(c.byte(0x10e)>1 || !c.effects_restored() || !actor.position || !actor.shape)throw DecodeError("Opponent tactical context is unavailable");
    if(progress.initialized && (actor.extension[0x15]<1 || actor.extension[0x15]>6))
        throw DecodeError("Opponent continuation has an invalid source direction policy");
    const auto finish=[&]() {quit(actor);out.finished=true;out.progress.finished=true;random=rng;return out;};
    const auto set_fallback=[&]() {
        const auto gear=EquipmentRules::inspect(c,catalog);
        const bool projectile=gear.slots[0] && catalog.definition(c.inventory().item(*gear.slots[0]).type()).byte(12)>=2;
        const bool guard=!helpless(c) && !projectile && actor.extension[3]!=0;
        quit(actor);if(guard)actor.extension[7]=1;
        out.finished=true;out.progress.finished=true;
    };
    const auto fallback=[&]() {set_fallback();random=rng;return out;};
    const auto acquire=[&](bool force) {
        if(out.progress.target && !force) {
            const auto& target=out.state.actors[find(out.state,*out.progress.target)];
            if(target.character.active() && target.character.byte(0x10e)!=c.byte(0x10e) &&
                attacks.target_available(out.state,actor.id,target.id))return true;
        }
        out.progress.target.reset();actor.retained_target.reset();
        for(unsigned pass=0;pass<2;++pass) {
            const auto options=candidates(out.state,actor,255,pass!=0);
            std::vector<bool> rejected(options.size(),false);
            for(unsigned attempt=0;attempt<20 && !options.empty();++attempt) {
                const auto n=roll(rng,static_cast<unsigned>(options.size()))-1;
                if(rejected[n])continue;
                const auto id=options[n].actor;
                if(pass || attacks.target_available(out.state,actor.id,id)){out.progress.target=id;actor.retained_target=id;return true;}
                rejected[n]=true;
                if(std::all_of(rejected.begin(),rejected.end(),[](bool value){return value;}))break;
            }
        }
        return false;
    };
    if(!out.progress.initialized) {
        //Direction policy randomization precedes morale and every item/spell
        //test, even if later branches make no action.
        auto policy=actor.extension[0x15];
        if(policy<1 || policy>4 || roll(rng,4)==1) {
            if(roll(rng,8)==8)policy=static_cast<std::uint8_t>(roll(rng,2)+4);
            else policy=static_cast<std::uint8_t>(roll(rng,4));
        }
        actor.extension[0x15]=policy;
        if(!c.active())return finish();
        auto preparation=flight_.prepare(out.state,actor.id,catalog,&spells,flight_context,rng);
        adopt(std::move(preparation.state));out.notices=std::move(preparation.notices);
        out.progress.initialized=true;out.progress.decision=OpponentDecisionPhase::items;
        out.progress.target=actor.retained_target;
        if(preparation.finished) {out.finished=true;out.progress.finished=true;random=rng;return out;}
        // Source16EC waits before the item pass. Preserve that genuine host
        // boundary, without rerunning policy/morale on the following call.
        if(!out.notices.empty()) {random=rng;return out;}
    }
    if(out.progress.decision==OpponentDecisionPhase::items) {
        out.casting=magic_.select_item(out.state,actor.id,catalog,spells,magic_context,rng);
        if(out.casting) {random=rng;return out;}
        out.progress.decision=OpponentDecisionPhase::queued_cast;
    }
    if(out.progress.decision==OpponentDecisionPhase::queued_cast) {
        if(actor.extension[0]) {out.execute_queued_cast=true;random=rng;return out;}
        out.progress.decision=OpponentDecisionPhase::turning;
    }
    if(out.progress.decision==OpponentDecisionPhase::turning) {
        if(magic_.wants_turn(out.state,actor.id)) {out.turn_undead=true;random=rng;return out;}
        out.progress.decision=OpponentDecisionPhase::spells;
    }
    if(out.progress.decision==OpponentDecisionPhase::spells) {
        out.casting=magic_.select_spell(out.state,actor.id,catalog,spells,magic_context,rng);
        if(out.casting) {random=rng;return out;}
        optimize(out.state,index,catalog,context.round_index,out.notices);
        out.progress.decision=OpponentDecisionPhase::weapons;
        if(!out.notices.empty()) {random=rng;return out;}
    }
    if(!out.progress.attack_loop_started) {
        if(!acquire(false))return fallback();
        // The fixed native phase14 table continues after Quit. Re-find the
        // first owned node for each code, since preceding callbacks can unlink.
        MonsterGazeContext scratch;scratch.magic.caster=actor.id;
        for(const auto code:{0x53u,0x54u,0x58u,0x79u}) {
            const auto node=std::find_if(c.effects().begin(),c.effects().end(),
                [code](const auto& effect){return effect.code()==code;});
            if(node==c.effects().end())continue;
            const auto effect_index=static_cast<std::size_t>(node-c.effects().begin());
            CombatMagicResult ability;
            if(code==0x58) {
                if(!MonsterBreathService::should_activate(context.round_index,rng))continue;
                auto target=magic_.breath_targets(out.state,actor.id,spells,rng);
                actor.retained_target=target.retained_target;out.progress.target=target.retained_target;
                if(!target.cursor)throw DecodeError("Breath automatic selector lost its actual native cursor");
                auto damage_context=scratch.magic;damage_context.absorption_suppressed=target.intent.has_value();
                // Approved native58 compatibility policy: resistance uses the
                // same metadata as targeting, rather than an unrelated earlier
                // spell's681B/item6DBF. SpellDefinition validates school0/1;
                // native26C2 reads the selected source's96/9B respectively.
                // This innate action has no item-origin level6 override. Do
                // not infer pending ID, dice count or other ambient ownership.
                const auto school=spells.definition(SpellId{OpponentMagicService::breath_metadata}).school();
                damage_context.level=actor.character.byte(school==0?0x96:0x9b);
                const auto result=breath_.stage_after_gate(out.state,
                    {actor.id,effect_index,*node,*target.cursor,damage_context,scratch.pending_id_known,true},catalog,spells,rng);
                ability=result.magic;
                //Every occupied28BC call sets0C before damage then clears6819
                //to0 on return. An entirely empty ray establishes no flags.
                if(!ability.damage.empty()) {scratch.damage_flags=0;scratch.damage_flags_known=true;}
                //295B clears6820 after a nonzero radial extension. Its
                //self/no-extension branch retains the actual selector result.
                scratch.magic.absorption_suppressed=target.intent.has_value() && *target.cursor==*actor.position;
            } else {
                auto result=gaze_.stage(out.state,actor.id,effect_index,scratch,rng);
                scratch=result.context;ability=std::move(result.magic);
            }
            adopt(ability.state);
            out.abilities.push_back({actor.id,static_cast<std::uint8_t>(code),std::move(ability)});
        }
        out.progress.attack_loop_started=true;out.progress.weapon_iterations=0;
        if(!out.abilities.empty()) {
            out.finished=!positive(actor.extension[3]);out.progress.finished=out.finished;
            random=rng;return out;
        }
    }
    if(!positive(actor.extension[3]))return finish();
    if(actor.extension[0x14] && actor.extension[6] && actor.extension[3]!=20) {
        auto progress_flight=out.progress.flight.value_or(OpponentFlightProgress{actor.id,out.progress.target,8,0,{}});
        auto flown=flight_.step(out.state,progress_flight,catalog,attacks,context.attacks,&spells,flight_context,rng);
        if(flown.reacquire_target) {
            adopt(std::move(flown.state));out.progress.target.reset();
            if(!acquire(true))return fallback();
            flown.progress.target=out.progress.target;
            flown=flight_.step(out.state,flown.progress,catalog,attacks,context.attacks,&spells,flight_context,rng);
            if(flown.reacquire_target)throw DecodeError("Opponent flight repeated an unresolved acquisition boundary");
        }
        out.state=std::move(flown.state);out.progress.target=flown.progress.target;
        out.progress.flight=std::move(flown.progress);
        out.strikes=std::move(flown.strikes);out.notices=std::move(flown.notices);out.moved=flown.moved;out.attacked=!out.strikes.empty();
        out.finished=flown.finished;out.progress.finished=flown.finished;
        random=rng;return out;
    }
    //0B3C's >20 fallback still falls through the current iteration tail.
    // Preserve that quirk; never re-Quit a completed fallback on selector resume.
    if(++out.progress.weapon_iterations>20)set_fallback();
    auto equipped=EquipmentRules::inspect(c,catalog);unsigned reach=1;
    if(equipped.slots[0]) {
        const auto& d=catalog.definition(c.inventory().item(*equipped.slots[0]).type());
        reach=static_cast<std::uint8_t>(d.byte(12)-1);if(!reach || reach==255)reach=1;
    }
    //Native0B3C keeps its retained target for the attack-range fast path only
    //when target10E is nonzero. Otherwise2433 picks a fresh random candidate.
    std::optional<CombatActorIdentity> in_reach;bool freshly_selected=false;
    if(out.progress.target) {
        const auto& target=out.state.actors[find(out.state,*out.progress.target)];
        if(target.character.active() && target.character.byte(0x10e) && attacks.target_available(out.state,actor.id,target.id) &&
           attacks.can_target(out.state,actor.id,target.id,catalog))in_reach=target.id;
    }
    if(!in_reach) {
        const auto options=candidates(out.state,actor,reach,false);
        if(!options.empty()) {in_reach=options[roll(rng,static_cast<unsigned>(options.size()))-1].actor;freshly_selected=true;}
    }
    if(in_reach) {
        //0B3C re-enters13CA when a nondual projectile user has an adjacent
        //opponent, then returns to its outer AI loop before attacking.
        if(equipped.slots[0]) {
            const auto& d=catalog.definition(c.inventory().item(*equipped.slots[0]).type());
            if(d.byte(12)>=2 && (d.attack_flags()&0x14)!=0x14 && !candidates(out.state,actor,1,false).empty()) {
                optimize(out.state,index,catalog,context.round_index,out.notices);out.progress.attack_loop_started=false;
                random=rng;return out;
            }
        }
        const auto& target=out.state.actors[find(out.state,*in_reach)];
        const bool adjacent_bypass=freshly_selected && attacks.native_distance(out.state,actor.id,*in_reach)==1;
        if(!adjacent_bypass && !attacks.target_available(out.state,actor.id,*in_reach)) {random=rng;return out;}
        if(!attacks.can_target(out.state,actor.id,*in_reach,catalog))throw DecodeError("Opponent selected an unavailable source melee target");
        actor.extension[9]=facing(out.state,actor,target);
        auto result=adjacent_bypass?attacks.stage_selected_adjacent_ai(out.state,actor.id,*in_reach,catalog,context.attacks,rng):
                                    attacks.stage(out.state,actor.id,*in_reach,catalog,context.attacks,rng);
        out.state=std::move(result.state);out.strikes=std::move(result.strikes);out.attacked=true;
        out.standalone_travel=std::move(result.standalone_travel);
        out.finished=out.finished || result.turn_finished;out.progress.finished=out.finished;
        if(!out.finished && !out.state.actors[find(out.state,*in_reach)].character.active())out.progress.attack_loop_started=false;
        out.progress.target=out.state.actors[index].retained_target;
        random=rng;return out;
    }
    if(actor.extension[6]<2 || c.byte(0x2f)==5)return fallback();
    if(!acquire(false))return fallback();
    const auto& target=out.state.actors[find(out.state,*out.progress.target)];
    const auto toward=facing(out.state,actor,target);unsigned selected=8;
    for(unsigned attempt=1;attempt<=5;++attempt) {
        const auto direction=(toward+directions_[actor.extension[0x15]*5+attempt])%8;
        const auto probe=spatial_.probe(out.state,actor.id,direction);
        if(!probe.terrain)continue;
        if(probe.occupied_slot || probe.terrain_cost>=actor.extension[6])continue;
        const auto needed=static_cast<std::uint8_t>(probe.step_cost*probe.terrain_cost);
        if(needed>actor.extension[6])continue;
        if(probe.terrain==30 && !(has(c,0x20)||has(c,0x1e)||has(c,0x6f)||has(c,0x7d)))continue;
        selected=direction;break;
    }
    if(selected==8 || out.progress.last_direction==(selected+4)%8) {
        ++out.progress.reversals;actor.extension[0x15]=static_cast<std::uint8_t>(actor.extension[0x15]%6+1);
        if(out.progress.reversals>1) {
            out.progress.target.reset();
            if(out.progress.reversals<3){if(!acquire(false))return fallback();}
            else {actor.extension[6]=0;random=rng;return out;}
        }
    }
    if(selected==8) {
        //The source caller retries; no artificial movement or turn skip.
        random=rng;return out;
    }
    out.progress.last_direction=static_cast<std::uint8_t>(selected);
    auto moved=movement_.stage(out.state,actor.id,selected,catalog,attacks,context.attacks,rng,&spells);
    if(!moved.step.state || (!moved.step.moved && !moved.interrupted))
        throw DecodeError("Opponent movement requires unresolved reaction, guard or terrain service: "+moved.step.message);
    out.state=std::move(*moved.step.state);out.moved=moved.step.moved;
    out.strikes=std::move(moved.strikes);out.attacked=!out.strikes.empty();
    out.notices=std::move(moved.notices);
    if(moved.interrupted){out.finished=true;out.progress.finished=true;}
    random=rng;return out;
}
}
