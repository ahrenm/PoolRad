#include "gbengine/qol/journal.hpp"
#include "gbengine/world/exploration_session.hpp"
#include "gbengine/debug/runtime.hpp"

namespace GBEngine {
void ExplorationSession::set_qol_upgrades(bool enabled,const std::filesystem::path& directory) {
    qol_upgrades_=enabled;
    if(!enabled || (journal_rules_ && journal_directory_==directory))return;
    journal_directory_=directory;journal_supported_=false;journal_rules_.reset();
    try {
        NativeFiles content(directory);
        journal_rules_=std::make_shared<JournalRules>(files_,content);
        journal_supported_=true;journal_rules_error_.clear();
    }catch(const std::exception&) {
        // Discovery metadata has no dependency on optional PDF artwork.
        // The built-in registry still authenticates all eight ECL archives.
        try {
            journal_rules_=std::make_shared<JournalRules>(files_);
            journal_supported_=true;journal_rules_error_.clear();
        }catch(const std::exception& error){journal_rules_error_=error.what();}
    } // Other QoL features remain usable.
}
void ExplorationSession::prepare_journal() {
    if(!qol_upgrades_)throw DecodeError("Journal requires --qol");
    if(!journal_supported_)throw DecodeError("Journal capture unavailable: "+journal_rules_error_);
    if(!can_act())throw DecodeError("Journal requires idle exploration");
    if(vm.script_modified_) {
        if(!journal_.state.stored)throw DecodeError("Journal recovery requires an unmodified source script");
        return;
    }
    journal_.state.recover(vm.state_banks());
}
void ExplorationSession::complete_text_request(std::uint64_t token) {
    const auto& request=vm.pending_request();
    if(!request || request->kind!=EclRequestKind::print || request->token!=token)
        throw DecodeError("Stale text receipt completion");
    // Resolve before accepting the request, but publish only after the VM accepts.
    // A failed/stale completion cannot create knowledge. No renderer callback owns it.
    std::optional<JournalReceipt> receipt;
    if(qol_upgrades_) {
        if(journal_.pending && journal_.pending->token!=token)throw DecodeError("Stale Journal receipt owner");
        if(journal_.pending)receipt=journal_.pending;
        else if(journal_supported_ && !vm.script_modified_)
            receipt=journal_rules_->resolve(token,disk(),record_,request->address,request->text_id,request->text);
        else receipt=JournalReceipt{token,disk(),record_,request->address,request->text_id,request->text,
            vm.script_modified_?"modified-script":"unsupported-profile-or-registry",{}};
    }
    if(receipt)journal_.stage(std::move(*receipt));
    vm.complete_request(token);
    if(journal_.pending)Debug::journal_receipt(journal_.completed(token));
}
}
