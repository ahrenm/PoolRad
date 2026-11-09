#pragma once
#include "gbengine/world/geometry.hpp"
#include "gbengine/platform/hal.hpp"

namespace GBEngine {
// Native presentation modes2..4 address overlapping regions of one resident map.
struct OverlandPosition {
    int x=12,y=26;
    unsigned region=3;
    Direction facing=Direction::north;
    bool operator==(const OverlandPosition&) const = default;
};
enum class OverlandTerrain {land,water,blocked};
enum class OverlandMoveResult {moved,water,blocked_terrain,map_edge};
// Original4CB9 overlays alter the displayed tile only. Movement and the ECL
// tile bridge continue to query the owned resident cells, including C018 edits.
struct OverlandCampaignState {
    std::uint16_t vm4a8c=0,vm4ad2=0,vm4a7c=0,vm4a9f=0,vm4aa0=0,vm4ab3=0;
};
class OverlandMapOverlay {
    struct Entry {std::uint16_t x=0,y=0,tile=0;};
    std::array<Entry,9> entries_{};
public:
    static OverlandMapOverlay load(const IFileSystem&);
    // Tile identifiers are1..256: source table's last entry is256, not zero.
    std::uint16_t display_tile(std::uint8_t raw,int global_x,int y,const OverlandCampaignState&) const;
};
// Stage1 travel has no ECL dispatcher, party mutation, campaign clock or RNG.
// Static map/passability data is read from the original resources at launch.
class OverlandTravel {
    std::array<std::uint8_t,44*36> cells_;
    OverlandPosition position_;
    std::vector<std::uint8_t> blocked_tiles_;
public:
    OverlandTravel(const std::array<std::uint8_t,44*36>&,OverlandPosition,
                   std::vector<std::uint8_t> blocked_tiles={});
    static OverlandTravel load(const IFileSystem&,OverlandPosition={});
    const OverlandPosition& position() const {return position_;}
    static OverlandTerrain classify_tile(std::uint8_t);
    std::uint8_t tile(int x,int y) const;
    OverlandMoveResult step(Direction);
};
}
