#include "gbengine/world/exploration_session.hpp"
#include "gbengine/campaign/treasure_service.hpp"
#include "gbengine/magic/spell_service.hpp"
#include "gbengine/resources/string_store.hpp"
namespace GBEngine {
namespace {
EconomyResult unavailable() {return {EconomyStatus::unresolved,"Campaign service is not active",{},0};}
}
bool ExplorationSession::campaign_requested() const {
    return vm.pending_request() && vm.pending_request()->kind==EclRequestKind::campaign_service;
}
bool ExplorationSession::begin_campaign() {
    if(party_defeated_ || !campaign_requested()) return false;
    if(campaign_token_) return true;
    if(!world.party.size() || !active_character()) {boundary="Campaign service requires an imported party";return false;}
    if(world.encounter.setup_prepared || world.encounter.duel_context || campaign_.combat_setup || vm.read_value(0x6de6)!=0) {boundary="Combat/duel outcome service is not implemented";return false;}
    auto staged=campaign_;auto staged_party=world.party;auto staged_vm=vm;
    const auto kind=vm.read_value(0x6e6c)==1?CampaignServiceKind::shop:
        vm.read_value(0x6de2)==1?CampaignServiceKind::shrine:CampaignServiceKind::treasure;
    std::optional<std::uint32_t> experience;
    std::vector<std::size_t> npc_members;
    if(kind==CampaignServiceKind::shop) {
        staged.amounts.fill(0);staged_vm.update_campaign_state(vm.pending_request()->token,std::array<EclStateUpdate,1>{{{0x6e6c,0}}});
    } else if(kind==CampaignServiceKind::shrine) {
        staged.amounts.fill(0);
        staged_vm.update_campaign_state(vm.pending_request()->token,std::array<EclStateUpdate,1>{{{0x6de2,0}}});
    } else {
        const auto result=TreasureService::begin_collection(staged_party,staged,vm.read_value(0x6e3e));
        if(!result.applied()) {boundary=result.message;return false;}
        experience=result.experience_per_member;
        npc_members=result.npc_members;
        staged_vm.update_campaign_state(vm.pending_request()->token,std::array<EclStateUpdate,2>{{{0x6dc7,0},{0x6dc8,0}}});
        staged_party.select(0); // Native post-encounter wrapper restores the roster head.
    }
    world.party=std::move(staged_party);campaign_=std::move(staged);vm=std::move(staged_vm);
    campaign_kind_=kind;campaign_token_=vm.pending_request()->token;campaign_experience_=experience;
    campaign_npc_members_=std::move(npc_members);
    campaign_result_text_id_=pascal_string_id("GAME.OVR",0x5e11);
    campaign_experience_prefix_id_=pascal_string_id("GAME.OVR",0x5e45);
    return true;
}
bool ExplorationSession::finish_campaign(std::uint64_t token,std::size_t budget) {
    if(appraisal_) throw DecodeError("Resolve the pending appraisal before leaving the shop");
    if(!campaign_token_ || *campaign_token_!=token || !campaign_requested() || vm.pending_request()->token!=token)
        throw DecodeError("Stale campaign service completion");
    if(party_defeated_) throw DecodeError("Defeated party cannot resume campaign execution");
    if(!budget || budget>1000000) throw DecodeError("Invalid campaign execution budget");
    auto staged_vm=vm;auto staged=campaign_;
    std::vector<EclStateUpdate> updates{{0x6dca,static_cast<std::uint16_t>(vm.read_value(0x6dca)&1)}};
    if(campaign_kind_==CampaignServiceKind::treasure) {
        staged.items.clear();
        for(const auto field:{0x6e70,0x6e71,0x6e72,0x6de3,0x6de6}) updates.push_back({static_cast<std::uint16_t>(field),0});
    }
    staged_vm.complete_request(token,updates);
    auto checkpoint=combat_checkpoint_;
    if(combat_collection_token_)checkpoint=prepare_collection_checkpoint(staged_vm,staged);
    vm=std::move(staged_vm);campaign_=std::move(staged);campaign_token_.reset();campaign_kind_.reset();campaign_experience_.reset();campaign_npc_members_.clear();
    campaign_result_text_id_.clear();campaign_experience_prefix_id_.clear();
    if(combat_collection_token_) {combat_collection_token_.reset();combat_checkpoint_=std::move(checkpoint);}
    display_text.clear();pump(budget);return true;
}
EconomyResult ExplorationSession::pool_money() {
    if(party_defeated_ || !campaign_token_ || appraisal_) return unavailable();
    return EconomyService(world.party,campaign_).pool();
}
MagicResult ExplorationSession::detect_treasure() {
    if(party_defeated_ || campaign_kind_!=CampaignServiceKind::treasure || !campaign_token_ ||
       !campaign_requested() || !active_character() || appraisal_ || campaign_.items.empty())
        return {MagicStatus::rejected,"Treasury detection is not available",{}};
    auto party=world.party;auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    MagicEffectService resolver(spell_catalog(),&world.party.item_catalog(),&random,6);
    auto result=SpellService(party,spell_catalog()).cast_treasury_detect(*active_character(),party.revision(),random,resolver);
    if(!result.applied())return result;
    try {next_vm.commit_campaign_random_state(*campaign_token_,expected,random);}
    catch(const DecodeError& error) {return {MagicStatus::unresolved,error.what(),{}};}
    world.party=std::move(party);vm=std::move(next_vm);return result;
}
EconomyResult ExplorationSession::share_money() {
    if(party_defeated_ || !campaign_token_ || appraisal_) return unavailable();
    return EconomyService(world.party,campaign_).share();
}
EconomyResult ExplorationSession::take_money(std::size_t denomination,std::uint16_t amount) {
    if(party_defeated_ || !campaign_token_ || !active_character() || appraisal_) return unavailable();
    return EconomyService(world.party,campaign_).take_money(*active_character(),denomination,amount);
}
EconomyResult ExplorationSession::take_campaign_item(CampaignItemId id) {
    if(party_defeated_ || campaign_kind_!=CampaignServiceKind::treasure || !active_character()) return unavailable();
    return EconomyService(world.party,campaign_).acquire_reward(*active_character(),id);
}
EconomyResult ExplorationSession::buy_item(CampaignItemId id) {
    if(party_defeated_ || campaign_kind_!=CampaignServiceKind::shop || !active_character() || appraisal_) return unavailable();
    return EconomyService(world.party,campaign_).buy(*active_character(),id);
}
EconomyResult ExplorationSession::sell_item(std::size_t member,ItemId id,bool confirmed) {
    if(party_defeated_ || campaign_kind_!=CampaignServiceKind::shop || appraisal_) return unavailable();
    return EconomyService(world.party,campaign_).sell(member,id,confirmed);
}
EconomyResult ExplorationSession::identify_item(std::size_t member,ItemId id,bool confirmed) {
    if(party_defeated_ || campaign_kind_!=CampaignServiceKind::shop || appraisal_) return unavailable();
    return EconomyService(world.party,campaign_).identify(member,id,confirmed);
}
bool ExplorationSession::select_campaign_member(int delta) {
    if(!campaign_token_ || !world.party.size() || appraisal_) return false;
    const auto count=static_cast<int>(world.party.size());const auto current=static_cast<int>(active_character().value_or(0));
    world.party.select(static_cast<std::size_t>((current+delta%count+count)%count));return true;
}
EconomyResult ExplorationSession::appraise_gem() {
    if(party_defeated_ || (campaign_kind_!=CampaignServiceKind::shop && campaign_kind_!=CampaignServiceKind::shrine) || !campaign_token_ || !active_character() || appraisal_) return unavailable();
    auto party=world.party;auto resources=campaign_;auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;AppraisalQuote quote;
    auto result=EconomyService(party,resources).appraise_gem(*active_character(),random,quote);
    if(!result.applied()) return result;
    try {next_vm.commit_campaign_random_state(*campaign_token_,expected,random);}
    catch(const DecodeError& error) {return {EconomyStatus::unresolved,error.what(),{},0};}
    world.party=std::move(party);campaign_=std::move(resources);vm=std::move(next_vm);appraisal_=quote;
    return result;
}
EconomyResult ExplorationSession::appraise_jewelry() {
    if(party_defeated_ || (campaign_kind_!=CampaignServiceKind::shop && campaign_kind_!=CampaignServiceKind::shrine) || !campaign_token_ || !active_character() || appraisal_) return unavailable();
    auto party=world.party;auto resources=campaign_;auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;AppraisalQuote quote;
    auto result=EconomyService(party,resources).appraise_jewelry(*active_character(),random,quote);
    if(!result.applied())return result;
    try {next_vm.commit_campaign_random_state(*campaign_token_,expected,random);}
    catch(const DecodeError& error) {return {EconomyStatus::unresolved,error.what(),{},0};}
    world.party=std::move(party);campaign_=std::move(resources);vm=std::move(next_vm);appraisal_=quote;
    return result;
}
EconomyResult ExplorationSession::resolve_appraisal(bool keep) {
    if(party_defeated_ || (campaign_kind_!=CampaignServiceKind::shop && campaign_kind_!=CampaignServiceKind::shrine) || !campaign_token_ || !appraisal_) return unavailable();
    auto party=world.party;auto resources=campaign_;auto next_vm=vm;const auto expected=vm.random_state();
    try {next_vm.commit_campaign_random_state(*campaign_token_,expected,expected);}
    catch(const DecodeError& error) {return {EconomyStatus::unresolved,error.what(),{},0};}
    auto result=EconomyService(party,resources).resolve_appraisal(*appraisal_,keep);
    if(result.applied()) {world.party=std::move(party);campaign_=std::move(resources);appraisal_.reset();}
    return result;
}
ShrineInspection ExplorationSession::shrine_inspection(std::size_t member,ShrineAction action) const {
    if(party_defeated_ || campaign_kind_!=CampaignServiceKind::shrine || !campaign_token_ || appraisal_)
        return {EconomyStatus::unresolved,0,false,{},{},"Shrine service is not active"};
    return ShrineService::inspect(world.party,member,action);
}
EconomyResult ExplorationSession::apply_shrine(std::size_t member,ShrineAction action,bool unneeded,bool fee) {
    if(party_defeated_ || campaign_kind_!=CampaignServiceKind::shrine || !campaign_token_ || appraisal_) return unavailable();
    auto party=world.party;auto resources=campaign_;auto next_vm=vm;
    const auto expected=vm.random_state();auto random=expected;
    auto result=ShrineService(party,resources,spell_catalog(),world.party.item_catalog()).apply(member,action,random,unneeded,fee);
    if(!result.applied()) return result;
    try {next_vm.commit_campaign_random_state(*campaign_token_,expected,random);}
    catch(const DecodeError& error) {return {EconomyStatus::unresolved,error.what(),{},0};}
    world.party=std::move(party);campaign_=std::move(resources);vm=std::move(next_vm);check_party_defeat();
    return result;
}
}
