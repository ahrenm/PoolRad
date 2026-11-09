#include "gbengine/world/world_state.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
constexpr std::array dx{0,1,0,-1},dy{-1,0,1,0};
unsigned index(Direction d) { return static_cast<unsigned>(d); }
}
const Character* WorldState::selected_encounter_actor()const {
    if(!encounter.selected_actor)return nullptr;
    const Character* found=nullptr;
    for(const auto& group:encounter.groups)for(const auto& actor:group.creatures)
        if(actor.id==*encounter.selected_actor) {
            if(found)throw DecodeError("Ambiguous selected encounter actor identity");
            found=&actor.character;
        }
    if(!found)throw DecodeError("Selected encounter actor no longer belongs to the encounter");
    return found;
}
Character* WorldState::selected_encounter_actor() {
    return const_cast<Character*>(static_cast<const WorldState&>(*this).selected_encounter_actor());
}
void WorldState::select_prepared_participant(std::uint8_t ordinal) {
    if(!encounter.setup_prepared || encounter.duel_context || encounter.special || removal_activity_armed || removal_name_armed)
        throw DecodeError("Prepared participant selection requires an ordinary non-destructive context");
    if(ordinal<party.size()) {party.select(ordinal);encounter.selected_actor.reset();return;}
    auto remaining=static_cast<std::size_t>(ordinal)-party.size();
    for(const auto& group:encounter.groups)for(const auto& actor:group.creatures) {
        if(remaining==0){encounter.selected_actor=actor.id;return;}
        --remaining;
    }
    // Native2182:02E0 leaves DS5D92 unchanged if traversal reaches null.
}
std::vector<const Character*> WorldState::participants() const {
    // A transient source graph is not proof of an imported persistent roster.
    if(party.size()==0)return {};
    std::vector<const Character*> members;members.reserve(party.size()+encounter.count);
    if(encounter.special && encounter.special->tactical_party.size()!=party.size())
        throw DecodeError("Special participant view lost its persistent roster");
    for(std::size_t i=0;i<party.size();++i)members.push_back(encounter.special?&encounter.special->tactical_party[i]:&party.member(i));
    for(const auto& group:encounter.groups)
        for(const auto& creature:group.creatures)members.push_back(&creature.character);
    return members;
}
std::optional<std::uint16_t> WorldState::party_strength() const {return CharacterQueries::strength_rating(participants());}
std::optional<std::uint16_t> WorldState::party_field_2f_check() const {return CharacterQueries::field_2f_check(participants());}
std::optional<bool> WorldState::party_has_item_type(std::uint8_t type) const {return CharacterQueries::has_item_type(participants(),type);}
std::optional<bool> WorldState::party_has_effect(std::uint8_t code) const {return CharacterQueries::has_effect(participants(),code);}
std::optional<std::array<std::uint16_t,3>> WorldState::party_field_summary(std::uint16_t field) const {return CharacterQueries::field_summary(participants(),field);}
std::optional<std::uint16_t> WorldState::read_value(std::uint16_t a) const {
    switch(a) {
    case 0x6e12:return static_cast<std::uint16_t>(resource_disk);
    case 0xfb:if(tile_query_available) return tile_query_x;break;
    case 0xfc:if(tile_query_available) return tile_query_y;break;
    case 0xb1:if(tile_query_available) return tile_query_value;break;
    case 0x35f:if(tile_query_available) return tile_query_result;break;
    case 0xc04b:return static_cast<std::uint16_t>(x);
    case 0xc04c:return static_cast<std::uint16_t>(y);
    case 0xc04d:return static_cast<std::uint16_t>(index(facing));
    case 0x033d:return static_cast<std::uint16_t>(index(facing)*2);
    case 0xc04e:if(map) return map->wall(x,y,facing);break;
    case 0xc04f:if(map) return map->event(x,y);break;
    default:break;
    }
    if(const auto* actor=selected_encounter_actor()) {
        if(a==0x6bb8)return actor->byte(0x84);
        if(a==0x6c0c)return static_cast<std::uint16_t>(actor->byte(0x10e)==0 && actor->byte(0x10f)!=0?0x80:actor->byte(0x10e)==1?0x81:0);
        return std::nullopt; // Wider selected-temporary-actor getters remain held.
    }
    return party.read_value(a);
}
bool WorldState::write_register(std::uint16_t a,std::uint16_t v) {
    const int byte=v&255;const int signed_byte=byte>=128?byte-256:byte;
    switch(a) {
    case 0x6b00:case 0x6c00:
        if(!party.selected_index() || encounter.setup_prepared || encounter.duel_context || encounter.special || !encounter.groups.empty())return false;
        if(a==0x6b00){if(v==0)removal_name_armed=true;return true;}
        if(v==0){removal_activity_armed=true;return true;}
        if(v>127 && party.member(*party.selected_index()).active()) {
            bool other=false;for(std::size_t i=0;i<party.size();++i)if(i!=*party.selected_index() && party.member(i).active())other=true;
            if(!other)return false; // Last-active-member terminal ownership is separate.
        }
        return party.write_value(a,v);
    case 0x3de:call_parameters[0]=v;return true;
    case 0xb8:call_parameters[1]=v;return true;
    case 0xb9:call_parameters[2]=v;return true;
    case 0x6e12:if(byte<1 || byte>8) throw DecodeError("Invalid resource disk");resource_disk=static_cast<unsigned>(byte);return true;
    case 0xfb:if(!tile_query_available) return false;tile_query_x=v;return true;
    case 0xfc:if(!tile_query_available) return false;tile_query_y=v;return true;
    case 0xb1:if(!tile_query_available) return false;tile_query_value=v;return true;
    case 0xc04b:x=signed_byte;return true;
    case 0xc04c:y=signed_byte;return true;
    case 0xc04d:facing=static_cast<Direction>(v%4);return true;
    // Native27A9:0C77 has no C04F store. It is a read-only scene event
    // query; every WORD write returns without changing geometry or cache.
    case 0xc04f:return true;
    default:
        if(auto* actor=selected_encounter_actor()) {
            if(a==0x6bb8){actor->set_byte(0x84,static_cast<std::uint8_t>(v>178?v-50:v));return true;}
            if(a==0x6c0c && (v==0 || v==0x80 || v==0x81)) {
                actor->set_byte(0x10e,static_cast<std::uint8_t>(v==0x81));
                actor->set_byte(0x10f,static_cast<std::uint8_t>(v!=0));return true;
            }
            return false; // Never redirect an unsupported actor write to Party.
        }
        return party.write_value(a,v);
    }
}
void WorldState::turn(int q) { facing=static_cast<Direction>((static_cast<int>(index(facing))+q%4+4)%4); }
MoveResult WorldState::move(bool backwards) {
    if(!map) throw DecodeError("Movement requires geometry");
    const auto d=static_cast<Direction>((index(facing)+(backwards?2:0))%4);
    const int nx=x+dx[index(d)],ny=y+dy[index(d)];
    if(!GeoMap::contains(x,y) || !GeoMap::contains(nx,ny)) return MoveResult::map_boundary;
    if(map->wall(x,y,d)) {
        const auto door=map->door(x,y,d);
        if(door==0) return MoveResult::solid_wall;
        if(door>=2) return MoveResult::locked_door;
    }
    x=nx;y=ny;return MoveResult::moved;
}
}
