#include "gbengine/combat/battlefield.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
int signed_byte(std::uint8_t value) {return value<128?value:static_cast<int>(value)-256;}
unsigned roll(std::mt19937& random,unsigned sides) {
    const auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    std::uint32_t value;
    do {value=static_cast<std::uint32_t>(random());}while(value<threshold);
    return value%sides+1;
}
std::uint8_t single_edge(const GeoMap& map,int origin_y,int x,int y,unsigned direction) {
    if(!GeoMap::contains(x,y))return y==origin_y && (direction==2 || direction==6)?0:1;
    const auto facing=static_cast<Direction>(direction/2);
    if(map.door(x,y,facing)==0)return 1;
    return map.wall(x,y,facing)==0?0:3;
}
struct InteriorBuilder {
    Battlefield& field;
    const GeoMap& map;
    int origin_y;
    int dx{},dy{},x{},y{};
    std::uint8_t west{},north{},east{};
    std::uint8_t edge(int at_x,int at_y,unsigned direction) const {
        return BattlefieldService::native_edge(map,origin_y,at_x,at_y,direction);
    }
    void put(int local_y,int local_x,unsigned tile) {
        const auto target_x=dx*6+dy*5+21+local_x;
        const auto target_y=dy*5+10+local_y;
        if(Battlefield::in_bounds(target_x,target_y))
            field.terrain[static_cast<std::size_t>(target_y*Battlefield::width+target_x)]=static_cast<std::uint8_t>(tile+1);
    }
    void body() {
        for(int row=2;row<=4;++row)for(int col=0;col<=5;++col)put(row,col,22);
        if(west==1)for(int row=2;row<=4;++row) {
            put(row,row-1,4);put(row,row,3);put(row,row+1,13);
        }
        else if(west==3) {put(2,1,8);put(4,5,0);}
    }
    void north_center() {
        const auto tile=north==1?5u:22u;
        put(0,3,tile);put(0,4,tile);
        put(1,3,north==1?10u:22u);put(1,4,north==1?10u:22u);
    }
    void northwest_corner() {
        const bool open=edge(x,y-1,6)==0 && edge(x-1,y,0)==0;
        unsigned a=22,b=22,c=22,d=22;
        if(north==0)a=west==0?22:west==3?13:open?0:13;
        else a=west==0?(open?15:5):(open?18:2);
        b=north==0?22:north==3?17:5;
        if(west==0)c=north==0?22:open?16:10;
        else if(west==3)c=open?20:7;
        else c=open?1:3;
        if(west==0 || west==3)d=north==0?22:north==3?23:10;
        else d=north==0?13:north==3?21:6;
        put(0,1,a);put(0,2,b);put(1,1,c);put(1,2,d);
    }
    void northeast_corner() {
        const auto above_east=edge(x,y-1,2),right_north=edge(x+1,y,0);
        const bool open=above_east==0 && right_north==0;
        const unsigned a=north==0?(above_east==1?4:22):north==3?15:5;
        unsigned b=22,c=22,d=22;
        if(north==0) {
            if(above_east==0)b=22;
            else if(above_east==3)b=east==0 && right_north!=0?24:1;
            else b=east==0 && right_north!=0?11:3;
        } else if(east==0)b=right_north==0?(open?17:19):5;
        else b=9;
        c=north==0?22:north==3?16:10;
        if(north==0) {
            if(above_east==0)d=22;
            else d=east==0?(right_north==0?8:12):4;
        } else d=east==0?(right_north==0?23:10):14;
        put(0,5,a);put(0,6,b);put(1,5,c);put(1,6,d);
    }
    void cell(int at_x,int at_y,int delta_x,int delta_y) {
        x=at_x;y=at_y;dx=delta_x;dy=delta_y;
        west=edge(x,y,6);north=edge(x,y,0);east=edge(x,y,2);
        body();north_center();northwest_corner();northeast_corner();
    }
};
}
BattlefieldTables BattlefieldTables::load(const IFileSystem& files) {
    const auto packed=files.read_asset("START.EXE");
    const auto module=decode_start_data(packed);
    constexpr std::size_t base=0xc7c0,property_at=base+0x2744;
    if(module.size()<property_at+66*4)throw DecodeError("Native battlefield property table is truncated");
    BattlefieldTables result;
    for(std::size_t id=0;id<result.properties_.size();++id)
        std::copy_n(module.begin()+static_cast<std::ptrdiff_t>(property_at+id*4),4,result.properties_[id].begin());
    for(std::size_t direction=0;direction<9;++direction) {
        result.delta_x_[direction]=static_cast<std::int8_t>(signed_byte(module[base+0x2736+direction]));
        result.delta_y_[direction]=static_cast<std::int8_t>(signed_byte(module[base+0x273f+direction]));
    }
    result.source_={"START.EXE",property_at,sha256(packed),sha256(module)};
    result.loaded_=true;
    return result;
}
const std::array<std::uint8_t,4>& BattlefieldTables::properties(std::uint8_t id) const {
    if(!loaded_)throw DecodeError("Native battlefield tables are not loaded");
    if(id>=properties_.size())throw DecodeError("Battlefield terrain ID exceeds the native property table");
    return properties_[id];
}
int BattlefieldTables::delta_x(unsigned direction) const {
    if(!loaded_)throw DecodeError("Native battlefield tables are not loaded");
    if(direction>=delta_x_.size())throw DecodeError("Battlefield direction exceeds the native table");
    return delta_x_[direction];
}
int BattlefieldTables::delta_y(unsigned direction) const {
    if(!loaded_)throw DecodeError("Native battlefield tables are not loaded");
    if(direction>=delta_y_.size())throw DecodeError("Battlefield direction exceeds the native table");
    return delta_y_[direction];
}
std::uint8_t Battlefield::terrain_at(int x,int y) const {
    if(!in_bounds(x,y))throw DecodeError("Battlefield coordinate is outside the tactical map");
    return terrain[static_cast<std::size_t>(y*width+x)];
}
const std::array<std::uint8_t,4>& Battlefield::properties_at(int x,int y) const {
    return tables.properties(terrain_at(x,y));
}
std::int8_t Battlefield::movement_cost(int x,int y) const {
    return static_cast<std::int8_t>(signed_byte(properties_at(x,y)[0]));
}
std::uint8_t BattlefieldService::native_edge(const GeoMap& map,int origin_y,int x,int y,unsigned direction) {
    if(direction>6 || direction%2!=0)throw DecodeError("Interior battlefield requires a cardinal doubled direction");
    constexpr std::array<int,4> dx{0,1,0,-1},dy{-1,0,1,0};
    const auto index=direction/2;
    return static_cast<std::uint8_t>(single_edge(map,origin_y,x,y,direction)|
        single_edge(map,origin_y,x+dx[index],y+dy[index],(direction+4)%8));
}
void BattlefieldService::vary_floor(Battlefield& field,std::uint8_t ecl_record,std::mt19937& random) {
    // Source visits columns first, unlike row-major storage. Retain every
    // floor draw, including the exceptionally rare 100/1 decoration branch.
    for(int x=0;x<Battlefield::width;++x)for(int y=0;y<Battlefield::height;++y) {
        auto& tile=field.terrain[static_cast<std::size_t>(y*Battlefield::width+x)];
        if(field.tables.properties(tile)[3]!=22)continue;
        const auto value=roll(random,100);
        if(value==98)tile=26;
        else if(value==99)tile=27;
        else if(value==100 && ecl_record!=10 && roll(random,100)==1)
            tile=roll(random,10)<=6?28:29;
    }
}
Battlefield BattlefieldService::build_interior(const BattlefieldTables& tables,const GeoMap& map,int x,int y,std::uint8_t ecl_record,std::mt19937& random) {
    if(!GeoMap::contains(x,y))throw DecodeError("Interior battlefield origin is outside the source map");
    auto staged_random=random;
    Battlefield result;result.tables=tables;result.presentation_mode=1;
    InteriorBuilder builder{result,map,y};
    for(int dy=-2;dy<=2;++dy)for(int dx=-6;dx<=6;++dx)builder.cell(x+dx,y+dy,dx,dy);
    vary_floor(result,ecl_record,staged_random);
    random=std::move(staged_random);
    return result;
}
}
