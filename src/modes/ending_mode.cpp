#include "gbengine/modes/modes.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include "gbengine/rendering/ending_presentation.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
class EndingMode final:public IGameMode {
    ExplorationSession& session_;
    const ExplorationScreen& screen_;
    EndingPresentation art_;
    std::vector<EndingPresentation::Step> steps_;
    std::unique_ptr<Framebuffer> backdrop_=std::make_unique<Framebuffer>();
    std::uint64_t token_,revision_,elapsed_=0;
    std::mt19937 expected_random_;
    std::size_t step_=0;
    bool presented_=false,leaving_=false;
    mutable bool rendered_=false;
public:
    EndingMode(ExplorationSession& session,const IFileSystem& files,const ExplorationScreen& screen)
        :session_(session),screen_(screen),art_(files,session.world.party.size(),session.game_speed()),steps_(art_.steps()),
        token_(session.vm.pending_request()?session.vm.pending_request()->token:0),
        revision_(session.world.party.revision()),expected_random_(session.vm.random_state()) {
        if(!session.ending_pending() || !token_)throw DecodeError("Ending requires its held native mode8 request");
        // Own the old scene once. Rendering never polls/mutates campaign state.
        screen_.draw(*backdrop_,session_,{});
        // The held mode8 request inherits exploration's generic Enter prompt.
        // This presentation advances on timers; its source suffix owns any
        // later confirmation menu.
        std::fill(backdrop_->begin()+192*320,backdrop_->end(),ega_ui_palette[0]);
    }
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed || leaving_)return;
        if(event.key==Key::quit)flow.quit();
        if(event.key==Key::combat_abort) {
            // Development presentation restart, not gameplay rollback: the
            // completed battle, source quest writes and held token remain owned.
            step_=0;elapsed_=0;presented_=false;rendered_=false;
        }
    }
    void update(std::uint64_t delta,IGameFlow& flow) override {
        if(!presented_ || leaving_)return;
        const auto duration=steps_.at(step_).milliseconds;
        if(delta<duration-elapsed_) {elapsed_+=delta;return;}
        // Discard excess wall time: every next source frame needs an actual
        // presentation before its own timer starts. No unseen-frame skipping.
        elapsed_=0;presented_=false;rendered_=false;
        if(++step_==steps_.size()) {
            session_.finish_ending(token_,revision_,expected_random_);
            leaving_=true;flow.pop_mode();
        }
    }
    void render(Framebuffer& frame) const override {
        frame=*backdrop_;
        art_.draw(frame,screen_.font(),steps_.at(std::min(step_,steps_.size()-1)));
        rendered_=true;
    }
    void after_present(IGameFlow&) override {if(rendered_){rendered_=false;presented_=true;}}
    void on_suspend() override {presented_=false;rendered_=false;}
    void on_resume() override {presented_=false;rendered_=false;}
    std::string title() const override {return "PoolRad | Ending";}
};
}
std::unique_ptr<IGameMode> make_ending_mode(ExplorationSession& session,const IFileSystem& files,const ExplorationScreen& screen) {
    return std::make_unique<EndingMode>(session,files,screen);
}
}

