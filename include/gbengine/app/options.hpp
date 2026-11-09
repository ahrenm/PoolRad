#pragma once
#include <filesystem>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
namespace GBEngine {
enum class RunMode {startup,asset_viewer,exploration,inspect_assets,extract_assets,ecl_trace,ecl_corpus,help};
struct LaunchOptions {
    RunMode mode=RunMode::startup;
    std::string data_root="SourceBinaries",archive="TITLE.DAX",script_archive,party_fixture,scenario_fixture,save_slot,resume_slot;
    std::string sound_device="auto";
    std::filesystem::path journal_directory="data/journal",strings_file="research/runtime/strings.toml",snapshot,extraction_directory;
    std::filesystem::path user_data_directory="userdata",debug_trace_directory;
    std::filesystem::path install_root,setup_source,journal_pdf;
    bool installed_layout=false,strings_override=false,user_data_override=false,journal_override=false;
    std::size_t disk=4,record=0,entry=4,instruction_budget=10000,random_seed=1,resolution_index=1;
    bool smoke=false,trace=false,load_menu=false,qol_upgrades=false;
    static LaunchOptions parse(std::span<const std::string_view>);
};
void print_help(std::ostream&);
}
