#pragma once
#include "gbengine/platform/hal.hpp"
#include <map>
#include <ostream>
#include <stdexcept>
namespace GBEngine {
class StringError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
std::string sha256(std::span<const std::uint8_t>);
// Authenticate the exact DOS executables before fixed-offset text extraction.
void validate_text_sources(const IFileSystem&,std::ostream* log=nullptr);
std::string pascal_string_id(std::string_view source,std::size_t offset);
// Port snapshots own their character names. Original imports still require
// checked source-character strings from the selected DOS sidecars.
enum class StringSourceScope { full_profile, game_resources };
class StringStore {
    friend class StringExtractor;
    std::map<std::string,std::string> strings_,sources_,records_,record_origins_,runtime_;
    void validate() const;
    void validate_profile() const;
    std::string content_hash() const;
public:
    static StringStore extract(const IFileSystem&,StringSourceScope=StringSourceScope::full_profile);
    static StringStore load(const IFileSystem&,const std::filesystem::path&,StringSourceScope=StringSourceScope::full_profile);
    static StringStore load_or_extract(const IFileSystem&,const std::filesystem::path&,std::ostream&,StringSourceScope=StringSourceScope::full_profile);
    void save(const std::filesystem::path&) const;
    // Explicit extraction/cache validation for a selected original roster.
    // Port resumes never require these source sidecars.
    void prepare_original_roster(const IFileSystem&,std::span<const std::string> stems,const std::filesystem::path& cache,std::ostream&);
    const std::string& get(std::string_view id) const;
    std::string runtime_id(std::string_view);
    const std::string& runtime_text(std::string_view text) {return get(runtime_id(text));}
    std::string record_origin(std::span<const std::uint8_t>) const;
    std::string ecl_id(std::string_view origin,std::uint16_t address,std::span<const std::uint8_t> packed,bool original);
    std::string reference_id(std::string_view origin,std::uint16_t address) const;
    std::size_t size() const {return strings_.size();}
    static StringStore& active();
    static StringStore* bind(StringStore*);
};
class ScopedStringStore {
    StringStore* previous_;
public:
    explicit ScopedStringStore(StringStore& store):previous_(StringStore::bind(&store)) {}
    ~ScopedStringStore() {StringStore::bind(previous_);}
    ScopedStringStore(const ScopedStringStore&)=delete;
    ScopedStringStore& operator=(const ScopedStringStore&)=delete;
};
inline const std::string& overlay_string(std::size_t offset) {return StringStore::active().get(pascal_string_id("GAME.OVR",offset));}
}
