#include "gbengine/app/options.hpp"
#include "gbengine/platform/paths.hpp"
#include "gbengine/app/exploration_scenario.hpp"
#include <charconv>
#include <stdexcept>
namespace GBEngine {
namespace {
std::size_t number(std::string_view text) {
    std::size_t value{};const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    if(error!=std::errc{} || end!=text.data()+text.size()) throw std::invalid_argument("Invalid numeric option");
    return value;
}
}
LaunchOptions LaunchOptions::parse(std::span<const std::string_view> arguments) {
    LaunchOptions result;result.installed_layout=true;bool inspect_only=false,explore=false,vm_corpus=false,record_supplied=false,disk_supplied=false,data_supplied=false;
    for (std::size_t i=0;i<arguments.size();++i) {
        const std::string_view arg=arguments[i];
        if(arg=="--qol") result.qol_upgrades=true;
        else if(arg=="--journal-pdf" && i+1<arguments.size()) {result.journal_pdf=utf8_path(arguments[++i]);if(result.journal_pdf.empty())throw std::invalid_argument("Journal PDF path cannot be empty");}
        else if(arg=="--journal-data" && i+1<arguments.size()) {result.journal_directory=utf8_path(arguments[++i]);result.journal_override=true;if(result.journal_directory.empty())throw std::invalid_argument("Journal directory cannot be empty");}
        else if((arg=="--install-root" || arg=="--setup-from") && i+1<arguments.size()) {
            const auto value=arguments[++i];if(value.empty())throw std::invalid_argument("Setup path cannot be empty");
            if(arg=="--install-root")result.install_root=utf8_path(value);else result.setup_source=utf8_path(value);
        }
        else if (arg=="--explore") explore=true;
        else if(arg=="--start") result.mode=RunMode::startup;
        else if(arg=="--mute") result.sound_device="off";
        else if(arg=="--sound" && i+1<arguments.size()) {
            result.sound_device=arguments[++i];
            if(result.sound_device!="auto" && result.sound_device!="off" && result.sound_device!="pc-speaker" && result.sound_device!="tandy")
                throw std::invalid_argument("Sound device must be auto, off, pc-speaker or tandy");
        }
        else if(arg=="--debug-trace" && i+1<arguments.size()) {result.debug_trace_directory=utf8_path(arguments[++i]);if(result.debug_trace_directory.empty())throw std::invalid_argument("Debug trace directory cannot be empty");}
        else if(arg=="--viewer") result.mode=RunMode::asset_viewer;
        else if(arg=="--load-menu") {result.load_menu=true;explore=true;}
        else if (arg=="--inspect") inspect_only=true;
        else if (arg=="--smoke") result.smoke=true;
        else if (arg=="--vm-corpus") vm_corpus=true;
        else if (arg=="--trace") result.trace=true;
        else if ((arg=="--data" || arg=="--archive" || arg=="--scale" || arg=="--snapshot" || arg=="--extract" ||
                  arg=="--strings" || arg=="--scenario" || arg=="--party" || arg=="--load" || arg=="--resume" || arg=="--user-data" || arg=="--disk" || arg=="--ecl" || arg=="--record" || arg=="--entry" || arg=="--budget" || arg=="--seed") && i+1<arguments.size()) {
            auto value=std::string_view(arguments[++i]);
            if (arg=="--load") {
                if(value.size()!=1 || !((value[0]>='A' && value[0]<='J') || (value[0]>='a' && value[0]<='j')))
                    throw std::invalid_argument("Campaign slot must be A..J");
                result.save_slot=value;
            }
            else if(arg=="--resume") {
                if(value.size()!=1 || !((value[0]>='A' && value[0]<='J') || (value[0]>='a' && value[0]<='j')))
                    throw std::invalid_argument("Port save slot must be A..J");
                result.resume_slot=value;
            }
            else if(arg=="--user-data") {if(value.empty()) throw std::invalid_argument("User-data directory cannot be empty");result.user_data_directory=utf8_path(value);result.user_data_override=true;}
            else if (arg=="--party") { if(value!="A" && value!="J" && value!="a" && value!="j") throw std::invalid_argument("Party fixture must be A or J");result.party_fixture=value; }
            else if (arg=="--scenario") {if(!find_exploration_scenario(value)) throw std::invalid_argument("Unknown exploration scenario");result.scenario_fixture=value;}
            else if (arg=="--disk") { disk_supplied=true;result.disk=number(value);if(result.disk<1 || result.disk>8) throw std::invalid_argument("Disk must be 1..8"); }
            else if (arg=="--data") {result.data_root=value;data_supplied=true;result.installed_layout=false;}
            else if (arg=="--strings") {result.strings_file=utf8_path(value);result.strings_override=true;}
            else if (arg=="--archive") {result.archive=value;result.mode=RunMode::asset_viewer;}
            else if (arg=="--snapshot") result.snapshot=utf8_path(value);
            else if (arg=="--extract") result.extraction_directory=utf8_path(value);
            else if (arg=="--ecl") result.script_archive=value;
            else if (arg=="--record") { record_supplied=true;result.record=number(value);if(result.record>255) throw std::invalid_argument("Record ID must be 0..255"); }
            else if (arg=="--entry") { result.entry=number(value);if(result.entry>4) throw std::invalid_argument("Entry must be 0..4"); }
            else if (arg=="--budget") { result.instruction_budget=number(value);if(result.instruction_budget<1 || result.instruction_budget>1000000) throw std::invalid_argument("Budget must be 1..1000000"); }
            else if (arg=="--seed") { result.random_seed=number(value);if(result.random_seed>0xffffffffu) throw std::invalid_argument("Seed must fit 32 bits"); }
            else { const auto n=number(value); if (n<2 || n>6) throw std::invalid_argument("Scale must be 2..6"); result.resolution_index=n-2; }
        } else if (arg=="--help") {
            result.mode=RunMode::help;return result;
        } else throw std::invalid_argument("Unknown or incomplete option: "+std::string(arg));
    }
    if(data_supplied && (!result.install_root.empty() || !result.setup_source.empty()))throw std::invalid_argument("Choose --data for development assets or installation setup options");
    if(!result.save_slot.empty() && !result.resume_slot.empty()) throw std::invalid_argument("Choose original campaign import or port save resume");
    if(result.load_menu && (!result.save_slot.empty() || !result.resume_slot.empty())) throw std::invalid_argument("Choose a load menu or an explicit slot");
    if(result.load_menu || !result.save_slot.empty() || !result.resume_slot.empty()) {
        if(!result.party_fixture.empty() || !result.scenario_fixture.empty() || disk_supplied || record_supplied || vm_corpus || inspect_only || !result.script_archive.empty() || !result.extraction_directory.empty())
            throw std::invalid_argument("Campaign load cannot be combined with fixtures, map overrides or inspection modes");
        explore=true;
    }
    if(!result.scenario_fixture.empty()) {
        if(disk_supplied || record_supplied || vm_corpus || inspect_only || !result.script_archive.empty() || !result.extraction_directory.empty()) throw std::invalid_argument("Scenario cannot be combined with another map or inspection mode");
        const auto* scenario=find_exploration_scenario(result.scenario_fixture);
        if(!scenario)throw std::invalid_argument("Unknown exploration scenario");
        explore=true;result.disk=scenario->disk;result.record=scenario->record;record_supplied=true;
    }
    if(vm_corpus) result.mode=RunMode::ecl_corpus;
    else if(!result.script_archive.empty()) result.mode=RunMode::ecl_trace;
    else if(!result.extraction_directory.empty()) result.mode=RunMode::extract_assets;
    else if(explore) result.mode=RunMode::exploration;
    else if(inspect_only) result.mode=RunMode::inspect_assets;
    if(explore && !record_supplied) result.record=2;
    return result;
}
void print_help(std::ostream& out) {
    out<<"Default launch: first-run setup beside the executable; Game, Save, Text and Journals folders\n"
        <<"--install-root DIR: override the installation folder; --setup-from DIR: unattended DOS resource import\n"
        <<"--data DIR: explicit developer asset path; preserves legacy data/cache/storage defaults\n";
    out<<"--journal-data DIR: QoL Journal PNG store (installed default Journals)\n"
        <<"--journal-pdf FILE: optional supported GOG Adventurers Journal PDF; failures are logged and non-fatal\n"
        <<"Setup diagnostics: pr_setup.log beside the executable (or under --install-root)\n";
    out<<"--qol: enable camp Fix, 2-hour wilderness steps, training indicators and J Journal\n"
        <<"PoolRad reconstruction: original startup, character creation and supported campaign play\n"
        <<"--debug-trace DIR: opt-in campaign recording/inspection; F8 assisted combat win; F9 state + screenshot\n"
        <<"--start: title, credits, retired copy-protection screen and character/party menu (default)\n"
        <<"--viewer: inspect original image records with arrow keys\n"
        <<"--sound auto|off|pc-speaker|tandy: original POOL.CFG selection or explicit host override; --mute disables sound\n"
        <<"--data DIR --archive FILE.DAX --scale 2..6 --inspect --smoke --snapshot FILE.ppm --extract DIR\n"
        <<"--strings FILE.toml: generated runtime string cache (installed default Text/strings.toml)\n"
        <<"--ecl ECLn.DAX --record ID --entry 0..4 --budget N --seed N --trace\n"
        <<"--vm-corpus: bounded coverage inventory of all five entry points of each ECL record\n"
        <<"--explore --disk 1..8 --record ID: ECL event preview; Up step, Left/Right turn, Down turn around, L look, Enter continue\n"
        <<"--party A|J: character/item fixture import; original saves are read-only; campaign/spells not restored\n"
        <<"--load A..J: import an original interior or supported overland campaign, read-only\n"
        <<"--load-menu: discover port saves and original imports; F10 opens the same browser (F9 also loads without debug tracing) in idle exploration\n"
        <<"--resume A..J: resume a port save (installed default Save); --user-data DIR overrides the storage root\n"
        <<"Idle exploration: E opens camp; save or cancel slot selection, then answer the exit query to quit\n"
        <<"With imported party: 1-6 View; I Inventory; L/Backspace Leave; Escape cancels modal choices\n"
        <<"Inventory: R ready/unready; T trade; D confirm drop; H halve; J join; U use; I shop identification\n"
        <<"--scenario plaza: opt-in GEO1/18 introduction/list-menu fixture; no saved campaign restored\n"
        <<"--scenario parlay: opt-in GEO4/2 direct parlay script fixture; no saved campaign restored\n"
        <<"--scenario text-input|number-input: opt-in original input instruction fixtures; no saved campaign restored\n"
        <<"--scenario glyphs: opt-in ECL4/21 symbol-table fixture; Enter resumes the original script\n"
        <<"--scenario party-strength --party A|J: opt-in original ECL1/18 rating instruction; Enter resumes\n"
        <<"--scenario overland [--party A|J]: isolated original map/party movement; arrows or keypad2/4/6/8; no events or links\n"
        <<"--scenario overland-west|overland-east [--party A|J]: isolated Region 2/4 rendering and movement\n"
        <<"--scenario overland-links|overland-links-west|overland-links-east --party A|J: original regional/link scripts, unbound campaign time\n"
        <<"--scenario overland-travel --party A|J: original overland events, source-save party/clock/persistent quest flags, explicit test location\n"
        <<"--scenario overland-recovery --party A|J: original pollution recovery event, explicit quest progress FE/river permission0\n"
        <<"--scenario combat-entry --party A|J: original ECL3/8 guard combat setup fixture; F12 rolls back\n"
        <<"--scenario combat-movement --party A|J: guard fixture with explicit enemy surprise for first-round movement testing\n"
        <<"--scenario combat-weapons --party A|J: guard fight fixture with restored party graphs and source-save clock\n"
        <<"--scenario combat-duel --party A|J: original mirror-duel setup with restored party and clock\n"
        <<"--scenario encounter-dialogue --party A|J: original GEO4/2 encounter choices and combat entry\n"
        <<"Left/Right: record; Up/Down: frame; F5: resolution; close the window to quit the asset viewer\n";

}
}
