#include "gbengine/modes/modes.hpp"
#include "gbengine/app/runtime_timing.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
const std::string& source_text(std::string_view source,std::size_t offset) {
    return StringStore::active().get(pascal_string_id(source,offset));
}
const std::string& module_text(std::size_t offset) {return source_text("START.EXE:module",offset);}
class SpellMode final : public IGameMode {
    enum class Page {list,target,feedback,empty,leaving};
    ExplorationSession& session_;
    const ExplorationScreen& screen_;
    const GameFont& font_;
    const PicturePresentation* backdrop_;
    std::size_t caster_,selected_=0,target_;
    std::uint64_t revision_;
    std::vector<MemorizedSpell> spells_;
    Page page_=Page::list;
    bool exploration_backdrop_=false,target_menu_=false,presented_=false;
    std::vector<MagicFeedback> messages_;
    std::size_t message_=0;
    std::size_t command_=0;
    std::uint64_t elapsed_=0;
    std::string diagnostic_;
    bool stale() const {return session_.active_character()!=caster_ || session_.world.party.revision()!=revision_;}
    void leave(IGameFlow& flow) {if(page_!=Page::leaving) {page_=Page::leaving;flow.pop_mode();}}
    void rebuild() {
        revision_=session_.world.party.revision();command_=0;diagnostic_.clear();
        auto remaining=session_.castable_spells();
        // The native empty builder returns without redrawing. Preserve either
        // the exploration backdrop or the previous spell rows above feedback.
        if(remaining.empty()) page_=Page::empty;
        else {spells_=std::move(remaining);selected_=0;page_=Page::list;exploration_backdrop_=false;target_menu_=false;}
        presented_=false;elapsed_=0;
    }
    void cast(MagicTarget target) {
        try {
            if(stale()) throw DecodeError("Spell list changed; leave and reopen casting");
            const auto plan=session_.prepare_spell(spells_.at(selected_).slot,target);
            if(plan.spell!=spells_[selected_].spell) throw DecodeError("Selected spell changed since the list opened");
            auto result=session_.cast_spell(plan);
            if(!result.applied()) {diagnostic_=result.message;return;}
            // Presentation never invokes the committed spell service again.
            revision_=session_.world.party.revision();
            messages_=std::move(result.feedback);message_=0;elapsed_=0;presented_=false;
            if(messages_.empty()) rebuild();else page_=Page::feedback;
        } catch(const DecodeError& error) {diagnostic_=error.what();}
    }
    void choose() {
        const auto mode=session_.spell_catalog().definition(spells_[selected_].spell).noncombat_target_mode();
        diagnostic_.clear();
        if(mode==1) {exploration_backdrop_=false;cast({MagicTargetKind::party_member,caster_});}
        else if(mode==2) {page_=Page::target;command_=0;exploration_backdrop_=true;target_menu_=true;}
        else if(mode==4) {exploration_backdrop_=false;cast({MagicTargetKind::party});}
        else diagnostic_="This spell needs an unavailable combat service.";
    }
    struct Row {std::optional<std::size_t> spell;std::uint8_t level;};
    std::vector<Row> rows() const {
        std::vector<Row> result;std::optional<std::uint8_t> previous;
        for(std::size_t i=0;i<spells_.size();++i) {
            const auto level=session_.spell_catalog().definition(spells_[i].spell).level();
            if(previous!=level) {result.push_back({{},level});previous=level;}
            result.push_back({i,level});
        }
        return result;
    }
    std::size_t first_row(const std::vector<Row>& entries) const {
        const auto at=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.spell==selected_;});
        return (static_cast<std::size_t>(at-entries.begin())/18)*18;
    }
    void page(int direction) {
        const auto entries=rows();const auto first=first_row(entries);
        if((direction<0 && !first) || (direction>0 && first+18>=entries.size())) return;
        const auto start=direction<0?first-18:first+18;
        for(auto i=start;i<entries.size() && i<start+18;++i) if(entries[i].spell) {selected_=*entries[i].spell;command_=0;return;}
    }
    struct Command {std::string text;std::uint8_t key;};
    std::vector<Command> commands() const {
        if(target_menu_ && exploration_backdrop_) return {{module_text(0x37e1),'S'},{module_text(0x37b9),'E'}};
        std::vector<Command> result{{overlay_string(0x27018),'C'}};
        const auto entries=rows();const auto first=first_row(entries);
        if(first+18<entries.size()) result.push_back({overlay_string(0x2d657),'N'});
        if(first) result.push_back({overlay_string(0x2d65d),'P'});
        result.push_back({overlay_string(0x2d663),'E'});return result;
    }
    void draw_commands(Framebuffer& frame,std::string_view prefix) const {
        font_.draw_text(frame,prefix,0,192,40,1,ega_ui_palette[13]);
        int x=static_cast<int>(prefix.size())*8;const auto entries=commands();
        for(std::size_t i=0;i<entries.size();++i) {
            font_.draw_text(frame,entries[i].text,x,192,(320-x)/8,1,ega_ui_palette[i==command_%entries.size()?15:10]);
            if(i!=command_%entries.size()) for(std::size_t j=0;j<entries[i].text.size();++j) {
                const auto c=entries[i].text[j];
                if((c>='A' && c<='Z') || (c>='0' && c<='9'))
                    font_.draw_text(frame,std::string_view(entries[i].text).substr(j,1),x+8*static_cast<int>(j),192,1,1,ega_ui_palette[15]);
            }
            x+=static_cast<int>(entries[i].text.size())*8;
        }
    }
    void list(Framebuffer& frame) const {
        frame.fill(ega_ui_palette[0]);
        screen_.chrome().draw(frame,{{{1,1,38,1},{1,3,38,22}}});
        const auto& member=session_.world.party.member(caster_);
        const auto color=!member.active()?12u:member.byte(0x10e)==1?14u:11u;
        font_.draw_text(frame,member.name()+module_text(0x23b3),8,8,38,1,ega_ui_palette[color]);
        const auto heading=overlay_string(0x239ce)+overlay_string(0x23973);
        const auto heading_x=8*static_cast<int>(member.name().size()+4);
        font_.draw_text(frame,heading,heading_x,8,(312-heading_x)/8,1,ega_ui_palette[10]);
        const auto entries=rows();const auto first=first_row(entries);
        for(std::size_t i=first;i<entries.size() && i<first+18;++i) {
            const int y=40+8*static_cast<int>(i-first);
            if(entries[i].spell) {
                const auto index=*entries[i].spell;
                font_.draw_text(frame,overlay_string(0x272a2)+std::string(session_.spell_catalog().definition(spells_[index].spell).name()),8,y,38,1,ega_ui_palette[page_==Page::list && index==selected_?15:10]);
            } else font_.draw_text(frame,module_text(0xd239+41*entries[i].level),8,y,38,1,ega_ui_palette[13]);
        }
        draw_commands(frame,overlay_string(0x27053));
        if(!diagnostic_.empty()) {
            for(int y=136;y<184;++y) std::fill_n(frame.begin()+y*320+8,304,ega_ui_palette[0]);
            font_.draw_text(frame,diagnostic_,8,136,38,6,ega_ui_palette[12],true);
        }
    }
