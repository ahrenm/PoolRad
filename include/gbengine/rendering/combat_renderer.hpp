#pragma once
#include "gbengine/combat/deployment.hpp"
#include "gbengine/assets/decoders.hpp"

namespace GBEngine {
// Read-only combat inspection shares the source's7x7 camera, without moving
// actors, running rounds or charging campaign time.
class CombatCamera {
    int x_{},y_{};
public:
    static constexpr int cells=7,tile_pixels=24,left=8,top=8,pixels=168;
    int x() const {return x_;}
    int y() const {return y_;}
    bool operator==(const CombatCamera&) const=default;
    void focus(int world_x,int world_y,unsigned margin=255);
    void pan(int dx,int dy);
    static CombatCamera initial(const DeploymentState&);
};
class CombatTerrainAssets {
    std::array<std::optional<Image>,40> tiles_{};
    std::array<std::array<CombatPosition,4>,4> footprints_{};
    std::uint8_t presentation_mode_{};
public:
    static CombatTerrainAssets load(const IFileSystem&,std::uint8_t presentation_mode);
    const Image& tile(std::uint8_t id) const;
    std::uint8_t presentation_mode() const {return presentation_mode_;}
    bool visible(const DeploymentActor&,const CombatCamera&,bool whole_footprint=false) const;
};
class CombatRenderer {
public:
    static void draw_terrain(Framebuffer&,const Battlefield&,const CombatTerrainAssets&,const CombatCamera&);
    // Relative grid coordinates may be negative for a partly visible actor.
    // The image owns the source mask represented by alpha; no color-key guess.
    static void draw_actor(Framebuffer&,const Image&,int relative_x,int relative_y);
    // Apply native combat EGA palette0/8 exchange after mask/composition.
    // Call once after the viewport has been completely redrawn.
    static void apply_display_palette(Framebuffer&);
};
}
