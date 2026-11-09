#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GBEngine {
using Bytes = std::vector<std::uint8_t>;
inline constexpr int screen_width = 320, screen_height = 200;
struct Resolution { int width, height; };
inline constexpr std::array resolutions{Resolution{640,400}, Resolution{960,600},
    Resolution{1280,800}, Resolution{1600,1000}, Resolution{1920,1200}};
using Framebuffer = std::array<std::uint32_t, screen_width * screen_height>;
// Borrowed original-resolution artwork, valid for the synchronous present call.
// Destination coordinates use the game's 320x200 layout, not window pixels.
struct ImagePresentation {
    int width,height;
    std::span<const std::uint32_t> pixels;
    float left,top,display_width,display_height;
};
enum class Key { none, quit, up, down, left, right, accept, cancel, next, previous, scale, look, backspace,home,end,combat_abort,automatic_party,automatic_magic,load,debug_win,debug_snapshot };
struct InputEvent { Key key; bool pressed; std::uint8_t character{}; };
struct IGraphics {
    virtual ~IGraphics() = default;
    virtual void present(const Framebuffer&) = 0;
    // Framebuffer-only backends retain the mode's software fallback.
    virtual void present_image(const Framebuffer& frame,const ImagePresentation&) {present(frame);}
    virtual void set_resolution(Resolution) = 0;
    virtual void set_title(std::string_view) = 0;
};
struct IInput { virtual ~IInput() = default; virtual std::vector<InputEvent> poll() = 0; virtual void set_text_input(bool) {} };
struct IAudio {
    virtual ~IAudio() = default;
    // Native mono floating-point PCM at 48 kHz, not emulated PC speaker I/O.
    virtual void queue(std::span<const float>) = 0;
    virtual void stop() = 0;
};
struct IClock {
    virtual ~IClock() = default;
    virtual std::uint64_t milliseconds() const = 0;
    virtual void sleep(std::uint32_t milliseconds) = 0;
};
struct IFileSystem {
    virtual ~IFileSystem() = default;
    virtual Bytes read_asset(std::string_view name) const = 0;
    virtual std::vector<std::string> list_assets() const = 0;
};
// Development exports and future user-data adapters must keep source assets read-only.
struct IArtifactWriter {
    virtual ~IArtifactWriter() = default;
    virtual void write(const std::filesystem::path&,std::span<const std::uint8_t>) = 0;
};
struct IPlatform : IGraphics,IInput,IAudio,IClock {~IPlatform() override = default;};
struct PlatformOptions {Resolution resolution;bool audio=true,hidden=false;};
class NativeFiles final : public IFileSystem {
    std::filesystem::path root_;
public:
    explicit NativeFiles(std::filesystem::path root);
    const std::filesystem::path& root() const {return root_;}
    Bytes read_asset(std::string_view name) const override;
    std::vector<std::string> list_assets() const override;
};
} // namespace GBEngine
