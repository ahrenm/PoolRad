#include "gbengine/rendering/character_portrait.hpp"
#include "gbengine/assets/records.hpp"
#include "gbengine/party/party.hpp"
#include <algorithm>

namespace GBEngine {
CharacterPortrait CharacterPortrait::load(const IFileSystem& files,const Character& character) {
    CharacterPortrait result;
    const auto head=character.byte(0xbb),body=character.byte(0xbc);
    //4CE2:04F9 suppresses the entire portrait if either selector is zero.
    if(!head || !body) return result;
    //393E:33E7 cycles through these positive ranges. Reject corrupted selectors
    //rather than copying the native unchecked read into adjacent resident data.
    if(head>14 || body>12) throw DecodeError("Character portrait selector outside native range");
    const auto resident=decode_start_data(files.read_asset("START.EXE"));
    constexpr std::size_t data_segment=0xc7c0;
    const auto head_id=resident.at(data_segment+0x286f+head);
    const auto body_id=resident.at(data_segment+0x287d+body);
    auto heads=decode_ega(load_record(files,"HEAD3.DAX",head_id),"HEAD3.DAX");
    auto bodies=decode_ega(load_record(files,"BODY3.DAX",body_id),"BODY3.DAX");
    if(heads.size()!=1 || bodies.size()!=1 || heads[0].width!=88 || heads[0].height!=40 ||
       bodies[0].width!=88 || bodies[0].height!=48)
        throw DecodeError("Unsupported native character portrait dimensions");
    result.head_=std::move(heads[0]);result.body_=std::move(bodies[0]);
    result.head_record_=head_id;result.body_record_=body_id;
    return result;
}

void CharacterPortrait::draw(Framebuffer& frame,int left,int top) const {
    if(!visible()) return;
    if(left<0 || top<0 || left>screen_width-88 || top>screen_height-88)
        throw DecodeError("Character portrait placement outside reference screen");
    const auto paint=[&](const Image& image,int y){
        for(int row=0;row<image.height;++row)
            std::copy_n(image.pixels.begin()+static_cast<std::ptrdiff_t>(row*88),88,
                        frame.begin()+static_cast<std::ptrdiff_t>((y+row)*screen_width+left));
    };
    //4CE2:0050/0070 draw the head first and the body five text rows below it.
    //There is no sprite color key or combat palette exchange in this path.
    paint(*head_,top);paint(*body_,top+40);
}
}
