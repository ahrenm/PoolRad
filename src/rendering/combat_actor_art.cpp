#include "gbengine/rendering/combat_actor_art.hpp"
#include "gbengine/assets/records.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
constexpr std::array<std::uint32_t,16> ega{
    0xff000000,0xff0000aa,0xff00aa00,0xff00aaaa,0xffaa0000,0xffaa00aa,0xffaa5500,0xffaaaaaa,
    0xff555555,0xff5555ff,0xff55ff55,0xff55ffff,0xffff5555,0xffff55ff,0xffffff55,0xffffffff};
struct IndexedIcon {int width{},height{};Bytes pixels;};
IndexedIcon indexed(std::span<const std::uint8_t> bytes) {
    if(bytes.size()<17)throw DecodeError("Truncated native combat icon header");
    const auto h=read_u16(bytes,0);
    const auto w=static_cast<unsigned>(read_u16(bytes,2))*8;
    // 506E:04FC copies exactly the first frame. Paired poses are separately
    // loaded records, not wall-tile frame containers or PIC delta images.
    if(h==0 || h>200 || w==0 || w>320 || bytes[8]!=1 ||
       read_u16(bytes,4)!=0 || read_u16(bytes,6)!=0 ||
       bytes.size()!=17+static_cast<std::size_t>(w)*h/2)
        throw DecodeError("Unsupported native combat icon layout");
    IndexedIcon out{static_cast<int>(w),static_cast<int>(h),{}};
    out.pixels.reserve(static_cast<std::size_t>(w)*h);
    for(const auto value:bytes.subspan(17)) {
        out.pixels.push_back(static_cast<std::uint8_t>(value>>4));
        out.pixels.push_back(static_cast<std::uint8_t>(value&15));
    }
    return out;
}
Image colored(const IndexedIcon& icon,CombatIconKind kind) {
    Image result{icon.width,icon.height,0,0,{}};
    result.pixels.reserve(icon.pixels.size());
    for(auto value:icon.pixels) {
        if(value==0)result.pixels.push_back(0);
        else {
            if(value==13 && kind==CombatIconKind::encounter)value=8;
            else if(value==13 && kind==CombatIconKind::effect)value=0;
            result.pixels.push_back(ega[value]);
        }
    }
    return result;
}
Image mirrored(const Image& image) {
    auto result=image;
    for(int y=0;y<image.height;++y)
        std::reverse(result.pixels.begin()+static_cast<std::ptrdiff_t>(y*image.width),
                     result.pixels.begin()+static_cast<std::ptrdiff_t>((y+1)*image.width));
    return result;
}
IndexedIcon component(const IFileSystem& files,const std::string& file,std::uint8_t record) {
    return indexed(load_record(files,file,record));
}
Image portrait(const IndexedIcon& head,const IndexedIcon& body,const Character& character,
               const std::array<std::uint8_t,6>& slots) {
    if(body.width!=24 || body.height!=24 || head.width!=body.width || head.height>body.height)
        throw DecodeError("Character combat icon components have incompatible dimensions");
    auto combined=body;
    for(std::size_t i=0;i<head.pixels.size();++i)
        combined.pixels[i]=static_cast<std::uint8_t>(combined.pixels[i]|head.pixels[i]);
    Image result{body.width,body.height,0,0,{}};
    result.pixels.reserve(combined.pixels.size());
    for(auto value:combined.pixels) {
        // Transparency is the AND of the two source masks, retained before
        // remapping: assigning a chosen component black cannot erase it.
        if(value==0)result.pixels.push_back(0);
        else {
            for(std::size_t i=0;i<slots.size();++i)if(value==slots[i]) {
                value=static_cast<std::uint8_t>(character.byte(0xc1+i)&15);break;
            } else if(value==slots[i]+8) {
                value=static_cast<std::uint8_t>(character.byte(0xc1+i)>>4);break;
            }
            result.pixels.push_back(ega[value]);
        }
    }
    return result;
}
std::array<std::uint8_t,6> native_slots(const IFileSystem& files) {
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    constexpr std::size_t ds=0xc7c0;
    if(module.size()<ds+0x3cfc)throw DecodeError("Truncated character combat icon table profile");
    std::array<std::uint8_t,6> slots{};
    std::copy_n(module.begin()+ds+0x3cf6,slots.size(),slots.begin());
    if(slots!=std::array<std::uint8_t,6>{1,2,3,4,6,7} ||
       module[ds+0x8c5]!=4 || module[ds+0x8c6]!='S' || module[ds+0x8c7]!='T')
        throw DecodeError("Unsupported character combat icon table profile");
    return slots;
}
CombatActorArtEntry character_entry(const IFileSystem& files,const Character& character,
                                   CombatActorIdentity id,const std::array<std::uint8_t,6>& slots) {
    CombatActorArtEntry entry;entry.actor=id;
    const auto style=character.byte(0xc0);
    // Source C0=0/1 selects the base pair; only suffixT adds64.
    if(style>2)throw DecodeError("Unsupported character combat icon style");
    const auto shift=style==2?64u:0u;
    const auto head_selector=static_cast<unsigned>(character.byte(0xbd));
    const auto body_selector=static_cast<unsigned>(character.byte(0xbe));
    if(head_selector>=64 || body_selector>=64)
        throw DecodeError("Character combat icon selector exceeds the paired record range");
    for(unsigned pose=0;pose<2;++pose) {
        const auto head_record=static_cast<std::uint8_t>(head_selector+shift+pose*128);
        const auto body_record=static_cast<std::uint8_t>(body_selector+shift+pose*128);
        const auto head=component(files,"CHEAD.DAX",head_record);
        const auto body=component(files,"CBODY.DAX",body_record);
        entry.sources.push_back({"CHEAD.DAX",head_record});entry.sources.push_back({"CBODY.DAX",body_record});
        entry.frames[pose][0]=portrait(head,body,character,slots);
        entry.frames[pose][1]=mirrored(entry.frames[pose][0]);
    }
    return entry;
}
}
Image decode_combat_icon(std::span<const std::uint8_t> bytes,CombatIconKind kind) {
    return colored(indexed(bytes),kind);
}
CombatActorArt CombatActorArt::load(const IFileSystem& files,const DeploymentState& deployment,
                                  const EncounterState& encounter) {
    CombatActorArt result;
    const auto slots=native_slots(files);
    for(unsigned pose=0;pose<2;++pose)
        result.cursor_[pose]=decode_combat_icon(load_record(files,"COMSPR.DAX",static_cast<std::uint8_t>(25+128*pose)),CombatIconKind::effect);
    for(const auto& actor:deployment.actors) {
        if(actor.removed)continue;
        if(std::any_of(result.entries_.begin(),result.entries_.end(),[&](const auto& value){return value.actor==actor.id;}))
            throw DecodeError("Duplicate actor combat artwork identity");
        CombatActorArtEntry entry;entry.actor=actor.id;
        if(actor.id.origin==CombatActorOrigin::party) {
            entry=character_entry(files,actor.character,actor.id,slots);
        } else {
            const auto group=std::find_if(encounter.groups.begin(),encounter.groups.end(),[&](const auto& value){
                return std::any_of(value.creatures.begin(),value.creatures.end(),[&](const auto& creature){return creature.id.value==actor.id.value;});
            });
            if(group==encounter.groups.end() || group->native_group!=actor.character.byte(0xbf) ||
               group->art_source_file.empty())
                throw DecodeError("Encounter actor lacks its owned combat icon binding");
            for(unsigned pose=0;pose<2;++pose) {
                const auto record=static_cast<std::uint8_t>(group->art_selector+pose*128);
                entry.sources.push_back({group->art_source_file,record});
                entry.frames[pose][0]=decode_combat_icon(load_record(files,group->art_source_file,record),CombatIconKind::encounter);
                entry.frames[pose][1]=mirrored(entry.frames[pose][0]);
            }
        }
        result.entries_.push_back(std::move(entry));
    }
    return result;
}
CombatActorArt CombatActorArt::load_character(const IFileSystem& files,const Character& character,CombatActorIdentity id) {
    if(id.origin!=CombatActorOrigin::party)throw DecodeError("Character icon preview requires a persistent component identity");
    CombatActorArt result;const auto slots=native_slots(files);
    for(unsigned pose=0;pose<2;++pose)
        result.cursor_[pose]=decode_combat_icon(load_record(files,"COMSPR.DAX",static_cast<std::uint8_t>(25+128*pose)),CombatIconKind::effect);
    result.entries_.push_back(character_entry(files,character,id,slots));return result;
}
const Image& CombatActorArt::image(CombatActorIdentity actor,unsigned pose,unsigned facing) const {
    if(pose>=2 || facing>=8)throw DecodeError("Combat icon pose or facing is outside the native range");
    const auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.actor==actor;});
    if(found==entries_.end())throw DecodeError("Combat actor artwork is unavailable");
    return found->frames[pose][facing>=4?1:0];
}
const Image& CombatActorArt::cursor(unsigned pose) const {
    if(pose>=2 || cursor_[pose].pixels.empty())throw DecodeError("Combat inspection cursor artwork is unavailable");
    return cursor_[pose];
}
}
