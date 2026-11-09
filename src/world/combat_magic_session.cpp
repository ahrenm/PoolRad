#include "gbengine/world/exploration_session.hpp"

namespace GBEngine {
bool ExplorationSession::schedule_combat_spell(CombatToken token,std::uint64_t revision,std::size_t slot) {
    if(!current_combat(token,revision))return false;
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    try {
        next->schedule_spell(slot,world.party.item_catalog(),vm.read_value(0x49e5)!=0,random);
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,random);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::schedule_combat_item(CombatToken token,std::uint64_t revision,ItemId item,std::optional<std::size_t> slot) {
    if(!current_combat(token,revision))return false;
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;
    const auto expected=vm.random_state();
    try {
        next->schedule_item(item,slot,world.party.item_catalog(),vm.read_value(0x49e5)!=0);
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,expected);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::read_combat_scroll(CombatToken token,std::uint64_t revision,ItemId item) {
    if(!current_combat(token,revision))return false;
    if(vm.read_value(0x49e5)!=0) {combat_->report_action("Casting is disabled by the current game state");return false;}
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;const auto expected=vm.random_state();
    try {
        next->read_scroll(item,world.party.item_catalog());
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,expected);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::resolve_combat_magic(CombatToken token,std::uint64_t revision,
        std::span<const CombatActorIdentity> targets,std::optional<CombatPosition> centre) {
    if(!current_combat(token,revision))return false;
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    try {
        const CombatAttackContext context{{static_cast<std::uint8_t>(vm.read_value(0x6e71)),
                                           static_cast<std::uint8_t>(vm.read_value(0x6e70))}};
        next->resolve_magic(targets,centre,world.party.item_catalog(),vm.read_value(0x49e6),random,context);
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,random);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::ready_combat_item(CombatToken token,std::uint64_t revision,ItemId item) {
    if(!current_combat(token,revision))return false;
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;const auto expected=vm.random_state();auto random=expected;
    try {
        next->ready_item(item,world.party.item_catalog(),&random);
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,random);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
bool ExplorationSession::cancel_combat_magic(CombatToken token,std::uint64_t revision) {
    if(!current_combat(token,revision))return false;
    auto next=std::make_unique<CombatSession>(*combat_);auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    try {
        next->cancel_magic(world.party.item_catalog(),random);
        next_vm.commit_combat_runtime(next->entry().ecl_token,expected,random);
    }catch(const DecodeError& error) {combat_->report_action(error.what());return false;}
    combat_=std::move(*next);vm=std::move(next_vm);return true;
}
}
