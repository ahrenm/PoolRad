#include "gbengine/resources/string_extractor.hpp"
#include "gbengine/assets/decoders.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace GBEngine {
namespace {
void require_string(bool condition,const char* message) {if(!condition) throw StringError(message);}
std::string hex_id(std::size_t value,unsigned width) {std::ostringstream out;out<<std::uppercase<<std::hex<<std::setw(static_cast<int>(width))<<std::setfill('0')<<value;return out.str();}
bool printable(std::uint8_t c) {return (c>=32 && c<=126) || c==9 || c==10 || c==13;}
}
StringStore StringExtractor::extract(const IFileSystem& files,StringSourceScope scope) {
    StringStore store;
    const auto start=files.read_asset("START.EXE"),overlay=files.read_asset("GAME.OVR");
    store.sources_["START.EXE"]=sha256(start);store.sources_["GAME.OVR"]=sha256(overlay);store.validate_profile();
    const auto module=decode_start_data(start);
    auto pascal=[&](std::string_view source,std::span<const std::uint8_t> bytes) {
        // Candidates are addressable by physical location; a scan does not claim
        // every printable sequence is a semantically verified game message.
        for(std::size_t at=0;at<bytes.size();++at) {
            const auto length=bytes[at];if(!length || length>bytes.size()-at-1) continue;
            const auto text=bytes.subspan(at+1,length);
            if(std::ranges::all_of(text,printable)) store.strings_.emplace(pascal_string_id(source,at),std::string(text.begin(),text.end()));
        }
    };
    pascal("START.EXE:module",module);pascal("GAME.OVR",overlay);
    // Native25C7:12E8 explicitly prints this length-zero Pascal string.
    require_string(overlay.at(0x6826)==0,"Invalid native NPC notice prefix");
    store.strings_[pascal_string_id("GAME.OVR",0x6826)]={};
    std::size_t record_count=0;
    for(const auto& name:files.list_assets()) {
        if(name.starts_with("ECL") && name.ends_with(".DAX")) {
            const auto archive_bytes=files.read_asset(name);store.sources_[name]=sha256(archive_bytes);const DaxArchive archive(archive_bytes);
            for(const auto& record:archive.entries) {
                inspect_ecl_header(record.data);++record_count;
                const auto origin=name+":"+hex_id(record.id,3),hash=sha256(record.data);
                store.records_[origin]=hash;store.record_origins_.try_emplace(hash,origin);
                const auto script=std::span(record.data).subspan(2);
                for(std::size_t at=0;at+1<script.size();++at) if(script[at]==0x80 && script[at+1]<=script.size()-at-2) {
                    const auto text=decode_ecl_text(script.subspan(at+2,script[at+1]));
                    if(text.size()<=255) store.strings_.emplace(origin+":"+hex_id(0x9900+at,4),text);
                }
                for(std::size_t at=0;at<script.size();++at) {
                    std::size_t end=at;
                    while(end<script.size() && end-at<255 && script[end] && printable(script[end])) ++end;
                    if(end<script.size() && script[end]==0)
                        store.strings_.emplace(store.reference_id(origin,static_cast<std::uint16_t>(0x9900+at)),std::string(script.begin()+static_cast<std::ptrdiff_t>(at),script.begin()+static_cast<std::ptrdiff_t>(end)));
                }
            }
        } else if(name.size()==11 && name.starts_with("MON") && name[3]>='1' && name[3]<='8' && name.ends_with("CHA.DAX")) {
            const auto bytes=files.read_asset(name);store.sources_[name]=sha256(bytes);const DaxArchive archive(bytes);
            for(const auto& record:archive.entries) {
                require_string(record.data.size()==285 && record.data[0]<=15,"Invalid source NPC string record");
                const auto text=std::span(record.data).subspan(1,record.data[0]);
                require_string(std::ranges::all_of(text,printable),"Invalid source NPC name");
                store.strings_[pascal_string_id(name+":"+hex_id(record.id,3),0)]=std::string(text.begin(),text.end());
            }
        } else if(scope==StringSourceScope::full_profile && name.size()<=12 && name.ends_with(".SAV")) {
            const auto bytes=files.read_asset(name);require_string(bytes.size()==285 && bytes[0]<=15,"Invalid source character string record");
            const auto text=std::span(bytes).subspan(1,bytes[0]);require_string(std::ranges::all_of(text,printable),"Invalid source character name");
            store.sources_[name]=sha256(bytes);store.strings_[pascal_string_id(name,0)]=std::string(text.begin(),text.end());
        }
    }
    require_string(record_count==29,"String extraction requires all 29 selected ECL records");
    store.validate();return store;
}
}
