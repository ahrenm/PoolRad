#include "gbengine/modes/shrine.hpp"
#include "gbengine/campaign/shrine_service.hpp"
#include "gbengine/world/exploration_session.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include "gbengine/app/runtime_timing.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
class ShrineHealingMode final:public IGameMode {
    enum class Page {list,unneeded,fee,notice,error};
    ExplorationSession& session_;
    const ExplorationScreen& screen_;
    Page page_=Page::list;
    std::size_t selected_=0,member_=0;
    std::uint64_t revision_=0;
    ShrineInspection inspection_;
    bool unneeded_=false,presented_=false;
    std::uint64_t elapsed_=0;
    std::string message_;
    std::vector<std::string> notices_;
    std::size_t notice_=0;
    ShrineAction action() const {return static_cast<ShrineAction>(selected_);}
    void choose(IGameFlow& flow) {
        if(selected_==9) {flow.pop_mode();return;}
        const auto member=session_.active_character();if(!member)return;
        member_=*member;revision_=session_.world.party.revision();
        inspection_=session_.shrine_inspection(member_,action());unneeded_=false;
        if(inspection_.status!=EconomyStatus::applied) {page_=Page::error;message_=inspection_.message;return;}
        page_=inspection_.needed?Page::fee:Page::unneeded;
    }
    void pay() {
        if(session_.world.party.revision()!=revision_) {page_=Page::error;message_="The party changed; choose the shrine service again.";return;}
        const auto result=session_.apply_shrine(member_,action(),unneeded_,true);
        message_=result.message;page_=Page::error;
        if(result.applied()) {
            page_=Page::notice;presented_=false;elapsed_=0;
            notices_={session_.world.party.member(member_).name()+" "+overlay_string(17964)};notice_=0;
            // Native00BF says cured before175D/4E can report revival.
            if(!result.message.empty())notices_.push_back(result.message);
            message_=notices_.front();
        }
    }
public:
    ShrineHealingMode(ExplorationSession& session,const ExplorationScreen& screen):session_(session),screen_(screen) {}
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed || page_==Page::notice)return;
        auto key=event.character;if(key>='a' && key<='z')key=static_cast<std::uint8_t>(key-'a'+'A');
        if(page_==Page::unneeded || page_==Page::fee) {
            // Native question helper accepts explicitY/N only; No is focus.
            if(key=='N') {page_=Page::list;return;}
            if(key=='Y') {if(page_==Page::unneeded){unneeded_=true;page_=Page::fee;}else pay();}return;
        }
        if(page_==Page::error) {if(event.key==Key::accept || event.key==Key::cancel || key=='E')page_=Page::list;return;}
        if(event.key==Key::cancel || key=='E' || key==' ') {flow.pop_mode();return;}
        if(event.key==Key::home || event.key==Key::up)selected_=(selected_+9)%10;
        else if(event.key==Key::end || event.key==Key::down)selected_=(selected_+1)%10;
        else if(event.key==Key::accept || key=='H')choose(flow);
    }
    void update(std::uint64_t elapsed,IGameFlow&) override {
        if(page_==Page::notice && presented_) {
            elapsed_=std::min<std::uint64_t>(RuntimeTiming::notification_dwell_ms,elapsed_+std::min(elapsed,RuntimeTiming::notification_dwell_ms));
            if(elapsed_>=RuntimeTiming::notification_dwell_ms) {
                if(++notice_<notices_.size()) {message_=notices_[notice_];presented_=false;elapsed_=0;}
                else page_=Page::list;
            }
        }
    }
    void render(Framebuffer& frame) const override {
        frame.fill(ega_ui_palette[0]);screen_.chrome().draw(frame,ChromeLayout::merchant());
        const auto& font=screen_.font();
        const auto member=session_.active_character();
        const auto title=member?session_.world.party.member(*member).name()+overlay_string(20269):std::string{};
        font.draw_text(frame,title,8,8,38,2,ega_ui_palette[15]);
        for(std::size_t i=0;i<10;++i)font.draw_text(frame,StringStore::active().get(pascal_string_id("START.EXE:module",51418+41*i)),8,32+8*static_cast<int>(i),38,1,ega_ui_palette[i==selected_?15:10]);
        if(page_==Page::fee || page_==Page::unneeded) {
            const auto& character=session_.world.party.member(member_);
            std::string prompt;
            if(page_==Page::unneeded)prompt=character.name()+" "+StringStore::active().get(inspection_.question_id);
            else prompt=StringStore::active().get(inspection_.service_id)+overlay_string(17901)+std::to_string(inspection_.fee)+overlay_string(17918);
            font.draw_text(frame,prompt,8,136,38,5,ega_ui_palette[10],true);
            font.draw_text(frame,overlay_string(page_==Page::unneeded?17783:17932),0,192,40,1,ega_ui_palette[13]);
            const auto offset=8*static_cast<int>(overlay_string(page_==Page::unneeded?17783:17932).size());
            auto answers=StringStore::active().get(pascal_string_id("GAME.OVR",20988));std::erase(answers,'~');
            font.draw_text(frame,answers,offset,192,(320-offset)/8,1,ega_ui_palette[10]);
            font.draw_text(frame,std::string_view(answers).substr(0,1),offset,192,1,1,ega_ui_palette[15]);
            const auto split=answers.find(' ');
            if(split!=std::string::npos)font.draw_text(frame,std::string_view(answers).substr(split+1),offset+8*static_cast<int>(split+1),192,(320-offset)/8,1,ega_ui_palette[15]);
        } else {
            const auto& footer=overlay_string(20292);font.draw_text(frame,footer,0,192,40,1,ega_ui_palette[10]);
            const auto split=footer.find(' ');font.draw_text(frame,std::string_view(footer).substr(0,split),0,192,40,1,ega_ui_palette[15]);
            if(split!=std::string::npos)font.draw_text(frame,std::string_view(footer).substr(split+1,1),8*static_cast<int>(split+1),192,1,1,ega_ui_palette[15]);
            if(page_==Page::notice || page_==Page::error)font.draw_text(frame,message_,8,136,38,5,ega_ui_palette[10],true);
        }
    }
    void after_present(IGameFlow&) override {if(page_==Page::notice)presented_=true;}
    std::string title() const override {return "Shrine";}
};
}
std::unique_ptr<IGameMode> make_shrine_healing_mode(ExplorationSession& session,const ExplorationScreen& screen) {return std::make_unique<ShrineHealingMode>(session,screen);}
}
