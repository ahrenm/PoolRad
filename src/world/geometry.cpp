#include "gbengine/world/geometry.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
unsigned index(Direction d) { return static_cast<unsigned>(d); }
int cell(const GeoMap& m,int x,int y) {
    if(!GeoMap::contains(x,y)) {
        if(m.id==0 || m.id==10) return -1;
        // Native queries clamp an outlying coordinate to the opposite edge.
        x=x<0?15:x>15?0:x; y=y<0?15:y>15?0:y;
    }
    return y*16+x;
}
}
GeoMap GeoMap::decode(std::span<const std::uint8_t> data,std::uint8_t id) {
    if(data.size()!=1026 || read_u16(data,0)!=1024) throw DecodeError("Invalid GEO record size/prefix");
    GeoMap m;m.id=id;std::copy(data.begin()+2,data.end(),m.planes.begin());return m;
}
std::uint8_t GeoMap::wall(int x,int y,Direction d) const {
    const int c=cell(*this,x,y);if(c<0) return 0;
    const auto n=index(d);if(n>3) throw DecodeError("Invalid facing");
    return static_cast<std::uint8_t>((planes[static_cast<std::size_t>(c)+(n>=2?256:0)]>>(n%2==0?4:0))&15);
}
std::uint8_t GeoMap::door(int x,int y,Direction d) const {
    if(!wall(x,y,d)) return 1;
    return static_cast<std::uint8_t>((planes[768+static_cast<std::size_t>(cell(*this,x,y))]>>(index(d)*2))&3);
}
std::uint8_t GeoMap::event(int x,int y) const {
    const int c=cell(*this,x,y);return c<0?0:planes[512+static_cast<std::size_t>(c)];
}
}
