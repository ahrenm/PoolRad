#include "gbengine/combat/deployment.hpp"
#include "gbengine/assets/records.hpp"
#include <algorithm>
#include <set>

namespace GBEngine {
namespace {
int signed_byte(int value) {
    const auto low=static_cast<std::uint8_t>(value);
    return low<128?low:static_cast<int>(low)-256;
}
struct FormationTables {
    std::array<std::uint8_t,16> alternate{},scan{};
    std::array<std::uint8_t,4> facing{};
    std::array<std::uint8_t,8> start_x{},start_y{};
    std::array<std::uint8_t,60> limits{};
    std::array<std::array<CombatPosition,4>,5> shapes{};
    explicit FormationTables(const IFileSystem& files) {
        const auto resident=decode_start_data(files.read_asset("START.EXE"));
        const auto copy=[&](std::size_t at,auto& target) {
            if(0xc7c0+at+target.size()>resident.size())throw DecodeError("Truncated combat formation table");
            std::copy_n(resident.begin()+static_cast<std::ptrdiff_t>(0xc7c0+at),target.size(),target.begin());
        };
        copy(0x2d2,alternate);copy(0x2e2,scan);copy(0x2f2,facing);
        copy(0x2f6,start_x);copy(0x2fe,start_y);copy(0x306,limits);
        for(auto value:alternate)if(value>8 || (value&1))throw DecodeError("Invalid native alternate formation direction");
        for(auto value:scan)if(value>6 || (value&1))throw DecodeError("Invalid native formation scan direction");
        for(auto value:facing)if(value>8)throw DecodeError("Invalid native combat facing table");
        for(auto value:start_x)if(value>10)throw DecodeError("Invalid native formation start column");
        for(auto value:start_y)if(value>5)throw DecodeError("Invalid native formation start row");
        for(auto value:limits)if(value>10)throw DecodeError("Invalid native formation mask range");
        for(unsigned shape=1;shape<=4;++shape)for(unsigned cell=0;cell<4;++cell) {
            const auto at=0xc7c0+0x2844+shape*8+cell*2;
            if(at+1>=resident.size())throw DecodeError("Truncated combat footprint table");
            shapes[shape][cell]={signed_byte(resident[at]),signed_byte(resident[at+1])};
            if(shapes[shape][cell].x>1 || shapes[shape][cell].y>1 ||
               shapes[shape][cell].x< -1 || shapes[shape][cell].y< -1)
                throw DecodeError("Invalid native combat footprint offsets");
        }
    }
};
class Placement {
    DeploymentState& state_;
    const DeploymentParameters& parameters_;
    FormationTables tables_;
    std::array<std::array<std::array<std::array<bool,11>,6>,4>,2> available_{};
    std::array<int,2> anchor_x_{},anchor_y_{};
    std::array<unsigned,2> direction_{},first_width_{};

