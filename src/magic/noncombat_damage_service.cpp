#include "gbengine/magic/noncombat_damage_service.hpp"
#include "gbengine/effects/effect_lifecycle.hpp"
#include <algorithm>
namespace GBEngine {
void NoncombatDamageService::apply(Character& target,std::uint8_t amount,std::uint8_t flags,std::mt19937*,EffectExpiryEdits* edits) {
    if(flags!=0 && flags!=8)throw DecodeError("Noncombat damage flags require another native damage context");
    if(!target.effects_restored())throw DecodeError("Damage requires a restored effect list");
    auto staged=target;
    // 49D5:02FA phase6 visits first matching nodes in fixed family order.
    // These callbacks depend on ambient pending code/spell/level or a live
    // attacker; the originating periodic/alignment call does not initialize it.
    const auto has=[&](std::uint8_t code){return std::any_of(staged.effects_.begin(),staged.effects_.end(),[&](const auto& e){return e.code()==code;}) ||
        (edits && std::any_of(edits->append.begin(),edits->append.end(),[&](const auto& e){return e.code()==code;}));};
    if(has(0x69)||has(0x6a)||has(0x11)||has(0x1c)||has(0x5b)||has(0x7a))
        throw DecodeError("Damage query requires the native pending spell or attacker context");
    //65/2EE2:2814 appends delayed3B;76 is a cold-bit reduction and does
    //nothing for these flags.7A needs the real selected primary/dice scratch.
    //Scheduling65 is independent of damage flags. Other participants are
    // empty or gated by elemental bits1/2/4 in this bounded flag0/8 context.
    if(has(0x65) && !has(0x62) && !has(0x3b)) {
        if(staged.effects_.size()>=65535)throw DecodeError("Damage effect count exceeds native bound");
        CharacterEffect node;node.payload={0x3b,3,0,255,1};
        node.source_file="GAME.OVR";node.source_offset=0x1146f;
        if(edits)edits->append.push_back(std::move(node));else staged.effects_.push_back(std::move(node));
    }
    if(amount) {
        // 49D5:1344 calls10BA:2228 directly. Unlike the ECL target wrapper,
        // this path does not exclude an already-dead status before HP math.
        const auto hp=staged.hit_points();
        const unsigned deficit=amount>hp?amount-hp:0;
        const unsigned remaining=amount<=hp?hp-amount:0;
        if(deficit>=10 || (remaining==0 && staged.status()==1))staged.set_byte(0x10c,6);
        else if(deficit)staged.set_byte(0x10c,5);
        else if(!remaining)staged.set_byte(0x10c,4);
        if(staged.status()!=0 && staged.status()!=1) {staged.set_byte(0x10d,0);staged.set_byte(0x11b,0);}
        else staged.set_byte(0x11b,static_cast<std::uint8_t>(remaining));
    }
    target=std::move(staged);
}
bool NoncombatRecoveryService::revive(Character& target,std::uint8_t hp,std::uint8_t execution_mode) {
    if(execution_mode!=1 && execution_mode!=2)throw DecodeError("Recovery requires a noncombat execution context");
    // 4F3E:10A7 succeeds outside execution5, without tactical placement.
    target.set_byte(0x10c,0);target.set_byte(0x10d,1);target.set_byte(0x11b,hp);return true;
}
bool NoncombatRecoveryService::heal(Character& target,std::uint8_t amount,bool force,std::uint8_t execution_mode) {
    const auto status=target.status();
    if(status!=0 && status!=1 && status!=4 && status!=5)return false;
    if(force ? target.hit_points()>=target.byte(0x32) :
       std::any_of(target.effects().begin(),target.effects().end(),[](const auto& e){return e.code()==0x32;}))return false;
    auto staged=target;
    staged.set_byte(0x11b,std::min(staged.byte(0x32),static_cast<std::uint8_t>(staged.hit_points()+amount)));
    if(!staged.active()) {
        if(status==5)staged.set_byte(0x10c,4);
        if(staged.status()==4)revive(staged,staged.hit_points(),execution_mode);
    }
    target=std::move(staged);return true;
}
}
