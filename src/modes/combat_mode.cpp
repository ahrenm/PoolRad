#include "gbengine/debug/runtime.hpp"
#include "gbengine/modes/modes.hpp"
#include "gbengine/ui/game_over_screen.hpp"
#include "gbengine/ui/combat_screen.hpp"
#include "gbengine/ui/combat_magic_menu.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include "gbengine/rendering/combat_presentation.hpp"
#include "gbengine/app/runtime_timing.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
class CombatMode final:public IGameMode {
    ExplorationSession& session_;
    CombatScreen screen_;
    CombatToken token_;
    CombatPresentation presentation_;
    const GameFont& font_;
    const ScreenChrome& chrome_;
    NativeSoundCues sounds_;
    std::optional<CombatMagicMenu> magic_menu_;
    std::uint64_t magic_menu_revision_{};
    bool magic_targeting_=false,magic_manual_=false,cancel_spell_question_=false;
    std::optional<CombatMagicTargetPolicy> magic_policy_;
    std::vector<CombatActorIdentity> magic_targets_,magic_selected_;
    std::size_t magic_target_index_=0;
    CombatPosition magic_cursor_{};
    struct ActionVisual {
        std::optional<CombatMagicFrame> projectile;
        std::shared_ptr<const DeploymentState> snapshot;
        CombatActorIdentity focus;
        std::string notice;
        unsigned pose{};
        std::uint64_t duration{};
        std::optional<CombatRecipientEffect> recipient_effect={};
        std::optional<std::uint16_t> sound_before={},sound_after={};
        unsigned color=14;
        std::optional<CombatWeaponFrame> weapon_projectile={};
        bool native_blocking=false;
    };
    std::vector<ActionVisual> action_visuals_;
    std::uint64_t selection_seen_{};
    std::shared_ptr<const std::vector<MonsterAbilityPlayback>> monsters_seen_;
    bool playing_action_=false;
    std::string magic_caption_;
    std::shared_ptr<CombatViewAbort> abort_=std::make_shared<CombatViewAbort>();
    std::unique_ptr<Party> view_party_;
    std::vector<CombatRoundNotice> notices_;
    std::size_t notice_=0;
    bool showing_notice_=false,done_menu_=false;
    bool aiming_=false,manual_aim_=false,friendly_question_=false,friendly_from_move_=false,finish_after_present_=false;
    bool manual_interrupt_=false,magic_toggle_pending_=false;
    std::vector<CombatTarget> targets_;
    std::size_t target_index_=0;
    std::optional<CombatTarget> aim_target_;
    std::optional<unsigned> flight_direction_;
    std::optional<std::uint64_t> automatic_revision_;
    std::string feedback_;
    mutable std::optional<CombatPresentationToken> rendered_step_;
    std::optional<PresentationAudioCue> pending_sound_end_;
    std::optional<CombatPresentationToken> started_sound_;
    bool done_=false,debug_win_requested_=false;
    void finish_presented_sound(IGameFlow& flow) {
        if(pending_sound_end_ && presentation_.token()!=pending_sound_end_->frame) {
            flow.audio_cue(*pending_sound_end_);pending_sound_end_.reset();
        }
    }
    void reset_change(bool turn) {
        feedback_.clear();
        action_visuals_.clear();playing_action_=false;magic_caption_.clear();
        if(turn) {
            screen_.focus_active(*session_.combat());notices_=session_.combat()->round().notices;notice_=0;
            showing_notice_=!notices_.empty();
        } else {notices_.clear();notice_=0;showing_notice_=false;}
    }
    void changed(bool turn=false) {
        reset_change(turn);
        if(turn)append_selection();
        if(!action_visuals_.empty())start_action();
        else presentation_.start({{0,showing_notice_?RuntimeTiming::notification_dwell_ms:0}});
    }
    bool target(int delta) {
        if(targets_.empty())return false;
        target_index_=(target_index_+(delta<0?targets_.size()-1:1))%targets_.size();
        aim_target_=targets_[target_index_];screen_.focus_target(*session_.combat(),*aim_target_);return true;
    }
    void aim() {
        try {
            targets_=session_.combat()->targets(session_.world.party.item_catalog());
            if(targets_.empty()) {feedback_="No visible targets.";presentation_.start({{0,0}});return;}
            target_index_=0;
            aiming_=true;manual_aim_=false;friendly_question_=false;friendly_from_move_=false;
            aim_target_=targets_[target_index_];screen_.focus_target(*session_.combat(),*aim_target_);changed();
        }catch(const DecodeError& error) {feedback_=error.what();presentation_.start({{0,0}});}
    }
    void command(TacticalCommand value) {
        const auto* combat=session_.combat();
        if(session_.command_combat_actor(token_,combat->revision(),value)) {done_menu_=false;changed(true);}
        else {feedback_=session_.combat()->action_message();presentation_.start({{0,0}});}
    }
    void toggle_magic() {
        const auto* combat=session_.combat();
        if(session_.toggle_combat_magic(token_,combat->revision())) {
            changed(true);
        }else {feedback_=session_.combat()->action_message();presentation_.start({{0,0}});}
    }
    static unsigned direction(InputEvent event) {
        switch(event.key) {
        case Key::up:return 0;case Key::right:return 2;case Key::down:return 4;case Key::left:return 6;
        case Key::home:return 7;case Key::previous:return 1;case Key::end:return 5;case Key::next:return 3;default:break;
        }
        constexpr std::array<unsigned,9> keypad{5,4,3,6,8,2,7,0,1};
        return event.character>='1' && event.character<='9'?keypad[event.character-'1']:8;
    }
    void select_target() {
        if(!aim_target_ || !aim_target_->actor || !aim_target_->weapon_eligible || aim_target_->body) {
            feedback_=aim_target_ && aim_target_->adjacent_enemy_blocks_ranged?
                "Enemy adjacent: cannot fire.":"Cannot target with this weapon.";
            presentation_.start({{0,0}});return;
        }
        if(aim_target_->confirmation_required) {friendly_question_=true;changed();return;}
        const auto* combat=session_.combat();const auto active=combat->round().active;
        if(session_.attack_combat_actor(token_,combat->revision(),*aim_target_->actor)) {
            aiming_=false;acted(active!=session_.combat()->round().active);
        }else {feedback_=session_.combat()->action_message();presentation_.start({{0,0}});}
    }
    void start_action() {
        std::vector<CombatVisualStep> steps;
        for(std::size_t i=0;i<action_visuals_.size();++i)steps.push_back({static_cast<std::uint32_t>(2000+i),action_visuals_[i].duration});
        playing_action_=true;presentation_.start(std::move(steps));
    }
    void recipient_notice(CombatActorIdentity id,std::string text,std::shared_ptr<const DeploymentState> snapshot,
                          unsigned effect_slot=0,unsigned color=14) {
        const auto first=action_visuals_.size();
        const auto append=[&](std::uint64_t duration,std::optional<CombatRecipientEffect> effect={}) {
            action_visuals_.push_back({{},snapshot,id,text,0,duration,effect});
            action_visuals_.back().color=color;
        };
        if(effect_slot==24) {
            append(RuntimeTiming::notification_dwell_ms,CombatRecipientEffect{24,0});
            action_visuals_.back().sound_before=sounds_.command(NativeSoundCue::defeat);
            action_visuals_.back().sound_after=sounds_.stop_command();return;
        }
        if(effect_slot) {
            const auto cycles=effect_slot==22?RuntimeTiming::combat_effect_cycles:1u;
            for(unsigned cycle=0;cycle<cycles;++cycle)for(unsigned pose=0;pose<4;++pose)
                append(RuntimeTiming::combat_recipient_frame_ms,CombatRecipientEffect{effect_slot,pose});
            action_visuals_[first].sound_before=sounds_.command(effect_slot==22?NativeSoundCue::effect:NativeSoundCue::damage);
            action_visuals_.back().sound_after=sounds_.stop_command();
            // Source restores the background between frames and after the
            // sequence; retain the paired text for the remaining readable dwell.
            append(RuntimeTiming::notification_dwell_ms-cycles*4*RuntimeTiming::combat_recipient_frame_ms);
        }else append(RuntimeTiming::notification_dwell_ms);
    }
    void defeat_notice(CombatActorIdentity id,std::string text,const std::shared_ptr<const DeploymentState>& before,
                        const std::shared_ptr<const DeploymentState>& after) {
        // The source shows the new HP/status while the icon still owns its
        // old footprint; the body terrain is published only after the skull.
        auto scene=std::make_shared<DeploymentState>(*before);
        const auto old=std::find_if(scene->actors.begin(),scene->actors.end(),[&](const auto& a){return a.id==id;});
        const auto current=std::find_if(after->actors.begin(),after->actors.end(),[&](const auto& a){return a.id==id;});
        if(old==scene->actors.end() || current==after->actors.end())throw DecodeError("Defeat feedback lost its owned actor");
        old->character=current->character;
        recipient_notice(id,std::move(text),std::move(scene),24);
    }
    void append_weapon_travel(std::span<const CombatStrike::Travel> travels,
                              const std::shared_ptr<const DeploymentState>& snapshot,CombatActorIdentity attacker) {
        // Native1884 readies the attacker, then1A40:029C(100), before
        // either ammunition or the separate type2F weapon travel call.
        if(!travels.empty())action_visuals_.push_back({{},snapshot,attacker,{},1,100});
        auto camera=screen_.camera();
        for(const auto& travel:travels) {
            const auto shape=CombatWeaponArt::form(travel.item_type,travel.direction);
            const auto start=action_visuals_.size();
            for(const auto& frame:CombatWeaponArt::projectile(travel,camera)) {
                action_visuals_.push_back({{},snapshot,attacker,{},1,RuntimeTiming::combat_projectile_ms(static_cast<std::uint8_t>(shape.wait))});
                action_visuals_.back().weapon_projectile=frame;
                camera=frame.motion.camera;
            }
            if(action_visuals_.size()>start) {
                action_visuals_[start].sound_before=sounds_.command(NativeSoundCue::weapon_projectile);
                // Native00DC(DS262C) is immediately followed by01A5(10):
                // asynchronous start then device mute, with no owned wait.
                action_visuals_.back().sound_after=sounds_.stop_command();
            }
        }
    }
    void append_strikes(std::span<const CombatStrike> strikes) {
        const auto& battle=*session_.combat();const auto& actors=battle.deployment()->actors;
        const auto name=[&](CombatActorIdentity id) {
            const auto found=std::find_if(actors.begin(),actors.end(),[&](const auto& actor){return actor.id==id;});
            if(found==actors.end())throw DecodeError("Combat feedback lost an owned actor");
            return found->character.name();
        };
        if(!strikes.empty()) {
            // Selection may already have advanced, but the presented action
            // belongs to its attacker until all strike feedback has finished.
            for(std::size_t work=0;work<actors.size() && actors[screen_.focused_actor()].id!=strikes.front().attacker;++work)
                if(!screen_.cycle_actor(battle,1))break;
            for(std::size_t begin=0;begin<strikes.size();) {
                const auto& first=strikes[begin];std::size_t end=begin+1;
                while(end<strikes.size() && strikes[end].attacker==first.attacker && strikes[end].target==first.target)++end;
                append_weapon_travel(first.travel,first.before,first.attacker);
                if(first.preparation)append_magic_events(*first.preparation,first.attacker,first.preparation->events,first.before);
                recipient_notice(first.attacker,name(first.attacker)+" "+overlay_string(0x12a48)+" "+name(first.target),first.before);
                action_visuals_.back().pose=1;
                bool hit=false;
                for(auto at=begin;at<end;++at)if(const auto& strike=strikes[at];strike.hit) {
                    hit=true;
                    recipient_notice(first.attacker,overlay_string(0x12a73)+std::to_string(strike.damage)+
                        overlay_string(strike.damage==1?0x12a80:0x12a88)+overlay_string(0x12a91),strike.before);
                    action_visuals_.back().sound_before=sounds_.command(NativeSoundCue::weapon_hit);
                    action_visuals_.back().native_blocking=true;
                    action_visuals_.back().sound_after=sounds_.stop_command();
                    if(strike.interrupted_spell)recipient_notice(strike.target,name(strike.target)+" "+overlay_string(0x12aa6),strike.before);
                    if(strike.before_hp && !strike.after_hp)defeat_notice(strike.target,name(strike.target)+" "+
                        overlay_string(strike.status==6?0x12aea:strike.status==5?0x12abd:0x12ab3),strike.before,strike.after);
                    if(strike.followup)append_magic_events(*strike.followup,strike.attacker,strike.followup->events,strike.after);
                }
                if(!hit) {
                    recipient_notice(first.attacker,name(first.attacker)+" "+overlay_string(0x12a9b),first.after);
                    action_visuals_.back().sound_before=sounds_.command(NativeSoundCue::weapon_miss);
                    action_visuals_.back().native_blocking=true;
                    action_visuals_.back().sound_after=sounds_.stop_command();
                }
                begin=end;
            }
        }
    }
    void acted(bool turn=false,std::shared_ptr<const DeploymentState> movement_before={},
               std::optional<CombatActorIdentity> moving_actor={}) {
        reset_change(turn);
        if(const auto& travel=session_.combat()->standalone_travel())
            append_weapon_travel(travel->travel,travel->before,travel->attacker);
        const auto& strikes=session_.combat()->strikes();
        std::size_t split=strikes.size();bool moved=false;
        if(movement_before && moving_actor) {
            const auto position=[&](const DeploymentState& state)->std::optional<CombatPosition> {
                const auto found=std::find_if(state.actors.begin(),state.actors.end(),[&](const auto& value){return value.id==*moving_actor;});
                return found==state.actors.end()?std::nullopt:found->position;
            };
            const auto from=position(*movement_before),to=position(*session_.combat()->deployment());
            moved=from && to && from!=to;
            if(moved)for(std::size_t i=0;i<strikes.size();++i)
                if(strikes[i].before && position(*strikes[i].before)==to){split=i;break;}
        }
        append_strikes(std::span(strikes).first(split));
        if(moved) {
            // Departure reactions precede the paid step; arrival reactions
            // observe its new position and therefore follow this owned frame.
            const auto scene=split<strikes.size()?strikes[split].before:
                std::make_shared<const DeploymentState>(*session_.combat()->deployment());
            action_visuals_.push_back({{},scene,*moving_actor,{},0,RuntimeTiming::combat_step_ms});
            action_visuals_.back().sound_before=sounds_.command(NativeSoundCue::ecl_call8); // Same nativeDS2628 command10.
            action_visuals_.back().native_blocking=true;
            action_visuals_.back().sound_after=sounds_.stop_command();
        }
        append_strikes(std::span(strikes).subspan(split));
        append_monsters();append_selection();
        if(!action_visuals_.empty())start_action();
        else presentation_.start({{0,showing_notice_?RuntimeTiming::notification_dwell_ms:RuntimeTiming::combat_step_ms}});
    }
    const DeploymentActor& actor(CombatActorIdentity id) const {
        const auto& actors=session_.combat()->deployment()->actors;
        const auto found=std::find_if(actors.begin(),actors.end(),[&](const auto& a){return a.id==id;});
        if(found==actors.end())throw DecodeError("Combat magic UI lost an owned actor");
        return *found;
    }
    void focus_actor(CombatActorIdentity id) {
        const auto& a=actor(id);if(!a.position)return;
        CombatTarget target;target.actor=id;target.position=*a.position;screen_.focus_target(*session_.combat(),target);
    }
    void fail(std::string message) {feedback_=std::move(message);presentation_.start({{0,0}});}
    void open_magic_menu(bool items) {
        const auto& battle=*session_.combat();
        try {
            magic_menu_=items?CombatMagicMenu::items(actor(*battle.round().active).character):
                CombatMagicMenu::spells(battle.castable_spells(session_.vm.read_value(0x49e5)!=0),battle.spell_catalog());
            magic_menu_revision_=battle.revision();changed();
        }catch(const DecodeError& error){fail(error.what());}
    }
    void menu_input(InputEvent event) {
        const auto* battle=session_.combat();
        if(battle->revision()!=magic_menu_revision_ || battle->round().phase!=CombatRoundPhase::player) {
            magic_menu_.reset();fail("Combat magic selection is stale; reopen the list.");return;
        }
        const auto action=magic_menu_->input(event);
        if(action==CombatMagicMenuAction::leave) {
            if(magic_menu_->scroll_list())magic_menu_=CombatMagicMenu::items(actor(*battle->round().active).character);
            else magic_menu_.reset();
            changed();return;
        }
        if(action==CombatMagicMenuAction::none){changed();return;}
        const auto choice=magic_menu_->choice();bool applied=false;
        if(action==CombatMagicMenuAction::ready && choice.item) {
            applied=session_.ready_combat_item(token_,battle->revision(),*choice.item);
            if(applied) {
                const auto& playback=session_.combat()->monster_playback();
                if(playback && playback!=monsters_seen_) {magic_menu_.reset();acted(true);}
                else {
                    magic_menu_revision_=session_.combat()->revision();
                    magic_menu_=CombatMagicMenu::items(actor(*session_.combat()->round().active).character);changed();
                }
            }else fail(session_.combat()->action_message());
            return;
        }
        if(choice.spell_slot)applied=session_.schedule_combat_spell(token_,battle->revision(),*choice.spell_slot);
        else if(choice.item) {
            const auto& a=actor(*battle->round().active);
            const auto& inventory=a.character.inventory();const Item* selected=nullptr;
            for(std::size_t i=0;i<inventory.size();++i)if(inventory.item(i).id()==*choice.item)selected=&inventory.item(i);
            if(!selected){fail("Combat item selection is stale.");return;}
            if(session_.world.party.item_catalog().definition(selected->type()).scroll() && !choice.scroll_slot) {
                if(session_.read_combat_scroll(token_,battle->revision(),*choice.item)) {
                    const auto& current=*session_.combat();magic_menu_revision_=current.revision();
                    magic_menu_=CombatMagicMenu::scroll(actor(*current.round().active).character,*choice.item,current.spell_catalog());changed();
                }else fail(session_.combat()->action_message());
                return;
            }
            applied=session_.schedule_combat_item(token_,battle->revision(),*choice.item,choice.scroll_slot);
        }
        if(applied){magic_menu_.reset();changed(true);}else fail(session_.combat()->action_message());
    }
    void focus_magic() {
        CombatTarget target;target.position=magic_cursor_;
        if(magic_manual_)target.actor=cursor_actor();
        if(!magic_manual_ && !magic_targets_.empty()) {
            target.actor=magic_targets_[magic_target_index_];
            if(actor(*target.actor).position)magic_cursor_=target.position=*actor(*target.actor).position;
        }
        screen_.focus_target(*session_.combat(),target);
    }
    std::optional<CombatActorIdentity> cursor_actor() const {
        const auto& state=*session_.combat()->deployment();
        const auto slot=state.occupancy[static_cast<std::size_t>(magic_cursor_.y*50+magic_cursor_.x)];
        for(const auto& a:state.actors)if(!a.removed && slot && a.native_slot==slot)return a.id;
        for(const auto& body:state.bodies)if(body.position==magic_cursor_)return body.actor;
        return {};
    }
    template<class Payload>
    void append_magic_events(const Payload& playback,CombatActorIdentity caster,
            std::span<const CombatMagicEvent> events,std::shared_ptr<const DeploymentState> fallback,
            std::optional<std::uint8_t> cast_index={},bool known_damage_flags=true) {
        const auto final=std::make_shared<const DeploymentState>(*session_.combat()->deployment());
        const auto append=[&](CombatActorIdentity id,std::string text,std::shared_ptr<const DeploymentState> snapshot,unsigned slot=0,unsigned color=14) {
            recipient_notice(id,std::move(text),snapshot?snapshot:final,slot,color);
        };
        auto camera=screen_.camera();
        const auto project=[&](CombatPosition from,CombatPosition to,unsigned slot,bool outside,
                std::shared_ptr<const DeploymentState> before,std::uint8_t wait,unsigned frames) {
            for(const auto& frame:CombatMagicArt::projectile(from,to,camera,slot,outside,frames)) {
                action_visuals_.push_back({frame,before,caster,{},0,RuntimeTiming::combat_projectile_ms(wait)});
                camera=frame.camera;
            }
        };
        for(const auto& event:events) {
            if(event.kind==CombatMagicEventKind::projectile) {
                const auto& ray=playback.projectiles.at(event.index);
                const auto first=action_visuals_.size();
                project(ray.from,ray.to,ray.effect_slot,true,event.before?event.before:fallback,ray.source_wait,ray.frame_count);
                // Current slot18 event producers are4612:1658's subsequent
                // valid Hold targets (cast23/49): cmd2, travel, immediate mute.
                if(ray.effect_slot==18 && action_visuals_.size()>first) {
                    action_visuals_[first].sound_before=sounds_.command(NativeSoundCue::spell_projectile);
                    action_visuals_.back().sound_after=sounds_.stop_command();
                }
            }else if(event.kind==CombatMagicEventKind::feedback) {
                const auto& message=playback.feedback.at(event.index);
                append(message.actor,(message.include_name?actor(message.actor).character.name()+" ":std::string{})+StringStore::active().get(message.text_id)+message.suffix,event.after?event.after:event.before,
                       message.recipient_animation?22:0,cast_index?14u:message.color);
            }else if(event.kind==CombatMagicEventKind::state) {
                ActionVisual visual{{},event.after?event.after:event.before,caster,{},0,0};
                if(!action_visuals_.empty()) {
                    visual.notice=action_visuals_.back().notice;
                    visual.color=action_visuals_.back().color;
                    visual.focus=action_visuals_.back().focus;
                }
                action_visuals_.push_back(std::move(visual));
            }else {
                const auto& damage=playback.damage.at(event.index);
                //49D51344 recipient feedback uses its own extracted text,
                // rather than the weapon attacker's hit announcement. These
                // are the flags of the recovered direct damage handlers.
                const auto cast=cast_index.value_or(0);
                const auto suffix=cast_index?((cast==9 || cast==47 || cast==64)?0x2c1a4:
                                  (cast==20 || cast==51 || cast==60)?0x2c1b8:0x2c1d3):
                                  ((damage.flags&0xf7)==1?0x2c1a4:(damage.flags&0xf7)==2?0x2c1ae:
                                   (damage.flags&0xf7)==4?0x2c1b8:(damage.flags&0xf7)==0x10?0x2c1c9:0x2c1d3);
                if(event.kind==CombatMagicEventKind::terminal) {
                    defeat_notice(damage.target,action_visuals_.empty()?std::string{}:action_visuals_.back().notice,event.before,event.after);continue;
                }
                if(damage.amount)append(damage.target,actor(damage.target).character.name()+" "+
                    (damage.amount==1?overlay_string(0x2c18b):overlay_string(0x2c171)+std::to_string(damage.amount)+overlay_string(0x2c178))+
                    (known_damage_flags?overlay_string(suffix):std::string{}),event.before,23);
                if(damage.interrupted_spell)append(damage.target,actor(damage.target).character.name()+" "+overlay_string(0x2c1de),event.after);
                if(damage.before_hp && !damage.after_hp)defeat_notice(damage.target,actor(damage.target).character.name()+" "+
                    (damage.status==6?overlay_string(0x2c224):overlay_string(0x2c1eb)+
                        (damage.status==5?overlay_string(0x2c1f5):std::string{})),event.before,event.after);
            }
        }
    }
    void append_monsters() {
        const auto& pending=session_.combat()->monster_playback();
        if(!pending || pending==monsters_seen_)return;
        monsters_seen_=pending;
        for(const auto& ability:*pending)append_magic_events(ability.magic,ability.caster,ability.magic.events,
            ability.magic.casting_before,{},ability.effect_code==0x58 || ability.effect_code==0x84);
    }
    void append_selection() {
        const auto& round=session_.combat()->round();
        if(round.selection_sequence==selection_seen_)return;
        selection_seen_=round.selection_sequence;
        if(round.selection_playback.empty())return;
        // Prefix notices from the action precede selector notices. Preserve the
        // legacy notice view while consuming the selector's ordered stream once.
        auto trailing=notices_;
        for(const auto& step:round.selection_playback)if(step.notice) {
            const auto at=std::find(trailing.begin(),trailing.end(),*step.notice);
            if(at!=trailing.end())trailing.erase(at);
        }
        for(const auto& notice:trailing)recipient_notice(notice.actor,
            (notice.include_name?actor(notice.actor).character.name()+" ":std::string{})+StringStore::active().get(notice.text_id),
            std::make_shared<const DeploymentState>(*session_.combat()->deployment()));
        notices_.clear();notice_=0;showing_notice_=false;
        for(const auto& step:round.selection_playback) {
            if(step.notice)recipient_notice(step.notice->actor,
                (step.notice->include_name?actor(step.notice->actor).character.name()+" ":std::string{})+
                StringStore::active().get(step.notice->text_id),step.before);
            if(step.ability)for(const auto& event:step.ability->events) {
                if(event.kind==RetainedAbilityEventKind::strike)
                    append_strikes(std::span(step.ability->strikes).subspan(event.index,1));
                else {
                    const auto& magic=step.ability->magic;
                    append_magic_events(magic,magic.damage.empty()?step.ability->magic.feedback.front().actor:magic.damage.front().caster,
                        std::span(magic.events).subspan(event.index,1),step.before);
                }
            }
        }
    }
    void magic_finished() {
        magic_targeting_=false;magic_manual_=false;cancel_spell_question_=false;magic_policy_.reset();
        reset_change(true);
        const auto& playback=*session_.combat()->magic_playback();
        magic_caption_=overlay_string(0x2a32b);
        if(playback.cast_index<=56)magic_caption_+=session_.combat()->spell_catalog().definition(SpellId(playback.cast_index)).name();
        action_visuals_.push_back({{},playback.before,playback.caster,
            actor(playback.caster).character.name()+" "+overlay_string(0x2a31d),1,RuntimeTiming::combat_step_ms});
        auto camera=screen_.camera();
        const auto project=[&](CombatPosition from,CombatPosition to,unsigned slot,bool outside,std::shared_ptr<const DeploymentState> before,std::uint8_t wait) {
            const auto first=action_visuals_.size();
            for(const auto& frame:CombatMagicArt::projectile(from,to,camera,slot,outside)) {
                action_visuals_.push_back({frame,before,playback.caster,{},0,RuntimeTiming::combat_projectile_ms(wait)});
                camera=frame.camera;
            }
            //4612:0C20 selects47/51 separately; remaining common travel uses2.
            // Additional handler-specific ray sounds remain a separate boundary.
            if(slot==18 && first<action_visuals_.size()) {
                action_visuals_[first].sound_before=sounds_.command(playback.cast_index==47?NativeSoundCue::spell47:
                    playback.cast_index==51?NativeSoundCue::spell51:NativeSoundCue::spell_projectile);
                action_visuals_.back().sound_after=sounds_.stop_command();
            }
        };
        if(playback.origin && playback.destination)project(*playback.origin,*playback.destination,18,false,playback.before,30);
        append_magic_events(playback,playback.caster,playback.events,playback.before,playback.cast_index);
        append_selection();
        start_action();
    }
    void resolve_magic(std::optional<CombatPosition> centre={}) {
        const auto* battle=session_.combat();
        if(session_.resolve_combat_magic(token_,battle->revision(),magic_selected_,centre))magic_finished();
        else {
            // Failed staged effects keep the executor and resource intact.
            // Return to a usable target/cancel path rather than trapping the
            // selected maximum target behind the duplicate-selection guard.
            magic_selected_.clear();magic_targeting_=true;
            fail(session_.combat()->action_message());
        }
    }
    void begin_magic_targets() {
        const auto& battle=*session_.combat();
        try {
            const auto* pending=battle.pending_cast();if(!pending)throw DecodeError("Selected queued cast has no owned source continuation");
            magic_policy_=battle.magic_target_policy();magic_selected_.clear();magic_targets_=battle.magic_candidates();
            if(magic_policy_->mode==CombatMagicTargetMode::self || magic_policy_->mode==CombatMagicTargetMode::all_visible) {
                resolve_magic();return;
            }
            magic_targeting_=true;cancel_spell_question_=false;magic_target_index_=0;
            magic_manual_=magic_policy_->mode==CombatMagicTargetMode::area || magic_targets_.empty();
            magic_cursor_=actor(pending->caster).position.value();focus_magic();changed();
        }catch(const DecodeError& error){automatic_revision_=battle.revision();fail(error.what());}
    }
    void magic_target_input(InputEvent event,int letter) {
        const auto* battle=session_.combat();
        if(cancel_spell_question_) {
            if(letter=='N' || event.key==Key::cancel){cancel_spell_question_=false;changed();}
            else if(letter=='Y') {
                if(session_.cancel_combat_magic(token_,battle->revision())) {
                    magic_targeting_=false;cancel_spell_question_=false;magic_policy_.reset();changed(true);
                }else fail(session_.combat()->action_message());
            }
            return;
        }
        if(letter=='E' || event.key==Key::cancel) {
            if(magic_manual_ && magic_policy_->mode!=CombatMagicTargetMode::area && !magic_targets_.empty()) {
                magic_manual_=false;focus_magic();changed();return;
            }
            if(!magic_selected_.empty())resolve_magic();
            else {cancel_spell_question_=true;changed();}
            return;
        }
        if(letter=='C'){focus_magic();changed();return;}
        if(letter=='M'){magic_manual_=true;focus_magic();changed();return;}
        if(event.key==Key::accept || letter=='T') {
            if(magic_policy_->mode==CombatMagicTargetMode::area){resolve_magic(magic_cursor_);return;}
            auto selected=magic_manual_?cursor_actor():magic_targets_.empty()?std::optional<CombatActorIdentity>{}:magic_targets_[magic_target_index_];
            if(!selected) {
                if(magic_policy_->empty_tiles)resolve_magic(magic_cursor_);
                else fail("Select an occupied square or a tactical body.");
                return;
            }
            if(std::find(magic_selected_.begin(),magic_selected_.end(),*selected)!=magic_selected_.end()) {
                fail(overlay_string(0x148a8));return;
            }
            magic_selected_.push_back(*selected);
            if(magic_selected_.size()>=magic_policy_->maximum_targets)resolve_magic();
            else {if(!magic_targets_.empty())magic_target_index_=(magic_target_index_+1)%magic_targets_.size();focus_magic();changed();}
            return;
        }
        if(magic_manual_) {
            const auto d=direction(event);if(d<8){magic_cursor_=CombatTargetingService::move_cursor(magic_cursor_,battle->deployment()->battlefield.tables,d);focus_magic();changed();}
        }else if(!magic_targets_.empty()) {
            if(letter=='N' || event.key==Key::end || event.key==Key::down || event.key==Key::right)magic_target_index_=(magic_target_index_+1)%magic_targets_.size();
            else if(letter=='P' || event.key==Key::home || event.key==Key::up || event.key==Key::left)magic_target_index_=(magic_target_index_+magic_targets_.size()-1)%magic_targets_.size();
            else return;
            focus_magic();changed();
        }
    }
    void view(IGameFlow& flow,bool inventory) {
        const auto& combat=*session_.combat();const auto index=combat.active_index();if(!index)return;
        try {
            view_party_=std::make_unique<Party>(combat.inspection_party(session_.world.party));
            CharacterViewServices services;services.combat=true;services.execution_mode=5;
            services.read_only=true;services.initial_inventory=inventory;services.combat_abort=abort_;
            services.portrait_enabled=session_.character_portraits_enabled();
            if(combat.round().phase==CombatRoundPhase::player && !combat.moving()) {
                services.combat_ready=[this,actor=*combat.round().active,member=*view_party_->selected_index(),
                                      revision=combat.revision()](std::size_t requested,ItemId item) mutable -> ItemActionResult {
                    const auto* current=session_.combat();
                    if(abort_->requested || !current || current->entry().token!=token_ ||
                       current->revision()!=revision || current->round().active!=actor ||
                       current->round().phase!=CombatRoundPhase::player || requested!=member)
                        return {ItemActionStatus::rejected,"This combat inventory is no longer current.",item,{}};
                    if(!session_.ready_combat_item(token_,revision,item)) {
                        const auto& message=session_.combat()->action_message();
                        return {ItemActionStatus::rejected,message.empty()?"This item cannot be readied now.":message,item,{}};
                    }
                    current=session_.combat();
                    // Damaging Ready can finish selection. Keep this borrowed
                    // inspection copy alive only until its immediate close.
                    if(current->monster_playback() && current->monster_playback()!=monsters_seen_)
                        return {ItemActionStatus::applied,{},item,{}};
                    *view_party_=current->inspection_party(session_.world.party);
                    revision=current->revision();
                    return {ItemActionStatus::applied,{},item,{}};
                };
                services.combat_ready_feedback_pending=[this] {
                    const auto* current=session_.combat();
                    return current && current->monster_playback() && current->monster_playback()!=monsters_seen_;
                };
            }
            flow.push_mode(make_character_view_mode(*view_party_,*view_party_->selected_index(),font_,session_.asset_files(),std::move(services)));
        }catch(const DecodeError& error) {feedback_=error.what();presentation_.start({{0,0}});}
    }
public:
    CombatMode(ExplorationSession& session,const ExplorationScreen& parent)
        :session_(session),screen_(parent.font(),parent.chrome(),session.asset_files(),*session.combat(),session.world.party.item_catalog()),
         token_(session.combat()->entry().token),presentation_(token_.value),font_(parent.font()),chrome_(parent.chrome()),
         sounds_(NativeSoundCues::load(session.asset_files())) {presentation_.start({{0,0}});}
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed || done_)return;
        if(event.key==Key::debug_win && Debug::enabled()) {debug_win_requested_=true;return;}
        if(event.key==Key::combat_abort) {abort_->requested=true;return;}
        if(session_.combat()->aftermath()) {
            if(!abort_->requested && presentation_.complete() && !playing_action_ && !showing_notice_ &&
               (event.key!=Key::none || event.character) &&
               session_.acknowledge_combat_defeat(token_,session_.combat()->revision())) {done_=true;flow.pop_mode();}
            return;
        }
        if((event.character=='2' || event.key==Key::automatic_magic) &&
           session_.combat()->round().phase==CombatRoundPhase::ai_pending) {
            if(presentation_.complete() && !playing_action_ && !showing_notice_)toggle_magic();
            else magic_toggle_pending_=!magic_toggle_pending_;
            return;
        }
        if(event.character==' ' && session_.combat()->round().phase==CombatRoundPhase::ai_pending) {
            // Queue the source manual-control interrupt at the next safe
            // presented action boundary; never erase pending strike notices.
            if(presentation_.complete() && !playing_action_ && !showing_notice_) {
                manual_interrupt_=false;command(TacticalCommand::manual_control);
            }else manual_interrupt_=true;
            return;
        }
        if(abort_->requested || !presentation_.complete() || playing_action_ || showing_notice_)return;
        const auto letter=event.character>='a' && event.character<='z'?event.character-'a'+'A':event.character;
        const auto* combat=session_.combat();
        if(combat->continuation_pending()) {
            if(letter=='Y' || letter=='N') {
                const auto applied=session_.answer_combat_continue(token_,combat->revision(),letter=='Y');changed(applied);
            }
            return;
        }
        if(magic_menu_){menu_input(event);return;}
        if(magic_targeting_){magic_target_input(event,letter);return;}
        if(aiming_) {
            if(friendly_question_) {
                if(letter=='N' || event.key==Key::cancel) {
                    friendly_question_=false;
                    if(friendly_from_move_) {aiming_=false;friendly_from_move_=false;aim_target_.reset();screen_.focus_active(*combat);}
                    changed();
                }
                else if(letter=='Y') {
                    friendly_question_=false;const bool from_move=friendly_from_move_;friendly_from_move_=false;
                    const auto active=combat->round().active;
                    if(aim_target_ && aim_target_->actor && session_.attack_combat_actor(token_,combat->revision(),*aim_target_->actor,true)) {
                        aiming_=false;acted(active!=session_.combat()->round().active);
                    }else {
                        if(from_move){aiming_=false;aim_target_.reset();}
                        fail(session_.combat()->action_message());
                    }
                }
                return;
            }
            if(event.key==Key::cancel || letter=='E') {
                if(manual_aim_) {
                    targets_=combat->targets(session_.world.party.item_catalog());
                    manual_aim_=false;
                    if(!targets_.empty()) {
                        target_index_=std::min(target_index_,targets_.size()-1);
                        aim_target_=targets_[target_index_];screen_.focus_target(*combat,*aim_target_);changed();return;
                    }
                }
                aiming_=false;aim_target_.reset();screen_.focus_active(*combat);changed();return;
            }
            if(event.key==Key::accept || letter=='T') {select_target();return;}
            if(letter=='C') {screen_.focus_target(*combat,*aim_target_);changed();return;}
            if(letter=='M') {
                manual_aim_=true;
                const auto position=screen_.cursor().value_or(aim_target_->position);
                aim_target_=combat->probe_target(position,session_.world.party.item_catalog());
                screen_.focus_target(*combat,*aim_target_);changed();return;
            }
            if(manual_aim_) {
                const auto d=direction(event);
                if(d<8) {
                    const auto position=CombatTargetingService::move_cursor(aim_target_->position,combat->deployment()->battlefield.tables,d);
                    aim_target_=combat->probe_target(position,session_.world.party.item_catalog());
                    screen_.focus_target(*combat,*aim_target_);changed();
                }
            }else if(event.key==Key::home || event.key==Key::up || event.key==Key::left || letter=='P') {target(-1);changed();}
            else if(event.key==Key::end || event.key==Key::down || event.key==Key::right || letter=='N') {target(1);changed();}
            return;
        }
        if(combat->moving()) {
            if(flight_direction_) {
                if(letter=='Y') {
                    const auto applied=session_.flee_combat_actor(token_,combat->revision(),*flight_direction_);
                    flight_direction_.reset();changed(applied);
                }else if(letter=='N' || event.key==Key::cancel) {flight_direction_.reset();changed();}
                return;
            }
            if(event.key==Key::cancel || event.key==Key::accept) {
                session_.finish_combat_movement(token_,combat->revision(),event.key==Key::cancel);changed();return;
            }
            const auto direction=CombatMode::direction(event);
            if(direction<8) {
                try {
                    if(const auto target=combat->bump_target(direction,true);target && combat->round().active &&
                       actor(*target).character.byte(0x10e)==actor(*combat->round().active).character.byte(0x10e)) {
                        if(actor(*combat->round().active).character.byte(0x10f)!=0) {
                            const auto active=combat->round().active;
                            if(session_.attack_combat_actor(token_,combat->revision(),*target))acted(active!=session_.combat()->round().active);
                            else fail(session_.combat()->action_message());
                        }else if(session_.begin_combat_friendly_bump(token_,combat->revision(),direction)) {
                            CombatTarget proposal;proposal.actor=*target;proposal.position=*actor(*target).position;
                            proposal.distance=CombatSpatialService::range(*actor(*combat->round().active).position,proposal.position);
                            proposal.confirmation_required=true;aim_target_=proposal;
                            aiming_=true;manual_aim_=false;friendly_question_=true;friendly_from_move_=true;
                            screen_.focus_target(*session_.combat(),proposal);changed();
                        }
                        return;
                    }
                    if(combat->flight_prompt(direction) && session_.begin_combat_flight(token_,combat->revision(),direction)) {
                        flight_direction_=direction;changed();return;
                    }
                }catch(const DecodeError& error) {feedback_=error.what();presentation_.start({{0,0}});return;}
                const auto active=combat->round().active;
                const auto movement_before=std::make_shared<const DeploymentState>(*combat->deployment());
                const auto applied=session_.move_combat_actor(token_,combat->revision(),direction);combat=session_.combat();
                const auto index=combat->active_index();
                if(combat->moving() && index && combat->deployment()->actors[*index].extension[6]<2)
                    session_.finish_combat_movement(token_,combat->revision(),false);
                screen_.focus_active(*session_.combat());
                const bool turn=active!=session_.combat()->round().active || !session_.combat()->round().notices.empty();
                const auto& old_actors=movement_before->actors;const auto& new_actors=session_.combat()->deployment()->actors;
                const auto old_actor=std::find_if(old_actors.begin(),old_actors.end(),[&](const auto& value){return active && value.id==*active;});
                const auto new_actor=std::find_if(new_actors.begin(),new_actors.end(),[&](const auto& value){return active && value.id==*active;});
                const bool moved=old_actor!=old_actors.end() && new_actor!=new_actors.end() && old_actor->position &&
                    new_actor->position && old_actor->position!=new_actor->position;
                if(applied && (moved || !session_.combat()->strikes().empty() || session_.combat()->standalone_travel()))acted(turn,movement_before,active);
                else changed(turn);
            }
            return;
        }
        if(done_menu_) {
            if(event.key==Key::cancel || letter=='E') {done_menu_=false;changed();}
            else if(letter=='Q')command(TacticalCommand::quit);
            else if(letter=='G')command(TacticalCommand::guard);
            else if(letter=='D')command(TacticalCommand::delay);
            else if(letter=='B')command(TacticalCommand::bandage);
            else if(letter=='S') {feedback_="Fixed 30 Hz runtime; animations use elapsed time.";presentation_.start({{0,0}});}
            return;
        }
        if((letter=='V' || letter=='I') && combat->active_index()) {view(flow,letter=='I');return;}
        if(combat->runtime_error().empty() && combat->round().phase==CombatRoundPhase::player) {
            if(event.character=='2' || event.key==Key::automatic_magic) {toggle_magic();return;}
            if(letter=='M') {
                if(session_.begin_combat_movement(token_,combat->revision())) {screen_.focus_active(*session_.combat());changed();}
                return;
            }
            if(letter=='D') {done_menu_=true;changed();return;}
            if(letter=='A') {aim();return;}
            if(letter=='Q') {command(TacticalCommand::quick);return;}
            if(event.key==Key::automatic_party) {command(TacticalCommand::quick_all);return;}
            if(letter==' ') {command(TacticalCommand::manual_control);return;}
            if(letter=='T') {
                if(session_.turn_combat_undead(token_,combat->revision()))changed(true);
                else {feedback_=session_.combat()->action_message();presentation_.start({{0,0}});}
                return;
            }
            if(letter=='U' || letter=='C') {open_magic_menu(letter=='U');return;}
        }
        const auto& battle=*session_.combat();bool inspected=false;
        switch(event.key) {
        case Key::up:inspected=screen_.move_cursor(battle,0,-1);break;
        case Key::down:inspected=screen_.move_cursor(battle,0,1);break;
        case Key::left:inspected=screen_.move_cursor(battle,-1,0);break;
        case Key::right:inspected=screen_.move_cursor(battle,1,0);break;
        case Key::home:inspected=screen_.cycle_actor(battle,-1);break;
        case Key::end:inspected=screen_.cycle_actor(battle,1);break;
        default:break;
        }
        if(inspected)changed();
    }
    void update(std::uint64_t elapsed,IGameFlow& flow) override {
        presentation_.advance(elapsed);
        finish_presented_sound(flow);
        if(playing_action_) {
            if(presentation_.complete()) {
                playing_action_=false;screen_.focus_active(*session_.combat());magic_caption_.clear();
                presentation_.start({{0,showing_notice_?RuntimeTiming::notification_dwell_ms:0}});
            }
            return;
        }
        if(showing_notice_ && presentation_.complete()) {
            ++notice_;showing_notice_=notice_<notices_.size();
            presentation_.start({{0,showing_notice_?RuntimeTiming::notification_dwell_ms:0}});
            return;
        }
        if(done_ || abort_->requested || aiming_ || done_menu_ || magic_menu_ || magic_targeting_ || !presentation_.complete() || showing_notice_)return;
        const auto* battle=session_.combat();
        if(!battle->runtime_attempted() || !battle->runtime_error().empty())return;
        if(battle->aftermath())return;
        if(debug_win_requested_ && !battle->moving() && !battle->pending_cast()) {
            debug_win_requested_=false;
            if(Debug::Access::quick_win(session_,token_.value,battle->revision())) {automatic_revision_.reset();changed();return;}
        }
        if(manual_interrupt_) {
            manual_interrupt_=false;
            command(TacticalCommand::manual_control);return;
        }
        if(magic_toggle_pending_) {magic_toggle_pending_=false;toggle_magic();return;}
        if(automatic_revision_==battle->revision())return;
        if(battle->outcome_pending()) {
            finish_after_present_=true;
            presentation_.start({{0,0}});return;
        }
        if(battle->continuation_pending())return;
        const auto revision=battle->revision();const auto active=battle->round().active;
        if(battle->round().phase==CombatRoundPhase::casting_pending) {
            automatic_revision_=revision;begin_magic_targets();
        }else if(battle->round().phase==CombatRoundPhase::ai_pending) {
            automatic_revision_=revision;
            const auto movement_before=std::make_shared<const DeploymentState>(*battle->deployment());
            if(session_.step_combat_opponent(token_,revision)) {
                if(session_.combat()->magic_playback())magic_finished();
                else acted(active!=session_.combat()->round().active || !session_.combat()->round().notices.empty(),movement_before,active);
            }
            else changed();
        } else if(battle->round().phase==CombatRoundPhase::round_end_pending) {
            automatic_revision_=revision;
            const auto applied=session_.finish_combat_round(token_,revision);changed(applied);
        }
    }
    void render(Framebuffer& frame) const override {
        const auto* combat=session_.combat();
        if(!combat || combat->entry().token!=token_)throw DecodeError("Combat mode lost its encounter ownership");
        if(combat->aftermath()) {
            draw_game_over(frame,font_,chrome_);
            rendered_step_=presentation_.token();return;
        }
        if(magic_menu_) {
            magic_menu_->draw(frame,font_,chrome_,actor(*combat->round().active).character,combat->spell_catalog(),feedback_);
            rendered_step_=presentation_.token();return;
        }
        CombatUiState ui;ui.done_menu=done_menu_;ui.aiming=aiming_;ui.manual_aim=manual_aim_;
        ui.friendly_question=friendly_question_;ui.flight_question=flight_direction_.has_value();
        if(aim_target_) {ui.target_range=aim_target_->distance;ui.target_eligible=aim_target_->weapon_eligible;}
        ui.magic_targeting=magic_targeting_;ui.cancel_spell_question=cancel_spell_question_;
        if(magic_targeting_) {
            ui.manual_aim=magic_manual_;
            if(combat->round().active && actor(*combat->round().active).position)
                ui.target_range=CombatSpatialService::range(*actor(*combat->round().active).position,magic_cursor_);
        }
        ui.magic_caption=magic_caption_;
        if(playing_action_ && presentation_.current()) {
            const auto& visual=action_visuals_.at(presentation_.current()->frame-2000);
            ui.snapshot_override=visual.snapshot.get();ui.projectile=visual.projectile;ui.actor_override=visual.focus;
            ui.weapon_projectile=visual.weapon_projectile;
            ui.recipient_effect=visual.recipient_effect;ui.action_busy=true;ui.magic_busy=!magic_caption_.empty();
        }
        ui.notice=feedback_;ui.casting_forbidden=session_.vm.read_value(0x49e5)!=0;
        if(showing_notice_) {
            const auto& notice=notices_.at(notice_);
            const auto& actors=combat->deployment()->actors;
            const auto actor=std::find_if(actors.begin(),actors.end(),[&](const auto& value){return value.id==notice.actor;});
            if(actor==actors.end())throw DecodeError("Combat notice lost its actor identity");
            ui.notice=(notice.include_name?actor->character.name()+" ":std::string{})+StringStore::active().get(notice.text_id);
        }
        unsigned pose=0;
        if(playing_action_ && presentation_.current()) {
            const auto& visual=action_visuals_.at(presentation_.current()->frame-2000);ui.notice=visual.notice;ui.notice_color=visual.color;pose=visual.pose;
        }
        screen_.draw(frame,*combat,pose,ui);
        rendered_step_=presentation_.token();
    }
    void after_present(IGameFlow& flow) override {
        if(abort_->requested && !done_) {
            if(!session_.abort_combat(token_))throw DecodeError("Combat development abort could not restore its checkpoint");
            done_=true;flow.pop_mode();
        }
        else if(rendered_step_) {
            const auto frame=*rendered_step_;
            const auto* visual=playing_action_ && presentation_.current()?&action_visuals_.at(presentation_.current()->frame-2000):nullptr;
            const bool presented=presentation_.presented(frame);rendered_step_.reset();
            if(presented && visual) {
                if(visual->sound_before && started_sound_!=frame) {
                    flow.audio_cue({frame,*visual->sound_before,0,visual->native_blocking});started_sound_=frame;
                }
                if(visual->sound_after)pending_sound_end_=PresentationAudioCue{frame,*visual->sound_after,1};
                finish_presented_sound(flow);
            }
            if(session_.combat()->aftermath())session_.present_combat_aftermath(token_,session_.combat()->revision());
            if(finish_after_present_) {
                finish_after_present_=false;
                if(session_.finish_combat(token_,session_.combat()->revision())) {done_=true;flow.pop_mode();return;}
                automatic_revision_=session_.combat()->revision();changed();
            }
            if(screen_.ready() && presentation_.complete() && !session_.combat()->runtime_attempted()) {
                session_.start_combat_runtime(token_,session_.combat()->revision());changed(true);
            }
        }
    }
    void on_suspend() override {presentation_.pause();rendered_step_.reset();}
    void on_resume() override {
        presentation_.resume();rendered_step_.reset();
        if(session_.combat() && session_.combat()->monster_playback() && session_.combat()->monster_playback()!=monsters_seen_)acted(true);
    }
    std::string title() const override {return "PoolRad | Combat | F12 returns to exploration";}
};
}
std::unique_ptr<IGameMode> make_combat_mode(ExplorationSession& session,const ExplorationScreen& parent) {
    if(!session.combat())throw DecodeError("Combat mode requires an owned encounter and checkpoint");
    return std::make_unique<CombatMode>(session,parent);
}
}
