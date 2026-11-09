#include "gbengine/combat/round_service.hpp"
#include "gbengine/combat/actor_preparation.hpp"
#include "gbengine/party/equipment.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <memory>
#include <limits>

namespace GBEngine {
namespace {
int signed_byte(std::uint8_t n) {return n<128?n:static_cast<int>(n)-256;}
unsigned roll(std::mt19937& random,unsigned sides) {
    const auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    std::uint32_t value;
    do {value=static_cast<std::uint32_t>(random());}while(value<threshold);
    return value%sides+1;
}
std::uint8_t half(std::uint8_t n,std::uint8_t round) {
    if(round&1)n=static_cast<std::uint8_t>(n+1);
    return static_cast<std::uint8_t>(n>>1);
}
void quit(DeploymentActor& actor) {
    actor.extension[3]=0;actor.extension[0]=0;
    actor.extension[7]=0;actor.extension[6]=0;
}
void begin_selection(CombatRoundState& round) {
    if(round.selection_sequence==std::numeric_limits<std::uint64_t>::max())throw DecodeError("Combat selection sequence exhausted");
    ++round.selection_sequence;round.notices.clear();round.selection_playback.clear();
}
}
int CombatRoundService::dexterity_modifier(std::uint8_t n) {
    if(n<3)return -4;
    if(n<=5)return static_cast<int>(n)-6;
    if(n>=16 && n<=18)return static_cast<int>(n)-15;
    if(n>=19 && n<=20)return 3;
    if(n>=21 && n<=23)return 4;
    if(n>=24 && n<=25)return 5;
    return 0;
}
std::uint8_t CombatRoundService::phase18(DeploymentActor& actor,std::uint8_t value,bool movement,
                                        std::vector<CombatRoundNotice>& notices) {
    auto& character=actor.character;
    if(!character.effects_restored_)throw DecodeError("Combat round requires restored effect ownership");
    auto first=[&](std::uint8_t code) {
        return std::find_if(character.effects_.begin(),character.effects_.end(),[&](const auto& e){return e.code()==code;});
    };
    const auto haste=first(0x27);
    if(haste!=character.effects_.end()) {
        if(!(haste->payload[3]&0x10)) {
            haste->payload[3]=static_cast<std::uint8_t>(haste->payload[3]+0x10);
            character.set_word(0x30,static_cast<std::uint16_t>(character.word(0x30)+1));
            notices.push_back({actor.id,pascal_string_id("GAME.OVR",0xf8a1)});
        }
        value=static_cast<std::uint8_t>(value<<1);
    }
    if(first(0x2a)!=character.effects_.end())value=static_cast<std::uint8_t>(value>>1);
    if(first(0x3a)!=character.effects_.end()) {
        actor.extension[6]=0;
        if(movement)value=0;
    }
    return value;
}
void CombatRoundService::prepare_actor(DeploymentActor& actor,const ItemCatalog& catalog,
                                      CombatRoundState& round,std::mt19937& random) {
    auto& ext=actor.extension;auto& character=actor.character;
    ext[0]=0;ext[1]=1;ext[2]=1;ext[8]=0;ext[4]=2;
    // 326D0D23: readied slotCC is the last equipped category0 item. Saved far
    // pointers are not identities. Ammunition slots are the verified49/1C.
    const auto equipped=EquipmentRules::inspect(character,catalog);
    bool projectile=false;const Item* ammunition=nullptr;
    auto attacks=character.byte(0xa1);
    if(equipped.slots[0]) {
        const auto& weapon=character.inventory().item(*equipped.slots[0]);
        const auto& definition=catalog.definition(weapon.type());
        if(definition.byte(12)>=2) {
            const auto flags=definition.attack_flags();
            if(flags&0x10)ammunition=&weapon;
            if(flags&8) {
                if(flags&1)ammunition=equipped.slots[11]?&character.inventory().item(*equipped.slots[11]):nullptr;
                if(flags&0x80)ammunition=equipped.slots[12]?&character.inventory().item(*equipped.slots[12]):nullptr;
            }
            projectile=ammunition || flags==10;
            if(projectile)attacks=std::max(std::uint8_t{2},definition.byte(5));
        }
    }
    attacks=half(phase18(actor,attacks,false,round.notices),round.round_index);
    if(projectile && ammunition)attacks=std::min(attacks,std::max(std::uint8_t{1},ammunition->quantity()));
    // ext8 was just reset, so the source reuse/doubling branch cannot retain
    // the preceding round's113 here.
    character.set_byte(0x113,attacks);
    character.set_byte(0x114,half(phase18(actor,character.byte(0xa2),false,round.notices),round.round_index));
    ext[5]=character.byte(0x6b);
    if(!character.active())ext[3]=0;
    else {
        auto priority=static_cast<std::uint8_t>(static_cast<int>(roll(random,6))+dexterity_modifier(character.byte(0x13)));
        if(signed_byte(priority)<1)priority=1;
        if((static_cast<std::uint8_t>(character.byte(0x10e)+1)&static_cast<std::uint8_t>(round.surprise_mask))!=0)
            priority=static_cast<std::uint8_t>(priority-6);
        if(signed_byte(priority)<0 || signed_byte(priority)>20)priority=0;
        ext[3]=priority;
    }
    auto movement=character.byte(0x11c);
    if(character.byte(0x10e)==0)movement=static_cast<std::uint8_t>(movement+round.party_movement_bonus);
    if(movement==0 || movement>96)movement=1;
    ext[6]=phase18(actor,static_cast<std::uint8_t>(movement<<1),true,round.notices);
}
std::optional<std::size_t> CombatRoundService::select_next(const DeploymentState& state,std::mt19937& random) {
    auto staged_random=random;
    std::optional<std::size_t> selected;
    unsigned priority=0,tie=0;
    for(std::size_t i=0;i<state.actors.size();++i) {
        const auto& actor=state.actors[i];if(actor.removed)continue;
        const auto draw=roll(staged_random,100);
        const int candidate=signed_byte(actor.extension[3]);
        if(static_cast<int>(priority)<candidate)tie=draw;
        if(static_cast<int>(priority)<=candidate && tie<=draw) {
            priority=actor.extension[3];tie=draw;selected=i;
        }
    }
    if(priority==0)selected.reset();
    random=std::move(staged_random);
    return selected;
}
void CombatRoundService::advance_in_place(DeploymentState& state,CombatRoundState& round,
                                         const ItemCatalog& catalog,std::mt19937& random,const CombatRoundContext* context) {
    // Phase7/15 may clear a selected actor. Source immediately rescans and
    // rolls again. Each skipped actor loses its positive priority, bounding it.
    for(std::size_t attempt=0;attempt<=state.actors.size();++attempt) {
        const auto next=select_next(state,random);
        if(!next) {round.active.reset();round.phase=CombatRoundPhase::round_end_pending;return;}
        round.effect_caster=state.actors[*next].id;
        state.actors[*next].extension[0xf]=0;state.actors[*next].extension[0x12]=0;state.actors[*next].extension[7]=0;
        CombatActorPreparation::actor_turn(state,*next,catalog,&round.notices,context,random,round.selection_playback);
        auto& actor=state.actors[*next];
        if(signed_byte(actor.extension[3])<=0)continue;
        if(actor.character.byte(0x10f)!=0) {
            round.active=actor.id;round.phase=CombatRoundPhase::ai_pending;return;
        }
        if(!actor.character.active()) {quit(actor);continue;}
        if(actor.extension[0]!=0) {
            round.active=actor.id;round.phase=CombatRoundPhase::casting_pending;return;
        }
        round.active=actor.id;round.phase=CombatRoundPhase::player;return;
    }
    throw DecodeError("Combat turn preparation exceeded the native participant bound");
}
void CombatRoundService::begin_round(DeploymentState& state,CombatRoundState& round,
                                    const ItemCatalog& catalog,std::mt19937& random,const CombatRoundContext* context) {
    if(round.phase!=CombatRoundPhase::not_started || round.active)
        throw DecodeError("Combat round is already started or requires unresolved round-end lifecycle");
    auto staged=std::make_unique<DeploymentState>(state);auto next=round;auto staged_random=random;
    begin_selection(next);
    for(auto& actor:staged->actors)if(!actor.removed)prepare_actor(actor,catalog,next,staged_random);
    next.surprise_mask=0;
    for(const auto& notice:next.notices)next.selection_playback.push_back({notice,{},std::make_shared<const DeploymentState>(*staged)});
    advance_in_place(*staged,next,catalog,staged_random,context);
    state=std::move(*staged);round=std::move(next);random=std::move(staged_random);
}
void CombatRoundService::finish_actor(DeploymentState& state,CombatRoundState& round,
                                    const ItemCatalog& catalog,std::mt19937& random,const CombatRoundContext* context) {
    if((round.phase!=CombatRoundPhase::player && round.phase!=CombatRoundPhase::ai_pending && round.phase!=CombatRoundPhase::casting_pending) || !round.active)
        throw DecodeError("Combat end-turn requires a current actor");
    auto staged=std::make_unique<DeploymentState>(state);auto next=round;auto staged_random=random;
    const auto found=std::find_if(staged->actors.begin(),staged->actors.end(),[&](const auto& a){return !a.removed && a.id==*next.active;});
    if(found==staged->actors.end())throw DecodeError("Combat active actor identity no longer exists");
    quit(*found);begin_selection(next);next.active.reset();
    advance_in_place(*staged,next,catalog,staged_random,context);
    state=std::move(*staged);round=std::move(next);random=std::move(staged_random);
}
void CombatRoundService::finish_turn(DeploymentState& state,CombatRoundState& round,
                                    const ItemCatalog& catalog,std::mt19937& random,const CombatRoundContext* context) {
    if(round.phase!=CombatRoundPhase::player)throw DecodeError("Player Done cannot skip an AI actor");
    finish_actor(state,round,catalog,random,context);
}
void CombatRoundService::finish_cast(DeploymentState& state,CombatRoundState& round,
                                    const ItemCatalog& catalog,std::mt19937& random,const CombatRoundContext* context) {
    if(round.phase!=CombatRoundPhase::casting_pending)throw DecodeError("Combat cast completion requires its executor boundary");
    finish_actor(state,round,catalog,random,context);
}
void CombatRoundService::resume_selection(DeploymentState& state,CombatRoundState& round,
                                         const ItemCatalog& catalog,std::mt19937& random,const CombatRoundContext* context) {
    if(!round.active || (round.phase!=CombatRoundPhase::player && round.phase!=CombatRoundPhase::ai_pending))
        throw DecodeError("Combat selection requires a current actor");
    auto staged=std::make_unique<DeploymentState>(state);auto next=round;auto next_random=random;
    begin_selection(next);next.active.reset();advance_in_place(*staged,next,catalog,next_random,context);
    state=std::move(*staged);round=std::move(next);random=std::move(next_random);
}
void CombatRoundService::finish_opponent_turn(DeploymentState& state,CombatRoundState& round,
                                    const ItemCatalog& catalog,std::mt19937& random,const CombatRoundContext* context) {
    if(round.phase!=CombatRoundPhase::ai_pending)throw DecodeError("Opponent completion requires its selected AI actor");
    if(!round.active)throw DecodeError("Opponent completion lost its actor identity");
    const auto found=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& actor){return actor.id==*round.active;});
    if(found==state.actors.end() || signed_byte(found->extension[3])>0)
        throw DecodeError("Opponent must genuinely complete its source action before selection advances");
    auto staged=std::make_unique<DeploymentState>(state);auto next=round;auto next_random=random;
    // The AI driver already performed its own Quit/Guard. Repeating Quit here
    // would erase a native fallback Guard before the next actor can move.
    begin_selection(next);next.active.reset();advance_in_place(*staged,next,catalog,next_random,context);
    state=std::move(*staged);round=std::move(next);random=std::move(next_random);
}
}
