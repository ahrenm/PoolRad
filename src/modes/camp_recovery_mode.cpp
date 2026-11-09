#include "gbengine/modes/camp_recovery.hpp"
#include "gbengine/app/runtime_timing.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include "gbengine/magic/camp_work_service.hpp"
#include "gbengine/time/rest_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <sstream>

namespace GBEngine {
namespace {
const std::string& module_text(std::size_t offset) {
    return StringStore::active().get(pascal_string_id("START.EXE:module",offset));
}
std::uint8_t letter(InputEvent event) {
    auto key=event.character;
    if(key>='a' && key<='z')key=static_cast<std::uint8_t>(key-'a'+'A');
    return key;
}
void clear_feedback(Framebuffer& frame) {
    for(int y=136;y<184;++y)std::fill_n(frame.begin()+y*320+8,304,ega_ui_palette[0]);
    std::fill(frame.begin()+192*320,frame.end(),ega_ui_palette[0]);
}
void draw_row(const GameFont& font,Framebuffer& frame,std::string_view row,std::size_t selected,std::string_view prefix={}) {
    font.draw_text(frame,prefix,0,192,40,1,ega_ui_palette[14]);
    const auto origin=static_cast<int>(prefix.size()*8);
    font.draw_text(frame,row,origin,192,(320-origin)/8,1,ega_ui_palette[10]);
    for(std::size_t i=0;i<row.size();++i)if(row[i]>='A' && row[i]<='Z')
        font.draw_text(frame,row.substr(i,1),origin+static_cast<int>(8*i),192,1,1,ega_ui_palette[15]);
    std::size_t start=0,index=0;
    while(start<row.size()) {
        while(start<row.size() && row[start]==' ')++start;
        const auto end=row.find(' ',start);
        if(index==selected && start<row.size()) {
            const auto size=end==std::string_view::npos?row.size()-start:end-start;
            font.draw_text(frame,row.substr(start,size),origin+static_cast<int>(8*start),192,static_cast<int>(size),1,ega_ui_palette[15]);
            break;
        }
        if(end==std::string_view::npos)break;
        start=end+1;++index;
    }
}
class CampWorkMode final:public IGameMode {
    enum class Page {choices,initial_review,final_review,initial_question,final_question,message,leaving};
    struct Choice {SpellId spell;std::optional<ScribeChoice> scroll;};
    struct Row {std::optional<std::size_t> choice;std::uint8_t level;};
    ExplorationSession& session_;
    const ExplorationScreen& screen_;
    const PicturePresentation* backdrop_;
    CampWorkKind kind_;
    std::size_t member_,top_=0,selected_=0,focus_=0;
    std::uint64_t revision_;
    Page page_=Page::choices;
    std::vector<Choice> choices_;
    std::vector<Row> rows_;
    bool changed_=false,presented_=false;
    std::string diagnostic_;
    std::size_t visible() const {return page_==Page::choices && !scribe()?11u:18u;}
    bool scribe() const {return kind_==CampWorkKind::scribe;}
    bool review() const {return page_==Page::initial_review || page_==Page::final_review;}
    bool question() const {return page_==Page::initial_question || page_==Page::final_question;}
    bool stale() const {return session_.world.party.revision()!=revision_ || session_.active_character()!=member_;}
    std::size_t bottom() const {return std::min(top_+visible(),rows_.size());}
    void leave(IGameFlow& flow) {page_=Page::leaving;flow.pop_mode();}
    void message(std::string value) {diagnostic_=std::move(value);page_=Page::message;presented_=false;}
    void rebuild_rows() {
        rows_.clear();std::optional<std::uint8_t> previous;
        for(std::size_t i=0;i<choices_.size();++i) {
            const auto level=session_.spell_catalog().definition(choices_[i].spell).level();
            if(level!=previous){rows_.push_back({{},level});previous=level;}
            rows_.push_back({i,level});
        }
        top_=0;selected_=0;focus_=0;normalize(-1);
    }
    void normalize(int direction) {
        if(rows_.empty())return;
        const auto count=bottom()-top_;
        for(std::size_t i=0;i<count && !rows_[selected_].choice;++i)
            selected_=top_+(selected_-top_+(direction<0?count-1:1))%count;
    }
    void move(int direction) {
        if(rows_.empty())return;
        const auto count=bottom()-top_;
        selected_=top_+(selected_-top_+(direction<0?count-1:1))%count;normalize(direction);
    }
    void page(int direction) {
        if(rows_.empty())return;
        const auto row=selected_-top_;
        const auto count=visible();
        if(direction>0){if(top_+count>=rows_.size())return;top_=std::min(top_+count,rows_.size()-count);}
        else {if(!top_)return;top_=top_>count?top_-count:0;}
        selected_=std::min(top_+row,bottom()-1);normalize(direction);focus_=0;
    }
    void load_pending() {
        choices_.clear();diagnostic_.clear();
        if(scribe()) {
            const auto result=session_.scribe_choices(member_);
            if(result.status!=CampWorkStatus::applied){message(result.message);return;}
            for(const auto& entry:result.spells)if(entry.pending)choices_.push_back({entry.spell,entry});
        } else {
            for(const auto& entry:Spellbook::memorized(session_.world.party.member(member_)))
                if(entry.pending)choices_.push_back({entry.spell,{}});
        }
        rebuild_rows();
    }
    void load_choices() {
        page_=Page::choices;choices_.clear();diagnostic_.clear();
        if(scribe()) {
            const auto result=session_.scribe_choices(member_);
            if(result.status!=CampWorkStatus::applied){message(result.message);return;}
            for(const auto& entry:result.spells)if(!entry.pending)choices_.push_back({entry.spell,entry});
        } else {
            const auto result=session_.memorize_choices(member_);
            if(result.status!=CampWorkStatus::applied){message(result.message);return;}
            for(const auto id:result.spells)choices_.push_back({id,{}});
        }
        rebuild_rows();
        if(choices_.empty()) {
            if(changed_) {page_=Page::final_review;load_pending();presented_=false;}
            else message(session_.world.party.member(member_).name()+overlay_string(0x17d9f)+overlay_string(scribe()?0x18300:0x18108));
        }
    }
    void finish_choices(IGameFlow& flow) {
        if(!changed_){leave(flow);return;}
        page_=Page::final_review;load_pending();
        if(page_==Page::message)return;
        if(choices_.empty())leave(flow);else presented_=false;
    }
    void answer(bool keep,IGameFlow& flow) {
        const bool initial=page_==Page::initial_question;
        if(!keep) {
            const auto result=scribe()?session_.clear_scribe(member_):session_.clear_memorize(member_);
            if(result.status!=CampWorkStatus::applied){message(result.message);return;}
            revision_=session_.world.party.revision();
        }
        if(initial && !keep)load_choices();else leave(flow);
    }
    void schedule() {
        if(rows_.empty() || !rows_[selected_].choice)return;
        const auto choice=choices_.at(*rows_[selected_].choice);
        const auto result=scribe()?session_.schedule_scribe(member_,choice.scroll->item,choice.scroll->slot):session_.schedule_memorize(member_,choice.spell);
        if(result.status!=CampWorkStatus::applied){
            diagnostic_=result.message;
            if(scribe() && result.status==CampWorkStatus::rejected) {
                const auto& member=session_.world.party.member(member_);
                const auto& definition=session_.spell_catalog().definition(choice.spell);
                if(Spellbook::known(member,choice.spell))diagnostic_=overlay_string(0x18318);
                else if(!Spellbook::capacity(member,definition.school(),definition.level()))diagnostic_=overlay_string(0x18357);
                else {
                    const auto choices=session_.scribe_choices(member_);
                    if(std::any_of(choices.spells.begin(),choices.spells.end(),[&](const auto& entry){return entry.pending && entry.spell==choice.spell;}))diagnostic_=overlay_string(0x18334);
                }
            }
            return;
        }
        revision_=session_.world.party.revision();changed_=true;
        // Native known-spell selection stays open after each marked slot.
        if(scribe())load_choices();else diagnostic_.clear();
    }
    std::vector<std::pair<std::string,std::uint8_t>> commands() const {
        std::vector<std::pair<std::string,std::uint8_t>> result;
        if(page_==Page::choices)result.push_back({overlay_string(scribe()?0x27026:0x2701d),static_cast<std::uint8_t>(scribe()?'S':'M')});
        if(top_+visible()<rows_.size())result.push_back({overlay_string(0x2d657),'N'});
        if(top_)result.push_back({overlay_string(0x2d65d),'P'});
        result.push_back({overlay_string(0x2d663),'E'});return result;
    }
    void draw_commands(Framebuffer& frame) const {
        const auto entries=commands();int x=0;
        for(std::size_t i=0;i<entries.size();++i) {
            const auto& text=entries[i].first;
            screen_.font().draw_text(frame,text,x,192,(320-x)/8,1,ega_ui_palette[i==focus_%entries.size()?15:10]);
            for(std::size_t j=0;j<text.size();++j)if(text[j]>='A' && text[j]<='Z')
                screen_.font().draw_text(frame,std::string_view(text).substr(j,1),x+static_cast<int>(j*8),192,1,1,ega_ui_palette[15]);
            x+=static_cast<int>(8*text.size());
        }
    }
public:
    CampWorkMode(ExplorationSession& session,const ExplorationScreen& screen,const PicturePresentation* backdrop,CampWorkKind kind):
        session_(session),screen_(screen),backdrop_(backdrop),kind_(kind),member_(session.active_character().value_or(0)),revision_(session.world.party.revision()) {
        if(!session.camp_open() || !session.active_character())throw DecodeError("Camp spell work requires an active camp member");
        const auto eligibility=scribe()?static_cast<CampWorkResult>(session_.scribe_choices(member_)):static_cast<CampWorkResult>(session_.memorize_choices(member_));
        if(eligibility.status!=CampWorkStatus::applied) {
            const auto& member=session_.world.party.member(member_);
            if(!member.active() || member.status()==1)message(member.name()+overlay_string(0x17d9f)+overlay_string(0x17b53)+overlay_string(scribe()?0x17b8a:0x17b7a));
            else message(eligibility.message);
            return;
        }
        page_=Page::initial_review;load_pending();
        if(page_==Page::message)return;
        if(choices_.empty())load_choices();
    }
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed || page_==Page::leaving)return;
        const auto key=letter(event);const bool escape=event.key==Key::cancel || event.key==Key::backspace;
        if(page_==Page::message){if(presented_ && (event.key==Key::accept || escape || key=='E' || key==' '))leave(flow);return;}
        if(stale()){message("Camp spell work changed; leave and reopen the menu.");return;}
        if(question()) {
            if(key=='Y')answer(true,flow);else if(key=='N')answer(false,flow);
            else if(key==',' || key=='.' || event.key==Key::left || event.key==Key::right)focus_=1-focus_;
            return;
        }
        if(escape || key=='E') {
            if(review()){if(!presented_)return;page_=page_==Page::initial_review?Page::initial_question:Page::final_question;focus_=1;}
            else finish_choices(flow);
        } else if(event.key==Key::accept || key==(scribe()?'S':'M')) {
            if(review()){if(presented_){page_=page_==Page::initial_review?Page::initial_question:Page::final_question;focus_=1;}}
            else schedule();
        } else if(event.key==Key::home || event.key==Key::up)move(-1);
        else if(event.key==Key::end || event.key==Key::down)move(1);
        else if(key=='N' || event.key==Key::next)page(1);
        else if(key=='P' || event.key==Key::previous)page(-1);
        else if(key==',' || key=='.') {const auto count=commands().size();focus_=(focus_+(key==','?count-1:1))%count;}
    }
    void update(std::uint64_t,IGameFlow&) override {}
    void render(Framebuffer& frame) const override {
        const auto& font=screen_.font();
        if(page_==Page::message) {
            ExplorationUiState ui;ui.picture_override=backdrop_;ui.command_line=std::string{};screen_.draw(frame,session_,ui);clear_feedback(frame);
            font.draw_text(frame,diagnostic_,8,144,38,4,ega_ui_palette[12],true);return;
        }
        frame.fill(ega_ui_palette[0]);screen_.chrome().draw(frame,{{{1,1,38,1},{1,3,38,page_==Page::choices && !scribe()?15:22}}});
        const auto& member=session_.world.party.member(member_);
        const auto color=!member.active()?12u:member.byte(0x10e)==1?14u:11u;
        font.draw_text(frame,member.name()+module_text(0x23b3),8,8,38,1,ega_ui_palette[color]);
        const bool pending=review() || question();
        const auto heading=overlay_string(0x239ce)+overlay_string(pending?(scribe()?0x239bf:0x239af):(scribe()?0x23995:0x2397d));
        const int heading_x=8*static_cast<int>(member.name().size()+4);
        font.draw_text(frame,heading,heading_x,8,std::max(0,(312-heading_x)/8),1,ega_ui_palette[10]);
        for(auto i=top_;i<bottom();++i) {
            const auto y=40+8*static_cast<int>(i-top_);
            if(rows_[i].choice)font.draw_text(frame,overlay_string(0x272a2)+std::string(session_.spell_catalog().definition(choices_[*rows_[i].choice].spell).name()),8,y,38,1,ega_ui_palette[i==selected_?15:10]);
            else font.draw_text(frame,module_text(0xd239+41*rows_[i].level),8,y,38,1,ega_ui_palette[13]);
        }
        if(page_==Page::choices && !scribe()) {
            int y=160;
            for(std::uint8_t school=0;school<2;++school) {
                bool available=false;
                for(std::uint8_t level=1;level<=3;++level)available=available || Spellbook::capacity(member,school,level)!=0;
                if(!available)continue;
                font.draw_text(frame,overlay_string(school?0x17dc2:0x17daf),8,y,30,1,ega_ui_palette[10]);
                for(std::uint8_t level=1;level<=3;++level) {
                    const auto capacity=Spellbook::capacity(member,school,level);if(!capacity)continue;
                    unsigned occupied=0;
                    for(const auto& slot:Spellbook::memorized(member)) {
                        const auto& definition=session_.spell_catalog().definition(slot.spell);
                        if(definition.school()==school && definition.level()==level)++occupied;
                    }
                    const auto remaining=capacity>occupied?static_cast<unsigned>(capacity)-occupied:0;
                    auto value=std::to_string(remaining);if(value.size()<2)value.insert(value.begin(),' ');
                    font.draw_text(frame,value,160+24*static_cast<int>(level-1),y,2,1,ega_ui_palette[10]);
                }
                y+=8;
            }
        }
        if(question()) {
            draw_row(font,frame,overlay_string(0x2d979),focus_,overlay_string(page_==Page::initial_question?(scribe()?0x182ea:0x180f0):(scribe()?0x18376:0x18123)));
        } else {
            draw_commands(frame);
            if(!diagnostic_.empty()){clear_feedback(frame);font.draw_text(frame,diagnostic_,8,144,38,4,ega_ui_palette[12],true);}
        }
    }
    void after_present(IGameFlow&) override {presented_=true;}
    std::string title() const override {return std::string("PoolRad | camp ")+(scribe()?"scribe":"memorize")+(question()?" confirmation":review()?" pending review":"");}
};
class CampRestMode final:public IGameMode {
    enum class Page {editor,running,feedback,stop_question,boundary,leaving};
    struct Notice {std::optional<CampWorkNotice> work;std::size_t text;std::optional<EffectNotice> effect={};};
    ExplorationSession& session_;
    const ExplorationScreen& screen_;
    const PicturePresentation* backdrop_;
    Page page_=Page::editor,resume_=Page::running;
    std::uint32_t duration_{};
    unsigned unit_=0;
    std::size_t focus_=0,notice_=0;
    std::vector<Notice> notices_;
    RestOutcome outcome_=RestOutcome::running;
    std::string diagnostic_;
    bool presented_=false,stop_requested_=false;
    std::uint64_t elapsed_=0;
    void leave(IGameFlow& flow) {page_=Page::leaving;flow.pop_mode();}
    void boundary(std::string message) {diagnostic_=std::move(message);page_=Page::boundary;presented_=false;}
    void start() {
        const auto result=session_.begin_rest(duration_);
        if(result.status!=CampWorkStatus::applied){boundary(result.message);return;}
        page_=Page::running;presented_=false;
    }
    std::uint32_t remaining() const {
        if(page_==Page::editor)return duration_;
        const auto* state=session_.rest_state();return state?state->remaining_minutes:duration_;
    }
    void tick(IGameFlow& flow) {
        const auto result=session_.rest_tick();
        if(result.status!=CampWorkStatus::applied){boundary(result.message);return;}
        outcome_=result.outcome;notices_.clear();notice_=0;elapsed_=0;
        for(const auto& effect:result.effect_notices)notices_.push_back({{},0,effect});
        if(result.daily_recovery)notices_.push_back({{},0x24623});
        for(const auto& work:result.work_notices)notices_.push_back({work,work.scribed?0x247a7u:0x246e3u});
        if(result.outcome==RestOutcome::interrupted)notices_.push_back({{},0x24a45});
        presented_=false;
        if(!notices_.empty())page_=Page::feedback;
        else if(outcome_!=RestOutcome::running)leave(flow);
    }
    void finish_notice(IGameFlow& flow) {
        elapsed_=0;presented_=false;
        if(++notice_<notices_.size())return;
        session_.rest_notice_presented();
        if(outcome_!=RestOutcome::running){leave(flow);return;}
        if(stop_requested_){stop_requested_=false;resume_=Page::running;page_=Page::stop_question;focus_=1;}
        else page_=Page::running;
    }
    void stop(bool yes,IGameFlow& flow) {
        if(!yes){page_=resume_;presented_=false;return;}
        session_.stop_rest();leave(flow);
    }
    void draw_time(Framebuffer& frame) const {
        const auto& font=screen_.font();const auto minutes=remaining();
        font.draw_text(frame,overlay_string(0x243af),8,136,38,1,ega_ui_palette[10]);
        const auto digits=[](std::uint32_t value){auto result=std::to_string(value);if(result.size()<2)result.insert(result.begin(),'0');return result;};
        const std::array<std::uint32_t,3> values{minutes%60,(minutes/60)%24,minutes/(24*60)};
        for(unsigned field=0;field<3;++field) {
            const auto x=144-24*static_cast<int>(field);
            font.draw_text(frame,digits(values[field]),x,136,2,1,ega_ui_palette[page_==Page::editor && unit_==field?15:10]);
            if(field)font.draw_text(frame,overlay_string(0x243ba),x+16,136,1,1,ega_ui_palette[10]);
        }
    }
public:
    CampRestMode(ExplorationSession& session,const ExplorationScreen& screen,const PicturePresentation* backdrop):session_(session),screen_(screen),backdrop_(backdrop) {
        if(!session.camp_open())throw DecodeError("Rest requires an open camp");
        const auto result=session_.prepare_rest();
        if(result.status!=CampWorkStatus::applied)boundary(result.message);else duration_=result.minutes;
    }
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed || page_==Page::leaving)return;
        const auto key=letter(event);const bool escape=event.key==Key::cancel || event.key==Key::backspace;
        if(page_==Page::boundary){if(presented_ && (escape || event.key==Key::accept || key=='E')){session_.stop_rest();leave(flow);}return;}
        if(page_==Page::stop_question) {
            if(key=='Y')stop(true,flow);else if(key=='N')stop(false,flow);
            else if(key==',' || key=='.' || event.key==Key::left || event.key==Key::right)focus_=1-focus_;
            return;
        }
        if(page_==Page::running || page_==Page::feedback) {
            // Keyboard input requests a decision before the next five-minute tick.
            // A committed encounter is displayed and cannot be canceled or rerolled.
            if(outcome_==RestOutcome::interrupted)return;
            if(page_==Page::feedback){stop_requested_=true;return;}
            resume_=page_;page_=Page::stop_question;focus_=1;presented_=false;return;
        }
        if(escape || key=='E'){leave(flow);return;}
        if(event.key==Key::accept || key=='R'){start();return;}
        if(key=='M')unit_=0;else if(key=='H')unit_=1;else if(key=='Y')unit_=2;
        else if(event.key==Key::left)unit_=(unit_+1)%3;
        else if(event.key==Key::right)unit_=(unit_+2)%3;
        else if(key=='I' || key=='D' || event.key==Key::up || event.key==Key::down) {
            const std::uint32_t amount=unit_==0?5u:unit_==1?60u:1440u;
            const bool increase=key=='I' || event.key==Key::up;
            if(increase) {
                const auto next=duration_+amount;
                // Native normalization caps only the day field. A carry out of
                // 99d23:55 therefore produces99d00:00, retaining the lower wrap.
                duration_=std::min(next/1440u,99u)*1440u+next%1440u;
            }
            else duration_=duration_>amount?duration_-amount:0;
        }
    }
    void update(std::uint64_t elapsed,IGameFlow& flow) override {
        if(!presented_)return;
        if(page_==Page::running){
            const auto* state=session_.rest_state();
            if(state && !state->running && !state->remaining_minutes){leave(flow);return;}
            tick(flow);return;
        }
        if(page_!=Page::feedback)return;
        // Native feedback uses the saved speed wait. Even speed zero advances
        // at most one already-presented notice per update, never a batch.
        const auto duration=RuntimeTiming::notification_dwell_ms;
        elapsed_+=std::min(elapsed,duration-std::min(duration,elapsed_));
        if(elapsed_>=duration)finish_notice(flow);
    }
    void render(Framebuffer& frame) const override {
        ExplorationUiState ui;ui.picture_override=backdrop_;ui.command_line=std::string{};
        if(page_==Page::feedback && notices_[notice_].work)ui.highlighted_member=notices_[notice_].work->member;
        screen_.draw(frame,session_,ui);clear_feedback(frame);draw_time(frame);
        const auto& font=screen_.font();
        if(page_==Page::editor)draw_row(font,frame,overlay_string(0x244a5),0);
        else if(page_==Page::stop_question) {
            draw_row(font,frame,overlay_string(0x2d979),focus_,overlay_string(0x24a36));
        } else if(page_==Page::boundary)font.draw_text(frame,diagnostic_,8,152,38,4,ega_ui_palette[12],true);
        else if(page_==Page::feedback) {
            const auto& current=notices_.at(notice_);
            if(current.effect) {
                const auto& member=session_.world.party.member(current.effect->member);
                font.draw_text(frame,member.name(),8,152,38,1,ega_ui_palette[member.active()?11:12]);
                font.draw_text(frame,StringStore::active().get(current.effect->text_id),8,160,38,3,ega_ui_palette[10],true);
            } else if(current.work) {
                const auto& member=session_.world.party.member(current.work->member);
                const auto color=!member.active()?12u:member.byte(0x10e)==1?14u:11u;
                font.draw_text(frame,member.name(),8,152,38,1,ega_ui_palette[color]);
                const auto x=8*static_cast<int>(member.name().size()+2);
                font.draw_text(frame,overlay_string(current.text),x,152,std::max(0,(312-x)/8),1,ega_ui_palette[10]);
                font.draw_text(frame,session_.spell_catalog().definition(current.work->spell).name(),8,160,38,2,ega_ui_palette[10],true);
            } else font.draw_text(frame,overlay_string(current.text),8,152,38,3,ega_ui_palette[10],true);
        }
    }
    void after_present(IGameFlow&) override {presented_=true;}
    std::string title() const override {
        switch(page_) {
        case Page::editor:return "PoolRad | camp rest duration";
        case Page::running:return "PoolRad | camp resting";
        case Page::feedback:return "PoolRad | camp rest feedback "+std::to_string(notice_+1)+"/"+std::to_string(notices_.size());
        case Page::stop_question:return "PoolRad | camp stop resting";
        case Page::boundary:return "PoolRad | camp rest unavailable";
        default:return "PoolRad | camp rest";
        }
    }
};
}
std::unique_ptr<IGameMode> make_camp_work_mode(ExplorationSession& session,const ExplorationScreen& screen,const PicturePresentation* backdrop,CampWorkKind kind) {
    return std::make_unique<CampWorkMode>(session,screen,backdrop,kind);
}
std::unique_ptr<IGameMode> make_camp_rest_mode(ExplorationSession& session,const ExplorationScreen& screen,const PicturePresentation* backdrop) {
    return std::make_unique<CampRestMode>(session,screen,backdrop);
}
}
