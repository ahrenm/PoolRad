#include "gbengine/combat/special_combat_service.hpp"
#include "gbengine/assets/records.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <limits>

namespace GBEngine {
namespace {
std::array<std::uint8_t,16> native_name(const Character& actor) {
    std::array<std::uint8_t,16> key{};
    if(actor.byte(0)>15)throw DecodeError("Special combat character name exceeds native capacity");
    for(std::size_t i=0;i<=actor.byte(0);++i)key[i]=actor.byte(i);
    return key;
}
}
SpecialCombatPreparation SpecialCombatService::stage(const IFileSystem& files,unsigned resource_disk,
        const Party& party,const EncounterState& encounter,std::size_t selected_party_index,bool surrogate) {
    if(!party.size() || party.size()>8 || selected_party_index>=party.size())
        throw DecodeError("Special combat requires a selected persistent party member");
    if(encounter.count>63 || encounter.duel_context || encounter.special)
        throw DecodeError("Special combat requires one unambiguous source setup context");
    if(surrogate && (resource_disk<1 || resource_disk>8))
        throw DecodeError("Special combat source resource disk is outside its native range");
    if(!party.item_catalog_)throw DecodeError("Special combat requires restored item ownership");
    SpecialCombatPreparation result{encounter,party,static_cast<std::uint16_t>(surrogate?1:0)};
    SpecialCombatContext special;special.selected_party_index=selected_party_index;special.surrogate=surrogate;
    special.selected_name=native_name(party.member(selected_party_index));
    special.tactical_party.reserve(party.size());special.original_active.reserve(party.size());
    // 27A9:1AA9 calls the resident Pascal-string comparator. Equal names,
    // including duplicate names, retain their original active byte unchanged.
    for(std::size_t i=0;i<party.size();++i) {
        auto actor=party.member(i);special.original_active.push_back(actor.byte(0x10d));
        if(native_name(actor)!=special.selected_name)actor.set_byte(0x10d,0);
        special.tactical_party.push_back(std::move(actor));
    }
    for(auto& group:result.encounter.groups)for(auto& creature:group.creatures)
        if(native_name(creature.character)!=special.selected_name)creature.character.set_byte(0x10d,0);
    if(surrogate) {
        std::size_t count=party.size()+1;
        for(const auto& group:encounter.groups)count+=group.creatures.size();
        if(count>71)throw DecodeError("Special combat exceeds the owned tactical participant capacity");
        if(!result.encounter.next_combatant_id || result.encounter.next_combatant_id==std::numeric_limits<std::uint64_t>::max())
            throw DecodeError("Special combat creature identity allocation is exhausted");
        EncounterGroup group;group.quantity=1;group.art_selector=11;group.native_group=encounter.next_group;group.surrogate=true;
        group.art_source_file="CPIC"+std::to_string(resource_disk)+".DAX";
        for(unsigned phase=0;phase<2;++phase) {
            group.art_frames[phase]=decode_ega(load_record(files,group.art_source_file,static_cast<std::uint8_t>(11+128*phase)),group.art_source_file);
            if(group.art_frames[phase].empty())throw DecodeError("Special combat icon record contains no frames");
        }
        auto clone=special.tactical_party[selected_party_index];
        // Source uses a full285-byte copy, then clears its SPC and inventory
        // graph heads. Cached stats/spells/colors survive; effect nodes do not.
        clone.effects_.clear();clone.effects_restored_=true;clone.bound_equipment_effects_.clear();
        clone.set_word(0x7f,0);clone.set_word(0x81,0);clone.set_word(0xc8,0);clone.set_word(0xca,0);
        clone.set_word(0x104,0);clone.set_word(0x106,0);
        clone.set_byte(0x10d,1);clone.set_byte(0x10f,1);clone.set_byte(0x10e,1);
        clone.set_byte(0x84,0xb2);clone.set_byte(0xbf,encounter.next_group);
        const auto overlay=files.read_asset("GAME.OVR");constexpr std::size_t name_at=0x8f3d;
        if(overlay.size()<=name_at || overlay[name_at]!=4 || overlay.size()<name_at+5)
            throw DecodeError("Special combat source name does not match the recovered layout");
        for(std::size_t i=0;i<=overlay[name_at];++i)clone.set_byte(i,overlay[name_at+i]);
        clone.name_id_=pascal_string_id("GAME.OVR",name_at);
        // Source appends each fresh inventory node at the head, reversing the
        // selected actor's list. The original owned graph remains untouched.
        std::reverse(clone.inventory_.items_.begin(),clone.inventory_.items_.end());
        for(auto& item:clone.inventory_.items_) {
            if(!result.party.next_item_id_ || result.party.next_item_id_==std::numeric_limits<ItemId>::max())
                throw DecodeError("Special combat item identity allocation is exhausted");
            item.id_=result.party.next_item_id_++;
        }
        group.creatures.push_back({CombatantId{result.encounter.next_combatant_id++},std::move(clone)});
        result.encounter.groups.push_back(std::move(group));
        if(party.next_item_id_!=result.party.next_item_id_)++result.party.revision_;
        // Native CALL8000 does not increment6F82/6F83 or set84E7. Its separate
        // resident84D4 flag selects ECL24 combat even without ECL0B creatures.
    }
    result.encounter.duel_context=true;result.encounter.special=std::move(special);
    return result;
}
}
