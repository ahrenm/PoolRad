#include "gbengine/party/inventory_service.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
ItemActionResult outcome(ItemActionStatus status,std::string message={},ItemId id=0) {
    return {status,StringStore::active().runtime_text(message),id,{}};
}
ItemActionResult reject(std::string message) {return outcome(ItemActionStatus::rejected,std::move(message));}
std::string native(std::size_t offset) {return overlay_string(0x2103a+offset);}
bool special_transfer(const Item& item,const ItemCatalog& catalog) {
    return catalog.definition(item.type()).scroll() && (item.byte(0x3c)>=128 || item.byte(0x3d)>=128 || item.byte(0x3e)>=128);
}
std::uint32_t gold_value(const std::array<std::uint32_t,5>& coins,const ItemCatalog& catalog) {
    std::uint64_t copper=0;for(std::size_t i=0;i<5;++i) copper+=static_cast<std::uint64_t>(coins[i])*catalog.coin_rates()[i];
    if(copper>0xffffffffu-100) throw DecodeError("Wealth exceeds native long-word range");
    return static_cast<std::uint32_t>((copper+100)/200);
}
void refresh(Character& character,const ItemCatalog& catalog) {EquipmentRules::recalculate(character,catalog);}
}
std::optional<std::size_t> InventoryService::locate(const Character& c,ItemId id) {
    if(!id) return {};
    for(std::size_t i=0;i<c.inventory().size();++i) if(c.inventory().item(i).id()==id) return i;
    return {};
}
ItemActionResult InventoryService::mutate(const std::function<ItemActionResult(std::vector<Character>&,ItemId&)>& action) {
    auto staged=party_.members_;
    auto next=party_.next_item_id_;
    const auto revision=party_.revision_;
    try {
        auto result=action(staged,next);
        if(result.applied()) {
            if(party_.revision_!=revision) return reject("Inventory changed while an effect was resolving");
            party_.members_=std::move(staged);party_.next_item_id_=next;++party_.revision_;
        }
        return result;
    } catch(const DecodeError& error) {return outcome(ItemActionStatus::unresolved,error.what());}
}
ItemActionResult InventoryService::ready(std::size_t member,ItemId id,IItemEffectResolver* effects) {
    if(member>=party_.size()) return reject("No such party member");
    return mutate([&](auto& members,ItemId&) {
        auto& c=members[member];
        const auto at=locate(c,id);
        if(!at) return reject("Item is no longer owned by this character");
        auto& item=c.inventory_.items_[*at];
        const auto& catalog=party_.item_catalog();
        const auto& def=catalog.definition(item.type());
        const auto equipped=EquipmentRules::inspect(c,catalog);
        const bool remove=item.readied();
        if(remove && item.cursed()) return reject(native(0x1494));
        if(!remove) {
            // Native error priority: class, occupied slot, then hand budget.
            if(!(def.class_mask()&c.byte(0xb0))) return reject(native(0x14a0));
            std::optional<unsigned> slot;
            if(def.category()<9) slot=def.category();
            if(def.category()==9) slot=10;
            if(item.type()==0x49) slot=11;
            if(item.type()==0x1c) slot=12;
            if(slot && equipped.slots[*slot]) return reject(native(0x14ac)+c.inventory().item(*equipped.slots[*slot]).name());
            if(static_cast<unsigned>(equipped.hands)+def.hands()>2) return reject(native(0x14bb));
        }
        item.bytes_[0x34]=static_cast<std::uint8_t>(!remove);
        if(item.byte(0x3e)>=128) {
            ItemEffectRequest request{remove?ItemEffectTrigger::unequip:ItemEffectTrigger::equip,member,member,id,party_.revision(),item.byte(0x3e),0,item.effect_bytes(),{},item.origin()};
            if(!effects) {auto result=outcome(ItemActionStatus::unresolved,"Equipped item effect is not implemented",id);result.effect=request;
            return result;}
            const auto result=effects->resolve(request,members);
            if(!result.applied()) return result;
            //84/87 can apply their native rejection while undoing readiness.
            //Bind the surviving item's actual state, not the attempted toggle.
            //Resolve again by identity: a removal callback can erase inventory.
            const auto surviving=locate(c,id);
            if(surviving && c.inventory_.item(*surviving).readied() && c.inventory_.item(*surviving).byte(0x3e)>=128)
                c.bound_equipment_effects_.insert(id);
            else c.bound_equipment_effects_.erase(id);
        }
        refresh(c,catalog);
        return outcome(ItemActionStatus::applied,{},id);
    });
}
ItemActionResult InventoryService::trade(std::size_t member,ItemId id,std::size_t recipient,bool confirmed) {
    if(member>=party_.size() || recipient>=party_.size() || member==recipient) return reject("Choose a different party member");
    return mutate([&](auto& members,ItemId&) {
        auto& from=members[member];auto& to=members[recipient];
        const auto at=locate(from,id);
        if(!at) return reject("Item is no longer owned by this character");
        const auto& item=from.inventory_.items_[*at];
        const auto& catalog=party_.item_catalog();
        if(item.readied()) return reject(native(0xd20));
        if(special_transfer(item,catalog) && !confirmed) return outcome(ItemActionStatus::confirmation_required,native(0xd32),id);
        if(to.inventory().size()>=16) return reject(native(0x1729));
        refresh(to,catalog);
        const auto weight=static_cast<std::uint16_t>(to.word(0x102)+item.unit_weight()*std::max(1,static_cast<int>(item.quantity())));
        const int capacity=EquipmentRules::load_allowance(to)+1500;
        if(capacity<0 || weight>capacity) return reject(native(0x1729));
        to.inventory_.items_.push_back(item);from.inventory_.items_.erase(from.inventory_.items_.begin()+static_cast<std::ptrdiff_t>(*at));
        refresh(from,catalog);
        refresh(to,catalog);
        return outcome(ItemActionStatus::applied,{},id);
    });
}
ItemActionResult InventoryService::drop(std::size_t member,ItemId id,bool confirmed) {
    if(member>=party_.size()) return reject("No such party member");
    return mutate([&](auto& members,ItemId&) {
        auto& c=members[member];
        const auto at=locate(c,id);
        if(!at) return reject("Item is no longer owned by this character");
        const auto& item=c.inventory_.items_[*at];
        if(item.readied()) return reject(native(0xd20));
        if(!confirmed) return outcome(ItemActionStatus::confirmation_required,
            (special_transfer(item,party_.item_catalog())?native(0xd32):std::string{})+native(0xed3)+item.name()+" "+native(0xed9),id);
        c.inventory_.items_.erase(c.inventory_.items_.begin()+static_cast<std::ptrdiff_t>(*at));
        refresh(c,party_.item_catalog());
        return outcome(ItemActionStatus::applied,{},id);
    });
}
ItemActionResult InventoryService::halve(std::size_t member,ItemId id) {
    if(member>=party_.size()) return reject("No such party member");
    return mutate([&](auto& members,ItemId& next) {
        auto& c=members[member];
        const auto at=locate(c,id);
        if(!at) return reject("Item is no longer owned by this character");
        auto& items=c.inventory_.items_;
        if(items.size()>=16) return reject("Inventory has sixteen items");
        if(items[*at].quantity()<2) return reject(native(0x17dc));
        auto half=items[*at];half.bytes_[0x39]=static_cast<std::uint8_t>(half.quantity()/2);half.bytes_[0x34]=0;half.id_=next++;
        items[*at].bytes_[0x39]=static_cast<std::uint8_t>(items[*at].quantity()-half.quantity());
        items.insert(items.begin()+static_cast<std::ptrdiff_t>(*at+1),std::move(half));
        refresh(c,party_.item_catalog());
        return outcome(ItemActionStatus::applied,{},id);
    });
}
ItemActionResult InventoryService::join(std::size_t member,ItemId id) {
    if(member>=party_.size()) return reject("No such party member");
    return mutate([&](auto& members,ItemId&) {
        auto& c=members[member];
        auto at=locate(c,id);
        if(!at) return reject("Item is no longer owned by this character");
        auto& items=c.inventory_.items_;
        if(!items[*at].quantity() || items[*at].charges()>=2) return reject("Item cannot be joined");
        ItemId survivor=id;
        bool changed=false;
        for(std::size_t i=0;i<items.size();) {
            at=locate(c,survivor);auto& base=items[*at];
            const auto& candidate=items[i];
            bool match=i!=*at && candidate.quantity()!=0 && candidate.charges()<2;
            // Conservatively retain hidden and activation metadata as well as
            // the native comparisons. Never merge different effects away.
            for(std::size_t field=0x2e;field<63 && match;++field) if(field!=0x34 && field!=0x39 && base.byte(field)!=candidate.byte(field)) match=false;
            if(!match) {++i;continue;}
            const unsigned total=static_cast<unsigned>(base.quantity())+candidate.quantity();changed=true;
            if(total<256) {base.bytes_[0x39]=static_cast<std::uint8_t>(total);items.erase(items.begin()+static_cast<std::ptrdiff_t>(i));}
            else {base.bytes_[0x39]=255;items[i].bytes_[0x39]=static_cast<std::uint8_t>(total-255);survivor=items[i].id();++i;}
        }
        if(!changed) return reject("No matching stack can be joined");
        refresh(c,party_.item_catalog());
        return outcome(ItemActionStatus::applied,{},survivor);
    });
}
ItemActionResult InventoryService::identify(std::size_t member,ItemId id,IdentificationContext context,bool confirmed) {
    if(member>=party_.size()) return reject("No such party member");
    const auto selected=locate(party_.member(member),id);
    if(!selected) return reject("Item is no longer owned by this character");
    if(!context.service_available) return outcome(ItemActionStatus::unresolved,"Identification requires an available shop service");
    if(!confirmed) return outcome(ItemActionStatus::confirmation_required,native(0x1ec8)+party_.member(member).inventory().item(*selected).name(),id);
    std::optional<std::array<std::uint32_t,5>> pool;
    if(context.pooled_coins) pool=*context.pooled_coins;
    const auto result=mutate([&](auto& members,ItemId&) {
        auto& c=members[member];
        const auto at=locate(c,id);
        if(!at) return reject("Item is no longer owned by this character");
        const auto& catalog=party_.item_catalog();
        std::array<std::uint32_t,5> coins{};
        for(std::size_t i=0;i<5;++i) coins[i]=c.word(0x88+2*i);
        auto wealth=gold_value(coins,catalog);
        bool use_pool=false;
        if(wealth<200) {
            if(!pool) return outcome(ItemActionStatus::unresolved,"Campaign treasure pool is not restored");
            wealth=gold_value(*pool,catalog);
            if(wealth<200) return reject(native(0x1eff));
            use_pool=true;
        }
        const auto remaining=static_cast<std::uint16_t>(wealth-200);
        if(use_pool) {*pool={0,0,0,static_cast<std::uint32_t>(remaining%5),static_cast<std::uint32_t>(remaining/5)};}
        else {for(std::size_t i=0;i<5;++i) c.set_word(0x88+2*i,0);c.set_word(0x8e,static_cast<std::uint16_t>(remaining%5));c.set_word(0x90,static_cast<std::uint16_t>(remaining/5));}
        c.inventory_.items_[*at].bytes_[0x35]=0;
        refresh(c,catalog);
        return outcome(ItemActionStatus::applied,{},id);
    });
    if(result.applied() && pool && context.pooled_coins) *context.pooled_coins=*pool;
    return result;
}
ItemActionResult InventoryService::read_scroll(std::size_t member,ItemId id) {
    if(member>=party_.size()) return reject("No such reader");
    return mutate([&](auto& members,ItemId&) {
        auto& c=members[member];const auto at=locate(c,id);
        if(!at) return reject("Scroll is no longer owned");
        auto& item=c.inventory_.items_[*at];const auto& definition=party_.item_catalog().definition(item.type());
        if(!definition.scroll()) return reject("Item is not a scroll");
        if(!item.readied()) return reject(native(0xec3));
        for(const auto raw:item.effect_bytes()) if(raw && (!(raw&127) || (raw&127)>56))
            return outcome(ItemActionStatus::unresolved,"Unknown native scroll spell identifier",id);
        const bool cleric=c.byte(0x96)>0 && c.byte(0x96)<128 && definition.category()==12;
        const bool magic=c.effects_restored() && std::any_of(c.effects().begin(),c.effects().end(),[](const CharacterEffect& effect){return effect.code()==0x10;});
        if(item.hidden_components() && !cleric && !magic)
            return outcome(ItemActionStatus::unresolved,"Reading requires this character's magic-reading effect",id);
        item.bytes_[0x35]=0;return outcome(ItemActionStatus::applied,{},id);
    });
}
ItemActionResult InventoryService::prepare_use(std::size_t member,ItemId id,std::size_t target,std::optional<std::size_t> scroll_slot) const {
    if(member>=party_.size() || target>=party_.size()) return reject("An actual target party member is required");
    const auto& c=party_.member(member);
    const auto at=locate(c,id);
    if(!at) return reject("Item is no longer owned by this character");
    const auto& item=c.inventory().item(*at);
    if(!item.readied()) return reject(native(0xec3));
    const auto scroll=party_.item_catalog().definition(item.type()).scroll();
    std::uint8_t effect=0;
    if(scroll) {
        for(const auto raw:item.effect_bytes()) if(raw && (!(raw&0x7f) || (raw&0x7f)>56))
            return outcome(ItemActionStatus::unresolved,"Unknown native scroll spell identifier",id);
        const bool reading_magic=c.effects_restored() && std::any_of(c.effects().begin(),c.effects().end(),[](const CharacterEffect& effect){return effect.code()==0x10;});
        if(item.hidden_components() && !reading_magic && !(c.byte(0x96)>0 && c.byte(0x96)<128 && party_.item_catalog().definition(item.type()).category()==12))
            return outcome(ItemActionStatus::unresolved,"Scroll reading requires its read-magic effect service",id);
        if(!scroll_slot || *scroll_slot>=3) return outcome(ItemActionStatus::choice_required,"Choose an actual spell on this item",id);
        effect=item.effect_bytes()[*scroll_slot];
        if(!effect) return reject("The selected spell slot is empty");
        if((effect&0x7f)>56 || !(effect&0x7f)) return outcome(ItemActionStatus::unresolved,"Unknown native scroll spell identifier",id);
    } else if(item.byte(0x3e)<128) effect=item.byte(0x3d);
    if(!effect) return reject("This item has no activation");
    ItemEffectRequest request{ItemEffectTrigger::use,member,target,id,party_.revision(),effect,static_cast<std::uint8_t>(scroll?effect&0x7f:effect>56?effect-23:effect),item.effect_bytes(),scroll_slot,item.origin()};
    auto result=outcome(ItemActionStatus::unresolved,"Item activation requires its spell/effect service",id);result.effect=request;
    return result;
}
ItemActionResult InventoryService::use(const ItemEffectRequest& request,IItemEffectResolver& resolver) {
    if(request.trigger!=ItemEffectTrigger::use || request.revision!=party_.revision()) return reject("Item activation is stale");
    const auto prepared=prepare_use(request.actor,request.item,request.target,request.scroll_slot);
    if(!prepared.effect || prepared.effect->native_effect!=request.native_effect || prepared.effect->raw_effects!=request.raw_effects || prepared.effect->cast_index!=request.cast_index || prepared.effect->origin!=request.origin)
        return reject("Item activation no longer matches the source item");
    return mutate([&](auto& members,ItemId&) {
        const auto resolved=resolver.resolve(request,members);
        if(!resolved.applied()) return resolved;
        auto& c=members[request.actor];
        const auto at=locate(c,request.item);
        if(!at) return reject("Effect removed its source item unexpectedly");
        auto& items=c.inventory_.items_;auto& item=items[*at];
        if(party_.item_catalog().definition(item.type()).scroll()) {
            std::size_t consumed=*request.scroll_slot;
            for(std::size_t i=0;i<3;++i) if((item.effect_bytes()[i]&0x7f)==request.cast_index) consumed=i;
            item.bytes_[0x3c+consumed]=0;--item.bytes_[0x30];item.bytes_[0x35]=0;
            if(item.byte(0x30)<0xd2) items.erase(items.begin()+static_cast<std::ptrdiff_t>(*at));
        } else if(item.charges()) {
            if(item.quantity()>=2) --item.bytes_[0x39];
            else if(--item.bytes_[0x3c]==0) items.erase(items.begin()+static_cast<std::ptrdiff_t>(*at));
        }
        refresh(c,party_.item_catalog());
        if(request.target!=request.actor) refresh(members[request.target],party_.item_catalog());
        return outcome(ItemActionStatus::applied,{},request.item);
    });
}
ItemActionResult InventoryService::grant(std::size_t member,ItemTemplateId template_id) {
    if(member>=party_.size()) return reject("No such party member");
    const auto& templates=party_.item_catalog().templates();
    const auto found=templates.find(template_id);
    if(found==templates.end()) return reject("Unknown original item template");
    return mutate([&](auto& members,ItemId& next) {
        auto& c=members[member];
        if(c.inventory().size()>=16) return reject("Inventory has sixteen items");
        auto item=found->second;item.id_=next++;item.bytes_[0x34]=0;
        const auto id=item.id();
        c.inventory_.items_.push_back(std::move(item));
        refresh(c,party_.item_catalog());
        return outcome(ItemActionStatus::applied,{},id);
    });
}
}
