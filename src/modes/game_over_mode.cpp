#include "gbengine/modes/modes.hpp"
#include "gbengine/ui/game_over_screen.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include "gbengine/world/exploration_session.hpp"
#include "gbengine/debug/runtime.hpp"
namespace GBEngine {
namespace {
class GameOverMode final:public IGameMode {
    std::unique_ptr<ExplorationSession> session_;
    std::unique_ptr<Debug::SessionAttachment> debug_session_;
    GameFont font_;ScreenChrome chrome_;
    const IFileSystem& files_;LaunchOptions options_;std::ostream& output_;
    ISaveRepository* saves_;ISaveRepository* characters_;
    bool presented_=false,done_=false;mutable bool rendered_=false;
public:
    GameOverMode(std::unique_ptr<ExplorationSession> session,const ExplorationScreen& screen,const IFileSystem& files,
                 const LaunchOptions& options,std::ostream& output,ISaveRepository* saves,ISaveRepository* characters)
        :session_(std::move(session)),font_(screen.font()),chrome_(screen.chrome()),files_(files),options_(options),output_(output),saves_(saves),characters_(characters) {
        if(!session_ || !session_->party_defeated())throw DecodeError("Game over requires a committed terminal party");
    }
    void handle_input(InputEvent e,IGameFlow& flow)override {
        if(!e.pressed || !presented_ || done_ || (e.key==Key::none && !e.character))return;
        auto cale=make_cale_mode(files_,options_,output_,saves_,characters_);
        done_=true;flow.change_mode(std::move(cale));
    }
    void update(std::uint64_t,IGameFlow&)override {}
    void render(Framebuffer& frame)const override {draw_game_over(frame,font_,chrome_);rendered_=true;}
    void after_present(IGameFlow&)override {
        if(!rendered_)return;
        rendered_=false;presented_=true;
        // The old modal owner has now been destroyed and detached from tracing.
        if(!debug_session_)debug_session_=std::make_unique<Debug::SessionAttachment>(*session_);
    }
    void on_suspend()override {presented_=rendered_=false;}
    void on_resume()override {presented_=rendered_=false;}
    std::string title()const override {return "PoolRad | Game Over";}
};
}
std::unique_ptr<IGameMode> make_game_over_mode(std::unique_ptr<ExplorationSession> session,const ExplorationScreen& screen,
        const IFileSystem& files,const LaunchOptions& options,std::ostream& out,ISaveRepository* saves,ISaveRepository* characters) {
    return std::make_unique<GameOverMode>(std::move(session),screen,files,options,out,saves,characters);
}
}
