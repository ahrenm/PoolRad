#include "gbengine/platform/native_installation.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib,"advapi32.lib")
#endif
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace GBEngine {
namespace fs=std::filesystem;
namespace {
std::string upper(std::string s){for(auto& c:s)if(c>='a' && c<='z')c=static_cast<char>(c-'a'+'A');return s;}
std::map<std::string,fs::path> assets(const fs::path& root) {
    std::map<std::string,fs::path> result;
    for(const auto& entry:fs::directory_iterator(root)) {
        const auto name=upper(entry.path().filename().string());
        if(std::find(NativeInstallation::required_assets().begin(),NativeInstallation::required_assets().end(),name)==NativeInstallation::required_assets().end())continue;
        if(entry.is_symlink() || !entry.is_regular_file() || fs::canonical(entry.path()).parent_path()!=fs::canonical(root))throw DecodeError("Game resources must be ordinary files in the selected folder.");
        if(!result.emplace(name,entry.path()).second)throw DecodeError("The game folder contains duplicate DOS filenames.");
    }
    return result;
}
fs::path unique_directory(const fs::path& parent,std::string_view label) {
    static std::atomic<std::uint64_t> sequence{0};
    for(unsigned i=0;i<1000;++i) {
        const auto name=std::string(label)+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++);
        const auto path=parent/name;if(fs::create_directory(path))return path;
    }
    throw DecodeError("Cannot create a temporary setup folder.");
}
void notify(const SetupProgress& progress,std::string_view text){if(progress)progress(text);}
}
fs::path setup_source_path(std::string_view text) {
    const auto begin=text.find_first_not_of(" \t\r\n");if(begin==std::string_view::npos)throw DecodeError("Enter the path to your POOLRAD folder.");
    text=text.substr(begin,text.find_last_not_of(" \t\r\n")-begin+1);
    if(text.size()>=2 && text.front()=='"' && text.back()=='"')text=text.substr(1,text.size()-2);
    if(text.empty() || text.find_first_of("\r\n")!=std::string_view::npos || text.find('\0')!=std::string_view::npos)throw DecodeError("Enter one folder path.");
    return utf8_path(text);
}
InstallationLayout InstallationLayout::at(const fs::path& path) {
    const auto root=fs::weakly_canonical(fs::absolute(path));return {root,root/"Game",root/"Save",root/"Text",root/"Journals"};
}
std::span<const std::string_view> NativeInstallation::required_assets() {
    static constexpr std::string_view files[]{
        "8X8D1.DAX",
        "8X8D2.DAX",
        "8X8D3.DAX",
        "8X8D4.DAX",
        "8X8D5.DAX",
        "8X8D6.DAX",
        "8X8D7.DAX",
        "8X8D8.DAX",
        "BACPAC.DAX",
        "BODY1.DAX",
        "BODY2.DAX",
        "BODY3.DAX",
        "BODY4.DAX",
        "BODY5.DAX",
        "BODY6.DAX",
        "BODY7.DAX",
        "BODY8.DAX",
        "CBODY.DAX",
        "CHEAD.DAX",
        "COMSPR.DAX",
        "CPIC1.DAX",
        "CPIC2.DAX",
        "CPIC3.DAX",
        "CPIC4.DAX",
        "CPIC5.DAX",
        "CPIC6.DAX",
        "CPIC7.DAX",
        "CPIC8.DAX",
        "DUNGCOM.DAX",
        "ECL1.DAX",
        "ECL2.DAX",
        "ECL3.DAX",
        "ECL4.DAX",
        "ECL5.DAX",
        "ECL6.DAX",
        "ECL7.DAX",
        "ECL8.DAX",
        "FINAL5.DAX",
        "GAME.OVR",
        "GEO1.DAX",
        "GEO2.DAX",
        "GEO3.DAX",
        "GEO4.DAX",
        "GEO5.DAX",
        "GEO6.DAX",
        "GEO7.DAX",
        "GEO8.DAX",
        "HEAD1.DAX",
        "HEAD2.DAX",
        "HEAD3.DAX",
        "HEAD4.DAX",
        "HEAD5.DAX",
        "HEAD6.DAX",
        "HEAD7.DAX",
        "HEAD8.DAX",
        "ICON.DAX",
        "ITEM1.DAX",
        "ITEM2.DAX",
        "ITEM3.DAX",
        "ITEM4.DAX",
        "ITEM5.DAX",
        "ITEM6.DAX",
        "ITEM7.DAX",
        "ITEM8.DAX",
        "ITEMS",
        "MON1CHA.DAX",
        "MON1ITM.DAX",
        "MON2CHA.DAX",
        "MON2ITM.DAX",
        "MON2SPC.DAX",
        "MON3CHA.DAX",
        "MON3ITM.DAX",
        "MON4CHA.DAX",
        "MON4ITM.DAX",
        "MON4SPC.DAX",
        "MON5CHA.DAX",
        "MON5ITM.DAX",
        "MON5SPC.DAX",
        "MON6CHA.DAX",
        "MON6ITM.DAX",
        "MON6SPC.DAX",
        "MON7CHA.DAX",
        "MON7ITM.DAX",
        "MON7SPC.DAX",
        "MON8CHA.DAX",
        "MON8ITM.DAX",
        "MON8SPC.DAX",
        "PIC1.DAX",
        "PIC2.DAX",
        "PIC3.DAX",
        "PIC4.DAX",
        "PIC5.DAX",
        "PIC6.DAX",
        "PIC7.DAX",
        "PIC8.DAX",
        "POOL.CFG",
        "RANDCOM.DAX",
        "SPRIT1.DAX",
        "SPRIT2.DAX",
        "SPRIT3.DAX",
        "SPRIT4.DAX",
        "SPRIT5.DAX",
        "SPRIT6.DAX",
        "SPRIT7.DAX",
        "SPRIT8.DAX",
        "SQRPACI.DAX",
        "START.EXE",
        "TITLE.DAX",
        "WALLDEF1.DAX",
        "WALLDEF2.DAX",
        "WALLDEF3.DAX",
        "WALLDEF4.DAX",
        "WALLDEF5.DAX",
        "WALLDEF6.DAX",
        "WALLDEF7.DAX",
        "WALLDEF8.DAX",
        "WILDCOM.DAX",
    };
    return files;
}
bool NativeInstallation::game_ready(const fs::path& path,std::string* reason) {
    try {
        const auto found=assets(path);
        for(const auto name:required_assets()) {
            const auto file=found.find(std::string(name));
            if(file==found.end())throw DecodeError("Missing game file: "+std::string(name));
            if(fs::file_size(file->second)<(name.ends_with(".DAX")?2u:1u))throw DecodeError("Empty or incomplete game file: "+std::string(name));
        }
        std::ifstream exe(found.at("START.EXE"),std::ios::binary);char signature[2]{};exe.read(signature,2);
        if(!exe || signature[0]!='M' || signature[1]!='Z')throw DecodeError("START.EXE is not a DOS executable.");
        validate_text_sources(NativeFiles(path));
        return true;
    }catch(const std::exception& e){if(reason)*reason=e.what();return false;}
}
void NativeInstallation::create_directories() const {
    fs::create_directories(layout_.root);
    for(const auto& dir:{layout_.save,layout_.text,layout_.journals}) {
        if(fs::is_symlink(dir))throw DecodeError("Installation storage folders cannot be symbolic links.");
        fs::create_directories(dir);
        if(fs::canonical(dir).parent_path()!=fs::canonical(layout_.root))throw DecodeError("Installation storage must stay inside its installation folder.");
    }
}
void NativeInstallation::import_from(const fs::path& source,const SetupProgress& progress) const {
    const auto root=fs::canonical(source);std::string problem;
    if(!game_ready(root,&problem))throw DecodeError(problem+". Select the POOLRAD folder containing the DOS game.");
    create_directories();const auto staging=unique_directory(layout_.root,".setup-pending-");
    const auto game=staging/"Game",cache=staging/"strings.toml";fs::path old_game,old_text;bool published_game=false,published_text=false;
    try {
        fs::create_directory(game);const auto files=assets(root);
        // An explicit resource list excludes ALL DOS saves/characters and GOG extras.
        for(const auto name:required_assets()){notify(progress,"Copying "+std::string(name));fs::copy_file(files.at(std::string(name)),game/std::string(name));}
        if(!game_ready(game,&problem))throw DecodeError(problem);
        notify(progress,"Extracting game text...");NativeFiles selected(game);
        auto strings=StringStore::extract(selected,StringSourceScope::game_resources);strings.save(cache);
        (void)StringStore::load(selected,cache,StringSourceScope::game_resources);
        // Validate before publishing. Repairs retain the previous files as backups.
        if(fs::exists(layout_.game)){old_game=staging;old_game+=".previous-Game";fs::rename(layout_.game,old_game);}
        fs::rename(game,layout_.game);published_game=true;
        if(fs::exists(layout_.strings())){old_text=layout_.text/(staging.filename().string()+".previous.toml");fs::rename(layout_.strings(),old_text);}
        fs::rename(cache,layout_.strings());published_text=true;fs::remove_all(staging);
        // A later launch can retry optional PDF extraction after a manual install.
        std::ofstream hint(layout_.text/"source-path.txt",std::ios::binary|std::ios::trunc);
        hint<<path_utf8(root)<<'\n';
        if(!hint)notify(progress,"Could not retain the optional Journal source path.");
        notify(progress,"Setup complete.");
    }catch(...) {
        std::error_code ignored;
        if(published_text)fs::remove(layout_.strings(),ignored);
        if(!old_text.empty())fs::rename(old_text,layout_.strings(),ignored);
        if(published_game)fs::remove_all(layout_.game,ignored);
        if(!old_game.empty())fs::rename(old_game,layout_.game,ignored);
        fs::remove_all(staging,ignored);throw;
    }
}
StringStore NativeInstallation::prepare_strings(std::ostream& log) const {
    create_directories();NativeFiles files(layout_.game);
    validate_text_sources(files,&log);
    try{return StringStore::load_or_extract(files,layout_.strings(),log,StringSourceScope::game_resources);}
    catch(const std::exception&) {
        // Do not displace a previous cache until the selected game extracts successfully.
        auto strings=StringStore::extract(files,StringSourceScope::game_resources);
        const auto staging=unique_directory(layout_.text,".text-pending-");const auto pending=staging/"strings.toml";fs::path previous;
        try {
            strings.save(pending);(void)StringStore::load(files,pending,StringSourceScope::game_resources);
            if(fs::exists(layout_.strings())){previous=layout_.text/(staging.filename().string()+".previous.toml");fs::rename(layout_.strings(),previous);}
            try{fs::rename(pending,layout_.strings());}catch(...){if(!previous.empty())fs::rename(previous,layout_.strings());throw;}
            fs::remove_all(staging);log<<"Rebuilt installation text cache.\n";return strings;
        }catch(...){std::error_code ignored;fs::remove_all(staging,ignored);throw;}
    }
}
fs::path executable_directory() {
#ifdef _WIN32
    std::wstring buffer(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(!length || length>=buffer.size())throw DecodeError("Cannot locate the application folder.");
    buffer.resize(length);return fs::path(buffer).parent_path();
#elif defined(__linux__)
    std::string buffer(65536,'\0');const auto length=readlink("/proc/self/exe",buffer.data(),buffer.size());
    if(length<=0 || static_cast<std::size_t>(length)>=buffer.size())throw DecodeError("Cannot locate the application folder.");
    buffer.resize(static_cast<std::size_t>(length));return fs::path(buffer).parent_path();
#else
    return fs::current_path();
#endif
}
std::optional<fs::path> gog_poolrad_directory() {
#ifdef _WIN32
    // The 32-bit registry view maps to HKLM\SOFTWARE\WOW6432Node\GOG.com.
    HKEY key{};if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\GOG.com\\Games\\1432640961",0,KEY_READ|KEY_WOW64_32KEY,&key)!=ERROR_SUCCESS)return {};
    DWORD type{},size{};auto code=RegQueryValueExW(key,L"path",nullptr,&type,nullptr,&size);
    if(code!=ERROR_SUCCESS || (type!=REG_SZ && type!=REG_EXPAND_SZ) || size<sizeof(wchar_t) || size>65536){RegCloseKey(key);return {};}
    std::wstring value(size/sizeof(wchar_t)+1,L'\0');code=RegQueryValueExW(key,L"path",nullptr,&type,reinterpret_cast<BYTE*>(value.data()),&size);RegCloseKey(key);
    if(code!=ERROR_SUCCESS)return {};
    value.resize(value.find(L'\0'));
    if(type==REG_EXPAND_SZ){std::wstring expanded(32768,L'\0');const auto n=ExpandEnvironmentStringsW(value.c_str(),expanded.data(),static_cast<DWORD>(expanded.size()));if(!n || n>expanded.size())return {};expanded.resize(n-1);value=std::move(expanded);}
    if(value.empty())return {};
    return fs::path(value)/"POOLRAD";
#else
    return {};
#endif
}
}
