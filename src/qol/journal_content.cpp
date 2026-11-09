#include "gbengine/qol/journal.hpp"
#include <algorithm>
#include <charconv>
#include <set>
#include <sstream>

namespace GBEngine {
namespace {
void need(bool ok,const char* message){if(!ok)throw DecodeError(message);}
bool leaf(std::string_view value){return !value.empty() && value.size()<96 && std::all_of(value.begin(),value.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='.';});}
std::vector<std::string> split(std::string_view text,char separator) {
    std::vector<std::string> result;std::size_t start=0;
    do {const auto end=text.find(separator,start);result.emplace_back(text.substr(start,end==text.npos?end:end-start));if(end==text.npos)break;start=end+1;}while(start<=text.size());return result;
}
std::uint32_t big(std::span<const std::uint8_t> bytes,std::size_t at) {
    need(at<=bytes.size() && bytes.size()-at>=4,"Truncated journal PNG");
    return (std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|(std::uint32_t(bytes[at+2])<<8)|bytes[at+3];
}
std::uint32_t crc(std::span<const std::uint8_t> bytes) {
    std::uint32_t value=0xffffffffu;
    for(auto b:bytes){value^=b;for(unsigned i=0;i<8;++i)value=(value>>1)^(0xedb88320u&(0u-(value&1u)));}return ~value;
}
}
JournalCatalog::JournalCatalog(std::shared_ptr<const IFileSystem> files):files_(std::move(files)) {
    need(bool(files_),"Missing journal content store");const auto raw=files_->read_asset("catalog.tsv");need(raw.size()<65536,"Journal catalog too large");
    std::istringstream input(std::string(raw.begin(),raw.end()));std::string line;std::set<std::string> identities;
    while(std::getline(input,line)) {
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line.empty() || line.front()=='#')continue;
        const auto fields=split(line,'\t');need(fields.size()==5,"Invalid journal catalog row");
        const auto kind=fields[1]=="entry"?JournalKind::entry:fields[1]=="tale"?JournalKind::tale:JournalKind::proclamation;
        need(fields[1]=="entry"||fields[1]=="tale"||fields[1]=="proclamation","Invalid journal category");
        unsigned number{};const auto parsed=std::from_chars(fields[2].data(),fields[2].data()+fields[2].size(),number);
        need(parsed.ec==std::errc{} && parsed.ptr==fields[2].data()+fields[2].size(),"Invalid journal number");
        const JournalId id{kind,number};need(fields[0]==journal_id(id) && identities.insert(fields[0]).second,"Invalid or duplicate journal ID");
        need(fields[3]=="-","Journal content requires scanned image pages");JournalRecord record{id,{}};
        if(!fields[4].empty())for(auto name:split(fields[4],',')){need(leaf(name)&&name.starts_with(fields[0]+"-")&&name.ends_with(".png"),"Invalid journal image filename");record.images.push_back(std::move(name));}
        need(!record.images.empty()&&record.images.size()<=8,"Journal entry requires 1 to 8 scanned pages");records_.push_back(std::move(record));
    }
    need(records_.size()==99,"Journal catalog requires all99 IDs");
}
Image JournalCatalog::image(std::string_view name)const {const auto raw=files_->read_asset(name);return decode_journal_png(raw);}
Image decode_journal_png(std::span<const std::uint8_t> bytes) {
    static constexpr std::array<std::uint8_t,8> signature{137,80,78,71,13,10,26,10};
    need(bytes.size()>=45 && bytes.size()<=16*1024*1024 && std::equal(signature.begin(),signature.end(),bytes.begin()),"Invalid journal PNG signature/size");
    std::size_t pos=8;unsigned width=0,height=0;bool ended=false;Bytes packed;
    while(pos<bytes.size()) {
        need(bytes.size()-pos>=12,"Truncated journal PNG chunk header");
        const auto length=big(bytes,pos);need(length<=bytes.size()-pos-12,"Truncated journal PNG chunk");
        const std::string type(bytes.begin()+static_cast<std::ptrdiff_t>(pos+4),bytes.begin()+static_cast<std::ptrdiff_t>(pos+8));
        need(crc(bytes.subspan(pos+4,4+length))==big(bytes,pos+8+length),"Journal PNG checksum mismatch");const auto data=bytes.subspan(pos+8,length);
        if(type=="IHDR") {
            need(pos==8 && length==13,"Invalid journal PNG header");width=big(data,0);height=big(data,4);
            need(width>0&&width<=2048&&height>0&&height<=2048&&data[8]==8&&data[9]==2&&data[10]==0&&data[11]==0&&data[12]==0,"Journal PNG requires RGB8 without interlace");
        }else if(type=="IDAT") {need(width!=0&&packed.size()+length<=16*1024*1024,"Invalid journal PNG image data");packed.insert(packed.end(),data.begin(),data.end());}
        else if(type=="IEND") {need(length==0,"Invalid journal PNG ending");ended=true;pos+=12;break;}
        else need(type[0]>='a'&&type[0]<='z',"Unsupported critical journal PNG chunk");
        pos+=length+12;
    }
    need(ended&&pos==bytes.size()&&packed.size()>=6,"Incomplete journal PNG");
    need((packed[0]&15)==8 && (packed[0]>>4)<=7 && ((unsigned(packed[0])*256+packed[1])%31)==0 && !(packed[1]&32),"Invalid journal PNG zlib header");
    const auto expected=std::size_t(height)*(1+std::size_t(width)*3);Bytes raw;raw.reserve(expected);pos=2;bool final=false;
    while(!final) {
        need(pos<packed.size()-4,"Truncated journal PNG deflate stream");const auto control=packed[pos++];final=(control&1)!=0;
        need((control&0xfe)==0,"Normalize journal PNG with the extraction tool before use");
        need(pos+4<=packed.size()-4,"Truncated journal PNG stored block");const auto length=read_u16(packed,pos),inverse=read_u16(packed,pos+2);pos+=4;
        need((length^inverse)==0xffff && length<=expected-raw.size() && length<=packed.size()-4-pos,"Invalid journal PNG stored block");
        raw.insert(raw.end(),packed.begin()+static_cast<std::ptrdiff_t>(pos),packed.begin()+static_cast<std::ptrdiff_t>(pos+length));pos+=length;
    }
    need(pos==packed.size()-4 && raw.size()==expected,"Invalid journal PNG decompressed size");
    std::uint32_t a=1,b=0;for(auto value:raw){a=(a+value)%65521;b=(b+a)%65521;}need(((b<<16)|a)==big(packed,pos),"Journal PNG Adler checksum mismatch");
    Image image{static_cast<int>(width),static_cast<int>(height),0,0,{}};image.pixels.reserve(std::size_t(width)*height);pos=0;
    for(unsigned y=0;y<height;++y){need(raw[pos++]==0,"Normalize journal PNG row filters before use");for(unsigned x=0;x<width;++x){image.pixels.push_back(0xff000000u|(std::uint32_t(raw[pos])<<16)|(std::uint32_t(raw[pos+1])<<8)|raw[pos+2]);pos+=3;}}
    return image;
}
}
