#include "gbengine/world/overland.hpp"
#include "gbengine/assets/records.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
bool valid(OverlandPosition p) {
    return p.region>=2 && p.region<=4 && p.x>=0 && p.x<16 && p.y>=0 && p.y<36 &&
        static_cast<unsigned>(p.facing)<4;
}
}
OverlandMapOverlay OverlandMapOverlay::load(const IFileSystem& files) {
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    constexpr std::size_t at=0xc7c0+0x0c36,size=9*6;
    if(module.size()<at+size)throw DecodeError("Original overland overlay table is truncated");
    const auto table=std::span(module).subspan(at,size);
    if(sha256(table)!="f0e807550fed4090e3e0e191eaff44423b05d8cc28b2d5e771d35e1f572a72af")
        throw DecodeError("Unsupported original overland overlay table");
    OverlandMapOverlay result;
    for(std::size_t i=0;i<result.entries_.size();++i) {
        auto& entry=result.entries_[i];entry={read_u16(table,i*6),read_u16(table,i*6+2),read_u16(table,i*6+4)};
        if(entry.x>=44 || entry.y>=36 || entry.tile==0 || entry.tile>256)
            throw DecodeError("Invalid original overland overlay entry");
    }
    return result;
}
std::uint16_t OverlandMapOverlay::display_tile(std::uint8_t raw,int x,int y,const OverlandCampaignState& state) const {
    if(!raw || x<0 || x>=44 || y<0 || y>=36)throw DecodeError("Invalid overland overlay query");
    std::uint16_t result=raw;
    // Native first-to-last table order; entries1/2 intentionally do no replacement.
    for(std::size_t i=0;i<entries_.size();++i) {
        const auto& entry=entries_[i];
        if(entry.x!=x || entry.y!=y)continue;
        bool replace=false;
        switch(i+1) {
        case 3:replace=state.vm4a8c!=255;break;
        case 4:replace=state.vm4ad2!=0 || (state.vm4a7c&8)!=0 || state.vm4a9f==0;break;
        case 5:replace=(state.vm4aa0&2)==0;break;
        case 6:replace=true;break;
        case 7:replace=(state.vm4aa0&1)==0;break;
        case 8:case 9:replace=state.vm4ab3>253;break;
        default:break;
        }
        if(replace)result=entry.tile;
    }
    return result;
}
OverlandTravel::OverlandTravel(const std::array<std::uint8_t,44*36>& cells,OverlandPosition position,
                             std::vector<std::uint8_t> blocked)
    :cells_(cells),position_(position),blocked_tiles_(std::move(blocked)) {
    if(!valid(position_))throw DecodeError("Invalid overland travel position");
    (void)tile(position_.x,position_.y);
}
OverlandTravel OverlandTravel::load(const IFileSystem& files,OverlandPosition position) {
    if(!valid(position))throw DecodeError("Invalid overland travel position");
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    constexpr std::size_t at=0xc7c0+0x3648;
    std::array<std::uint8_t,44*36> cells{};
    if(module.size()<at+cells.size())throw DecodeError("Original overland map table is truncated");
    std::copy_n(module.begin()+at,cells.size(),cells.begin());
    // Static before-move tile veto tables, read as data without invoking ECL.
    // Stage1 deliberately selects the baseline campaign variant for region3;
    // quest-dependent replacements/4AB3 variants belong to later integration.
    struct Table {unsigned disk;std::uint8_t record;std::size_t address,count;const char* hash;};
    constexpr std::array tables{
        Table{6,25,0xadce,8,"57cbc92a3029679011bc591809de06028e869a05477f8a1677a2e24bdcef23e5"},
        Table{7,26,0xb019,29,"c3a1a667e768b48bdfe249a5e23f0cac31204822bf708d74f98bda97d15a6d88"},
        Table{8,27,0xab9e,8,"2120554c2597ffaf4ebbb7487f5b8a73dbd9495c1ad8249dd5756b35b2574295"}};
    const auto& table=tables[position.region-2];
    const auto script=load_record(files,"ECL"+std::to_string(table.disk)+".DAX",table.record);
    (void)inspect_ecl_header(script);
    const auto offset=table.address-0x9900+2;
    if(script.size()<offset+table.count)throw DecodeError("Original overland passability table is truncated");
    const auto bytes=std::span(script).subspan(offset,table.count);
    if(sha256(bytes)!=table.hash)throw DecodeError("Unsupported original overland passability table");
    return OverlandTravel(cells,position,{bytes.begin(),bytes.end()});
}
OverlandTerrain OverlandTravel::classify_tile(std::uint8_t t) {
    if(!t)throw DecodeError("Invalid zero overland tile");
    // Source3635:0818. Stage1 never grants the separate VM4AA2 permission.
    if((t>=0x66 && t<=0x6c) || t==0x72)return OverlandTerrain::water;
    if(t==0x4e || (t>=0x7e && t<=0x80) || (t>=0x82 && t<=0x84) ||
       (t>=0xa8 && t<=0xac) || (t>=0xae && t<=0xb3) || t==0xbc ||
       (t>=0xdd && t<=0xde) || (t>=0xe1 && t<=0xe9) || t==0xf5)return OverlandTerrain::blocked;
    return OverlandTerrain::land;
}
std::uint8_t OverlandTravel::tile(int x,int y) const {
    if(x<0 || x>=16 || y<0 || y>=36)throw DecodeError("Invalid overland tile coordinates");
    const auto value=cells_.at(static_cast<std::size_t>(y*44+x+(position_.region-2)*13));
    if(!value)throw DecodeError("Invalid zero overland tile");
    return value;
}
OverlandMoveResult OverlandTravel::step(Direction facing) {
    const auto direction=static_cast<unsigned>(facing);
    if(direction>3)throw DecodeError("Invalid overland movement direction");
    constexpr std::array dx{0,1,0,-1},dy{-1,0,1,0};
    const auto x=position_.x+dx[direction],y=position_.y+dy[direction];
    // By design for isolated stage1: an edge attempt is blocked, never wrapped,
    // clamped into a successful same-cell move, or dispatched to a map link.
    if(x<0 || x>=16 || y<0 || y>=36) {position_.facing=facing;return OverlandMoveResult::map_edge;}
    const auto value=tile(x,y);const auto terrain=classify_tile(value);
    // Validate all queried data before publishing facing or position.
    position_.facing=facing;
    if(terrain==OverlandTerrain::water)return OverlandMoveResult::water;
    if(terrain==OverlandTerrain::blocked || std::find(blocked_tiles_.begin(),blocked_tiles_.end(),value)!=blocked_tiles_.end())
        return OverlandMoveResult::blocked_terrain;
    position_.x=x;position_.y=y;return OverlandMoveResult::moved;
}
}
