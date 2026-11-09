#include "gbengine/saves/original_save.hpp"
#include <algorithm>
#include <set>
namespace GBEngine {
OriginalSave OriginalSave::decode(std::span<const std::uint8_t> bytes) {
    if(bytes.size()!=file_size) throw DecodeError("Original campaign save must be exactly 13137 bytes");
    OriginalSave result;result.header_disk_=bytes[0];
    const auto words=[&](auto& bank,std::size_t at) {
        for(auto& word:bank) {word=read_u16(bytes,at);at+=2;}
    };
    words(result.banks_.area1,0x0001);words(result.banks_.area2,0x0801);words(result.banks_.area3,0x1001);
    result.script_.assign(bytes.begin()+0x1401,bytes.begin()+0x3201);
    std::copy_n(bytes.begin()+0x3201,5,result.location_.begin());
    result.presentation_mode_=bytes[0x3206];result.execution_mode_=bytes[0x3207];
    const auto count=bytes[0x3208];
    if(count==0 || count>8) throw DecodeError("Original campaign roster count must be 1..8");
    std::copy_n(bytes.begin()+0x3209,328,result.roster_storage_.begin());
    std::set<std::string> names;
    for(std::size_t i=0;i<count;++i) {
        const auto at=0x3209+41*i;const auto length=bytes[at];
        if(length==0 || length>8) throw DecodeError("Unsupported original character file stem length");
        std::string stem;
        for(std::size_t j=1;j<=length;++j) {
            const auto c=bytes[at+j];
            if(!((c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_')) throw DecodeError("Unsafe original character file stem");
            stem.push_back(static_cast<char>(c));
        }
        if(!names.insert(stem).second) throw DecodeError("Duplicate original campaign roster entry");
        result.roster_.push_back(std::move(stem));
    }
    if(result.header_disk_<1 || result.header_disk_>8 || result.resource_disk()<1 || result.resource_disk()>8)
        throw DecodeError("Invalid original campaign resource disk");
    // DAX record IDs are bytes, including zero (the adjoining ECL3/GEO3 map).
    // Existence is checked by the resource loader, not by a nonzero sentinel.
    if(result.ecl_record()>255 || result.geometry_record()>255)
        throw DecodeError("Invalid original campaign script or geometry record");
    // Validate the stored script's entry table, using a synthetic DAX prefix only
    // for this existing parser. The prefix is not saved or addressable VM data.
    Bytes record{0,0};record.insert(record.end(),result.script_.begin(),result.script_.end());
    (void)inspect_ecl_header(record);
    return result;
}
OriginalSave OriginalSave::load(const IFileSystem& files,char slot) {
    if(slot>='a' && slot<='j') slot=static_cast<char>(slot-'a'+'A');
    if(slot<'A' || slot>'J') throw DecodeError("Original campaign slot must be A..J");
    return decode(files.read_asset("SAVGAM"+std::string(1,slot)+".DAT"));
}
}
