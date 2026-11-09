#pragma once
#include "gbengine/assets/decoders.hpp"
namespace GBEngine {
enum class Direction : unsigned { north, east, south, west };
struct GeoMap {
    std::uint8_t id{};
    std::array<std::uint8_t,1024> planes{};
    static GeoMap decode(std::span<const std::uint8_t>,std::uint8_t id);
    static bool contains(int x,int y) { return x>=0 && x<16 && y>=0 && y<16; }
    std::uint8_t wall(int x,int y,Direction) const;
    std::uint8_t door(int x,int y,Direction) const;
    std::uint8_t event(int x,int y) const;
};
}
