#include "gbengine/party/character_recovery_service.hpp"
#include <algorithm>
#include <bit>

namespace GBEngine {
namespace {
int signed_byte(std::uint8_t value) {return value<128?value:static_cast<int>(value)-256;}
class ResidentTables {
    std::span<const std::uint8_t> module_;
public:
    explicit ResidentTables(std::span<const std::uint8_t> module):module_(module) {}
    std::uint8_t byte(int offset) const {
        const auto at=0xc7c0u+static_cast<std::uint16_t>(offset);
        if(at>=module_.size())throw DecodeError("Character recovery source table is truncated");
        return module_[at];
    }
    std::uint32_t dword(int offset) const {
        std::uint32_t value=0;
        for(unsigned i=0;i<4;++i)value|=static_cast<std::uint32_t>(byte(offset+static_cast<int>(i)))<<(8*i);
        return value;
    }
};
void rebuild(Character& member,const ResidentTables& tables) {
    const auto put=[&](std::size_t at,int value){member.set_byte(at,static_cast<std::uint8_t>(value));};
    put(0x2d,0);
    for(int cls=0;cls<8;++cls) {
        const auto level=signed_byte(member.byte(0x96u+static_cast<unsigned>(cls)));
        const auto attack=tables.byte(0x3c7c+cls*11+level);
        if(member.byte(0x2d)<attack)put(0x2d,attack);
        // The native rebuild does not clear73 before taking this maximum.
        if(signed_byte(member.byte(0x73))<level)put(0x73,level);
        if(cls==2)put(0xa1,level<7?2:3);
        else if(cls==0 && level>1) {
            for(unsigned i=1;i<=3;++i)put(0xb1u+i,tables.byte(0x40bc+level*3+static_cast<int>(i)));
            for(const auto [wisdom,at]:std::array<std::pair<unsigned,std::size_t>,5>{{{12,0xb2},{13,0xb2},{14,0xb3},{15,0xb3},{16,0xb4}}})
                if(member.byte(0x12)>wisdom && member.byte(at)!=0)put(at,member.byte(at)+1);
            for(unsigned id=1;id<=56;++id) {
                const auto school=tables.byte(0x31fa+static_cast<int>(id)*16);
                const auto spell_level=signed_byte(tables.byte(0x31fb+static_cast<int>(id)*16));
                const auto capacity=0xb1+spell_level;
                if(capacity<0 || capacity>=285)throw DecodeError("Character spell capacity source index is invalid");
                if(school==0 && member.byte(static_cast<std::size_t>(capacity))!=0)put(0x32u+id,1);
            }
        } else if(cls==5 && level>1)
            for(unsigned i=1;i<=3;++i)put(0xb4u+i,tables.byte(0x41d4+level*3+static_cast<int>(i)));
    }
    for(int kind=0;kind<5;++kind) {
        put(0x6du+static_cast<unsigned>(kind),20);
        for(int cls=0;cls<8;++cls) {
            const auto level=signed_byte(member.byte(0x96u+static_cast<unsigned>(cls)));
            if(level>0) {
                const auto save=tables.byte(0x426e +cls*45+level*5+kind);
                if(save<member.byte(0x6du+static_cast<unsigned>(kind)))put(0x6du+static_cast<unsigned>(kind),save);
            }
        }
    }
    const auto thief=signed_byte(member.byte(0x9c));
    if(thief>0)for(int skill=1;skill<=8;++skill) {
        const auto modifier=signed_byte(tables.byte(0x3d3b+signed_byte(member.byte(0x2e))*8+skill));
        const auto base=tables.byte(0x3cf3+thief*8+skill);
        if(modifier<0 && base<-modifier)put(0x76u+static_cast<unsigned>(skill),0);
        else {
            auto value=static_cast<int>(base)+modifier;
            if(skill<6)value+=signed_byte(tables.byte(0x3d4e +member.byte(0x13)*5+skill));
            put(0x76u+static_cast<unsigned>(skill),value);
        }
    }
}
}
bool CharacterRecoveryService::advancement_capped(const Character& member,std::uint8_t cls) {
    if(cls>=8)throw DecodeError("Character recovery class index is invalid");
    const auto level=signed_byte(member.byte(0x96u+cls));
    if(level<=0)return false;
    const auto strength=member.byte(0x10);
    switch(signed_byte(member.byte(0x2e))) {
        case 1:return cls==2 && ((level==8 && strength==17) || (level==7 && strength<17));
        case 2:return cls==2 && (level==7 || (level==6 && strength==17) || (level==5 && strength<17));
        case 3:return cls==2 && (level==6 || (level==5 && strength<18));
        case 4:return (cls==0 && level==5) || (cls==2 && (level==8 || (level==7 && strength==17) || (level==6 && strength<17)));
        case 5:return cls==2 && (level==6 || (level==5 && strength==17) || (level==4 && strength<17));
        default:return false;
    }
}
void CharacterRecoveryService::rebuild_class_cache(Character& member,std::span<const std::uint8_t> module) {
    auto staged=member;rebuild(staged,ResidentTables(module));member=std::move(staged);
}
bool CharacterRecoveryService::restore_level(Character& member,std::span<const std::uint8_t> module) {
    if(member.byte(0x74)==0)return false;
    const ResidentTables tables(module);
    auto staged=member;
    const auto amount=static_cast<std::uint8_t>(staged.byte(0x75)/staged.byte(0x74));
    for(const auto at:{0x32u,0x11bu,0xb1u})staged.set_byte(at,static_cast<std::uint8_t>(staged.byte(at)+amount));
    staged.set_byte(0x75,static_cast<std::uint8_t>(staged.byte(0x75)-amount));
    staged.set_byte(0x74,static_cast<std::uint8_t>(staged.byte(0x74)-1));
    int lowest_level=13;
    std::int32_t required_xp=10000000;
    std::optional<unsigned> selected;
    for(unsigned cls=0;cls<8;++cls) {
        const auto level=signed_byte(staged.byte(0x96u+cls));
        if(level<=0 || level>lowest_level)continue;
        const auto bits=tables.dword(0x409b+static_cast<int>(cls)*0x38+(level+1)*4);
        const auto xp=std::bit_cast<std::int32_t>(bits);
        if(xp>0 && xp<required_xp && !advancement_capped(staged,static_cast<std::uint8_t>(cls))) {
            lowest_level=level;required_xp=xp;selected=cls;
        }
    }
    if(!selected)throw DecodeError("Level recovery has no initialized native class selection");
    staged.set_byte(0x96u+*selected,static_cast<std::uint8_t>(staged.byte(0x96u+*selected)+1));
    const auto current_bits=static_cast<std::uint32_t>(staged.word(0xac))|(static_cast<std::uint32_t>(staged.word(0xae))<<16);
    if(std::bit_cast<std::int32_t>(current_bits)<required_xp) {
        const auto bits=std::bit_cast<std::uint32_t>(required_xp);
        staged.set_word(0xac,static_cast<std::uint16_t>(bits));staged.set_word(0xae,static_cast<std::uint16_t>(bits>>16));
    }
    rebuild(staged,tables);member=std::move(staged);return true;
}
}
