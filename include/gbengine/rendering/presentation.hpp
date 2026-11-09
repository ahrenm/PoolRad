#pragma once
#include "gbengine/assets/decoders.hpp"
#include <optional>
namespace GBEngine {
class GameFont {
    Bytes glyphs_;
public:
    static GameFont load(const IFileSystem&);
    void draw_glyph(Framebuffer&,unsigned glyph,int left,int top,std::uint32_t color=0xffffffffu) const;
    void draw_text(Framebuffer&,std::string_view,int left,int top,int columns,int lines,
                   std::uint32_t color=0xff55ff55u,bool word_wrap=false) const;
    // Pixel underline, independent of the source font character mapping.
    static void draw_caret(Framebuffer&,int left,int top,std::uint32_t color=0xffffffffu);
    static constexpr unsigned text_glyph(unsigned char ch) {
        return (ch>='a' && ch<='z'?ch-32:ch)%64;
    }
    static constexpr std::uint32_t green=0xff55ff55u,yellow=0xffffff55u;
    static constexpr unsigned glyph_count=177;
};
class PicturePresentation {
    friend class PortSave;
    unsigned disk_{};
    std::uint8_t picture_{},head_=255;
    std::vector<Image> frames_;
    std::vector<std::uint32_t> durations_;
    std::size_t frame_{};
    std::uint64_t elapsed_{};
    std::optional<Image> body_;
    std::string source_;
    bool ambient_loop_=false;
    static PicturePresentation decode_picture(unsigned,std::uint8_t,std::span<const std::uint8_t>);
public:
    static PicturePresentation load(const IFileSystem&,unsigned disk,std::uint8_t picture,std::uint8_t head);
    // Native sprite close-ups can name an absent PIC record: no blit, retaining
    // already drawn pixels. Missing archives/corruption and composites stay strict.
    static std::optional<PicturePresentation> load_sprite_closeup(const IFileSystem&,unsigned disk,std::uint8_t picture,std::uint8_t head);
    // Fixed ambient category cadence, preserving the original image metadata.
    static PicturePresentation load_ambient_loop(const IFileSystem&,unsigned disk,std::uint8_t picture);
    void draw(Framebuffer&) const;
    void advance(std::uint64_t elapsed_ms);
    std::size_t frame_index() const { return frame_; }
    std::size_t frame_count() const { return frames_.size(); }
    bool composite() const { return body_.has_value(); }
    const std::string& source() const { return source_; }
};
}
