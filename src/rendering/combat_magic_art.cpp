#include "gbengine/rendering/combat_magic_art.hpp"
#include "gbengine/assets/records.hpp"
#include <algorithm>
#include <cstdlib>

namespace GBEngine {
namespace {
Image mirror(Image image) {
    for(int y=0;y<image.height;++y)
        std::reverse(image.pixels.begin()+static_cast<std::ptrdiff_t>(y*image.width),
                     image.pixels.begin()+static_cast<std::ptrdiff_t>((y+1)*image.width));
    return image;
}
bool visible(CombatPosition point,const CombatCamera& camera) {
    const auto x=point.x-camera.x(),y=point.y-camera.y();return x>=0 && x<7 && y>=0 && y<7;
}
//4E80:01A0/02CC: equality chooses Y-major and error0 diagonal.
std::vector<CombatPosition> directions(CombatPosition from,CombatPosition to) {
    const auto dx=std::abs(to.x-from.x),dy=std::abs(to.y-from.y);
    const auto sx=(to.x>from.x)-(to.x<from.x),sy=(to.y>from.y)-(to.y<from.y);
    const bool major_x=dy<dx;const auto major=major_x?dx:dy,minor=major_x?dy:dx;
    auto error=minor*2-major;std::vector<CombatPosition> result;result.reserve(static_cast<std::size_t>(major+1));
    for(int step=0;step<major;++step) {
        CombatPosition delta{major_x?sx:0,major_x?0:sy};
        if(error<0)error+=minor*2;
        else {if(major_x)delta.y=sy;else delta.x=sx;error+=(minor-major)*2;}
        result.push_back(delta);
    }
    result.push_back({0,0});return result;
}
}
CombatMagicArt CombatMagicArt::load(const IFileSystem& files) {
    CombatMagicArt result;
    result.frames_[0]=decode_combat_icon(load_record(files,"COMSPR.DAX",5),CombatIconKind::effect);
    result.frames_[3]=decode_combat_icon(load_record(files,"COMSPR.DAX",133),CombatIconKind::effect);
    for(const auto& image:{&result.frames_[0],&result.frames_[3]})if(image->width!=24 || image->height!=24)
        throw DecodeError("Native casting projectile has an incompatible24px pose");
    result.frames_[1]=mirror(result.frames_[0]);result.frames_[2]=mirror(result.frames_[3]);
    result.lightning_[0]=decode_combat_icon(load_record(files,"COMSPR.DAX",6),CombatIconKind::effect);
    result.lightning_[3]=decode_combat_icon(load_record(files,"COMSPR.DAX",134),CombatIconKind::effect);
    for(const auto& image:{&result.lightning_[0],&result.lightning_[3]})if(image->width!=24 || image->height!=24)
        throw DecodeError("Native lightning projectile has an incompatible24px pose");
    result.lightning_[1]=mirror(result.lightning_[0]);result.lightning_[2]=mirror(result.lightning_[3]);
    const auto recipient=[&](std::array<Image,4>& bank,std::uint8_t record) {
        bank[0]=decode_combat_icon(load_record(files,"COMSPR.DAX",record),CombatIconKind::effect);
        bank[3]=decode_combat_icon(load_record(files,"COMSPR.DAX",static_cast<std::uint8_t>(record+128)),CombatIconKind::effect);
        if(bank[0].width!=24 || bank[0].height!=24 || bank[3].width!=24 || bank[3].height!=24)
            throw DecodeError("Native recipient animation has an incompatible24px pose");
        bank[1]=mirror(bank[0]);bank[2]=mirror(bank[3]);
    };
    recipient(result.effects_,9);recipient(result.damage_,10);
    result.defeat_=decode_combat_icon(load_record(files,"COMSPR.DAX",11),CombatIconKind::effect);
    if(result.defeat_.width!=24 || result.defeat_.height!=24)throw DecodeError("Native defeat icon has incompatible dimensions");
    return result;
}
const Image& CombatMagicArt::image(unsigned pose,unsigned effect_slot) const {
    if(effect_slot==24) {
        if(pose)throw DecodeError("Native defeat icon is a fixed unmirrored frame");
        return defeat_;
    }
    if(effect_slot!=18 && effect_slot!=19 && effect_slot!=22 && effect_slot!=23)
        throw DecodeError("Unknown native combat effect slot");
    const auto& bank=effect_slot==18?frames_:effect_slot==19?lightning_:effect_slot==22?effects_:damage_;
    if(pose>=bank.size() || bank[pose].pixels.size()!=576)throw DecodeError("Casting projectile pose is not loaded");
    return bank[pose];
}
std::vector<CombatMagicFrame> CombatMagicArt::projectile(CombatPosition origin,CombatPosition destination,CombatCamera camera,unsigned effect_slot,bool allow_outside,unsigned frame_count) {
    if(effect_slot!=18 && effect_slot!=19 && effect_slot!=23)throw DecodeError("Unknown native casting projectile effect slot");
    if(frame_count!=1 && frame_count!=4)throw DecodeError("Unknown native projectile frame count");
    const auto valid=[allow_outside](CombatPosition at) {
        return allow_outside?(at.x>=-1 && at.x<=50 && at.y>=-1 && at.y<=25):Battlefield::in_bounds(at.x,at.y);
    };
    if(!valid(origin) || !valid(destination))
        throw DecodeError("Casting projectile endpoints are outside the source battlefield");
    if(origin==destination)return {};
    const auto steps=directions({origin.x*3,origin.y*3},{destination.x*3,destination.y*3});
    std::vector<CombatMagicFrame> result;result.reserve(steps.size()+8);
    auto center=CombatPosition{camera.x()+3,camera.y()+3};
    if((!visible(origin,camera) || !visible(destination,camera)) &&
       std::abs(destination.x-origin.x)<7 && std::abs(destination.y-origin.y)<7)
        center={origin.x+(destination.x-origin.x)/2,origin.y+(destination.y-origin.y)/2};
    const auto redraw=[&](CombatPosition focus,unsigned margin=255) {
        camera.focus(std::clamp(focus.x,0,49),std::clamp(focus.y,0,24),margin);
        result.push_back({{0,0},0,camera,false});
    };
    redraw(center);
    auto tile=origin;CombatPosition phase{};std::size_t cursor=0;unsigned pose=0;
    while(cursor<steps.size()) {
        const auto segment_start=tile;auto point=CombatPosition{tile.x*3+phase.x,tile.y*3+phase.y};
        bool outside=false;
        do {
            result.push_back({point,pose,camera,true});pose=(pose+1)%frame_count;
            ++cursor;
            // Native1D22 increments BEFORE reading the stored direction. The
            // unused first generated step and trailing stationary8 are retained.
            const auto delta=cursor<steps.size()?steps[cursor]:CombatPosition{};
            const auto next=CombatPosition{point.x+delta.x,point.y+delta.y};
            const auto rx=next.x-camera.x()*3,ry=next.y-camera.y()*3;
            outside=rx<0 || rx>18 || ry<0 || ry>18;
            if(!outside && cursor<steps.size()) {
                phase.x+=delta.x;phase.y+=delta.y;
                if(std::abs(phase.x)==3) {tile.x+=(phase.x>0?1:-1);phase.x=0;}
                if(std::abs(phase.y)==3) {tile.y+=(phase.y>0?1:-1);phase.y=0;}
                point=next;
            }
        } while(cursor<steps.size() && !outside);
        if(cursor<steps.size()) {
            if(segment_start.x!=tile.x) {
                if(tile.x>=camera.x()+6)center.x=tile.x+3;
                else if(tile.x<=camera.x())center.x=tile.x-3;
            }
            if(segment_start.y!=tile.y) {
                if(tile.y>=camera.y()+6)center.y=tile.y+3;
                else if(tile.y<=camera.y())center.y=tile.y-3;
            }
            redraw(center);
        }
    }
    if(!visible(destination,camera))redraw(destination,3);
    result.push_back({{destination.x*3,destination.y*3},pose,camera,true});
    if(result.size()>1024)throw DecodeError("Casting projectile exceeded the bounded source presentation plan");
    for(auto& frame:result)frame.effect_slot=effect_slot;
    return result;
}
void CombatMagicArt::draw_raw(Framebuffer& frame,const CombatMagicFrame& spec) const {
    if(!spec.draw_projectile)return;
    const auto& art=image(spec.pose,spec.effect_slot);
    if(spec.subtile.x< -3 || spec.subtile.x>150 || spec.subtile.y< -3 || spec.subtile.y>75)
        throw DecodeError("Casting projectile subtile is outside the source battlefield");
    const auto left=CombatCamera::left+(spec.subtile.x-spec.camera.x()*3)*8;
    const auto top=CombatCamera::top+(spec.subtile.y-spec.camera.y()*3)*8;
    for(int py=std::max(0,CombatCamera::top-top);py<std::min(24,CombatCamera::top+CombatCamera::pixels-top);++py)
        for(int px=std::max(0,CombatCamera::left-left);px<std::min(24,CombatCamera::left+CombatCamera::pixels-left);++px) {
            const auto color=art.pixels[static_cast<std::size_t>(py*24+px)];
            if(color>>24)frame[static_cast<std::size_t>((top+py)*screen_width+left+px)]=color;
        }
}
}
