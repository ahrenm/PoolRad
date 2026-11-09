#include "gbengine/tools/ecl_diagnostics.hpp"
#include <iomanip>
#include <sstream>
namespace GBEngine {
namespace {
void drive(EclVm& vm,std::size_t budget,std::ostream* text) {
    while(vm.steps()<budget) {
        const auto status=vm.run(budget-vm.steps());
        if(status!=EclStatus::waiting || vm.pending_request()->kind!=EclRequestKind::print || vm.steps()==budget) return;
        if(text) *text<<"TEXT "<<std::quoted(vm.pending_request()->text)<<'\n';
        vm.complete_request(vm.pending_request()->token);
    }
}
std::string fields(const std::vector<std::uint16_t>& values) {
    std::ostringstream out;
    for(std::size_t i=0;i<values.size();++i) { if(i) out<<',';out<<values[i]; }
    return out.str();
}
}
int trace_ecl(const IFileSystem& files,std::string_view name,std::uint8_t id,
              std::size_t entry,std::size_t budget,std::uint32_t seed,bool trace,std::ostream& out) {
    if(classify_asset(name)!=AssetKind::ecl) throw DecodeError("--ecl requires an ECL archive");
    const DaxArchive archive(files.read_asset(name));
    for(const auto& record:archive.entries) if(record.id==id) {
        EclVm vm(record.data,seed);vm.start_entry(entry);
        out<<"ECL "<<name<<" record="<<int(id)<<" entry="<<entry<<" seed="<<seed
           <<" initial_state=zero_neutral_banks,no_character_bindings\n";
        drive(vm,budget,&out);
        if(trace) for(const auto& t:vm.trace()) out<<"TRACE 0x"<<std::hex<<t.address<<" "<<ecl_opcode_name(t.opcode)
            <<" [0x"<<int(t.opcode)<<"] -> 0x"<<t.next_address<<std::dec<<" "<<ecl_status_name(t.status)<<'\n';
        if(trace && vm.steps()>vm.trace().size()) out<<"TRACE limited to first "<<vm.trace().size()<<" instructions\n";
        out<<"STOP "<<ecl_status_name(vm.status())<<" pc=0x"<<std::hex<<vm.pc()<<std::dec<<" steps="<<vm.steps()<<'\n';
        if(vm.pending_request()) {
            const auto& r=*vm.pending_request();
            out<<"HOST "<<ecl_request_name(r.kind)<<" token="<<r.token<<" values="<<fields(r.values);
            if(!r.text.empty()) out<<" text="<<std::quoted(r.text);
            out<<'\n';
        }
        if(!vm.error().empty()) out<<vm.error()<<'\n';
        if(vm.status()==EclStatus::halted) return 0;
        if(vm.status()==EclStatus::waiting) return 2;
        if(vm.status()==EclStatus::budget_exhausted) return 3;
        return 1;
    }
    throw DecodeError("ECL archive has no requested record ID");
}
int inspect_ecl_corpus(const IFileSystem& files,std::size_t budget,std::uint32_t seed,std::ostream& out) {
    out<<"archive\trecord\tentry\tstatus\tpc\tsteps\trequest\tvalues\tdiagnostic\n";
    std::size_t count=0;
    for(const auto& name:files.list_assets()) {
        if(classify_asset(name)!=AssetKind::ecl) continue;
        const DaxArchive archive(files.read_asset(name));
        for(const auto& record:archive.entries) for(std::size_t entry=0;entry<5;++entry) {
            EclVm vm(record.data,seed);vm.start_entry(entry);drive(vm,budget,nullptr);
            out<<name<<'\t'<<int(record.id)<<'\t'<<entry<<'\t'<<ecl_status_name(vm.status())
               <<"\t0x"<<std::hex<<vm.pc()<<std::dec<<'\t'<<vm.steps()<<'\t';
            if(vm.pending_request()) out<<ecl_request_name(vm.pending_request()->kind)<<'\t'<<fields(vm.pending_request()->values);
            else out<<'\t';
            out<<'\t'<<vm.error()<<'\n';++count;
        }
    }
    if(!count) throw DecodeError("No ECL records found");
    // This is a stop/coverage inventory, not a claim that every case completed.
    return 0;
}
}
