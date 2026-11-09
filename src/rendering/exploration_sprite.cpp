#include "gbengine/rendering/exploration_sprite.hpp"
#include "gbengine/assets/records.hpp"
#include <utility>

namespace GBEngine {
ExplorationSpritePresentation ExplorationSpritePresentation::load(const IFileSystem& files,
    unsigned disk,std::uint8_t record,std::uint8_t distance) {
    if(disk<1 || disk>8) throw DecodeError("Exploration sprite disk is outside native resource range");
    if(distance>2) throw DecodeError("Exploration sprite distance exceeds the three source frames");
    const auto filename="SPRIT"+std::to_string(disk)+".DAX";
    const auto bytes=load_record(files,filename,record);
    // 4CE2:00A2 loads separately headed distance images. Validate this
    // family explicitly; the generic EGA decoder's other layouts are not an
    // alternate exploration-sprite format.
    if(bytes.empty() || bytes[0]<2 || bytes[0]>3) throw DecodeError("Exploration sprite requires its verified two/three-frame profile");
    std::size_t at=1;
    for(unsigned frame=0;frame<bytes[0];++frame) {
        if(at>bytes.size() || bytes.size()-at<21) throw DecodeError("Truncated exploration sprite frame header");
        const auto height=read_u16(bytes,at+4);
        const auto width=read_u16(bytes,at+6);
        const auto x=read_u16(bytes,at+8),y=read_u16(bytes,at+10);
        if(!height || height>88 || !width || width>11 || x>10 || y>10 ||
           x*8u+width*8u>88 || y*8u+height>88)
            throw DecodeError("Exploration sprite frame exceeds its verified viewport profile");
        const auto size=static_cast<std::size_t>(height)*width*4;
        if(size>bytes.size()-at-21) throw DecodeError("Truncated exploration sprite pixels");
        at+=21+size;
    }
    if(at!=bytes.size()) throw DecodeError("Trailing exploration sprite frame data");
    auto images=decode_ega(bytes,filename);
    if(images.size()!=bytes[0]) throw DecodeError("Exploration sprite distance frame count disagrees with source");
    ExplorationSpritePresentation result;
    result.frames_=std::move(images);result.disk_=disk;result.record_=record;result.distance_=distance;
    result.source_=filename+"/"+std::to_string(record);
    return result;
}
ExplorationSpritePresentation ExplorationSpritePresentation::load_script(const IFileSystem& files,
    unsigned disk,std::uint8_t record,std::uint8_t distance) {
    if(disk<1 || disk>8 || distance>2)throw DecodeError("Invalid script sprite source or distance");
    const auto filename="SPRIT"+std::to_string(disk)+".DAX";
    if(find_record(files,filename,record))return load(files,disk,record,distance);
    ExplorationSpritePresentation result;
    result.disk_=disk;result.record_=record;result.distance_=distance;
    result.source_=filename+"/"+std::to_string(record);
    return result;
}
ExplorationSpritePresentation ExplorationSpritePresentation::at_distance(std::uint8_t distance) const {
    if(source_.empty()) throw DecodeError("Exploration sprite cache is not loaded");
    if(distance>2) throw DecodeError("Exploration sprite distance exceeds the three native positions");
    auto result=*this;result.distance_=distance;return result;
}
void ExplorationSpritePresentation::draw(Framebuffer& frame) const {
    if(source_.empty()) throw DecodeError("Exploration sprite has no loaded resource");
    // Six selected-build records omit the farthest image. 0708 tests that
    // frame pointer and performs no blit, without substituting a nearer view.
    if(!visible()) return;
    const auto& image=frames_[distance_];
    // 4CE2:0708 adds2 to source X/Y units; 1464:0D71 resolves those units
    // to eight pixels. Native transparency leaves the current scene intact.
    const int left=16+image.x,top=16+image.y;
    for(int y=0;y<image.height;++y) for(int x=0;x<image.width;++x) {
        const auto color=image.pixels[static_cast<std::size_t>(y*image.width+x)];
        if(color>>24) frame[static_cast<std::size_t>((top+y)*screen_width+left+x)]=color;
    }
}
}
