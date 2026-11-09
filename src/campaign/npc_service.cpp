#include "gbengine/campaign/npc_service.hpp"
#include "gbengine/party/character_recovery_service.hpp"
#include "gbengine/items/item_catalog.hpp"
#include "gbengine/party/equipment.hpp"
#include "gbengine/magic/equipment_effect_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <limits>

namespace GBEngine {
namespace {
unsigned npc_roll(std::mt19937& random,std::uint32_t sides) {
    const auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    std::uint32_t value;do {value=static_cast<std::uint32_t>(random());}while(value<threshold);
    return value%sides+1;
}
int signed_byte(std::uint8_t value) {return value<128?value:static_cast<int>(value)-256;}
std::string record_source(std::string file,std::uint8_t record) {
    constexpr char digits[]="0123456789ABCDEF";
    file+=':';file+='0';file+=digits[record>>4];file+=digits[record&15];return file;
}
Bytes archive_record(const IFileSystem& files,const std::vector<std::string>& assets,const std::string& file,std::uint8_t id,bool required) {
    if(std::find(assets.begin(),assets.end(),file)==assets.end()) {
        if(required)throw DecodeError("Required NPC character archive is missing");
        return {};
    }
    const DaxArchive archive(files.read_asset(file));
    const auto found=std::find_if(archive.entries.begin(),archive.entries.end(),[&](const auto& record){return record.id==id;});
    if(found==archive.entries.end()) {
        if(required)throw DecodeError("Required NPC character record is missing");
        return {};
    }
    return found->data;
}

}
NpcResult NpcService::append(Party& party,const IFileSystem& files,unsigned disk,std::uint8_t template_id,std::mt19937& random) {
    if(party.members_.size()>8)return {NpcStatus::rejected,"NPC roster exceeds the native limit"};
    // The original capacity test happens before disk/template access and RNG.
    if(party.members_.size()==8)return {NpcStatus::applied,{},false};
    if(disk<1 || disk>8)return {NpcStatus::rejected,"NPC resource disk is outside the supported range"};
    if(!party.item_catalog_)return {NpcStatus::unresolved,"NPC item catalog requires a restored party"};
    auto staged=party;
    auto staged_random=random;
    try {
        const auto stem="MON"+std::to_string(disk);
        const auto cha=stem+"CHA.DAX",itm=stem+"ITM.DAX",spc=stem+"SPC.DAX";
        const auto assets=files.list_assets();
        const auto raw=archive_record(files,assets,cha,template_id,true);
        auto member=Character::decode(raw,pascal_string_id(record_source(cha,template_id),0));
        // MONCHA C7 is stale template scratch (unlike matched saved ITM).
        // The ECL36 tail rebuilds it only after operand2 has been evaluated.
        member.inventory_=Inventory::decode(archive_record(files,assets,itm,template_id,false));
        member.load_effects(archive_record(files,assets,spc,template_id,false),record_source(spc,template_id));
        if(member.inventory_.size()>std::numeric_limits<ItemId>::max()-staged.next_item_id_ || staged.next_item_id_==0)
            return {NpcStatus::rejected,"NPC owned item identity allocation is exhausted"};
        for(std::size_t i=0;i<member.inventory_.size();++i) {
            auto& item=member.inventory_.items_[i];(void)party.item_catalog_->definition(item.type());
            item.id_=staged.next_item_id_++;item.origin_={itm,template_id,63*i};
        }
        const auto race=member.byte(0x2e);
        if(race==1 || race==3 || race==5)member.set_byte(0xc0,1);
        else if(race==2 || race==4 || race==6 || race==7)member.set_byte(0xc0,2);
        for(const auto at:{0xc1u,0xc2u,0xc3u,0xc5u,0xc6u})member.set_byte(at,static_cast<std::uint8_t>(npc_roll(staged_random,6)*17+128));
        member.set_byte(0xc4,0xc4);
        member.set_byte(0xab,static_cast<std::uint8_t>(npc_roll(staged_random,256)-1));
        std::vector<unsigned> classes;
        for(unsigned i=0;i<7;++i)if(signed_byte(member.byte(0x96u+i))>0)classes.push_back(i);
        if(classes.empty())classes.push_back(2);
        const auto selected_class=classes[npc_roll(staged_random,static_cast<unsigned>(classes.size()))-1];
        const auto module=decode_start_data(files.read_asset("START.EXE"));
        const auto read=[&](std::size_t offset){if(offset>=module.size())throw DecodeError("NPC portrait source table is truncated");return module[offset];};
        const auto sex=signed_byte(member.byte(0x9e));
        if(sex<0 || sex>1)return {NpcStatus::unresolved,"NPC portrait sex selector is outside the verified source profiles"};
        const auto body_base=selected_class==0?0x8d7u:selected_class==5?0x8ebu:selected_class==6?0x8f5u:0x8e1u;
        member.set_byte(0xbc,read(0xc7c0u+body_base+static_cast<unsigned>(sex)*5+npc_roll(staged_random,5)));
        member.set_byte(0xbb,read(0xc7c0u+0x8c7+static_cast<unsigned>(sex)*8+npc_roll(staged_random,8)));
        std::array<bool,8> used{};
        for(const auto& existing:staged.members_)if(existing.byte(0xbf)<8)used[existing.byte(0xbf)]=true;
        unsigned ordinal=0;while(ordinal<used.size() && used[ordinal])++ordinal;
        if(ordinal==8)return {NpcStatus::rejected,"NPC presentation ordinal is unavailable"};
        member.set_byte(0xbf,static_cast<std::uint8_t>(ordinal));
        CharacterRecoveryService::rebuild_class_cache(member,module);
        staged.members_.push_back(std::move(member));staged.selected_=staged.members_.size()-1;++staged.revision_;
    } catch(const std::exception& error) {return {NpcStatus::rejected,error.what()};}
    party=std::move(staged);random=std::move(staged_random);
    return {NpcStatus::applied,{},true};
}
NpcResult NpcService::finish(Party& party,std::uint8_t template_id,std::uint8_t parameter) {
    if(!party.selected_ || *party.selected_>=party.members_.size())return {NpcStatus::unresolved,"NPC finalization requires a selected member"};
    auto staged=party;
    auto& member=staged.members_[*staged.selected_];
    member.set_byte(0x84,static_cast<std::uint8_t>((parameter>>1)|0x80));
    member.set_byte(0x10e,static_cast<std::uint8_t>(template_id==24));
    try {
        if(!member.equipment_effects_bound())(void)EquipmentEffectService::bind_restored(staged);
        EquipmentRules::recalculate(member,staged.item_catalog());
    }
    catch(const DecodeError& error) {return {NpcStatus::unresolved,error.what()};}
    ++staged.revision_;party=std::move(staged);
    return {NpcStatus::applied,{}};
}
}
