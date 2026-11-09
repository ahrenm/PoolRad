#include "gbengine/modes/modes.hpp"
#include "gbengine/ui/overland_screen.hpp"
#include "gbengine/app/runtime_timing.hpp"
#include "gbengine/app/exploration_scenario.hpp"

namespace GBEngine {
namespace {
class OverlandMode final:public IGameMode {
    OverlandScreen screen_;
    OverlandTravel travel_;
    Party party_;
    OverlandMoveResult last_move_=OverlandMoveResult::moved;
    std::uint64_t phase_ms_=0;
    bool presented_=false,suspended_=false;
public:
    OverlandMode(const IFileSystem& files,const LaunchOptions& options,std::ostream& out)
        :screen_(OverlandScreen::load(files)),travel_(OverlandTravel::load(files,
            options.scenario_fixture=="overland-west"?OverlandPosition{7,18,2,Direction::north}:
            options.scenario_fixture=="overland-east"?OverlandPosition{7,18,4,Direction::north}:
            OverlandPosition{10,26,3,Direction::north})) {
        if(!isolated_overland_scenario(options.scenario_fixture))throw DecodeError("Invalid isolated overland scenario");
        if(!options.party_fixture.empty())party_.import_characters(files,options.party_fixture.front());
        const auto p=travel_.position();
        out<<"Overland stage1: original map and six-pose party icon, region"<<p.region<<" at"<<p.x<<","<<p.y<<". "
              "Arrows/keypad2/4/6/8 move; water, blocked terrain and region edges stop movement. "
              "No events, links, campaign time, encounters or saves run in this isolated fixture.\n";
    }
    void handle_input(InputEvent event,IGameFlow&) override {
        if(!event.pressed || suspended_)return;
        std::optional<Direction> direction;
        if(event.character=='8')direction=Direction::north;
        else if(event.character=='6')direction=Direction::east;
        else if(event.character=='2')direction=Direction::south;
        else if(event.character=='4')direction=Direction::west;
        else switch(event.key) {
        case Key::up:direction=Direction::north;break;
        case Key::right:direction=Direction::east;break;
        case Key::down:direction=Direction::south;break;
        case Key::left:direction=Direction::west;break;
        default:break;
        }
        if(direction)last_move_=travel_.step(*direction);
    }
    void update(std::uint64_t elapsed,IGameFlow&) override {
        if(suspended_ || !presented_)return;
        constexpr auto cycle=std::uint64_t{RuntimeTiming::ambient_frame_ms}*6;
        phase_ms_=(phase_ms_+elapsed%cycle)%cycle;
    }
    void render(Framebuffer& frame) const override {
        screen_.draw(frame,travel_.position(),static_cast<unsigned>(phase_ms_/RuntimeTiming::ambient_frame_ms),
                     party_.size()?&party_:nullptr);
    }
    std::string title() const override {
        const auto p=travel_.position();
        const char* result=last_move_==OverlandMoveResult::moved?"READY":last_move_==OverlandMoveResult::water?"WATER":
            last_move_==OverlandMoveResult::map_edge?"MAP EDGE":"BLOCKED TERRAIN";
        return "Overland stage1 | region="+std::to_string(p.region)+" x="+std::to_string(p.x)+" y="+std::to_string(p.y)+
            " facing="+std::to_string(static_cast<unsigned>(p.facing))+" | "+result;
    }
    void after_present(IGameFlow&) override {presented_=true;}
    void on_suspend() override {suspended_=true;}
    void on_resume() override {suspended_=false;presented_=false;}
};
}
std::unique_ptr<IGameMode> make_overland_mode(const IFileSystem& files,const LaunchOptions& options,std::ostream& out) {
    return std::make_unique<OverlandMode>(files,options,out);
}
}
