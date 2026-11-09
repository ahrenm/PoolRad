#include "gbengine/combat/monster_gaze_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <limits>

namespace GBEngine {
namespace {
// By design: Charming Gaze reaches anywhere on the battlefield, with native
// terrain LOS still required. Native54 leaves its range WORD uninitialized;
// two original-runtime captures inherited3E58, not an intended range constant.
// The field's Manhattan span conservatively covers its cardinal2/diagonal3
// metric after sight() converts whole-square range to range*2+1 half steps.
constexpr std::uint16_t charm_battlefield_range=Battlefield::width+Battlefield::height;
std::size_t find_actor(const DeploymentState& state,CombatActorIdentity id) {
    const auto at=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& a){return a.id==id;});
    if(at==state.actors.end() || at->removed)throw DecodeError("Monster ability requires a retained actor identity");
    if(!at->character.effects_restored())throw DecodeError("Monster ability requires restored effect ownership");
    return static_cast<std::size_t>(at-state.actors.begin());
}
bool has(const Character& c,std::uint8_t code) {
    return std::any_of(c.effects().begin(),c.effects().end(),[&](const auto& e){return e.code()==code;});
}
bool any(const Character& c,std::initializer_list<std::uint8_t> codes) {
    return std::any_of(codes.begin(),codes.end(),[&](const auto code){return has(c,code);});
}
std::uint8_t die(std::mt19937& random,unsigned sides,unsigned count=1) {
    unsigned sum=0;const auto threshold=(std::uint32_t{0}-sides)%sides;
    for(unsigned i=0;i<count;++i){std::uint32_t n;do {n=static_cast<std::uint32_t>(random());}while(n<threshold);sum+=n%sides+1;}
    return static_cast<std::uint8_t>(sum);
}
CombatPosition position(const DeploymentState& state,CombatActorIdentity id) {
    const auto& a=state.actors[find_actor(state,id)];
    if(!a.position)throw DecodeError("Monster ability requires native tactical coordinates");
    return *a.position;
}
}
MonsterGazeService MonsterGazeService::load(const IFileSystem& files) {
    MonsterGazeService result;result.effects_=CombatMagicEffects::load(files);
    result.attacks_=CombatAttackService::load(files);result.spatial_=CombatSpatialService::load(files);
    result.items_=ItemCatalog::load(files);result.spells_=SpellCatalog::load(files);return result;
}
MonsterGazeResult MonsterGazeService::stage(const DeploymentState& state,CombatActorIdentity source,
        std::size_t effect_index,const MonsterGazeContext& context,std::mt19937& random) const {
    const auto si=find_actor(state,source);const auto& original=state.actors[si];
    if(effect_index>=original.character.effects().size())throw DecodeError("Monster ability source node is stale");
    const auto source_node=original.character.effects()[effect_index];const auto code=source_node.code();
    if(code!=0x53 && code!=0x54 && code!=0x79)throw DecodeError("Monster gaze service received another native effect code");
    if(context.magic.caster!=source)throw DecodeError("Monster ability requires the genuine selected source actor");
    auto rng=random;MonsterGazeResult result;result.magic.state=state;result.context=context;
    auto& out=result.magic;auto& scratch=result.context;
    if(scratch.inherited_scratch_known) {
        scratch.damage_flags_known=true;scratch.pending_id_known=true;
    }
    out.casting_before=std::make_shared<const DeploymentState>(state);
    const auto retained=[&] {
        const auto& a=out.state.actors[find_actor(out.state,source)];
        if(!a.retained_target)throw DecodeError("Monster ability has a native null retained-target dependency");
        (void)find_actor(out.state,*a.retained_target);return *a.retained_target;
    };
    const auto emit=[&](CombatActorIdentity recipient,std::size_t offset,bool animation=false,std::uint8_t color=10) {
        out.events.push_back({CombatMagicEventKind::feedback,out.feedback.size(),std::make_shared<const DeploymentState>(out.state)});
        out.feedback.push_back({recipient,pascal_string_id("GAME.OVR",offset),animation,color,true});
    };
    const auto ray=[&](CombatActorIdentity from,CombatActorIdentity to,std::uint8_t slot,std::uint8_t frames,std::uint8_t wait) {
        out.events.push_back({CombatMagicEventKind::projectile,out.projectiles.size(),std::make_shared<const DeploymentState>(out.state)});
        out.projectiles.push_back({position(out.state,from),position(out.state,to),slot,frames,wait});
    };
    const auto level=[&](std::uint8_t pending) {
        const auto module=spells_.native_module();const auto at=std::size_t{0xf9ba}+16u*pending;
        if(at>=module.size())throw DecodeError("Monster ability pending-cast school is outside original data");
        const auto school=module[at];if(school>2)throw DecodeError("Monster ability has a native uninitialized pending-cast level");
        if(school==2)return std::uint8_t{12};
        const auto value=out.state.actors[find_actor(out.state,source)].character.byte(school==0?0x96:0x9b);
        if(!scratch.item_context && value!=6)throw DecodeError("Monster resistance requires native item-context scratch6DBF");
        return scratch.item_context.value_or(false)?std::uint8_t{6}:value;
    };
    const auto save=[&](CombatActorIdentity target,std::uint8_t category,std::int8_t modifier) {
        // User-approved non-elemental petrification policy. A reflected gaze
        // uses the same category1 save, without ambient fire/cold bonuses.
        if(code==0x53)
            return CombatMagicEffects::save(out.state,target,category,modifier,scratch.magic,rng,0).saved;
        if(scratch.damage_flags_known)
            return CombatMagicEffects::save(out.state,target,category,modifier,scratch.magic,rng,scratch.damage_flags).saved;
        // A successful proof does not establish flags for later damage,
        // application or terminal callbacks; those retain their own guards.
        return CombatMagicEffects::save_invariant(out.state,target,category,modifier,scratch.magic,rng);
    };
    const auto damage=[&](CombatActorIdentity target,std::uint8_t amount,bool saved,std::uint8_t mode) {
        const auto& target_character=out.state.actors[find_actor(out.state,target)].character;
        if(!scratch.inherited_scratch_known && any(target_character,{0x69,0x6a}))
            throw DecodeError("Monster damage requires inherited pending-cast or elemental scratch");
        //79/50 establish681C=8/1 through0E2E. Their elemental flags remain
        //independently inherited, except50's explicit10 or a prior28BC clear.
        if(!scratch.damage_flags_known && any(target_character,{0x71,0x3d,0x0a,0x14,0x70,0x72,0x76,0x5d,0x7a}))
            throw DecodeError("Monster damage requires inherited elemental flags");
        if(!scratch.pending_id_known && (has(target_character,0x11) ||
            (has(target_character,0x1c) && !scratch.magic.absorption_suppressed) ||
            (has(target_character,0x5b) && (!scratch.damage_flags_known || !(scratch.damage_flags&4)))))
            throw DecodeError("Monster damage requires inherited pending-cast ID");
        if(any(target_character,{0x69,0x6a}))scratch.magic.level=level(scratch.magic.pending_id);
        const bool changes_pending=any(target_character,{0x3d,0x5b,0x69,0x6a,0x70,0x1c});
        const auto before=std::make_shared<const DeploymentState>(out.state);
        auto hit=effects_.damage(out.state,target,amount,scratch.damage_flags,saved,mode,scratch.magic,rng);
        out.state=std::move(hit.state);const auto& d=hit.outcome;
        out.events.push_back({CombatMagicEventKind::damage,out.damage.size(),before,std::make_shared<const DeploymentState>(out.state)});
        out.damage.push_back({source,target,d.amount,d.before_hp,d.after_hp,d.status,saved,d.interrupted_spell,scratch.damage_flags});
        for(const auto& notice:hit.notices) {
            if(notice.member>=out.state.actors.size())throw DecodeError("Monster damage notice recipient is stale");
            out.events.push_back({CombatMagicEventKind::feedback,out.feedback.size(),std::make_shared<const DeploymentState>(out.state)});
            out.feedback.push_back({out.state.actors[notice.member].id,notice.text_id,false});
        }
        // The existing damage adapter does not expose cancelled6817. Do not
        // manufacture its successor value for a later dependent phase14 hook.
        if(changes_pending)scratch.inherited_scratch_known=false;
    };
    if(code==0x53) {
        auto victim=retained();emit(source,0x108e8);ray(source,victim,18,4,45);
        if(has(original.character,0x7f)) {
            const auto& inventory=out.state.actors[find_actor(out.state,victim)].character.inventory();bool reflected=false;
            for(std::size_t i=0;i<inventory.size();++i) {
                const auto& item=inventory.item(i);
                if(item.byte(0x34) && (item.byte(0x2f)==0x76 || item.byte(0x30)==0x76 || item.byte(0x31)==0x76)) {reflected=true;break;}
            }
            if(reflected){emit(victim,0x108f1,false,12);ray(victim,source,18,4,45);victim=source;}
        }
        if(!save(victim,1,0)) {
            emit(victim,0x108fe);const auto before=std::make_shared<const DeploymentState>(out.state);
            const auto hp=out.state.actors[find_actor(out.state,victim)].character.hit_points();
            const auto flags=scratch.damage_flags_known?std::optional<std::uint8_t>{scratch.damage_flags}:std::nullopt;
            std::vector<CombatDefeatNotice> notices;
            const bool applied=attacks_.health_service().apply_terminal_status_in_place(out.state,victim,7,flags,&rng,&notices);
            for(const auto& notice:notices) {
                (void)find_actor(out.state,notice.actor);
                const auto scene=std::make_shared<const DeploymentState>(out.state);
                out.events.push_back({CombatMagicEventKind::feedback,out.feedback.size(),scene,scene});
                out.feedback.push_back({notice.actor,notice.text_id,false});
            }
            if(applied) {
                const auto& c=out.state.actors[find_actor(out.state,victim)].character;
                out.events.push_back({CombatMagicEventKind::terminal,out.damage.size(),before,std::make_shared<const DeploymentState>(out.state)});
                out.damage.push_back({source,victim,0,hp,c.hit_points(),c.status(),false,{}});
            }
        }
    } else if(code==0x54) {
        const auto victim=retained();const auto from=position(out.state,source),to=position(out.state,victim);
        if(!attacks_.target_available(out.state,source,victim)){random=std::move(rng);return result;}
        if(!CombatSpatialService::sight(out.state.battlefield,from,to,charm_battlefield_range).clear){random=std::move(rng);return result;}
        emit(source,0x10abe);ray(source,victim,18,4,45);scratch.magic.pending_id=10;scratch.pending_id_known=true;
        const bool saved=save(victim,4,-2);scratch.magic.pending_effect=0x0b;
        auto& target=out.state.actors[find_actor(out.state,victim)].character;std::uint8_t pending=0x0b;
        constexpr std::array<std::uint8_t,10> queries{0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,0x70,0x7c,0x7d};
        for(const auto query:queries)if(has(target,query)) {
            if(query==0x69 || query==0x6a) {
                if(!pending && !scratch.damage_flags_known)throw DecodeError("Charm resistance requires inherited damage flags after cancellation");
                if(pending || (scratch.damage_flags&8)) {
                    const auto threshold=static_cast<std::uint8_t>((query==0x69?50:15)-(11-static_cast<int>(level(10)))*5);
                    if(die(rng,100)<=threshold)pending=0;
                }
            } else if(query==0x6b || query==0x7c) {
                if(die(rng,100)<=(query==0x6b?90u:30u) && (pending==0x0b || pending==0x35))pending=0;
            } else if(query==0x6c || query==0x7d) {if(pending==0x0b || pending==0x35)pending=0;}
            else if(query==0x6e || query==0x70) {
                if(!scratch.damage_flags_known)throw DecodeError("Charm application requires inherited elemental flags");
                if(scratch.damage_flags&(query==0x6e?2:1))pending=0;
            }
        }
        scratch.magic.pending_effect=pending;
        if(!pending || saved)emit(victim,0x2c532);
        else {
            const auto old=std::find_if(target.effects_.begin(),target.effects_.end(),[](const auto& e){return e.code()==0x0b;});
            if(old!=target.effects_.end()) {
                const auto notices=CombatMagicEffects::remove_effect_in_place(out.state,victim,static_cast<std::size_t>(old-target.effects_.begin()),items_,spells_,rng);
                if(!notices.empty())throw DecodeError("Charm replacement requires an unbound callback presentation");
            }
            auto& c=out.state.actors[find_actor(out.state,victim)].character;
            if(c.effects_.size()>=65535)throw DecodeError("Monster Charm exceeds native effect capacity");
            CharacterEffect node;node.payload={0x0b,0,0,static_cast<std::uint8_t>(out.state.actors[find_actor(out.state,source)].character.byte(0x10e)*128+12),1};
            node.source_file="GAME.OVR";node.source_offset=0x10ad2;c.effects_.push_back(std::move(node));emit(victim,0x10ac7,true);
        }
        // Source always activates the first surviving0B, including a vetoed
        // new application when an older duplicate already exists.
        CombatMagicEffects::apply_charm_in_place(out.state,victim);
    } else {
        if(die(rng,100)>=26){random=std::move(rng);return result;}
        const auto victim=retained();unsigned distance=256;
        for(const auto from:spatial_.footprint(out.state.actors[si]))
            for(const auto to:spatial_.footprint(out.state.actors[find_actor(out.state,victim)]))
                distance=std::min(distance,static_cast<unsigned>(CombatSpatialService::range(from,to)));
        if(distance==256)throw DecodeError("Native acid distance has no enumerated victim footprint");
        if((distance>>1)>=4){random=std::move(rng);return result;}
        // Recursive flagged79 can free its own active node. Never suppress
        // that callback or publish only the preceding Quit/damage/RNG.
        if(source_node.payload[4])throw DecodeError("Acid79 removal requires unsafe recursive native node ownership");
        auto& owner=out.state.actors[si];for(const auto offset:{3u,0u,7u,6u})owner.extension[offset]=0;
        result.quit=true;emit(source,0x1186c);ray(source,victim,23,1,30);
        scratch.magic.dice_count=8;const auto amount=die(rng,4,8);const bool saved=save(victim,3,0);
        damage(victim,amount,saved,2);
        auto& effects=out.state.actors[si].character.effects_;
        if(effect_index>=effects.size() || effects[effect_index]!=source_node)
            throw DecodeError("Acid source node was invalidated by its own impact cleanup");
        effects.erase(effects.begin()+static_cast<std::ptrdiff_t>(effect_index));
        const auto old=std::find_if(effects.begin(),effects.end(),[](const auto& e){return e.code()==0x50;});
        if(old!=effects.end()) {
            const auto index=static_cast<std::size_t>(old-effects.begin());const auto node=*old;
            if(node.payload[4]) {
                scratch.damage_flags=0x10;scratch.damage_flags_known=true;scratch.magic.dice_count=1;const auto followup=die(rng,4);
                damage(retained(),followup,false,0);
            }
            auto& current=out.state.actors[si].character.effects_;
            if(index>=current.size() || current[index]!=node)throw DecodeError("Acid50 saved node was invalidated by its reversal");
            current.erase(current.begin()+static_cast<std::ptrdiff_t>(index));
        }
    }
    random=std::move(rng);return result;
}
}
