#include "gbengine/magic/camp_work_service.hpp"
#include "gbengine/party/equipment.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
CampWorkResult rejected(std::string message) {return {CampWorkStatus::rejected,std::move(message)};}
CampWorkResult unresolved(std::string message) {return {CampWorkStatus::unresolved,std::move(message)};}
CampWorkResult eligibility(const Party& party,std::size_t member) {
    if(member>=party.size()) return rejected("Camp work requires a real party member");
    const auto& character=party.member(member);
    if(!character.active() || character.status()==1) return rejected("Character is in no condition for camp spell work");
    return {};
}
void validate_book(const Character& character,const SpellCatalog& catalog) {
    for(const auto& entry:Spellbook::memorized(character)) (void)catalog.definition(entry.spell);
}
std::uint8_t capacity(const Character& character,const SpellDefinition& definition) {
    // Only the six verified saved capacity bytes are currently class-cache fields.
    return Spellbook::capacity(character,definition.school(),definition.level());
}
bool scroll(const Party& party,const Item& item) {return party.item_catalog().definition(item.type()).scroll();}
std::optional<std::size_t> pending_memorize(const Character& character) {
    for(std::size_t slot=0;slot<21;++slot) if(character.byte(0x17+slot)>127) return slot;
    return std::nullopt;
}
struct PendingScribe {std::size_t item,slot;SpellId spell;};
std::optional<PendingScribe> pending_scribe(const Party& party,const Character& character,std::size_t first_item=0,std::size_t first_slot=0) {
    for(std::size_t item=first_item;item<character.inventory().size();++item) {
        const auto& owned=character.inventory().item(item);
        if(!scroll(party,owned)) continue;
        for(std::size_t slot=item==first_item?first_slot:0;slot<3;++slot) if(owned.byte(0x3c+slot)>128)
            return PendingScribe{item,slot,SpellId(static_cast<std::uint8_t>(owned.byte(0x3c+slot)&127))};
    }
    return std::nullopt;
}
std::uint8_t next_level(const Party& party,const Character& character,const SpellCatalog& catalog) {
    if(const auto job=pending_scribe(party,character)) return catalog.definition(job->spell).level();
    if(const auto slot=pending_memorize(character))
        return catalog.definition(SpellId(static_cast<std::uint8_t>(character.byte(0x17+*slot)&127))).level();
    return 0;
}
void validate_work(const Party& party,const Character& character,const SpellCatalog& catalog) {
    validate_book(character,catalog);
    for(std::size_t item=0;item<character.inventory().size();++item) {
        const auto& owned=character.inventory().item(item);
        if(!scroll(party,owned)) continue;
        for(const auto raw:owned.effect_bytes()) if(raw) (void)catalog.definition(SpellId(static_cast<std::uint8_t>(raw&127)));
    }
}
}

