#pragma once
#include "gbengine/assets/decoders.hpp"
#include <optional>

namespace GBEngine {
class Character;

// The native View portrait uses the character's BB/BC selectors and the
// resident lookup tables, independently of the campaign's resource disk.
class CharacterPortrait {
    std::optional<Image> head_,body_;
    std::optional<std::uint8_t> head_record_,body_record_;
public:
    static CharacterPortrait load(const IFileSystem&,const Character&);
    bool visible() const {return head_.has_value();}
    std::optional<std::uint8_t> head_record() const {return head_record_;}
    std::optional<std::uint8_t> body_record() const {return body_record_;}
    // Paints an opaque88x88 composite. The caller owns the surrounding chrome.
    // Native View placement is(224,8), with the body beginning40pixels lower.
    void draw(Framebuffer&,int left,int top) const;
};
}
