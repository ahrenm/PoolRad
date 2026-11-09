#include "gbengine/party/party.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/items/item_catalog.hpp"
#include <algorithm>
namespace GBEngine {
Character Character::decode(std::span<const std::uint8_t> bytes,std::string name_id) {
    if(bytes.size()!=285 || bytes[0]>15) throw DecodeError("Invalid 285-byte character record or name length");
    for(std::size_t i=1;i<=bytes[0];++i)
        if(bytes[i]<32 || bytes[i]>126) throw DecodeError("Unsupported character name encoding");
    Character result;result.name_id_=std::move(name_id);std::copy(bytes.begin(),bytes.end(),result.bytes_.begin());
    // The native loader clears serialized far pointers. Retain their scalar
    // neighbors, but never interpret saved pointers as replacement addresses.
    for(const auto offset:{0x104,0x106,0xc8,0xca,0x7f,0x81,0x108,0x10a}) result.set_word(offset,0);
    // Native 0BB8 rebuilds thirteen equipped slots. Host identities live in
    // EquipmentRules; saved slot pointers are never usable object addresses.
    for(std::size_t slot=0;slot<13;++slot) {result.set_word(0xcc+4*slot,0);result.set_word(0xce +4*slot,0);}
    return result;
}
std::string Character::name() const {
    if(!name_id_.empty()) return StringStore::active().get(name_id_);
    return StringStore::active().runtime_text(std::string(bytes_.begin()+1,bytes_.begin()+1+bytes_[0]));
}
bool Character::equipment_effects_bound() const {
    for(std::size_t i=0;i<inventory_.size();++i) {
        const auto& item=inventory_.item(i);
        if(item.readied() && item.byte(0x3e)>=128 && !bound_equipment_effects_.contains(item.id())) return false;
    }
    return true;
}
namespace {
std::string_view table_name(std::uint8_t index,unsigned count,std::size_t base,unsigned stride) {
    if(index>=count) throw DecodeError("Unsupported character identity table index");
    return StringStore::active().get(pascal_string_id("START.EXE:module",base+stride*index));
}
}
std::string_view Character::race_name() const {return table_name(byte(0x2e),8,0xd6c3,10);}
std::string_view Character::class_name() const {return table_name(byte(0x2f),17,0xd4f8,27);}
std::string_view Character::alignment_name() const {return table_name(byte(0xa0),9,0xd713,17);}
std::string_view Character::sex_name() const {return table_name(byte(0x9e),2,0xd7ac,7);}
CharacterStats Character::stats() const {
    CharacterStats result;
    for(std::size_t i=0;i<6;++i) result.abilities[i]=byte(0x10+i);
    result.exceptional_strength=byte(0x16);result.maximum_hp=byte(0x32);result.current_hp=hit_points();
    result.age=word(0x30);result.experience=word(0xac)|(static_cast<std::uint32_t>(word(0xae))<<16);
    for(std::size_t i=0;i<8;++i) result.levels[i]=byte(0x96+i);
    for(std::size_t i=0;i<7;++i) result.money[i]=word(0x88+2*i);
    result.armor_class=60-byte(0x111);result.thac0=60-byte(0x110);
    result.encumbrance=word(0x102);result.movement=byte(0x11c);
    result.damage_dice=byte(0x115);result.damage_sides=byte(0x117);
    result.damage_bonus=byte(0x119)<128?byte(0x119):static_cast<int>(byte(0x119))-256;
    return result;
}
void Character::load_inventory(std::span<const std::uint8_t> bytes) {
    auto staged=Inventory::decode(bytes);
    if(staged.size()!=byte(0xc7)) throw DecodeError("Character inventory count disagrees with item sidecar");
    inventory_=std::move(staged);
    bound_equipment_effects_.clear();
}
void Character::load_effects(std::span<const std::uint8_t> bytes,std::string source) {
    if(bytes.size()%9 || bytes.size()/9>65535) throw DecodeError("Invalid original character effect sidecar");
    std::vector<CharacterEffect> staged;
    for(std::size_t at=0;at<bytes.size();at+=9) {
        CharacterEffect effect;std::copy_n(bytes.begin()+static_cast<std::ptrdiff_t>(at),5,effect.payload.begin());
        effect.source_file=source;effect.source_offset=at;staged.push_back(std::move(effect));
    }
    effects_=std::move(staged);effects_restored_=true;bound_equipment_effects_.clear();
}
std::string_view character_status_name(std::uint8_t status) {
    // Native resident DS:1047, Pascal string[12] table, stride 13.
    if(status>=9) return "Unknown";
    return StringStore::active().get(pascal_string_id("START.EXE:module",0xc7c0+0x1047+13*status));
}
void Character::apply_exploration_damage(std::uint8_t amount) {
    if(status()==6) return; // Native 27a9:20cc excludes already-dead characters.
    const unsigned hp=hit_points();
    const unsigned deficit=amount>hp?amount-hp:0;
    const unsigned remaining=amount<=hp?hp-amount:0;
    if(deficit>=10 || (remaining==0 && status()==1)) set_byte(0x10c,6);
    else if(deficit>0) set_byte(0x10c,5);
    else if(remaining==0) set_byte(0x10c,4);
    // Raw 10ba:22c9 tests set {0,1}; pseudocode does not preserve its flags.
    if(status()!=0 && status()!=1) {set_byte(0x10d,0);set_byte(0x11b,0);}
    else set_byte(0x11b,static_cast<std::uint8_t>(remaining));
}
DamageOutcome Party::damage_selected(std::uint16_t amount) {
    if(!selected_ || members_.empty()) throw DecodeError("Damage requires an imported party and selected character");
    auto& c=members_.at(*selected_);
    ++revision_;
    DamageOutcome result;result.member=*selected_;result.rolled_amount=amount;result.before_hp=c.hit_points();result.ignored=c.status()==6;
    if(!result.ignored) {
        result.message=StringStore::active().runtime_text(c.name()+(amount>static_cast<unsigned>(c.hit_points())+10?overlay_string(0x9554):overlay_string(0x955c)+std::to_string(amount)+overlay_string(0x9569)));
        // Native presentation receives the word; HP application takes its low byte.
        c.apply_exploration_damage(static_cast<std::uint8_t>(amount));
    }
    result.after_hp=c.hit_points();result.status=c.status();
    result.party_defeated=std::none_of(members_.begin(),members_.end(),[](const Character& member){return member.active();});
    return result;
}
std::uint16_t Character::word(std::size_t offset) const {
    return static_cast<std::uint16_t>(bytes_.at(offset)|(static_cast<unsigned>(bytes_.at(offset+1))<<8));
}
void Character::set_word(std::size_t offset,std::uint16_t value) {
    if(offset>=bytes_.size()-1) throw DecodeError("Character word outside record");
    bytes_[offset]=static_cast<std::uint8_t>(value);bytes_[offset+1]=static_cast<std::uint8_t>(value>>8);
    if(offset<=15) name_id_.clear();
}
void Party::import_characters(const IFileSystem& files,char slot) {
    if(slot>='a' && slot<='z') slot=static_cast<char>(slot-'a'+'A');
    if(slot!='A' && slot!='J') throw DecodeError("Character fixture slot must be A or J");
    std::vector<std::string> stems;
    for(int i=1;i<=6;++i) stems.push_back(std::string(slot=='A'?"CHRDATA":"CHRDATJ")+std::to_string(i));
    import_roster(files,stems,false);
}
void Party::restore_characters(const IFileSystem& files,std::span<const std::string> stems) {
    import_roster(files,stems,true);
}
void Party::import_roster(const IFileSystem& files,std::span<const std::string> stems,bool restore_effects) {
    if(stems.empty() || stems.size()>8) throw DecodeError("Original roster must contain 1..8 members");
    std::set<std::string> unique;
    for(const auto& stem:stems) {
        if(stem.empty() || stem.size()>8 || !std::all_of(stem.begin(),stem.end(),[](unsigned char c){return (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_';}) || !unique.insert(stem).second)
            throw DecodeError("Invalid or duplicate original character file stem");
    }
    std::vector<Character> staged;
    const auto catalog=std::make_shared<ItemCatalog>(ItemCatalog::load(files));
    auto next_id=next_item_id_;
    const auto assets=files.list_assets();
    for(const auto& stem:stems) {
        const auto name=stem+".SAV",items=stem+".ITM";
        const auto saved=files.read_asset(name);
        staged.push_back(Character::decode(saved,pascal_string_id(name,0)));
        if(std::find(assets.begin(),assets.end(),items)!=assets.end()) staged.back().load_inventory(files.read_asset(items));
        else staged.back().load_inventory({});
        for(std::size_t ordinal=0;ordinal<staged.back().inventory_.items_.size();++ordinal) {
            auto& item=staged.back().inventory_.items_[ordinal];(void)catalog->definition(item.type());
            item.id_=next_id++;item.origin_={items,std::nullopt,63*ordinal};
        }
        if(restore_effects) {
            const auto spells=stem+".SPC";
            if(std::find(assets.begin(),assets.end(),spells)!=assets.end()) {
                const auto effects=files.read_asset(spells);
                if(effects.empty()) throw DecodeError("Empty original character effect sidecar");
                staged.back().load_effects(effects,spells);
            }
            else {
                if(read_u16(saved,0x7f)!=0 || read_u16(saved,0x81)!=0) throw DecodeError("Missing saved character effect sidecar");
                staged.back().load_effects({},spells);
            }
            // 3D84:1446 assigns the lowest unused presentation ordinal while
            // appending each restored member; saved process ordinals are stale.
            staged.back().set_byte(0xbf,static_cast<std::uint8_t>(staged.size()-1));
        }
    }
    members_=std::move(staged);selected_=0;item_catalog_=catalog;next_item_id_=next_id;++revision_;
}
const ItemCatalog& Party::item_catalog() const {
    if(!item_catalog_) throw DecodeError("Item catalog requires an imported party");
    return *item_catalog_;
}
void Party::select(std::size_t index) {
    if(index>=members_.size()) throw DecodeError("Selected character outside imported party");
    selected_=index;++revision_;
}
std::optional<std::string> Party::selected_name() const {
    if(!selected_) return std::nullopt;
    return members_[*selected_].name();
}
namespace {
std::vector<const Character*> query_members(const Party& party) {
    std::vector<const Character*> members;members.reserve(party.size());
    for(std::size_t i=0;i<party.size();++i)members.push_back(&party.member(i));
    return members;
}
}
std::optional<std::uint16_t> Party::strength_rating() const {return CharacterQueries::strength_rating(query_members(*this));}
std::optional<std::uint16_t> Party::field_2f_check() const {return CharacterQueries::field_2f_check(query_members(*this));}
std::optional<bool> Party::has_item_type(std::uint8_t type) const {return CharacterQueries::has_item_type(query_members(*this),type);}
std::optional<bool> Party::has_effect(std::uint8_t code) const {return CharacterQueries::has_effect(query_members(*this),code);}
std::optional<std::array<std::uint16_t,3>> Party::field_summary(std::uint16_t field) const {return CharacterQueries::field_summary(query_members(*this),field);}
std::optional<std::uint16_t> CharacterQueries::strength_rating(std::span<const Character* const> members) {
    if(members.empty()) return std::nullopt;
    unsigned total=0;
    for(const auto* member:members) {
        const auto& c=*member;
        const unsigned bonus111=c.byte(0x111)>60?c.byte(0x111)-60:0;
        const unsigned bonus110=c.byte(0x110)>39?c.byte(0x110)-39:0;
        const unsigned rating=(4u*c.byte(0x96)+c.byte(0x11b)+5*bonus111+5*bonus110+8u*c.byte(0x9b))/10;
        // Native accumulator is a byte; every roster member contributes,
        // including inactive/dead members. Divide each contribution first.
        total=(total+rating)&255;
    }
    return static_cast<std::uint16_t>(total);
}
std::optional<std::uint16_t> CharacterQueries::field_2f_check(std::span<const Character* const> members) {
    if(members.empty()) return std::nullopt;
    return static_cast<std::uint16_t>(std::any_of(members.begin(),members.end(),[](const Character* c){return c->byte(0x2f)==4 || c->byte(0x2f)==10;}));
}
std::optional<bool> CharacterQueries::has_item_type(std::span<const Character* const> members,std::uint8_t type) {
    if(members.empty()) return std::nullopt;
    for(const auto* character:members)
        for(std::size_t i=0;i<character->inventory().size();++i)
            if(character->inventory().item(i).type()==type) return true;
    return false;
}
std::optional<bool> CharacterQueries::has_effect(std::span<const Character* const> members,std::uint8_t code) {
    if(members.empty()) return std::nullopt;
    bool complete=true;
    for(const auto* character:members) {
        if(!character->effects_restored()) {complete=false;continue;}
        for(const auto& effect:character->effects()) if(effect.code()==code) return true;
    }
    return complete?std::optional<bool>{false}:std::nullopt;
}
std::optional<std::array<std::uint16_t,3>> CharacterQueries::field_summary(std::span<const Character* const> members,std::uint16_t field) {
    if(members.empty() || (field!=0x6ba7 && field!=0x6c1b)) return std::nullopt;
    const std::size_t offset=field==0x6ba7?0x79:0x11c;
    std::uint16_t minimum=255,maximum=0,sum=0;
    for(const auto* character:members) {
        const auto value=character->byte(offset);
        minimum=std::min(minimum,static_cast<std::uint16_t>(value));
        maximum=std::max(maximum,static_cast<std::uint16_t>(value));
        sum=static_cast<std::uint16_t>(sum+value);
    }
    return std::array<std::uint16_t,3>{minimum,maximum,
        static_cast<std::uint16_t>(static_cast<std::uint8_t>(sum/members.size()))};
}
std::optional<std::uint16_t> Party::read_value(std::uint16_t address) const {
    if(!selected_) return std::nullopt;
    const auto& c=members_[*selected_];
    auto signed_byte=[&](std::size_t o){const int b=c.byte(o);return static_cast<std::uint16_t>(b<128?b:b-256);};
    if(address>=0x6ba5 && address<=0x6bac) return c.byte(0x76+address-0x6ba4);
    switch(address) {
    case 0x6b18:return c.byte(0x14);
    case 0x6b9b:return c.byte(0x6e);
    case 0x6ba0:return signed_byte(0x73);
    case 0x6bb8:return c.byte(0x84);
    case 0x6bbb:return c.word(0x88);
    case 0x6bbd:return c.word(0x8a);
    case 0x6bbf:return c.word(0x8c);
    case 0x6bc1:return c.word(0x8e);
    case 0x6bc3:return c.word(0x90);
    case 0x6bd8:return signed_byte(0xa0);
    case 0x6bf7:return c.word(0xb8);
    case 0x6bf9:return c.byte(0xba);
    case 0x6c00:return static_cast<std::uint16_t>(c.byte(0x10d)==0?0x80:1);
    case 0x6c0c:return static_cast<std::uint16_t>(c.byte(0x10e)==0 && c.byte(0x10f)!=0?0x80:c.byte(0x10e)==1?0x81:0);
    case 0x6c1b:return c.byte(0x11c);
    case 0x6db1:case 0x6db4:return static_cast<std::uint16_t>(*selected_);
    case 0x6dcf: {
        // Native 27A9:07BC branches on charisma byte15; no result is
        // initialized outside 3..25. Keep those invalid records unresolved.
        constexpr std::array<std::uint16_t,23> reaction{
            0,5,10,15,20,25,25,25,25,25,30,35,40,50,55,60,60,60,60,60,60,60,60};
        const auto charisma=c.byte(0x15);
        if(charisma<3 || charisma>25) return std::nullopt;
        return reaction[charisma-3];
    }
    default:return std::nullopt;
    }
}
bool Party::write_value(std::uint16_t address,std::uint16_t value) {
    if(!selected_) return false;
    ++revision_; // Conservatively invalidate pending item plans on host writes.
    auto& c=members_[*selected_];
    if(address>=0x6b20 && address<=0x6b70) {c.set_byte(0x17+((address+0x94e1)&255),static_cast<std::uint8_t>(value));return true;}
    switch(address) {
    case 0x6b00:return false; // Numeric name/removal latch belongs to WorldState.
    case 0x6c00:
        if(value==0)return false; // Resident84D2 belongs to WorldState.
        if(value>127){c.set_byte(0x10d,0);if(value==0x87)c.set_byte(0x10c,7);}
        return true;
    case 0x6bb8:c.set_byte(0x84,static_cast<std::uint8_t>(value>178?value-50:value));return true;
    case 0x6bbb:c.set_word(0x88,value);return true;
    case 0x6bbd:c.set_word(0x8a,value);return true;
    case 0x6bbf:c.set_word(0x8c,value);return true;
    case 0x6bc1:c.set_word(0x8e,value);return true;
    case 0x6bc3:c.set_word(0x90,value);return true;
    case 0x6bf7:c.set_word(0xb8,value);return true;
    case 0x6bf9:c.set_byte(0xba,static_cast<std::uint8_t>(value));return true;
    case 0x6c0c:
        if(value==0 || value==0x80 || value==0x81) {
            c.set_byte(0x10e,static_cast<std::uint8_t>(value==0x81));
            c.set_byte(0x10f,static_cast<std::uint8_t>(value!=0));return true;
        }
        return false;
    default:return false;
    }
}
}
