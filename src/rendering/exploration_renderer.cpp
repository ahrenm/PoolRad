#include "gbengine/rendering/exploration_renderer.hpp"
#include "gbengine/rendering/screen_chrome.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
constexpr std::array dx{0,1,0,-1},dy{-1,0,1,0};
unsigned index(Direction d) { return static_cast<unsigned>(d); }
constexpr std::array posx{5,4,6,4,2,7,2,0,9,5};
constexpr std::array posy{4,3,3,3,1,1,1,0,0,4};
}
void render_exploration(Framebuffer& out,const WorldState& w,const ExplorationAssets& a) {
    if(!w.map) throw DecodeError("Rendering requires geometry");
    std::array<std::uint8_t,4> colors{11,0,7,6}; // Retained legacy presentation outside recovered profiles.
    if(w.background) {
        const auto& b=*w.background;
        colors={a.background_color(w.map->event(w.x,w.y)&0x80?b.alternate_sky:b.sky),b.horizon,b.lower_horizon,b.floor};
    }
    for(int y=16;y<104;++y) for(int x=16;x<104;++x)
        out[static_cast<std::size_t>(y*320+x)]=ega_ui_palette[colors[y<59?0:y==59?1:y<62?2:3]];
    const auto f=index(w.facing),left=(f+3)%4,right=(f+1)%4;
    auto wall=[&](unsigned d,int x,int y){return w.map->wall(x,y,static_cast<Direction>(d));};
    auto draw=[&](unsigned slice,unsigned type,int shift){a.draw_slice(out,slice,type,posx[slice]+shift,posy[slice]);};
    int cx=w.x+2*dx[f],cy=w.y+2*dy[f];
    // Transcribed painter order from overlay 29:05fd, including far end caps.
    for(int side=0;side<2;++side) {
        const auto dir=side?right:left,other=side?left:right;const int sign=side?1:-1;
        int x=cx,y=cy;unsigned previous=0;
        for(int i=0;i<4;++i,x+=dx[dir],y+=dy[dir]) {
            const auto type=wall(f,x,y);
            if(!GeoMap::contains(x,y) && !wall(other,x,y)) previous=0;
            if(!type) {
                if(previous && wall(dir,x-dx[dir],y-dy[dir])) draw(9,previous,sign*(2*i-1));
                previous=0;
            } else {
                if(previous) draw(9,previous,sign*(2*i-1));
                draw(0,type,sign*2*i);previous=type;
            }
        }
    }
    for(int side=0;side<2;++side) {
        const auto dir=side?right:left;const int sign=side?1:-1;
        int x=cx,y=cy;
        for(int i=0;i<3;++i,x+=dx[dir],y+=dy[dir]) draw(side?2:1,wall(dir,x,y),sign*(2*i+(i?1:0)));
    }
    cx-=dx[f];cy-=dy[f];
    for(int depth=1;depth>=0;--depth) {
        const int reach=depth?2:1,spacing=depth?3:7;
        for(int side=0;side<2;++side) {
            const auto dir=side?right:left,step=side?left:right;const int sign=side?1:-1;
            int x=cx+reach*dx[dir],y=cy+reach*dy[dir];
            for(int i=0;i<=reach;++i,x+=dx[step],y+=dy[step]) {
                const int shift=sign*spacing*(reach-i);
                draw(depth?3:6,wall(f,x,y),shift);
                draw(depth?(side?5:4):(side?8:7),wall(dir,x,y),shift);
            }
        }
        cx-=dx[f];cy-=dy[f];
    }
}
}