    std::uint8_t edge(unsigned side,unsigned direction) const {
        if(parameters_.presentation_mode>1)return 0;
        if(!parameters_.edge)throw DecodeError("Combat deployment requires original geometry edge binding");
        return parameters_.edge(signed_byte(parameters_.world_x+anchor_x_[side]),
                                signed_byte(parameters_.world_y+anchor_y_[side]),
                                static_cast<std::uint8_t>(direction));
    }
    std::uint8_t alternate(unsigned side,unsigned region) const {
        return tables_.alternate.at(direction_[side]*4+region);
    }
    int dx(unsigned direction)const{return state_.battlefield.tables.delta_x(direction);}
    int dy(unsigned direction)const{return state_.battlefield.tables.delta_y(direction);}
    bool try_position(DeploymentActor& actor,unsigned side,unsigned region,int anchor_x,int anchor_y,int local_x,int local_y) {
        if(local_x<0 || local_x>10 || local_y<0 || local_y>5 ||
           !available_[side][region][static_cast<unsigned>(local_y)][static_cast<unsigned>(local_x)])return false;
        const CombatPosition position{signed_byte(local_x+anchor_x*6+anchor_y*5+22),
                                      signed_byte(local_y+anchor_y*5+10)};
        // 4F3E:0CB9 combines all occupied footprint cells in source order;
        // 0 and1E are sticky. Otherwise the largest unsigned movement byte
        // wins, with later equal-cost terrain replacing earlier terrain.
        std::uint8_t terrain=0x17,largest=1;
        bool occupied=false;
        for(unsigned cell=0;cell<4;++cell) {
            if(actor.shape==0)break;
            const auto offset=tables_.shapes[actor.shape][cell];
            if(offset.x<0)continue;
            const int x=position.x+offset.x,y=position.y+offset.y;
            std::uint8_t current=0;
            if(Battlefield::in_bounds(x,y)) {
                current=state_.battlefield.terrain_at(x,y);
                if(state_.occupancy[static_cast<std::size_t>(y*50+x)]!=0)occupied=true;
            }
            if(current==0 || terrain==0)terrain=0;
            else if(current==0x1e || terrain==0x1e)terrain=0x1e;
            else {
                const auto cost=state_.battlefield.tables.properties(current)[0];
                if(cost>=largest){largest=cost;terrain=current;}
            }
        }
        if(occupied || terrain==0 || state_.battlefield.tables.properties(terrain)[0]==0xff)return false;
        available_[side][region][static_cast<unsigned>(local_y)][static_cast<unsigned>(local_x)]=false;
        actor.position=position;
        return true;
    }
    void rebuild_occupancy() {
        state_.occupancy.fill(0);
        for(const auto& actor:state_.actors) {
            if(actor.removed || !actor.position || !actor.shape)continue;
            for(const auto offset:tables_.shapes[actor.shape])if(offset.x>=0) {
                const auto x=actor.position->x+offset.x,y=actor.position->y+offset.y;
                if(!Battlefield::in_bounds(x,y))throw DecodeError("Combat footprint outside battlefield");
                auto& cell=state_.occupancy[static_cast<std::size_t>(y*50+x)];
                if(cell)throw DecodeError("Combat deployment produced overlapping footprints");
                cell=actor.native_slot;
            }
        }
    }
    bool place(DeploymentActor& actor,unsigned side) {
        bool first_row=true,exhausted=false;
        unsigned phase=1,row=0,region=0,span=0,offset=0;
        int region_x=anchor_x_[side],region_y=anchor_y_[side];
        int base_x=0,base_y=0,local_x=0,local_y=0;
        // The original byte scan is finite for supported tables, but carries
        // no host-safe bound if damaged inputs wrap. Fail without publishing.
        for(unsigned work=0;work<65536;++work) {
            const auto scan_direction=tables_.scan.at(direction_[side]*4+region)/2;
            const auto cardinal=[&](unsigned turn){return tables_.facing.at((scan_direction+turn)%4);};
            if(phase==1) {
                const unsigned at=(region!=0?4u:0u)+scan_direction;
                base_x=signed_byte(tables_.start_x.at(at)+dx(cardinal(2))*static_cast<int>(row));
                base_y=signed_byte(tables_.start_y.at(at)+dy(cardinal(2))*static_cast<int>(row));
                local_x=base_x;local_y=base_y;offset=1;phase=2;span=1;
            } else if(phase==2) {
                local_x=signed_byte(base_x+dx(cardinal(1))*static_cast<int>(offset));
                local_y=signed_byte(base_y+dy(cardinal(1))*static_cast<int>(offset));
                phase=3;++span;
            } else if(phase==3) {
                local_x=signed_byte(base_x+dx(cardinal(3))*static_cast<int>(offset));
                local_y=signed_byte(base_y+dy(cardinal(3))*static_cast<int>(offset));
                phase=2;++offset;++span;
            }
            const bool outside=local_x<0 || local_y<0 || local_x>10 || local_y>5;
            const bool outside_both=(local_x<0 || local_x>10) && (local_y<0 || local_y>5);
            if(phase>1 && ((outside && !outside_both) ||
                (first_row && static_cast<int>(span)>=signed_byte(static_cast<int>(first_width_[side]))) ||
                (!first_row && span>11))) {
                ++row;
                if(side==0 && (direction_[0]&1u) && region==0 && row==1) {
                    bool skip=false;
                    for(unsigned next=1;next<=3;++next)if(edge(side,alternate(side,next))!=1)skip=true;
                    if(skip)++row;
                }
                phase=1;first_row=false;
            }
            if(outside && outside_both) {
                phase=0;
                while(region<3 && phase!=1) {
                    ++region;const auto next=alternate(side,region);
                    if(edge(side,next)!=1) {
                        region_x=signed_byte(anchor_x_[side]+dx(next));
                        region_y=signed_byte(anchor_y_[side]+dy(next));
                        row=0;phase=1;
                    }
                }
                if(phase!=1)exhausted=true;
            }
            if(!outside && try_position(actor,side,region,region_x,region_y,local_x,local_y))return true;
            if(exhausted)return false;
            // row/span/offset are native bytes.
            row=static_cast<std::uint8_t>(row);span=static_cast<std::uint8_t>(span);offset=static_cast<std::uint8_t>(offset);
        }
        throw DecodeError("Combat deployment scan exceeds supported traversal bound");
    }
public:
    Placement(DeploymentState& state,const IFileSystem& files,const DeploymentParameters& parameters)
        :state_(state),parameters_(parameters),tables_(files) {
        direction_={static_cast<unsigned>(parameters.native_direction/2),
                    static_cast<unsigned>((parameters.native_direction+4)%8/2)};
        anchor_x_[1]=signed_byte(static_cast<int>(parameters.separation)*dx(parameters.native_direction));
        anchor_y_[1]=signed_byte(static_cast<int>(parameters.separation)*dy(parameters.native_direction));
        std::array<unsigned,2> active{};
        for(const auto& actor:state_.actors)if(actor.character.active())++active.at(actor.character.byte(0x10e));
        for(unsigned side=0;side<2;++side) {
            first_width_[side]=(active[side]+1)/2;
            for(unsigned region=0;region<4;++region)for(unsigned y=0;y<6;++y)for(unsigned x=0;x<11;++x) {
                const auto orientation=region==1?4u:direction_[side];
                const auto at=orientation*12+y*2;
                available_[side][region][y][x]=tables_.limits[at]<=x && x<=tables_.limits[at+1];
            }
        }
    }
    void run() {
        unsigned ordinal=0,slot=1;
        for(auto& actor:state_.actors) {
            ++ordinal;const auto side=actor.character.byte(0x10e);
            actor.extension.fill(0);actor.extension[0x13]=ordinal>parameters_.persistent_party_count?1:0;
            actor.extension[9]=tables_.facing[parameters_.native_direction/2];
            if(side==1)actor.extension[9]=static_cast<std::uint8_t>((actor.extension[9]+4)%8);
            const auto identity=actor.character.byte(0x84)&0x7f;
            if(side==0 && actor.extension[0x13] && (!identity || identity>102))
                actor.character.set_byte(0x84,static_cast<std::uint8_t>(std::min<unsigned>(parameters_.identity_parameter,100)-128));
            actor.native_slot=static_cast<std::uint8_t>(slot);
            actor.shape=actor.character.byte(0x6c)&7;
            if(actor.shape>4)throw DecodeError("Combat footprint selector outside verified source shapes");
            if(!place(actor,side)) {
                actor.shape=0;
                if(actor.extension[0x13]){actor.removed=true;continue;}
            } else if(!actor.character.active()) {
                actor.shape=0;
                if(!parameters_.duel_context && !actor.extension[0x13]) {
                    const auto position=*actor.position;
                    if(!Battlefield::in_bounds(position.x,position.y))throw DecodeError("Combat body outside battlefield");
                    auto& terrain=state_.battlefield.terrain[static_cast<std::size_t>(position.y*50+position.x)];
                    state_.bodies.push_back({actor.id,position,terrain});terrain=0x1f;
                }
            }
            rebuild_occupancy();++slot;
        }
    }
};
}
DeploymentState DeploymentService::build(const IFileSystem& files,const Battlefield& battlefield,
                                         std::vector<DeploymentActor> actors,const DeploymentParameters& parameters) {
    if(actors.empty() || actors.size()>71)throw DecodeError("Combat deployment requires at most eight party and63 encounter participants");
    if(parameters.persistent_party_count>8 || parameters.persistent_party_count>actors.size())
        throw DecodeError("Combat deployment original-party count is inconsistent with participants");
    if(parameters.native_direction>6 || (parameters.native_direction&1))throw DecodeError("Invalid native deployment direction");
    std::set<std::pair<CombatActorOrigin,std::uint64_t>> identities;
    for(auto& actor:actors) {
        if(!identities.emplace(actor.id.origin,actor.id.value).second)throw DecodeError("Duplicate combat actor identity");
        if(actor.id.origin==CombatActorOrigin::encounter && !actor.id.value)throw DecodeError("Missing encounter combatant identity");
        if(actor.character.byte(0x10e)>1)throw DecodeError("Unsupported combat side");
        actor.position.reset();actor.removed=false;actor.shape=0;actor.native_slot=0;
    }
    DeploymentState result{battlefield,std::move(actors),{}, {}};
    Placement(result,files,parameters).run();return result;
}
}
