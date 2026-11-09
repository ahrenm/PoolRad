#pragma once
#include "gbengine/world/geometry.hpp"
#include "gbengine/platform/hal.hpp"
#include <random>
namespace GBEngine {
struct BattlefieldSource {
    std::string file="START.EXE";
    std::size_t module_offset{};
    std::string source_sha256,module_sha256;
};
class BattlefieldTables {
    std::array<std::array<std::uint8_t,4>,66> properties_{};
    std::array<std::int8_t,9> delta_x_{},delta_y_{};
    BattlefieldSource source_;
    bool loaded_=false;
public:
    static BattlefieldTables load(const IFileSystem&);
    const std::array<std::uint8_t,4>& properties(std::uint8_t id) const;
    int delta_x(unsigned direction) const;
    int delta_y(unsigned direction) const;
    const BattlefieldSource& source() const {return source_;}
};
struct Battlefield {
    static constexpr int width=50,height=25;
    // The seven-byte native descriptor header is presentation state, kept
    // separately from these verified row-major terrain bytes.
    std::array<std::uint8_t,width*height> terrain{};
    BattlefieldTables tables;
    std::uint8_t presentation_mode{};
    std::uint8_t environment_selector{};
    static bool in_bounds(int x,int y) {return x>=0 && x<width && y>=0 && y<height;}
    std::uint8_t terrain_at(int x,int y) const;
    std::int8_t movement_cost(int x,int y) const;
    const std::array<std::uint8_t,4>& properties_at(int x,int y) const;
};
class BattlefieldService {
public:
    // Native 2C86:01AD: combine this edge with its neighbor's opposite edge.
    // origin_y retains the out-of-map horizontal corridor exception.
    static std::uint8_t native_edge(const GeoMap&,int origin_y,int x,int y,unsigned direction);
    // Stages every RNG draw; failures leave the caller's generator unchanged.
    static Battlefield build_interior(const BattlefieldTables&,const GeoMap&,int x,int y,std::uint8_t ecl_record,std::mt19937&);
    // Shared final source floor variation pass (2C86:0000), also called by
    // the noninterior builder after its environment-specific scenery passes.
    static void vary_floor(Battlefield&,std::uint8_t ecl_record,std::mt19937&);
};
}
