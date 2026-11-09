#include "gbengine/rendering/combat_weapon_art.hpp"
#include "gbengine/assets/records.hpp"
#include <algorithm>
namespace GBEngine {
CombatWeaponArt CombatWeaponArt::load(const IFileSystem& files) {
    CombatWeaponArt result;
    for(unsigned slot=13;slot<=21;++slot)for(unsigned pair=0;pair<2;++pair) {
        auto image=decode_combat_icon(load_record(files,"COMSPR.DAX",static_cast<std::uint8_t>(slot-13+128*pair)),CombatIconKind::effect);
        if(image.width!=24 || image.height!=24)throw DecodeError("Native weapon travel requires24px COMSPR art");
        result.slots_[slot-13][pair]=std::move(image);
    }
    return result;
}
CombatWeaponForm CombatWeaponArt::form(std::uint8_t type,unsigned direction) {
    if(direction>7)throw DecodeError("Native weapon travel direction is outside0..7");
    if(type==9 || type==0x15 || type==0x1c || type==0x1f || type==0x49) {
        if(direction&1) {
            if(direction==3 || direction==5)return {14,1,1,10,direction==5};
            return {14,0,1,10,direction==7};
        }
        return {direction%4+13,direction>>2,1,10,false};
    }
    if(type==2 || type==7 || type==0x14)return {16,0,4,50,false};
    if(type==0x55 || type==0x56)return {17,0,4,50,false};
    return {type==0x2f?21u:20u,0,2,20,false};
}
std::vector<CombatWeaponFrame> CombatWeaponArt::projectile(const CombatStrike::Travel& travel,CombatCamera camera) {
    const auto shape=form(travel.item_type,travel.direction);
    // Shared10BA:1A29 path and camera barriers; native2-frame cycling is
    // selected after the common geometrical plan, without consuming RNG.
    auto path=CombatMagicArt::projectile(travel.origin,travel.destination,camera,18,false,shape.frames==1?1:4);
    if(path.empty())path.push_back({{travel.origin.x*3,travel.origin.y*3},0,camera,false});
    std::vector<CombatWeaponFrame> result;result.reserve(path.size());
    for(auto& frame:path){frame.pose%=shape.frames;result.push_back({frame,travel.item_type,travel.direction});}
    return result;
}
Image CombatWeaponArt::image(const CombatWeaponFrame& frame) const {
    const auto shape=form(frame.item_type,frame.direction);
    if(frame.motion.pose>=shape.frames)throw DecodeError("Invalid native weapon travel pose");
    auto pair=shape.paired_pose;bool mirrored=shape.mirrored;
    if(shape.frames==4){pair=frame.motion.pose>=2;mirrored=frame.motion.pose==1 || frame.motion.pose==2;}
    // The2-frame branch copies base then paired without mirroring.
    if(shape.frames==2)pair=frame.motion.pose;
    auto result=slots_.at(shape.slot-13).at(pair);
    if(mirrored)for(int y=0;y<24;++y)std::reverse(result.pixels.begin()+y*24,result.pixels.begin()+(y+1)*24);
    return result;
}
void CombatWeaponArt::draw_raw(Framebuffer& frame,const CombatWeaponFrame& spec) const {
    if(!spec.motion.draw_projectile)return;
    const auto art=image(spec);const auto& motion=spec.motion;
    const auto left=CombatCamera::left+(motion.subtile.x-motion.camera.x()*3)*8;
    const auto top=CombatCamera::top+(motion.subtile.y-motion.camera.y()*3)*8;
    for(int y=std::max(0,CombatCamera::top-top);y<std::min(24,CombatCamera::top+CombatCamera::pixels-top);++y)
        for(int x=std::max(0,CombatCamera::left-left);x<std::min(24,CombatCamera::left+CombatCamera::pixels-left);++x) {
            const auto color=art.pixels[static_cast<std::size_t>(y*24+x)];
            if(color>>24)frame[static_cast<std::size_t>((top+y)*screen_width+left+x)]=color;
        }
}
}
