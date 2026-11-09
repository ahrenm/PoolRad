#pragma once
#include "gbengine/app/game_mode.hpp"
#include "gbengine/app/runtime_timing.hpp"

namespace GBEngine {
// Explicit dependencies keep SDL and platform ownership outside the loop/modes.
struct FrameServices {IGraphics& graphics;IInput& input;IClock& clock;IAudio* audio=nullptr;IAudioCueResolver* audio_cues=nullptr;};
struct LoopOptions {
    std::size_t resolution_index=1;
    std::size_t frame_limit=0; // Zero is interactive, not a simulation tick limit.
    IFrameObserver* observer=nullptr;
};
class GameLoop final : private IGameFlow {
    FrameServices services_;
    LoopOptions options_;
    std::unique_ptr<IGameMode> mode_,next_mode_;
    std::vector<std::unique_ptr<IGameMode>> suspended_;
    enum class Transition {none,replace,push,pop};
    Transition transition_=Transition::none;
    bool quitting_=false,running_=false;
    PresentationAudio audio_;
    void quit() override {quitting_=true;}
    void change_mode(std::unique_ptr<IGameMode>) override;
    void push_mode(std::unique_ptr<IGameMode>) override;
    void pop_mode() override;
    void audio_cue(PresentationAudioCue cue) override {audio_.submit(cue);}
    void clear_modes();
    bool install_terminal_mode();
public:
    GameLoop(FrameServices services,LoopOptions options={}):services_(services),options_(options),audio_(services.audio,services.audio_cues) {}
    std::size_t run(std::unique_ptr<IGameMode>);
    const auto& audio_stats() const {return audio_.stats();}
};
}
