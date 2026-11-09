#include "gbengine/party/character_creation_service.hpp"
#include "gbengine/party/character_recovery_service.hpp"
#include "gbengine/party/equipment.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
unsigned roll(std::mt19937& random,unsigned sides) {
    if(!sides || sides>256)throw DecodeError("Invalid creation die");
    const auto threshold=(std::uint32_t{0}-sides)%sides;
    std::uint32_t n;do{n=static_cast<std::uint32_t>(random());}while(n<threshold);
    return n%sides+1;
}
std::uint8_t dice(std::mt19937& random,unsigned count,unsigned sides) {
    std::uint8_t total=0;for(unsigned i=0;i<count;++i)total=static_cast<std::uint8_t>(total+roll(random,sides));return total;
}
constexpr std::array<std::array<int,6>,8> racial{{{},{{0,0,0,0,1,-1}},{{0,0,0,1,-1,0}},{},{},{{-1,0,0,1,0,0}},{{1,0,0,0,1,-2}},{}}};
}
std::uint8_t CharacterCreationService::byte(unsigned offset)const {
    if(0xc7c0u+offset>=module_.size())throw DecodeError("Creation source table is truncated");
    return module_[0xc7c0u+offset];
}
std::uint16_t CharacterCreationService::word(unsigned offset)const {return static_cast<std::uint16_t>(byte(offset)|(byte(offset+1)<<8));}
CharacterCreationService CharacterCreationService::load(const IFileSystem& files) {
    CharacterCreationService result;result.module_=decode_start_data(files.read_asset("START.EXE"));result.overlay_=files.read_asset("GAME.OVR");
    (void)result.byte(0x43ef);if(result.overlay_.size()<0x1997f+0x56c)throw DecodeError("Creation native set data is truncated");return result;
}
std::vector<std::uint8_t> CharacterCreationService::classes(std::uint8_t race)const {
    if(race<1 || race>7 || race==6)throw DecodeError("Race is not offered by native creation");
    const auto count=byte(0x3e17+race*12u);if(count>11)throw DecodeError("Invalid native race/class count");
    std::vector<std::uint8_t> result;for(unsigned i=1;i<=count;++i)result.push_back(byte(0x3e17+race*12u+i));return result;
}
std::vector<std::uint8_t> CharacterCreationService::alignments(std::uint8_t cls)const {
    if(cls>=17)throw DecodeError("Invalid creation class");
    const auto count=byte(0x3fe7+cls*10u);if(!count || count>9)throw DecodeError("Invalid native alignment count");
    std::vector<std::uint8_t> result;for(unsigned i=1;i<=count;++i)result.push_back(byte(0x3fe7+cls*10u+i));return result;
}
CharacterDraft CharacterCreationService::begin(CreationChoices choices,const std::mt19937& random)const {
    const auto offered=classes(choices.race),alignment=alignments(choices.character_class);
    if(choices.sex>1 || std::find(offered.begin(),offered.end(),choices.character_class)==offered.end() ||
       std::find(alignment.begin(),alignment.end(),choices.alignment)==alignment.end())throw DecodeError("Creation choice is not source eligible");
    CharacterDraft out{Character::decode(Bytes(285)),choices,random};auto& c=out.character;
    const auto put=[&](unsigned at,unsigned n){c.set_byte(at,static_cast<std::uint8_t>(n));};
    for(unsigned i=1;i<=6;++i) {const auto colour=byte(0x3cf5+i);put(0xc0+i,colour+(colour+8)*16u);}
    put(0xa9,50);put(0x2d,40);put(0x10c,0);put(0x10d,1);put(0xbb,1);put(0xbc,1);put(0x6c,1);
    put(0xab,roll(out.random,256)-1);put(0x2e,choices.race);put(0x9e,choices.sex);put(0x2f,choices.character_class);put(0xa0,choices.alignment);
    put(0xc0,choices.race==1 || choices.race==3 || choices.race==5?1:2);
    Bytes effects;const auto node=[&](std::uint8_t code){effects.insert(effects.end(),{code,0,0,255,0,0,0,0,0});};
    if(choices.race==1){node(0x5a);node(0x61);node(0x1a);node(0x2f);}
    else if(choices.race==5){node(0x5a);node(0x61);}
    else if(choices.race==3){node(0x61);node(0x12);node(0x2f);node(0x30);}
    else if(choices.race==2)node(0x6b);else if(choices.race==4)node(0x7c);
    c.load_effects(effects,"GAME.OVR");
    for(auto& effect:c.effects_)effect.source_offset=0x1997f+0x56c;
    c.load_inventory({});put(0x73,1);
    const auto cls=choices.character_class;
    if(cls<8)put(0x96+cls,1);
    else {
        if(cls>=8 && cls<=12)put(0x96,1);
        if(cls==8 || cls==9 || cls==13 || cls==14 || cls==15)put(0x98,1);
        if(cls==10)put(0x9a,1);
        if(cls==9 || cls==11 || cls==13 || cls==15 || cls==16)put(0x9b,1);
        if(cls==12 || cls==14 || cls==15 || cls==16)put(0x9c,1);
    }
    unsigned age_class=cls;
    if(cls>=8)age_class=cls==13 || cls==15 || cls==16?5:cls==14?2:0;
    const auto age=0x3e5b+choices.race*28u+age_class*4u;
    const auto increment=cls<8?dice(out.random,byte(age+2),byte(age+3)):static_cast<std::uint8_t>(byte(age+2)*byte(age+3));
    c.set_word(0x30,static_cast<std::uint16_t>(word(age)+increment));
    reroll(out);return out;
}
void CharacterCreationService::reroll(CharacterDraft& draft)const {
    auto next=draft;auto& c=next.character;const auto choice=next.choices;
    const auto put=[&](unsigned at,int n){c.set_byte(at,static_cast<std::uint8_t>(n));};
    const auto young=c.word(0x30)<=word(0x3f31+choice.race*10u);
    const auto mature=c.word(0x30)>word(0x3f33+choice.race*10u);
    const auto old=c.word(0x30)>word(0x3f35+choice.race*10u);
    const auto venerable=c.word(0x30)>word(0x3f37+choice.race*10u);
    const std::array<int,6> age_bonus{(young?1:0)-(mature?1:0)-(old?2:0)-(venerable?1:0),
        (mature?1:0)+(venerable?1:0),-(young?1:0)+(mature?1:0)+(old?1:0)+(venerable?1:0),
        -(old?2:0)-(venerable?1:0),(young?1:0)-(old?1:0)-(venerable?1:0),0};
    put(0x16,0);
    for(unsigned ability=0;ability<6;++ability) {
        int value=0;for(unsigned attempt=0;attempt<6;++attempt)value=std::max(value,static_cast<int>(dice(next.random,3,6))+racial[choice.race][ability]);
        value+=age_bonus[ability];
        const auto race_at=0x3da3+choice.race*16u+(ability?4u+ability*2u:choice.sex);
        const auto minimum=byte(race_at),maximum=byte(race_at+(ability?1u:2u));
        value=std::clamp(value,static_cast<int>(minimum),static_cast<int>(maximum));
        value=std::max(value,static_cast<int>(byte(0x3f81+choice.character_class*6u+ability)));
        // Native TEST in1AF8:0DC0 returns flags, which the pseudocode loses.
        // The literal set at393E:0525, not every class, gets Wisdom >=13.
        const auto cls=choice.character_class;
        if(ability==2 && (overlay_.at(0x1997f+0x525+cls/8u)&(1u<<(cls%8u))))value=std::max(value,13);
        put(0x10+ability,value);
        if(ability==0 && value==18 && c.byte(0x98))put(0x16,std::min(roll(next.random,100),static_cast<unsigned>(byte(0x3da7+choice.race*16u+choice.sex))));
    }
    put(0xa1,2);put(0xa3,1);put(0xa5,2);put(0xaa,1);put(0x72,12);put(0xb0,0);
    unsigned classes_count=0;std::uint8_t gold=0,hp=0;
    for(unsigned cls=0;cls<8;++cls)if(c.byte(0x96+cls)) {
        ++classes_count;put(0xb0,c.byte(0xb0)+byte(0x5d6+cls));
        gold=static_cast<std::uint8_t>(gold+dice(next.random,byte(0x4263+cls*2u),byte(0x4264+cls*2u)));
        if(cls==0) {
            put(0xb2,1+(c.byte(0x12)>12?1:0)+(c.byte(0x12)>13?1:0));
            for(unsigned id=1;id<=56;++id)if(byte(0x31fa+id*16u)==0 && byte(0x31fb+id*16u)==1)put(0x32+id,1);
        } else if(cls==5) {put(0xb5,1);for(const auto at:{0x3d,0x44,0x45,0x47})put(static_cast<unsigned>(at),1);}
    }
    c.set_word(0x8e,static_cast<std::uint16_t>(gold*10u/classes_count));
    for(unsigned cls=0;cls<8;++cls)if(c.byte(0x96+cls)) {
        const auto sides=byte(0x8be + cls);
        hp=static_cast<std::uint8_t>(hp+std::max<unsigned>(dice(next.random,byte(0x8b6+cls),sides),sides*2u/3u));
    }
    int con=static_cast<std::int8_t>(byte(0x408e + c.byte(0x14)));
    if(choice.character_class==2){if(c.byte(0x14)>16)++con;if(c.byte(0x14)>17)++con;}
    con*=static_cast<int>(classes_count);
    put(0xb1,hp/classes_count);put(0x32,std::max(1,(static_cast<int>(hp)+con)/static_cast<int>(classes_count)));put(0x11b,c.byte(0x32));
    CharacterRecoveryService::rebuild_class_cache(c,module_);
    draft=std::move(next);
}
Character CharacterCreationService::finish(const CharacterDraft& draft,std::string_view name,std::uint8_t head,std::uint8_t body)const {
    if(name.empty() || name.size()>15 || !std::all_of(name.begin(),name.end(),[](unsigned char n){return n>=32 && n<=126;}) || head<1 || head>14 || body<1 || body>12)
        throw DecodeError("Invalid native character identity or appearance");
    auto result=draft.character;result.set_byte(0,static_cast<std::uint8_t>(name.size()));
    for(unsigned i=1;i<=15;++i)result.set_byte(i,i<=name.size()?static_cast<std::uint8_t>(name[i-1]):0);
    result.set_byte(0xbb,head);result.set_byte(0xbc,body);return result;
}
Party CharacterCreationService::assemble(const IFileSystem& files,std::span<const Character> characters)const {
    if(characters.empty() || characters.size()>6)throw DecodeError("New party requires one to six created characters");
    Party result;result.item_catalog_=std::make_shared<ItemCatalog>(ItemCatalog::load(files));
    for(const auto& member:characters) {
        if(!member.effects_restored() || member.inventory().size())throw DecodeError("New party requires owned creation records");
        auto c=member;c.set_byte(0xbf,static_cast<std::uint8_t>(result.members_.size()));EquipmentRules::recalculate(c,*result.item_catalog_);result.members_.push_back(std::move(c));
    }
    result.selected_=0;++result.revision_;return result;
}
void CharacterCreationService::append(Party& party,const Character& member)const {
    if(!party.item_catalog_ || !member.effects_restored() || member.inventory().size())throw DecodeError("Adding a created character requires an owned source catalog and empty inventory");
    unsigned pcs=0;std::array<bool,8> used{};
    for(const auto& old:party.members_) {
        if(old.byte(0x84)<128)++pcs;
        if(old.byte(0xbf)<8)used[old.byte(0xbf)]=true;
        if(old.name()==member.name() && old.byte(0xab)==member.byte(0xab))throw DecodeError("That character already belongs to the party");
    }
    if(pcs>=6 || party.size()>=8)throw DecodeError("The party is full");
    unsigned ordinal=0;while(ordinal<8 && used[ordinal])++ordinal;
    if(ordinal==8)throw DecodeError("No native character identity is available");
    auto next=party;auto c=member;c.set_byte(0xbf,static_cast<std::uint8_t>(ordinal));EquipmentRules::recalculate(c,*next.item_catalog_);
    next.members_.push_back(std::move(c));next.selected_=next.members_.size()-1;++next.revision_;party=std::move(next);
}
void CharacterCreationService::remove(Party& party,std::size_t member)const {
    if(member>=party.size())throw DecodeError("Character removal selection is stale");
    if(party.member(member).byte(0x84)>=128)throw DecodeError("NPC character-menu removal requires its quest ownership context");
    auto next=party;next.members_.erase(next.members_.begin()+static_cast<std::ptrdiff_t>(member));
    next.selected_=next.members_.empty()?std::nullopt:std::optional<std::size_t>{member?member-1:0};++next.revision_;party=std::move(next);
}
void CharacterCreationService::adjust(Party& party,std::size_t member,unsigned field,bool increase)const {
    if(member>=party.size() || field>6)throw DecodeError("Invalid character modification field");
    auto next=party;auto& c=next.members_[member];
    if(c.word(0xac) || c.word(0xae))throw DecodeError("Only a character with no experience can be modified");
    if(c.byte(0x2e)<1 || c.byte(0x2e)>7 || c.byte(0x2f)>=17 || c.byte(0x9e)>1)throw DecodeError("Unverified character modification profile");
    const auto put=[&](unsigned at,int n){c.set_byte(at,static_cast<std::uint8_t>(n));};
    const auto hp_bound=[&](bool maximum) {
        unsigned count=0;std::uint8_t total=0;
        constexpr std::array<unsigned,8> sides{8,8,10,10,8,4,6,4};
        for(unsigned cls=0;cls<8;++cls)if(c.byte(0x96+cls) && c.byte(0x96+cls)<128) {
            ++count;const unsigned n=c.byte(0x96+cls)+(cls==4 || cls==7?1u:0u);
            total=static_cast<std::uint8_t>(total+n*(maximum?sides[cls]:1u));
        }
        if(!count)throw DecodeError("Character modification has no native class");
        int con=static_cast<std::int8_t>(byte(0x408e + c.byte(0x14)));
        if(c.byte(0x2f)==2){if(c.byte(0x14)>16)++con;if(c.byte(0x14)>17)++con;}
        return static_cast<std::uint8_t>(std::max(1,(static_cast<int>(total)+con*static_cast<int>(count))/static_cast<int>(count)));
    };
    if(field<6) {
        put(0x10+field,c.byte(0x10+field)+(increase?1:-1));
        const unsigned at=0x3da3+c.byte(0x2e)*16u+(field?4u+field*2u:c.byte(0x9e));
        const auto minimum=byte(at),maximum=byte(at+(field?1u:2u));
        if(field==0 && !increase && c.byte(0x16)){put(0x16,c.byte(0x16)-1);put(0x10,c.byte(0x10)+1);}
        else if(!increase && (c.byte(0x10+field)<minimum || c.byte(0x10+field)<byte(0x3f81+c.byte(0x2f)*6u+field)))put(0x10+field,minimum);
        else if(increase && c.byte(0x10+field)>maximum) {
            if(field==0 && c.byte(0x10)>18 && c.byte(0x98) && c.byte(0x16)<byte(0x3da7+c.byte(0x2e)*16u+c.byte(0x9e)))put(0x16,c.byte(0x16)+1);
            put(0x10+field,maximum);
        }
        if(field==2) {
            if(c.byte(0xb2))put(0xb2,1);
            constexpr std::array<unsigned,5> thresholds{12,13,14,15,16},slots{0xb2,0xb2,0xb3,0xb3,0xb4};
            for(unsigned i=0;i<5;++i)if(c.byte(0x12)>thresholds[i] && c.byte(slots[i]))put(slots[i],c.byte(slots[i])+1);
        }
        if(field==4){if(increase && c.byte(0x32)<hp_bound(false))put(0x32,hp_bound(false));if(!increase && c.byte(0x32)>hp_bound(true))put(0x32,hp_bound(true));put(0x11b,c.byte(0x32));}
    } else {
        put(0x32,c.byte(0x32)+(increase?1:-1));
        if(increase && c.byte(0x32)>hp_bound(true))put(0x32,hp_bound(true));
        if(!increase && c.byte(0x32)<hp_bound(false))put(0x32,hp_bound(false));
        put(0x11b,c.byte(0x32));
    }
    EquipmentRules::recalculate(c,next.item_catalog());++next.revision_;party=std::move(next);
}
void CharacterCreationService::finish_modification(Party& party,std::size_t member,const Character& baseline,bool keep)const {
    if(member>=party.size() || party.member(member).byte(0xbf)!=baseline.byte(0xbf))throw DecodeError("Stale modified character");
    auto next=party;auto& c=next.members_[member];
    if(keep)c.set_byte(0x85,1);
    else {for(unsigned i=0;i<7;++i)c.set_byte(0x10+i,baseline.byte(0x10+i));c.set_byte(0x32,baseline.byte(0x32));c.set_byte(0x11b,baseline.byte(0x32));}
    // Native Exit restores these seven fields/HP, then0BB8 only. In particular
    // it does not replay the wisdom-capacity helper or a class rebuild.
    EquipmentRules::recalculate(c,next.item_catalog());++next.revision_;party=std::move(next);
}
Bytes CharacterCreationService::encode_character(const IFileSystem& files,const Character& c)const {
    if(!c.effects_restored() || c.inventory().size() || c.byte(0xc7) || c.name().empty() || c.byte(0x84)>=128)
        throw DecodeError("Character library currently requires an owned player with empty inventory");
    Bytes payload;
    for(const auto file:{"START.EXE","GAME.OVR","ITEMS"}){const auto hash=sha256(files.read_asset(file));payload.insert(payload.end(),hash.begin(),hash.end());}
    for(unsigned i=0;i<285;++i)payload.push_back(c.byte(i));
    const auto number=[&](std::uint64_t n,unsigned width){for(unsigned i=0;i<width;++i)payload.push_back(static_cast<std::uint8_t>(n>>(i*8u)));};
    if(c.effects().size()>65535)throw DecodeError("Character effect count exceeds native bound");
    number(c.effects().size(),4);
    for(const auto& e:c.effects()) {
        if(e.equipment_item_id || e.item_origin || e.source_file.size()>255 || e.source_offset>0xffffffffu || (e.spell_id && e.item_cast_id))throw DecodeError("Character library has unsupported item-effect ownership");
        payload.insert(payload.end(),e.payload.begin(),e.payload.end());number(e.source_file.size(),2);payload.insert(payload.end(),e.source_file.begin(),e.source_file.end());number(e.source_offset,4);
        number(e.spell_id.has_value(),1);if(e.spell_id)number(*e.spell_id,1);
        number(e.item_cast_id.has_value(),1);if(e.item_cast_id)number(*e.item_cast_id,1);
    }
    if(payload.size()>1024*1024)throw DecodeError("Character library entry exceeds size bound");
    Bytes result{'G','B','C','H','A','R',0,1};const auto hash=sha256(payload);result.insert(result.end(),hash.begin(),hash.end());result.insert(result.end(),payload.begin(),payload.end());
    // Publication never relies on a write that its own decoder cannot read.
    (void)decode_character(files,result);return result;
}
Character CharacterCreationService::decode_character(const IFileSystem& files,std::span<const std::uint8_t> bytes)const {
    constexpr std::array<std::uint8_t,8> signature{'G','B','C','H','A','R',0,1};
    if(bytes.size()<553 || bytes.size()>1024*1024+72 || !std::equal(signature.begin(),signature.end(),bytes.begin()))throw DecodeError("Unsupported character library format");
    const auto payload=bytes.subspan(72);
    if(std::string(bytes.begin()+8,bytes.begin()+72)!=sha256(payload))throw DecodeError("Character library checksum mismatch");
    std::size_t at=0;
    const auto take=[&](std::size_t length){if(length>payload.size()-at)throw DecodeError("Truncated character library record");const auto part=payload.subspan(at,length);at+=length;return part;};
    const auto number=[&](unsigned width){const auto part=take(width);std::uint64_t value=0;for(unsigned i=0;i<width;++i)value|=static_cast<std::uint64_t>(part[i])<<(i*8u);return value;};
    for(const auto file:{"START.EXE","GAME.OVR","ITEMS"}){const auto hash=take(64);if(std::string(hash.begin(),hash.end())!=sha256(files.read_asset(file)))throw DecodeError("Character library source fingerprint mismatch");}
    const auto raw=take(285);auto c=Character::decode(raw);
    for(unsigned i=0;i<285;++i)if(c.byte(i)!=raw[i])throw DecodeError("Character library contains native process pointers");
    if(c.byte(0)==0 || c.byte(0xc7) || c.byte(0x84)>=128 || c.byte(0xbb)<1 || c.byte(0xbb)>14 || c.byte(0xbc)<1 || c.byte(0xbc)>12)throw DecodeError("Invalid stored player character");
    const auto count=number(4);if(count>65535)throw DecodeError("Character library effect count exceeds bound");
    for(std::uint64_t i=0;i<count;++i) {
        CharacterEffect e;const auto raw_effect=take(5);std::copy(raw_effect.begin(),raw_effect.end(),e.payload.begin());
        const auto length=number(2);if(length>255)throw DecodeError("Character effect source exceeds bound");const auto source=take(static_cast<std::size_t>(length));e.source_file.assign(source.begin(),source.end());e.source_offset=static_cast<std::size_t>(number(4));
        const auto spell=number(1);if(spell>1)throw DecodeError("Invalid character spell provenance flag");if(spell){const auto id=number(1);if(id<1 || id>56)throw DecodeError("Invalid character spell provenance");e.spell_id=static_cast<std::uint8_t>(id);}
        const auto item=number(1);if(item>1 || (item && spell))throw DecodeError("Conflicting character effect provenance");if(item){const auto id=number(1);if(id<57 || id>67)throw DecodeError("Invalid character item-cast provenance");e.item_cast_id=static_cast<std::uint8_t>(id);}
        c.effects_.push_back(std::move(e));
    }
    if(at!=payload.size())throw DecodeError("Trailing character library data");
    c.effects_restored_=true;return c;
}
}
