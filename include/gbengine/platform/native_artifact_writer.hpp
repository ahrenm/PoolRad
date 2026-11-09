#pragma once
#include "gbengine/platform/hal.hpp"
namespace GBEngine {
class NativeArtifactWriter final : public IArtifactWriter {
    std::filesystem::path source_root_;
public:
    explicit NativeArtifactWriter(const std::filesystem::path& source_root);
    void write(const std::filesystem::path&,std::span<const std::uint8_t>) override;
};
}
