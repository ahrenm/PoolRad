#pragma once
#include "gbengine/assets/decoders.hpp"
#include "gbengine/rendering/presentation.hpp"

namespace GBEngine {
// Owned FINAL5 resources and source timing. No VM, party or random generator.
class EndingPresentation {
    struct Record {std::vector<Image> images;std::vector<std::uint32_t> times;};
    std::array<Record,9> records_;
    std::size_t party_count_;
    unsigned speed_;
public:
    struct Step {unsigned scene,frame;std::uint64_t milliseconds;};
    EndingPresentation(const IFileSystem&,std::size_t party_count,unsigned speed=5);
    std::vector<Step> steps() const;
    void draw(Framebuffer&,const GameFont&,Step) const;
};
}
