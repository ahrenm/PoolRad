#pragma once
#include "gbengine/platform/hal.hpp"
#include "gbengine/audio/presentation_audio.hpp"
#include <memory>
#include <optional>

namespace GBEngine {
struct IGameMode;
// Requests take effect at a frame boundary. A mode never destroys itself while
// handling input, rendering or acknowledging a successfully presented picture.
struct IGameFlow {
    virtual ~IGameFlow() = default;
    virtual void quit() = 0;
    virtual void change_mode(std::unique_ptr<IGameMode>) = 0;
    virtual void push_mode(std::unique_ptr<IGameMode>) = 0;
    virtual void pop_mode() = 0;
    // Headless modal harnesses may omit audio. Runtime routes these source
    // commands through the loop-owned presentation subsystem and audio HAL.
    virtual void audio_cue(PresentationAudioCue) {}
};
struct IGameMode {
    virtual ~IGameMode() = default;
    virtual bool text_input_active() const {return false;}
    // Called at committed input/update boundaries, including suspended owners.
    // A non-null replacement abandons the whole modal stack before further work.
    virtual std::unique_ptr<IGameMode> terminal_mode() {return {}; }
    virtual void handle_input(InputEvent,IGameFlow&) = 0;
    // Called once per host frame (target30Hz). Animations use actual elapsed
    // milliseconds; campaign turns/minutes belong to gameplay services.
    virtual void update(std::uint64_t elapsed_ms,IGameFlow&) = 0;
    virtual void render(Framebuffer&) const = 0;
    virtual std::optional<ImagePresentation> image_presentation() const {return {};}
    virtual std::string title() const = 0;
    virtual void after_present(IGameFlow&) {}
    virtual void on_suspend() {}
    virtual void on_resume() {}
};
struct IFrameObserver {
    virtual ~IFrameObserver() = default;
    virtual void after_frame(const Framebuffer&,IGameFlow&) = 0;
};
}