CampWorkChoices CampWorkService::memorize_choices(const Party& party,std::size_t member,const SpellCatalog& catalog) {
    CampWorkChoices result;static_cast<CampWorkResult&>(result)=eligibility(party,member);
    if(result.status!=CampWorkStatus::applied) return result;
    try {
        validate_book(party.member(member),catalog);
        for(std::uint8_t id=1;id<=55;++id) if(Spellbook::known(party.member(member),SpellId(id))) {
            (void)catalog.definition(SpellId(id));result.spells.push_back(SpellId(id));
        }
    } catch(const DecodeError& error) {static_cast<CampWorkResult&>(result)=unresolved(error.what());result.spells.clear();}
    return result;
}
CampScribeChoices CampWorkService::scribe_choices(const Party& party,std::size_t member,const SpellCatalog& catalog) {
    CampScribeChoices result;static_cast<CampWorkResult&>(result)=eligibility(party,member);
    if(result.status!=CampWorkStatus::applied) return result;
    try {
        const auto& character=party.member(member);validate_work(party,character,catalog);
        for(std::size_t item=0;item<character.inventory().size();++item) {
            const auto& owned=character.inventory().item(item);
            if(!scroll(party,owned)) continue;
            for(std::size_t slot=0;slot<3;++slot) if(const auto raw=owned.byte(0x3c+slot))
                result.spells.push_back({owned.id(),slot,SpellId(static_cast<std::uint8_t>(raw&127)),raw>127});
        }
    } catch(const DecodeError& error) {static_cast<CampWorkResult&>(result)=unresolved(error.what());result.spells.clear();}
    return result;
}
CampWorkResult CampWorkService::schedule_memorize(Party& party,std::size_t member,const SpellCatalog& catalog,SpellId spell) {
    if(const auto gate=eligibility(party,member);gate.status!=CampWorkStatus::applied) return gate;
    try {
        const auto& character=party.member(member);validate_book(character,catalog);
        const auto& definition=catalog.definition(spell);
        if(spell.value>55 || !Spellbook::known(character,spell)) return rejected("Spell is not available in the native memorization list");
        const auto available=capacity(character,definition);
        std::size_t occupied=0;std::optional<std::size_t> empty;
        for(std::size_t slot=0;slot<21;++slot) {
            const auto raw=character.byte(0x17+slot);
            if(!raw) {if(!empty) empty=slot;continue;}
            const auto& other=catalog.definition(SpellId(static_cast<std::uint8_t>(raw&127)));
            if(other.school()==definition.school() && other.level()==definition.level()) ++occupied;
        }
        if(occupied>available) return unresolved("Saved spell capacity is inconsistent with occupied slots");
        if(!empty || occupied==available) return rejected("No remaining memorization capacity");
        auto staged=party;auto& edited=staged.members_[member];edited.set_byte(0x17+*empty,static_cast<std::uint8_t>(spell.value|128));
        // Exact3740:0824 nested swaps; equal masked IDs do not trigger a swap.
        for(std::size_t left=0;left<20;++left) for(std::size_t right=left;right<21;++right)
            if((edited.byte(0x17+right)&127)<(edited.byte(0x17+left)&127)) {
                const auto value=edited.byte(0x17+left);edited.set_byte(0x17+left,edited.byte(0x17+right));edited.set_byte(0x17+right,value);
            }
        ++staged.revision_;party=std::move(staged);return {};
    } catch(const DecodeError& error) {return unresolved(error.what());}
}
CampWorkResult CampWorkService::clear_memorize(Party& party,std::size_t member) {
    if(member>=party.size()) return rejected("Camp work requires a real party member");
    auto staged=party;auto& character=staged.members_[member];bool changed=character.byte(0x2c)!=0;
    for(std::size_t slot=0;slot<21;++slot) if(character.byte(0x17+slot)>127) {character.set_byte(0x17+slot,0);changed=true;}
    character.set_byte(0x2c,0);if(changed) {++staged.revision_;party=std::move(staged);}return {};
}
CampWorkResult CampWorkService::schedule_scribe(Party& party,std::size_t member,const SpellCatalog& catalog,ItemId id,std::size_t slot) {
    if(const auto gate=eligibility(party,member);gate.status!=CampWorkStatus::applied) return gate;
    if(slot>=3) return rejected("Invalid scroll spell slot");
    try {
        const auto& character=party.member(member);validate_work(party,character,catalog);
        const auto& items=character.inventory_.items_;
        const auto found=std::find_if(items.begin(),items.end(),[&](const auto& item){return item.id()==id;});
        if(found==items.end() || !scroll(party,*found)) return rejected("Scribing requires an owned scroll");
        const auto raw=found->byte(0x3c+slot);
        if(!raw || raw>127) return rejected("Scroll spell is empty or already pending");
        const SpellId spell(raw);const auto& definition=catalog.definition(spell);
        if(Spellbook::known(character,spell)) return rejected("Character already knows this spell");
        for(const auto& item:items) if(scroll(party,item)) for(const auto other:item.effect_bytes())
            if(other>127 && (other&127)==spell.value) return rejected("Spell is already scheduled for scribing");
        if(!capacity(character,definition)) return rejected("Character has no saved capacity for this spell level");
        const auto item_index=static_cast<std::size_t>(found-items.begin());auto staged=party;
        staged.members_[member].inventory_.items_[item_index].bytes_[0x3c+slot]=static_cast<std::uint8_t>(raw|128);
        ++staged.revision_;party=std::move(staged);return {};
    } catch(const DecodeError& error) {return unresolved(error.what());}
}
CampWorkResult CampWorkService::clear_scribe(Party& party,std::size_t member) {
    if(member>=party.size()) return rejected("Camp work requires a real party member");
    try {
        auto staged=party;bool changed=false;
        for(auto& item:staged.members_[member].inventory_.items_) if(scroll(staged,item))
            for(std::size_t slot=0;slot<3;++slot) if(item.byte(0x3c+slot)>127) {item.bytes_[0x3c+slot]&=127;changed=true;}
        if(changed) {++staged.revision_;party=std::move(staged);}return {};
    } catch(const DecodeError& error) {return unresolved(error.what());}
}
CampWorkResult CampWorkService::cleanup_camp(Party& party) {
    try {
        auto staged=party;
        for(std::size_t member=0;member<staged.size();++member) {
            if(const auto result=clear_memorize(staged,member);result.status!=CampWorkStatus::applied) return result;
            if(const auto result=clear_scribe(staged,member);result.status!=CampWorkStatus::applied) return result;
        }
        party=std::move(staged);return {};
    } catch(const DecodeError& error) {return unresolved(error.what());}
}
CampWorkPreparation CampWorkService::prepare_rest(Party& party,const SpellCatalog& catalog) {
    CampWorkPreparation result;
    if(party.size()>8) {static_cast<CampWorkResult&>(result)=unresolved("Native camp work supports at most eight members");return result;}
    try {
        auto staged=party;bool changed=false;
        for(auto& character:staged.members_) {
            validate_work(staged,character,catalog);
            std::uint8_t sum_mem=0,sum_scribe=0,max_mem=0,max_scribe=0;
            for(const auto& entry:Spellbook::memorized(character)) if(entry.pending) {
                const auto level=catalog.definition(entry.spell).level();sum_mem=static_cast<std::uint8_t>(sum_mem+level);max_mem=std::max(max_mem,level);
            }
            for(const auto& item:character.inventory_.items_) if(scroll(staged,item)) for(const auto raw:item.effect_bytes()) if(raw>127) {
                const auto level=catalog.definition(SpellId(static_cast<std::uint8_t>(raw&127))).level();
                sum_scribe=static_cast<std::uint8_t>(sum_scribe+level);max_scribe=std::max(max_scribe,level);
            }
            const std::uint8_t prep=max_mem>2 || max_scribe>2?6:sum_mem || sum_scribe?4:0;
            changed=changed || character.byte(0x2c)!=prep;character.set_byte(0x2c,prep);
            const auto minutes=static_cast<std::uint16_t>(60u*prep+15u*sum_mem+15u*sum_scribe);
            result.minutes=std::max(result.minutes,minutes);
        }
        if(changed) ++staged.revision_;
        party=std::move(staged);
    } catch(const DecodeError& error) {static_cast<CampWorkResult&>(result)=unresolved(error.what());result.minutes=0;}
    return result;
}
CampWorkTick CampWorkService::tick_work(Party& party,const SpellCatalog& catalog,CampWorkState& state) {
    CampWorkTick result;
    if(party.size()>8) {static_cast<CampWorkResult&>(result)=unresolved("Native camp work supports at most eight members");return result;}
    try {
        auto staged=party;auto next=state;bool changed=false;
        for(std::size_t member=0;member<staged.size();++member) {
            auto& character=staged.members_[member];validate_work(staged,character,catalog);
            next.counters[member]=static_cast<std::uint8_t>(next.counters[member]-1);
            if(next.counters[member] || character.byte(0x2c)) continue;
            std::optional<std::uint8_t> continuation;
            if(const auto job=pending_scribe(staged,character)) {
                auto& item=character.inventory_.items_[job->item];std::size_t last=job->slot;
                for(std::size_t slot=0;slot<3;++slot) if((item.byte(0x3c+slot)&127)==job->spell.value) last=slot;
                character.set_byte(0x32+job->spell.value,1);item.bytes_[0x3c+last]=0;
                item.bytes_[0x30]=static_cast<std::uint8_t>(item.byte(0x30)-1);
                const bool removed=item.byte(0x30)<210;
                if(removed) {
                    if(character.bound_equipment_effects_.contains(item.id()) || std::any_of(character.effects_.begin(),character.effects_.end(),[&](const auto& effect){return effect.equipment_item_id==item.id();}))
                        throw DecodeError("Scribing cannot remove a scroll with an active equipment effect binding");
                    character.inventory_.items_.erase(character.inventory_.items_.begin()+static_cast<std::ptrdiff_t>(job->item));
                    EquipmentRules::recalculate(character,staged.item_catalog());
                }
                //09E7 resumes after the completed slot. Last-match consumption
                // can leave that slot pending; do not reselect it in this call.
                if(const auto following=pending_scribe(staged,character,job->item,removed?0:job->slot+1))
                    continuation=catalog.definition(following->spell).level();
                else if(const auto slot=pending_memorize(character))
                    continuation=catalog.definition(SpellId(static_cast<std::uint8_t>(character.byte(0x17+*slot)&127))).level();
                else continuation=std::uint8_t{0};
                result.notices.push_back({member,job->spell,true});changed=true;
            } else if(const auto slot=pending_memorize(character)) {
                const SpellId spell(static_cast<std::uint8_t>(character.byte(0x17+*slot)&127));character.set_byte(0x17+*slot,spell.value);
                result.notices.push_back({member,spell,false});changed=true;
            }
            next.counters[member]=static_cast<std::uint8_t>(3u*(continuation?*continuation:next_level(staged,character,catalog)));
        }
        next.hourly_ticks=static_cast<std::uint8_t>(next.hourly_ticks+1);
        if(next.hourly_ticks>11) {
            next.hourly_ticks=0;
            for(std::size_t member=0;member<staged.size();++member) {
                auto& character=staged.members_[member];const auto prep=character.byte(0x2c);
                if(prep>0 && prep<128) {
                    character.set_byte(0x2c,static_cast<std::uint8_t>(prep-1));changed=true;
                    if(prep==1) next.counters[member]=static_cast<std::uint8_t>(3u*next_level(staged,character,catalog));
                }
            }
        }
        if(changed) ++staged.revision_;
        party=std::move(staged);state=next;
    } catch(const DecodeError& error) {static_cast<CampWorkResult&>(result)=unresolved(error.what());result.notices.clear();}
    return result;
}
}
