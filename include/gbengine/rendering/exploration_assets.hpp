#pragma once
#include "gbengine/assets/decoders.hpp"
#include <optional>
namespace GBEngine {
struct WallSlot {
    std::array<std::uint8_t,780> definitions{};
    std::vector<Image> tiles;
};
class ExplorationAssets {
    friend class PortSave;
    struct Source {unsigned disk{},slot{};std::uint8_t record{};};
    std::vector<Image> universal_;
    std::array<std::optional<WallSlot>,3> slots_;
    std::array<std::optional<Source>,3> sources_;
    std::optional<std::array<std::uint8_t,16>> background_selectors_;
public:
    void load_background(const IFileSystem&);
    std::uint8_t background_color(std::uint16_t) const;
    void load_walls(const IFileSystem&,unsigned disk,unsigned slot,std::uint8_t record);
    void draw_slice(Framebuffer&,unsigned slice,unsigned wall,int col,int row) const;
    // Read-only provenance of the currently owned wall cache.
    bool resident_walls_from_disk(unsigned disk) const {
        bool any=false;
        for(std::size_t i=0;i<slots_.size();++i)if(slots_[i]) {
            if(!sources_[i] || sources_[i]->disk!=disk)return false;
            any=true;
        }
        return any;
    }
    bool loaded(unsigned slot) const { return slots_.at(slot).has_value(); }
};
}
