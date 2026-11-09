#include "gbengine/rendering/presentation.hpp"
#include "gbengine/assets/records.hpp"
#include "gbengine/app/runtime_timing.hpp"
#include <algorithm>

namespace GBEngine {
GameFont GameFont::load(const IFileSystem& files) {
    GameFont result;result.glyphs_=load_record(files,"8X8D1.DAX",201);
    if(result.glyphs_.size()!=glyph_count*8) throw DecodeError("Invalid native font size");
    return result;
}
void GameFont::draw_glyph(Framebuffer& frame,unsigned glyph,int left,int top,std::uint32_t color) const {
    if(glyph>=glyph_count || glyphs_.size()!=glyph_count*8) throw DecodeError("Native font glyph out of range");
    for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
        const int px=left+x,py=top+y;
        if(px>=0 && px<320 && py>=0 && py<200 && (glyphs_[glyph*8+static_cast<unsigned>(y)]&(128>>x)))
            frame[static_cast<std::size_t>(py*320+px)]=color;
    }
}
void GameFont::draw_caret(Framebuffer& frame,int left,int top,std::uint32_t color) {
    for(int y=6;y<8;++y)for(int x=0;x<8;++x)
        if(left+x>=0 && left+x<320 && top+y>=0 && top+y<200)
            frame[static_cast<std::size_t>((top+y)*320+left+x)]=color;
}
void GameFont::draw_text(Framebuffer& frame,std::string_view text,int left,int top,int columns,int lines,std::uint32_t color,bool word_wrap) const {
    if(columns<=0 || lines<=0) return;
    columns=std::min(columns,std::max(0,(320-left)/8));if(!columns) return;
    int col=0,row=0;
    for(std::size_t i=0;i<text.size() && row<lines;++i) {
        unsigned glyph=static_cast<unsigned char>(text[i]);
        if(glyph=='\n') {col=0;++row;continue;}
        if(word_wrap && glyph==' ' && col==0) continue;
        if(word_wrap && glyph!=1 && glyph!=' ' && (i==0 || text[i-1]==' ' || text[i-1]=='\n')) {
            const auto end=text.find_first_of(" \n",i);
            const auto length=(end==std::string_view::npos?text.size():end)-i;
            if(col && length<=static_cast<std::size_t>(columns) && col+static_cast<int>(length)>columns) {col=0;++row;}
            if(row>=lines) break;
        }
        if(glyph==1) {if(++i==text.size()) break;glyph=static_cast<unsigned char>(text[i]);}
        else glyph=text_glyph(static_cast<unsigned char>(glyph));
        if(col==columns) {col=0;if(++row==lines) break;}
        draw_glyph(frame,glyph,left+col*8,top+row*8,color);++col;
    }
}
namespace {
void draw_image(Framebuffer& target,const Image& image,int left,int top) {
    for(int y=0;y<image.height;++y) for(int x=0;x<image.width;++x) {
        const int px=left+x,py=top+y;
        if(px>=0 && px<320 && py>=0 && py<200)
            target[static_cast<std::size_t>(py*320+px)]=image.pixels[static_cast<std::size_t>(y*image.width+x)];
    }
}
}
PicturePresentation PicturePresentation::load(const IFileSystem& files,unsigned disk,std::uint8_t picture,std::uint8_t head) {
    PicturePresentation result;
    result.disk_=disk;result.picture_=picture;result.head_=head;
    if(head==255) {
        const auto filename="PIC"+std::to_string(disk)+".DAX";
        const auto bytes=load_record(files,filename,picture);
        return decode_picture(disk,picture,bytes);
    } else {
        const auto head_file="HEAD"+std::to_string(disk)+".DAX";
        const auto body_file="BODY"+std::to_string(disk)+".DAX";
        auto heads=decode_ega(load_record(files,head_file,head),head_file);
        auto bodies=decode_ega(load_record(files,body_file,picture),body_file);
        if(heads.size()!=1 || bodies.size()!=1 || heads[0].width!=88 || heads[0].height!=40 ||
           bodies[0].width!=88 || bodies[0].height!=48) throw DecodeError("Unsupported composite portrait dimensions");
        result.frames_=std::move(heads);result.body_=std::move(bodies[0]);
        result.source_=head_file+"/"+std::to_string(head)+" + "+body_file+"/"+std::to_string(picture);
    }
    return result;
}
PicturePresentation PicturePresentation::decode_picture(unsigned disk,std::uint8_t picture,std::span<const std::uint8_t> bytes) {
    PicturePresentation result;result.disk_=disk;result.picture_=picture;
    const auto filename="PIC"+std::to_string(disk)+".DAX";
    result.frames_=decode_ega(bytes,filename);
    // Native PIC containers have four-byte centisecond durations followed
    // by the existing 17-byte EGA header per frame. Do not guess another layout.
    if(bytes.empty() || bytes[0]!=result.frames_.size() || result.frames_.size()>8)
        throw DecodeError("Unsupported PIC animation container");
    std::size_t at=1;
    for(const auto& image:result.frames_) {
        if(image.width!=88 || image.height!=88) throw DecodeError("Unsupported exploration picture dimensions");
        const auto centiseconds=read_u32(bytes,at);
        if(centiseconds>0xffffffffu/10) throw DecodeError("Picture frame duration overflow");
        result.durations_.push_back(centiseconds*10);
        at+=21+image.pixels.size()/2;
    }
    if(at!=bytes.size()) throw DecodeError("Invalid PIC frame boundaries");
    result.source_=filename+"/"+std::to_string(picture);
    return result;
}
std::optional<PicturePresentation> PicturePresentation::load_sprite_closeup(const IFileSystem& files,unsigned disk,std::uint8_t picture,std::uint8_t head) {
    if(head!=255)return load(files,disk,picture,head);
    const auto bytes=find_record(files,"PIC"+std::to_string(disk)+".DAX",picture);
    if(!bytes)return std::nullopt;
    return decode_picture(disk,picture,*bytes);
}
PicturePresentation PicturePresentation::load_ambient_loop(const IFileSystem& files,unsigned disk,std::uint8_t picture) {
    auto result=load(files,disk,picture,255);result.ambient_loop_=true;return result;
}
void PicturePresentation::draw(Framebuffer& target) const {
    if(frames_.empty()) throw DecodeError("Picture presentation has no frame");
    // Shared logical backbuffer coordinates. Native direct video presentation
    // adds an eight-pixel border; the replacement SDL canvas already owns its frame.
    draw_image(target,frames_.at(frame_),16,16);
    if(body_) draw_image(target,*body_,16,56);
}
void PicturePresentation::advance(std::uint64_t delta) {
    if(frames_.size()<2) return;
    if(ambient_loop_) {
        static_assert(RuntimeTiming::ambient_frame_ms>0);
        constexpr auto duration=RuntimeTiming::ambient_frame_ms;
        // Ambient loops have no gameplay markers. Resolve their phase in O(1),
        // retaining the remainder without replaying missed frames after a stall.
        // Split the quotient first so even a maximum elapsed delta cannot wrap.
        const auto steps=(delta/duration)%frames_.size();
        const auto partial=elapsed_+delta%duration;
        frame_=(frame_+steps+partial/duration)%frames_.size();
        elapsed_=partial%duration;
        return;
    }
    // Native menu polling advances at most once, then resets its timestamp.
    // Zero means advance on this poll, not an invented minimum millisecond delay.
    const auto duration=durations_[frame_];
    if(delta>=duration-elapsed_) {frame_=(frame_+1)%frames_.size();elapsed_=0;}
    else elapsed_+=delta;
}
}
