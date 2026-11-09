#pragma once
#include "gbengine/app/options.hpp"
#include "gbengine/platform/hal.hpp"
#include "gbengine/saves/save_repository.hpp"
#include "gbengine/audio/presentation_audio.hpp"
#include <functional>
#include <memory>
namespace GBEngine {
using PlatformFactory=std::function<std::unique_ptr<IPlatform>(PlatformOptions)>;
struct AppDependencies {
    std::unique_ptr<IFileSystem> files;
    std::unique_ptr<IArtifactWriter> artifacts;
    std::unique_ptr<ISaveRepository> saves;
    PlatformFactory platform_factory;
    std::ostream* output=nullptr;
    std::ostream* errors=nullptr;
    std::ostream* log=nullptr;
    std::unique_ptr<IAudioCueResolver> audio_cues;
    std::unique_ptr<ISaveRepository> characters;
};
// Composition root: owns resource lifetime, platform and active game flow.
class App {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    App(int argc,char** argv);
    explicit App(LaunchOptions,AppDependencies dependencies={});
    ~App();
    App(const App&)=delete;
    App& operator=(const App&)=delete;
    int run();
};
}
