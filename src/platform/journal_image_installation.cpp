#include "gbengine/platform/journal_image_installation.hpp"
#include "gbengine/qol/journal.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#ifdef _MSC_VER
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"uuid.lib")
#endif
#endif
namespace GBEngine {
namespace {
struct PdfImageStream {unsigned page,offset,length;};
struct CropFragment {unsigned page,left,top,right,bottom;};
struct ScannedPage {const char* id;unsigned number,first,count;};
#include "journal_image_recipe.inc"
struct Raster {unsigned width,height;Bytes pixels;};
std::string filename(const ScannedPage& page){return std::string(page.id)+"-"+(page.number<10?"0":"")+std::to_string(page.number)+".png";}
std::pair<unsigned,unsigned> dimensions(const ScannedPage& page) {
    unsigned width=0,height=0;
    for(unsigned i=0;i<page.count;++i){const auto& p=fragments[page.first+i];width=std::max(width,p.right-p.left);height+=p.bottom-p.top+(i?12:0);}
    if(!width||!height||width>2048||height>2048)throw DecodeError("Invalid precomputed Journal dimensions");
    return {width,height};
}
Bytes read(const std::filesystem::path& path) {
    if(!std::filesystem::is_regular_file(path)||std::filesystem::is_symlink(path)||std::filesystem::file_size(path)>20*1024*1024)throw DecodeError("Journal source must be an ordinary supported PDF file");
    std::ifstream f(path,std::ios::binary);if(!f)throw DecodeError("Cannot open Journal source");return {std::istreambuf_iterator<char>(f),{}};
}
void write(const std::filesystem::path& path,std::span<const std::uint8_t> bytes) {
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!f)throw DecodeError("Cannot write extracted Journal content");
}
void write(const std::filesystem::path& path,std::string_view text){write(path,std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));}
void big(Bytes& out,std::uint32_t n){for(int shift=24;shift>=0;shift-=8)out.push_back(static_cast<std::uint8_t>(n>>shift));}
void chunk(Bytes& out,std::string_view type,const Bytes& data) {
    big(out,static_cast<std::uint32_t>(data.size()));const auto start=out.size();out.insert(out.end(),type.begin(),type.end());out.insert(out.end(),data.begin(),data.end());
    std::uint32_t crc=0xffffffff;for(auto p:std::span(out).subspan(start)){crc^=p;for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));}big(out,~crc);
}
Bytes png(const Raster& raster) {
    Bytes raw;raw.reserve(raster.height*(raster.width*3+1));
    for(unsigned y=0;y<raster.height;++y){raw.push_back(0);const auto row=std::span(raster.pixels).subspan(y*raster.width*3,raster.width*3);raw.insert(raw.end(),row.begin(),row.end());}
    Bytes packed{0x78,0x01};std::size_t pos=0;
    while(pos<raw.size()) {
        const auto size=std::min<std::size_t>(65535,raw.size()-pos);packed.push_back(pos+size==raw.size()?1:0);
        const auto n=static_cast<std::uint16_t>(size),inverse=static_cast<std::uint16_t>(~n);
        for(auto word:{n,inverse}){packed.push_back(static_cast<std::uint8_t>(word));packed.push_back(static_cast<std::uint8_t>(word>>8));}
        packed.insert(packed.end(),raw.begin()+static_cast<std::ptrdiff_t>(pos),raw.begin()+static_cast<std::ptrdiff_t>(pos+size));pos+=size;
    }
    std::uint32_t a=1,b=0;for(auto p:raw){a=(a+p)%65521;b=(b+a)%65521;}big(packed,(b<<16)|a);
    Bytes result{137,80,78,71,13,10,26,10},header;big(header,raster.width);big(header,raster.height);header.insert(header.end(),{8,2,0,0,0});
    chunk(result,"IHDR",header);chunk(result,"IDAT",packed);chunk(result,"IEND",{});return result;
}
std::string catalog() {
    std::ostringstream out;out<<"# Journal image-only catalog v1: id\tcategory\tnumber\t-\tordered PNG pages\n";
    std::string previous;
    for(const auto& page:pages) {
        if(previous!=page.id) {
            if(!previous.empty())out<<'\n';
            previous=page.id;const auto id=std::string_view(page.id);const auto split=id.find_last_of('-');
            out<<id<<'\t'<<id.substr(0,split)<<'\t'<<std::stoul(std::string(id.substr(split+1)))<<"\t-\t";
        }else out<<',';
        out<<filename(page);
    }
    out<<'\n';return out.str();
}
std::string profile(){return std::string(pdf_hash)+"\t"+std::string(recipe_hash)+"\n";}
#ifdef _WIN32
void com_check(HRESULT result){if(FAILED(result))throw DecodeError("Windows JPEG decoder could not extract the Journal image");}
template<class T>struct Com {
    T* value=nullptr;~Com(){if(value)value->Release();}T** out(){return &value;}T* operator->(){return value;}
};
struct Apartment {
    HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Apartment(){if(result!=RPC_E_CHANGED_MODE)com_check(result);}
    ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}
};
Raster decode(IWICImagingFactory* factory,std::span<const std::uint8_t> jpeg) {
    Com<IWICStream> stream;com_check(factory->CreateStream(stream.out()));
    com_check(stream->InitializeFromMemory(const_cast<BYTE*>(jpeg.data()),static_cast<DWORD>(jpeg.size())));
    Com<IWICBitmapDecoder> decoder;com_check(factory->CreateDecoderFromStream(stream.value,nullptr,WICDecodeMetadataCacheOnLoad,decoder.out()));
    Com<IWICBitmapFrameDecode> frame;com_check(decoder->GetFrame(0,frame.out()));
    UINT width=0,height=0;com_check(frame->GetSize(&width,&height));if(width!=1328||height!=1003)throw DecodeError("Unexpected Journal scan size");
    Com<IWICFormatConverter> converter;com_check(factory->CreateFormatConverter(converter.out()));
    com_check(converter->Initialize(frame.value,GUID_WICPixelFormat24bppRGB,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    Raster result{width,height,Bytes(static_cast<std::size_t>(width)*height*3)};
    com_check(converter->CopyPixels(nullptr,width*3,static_cast<UINT>(result.pixels.size()),result.pixels.data()));return result;
}
#endif
}
bool journal_images_ready(const std::filesystem::path& root) {
    try {
        if(std::filesystem::is_symlink(root)||!std::filesystem::is_directory(root))return false;
        const auto raw=read(root/"profile.tsv");if(std::string(raw.begin(),raw.end())!=profile())return false;
        const auto index=read(root/"catalog.tsv"),references=read(root/"references.tsv");
        if(std::string(index.begin(),index.end())!=catalog()||std::string(references.begin(),references.end())!=journal_references)return false;
        for(const auto& page:pages) {
            const auto path=root/filename(page);if(!std::filesystem::is_regular_file(path)||std::filesystem::is_symlink(path))return false;
            const auto [width,height]=dimensions(page);const auto raw_size=static_cast<std::uint64_t>(height)*(1+width*3);
            const auto expected=63+raw_size+5*((raw_size+65534)/65535);if(std::filesystem::file_size(path)!=expected)return false;
            std::ifstream f(path,std::ios::binary);std::array<unsigned char,24> header{};f.read(reinterpret_cast<char*>(header.data()),header.size());
            if(!f||header[0]!=137||header[1]!='P'||header[2]!='N'||header[3]!='G')return false;
            const auto value=[&](unsigned at){return (unsigned(header[at])<<24)|(unsigned(header[at+1])<<16)|(unsigned(header[at+2])<<8)|header[at+3];};
            if(value(16)!=width||value(20)!=height)return false;
        }
        return true;
    }catch(const std::exception&){return false;}
}
bool prepare_journal_images(const std::filesystem::path& pdf,const std::filesystem::path& destination,const SetupProgress& progress) {
    if(journal_images_ready(destination)){if(progress)progress("Journal image cache is ready");return false;}
    const auto data=read(pdf);const auto actual_hash=sha256(data);
    if(progress)progress("Journal PDF SHA256 "+actual_hash+" (expected "+std::string(pdf_hash)+")");
    if(actual_hash!=pdf_hash)throw DecodeError("This Journal PDF does not match the supported GOG scan; no content was changed");
#ifndef _WIN32
    (void)progress;throw DecodeError("Journal image extraction requires the Windows JPEG decoder");
#else
    const auto root=std::filesystem::absolute(destination).lexically_normal(),parent=root.parent_path();
    if(std::filesystem::is_symlink(root)||root==parent)throw DecodeError("Journal output must be an ordinary install directory");
    std::filesystem::create_directories(parent);const auto suffix=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto stage=parent/(".Journal-images-stage-"+suffix),backup=parent/(".Journal-images-backup-"+suffix);
    if(!std::filesystem::create_directory(stage))throw DecodeError("Cannot stage Journal extraction");
    Apartment apartment;Com<IWICImagingFactory> factory;
    com_check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,reinterpret_cast<void**>(factory.out())));
    std::map<unsigned,Raster> decoded;
    const auto source=[&](unsigned number)->const Raster& {
        auto found=decoded.find(number);if(found!=decoded.end())return found->second;
        const auto stream=std::find_if(std::begin(streams),std::end(streams),[&](const auto& s){return s.page==number;});
        if(stream==std::end(streams)||stream->offset>data.size()||stream->length>data.size()-stream->offset)throw DecodeError("Invalid precomputed PDF image range");
        return decoded.emplace(number,decode(factory.value,std::span(data).subspan(stream->offset,stream->length))).first->second;
    };
    for(const auto& page:pages) {
        if(progress)progress(std::string("Extracting ")+filename(page));
        const auto [width,height]=dimensions(page);Raster image{width,height,Bytes(width*height*3,255)};unsigned y=0;
        for(unsigned i=0;i<page.count;++i) {
            const auto& p=fragments[page.first+i];const auto& scan=source(p.page);
            if(p.left>=p.right||p.top>=p.bottom||p.right>scan.width||p.bottom>scan.height)throw DecodeError("Invalid precomputed Journal crop");
            const auto w=p.right-p.left,h=p.bottom-p.top,x=(width-w)/2;y+=i?12:0;
            for(unsigned row=0;row<h;++row)std::copy_n(scan.pixels.begin()+static_cast<std::ptrdiff_t>(((p.top+row)*scan.width+p.left)*3),w*3,image.pixels.begin()+static_cast<std::ptrdiff_t>(((y+row)*width+x)*3));
            y+=h;
        }
        const auto bytes=png(image);const auto verified=decode_journal_png(bytes);if(verified.width!=static_cast<int>(width)||verified.height!=static_cast<int>(height))throw DecodeError("Generated Journal PNG failed validation");write(stage/filename(page),bytes);
    }
    write(stage/"catalog.tsv",catalog());write(stage/"references.tsv",journal_references);write(stage/"profile.tsv",profile());
    if(!journal_images_ready(stage))throw DecodeError("Extracted Journal cache failed validation");
    const bool existing=std::filesystem::exists(root);if(existing)std::filesystem::rename(root,backup);
    try{std::filesystem::rename(stage,root);}catch(...){if(existing)std::filesystem::rename(backup,root);throw;}
    if(progress)progress("Journal ready: 99 entries, "+std::to_string(std::size(pages))+" image pages; no OCR or transcribed text");
    return true;
#endif
}
}
