#include "gbengine/resources/string_store.hpp"
#include "gbengine/platform/paths.hpp"
#include "gbengine/resources/string_extractor.hpp"
#include "gbengine/assets/decoders.hpp"
#include "tomlplusplus/toml.hpp"
#include <algorithm>
#include <bit>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <chrono>

namespace GBEngine {
namespace {
thread_local StringStore* current_store=nullptr;
void require_string(bool condition,const char* message) {if(!condition) throw StringError(message);}
std::string hex_id(std::size_t value,unsigned width) {std::ostringstream out;out<<std::uppercase<<std::hex<<std::setw(static_cast<int>(width))<<std::setfill('0')<<value;return out.str();}
std::string hash_text(std::string_view text) {return sha256({reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});}
bool printable(std::uint8_t c) {return (c>=32 && c<=126) || c==9 || c==10 || c==13;}
void profile(const std::map<std::string,std::string>& sources) {
    require_string(sources.contains("START.EXE") && sources.contains("GAME.OVR"),"String sources are incomplete");
    require_string(sources.at("START.EXE")=="d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd" &&
                   sources.at("GAME.OVR")=="872964a32cef0fc32bbf83bab68b15e04ad36d59076f37b00a179dfee7928614","Unsupported START.EXE / GAME.OVR string extraction profile");
}
}
std::string sha256(std::span<const std::uint8_t> input) {
    constexpr std::array<std::uint32_t,64> k{
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::array<std::uint32_t,8> h{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    Bytes bytes(input.begin(),input.end());const auto bits=static_cast<std::uint64_t>(bytes.size())*8;
    bytes.push_back(128);while(bytes.size()%64!=56) bytes.push_back(0);
    for(int shift=56;shift>=0;shift-=8) bytes.push_back(static_cast<std::uint8_t>(bits>>shift));
    for(std::size_t at=0;at<bytes.size();at+=64) {
        std::array<std::uint32_t,64> w{};
        for(unsigned i=0;i<16;++i) for(unsigned j=0;j<4;++j) w[i]=(w[i]<<8)|bytes[at+i*4+j];
        for(unsigned i=16;i<64;++i) {
            const auto x=w[i-15],y=w[i-2];
            w[i]=w[i-16]+(std::rotr(x,7)^std::rotr(x,18)^(x>>3))+w[i-7]+(std::rotr(y,17)^std::rotr(y,19)^(y>>10));
        }
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for(unsigned i=0;i<64;++i) {
            const auto t1=hh+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
            const auto t2=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));
            hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(const auto word:h) out<<std::setw(8)<<word;return out.str();
}
std::string pascal_string_id(std::string_view source,std::size_t offset) {return std::string(source)+":"+hex_id(offset,8);}
StringStore& StringStore::active() {require_string(current_store!=nullptr,"Runtime string store has not been initialized");return *current_store;}
StringStore* StringStore::bind(StringStore* store) {auto* previous=current_store;current_store=store;return previous;}
const std::string& StringStore::get(std::string_view id) const {
    const std::string key(id);
    if(const auto it=strings_.find(key);it!=strings_.end()) return it->second;
    if(const auto it=runtime_.find(key);it!=runtime_.end()) return it->second;
    throw StringError("Missing runtime string ID: "+key);
}
std::string StringStore::runtime_id(std::string_view text) {
    const auto id="runtime:"+hash_text(text);runtime_.try_emplace(id,text);return id;
}
std::string StringStore::record_origin(std::span<const std::uint8_t> record) const {
    const auto hash=sha256(record);const auto it=record_origins_.find(hash);
    return it==record_origins_.end()?"runtime:"+hash:it->second;
}
std::string StringStore::ecl_id(std::string_view origin,std::uint16_t address,std::span<const std::uint8_t> packed,bool original) {
    if(original && !origin.starts_with("runtime:")) {
        const auto id=std::string(origin)+":"+hex_id(address,4);(void)get(id);return id;
    }
    return runtime_id(decode_ecl_text(packed));
}
std::string StringStore::reference_id(std::string_view origin,std::uint16_t address) const {return std::string(origin)+":str:"+hex_id(address,4);}
StringStore StringStore::extract(const IFileSystem& files,StringSourceScope scope) {return StringExtractor::extract(files,scope);}
void validate_text_sources(const IFileSystem& files,std::ostream* log) {
    std::map<std::string,std::string> sources;
    for(const auto* name:{"START.EXE","GAME.OVR"}) {
        const auto hash=sha256(files.read_asset(name));sources.emplace(name,hash);
        if(log)*log<<name<<" SHA256 "<<hash<<'\n';
    }
    profile(sources);
    if(log)*log<<"DOS text source hashes match the supported profile.\n";
}
void StringStore::validate_profile() const {profile(sources_);}
void StringStore::validate() const {
    profile(sources_);require_string(records_.size()==29 && strings_.size()>100,"Incomplete extracted string store");
    for(unsigned disk=1;disk<=8;++disk)
        require_string(sources_.contains("MON"+std::to_string(disk)+"CHA.DAX"),"String cache lacks NPC sources; remove cache to re-extract");
    for(const auto& [id,text]:strings_) {
        require_string(!id.empty() && text.size()<=255,"Invalid extracted string entry");
        require_string(std::ranges::all_of(text,[](unsigned char c){return printable(c);}),"Unsupported extracted string encoding");
    }
    for(unsigned i=0;i<9;++i) require_string(!get(pascal_string_id("START.EXE:module",0xc7c0+0x1047+13*i)).empty(),"Missing status string");
    for(unsigned i=1;i<=56;++i) (void)get(pascal_string_id("START.EXE:module",0xf0a9+41*i));
    // Five native item-table slots have length zero; the Pascal candidate scan
    // intentionally stores only nonempty strings.
    for(unsigned i=1;i<256;++i) if(i!=62 && i!=63 && i!=135 && i!=144 && i!=168)
        (void)get(pascal_string_id("START.EXE:module",0xd867+21*i));
    for(const auto offset:{0xfcdu,0xfd4u,0xfdbu,0xfddu,0xfdfu,0xd4f8u,0xd6c3u,0xd713u,0xd7acu}) (void)get(pascal_string_id("START.EXE:module",offset));
    for(const auto offset:{0x12fbu,0x1300u,0xcc76u,0xcc9fu,0x3494u,0x3496u,0x3498u,0x34a3u,0xed68u,0xed6eu,0xed74u,0xed7au})
        require_string(!get(pascal_string_id("START.EXE:module",offset)).empty(),"Missing exploration UI text");
    for(const auto offset:{0x21040u,0x21045u,0x2104au,0x2104fu,0x21054u,0x21059u,0x2105eu,0x21063u,0x2106bu,0x2107du,
                          0x215f8u,0x215ffu,0x21606u,0x2160fu,0x21611u,0x21613u,0x21615u,0x21624u,0x21636u,0x21ef2u})
        (void)get(pascal_string_id("GAME.OVR",offset));
    for(const auto offset:{0x2103au,0x21069u,0x21070u,0x21077u,0x21932u,0x21934u,0x21937u,0x21939u,
                          0x21aa6u,0x21aadu,0x21ab5u,0x21abcu,0x21ac2u,0x21ac7u})
        (void)get(pascal_string_id("GAME.OVR",offset));
    for(const auto offset:{0x32eeu,0x9554u,0x955cu,0x9569u,0x347cu,0x1dcbu,0x1df0u,0x16d8u,0x17046u}) (void)get(pascal_string_id("GAME.OVR",offset));
    // Recovered casting UI and effect-branch feedback; stale caches must fail
    // before a spell can commit and attempt to present a missing message.
    for(const auto offset:{0x23b3u,0x37b9u,0x37dfu,0x37e1u,0x2ec6u,0x2ed6u}) (void)get(pascal_string_id("START.EXE:module",offset));
    for(const auto offset:{0xccc8u,0xccf1u,0xcd1au}) (void)get(pascal_string_id("START.EXE:module",offset));
    (void)get(pascal_string_id("START.EXE:module",0xcd44));
    (void)get(pascal_string_id("START.EXE:module",0xcd6d));
    for(const auto offset:{0x18f71u,0x18f9fu,0x190a8u,0x190b6u,0x190c3u,0x190d5u,0x190e7u,0x190fcu})
        (void)get(pascal_string_id("GAME.OVR",offset));
    for(const auto offset:{0x17d9fu,0x17dafu,0x17dc2u,0x18108u,0x18300u,0x180f0u,0x18123u,0x182eau,0x18376u,
                          0x2397du,0x23995u,0x239afu,0x239bfu,0x243afu,0x243bau,0x244a5u,0x24623u,0x246e3u,
                          0x247a7u,0x24a36u,0x24a45u,0x2701du,0x27026u})
        (void)get(pascal_string_id("GAME.OVR",offset));
    for(const auto offset:{0x17b53u,0x17b7au,0x17b8au,0x18318u,0x18334u,0x18357u})
        (void)get(pascal_string_id("GAME.OVR",offset));
    for(const auto offset:{0x186efu,0x186f1u,0x186f9u,0x1871au,0x18740u,0x1874du,0x18756u,0x1875du,
                          0x1876bu,0x18781u,0x1878au,0x18793u,0x187a0u,0x187b0u,0x187bau,0x187c6u,0x187d0u,0x187e4u,
                          0x19441u,0x19449u,0x19457u,0x19466u,0x19475u,0x19485u})
        (void)get(pascal_string_id("GAME.OVR",offset));
    for(const auto offset:{0x19654u,0x1968cu,0x19693u,0x1fcabu,0x1fcbdu,0x1fd11u,0x2d979u})
        (void)get(pascal_string_id("GAME.OVR",offset));
    for(const auto offset:{0x23154u,0x2315eu,0x23160u,0x23175u,0x2317fu,0x234b0u,0x234b2u,0x234c7u,0x234d1u,
                          0x250cau,0x257e3u,0x257e9u,0x257efu,0x257f9u,0x25803u,0x2580bu,0x25813u,
                          0x26569u,0x2657cu,0x26581u,0x26587u,0x26599u,0x265acu,0x265cbu,0x265d2u,0x265dcu,
                          0x265e2u,0x265eeu,0x26604u,0x26609u,0x2660eu,0x26618u,0x26623u})
        (void)get(pascal_string_id("GAME.OVR",offset));
    for(unsigned level=1;level<=7;++level) (void)get(pascal_string_id("START.EXE:module",0xd239+41*level));
    for(const auto offset:{0x239ceu,0x23973u,0x27053u,0x27018u,0x272a2u,0x2d657u,0x2d65du,0x2d663u,0x27a96u,0x17cbau,
                          0x2801cu,0x280f5u,0x2812eu,0x28168u,0x283e4u,0x284a5u,0x287efu,0x28a21u,0x28a94u,0x294e3u,
                          0x29649u,0x298a0u,0x2c532u,0x282b1u,0x28f4du,0x29227u,0x29391u,0x29536u,0x29543u})
        (void)get(pascal_string_id("GAME.OVR",offset));
    // Combat casting, cancellation, interruption and generated-weapon feedback.
    for(const auto offset:{0x14c00u,0x14bf0u,0x2a31du,0x2a32bu,0x2a0e7u,0x103b9u,0x108e8u,0x108f1u,0x108feu,0x10abeu,0x10ac7u,0x1186cu,0x27c1cu,0x27c2au,0x148a8u,0x12aa6u,0x2c1deu,0xf42du,0xf6b4u,0xf903u,0xf60du,0xf76cu})
        (void)get(pascal_string_id("GAME.OVR",offset));
    // All recovered combat handler feedback must exist before a cast commits.
    for(const auto offset:{0x2c171u,0x2c178u,0x2c18bu,0x2c1a4u,0x2c1aeu,0x2c1c9u,0x2c1b8u,0x2c1d3u,0x2c224u,0x2c1ebu,0x2c1f5u})
        (void)get(pascal_string_id("GAME.OVR",offset));
    for(const auto offset:{0xf60du,0xf6b4u,0xf8a1u,0x2801cu,0x2804eu,0x280f5u,0x2812eu,0x28168u,
                          0x281dcu,0x281eau,0x284a5u,0x28526u,0x28668u,0x287efu,0x2882eu,0x28867u,
                          0x2891cu,0x28a21u,0x28a94u,0x28ad8u,0x28b11u,0x2907bu,0x2926cu,0x29358u,
                          0x29391u,0x294e3u,0x2960bu,0x29649u,0x298a0u,0x29c17u,0x29e10u,0x29e5bu,
                          0x29fa4u,0x29fdeu,0x2a035u,0x2b9a5u,0x2b9b5u,0x2c532u,0x2c732u,0x2c746u})
        (void)get(pascal_string_id("GAME.OVR",offset));
    const auto& parlay=get(pascal_string_id("GAME.OVR",0x32ee));
    for(unsigned i=0;i<7;++i) (void)get(pascal_string_id("START.EXE:module",0xd7ba+11*i));
    for(const auto offset:{0x6cf3u,0x6cf8u,0x6d00u,0x7148u,0x716fu,0x718bu,0x7194u,0x71e7u,0x26609u,
                          0x6080u,0x62a5u,0x63acu,0x63bbu,0x63ceu,0x63e3u,0x63f2u,0x63fbu,0x641bu,
                          0x5e11u,0x5e45u,0x5e5eu,0x5e71u,0x25946u,0x2595au,0x25964u,
                          0x22cc5u,0x22cd4u,0x22cebu,0x22cfau})
        (void)get(pascal_string_id("GAME.OVR",offset));
    for(const auto offset:{17783u,17901u,17918u,17932u,17946u,17964u,18339u,18353u,18502u,18519u,18749u,18767u,
                          18787u,19055u,19068u,19621u,19638u,19861u,19876u,20097u,20112u,20269u,20292u,
                          20918u,20958u,20988u,20997u,21072u,182066u,182086u,0x6826u,0x6827u,0x6843u})
        (void)get(pascal_string_id("GAME.OVR",offset));
    (void)get(pascal_string_id("START.EXE:module",0x321d));
    for(const auto offset:{0xa1d8u,0x1344au,0x13453u,0xa605u,0xa60cu,0xa618u,0xa621u,0xa7fau,0x1392bu,0x1393bu,0x13945u,0x13952u,
                          0x15185u,0x15264u,0x15270u,0x15278u,0x1528au,0x15296u,0x155c2u,0x155ceu,0x155dau,0x155e2u,
                          0xabc0u,0xbc7au,0xbc8cu,0xf35eu,0x11ba6u,0x11d76u,0xbb6eu,0xbb77u,
                          0x5dbbu,0x5dd3u,0x5deau,0x5dfdu,0x5e2fu,0x6085u,0x608du,0x629eu,0x63c1u,
                          0x6a4cu,0x6a55u,0x6a8bu,0x8f3du})
        (void)get(pascal_string_id("GAME.OVR",offset));
    for(unsigned action=0;action<10;++action) (void)get(pascal_string_id("START.EXE:module",51418+41*action));
    for(const auto offset:{0x1d148u,0x1d14cu,0x1d15bu,0x1d15fu,0x1d165u,0x1d16bu,0x1d170u,0x1d19au})
        (void)get(pascal_string_id("GAME.OVR",offset));
    for(const auto offset:{0xcfa8u,0xcfd1u,0xcffau,0xd023u,0xd04cu})
        (void)get(pascal_string_id("START.EXE:module",offset));
    for(const auto offset:{60507u+0x20df,60507u+0x20e7,60507u+0x20ef,60507u+0x20fb,60507u+0x2118})
        (void)get(pascal_string_id("GAME.OVR",offset));
    require_string(std::count(parlay.begin(),parlay.end(),'~')==5,"Invalid extracted fixed-menu layout");
}
std::string StringStore::content_hash() const {
    std::string content;
    for(const auto* table:{&sources_,&records_,&strings_}) for(const auto& [key,value]:*table) {
        content+=std::to_string(key.size())+":"+key+std::to_string(value.size())+":"+value;
    }
    return hash_text(content);
}
void StringStore::save(const std::filesystem::path& path) const {
    validate();toml::table document;
    document.insert("format_version",2);document.insert("entry_count",static_cast<std::int64_t>(strings_.size()));document.insert("content_sha256",content_hash());
    auto table=[](const auto& values) {toml::table out;for(const auto& [key,value]:values) out.insert(key,value);return out;};
    document.insert("sources",table(sources_));document.insert("ecl_records",table(records_));document.insert("strings",table(strings_));
    // Escape CR/LF and quotes explicitly; TOML multiline newline normalization
    // must not change the byte-oriented original text during a round trip.
    std::ofstream file(path,std::ios::binary);file<<toml::toml_formatter(document,toml::format_flags::none)<<'\n';file.close();require_string(static_cast<bool>(file),"Could not write extracted strings TOML");
}
StringStore StringStore::load(const IFileSystem& files,const std::filesystem::path& path,StringSourceScope scope) {
    require_string(std::filesystem::file_size(path)<=64*1024*1024,"Strings TOML exceeds size limit");
    std::ifstream input(path,std::ios::binary);require_string(static_cast<bool>(input),"Could not read extracted strings TOML");
    const auto document=toml::parse(input);
    require_string(document["format_version"].value<std::int64_t>()==2,"Unsupported strings TOML version");
    StringStore store;
    auto read=[&](const char* key,auto& destination) {
        const auto* table=document[key].as_table();require_string(table!=nullptr,"Missing strings TOML table");
        for(const auto& [id,value]:*table) {const auto text=value.template value<std::string>();require_string(text.has_value(),"Non-string TOML entry");destination.emplace(std::string(id.str()),*text);}
    };
    read("sources",store.sources_);read("ecl_records",store.records_);read("strings",store.strings_);store.validate();
    require_string(document["entry_count"].value<std::int64_t>()==static_cast<std::int64_t>(store.size()),"String cache entry count mismatch");
    require_string(document["content_sha256"].value<std::string>()==store.content_hash(),"String cache content checksum mismatch");
    const auto character_source=[](std::string_view name){return name.size()<=12&&name.ends_with(".SAV");};
    for(const auto& [name,hash]:store.sources_) {
        if(scope==StringSourceScope::game_resources&&character_source(name))continue;
        require_string(sha256(files.read_asset(name))==hash,"String cache source fingerprint mismatch; remove cache to extract from selected data");
    }
    if(scope==StringSourceScope::full_profile)for(const auto& name:files.list_assets())if(character_source(name))
        require_string(store.sources_.contains(name)&&store.strings_.contains(pascal_string_id(name,0)),"String cache lacks original character sources; remove cache to extract from selected data");
    for(const auto& [origin,hash]:store.records_) store.record_origins_.try_emplace(hash,origin);
    return store;
}
void StringStore::prepare_original_roster(const IFileSystem& files,std::span<const std::string> stems,const std::filesystem::path& cache,std::ostream& log) {
    struct RosterFiles final:IFileSystem {
        const IFileSystem& base;std::vector<std::string> names;
        RosterFiles(const IFileSystem& f,std::span<const std::string> roster):base(f) {
            for(const auto& stem:roster)names.push_back(stem+".SAV");
        }
        Bytes read_asset(std::string_view name) const override {return base.read_asset(name);}
        std::vector<std::string> list_assets() const override {
            auto assets=base.list_assets();
            std::erase_if(assets,[&](const auto& name){return name.ends_with(".SAV") && std::find(names.begin(),names.end(),name)==names.end();});
            return assets;
        }
    } selected(files,stems);
    const auto extracted=load_or_extract(selected,cache,log,StringSourceScope::full_profile);
    for(const auto& name:selected.names) {
        const auto id=pascal_string_id(name,0);
        require_string(extracted.sources_.contains(name),"Original roster lacks required source character string");
        const auto& text=extracted.get(id);
        const auto found=strings_.find(id);
        require_string(found==strings_.end() || found->second==text,"Original character source name conflicts with the running campaign");
    }
    for(const auto& name:selected.names) {
        strings_[pascal_string_id(name,0)]=extracted.get(pascal_string_id(name,0));
        sources_[name]=extracted.sources_.at(name);
    }
}
StringStore StringStore::load_or_extract(const IFileSystem& files,const std::filesystem::path& path,std::ostream& log,StringSourceScope scope) {
    if(const auto* native=dynamic_cast<const NativeFiles*>(&files)) {
        const auto root=std::filesystem::canonical(native->root());
        for(auto parent=std::filesystem::weakly_canonical(std::filesystem::absolute(path));!parent.empty();) {
            require_string(!std::filesystem::exists(parent) || !std::filesystem::equivalent(parent,root),"String cache must be outside the read-only game data directory");
            const auto next=parent.parent_path();if(next==parent)break;parent=next;
        }
    }
    if(!std::filesystem::exists(path)) {
        log<<"Extracting runtime strings from selected binaries and game data...\n";
        const auto extracted=extract(files,scope);
        if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        // First launches may extract the same cache concurrently. Each writer
        // owns a staging directory, so one cannot truncate/remove another's
        // pending file. A verified winner is reused rather than deleted.
        static std::atomic<std::uint64_t> sequence{0};
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path staging;
        for(unsigned attempt=0;attempt<128;++attempt) {
            auto candidate=path;candidate+=".pending-"+std::to_string(stamp)+"-"+std::to_string(sequence.fetch_add(1));
            if(std::filesystem::create_directory(candidate)){staging=std::move(candidate);break;}
        }
        require_string(!staging.empty(),"Cannot reserve a private string-cache staging directory");
        const auto temporary=staging/"cache.toml";
        const auto cleanup=[&] {std::error_code error;std::filesystem::remove(temporary,error);std::filesystem::remove(staging,error);};
        try {
            extracted.save(temporary);(void)load(files,temporary,scope);
            if(std::filesystem::exists(path))(void)load(files,path,scope);
            else {
                std::error_code error;std::filesystem::rename(temporary,path,error);
                if(error) {
                    if(!std::filesystem::exists(path))throw std::filesystem::filesystem_error("Publish string cache",temporary,path,error);
                    (void)load(files,path,scope);
                }
            }
            cleanup();
        }catch(...) {cleanup();throw;}
        log<<"Extracted "<<extracted.size()<<" source string entries to "<<path_utf8(path)<<'\n';
    }
    auto store=load(files,path,scope);log<<"Loaded "<<store.size()<<" runtime string entries from "<<path_utf8(path)<<'\n';return store;
}
}
