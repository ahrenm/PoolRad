#include "gbengine/qol/fix.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
bool caster(const Character& c) {return c.active() && c.status()==0 && Spellbook::known(c,SpellId(3)) && Spellbook::capacity(c,0,1)>0;}
bool wounded(const Character& c) {return c.hit_points()<c.byte(0x32);}
bool healable(const Character& c) {
    const auto status=c.status();return wounded(c) && (status==0 || status==1 || status==4 || status==5) &&
        std::none_of(c.effects().begin(),c.effects().end(),[](const auto& e){return e.code()==0x32;});
}
}
FixLoadout::FixLoadout(const Party& p) {
    for(std::size_t i=0;i<p.size();++i){std::array<std::uint8_t,21> row{};for(std::size_t j=0;j<21;++j)row[j]=p.member(i).byte(0x17+j);original_.push_back(row);}
}
bool FixLoadout::prepare_healing(Party& party,const SpellCatalog& catalog) {
    auto next=party;bool available=false,changed=false;
    for(std::size_t i=0;i<next.size();++i) {
        auto& c=next.members_[i];if(!caster(c))continue;available=true;
        for(std::size_t slot=0;slot<21;++slot)if(const auto raw=c.byte(0x17+slot)) {
            const auto& spell=catalog.definition(SpellId(static_cast<std::uint8_t>(raw&127)));
            if(spell.school()==0 && spell.level()==1 && (raw&127)!=3){c.set_byte(0x17+slot,0);changed=true;}
        }
        const auto capacity=Spellbook::capacity(c,0,1);
        unsigned count=0;for(const auto& spell:Spellbook::memorized(c))if(spell.spell.value==3)++count;
        if(count>capacity)throw DecodeError("Healing spell capacity is inconsistent.");
        for(;count<capacity;++count){const auto result=CampWorkService::schedule_memorize(next,i,catalog,SpellId(3));if(result.status!=CampWorkStatus::applied)throw DecodeError(result.message);changed=true;}
    }
    if(changed){++next.revision_;party=std::move(next);}return available;
}
void FixLoadout::restore(Party& party,const SpellCatalog& catalog) {
    if(party.size()!=original_.size())throw DecodeError("Party changed during Fix.");
    auto next=party;bool changed=false;
    for(std::size_t i=0;i<next.size();++i) {
        auto& c=next.members_[i];std::array<unsigned,128> ready{};
        for(const auto& spell:Spellbook::memorized(c))if(!spell.pending)++ready[spell.spell.value];
        for(std::size_t slot=0;slot<21;++slot) {
            const auto id=static_cast<std::uint8_t>(original_[i][slot]&127);std::uint8_t raw=0;
            if(id){(void)catalog.definition(SpellId(id));raw=id;if(ready[id])--ready[id];else raw|=128;}
            if(c.byte(0x17+slot)!=raw){c.set_byte(0x17+slot,raw);changed=true;}
        }
        if(changed)c.set_byte(0x2c,0);
    }
    if(changed){++next.revision_;party=std::move(next);}
}
CampFix::CampFix(ExplorationSession& session):session_(session),loadout_(session.world.party),revision_(session.world.party.revision()) {
    if(!session.camp_open() || !session.can_act())throw DecodeError("Fix requires an idle camp.");
}
bool CampFix::pending() const {
    for(std::size_t i=0;i<session_.world.party.size();++i)for(const auto& spell:Spellbook::memorized(session_.world.party.member(i)))if(spell.pending)return true;
    return false;
}
void CampFix::restore(){if(installed_){loadout_.restore(session_.world.party,session_.spell_catalog());installed_=false;}revision_=session_.world.party.revision();}
void CampFix::finish(FixPhase phase,std::string message) {
    session_.stop_rest();
    if(session_.world.party.revision()==revision_)restore();
    phase_=phase;message_=std::move(message);
}
void CampFix::cancel() {
    if(terminal())return;
    if(phase_==FixPhase::feedback){stop_requested_=true;return;}
    finish(FixPhase::stopped,"Fix stopped. Spent spells and elapsed time are retained.");
}
void CampFix::begin_rest() {
    const auto plan=session_.prepare_rest();revision_=session_.world.party.revision();
    if(plan.status!=CampWorkStatus::applied)throw DecodeError(plan.message);
    if(!plan.minutes)throw DecodeError("Fix cannot make further progress.");
    if(elapsed_minutes_+plan.minutes>RestService::maximum_minutes)throw DecodeError("Fix reached its rest-time limit.");
    const auto result=session_.begin_rest(plan.minutes);if(result.status!=CampWorkStatus::applied)throw DecodeError(result.message);
    phase_=FixPhase::resting;message_=restoring_?"Restoring original spells.":"Resting to memorise healing spells.";
}
void CampFix::advance() {
    if(terminal() || phase_==FixPhase::feedback)return;
    if(session_.world.party.revision()!=revision_){phase_=FixPhase::blocked;message_="Party changed during Fix. Automation stopped.";session_.stop_rest();return;}
    try {
        if(phase_==FixPhase::resting) {
            const auto result=session_.rest_tick();revision_=session_.world.party.revision();
            if(result.status!=CampWorkStatus::applied)throw DecodeError(result.message);
            elapsed_minutes_+=5;notices_.clear();
            for(const auto& effect:result.effect_notices)notices_.push_back(session_.world.party.member(effect.member).name()+": "+StringStore::active().get(effect.text_id));
            for(const auto& work:result.work_notices)notices_.push_back(session_.world.party.member(work.member).name()+": "+std::string(session_.spell_catalog().definition(work.spell).name())+" memorised.");
            if(result.daily_recovery)notices_.push_back("A day of rest has passed.");
            if(result.outcome==RestOutcome::interrupted)notices_.push_back("Rest interrupted!");
            resume_=result.outcome==RestOutcome::interrupted?FixPhase::interrupted:result.outcome==RestOutcome::completed?(restoring_?FixPhase::restoring:FixPhase::healing):FixPhase::resting;
            if(!notices_.empty()){rest_notice_=true;phase_=FixPhase::feedback;}else phase_=resume_;
            return;
        }
        if(phase_==FixPhase::restoring) {
            if(pending()){begin_rest();return;}
            bool remaining=false,can_heal=false;for(std::size_t i=0;i<session_.world.party.size();++i){remaining=remaining||wounded(session_.world.party.member(i));can_heal=can_heal||healable(session_.world.party.member(i));}
            if(can_heal){restoring_=false;phase_=FixPhase::healing;return;}
            finish(remaining?FixPhase::blocked:FixPhase::complete,remaining?"Original spells restored. Remaining wounds need other treatment.":"Party healed. Original spells memorised.");return;
        }
        auto& party=session_.world.party;std::optional<std::size_t> target;
        for(std::size_t i=0;i<party.size();++i)if(healable(party.member(i))){target=i;break;}
        if(!target){restore();restoring_=true;phase_=FixPhase::restoring;return;}
        if(!installed_) {
            if(session_.vm.read_value(0x49e5)!=0){finish(FixPhase::blocked,"Casting is blocked here.");return;}
            if(!loadout_.prepare_healing(party,session_.spell_catalog())){finish(FixPhase::blocked,"No usable cleric healing spells are available.");return;}
            installed_=true;revision_=party.revision();return;
        }
        for(std::size_t i=0;i<party.size();++i)if(caster(party.member(i))) {
            const auto entries=Spellbook::memorized(party.member(i));
            const auto spell=std::find_if(entries.begin(),entries.end(),[](const auto& entry){return entry.spell.value==3 && !entry.pending;});
            if(spell==entries.end())continue;
            const auto selected=party.selected_index();const auto hp=party.member(*target).hit_points();
            party.select(i);MagicResult result;
            try {result=session_.cast_spell(session_.prepare_spell(spell->slot,{MagicTargetKind::party_member,*target}));}
            catch(...){if(selected)party.select(*selected);throw;}
            if(selected)party.select(*selected);
            revision_=party.revision();
            if(!result.applied())throw DecodeError(result.message);
            if(party.member(*target).hit_points()<=hp)throw DecodeError("Healing made no progress; Fix stopped.");
            // Requeue exactly the spell that was consumed, through ordinary camp work.
            const auto queue=session_.schedule_memorize(i,SpellId(3));revision_=party.revision();if(queue.status!=CampWorkStatus::applied)throw DecodeError(queue.message);
            notices_.clear();for(const auto& feedback:result.feedback)notices_.push_back(party.member(feedback.recipient).name()+": "+StringStore::active().get(feedback.text_id));
            if(notices_.empty())notices_.push_back(party.member(*target).name()+" healed.");
            rest_notice_=false;resume_=FixPhase::healing;phase_=FixPhase::feedback;return;
        }
        begin_rest();
    } catch(const std::exception& error){revision_=session_.world.party.revision();finish(FixPhase::blocked,error.what());}
}
void CampFix::acknowledge() {
    if(phase_!=FixPhase::feedback)return;
    if(rest_notice_)session_.rest_notice_presented();
    rest_notice_=false;notices_.clear();
    if(resume_==FixPhase::interrupted){finish(FixPhase::interrupted,"Fix interrupted by an encounter.");return;}
    phase_=resume_;
    if(stop_requested_){stop_requested_=false;cancel();}
}
}
