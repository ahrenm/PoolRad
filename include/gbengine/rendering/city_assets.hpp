#pragma once
#include "gbengine/assets/decoders.hpp"
#include "gbengine/world/geometry.hpp"
#include "gbengine/world/overland.hpp"
#include <random>
namespace GBEngine {
struct CityAssets {
    std::array<std::uint8_t,44*36> cells{}; // Existing compatibility queries/collision and schema6 data.
    // Native START map used by drawing. Legacy C018 bridge edits are not art IDs.
    // Derived from immutable source data on load/restore; never a new save field.
    std::array<std::uint8_t,44*36> display_cells{};
    std::vector<Image> tiles;
    std::array<Image,6> party_marker;
    bool replacement_tiles_loaded=false;
    OverlandMapOverlay overlay;
    std::array<std::uint8_t,29> river_selectors{};
    std::array<std::array<std::uint8_t,16>,2> river_palettes{};
    // Native animation windows, global resident coordinates {left,top}.
    std::array<std::array<std::uint16_t,2>,9> river_windows{};
    static CityAssets load(const IFileSystem&);
    void load_replacement_tiles(const IFileSystem&);
    // Native3FC3:06B3/1464:1E5C EGA pass. Non-null RNG applies one stochastic
    // pass (5760 Random(256) calls); null applies the final deterministic pass.
    // The owning session stages/commits RNG and presentation, not the renderer.
    void apply_river_recovery(std::mt19937* random=nullptr);
    std::uint8_t tile(int x,int y,unsigned mode) const;
    std::uint16_t display_tile(int global_x,int y,const OverlandCampaignState&) const;
    // Native4CB9 map window, without the separately drawn4C8E party marker.
    void draw_window(Framebuffer&,int global_left,int global_top,int left=16,int top=0,
                     const OverlandCampaignState* state=nullptr) const;
    void draw(Framebuffer&,int x,int y,unsigned mode,Direction facing=Direction::north,
              unsigned pose=0,int left=16,int top=0,const OverlandCampaignState* state=nullptr) const;
};
}
