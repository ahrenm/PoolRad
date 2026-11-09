#include "gbengine/combat/equipment_service.hpp"
#include "gbengine/magic/equipment_effect_service.hpp"
#include "gbengine/combat/magic_service.hpp"
#include <algorithm>
#include <functional>

namespace GBEngine {
namespace {
class EquipmentResolver final:public IItemEffectResolver {
public:
    std::function<ItemActionResult(const ItemEffectRequest&,std::span<Character>)> callback;
    ItemActionResult resolve(const ItemEffectRequest& request,std::span<Character> members) override {return callback(request,members);}
};
bool has(const Character& c,std::uint8_t code) {
    return std::any_of(c.effects().begin(),c.effects().end(),[&](const auto& node){return node.code()==code;});
}
}
void CombatEquipmentService::refresh_primary(DeploymentActor& actor,const ItemCatalog& catalog,std::uint8_t round) {
    auto& c=actor.character;const auto old=c.byte(0x113);
    const auto equipped=EquipmentRules::inspect(c,catalog);bool projectile=false;const Item* ammunition=nullptr;
    auto n=c.byte(0xa1);
    if(equipped.slots[0]) {
        const auto& item=c.inventory().item(*equipped.slots[0]);const auto& definition=catalog.definition(item.type());
        if(definition.byte(12)>=2) {
            const auto flags=definition.attack_flags();
            if(flags&16)ammunition=&item;
            if(flags&8) {
                if(flags&1)ammunition=equipped.slots[11]?&c.inventory().item(*equipped.slots[11]):nullptr;
                if(flags&128)ammunition=equipped.slots[12]?&c.inventory().item(*equipped.slots[12]):nullptr;
            }
            projectile=ammunition || flags==10;
            if(projectile)n=std::max(std::uint8_t{2},definition.byte(5));
        }
    }
    if(has(c,0x27)) {
        const auto at=std::find_if(c.effects().begin(),c.effects().end(),[](const auto& node){return node.code()==0x27;});
        if(!(at->payload[3]&16))throw DecodeError("Combat equipment refresh requires presented Haste activation");
        n=static_cast<std::uint8_t>(n<<1);
    }
    if(has(c,0x2a))n=static_cast<std::uint8_t>(n>>1);
    if(has(c,0x3a))actor.extension[6]=0;
    if(round&1)n=static_cast<std::uint8_t>(n+1);
    n=static_cast<std::uint8_t>(n>>1);
    if(projectile && ammunition)n=std::min(n,std::max(std::uint8_t{1},ammunition->quantity()));
    c.set_byte(0x113,actor.extension[8] && n>=old && (projectile || n>=static_cast<unsigned>(old)*2)?c.byte(0xa1):n);
}
std::shared_ptr<const CombatMagicResult> CombatEquipmentService::reject_alignment(DeploymentState& state,
        std::size_t index,ItemId item_id,const CombatEquipmentDamageContext& context) {
    if(!context.effects || !context.random)
        throw DecodeError("Combat alignment equipment needs a staged tactical damage callback");
    auto& c=state.actors.at(index).character;
    if(!context.inherited)for(const auto code:{0x69u,0x6au,0x7au,0x1cu,0x5bu,0x11u})
        if(has(c,static_cast<std::uint8_t>(code)))
            throw DecodeError("Combat alignment equipment requires owned inherited damage scratch");
    auto item=std::find_if(c.inventory_.items_.begin(),c.inventory_.items_.end(),[&](const Item& value){return value.id()==item_id;});
    if(item==c.inventory_.items_.end())throw DecodeError("Combat alignment equipment identity is stale");
    const auto amount=static_cast<std::uint8_t>(item->byte(0x3d)>>4);
    //2EE2:30A5 first unreadies, then sets6819=8 and calls1344 with save0.
    item->bytes_[0x34]=0;c.bound_equipment_effects_.erase(item_id);
    const auto actor=state.actors[index].id;auto native=context.inherited.value_or(CombatMagicContext{});native.caster=actor;
    const auto before=std::make_shared<const DeploymentState>(state);
    auto damage=context.effects->damage(state,actor,amount,8,false,0,native,*context.random);
    auto playback=std::make_shared<CombatMagicResult>();playback->casting_before=before;
    for(const auto& notice:damage.notices) {
        playback->feedback.push_back({damage.state.actors.at(notice.member).id,notice.text_id});
        playback->events.push_back({CombatMagicEventKind::feedback,playback->feedback.size()-1,before,{}});
    }
    const auto& outcome=damage.outcome;
    if(outcome.amount) {
        playback->damage.push_back({actor,actor,outcome.amount,outcome.before_hp,outcome.after_hp,outcome.status,false,outcome.interrupted_spell,8});
        playback->events.push_back({CombatMagicEventKind::damage,0,before,std::make_shared<const DeploymentState>(damage.state)});
    }
    playback->state=damage.state;state=std::move(damage.state);return playback;
}
CombatEquipmentResult CombatEquipmentService::ready(const DeploymentState& state,CombatActorIdentity id,ItemId item,
        const ItemCatalog& catalog,std::uint8_t round,CombatEquipmentDamageContext context) {
    CombatEquipmentResult result{state,{ItemActionStatus::unresolved,{},item,{}}};
    const auto found=std::find_if(result.state.actors.begin(),result.state.actors.end(),[&](const auto& actor){return actor.id==id;});
    if(found==result.state.actors.end() || found->removed)throw DecodeError("Combat equipment actor identity is stale");
    if(!found->character.active() || found->extension[0] || found->extension[3]==0 || found->extension[3]>=128)
        throw DecodeError("Combat equipment requires the active unqueued actor");
    const auto index=static_cast<std::size_t>(found-result.state.actors.begin());
    Party adapter;adapter.members_.push_back(found->character);adapter.item_catalog_=std::make_shared<ItemCatalog>(catalog);
    auto rng=context.random?*context.random:std::mt19937{};auto bound=context;bound.random=context.random?&rng:nullptr;
    EquipmentEffectService effects(&catalog,{&catalog,context.random?&rng:nullptr,5});InventoryService inventory(adapter);
    EquipmentResolver resolver;resolver.callback=[&](const ItemEffectRequest& request,std::span<Character> members) {
        if(request.trigger==ItemEffectTrigger::equip && request.native_effect==0x84) {
            auto& c=members[0];
            const auto owned=std::find_if(c.inventory_.items_.begin(),c.inventory_.items_.end(),[&](const Item& value){return value.id()==item;});
            if(owned==c.inventory_.items_.end())throw DecodeError("Combat equipment item is no longer owned");
            if((owned->byte(0x3d)&15)!=c.byte(0xa0)) {
                result.state.actors[index].character=c;
                result.playback=reject_alignment(result.state,index,item,bound);
                c=result.state.actors[index].character;
                return ItemActionResult{ItemActionStatus::applied,{},item,{}};
            }
        }
        return effects.resolve(request,members);
    };
    result.action=inventory.ready(0,item,&resolver);
    if(!result.action.applied()) {result.state=state;result.playback.reset();return result;}
    result.state.actors[index].character=adapter.member(0);refresh_primary(result.state.actors[index],catalog,round);
    if(context.random)*context.random=std::move(rng);
    return result;
}
CombatEquipmentResult CombatEquipmentService::toggle_native(const DeploymentState& state,CombatActorIdentity id,ItemId id_item,
                                                            const ItemCatalog& catalog,CombatEquipmentDamageContext context) {
    if(!id_item)throw DecodeError("Combat equipment requires a nonzero owned item identity");
    CombatEquipmentResult result{state,{ItemActionStatus::rejected,{},id_item,{}}};
    auto rng=context.random?*context.random:std::mt19937{};
    const auto actor=std::find_if(result.state.actors.begin(),result.state.actors.end(),[&](const auto& candidate){return candidate.id==id;});
    if(actor==result.state.actors.end() || actor->removed)throw DecodeError("Combat equipment actor identity is stale");
    auto& c=actor->character;
    const auto at=std::find_if(c.inventory_.items_.begin(),c.inventory_.items_.end(),[&](const Item& item){return item.id()==id_item;});
    if(at==c.inventory_.items_.end())throw DecodeError("Combat equipment item is no longer owned");
    auto& item=*at;const auto& definition=catalog.definition(item.type());const bool remove=item.readied();
    if(remove && item.cursed())return result;
    if(!remove) {
        const auto slots=EquipmentRules::inspect(c,catalog);
        if(static_cast<unsigned>(c.byte(0x100))+definition.hands()>2)return result;
        std::optional<unsigned> slot;
        if(definition.category()<9)slot=definition.category();
        else if(definition.category()==9)slot=10;
        if(item.type()==0x49)slot=11;
        if(item.type()==0x1c)slot=12;
        if(slot && slots.slots[*slot])return result;
        if(!(definition.class_mask()&c.byte(0xb0)))return result;
        if(item.byte(0x3e)==0x84 && (item.byte(0x3d)&15)!=c.byte(0xa0)) {
            auto bound=context;bound.random=context.random?&rng:nullptr;
            result.playback=reject_alignment(result.state,static_cast<std::size_t>(actor-result.state.actors.begin()),id_item,bound);
            result.action={ItemActionStatus::applied,{},id_item,{}};
            if(context.random)*context.random=std::move(rng);
            return result;
        }
    }
    item.bytes_[0x34]=static_cast<std::uint8_t>(!remove);
    if(item.byte(0x3e)>=128) {
        const auto origin=item.origin();const auto raw=item.effect_bytes();const auto handler=item.byte(0x3e);
        EquipmentEffectService effects(&catalog,{&catalog,context.random?&rng:nullptr,5});
        ItemEffectRequest request{remove?ItemEffectTrigger::unequip:ItemEffectTrigger::equip,0,0,id_item,0,handler,0,raw,{},origin};
        result.action=effects.resolve(request,std::span<Character>(&c,1));
        if(!result.action.applied()) {result.state=state;return result;}
        const auto surviving=std::find_if(c.inventory_.items_.begin(),c.inventory_.items_.end(),[&](const auto& candidate){return candidate.id()==id_item;});
        if(surviving!=c.inventory_.items_.end() && surviving->readied() && surviving->byte(0x3e)>=128)
            c.bound_equipment_effects_.insert(id_item);
        else c.bound_equipment_effects_.erase(id_item);
    } else result.action={ItemActionStatus::applied,{},id_item,{}};
    if(context.random)*context.random=std::move(rng);
    return result;
}
}
