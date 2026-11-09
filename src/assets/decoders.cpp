#include "gbengine/assets/decoders.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>

namespace GBEngine {
namespace {
void require(bool condition, const char* message) { if (!condition) throw DecodeError(message); }
std::string upper(std::string_view name) {
    std::string s(name);
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
constexpr std::array<std::uint32_t, 16> ega{
    0xff000000,0xff0000aa,0xff00aa00,0xff00aaaa,0xffaa0000,0xffaa00aa,0xffaa5500,0xffaaaaaa,
    0xff555555,0xff5555ff,0xff55ff55,0xff55ffff,0xffff5555,0xffff55ff,0xffffff55,0xffffffff};
}
Bytes decode_start_data(std::span<const std::uint8_t> b) {
    require(b.size()==65231 && read_u16(b,0)==0x5a4d,"Unsupported START data profile");
    const auto header=static_cast<std::size_t>(read_u16(b,8))*16;
    const auto cs=static_cast<std::size_t>(read_u16(b,22));
    const auto pack=header+cs*16;
    require(pack+18<=b.size() && read_u16(b,20)==0x12 && read_u16(b,pack+16)==0x4252,"Invalid START EXEPACK header");
    const auto skip=static_cast<std::size_t>(read_u16(b,pack+14));
    const auto paras=static_cast<std::size_t>(read_u16(b,pack+12));
    require(cs>=skip && paras>=skip,"Invalid EXEPACK paragraphs");
    const auto module=b.subspan(header);
    std::size_t source=(cs-skip)*16+16,destination=(paras-skip)*16+16;
    require(source<=module.size() && destination==68592,"Unsupported START data size");
    Bytes out(destination);
    for(unsigned n=0;n<16 && source && module[source-1]==255;++n) --source;
    for(;;) {
        require(source>=3,"Truncated EXEPACK run");
        const auto marker=module[source-1];const auto count=read_u16(module,source-3);source-=3;
        require(count && count<=destination,"Invalid EXEPACK run length");
        if((marker&254)==0xb0) {
            require(source>0,"Truncated EXEPACK repeat");
            std::fill(out.begin()+static_cast<std::ptrdiff_t>(destination-count),out.begin()+static_cast<std::ptrdiff_t>(destination),module[--source]);
        } else if((marker&254)==0xb2) {
            require(count<=source,"Truncated EXEPACK literal");source-=count;
            std::copy_n(module.begin()+static_cast<std::ptrdiff_t>(source),count,out.begin()+static_cast<std::ptrdiff_t>(destination-count));
        } else throw DecodeError("Unknown EXEPACK marker");
        destination-=count;
        if(marker&1) break;
    }
    require(destination==source,"EXEPACK prefix mismatch");
    std::copy_n(module.begin(),source,out.begin());return out;
}
std::uint16_t read_u16(std::span<const std::uint8_t> b, std::size_t p) {
    require(p <= b.size() && b.size()-p >= 2, "Truncated 16-bit field");
    return static_cast<std::uint16_t>(b[p] | (b[p+1] << 8));
}
std::uint32_t read_u32(std::span<const std::uint8_t> b, std::size_t p) {
    require(p <= b.size() && b.size()-p >= 4, "Truncated 32-bit field");
    return read_u16(b,p) | (static_cast<std::uint32_t>(read_u16(b,p+2)) << 16);
}
Bytes decompress_dax(std::span<const std::uint8_t> input, std::size_t expected) {
    require(expected <= 65535, "DAX decoded size exceeds format limit");
    Bytes output;
    output.reserve(expected);
    for (std::size_t p = 0; p < input.size();) {
        const int control = input[p] < 128 ? input[p] : static_cast<int>(input[p])-256;
        ++p;
        const auto count = static_cast<std::size_t>(control >= 0 ? control+1 : -control);
        require(count <= expected-output.size(), "DAX run exceeds declared decoded size");
        if (control >= 0) {
            require(count <= input.size()-p, "Truncated DAX literal run");
            output.insert(output.end(), input.begin()+static_cast<std::ptrdiff_t>(p),
                          input.begin()+static_cast<std::ptrdiff_t>(p+count));
            p += count;
        } else {
            require(p < input.size(), "Truncated DAX repeat run");
            output.insert(output.end(), count, input[p++]);
        }
    }
    require(output.size() == expected, "DAX decoded size differs from directory");
    return output;
}
DaxArchive::DaxArchive(std::span<const std::uint8_t> b) {
    const std::size_t directory = read_u16(b,0);
    require(directory % 9 == 0 && directory <= b.size()-2, "Invalid DAX directory");
    const auto base = directory + 2;
    std::set<std::uint8_t> ids;
    std::vector<std::pair<std::size_t,std::size_t>> ranges;
    for (std::size_t p=2; p<base; p+=9) {
        DaxEntry e{b[p],read_u32(b,p+1),read_u16(b,p+5),read_u16(b,p+7),{}};
        require(ids.insert(e.id).second, "Duplicate DAX entry ID");
        require(e.offset <= b.size()-base && e.compressed_size <= b.size()-base-e.offset,
                "DAX entry outside archive");
        const auto start = base + e.offset, end = start + e.compressed_size;
        for (const auto& [a,z] : ranges)
            require(start >= z || end <= a || start == end, "Overlapping DAX entries");
        ranges.emplace_back(start,end);
        auto packed = b.subspan(start,e.compressed_size);
        if (e.raw_size == 0) e.data.assign(packed.begin(),packed.end());
        else e.data = decompress_dax(packed,e.raw_size);
        entries.push_back(std::move(e));
    }
}
AssetKind classify_asset(std::string_view filename) {
    const auto s = upper(filename);
    if (s.starts_with("ECL")) return AssetKind::ecl;
    if (s.starts_with("CPIC") || s.starts_with("CHEAD") || s.starts_with("CBODY") ||
        s.starts_with("DUNGCOM") || s.starts_with("WILDCOM") || s.starts_with("RANDCOM") ||
        s.starts_with("COMSPR")) return AssetKind::combat_picture;
    if (s.starts_with("SPRI")) return AssetKind::sprite;
    if (s.starts_with("PIC") || s.starts_with("FINAL") || s.starts_with("TITLE")) return AssetKind::picture;
    if (s.starts_with("GEO")) return AssetKind::geometry;
    if (s.starts_with("WALLDEF")) return AssetKind::wall;
    return AssetKind::other;
}
std::vector<Image> decode_ega(std::span<const std::uint8_t> b, std::string_view filename) {
    require(classify_asset(filename) != AssetKind::ecl, "ECL bytecode is not image data");
    require(b.size() >= 17, "Truncated EGA image header");
    std::vector<Image> result;
    const auto name = upper(filename);
    auto palette = ega;
    if (classify_asset(filename) == AssetKind::combat_picture) std::swap(palette[0],palette[8]);
    if (name.starts_with("SPRI")) { palette[0] = 0; palette[13] = ega[0]; }
    const bool delta = name.starts_with("PIC") || name.starts_with("FINAL");
    Bytes first;
    auto image = [&](std::size_t at,int width,int height,int x,int y,bool use_delta) {
        require(width > 0 && width <= 320 && height > 0 && height <= 200 &&
                x >= 0 && x <= 320 && y >= 0 && y <= 200,"Invalid EGA dimensions");
        const auto count = static_cast<std::size_t>(width*height/2);
        require(at <= b.size() && count <= b.size()-at, "Truncated EGA pixels");
        Bytes pixels(b.begin()+static_cast<std::ptrdiff_t>(at),b.begin()+static_cast<std::ptrdiff_t>(at+count));
        if (use_delta) {
            if (first.empty()) first = pixels;
            else {
                require(first.size() == pixels.size(),"EGA delta dimensions differ");
                for (std::size_t i=0;i<count;++i) pixels[i] ^= first[i];
            }
        }
        Image out{width,height,x,y,{}};
        out.pixels.reserve(static_cast<std::size_t>(width*height));
        for (auto v : pixels) { out.pixels.push_back(palette[v >> 4]); out.pixels.push_back(palette[v & 15]); }
        result.push_back(std::move(out));
        return count;
    };
    // Frame container: frame count, then a 21-byte header and packed nibbles per frame.
    bool frame_container = b[0] >= 1 && b[0] <= 8;
    std::size_t p=1;
    if (frame_container) {
        for (int f=0; f<b[0]; ++f) {
            if (p > b.size() || b.size()-p < 21) { frame_container=false; break; }
            const auto h=read_u16(b,p+4), w=read_u16(b,p+6);
            if (!h || h>200 || !w || w>40) { frame_container=false; break; }
            p += 21 + static_cast<std::size_t>(h)*w*4;
            if (p > b.size()) { frame_container=false; break; }
        }
        frame_container = frame_container && p == b.size();
    }
    if (frame_container) {
        p=1;
        for (int f=0;f<b[0];++f) {
            const int h=read_u16(b,p+4), w=read_u16(b,p+6)*8;
            const int x=read_u16(b,p+8)*8, y=read_u16(b,p+10)*8;
            p+=21;
            p+=image(p,w,h,x,y,delta);
        }
    } else {
        const int h=read_u16(b,0), w=read_u16(b,2)*8;
        const int x=read_u16(b,4)*8, y=read_u16(b,6)*8;
        const auto size=static_cast<std::size_t>(h)*static_cast<std::size_t>(w)/2;
        const std::size_t frames=b[8];
        require(frames > 0 && 17+size*frames == b.size(),"Unsupported EGA record layout");
        for (std::size_t f=0;f<frames;++f) image(17+size*f,w,h,x,y,false);
    }
    return result;
}
std::string decode_ecl_text(std::span<const std::uint8_t> b) {
    std::string out;
    std::uint32_t accumulator=0;
    int bits=0;
    for (auto v : b) {
        accumulator=(accumulator << 8)|v;
        bits+=8;
        while (bits >= 6) {
            bits-=6;
            auto c=(accumulator >> bits)&63;
            if (c) out.push_back(static_cast<char>(c<32 ? c+64 : c));
        }
    }
    return out;
}
EclEntryPoints inspect_ecl_header(std::span<const std::uint8_t> b) {
    EclEntryPoints out{};
    // Gold Box Explorer's PoR profile uses a 0x9900 script base and five tagged words.
    require(b.size() >= 22 && b.size()-2 <= 0x1e00, "Invalid PoR ECL size");
    // A two-byte record prefix is not part of the addressable ECL image.
    b=b.subspan(2);
    for (std::size_t i=0;i<5;++i) {
        require(b[i*4] == 1 && b[i*4+1] == 1, "Unsupported ECL entry GOTO/tag");
        out.addresses[i]=read_u16(b,i*4+2);
        require(out.addresses[i] >= 0x9900 && static_cast<std::size_t>(out.addresses[i]-0x9900) < b.size(),
                "ECL entry address outside script");
    }
    return out;
}
ScriptLocation locate_script_value(std::uint16_t a) {
    if (a >= 0x4900 && a < 0x4d00) return {ScriptRegion::area1,static_cast<std::uint16_t>((a-0x4900)*2),2};
    if (a >= 0x6b00 && a < 0x6f00) return {ScriptRegion::area2,static_cast<std::uint16_t>((a-0x6b00)*2),2};
    if (a >= 0x9700 && a < 0x9900) return {ScriptRegion::area3,static_cast<std::uint16_t>((a-0x9700)*2),2};
    if (a >= 0x9900 && a < 0xb700) return {ScriptRegion::script,static_cast<std::uint16_t>(a-0x9900),1};
    // A special register needs its individual native handler; no flat-memory fallback.
    return {ScriptRegion::special,a,0};
}
}
