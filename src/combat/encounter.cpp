#include "gbengine/combat/encounter.hpp"
#include "gbengine/assets/records.hpp"
#include "gbengine/items/item_catalog.hpp"
#include "gbengine/magic/equipment_effect_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <limits>

namespace GBEngine {
namespace {
unsigned color_roll(std::mt19937& random) {
    constexpr std::uint32_t sides=6;
    constexpr auto threshold=static_cast<std::uint32_t>(0u-sides)%sides;
    std::uint32_t value;
    do {value=static_cast<std::uint32_t>(random());}while(value<threshold);
    return value%sides+1;
}
std::string record_source(const std::string& file,std::uint8_t id) {
    constexpr char hex[]="0123456789ABCDEF";
    return file+":0"+hex[id>>4]+hex[id&15];
}
Bytes source_record(const IFileSystem& files,const std::vector<std::string>& assets,
                    const std::string& file,std::uint8_t id,bool required) {
    if(std::find(assets.begin(),assets.end(),file)==assets.end()) {
        if(required)throw DecodeError("Required encounter character archive is missing");
        return {};
    }
    const DaxArchive archive(files.read_asset(file));
    const auto found=std::find_if(archive.entries.begin(),archive.entries.end(),
                                 [id](const auto& entry){return entry.id==id;});
    if(found==archive.entries.end()) {
        if(required)throw DecodeError("Required encounter character record is missing");
        return {};
    }
    return found->data;
}
}
void EncounterService::add_group(EncounterState& encounter,Party& party,const IFileSystem& files,
                                unsigned disk,std::uint8_t template_id,std::mt19937& random,
                                const std::function<std::array<std::uint8_t,2>()>& late_parameters) {
    // 2182:0470 tests capacity before even evaluating operand one.
    if(encounter.count==63)return;
    if(encounter.count>63)throw DecodeError("Encounter creature count exceeds the native limit");
    if(!encounter.initialized)throw DecodeError("Encounter preparation requires the native setup reset");
    if(encounter.duel_context && (!encounter.special ||
       encounter.special->selected_party_index>=party.size() || encounter.special->tactical_party.size()!=party.size()))
        throw DecodeError("Special encounter preparation requires its owned persistent participant binding");
    if(disk<1 || disk>8)throw DecodeError("Encounter resource disk is outside the supported range");
    if(!party.item_catalog_)throw DecodeError("Encounter equipment requires a restored party item catalog");
    if(!late_parameters)throw DecodeError("Encounter late operand evaluator is missing");
    auto staged=encounter;
    auto staged_party=party;
    auto staged_random=random;
    const auto stem="MON"+std::to_string(disk);
    const auto cha=stem+"CHA.DAX",spc=stem+"SPC.DAX",itm=stem+"ITM.DAX";
    const auto assets=files.list_assets();
    auto source=Character::decode(source_record(files,assets,cha,template_id,true),
                                  pascal_string_id(record_source(cha,template_id),0));
    // 3D84:0EC4: these are the only constructor RNG draws. NPC recruitment's
    // additional portraits, class caches and AB draw do not belong here.
    const auto race=source.byte(0x2e);
    if(race==1 || race==3 || race==5)source.set_byte(0xc0,1);
    else if(race==2 || race==4 || race==6 || race==7)source.set_byte(0xc0,2);
    for(const auto at:{0xc1u,0xc2u,0xc3u,0xc5u,0xc6u})
        source.set_byte(at,static_cast<std::uint8_t>(color_roll(staged_random)*17+128));
    source.set_byte(0xc4,0xc4);
    source.load_effects(source_record(files,assets,spc,template_id,false),record_source(spc,template_id));
    // MONCHA C7 is template scratch. Tactical inventory refresh is a later
    // source operation; saved-character count validation does not apply here.
    source.inventory_=Inventory::decode(source_record(files,assets,itm,template_id,false));
    const auto parameters=late_parameters();
    const auto requested=parameters[0]==0?1u:static_cast<unsigned>(parameters[0]);
    const auto quantity=std::min(requested,63u-staged.count);
    EncounterGroup group;
    group.template_id=template_id;group.quantity=static_cast<std::uint8_t>(quantity);
    group.art_selector=parameters[1];group.native_group=staged.next_group;
    // 2182:0532 calls 506E:01C8 before linking the first creature. Its CPIC
    // branch loads the selected record and selector+80, using the source disk.
    // 506E:02D3 zero-extends the selector then adds80 in AX.1464:0157
    // and1802:0855 consume its low byte, so high-bit selectors swap poses.
    group.art_source_file="CPIC"+std::to_string(disk)+".DAX";
    for(unsigned phase=0;phase<2;++phase) {
        const auto id=static_cast<std::uint8_t>(group.art_selector+phase*128);
        group.art_frames[phase]=decode_ega(load_record(files,group.art_source_file,id),group.art_source_file);
        if(group.art_frames[phase].empty())throw DecodeError("Encounter icon record contains no image frames");
    }
    for(unsigned ordinal=0;ordinal<quantity;++ordinal) {
        if(staged.next_combatant_id==0 || staged.next_combatant_id==std::numeric_limits<std::uint64_t>::max())
            throw DecodeError("Encounter combatant identity allocation is exhausted");
        auto creature=source;
        creature.set_byte(0xbf,staged.next_group);
        // Native clones prepend copies of each source graph node, reversing
        // list order. The first creature retains the source loader's order.
        if(ordinal!=0) {
            std::reverse(creature.inventory_.items_.begin(),creature.inventory_.items_.end());
            std::reverse(creature.effects_.begin(),creature.effects_.end());
        }
        for(std::size_t i=0;i<creature.inventory_.size();++i) {
            auto& item=creature.inventory_.items_[i];
            (void)party.item_catalog_->definition(item.type());
            if(staged_party.next_item_id_==0 || staged_party.next_item_id_==std::numeric_limits<ItemId>::max())
                throw DecodeError("Encounter item identity allocation is exhausted");
            const auto source_ordinal=ordinal==0?i:creature.inventory_.size()-i-1;
            item.id_=staged_party.next_item_id_++;
            item.origin_={itm,template_id,63*source_ordinal};
        }
        // Loaded graphs/readiness are native data. Bind only after assigning
        // final clone-local identities; never replay Ready or recreate nodes.
        (void)EquipmentEffectService::bind_restored(creature);
        group.creatures.push_back({CombatantId{staged.next_combatant_id++},std::move(creature)});
    }
    // CALL8000 leaves6F83 unchanged, so its surrogate BF aliases this slot.
    // Native1464:013D replaces the old slot pointers. Publish matching owned
    // bindings only after every source graph, art and allocation check passes.
    for(auto& previous:staged.groups)if(previous.native_group==group.native_group) {
        previous.art_selector=group.art_selector;previous.art_source_file=group.art_source_file;
        previous.art_frames=group.art_frames;
    }
    staged.count=static_cast<std::uint8_t>(staged.count+quantity);
    staged.next_group=static_cast<std::uint8_t>(staged.next_group+1);
    staged.setup_prepared=true;staged.groups.push_back(std::move(group));
    ++staged_party.revision_;
    encounter=std::move(staged);party=std::move(staged_party);random=std::move(staged_random);
}
}
