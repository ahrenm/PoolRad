#include "gbengine/rendering/combat_renderer.hpp"
#include "gbengine/assets/records.hpp"
#include <algorithm>

namespace GBEngine {
void CombatCamera::focus(int world_x,int world_y,unsigned margin) {
    if(!Battlefield::in_bounds(world_x,world_y) || (margin>127 && margin!=255))
        throw DecodeError("Invalid combat camera target or margin");
    const auto center_x=x_+3,center_y=y_+3;
    if(margin==255 || world_x<center_x-static_cast<int>(margin) || world_x>center_x+static_cast<int>(margin) ||
       world_y<center_y-static_cast<int>(margin) || world_y>center_y+static_cast<int>(margin)) {
        // Source07D4 adjusts each outlying axis to the target, not merely to
        // the nearest edge of the retained center margin.
        if(margin==255 || world_x<center_x-static_cast<int>(margin) || world_x>center_x+static_cast<int>(margin))
            x_=std::clamp(world_x,3,46)-3;
        if(margin==255 || world_y<center_y-static_cast<int>(margin) || world_y>center_y+static_cast<int>(margin))
            y_=std::clamp(world_y,3,21)-3;
    }
}
void CombatCamera::pan(int dx,int dy) {
    // Inspection input is a host adaptation, bounded by the same source grid.
    x_=static_cast<int>(std::clamp(static_cast<long long>(x_)+dx,0ll,43ll));
    y_=static_cast<int>(std::clamp(static_cast<long long>(y_)+dy,0ll,18ll));
}
CombatCamera CombatCamera::initial(const DeploymentState& state) {
    if(state.actors.empty() || !state.actors.front().position)
        throw DecodeError("Native head actor has no initial camera position");
    CombatCamera result;result.focus(state.actors.front().position->x,state.actors.front().position->y);return result;
}
CombatTerrainAssets CombatTerrainAssets::load(const IFileSystem& files,std::uint8_t mode) {
    if(mode<1 || mode>4)throw DecodeError("Unknown native combat presentation mode");
    CombatTerrainAssets result;result.presentation_mode_=mode;
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    for(std::size_t shape=0;shape<4;++shape)for(std::size_t cell=0;cell<4;++cell) {
        const auto offset=0xc7c0+0x2844+(shape+1)*8+cell*2;
        const auto signed_byte=[](std::uint8_t value){return value<128?static_cast<int>(value):static_cast<int>(value)-256;};
        result.footprints_[shape][cell]={signed_byte(module.at(offset)),signed_byte(module.at(offset+1))};
    }
    const auto load=[&](std::string_view name,std::size_t first,std::size_t count) {
        const auto record=load_record(files,name,1);
        if(record.size()!=17+count*288 || read_u16(record,0)!=24 || read_u16(record,2)!=3 || record[8]!=count)
            throw DecodeError("Invalid native combat terrain record layout");
        // Native EGA1464:02A5 writes the raw nibble's bitplanes directly
        // (resident25F6=01,02,04,08). These opaque banks do not use the asset
        // viewer's combat-family0/8 palette adaptation or actor color masks.
        auto images=decode_ega(record,"native-opaque-terrain");
        if(images.size()!=count || first+count>result.tiles_.size() ||
           std::any_of(images.begin(),images.end(),[](const auto& image) {
               return image.width!=24 || image.height!=24 || image.pixels.size()!=576;
           }))throw DecodeError("Invalid native combat terrain bank");
        for(std::size_t i=0;i<count;++i)result.tiles_[first+i]=std::move(images[i]);
    };
    // 2C86:1244 copies complete source record1 banks into these slots. The
    // 24/33/5 first arguments are source last indices, not destination slots.
    load(mode==1?"DUNGCOM.DAX":"WILDCOM.DAX",0,mode==1?25:34);
    load("RANDCOM.DAX",34,6);
    return result;
}
const Image& CombatTerrainAssets::tile(std::uint8_t id) const {
    if(id>=tiles_.size() || !tiles_[id])throw DecodeError("Unloaded native combat terrain tile");
    return *tiles_[id];
}
bool CombatTerrainAssets::visible(const DeploymentActor& actor,const CombatCamera& camera,bool whole) const {
    if(actor.removed || !actor.position || !actor.shape)return false;
    if(!presentation_mode_ || actor.shape>4)throw DecodeError("Invalid native combat visibility footprint");
    bool found=false;
    for(const auto offset:footprints_[actor.shape-1])if(offset.x>=0) {
        const int x=actor.position->x+offset.x-camera.x(),y=actor.position->y+offset.y-camera.y();
        const bool inside=x>=0 && x<7 && y>=0 && y<7;
        if(whole && !inside)return false;
        if(!whole && inside)return true;
        found=true;
    }
    return whole && found;
}
void CombatRenderer::draw_terrain(Framebuffer& frame,const Battlefield& field,const CombatTerrainAssets& assets,const CombatCamera& camera) {
    if(field.presentation_mode!=assets.presentation_mode())throw DecodeError("Combat terrain presentation mode mismatch");
    // Stage before publication so an invalid cell/resource never leaves a
    // partially changed framebuffer or falls back to an invented floor tile.
    std::array<const Image*,49> images{};
    for(int row=0;row<7;++row)for(int col=0;col<7;++col)
        images[static_cast<std::size_t>(row*7+col)]=&assets.tile(field.properties_at(camera.x()+col,camera.y()+row)[3]);
    for(int row=0;row<7;++row)for(int col=0;col<7;++col) {
        const auto& image=*images[static_cast<std::size_t>(row*7+col)];
        for(int py=0;py<24;++py)for(int px=0;px<24;++px)
            frame[static_cast<std::size_t>((CombatCamera::top+row*24+py)*320+CombatCamera::left+col*24+px)]=
                image.pixels[static_cast<std::size_t>(py*24+px)];
    }
}
void CombatRenderer::draw_actor(Framebuffer& frame,const Image& image,int relative_x,int relative_y) {
    if(image.width<=0 || image.width>320 || image.height<=0 || image.height>200 ||
       image.pixels.size()!=static_cast<std::size_t>(image.width*image.height))
        throw DecodeError("Invalid combat actor image");
    // Native positions fit a signed byte; explicit bounds also prevent integer
    // overflow if a caller supplies malformed inspection coordinates.
    if(relative_x< -127 || relative_x>127 || relative_y< -127 || relative_y>127)
        throw DecodeError("Invalid relative combat actor position");
    const int left=CombatCamera::left+relative_x*24,top=CombatCamera::top+relative_y*24;
    const int x0=std::max(left,CombatCamera::left),x1=std::min(left+image.width,CombatCamera::left+CombatCamera::pixels);
    const int y0=std::max(top,CombatCamera::top),y1=std::min(top+image.height,CombatCamera::top+CombatCamera::pixels);
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
        const auto color=image.pixels[static_cast<std::size_t>((y-top)*image.width+x-left)];
        if(color>>24)frame[static_cast<std::size_t>(y*320+x)]=color;
    }
}
void CombatRenderer::apply_display_palette(Framebuffer& frame) {
    // 4F3E:00E3 maps EGA register0 to dark gray and8 to black;0105
    // restores them on exit. Keep raw source colors through actor masking
    // and component recoloring, then translate the completed viewport.
    // Other UI layers already draw resolved display colors.
    for(int y=CombatCamera::top;y<CombatCamera::top+CombatCamera::pixels;++y)
        for(int x=CombatCamera::left;x<CombatCamera::left+CombatCamera::pixels;++x) {
            auto& color=frame[static_cast<std::size_t>(y*320+x)];
            if(color==0xff000000u)color=0xff555555u;
            else if(color==0xff555555u)color=0xff000000u;
        }
}
}
