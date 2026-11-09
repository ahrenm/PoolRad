#pragma once
#include "gbengine/assets/decoders.hpp"

namespace GBEngine {
// A source-defined distance frame composited over the existing exploration
// backbuffer. Distance changes are ECL actions, never animation clock ticks.
class ExplorationSpritePresentation {
    std::vector<Image> frames_;
    unsigned disk_{};
    std::uint8_t record_{},distance_{};
    std::string source_;
public:
    static ExplorationSpritePresentation load(const IFileSystem&,unsigned disk,
                                               std::uint8_t record,std::uint8_t distance);
    // Script-only absent-record handling retains a loaded empty descriptor.
    static ExplorationSpritePresentation load_script(const IFileSystem&,unsigned disk,std::uint8_t record,std::uint8_t distance);
    ExplorationSpritePresentation at_distance(std::uint8_t) const;
    void draw(Framebuffer&) const;
    bool visible() const {return distance_<frames_.size();}
    std::size_t frame_count() const {return frames_.size();}
    unsigned disk() const {return disk_;}
    std::uint8_t record() const {return record_;}
    std::uint8_t distance() const {return distance_;}
    const std::string& source() const {return source_;}
};
}
