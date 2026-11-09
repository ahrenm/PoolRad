#include "gbengine/qol/fix.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include "gbengine/app/runtime_timing.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
class FixMode final:public IGameMode {
    ExplorationSession& session_;
    const ExplorationScreen& screen_;
    const PicturePresentation* backdrop_;
    CampFix fix_;
    bool presented_=false,left_=false;
    std::size_t notice_=0;
    std::uint64_t dwell_=0;
public:
    FixMode(ExplorationSession& session,const ExplorationScreen& screen,const PicturePresentation* backdrop)
        :session_(session),screen_(screen),backdrop_(backdrop),fix_(session) {}
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed || left_)return;
        if(fix_.terminal()) {if(presented_ && (event.key==Key::accept || event.key==Key::cancel || event.key==Key::backspace)){left_=true;flow.pop_mode();}return;}
        if(event.key==Key::cancel || event.key==Key::backspace || event.character=='S' || event.character=='s') {
            fix_.cancel();if(fix_.terminal())presented_=false;
        }
    }
    void update(std::uint64_t elapsed,IGameFlow& flow) override {
        if(!presented_ || left_)return;
        if(fix_.terminal()) {
            if(fix_.phase()==FixPhase::interrupted){left_=true;flow.pop_mode();}
            return;
        }
        if(fix_.phase()==FixPhase::feedback) {
            dwell_+=std::min(elapsed,RuntimeTiming::notification_dwell_ms);
            if(dwell_<RuntimeTiming::notification_dwell_ms)return;
            dwell_=0;presented_=false;
            if(++notice_>=fix_.notices().size()){notice_=0;fix_.acknowledge();}
            return;
        }
        fix_.advance();presented_=false;dwell_=0;notice_=0;
    }
    void render(Framebuffer& frame) const override {
        ExplorationUiState ui;ui.picture_override=backdrop_;ui.command_line=std::string{};
        screen_.draw(frame,session_,ui);
        for(int y=136;y<184;++y)std::fill_n(frame.begin()+y*320+8,304,ega_ui_palette[0]);
        const auto text=fix_.phase()==FixPhase::feedback?fix_.notices().at(notice_):fix_.message();
        screen_.font().draw_text(frame,text,8,136,38,5,ega_ui_palette[fix_.phase()==FixPhase::blocked?12:10],true);
        screen_.font().draw_text(frame,"Fix: "+std::to_string(fix_.elapsed_minutes())+" minutes",8,176,38,1,ega_ui_palette[10]);
        screen_.font().draw_text(frame,fix_.terminal()?"ENTER TO RETURN":"ESC / S TO STOP",0,192,40,1,ega_ui_palette[15]);
    }
    void after_present(IGameFlow&) override {presented_=true;}
    void on_suspend() override {presented_=false;}
    void on_resume() override {presented_=false;}
    std::string title() const override {return "PoolRad | QoL Fix";}
};
}
std::unique_ptr<IGameMode> make_camp_fix_mode(ExplorationSession& session,const ExplorationScreen& screen,const PicturePresentation* backdrop) {
    return std::make_unique<FixMode>(session,screen,backdrop);
}
}
