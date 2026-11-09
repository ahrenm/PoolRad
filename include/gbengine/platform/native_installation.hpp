#pragma once
#include "gbengine/resources/string_store.hpp"
#include "gbengine/assets/decoders.hpp"
#include "gbengine/platform/paths.hpp"
#include <functional>
#include <optional>

namespace GBEngine {
struct InstallationLayout {
    std::filesystem::path root,game,save,text,journals;
    static InstallationLayout at(const std::filesystem::path&);
    std::filesystem::path strings() const {return text/"strings.toml";}
};
using SetupProgress=std::function<void(std::string_view)>;
// Owns installation files only. It never imports or changes campaign saves.
class NativeInstallation {
    InstallationLayout layout_;
public:
    explicit NativeInstallation(InstallationLayout layout):layout_(std::move(layout)) {}
    const InstallationLayout& layout() const {return layout_;}
    static std::span<const std::string_view> required_assets();
    static bool game_ready(const std::filesystem::path&,std::string* reason=nullptr);
    void create_directories() const;
    void import_from(const std::filesystem::path&,const SetupProgress& ={}) const;
    StringStore prepare_strings(std::ostream&) const;
};
std::filesystem::path executable_directory();
std::optional<std::filesystem::path> gog_poolrad_directory();
std::filesystem::path setup_source_path(std::string_view utf8);
std::string path_utf8(const std::filesystem::path&);
// First-run host UI, available before any DOS font or text has been installed.
std::optional<std::filesystem::path> run_installation_setup(const NativeInstallation&,std::ostream&,
                            std::optional<std::filesystem::path> discovered={},bool hidden=false);
}
