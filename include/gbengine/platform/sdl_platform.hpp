#pragma once
#include "gbengine/platform/hal.hpp"
#include <memory>
namespace GBEngine {
class SdlPlatform final : public IPlatform {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit SdlPlatform(Resolution resolution, bool audio = true, bool hidden = false);
    ~SdlPlatform() override;
    void present(const Framebuffer&) override;
    void present_image(const Framebuffer&,const ImagePresentation&) override;
    void set_resolution(Resolution) override;
    void set_title(std::string_view) override;
    std::vector<InputEvent> poll() override;
    void set_text_input(bool) override;
    void queue(std::span<const float>) override;
    void stop() override;
    std::uint64_t milliseconds() const override;
    void sleep(std::uint32_t) override;
};
}
