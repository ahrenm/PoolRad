#include "gbengine/party/inventory.hpp"
#include "gbengine/assets/decoders.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>

namespace GBEngine {
Item Item::decode(std::span<const std::uint8_t> bytes,ItemOrigin origin) {
    if(bytes.size()!=63) throw DecodeError("Invalid 63-byte inventory record");
    Item result;result.origin_=std::move(origin);std::copy(bytes.begin(),bytes.end(),result.bytes_.begin());
    // 00..28 is a mutable Pascal string[40] display buffer. The native loader
    // clears/relinks 2A:2C, and the screen regenerates the name from 2F..31.
    std::fill(result.bytes_.begin(),result.bytes_.begin()+41,std::uint8_t{});
    std::fill(result.bytes_.begin()+0x2a,result.bytes_.begin()+0x2e,std::uint8_t{});
    return result;
}
Inventory Inventory::decode(std::span<const std::uint8_t> bytes) {
    if(bytes.size()%63 || bytes.size()/63>16) throw DecodeError("Invalid inventory record count or truncated item");
    Inventory result;
    for(std::size_t at=0;at<bytes.size();at+=63) result.items_.push_back(Item::decode(bytes.subspan(at,63)));
    return result;
}
std::string Item::ready_label() const {
    return StringStore::active().get(pascal_string_id("START.EXE:module",readied()?0xfcd:0xfd4));
}
std::string Item::name(bool detects_magic) const {
    // Resident 10BA:0442: visible name components are emitted in 31/30/2F
    // order. Bits 0/1/2 of 35 conceal those components respectively.
    auto& strings=StringStore::active();
    auto text=[&](std::size_t offset) -> const std::string& {return strings.get(pascal_string_id("START.EXE:module",offset));};
    std::string name;
    if(detects_magic && ((byte(0x32)>0 && byte(0x32)<128) || (byte(0x33)>0 && byte(0x33)<128) || cursed()))
        name=text(0xfdb)+text(0xfdd);
    if(quantity()) name+=std::to_string(quantity())+text(0xfdd);
    unsigned visible=0;
    for(unsigned i=1;i<=3;++i) if(byte(0x2e + i) && !(byte(0x35)&(1u<<(3-i)))) visible|=1u<<(i-1);
    bool pluralized=false;
    for(unsigned i=3;i>=1;--i) if(visible&(1u<<(i-1))) {
        const auto component=byte(0x2e + i);
        // Verified zero-length slots in this selected build's string[20] table.
        // They emit no text; they are not a missing-string fallback.
        if(component!=62 && component!=63 && component!=135 && component!=144 && component!=168)
            name+=text(0xd867+21u*component);
        if(quantity()<2 || pluralized) name+=text(0xfdd);
        else if((1u<<(i-1))==visible || (i==1 && visible>4 && byte(0x2e)!=0x56) ||
                (i==2 && !(visible&1)) || (i==3 && byte(0x2e)==0x56) ||
                ((byte(0x2e)==0x49 || byte(0x2e)==0x1c) && byte(0x31)!=0xb1)) {
            name+=text(0xfdf);pluralized=true;
        } else name+=text(0xfdd);
    }
    if(name.size()>40) name.resize(40);
    while(!name.empty() && name.back()==' ') name.pop_back();
    return strings.runtime_text(name);
}
}