public:
    SpellMode(ExplorationSession& session,const ExplorationScreen& screen,const PicturePresentation* backdrop):session_(session),screen_(screen),font_(screen.font()),backdrop_(backdrop),
        caster_(session.active_character().value_or(0)),target_(caster_),revision_(session.world.party.revision()),spells_(session.castable_spells()) {
        if(spells_.empty()) {page_=Page::empty;exploration_backdrop_=true;}
    }
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed || page_==Page::leaving || page_==Page::feedback || page_==Page::empty) return;
        auto letter=event.character;if(letter>='a' && letter<='z') letter=static_cast<std::uint8_t>(letter-'a'+'A');
        if(letter=='E' || letter=='L' || event.key==Key::backspace || event.key==Key::cancel) {
            if(page_==Page::target) {target_=caster_;rebuild();}else leave(flow);
            return;
        }
        if(stale()) {diagnostic_="Spell list changed; leave and reopen casting";return;}
        const bool target=page_==Page::target;
        const auto count=target?session_.world.party.size():spells_.size();
        if(!count) return;
        auto& selected=target?target_:selected_;
        if(letter==',' || letter=='.') {const auto n=commands().size();command_=(command_+(letter==','?n-1:1))%n;return;}
        if(event.key==Key::accept) letter=commands().at(command_%commands().size()).key;
        if(event.key==Key::up || event.key==Key::home || event.key==Key::left) {selected=(selected+count-1)%count;command_=0;}
        else if(event.key==Key::down || event.key==Key::end || event.key==Key::right) {selected=(selected+1)%count;command_=0;}
        else if(!target && (event.key==Key::next || letter=='N')) page(1);
        else if(!target && (event.key==Key::previous || letter=='P')) page(-1);
        else if(target && letter>='1' && letter<='8' && static_cast<std::size_t>(letter-'1')<count) {
            target_=letter-'1';cast({MagicTargetKind::party_member,target_});
        } else if(letter=='E') {if(target) {target_=caster_;rebuild();}else leave(flow);}
        else if(target?letter=='S':letter=='C') {
            if(target) cast({MagicTargetKind::party_member,target_});else choose();
        }
    }
    void update(std::uint64_t elapsed,IGameFlow& flow) override {
        if((page_!=Page::feedback && page_!=Page::empty) || !presented_) return;
        // Fixed host policy replaces the native hardware-dependent Speed loop.
        const auto duration=RuntimeTiming::notification_dwell_ms;
        elapsed_+=std::min(elapsed,duration-std::min(duration,elapsed_));
        if(elapsed_<duration) return;
        elapsed_=0;presented_=false;
        if(page_==Page::empty) {leave(flow);return;}
        if(++message_==messages_.size()) rebuild();
    }
    void render(Framebuffer& frame) const override {
        if(exploration_backdrop_) {
            ExplorationUiState ui;ui.focus=ExplorationCommand::cast;ui.highlighted_member=target_;
            ui.picture_override=backdrop_;
            if(target_menu_) ui.command_line=std::string{};
            screen_.draw(frame,session_,ui);
            for(int y=136;y<184;++y) std::fill_n(frame.begin()+y*320+8,304,ega_ui_palette[0]);
            if(target_menu_) draw_commands(frame,overlay_string(0x27a96)+module_text(0x37df));
            if(page_==Page::target) {
                font_.draw_text(frame,diagnostic_,8,136,38,6,ega_ui_palette[12],true);
            }
        } else list(frame);
        if(page_==Page::feedback || page_==Page::empty) {
            for(int y=136;y<184;++y) std::fill_n(frame.begin()+y*320+8,304,ega_ui_palette[0]);
            const auto recipient=page_==Page::empty?caster_:messages_.at(message_).recipient;
            const auto& member=session_.world.party.member(recipient);
            const auto color=!member.active()?12u:member.byte(0x10e)==1?14u:11u;
            font_.draw_text(frame,member.name(),8,144,38,1,ega_ui_palette[color]);
            const auto& text=page_==Page::empty?overlay_string(0x17cba):StringStore::active().get(messages_.at(message_).text_id);
            font_.draw_text(frame,text,8,152,38,4,ega_ui_palette[10],true);
        }
    }
    void after_present(IGameFlow&) override {if(page_==Page::feedback || page_==Page::empty) presented_=true;}
    std::string title() const override {
        if(page_==Page::target) return "PoolRad | spell target";
        if(page_==Page::feedback) return "PoolRad | spell feedback "+std::to_string(message_+1)+"/"+std::to_string(messages_.size());
        if(page_==Page::empty) return "PoolRad | spells empty";
        return "PoolRad | spells";
    }
};
}
std::unique_ptr<IGameMode> make_spell_mode(ExplorationSession& session,const ExplorationScreen& screen,const PicturePresentation* backdrop) {
    return std::make_unique<SpellMode>(session,screen,backdrop);
}
}
