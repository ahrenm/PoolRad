#include "gbengine/debug/save_repository.hpp"
#include "gbengine/debug/runtime.hpp"
#include "gbengine/app/app.hpp"
#include "gbengine/app/game_loop.hpp"
#include "gbengine/app/exploration_scenario.hpp"
#include "gbengine/modes/modes.hpp"
#include "gbengine/platform/sdl_platform.hpp"
#include "gbengine/platform/native_artifact_writer.hpp"
#include "gbengine/platform/native_save_repository.hpp"
#include "gbengine/platform/native_installation.hpp"
#include "gbengine/platform/journal_image_installation.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/tools/asset_tools.hpp"
#include "gbengine/tools/ecl_diagnostics.hpp"
#include "gbengine/audio/native_sound_resolver.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GBEngine {
namespace {
LaunchOptions arguments(int argc,char** argv) {
    std::vector<std::string_view> values;for(int i=1;i<argc;++i) values.emplace_back(argv[i]);
    return LaunchOptions::parse(values);
}
class SnapshotObserver final : public IFrameObserver {
    IArtifactWriter& writer_;const std::filesystem::path& path_;
public:
    SnapshotObserver(IArtifactWriter& writer,const std::filesystem::path& path):writer_(writer),path_(path) {}
    void after_frame(const Framebuffer& frame,IGameFlow& flow) override {write_snapshot(writer_,path_,frame);flow.quit();}
};
}
struct App::Impl {
    LaunchOptions options;
    AppDependencies dependencies;
    std::optional<StringStore> strings;
    std::unique_ptr<ScopedStringStore> string_binding;
    std::unique_ptr<IPlatform> platform;
    std::unique_ptr<GameLoop> loop;
    bool started=false;
    Impl(LaunchOptions launch,AppDependencies deps):options(std::move(launch)),dependencies(std::move(deps)) {}
    int run() {
        if(started) throw std::logic_error("App can only be run once");
        started=true;
        auto& out=dependencies.output?*dependencies.output:std::cout;
        auto& errors=dependencies.errors?*dependencies.errors:std::cerr;
        if(options.mode==RunMode::help) {print_help(out);return 0;}
        std::ofstream setup_log;
        if(!dependencies.log && !dependencies.files) {
            const auto root=options.install_root.empty()?executable_directory():options.install_root;
            std::filesystem::create_directories(root);
            setup_log.open(root/"pr_setup.log",std::ios::app);
            if(!setup_log)throw DecodeError("Cannot open pr_setup.log in the installation folder");
            setup_log<<std::unitbuf<<"\nPoolRad startup / setup\n";
        }
        auto& log=dependencies.log?*dependencies.log:setup_log.is_open()?static_cast<std::ostream&>(setup_log):std::clog;
        try {
        std::optional<NativeInstallation> installation;
        if(options.installed_layout && !dependencies.files) {
            installation.emplace(InstallationLayout::at(options.install_root.empty()?executable_directory():options.install_root));
            const auto& layout=installation->layout();
            if(!NativeInstallation::game_ready(layout.game)) {
                if(!options.setup_source.empty())installation->import_from(options.setup_source,[&](std::string_view message){log<<message<<'\n';});
                else {
                    const auto gog=gog_poolrad_directory();
                    const bool unattended=options.smoke || !options.snapshot.empty() ||
                        options.mode==RunMode::inspect_assets || options.mode==RunMode::extract_assets ||
                        options.mode==RunMode::ecl_trace || options.mode==RunMode::ecl_corpus;
                    if(unattended) {
                        if(!gog)throw DecodeError("Game resources are not installed. Run normal setup or supply --setup-from DIR.");
                        installation->import_from(*gog,[&](std::string_view message){log<<message<<'\n';});
                    }else {
                        const auto selected=run_installation_setup(*installation,log,gog);
                        if(!selected){log<<"Setup cancelled.\n";return 0;}
                        options.setup_source=*selected;
                    }
                }
            }
            installation->create_directories();options.data_root=path_utf8(layout.game);
            if(!options.strings_override)options.strings_file=layout.strings();
            if(!options.user_data_override)options.user_data_directory=layout.save;
            if(!options.journal_override)options.journal_directory=layout.journals;
        }
        if(!installation && !dependencies.files && !options.journal_override)options.journal_directory=executable_directory()/"Journals";
        if(!options.journal_pdf.empty() || (!dependencies.files && !options.journal_override && (installation || options.qol_upgrades))) {
            try {
            auto pdf=options.journal_pdf;
            if(pdf.empty() && !options.setup_source.empty())pdf=options.setup_source.parent_path()/"Adventurers Journal.pdf";
            if(pdf.empty() && installation) {
                std::ifstream hint(installation->layout().text/"source-path.txt",std::ios::binary);
                std::string source;if(std::getline(hint,source) && source.size()<=32768)pdf=utf8_path(source).parent_path()/"Adventurers Journal.pdf";
            }
            if((pdf.empty()||!std::filesystem::is_regular_file(pdf)) && options.journal_pdf.empty()) {
                if(const auto gog=gog_poolrad_directory())pdf=gog->parent_path()/"Adventurers Journal.pdf";
            }
            log<<"Journal PDF: "<<path_utf8(pdf)<<'\n';
            prepare_journal_images(pdf,options.journal_directory,[&](std::string_view message){log<<message<<'\n';});
            }catch(const std::exception& error){log<<"Journal images unavailable: "<<error.what()<<". Game startup continues.\n";}
        }
        if(!dependencies.files) dependencies.files=std::make_unique<NativeFiles>(utf8_path(options.data_root));
        auto& files=*dependencies.files;
        // Every launch owns a game-resource cache. Original character strings
        // are a separate checked roster capability, never a prerequisite for
        // creating a party or viewing unrelated assets.
        strings=installation && !options.strings_override?installation->prepare_strings(log):
            StringStore::load_or_extract(files,options.strings_file,log,StringSourceScope::game_resources);
        string_binding=std::make_unique<ScopedStringStore>(*strings);
        if(!options.party_fixture.empty()) {
            const auto slot=options.party_fixture.front();const bool first=slot=='A' || slot=='a';
            std::vector<std::string> roster;for(unsigned i=1;i<=6;++i)roster.push_back(std::string(first?"CHRDATA":"CHRDATJ")+std::to_string(i));
            auto cache=options.strings_file;cache+=first?".party-A.toml":".party-J.toml";
            strings->prepare_original_roster(files,roster,cache,log);
        }
        if(options.mode==RunMode::ecl_corpus) return inspect_ecl_corpus(files,options.instruction_budget,static_cast<std::uint32_t>(options.random_seed),out);
        if(options.mode==RunMode::ecl_trace) return trace_ecl(files,options.script_archive,static_cast<std::uint8_t>(options.record),options.entry,options.instruction_budget,static_cast<std::uint32_t>(options.random_seed),options.trace,out);
        if(options.mode==RunMode::inspect_assets) return inspect_assets(files,out,errors);
        if(!dependencies.artifacts) {
            const auto* native=dynamic_cast<const NativeFiles*>(&files);
            dependencies.artifacts=std::make_unique<NativeArtifactWriter>(native?native->root():std::filesystem::path(options.data_root));
        }
        if(options.mode==RunMode::extract_assets) return extract_assets(files,*dependencies.artifacts,options.extraction_directory,out);
        if((options.mode==RunMode::startup || options.mode==RunMode::exploration) && !isolated_overland_scenario(options.scenario_fixture) && !dependencies.saves) {
            const auto* native=dynamic_cast<const NativeFiles*>(&files);
            dependencies.saves=std::make_unique<NativeSaveRepository>(installation && !options.user_data_override?options.user_data_directory:options.user_data_directory/"saves",native?native->root():utf8_path(options.data_root));
        }
        if((options.mode==RunMode::startup || options.mode==RunMode::exploration) && !dependencies.characters) {
            const auto* native=dynamic_cast<const NativeFiles*>(&files);
            dependencies.characters=std::make_unique<NativeSaveRepository>(options.user_data_directory/(installation && !options.user_data_override?"Characters":"characters"),native?native->root():utf8_path(options.data_root));
        }
        std::unique_ptr<Debug::Runtime> debug;
        if(!options.debug_trace_directory.empty())debug=std::make_unique<Debug::Runtime>(options.debug_trace_directory,files,*dependencies.artifacts,log);
        if(debug) {
            dependencies.saves=Debug::trace_repository(std::move(dependencies.saves));
            dependencies.characters=Debug::trace_repository(std::move(dependencies.characters),true);
            debug->event("launch","{\"initial_seed_option\":"+std::to_string(options.random_seed)+",\"port_resume_requested\":"+(!options.resume_slot.empty()?"true":"false")+",\"original_import_requested\":"+(!options.save_slot.empty()?"true":"false")+",\"qol_upgrades\":"+(options.qol_upgrades?"true":"false")+"}");
        }
        auto mode=options.load_menu?make_load_mode(files,options,out,dependencies.saves.get(),{},dependencies.characters.get()):
            options.mode==RunMode::startup?make_startup_mode(files,options,out,dependencies.saves.get(),dependencies.characters.get()):
            options.mode==RunMode::exploration?make_exploration_mode(files,options,out,dependencies.saves.get(),dependencies.characters.get()):make_asset_viewer_mode(files,options.archive,errors);
        PlatformOptions platform_options{resolutions.at(options.resolution_index),options.sound_device!="off" && !options.smoke && options.snapshot.empty(),options.smoke || !options.snapshot.empty()};
        if(platform_options.audio && !dependencies.audio_cues) {
            try {
                std::optional<NativeSoundDevice> device;
                if(options.sound_device=="pc-speaker")device=NativeSoundDevice::pc_speaker;
                else if(options.sound_device=="tandy")device=NativeSoundDevice::tandy;
                auto native=std::make_unique<NativeSoundResolver>(NativeSoundResolver::load(files,device));
                if(native->device()==NativeSoundDevice::disabled)platform_options.audio=false;
                dependencies.audio_cues=std::move(native);
            }catch(const std::exception& error){log<<"Native sound unavailable: "<<error.what()<<'\n';}
        }
        platform=dependencies.platform_factory?dependencies.platform_factory(platform_options):std::make_unique<SdlPlatform>(platform_options.resolution,platform_options.audio,platform_options.hidden);
        if(!platform) throw std::runtime_error("Platform factory returned no platform");
        SnapshotObserver observer(*dependencies.artifacts,options.snapshot);
        loop=std::make_unique<GameLoop>(FrameServices{*platform,*platform,*platform,
            platform_options.audio?platform.get():nullptr,dependencies.audio_cues.get()},
            LoopOptions{options.resolution_index,options.smoke?3u:0u,options.snapshot.empty()?nullptr:&observer});
        const auto frames=loop->run(std::move(mode));
        if(loop->audio_stats().unresolved)log<<"Native audio: "<<loop->audio_stats().unresolved<<" source cues were unavailable or unresolved.\n";
        loop.reset();
        if(options.mode==RunMode::exploration) out<<"Exploration preview: "<<frames<<" frames\n";
        else {
            if(!options.snapshot.empty()) out<<"Snapshot: "<<path_utf8(options.snapshot)<<'\n';
            if(options.smoke) out<<"SDL3 smoke passed: "<<frames<<" frames from original "<<options.archive<<'\n';
        }
        return 0;
        }catch(const std::exception& error){log<<"Startup failed: "<<error.what()<<'\n';throw;}
    }
};
App::App(int argc,char** argv):App(arguments(argc,argv)) {}
App::App(LaunchOptions options,AppDependencies dependencies):impl_(std::make_unique<Impl>(std::move(options),std::move(dependencies))) {}
App::~App()=default;
int App::run() {return impl_->run();}
}
