#include "gbengine/rendering/exploration_assets.hpp"
#include "gbengine/assets/records.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
constexpr std::array offsets{0,2,6,10,22,38,54,110,132,154};
constexpr std::array cols{1,1,1,3,2,2,7,2,2,1};
constexpr std::array rows{2,4,4,4,8,8,8,11,11,2};
std::vector<Image> tiles(const IFileSystem& f,unsigned disk,std::uint8_t id,std::size_t count) {
    auto result=decode_ega(load_record(f,"8X8D"+std::to_string(disk)+".DAX",id),"8X8D.DAX");
    if(result.size()!=count || std::any_of(result.begin(),result.end(),[](const Image& i){return i.width!=8 || i.height!=8;}))
        throw DecodeError("Invalid exploration tile bank");
    return result;
}}
void ExplorationAssets::load_background(const IFileSystem& files) {
    if(background_selectors_)return;
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    constexpr std::size_t offset=0xc7c0+0xc26;
    if(module.size()<offset+16)throw DecodeError("Truncated native background palette selectors");
    std::array<std::uint8_t,16> selectors{};std::copy_n(module.begin()+offset,16,selectors.begin());
    if(std::any_of(selectors.begin(),selectors.end(),[](auto c){return c>15;}))throw DecodeError("Invalid native background palette selector");
    background_selectors_=selectors;
}
std::uint8_t ExplorationAssets::background_color(std::uint16_t selector)const {
    if(!background_selectors_ || selector>=16)throw DecodeError("Native background palette is unavailable");
    return background_selectors_->at(selector);
}
void ExplorationAssets::load_walls(const IFileSystem& f,unsigned disk,unsigned slot,std::uint8_t id) {
    if(slot<1 || slot>3) throw DecodeError("Wall slot must be 1..3");
    const auto data=load_record(f,"WALLDEF"+std::to_string(disk)+".DAX",id);
    if(data.empty() || data.size()%780 || data.size()/780+slot>4) throw DecodeError("Invalid WALLDEF bank count");
    // Stage every bank, including graphics, before committing any replacement.
    auto staged=slots_;
    auto sources=sources_;
    auto universal=universal_.empty()?tiles(f,1,203,45):universal_;
    const auto count=data.size()/780;
    const unsigned shift=(slot-1)*70; // Native loader uses one shift for a multi-bank record.
    for(std::size_t n=0;n<count;++n) {
        WallSlot bank;
        for(std::size_t i=0;i<780;++i) {
            const unsigned value=data[n*780+i];
            bank.definitions[i]=static_cast<std::uint8_t>(value>=45 ? value+shift : value);
        }
        const unsigned tile_id=count==1?id:(id==0?100:static_cast<unsigned>(id)*10)+static_cast<unsigned>(n)+1;
        if(tile_id>255) throw DecodeError("Wall tile record overflow");
        bank.tiles=tiles(f,disk,static_cast<std::uint8_t>(tile_id),70);
        staged[slot-1+n]=std::move(bank);
        sources[slot-1+n]=Source{disk,slot,id};
    }
    slots_=std::move(staged);universal_=std::move(universal);
    sources_=std::move(sources);
}
void ExplorationAssets::draw_slice(Framebuffer& out,unsigned slice,unsigned wall,int col,int row) const {
    if(!wall) return;
    if(wall>15 || slice>=10) throw DecodeError("Invalid wall slice");
    const auto& slot=slots_[(wall-1)/5];
    if(!slot) throw DecodeError("Scene references an unloaded wall bank");
    const auto start=((wall-1)%5)*156+static_cast<unsigned>(offsets[slice]);
    for(int y=0;y<rows[slice];++y) for(int x=0;x<cols[slice];++x) {
        const auto tile=slot->definitions[start+static_cast<unsigned>(y*cols[slice]+x)];
        if(!tile || row+y<0 || row+y>10 || col+x<0 || col+x>10) continue;
        const Image* image;
        if(tile<=45) image=&universal_.at(tile-1);
        else {
            const auto bank=(tile-46)/70;
            if(!slots_.at(bank)) throw DecodeError("Wall tile references an unloaded bank");
            image=&slots_[bank]->tiles.at((tile-46)%70);
        }
        for(int py=0;py<8;++py) for(int px=0;px<8;++px) {
            const auto color=image->pixels[static_cast<std::size_t>(py*8+px)];
            // Native tile loader 50e3:003b selects palette index 13 as its mask
            // key. Keep the already-painted scene beneath those pixels.
            if(color!=0xffff55ffu)
                out[static_cast<std::size_t>(((row+y+2)*8+py)*320+(col+x+2)*8+px)]=color;
        }
    }
}
}
