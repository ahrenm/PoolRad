#include "gbengine/platform/native_save_repository.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace GBEngine {
namespace {
char slot_name(char slot) {
    if(slot>='a' && slot<='j') slot=static_cast<char>(slot-'a'+'A');
    if(slot<'A' || slot>'J') throw std::runtime_error("Save slot must be A..J");
    return slot;
}
constexpr std::size_t maximum_save_bytes=16*1024*1024;
Bytes read_save(const std::filesystem::path& path) {
    const auto size=std::filesystem::file_size(path);
    if(!size || size>maximum_save_bytes) throw std::runtime_error("Invalid save file size");
    Bytes data(static_cast<std::size_t>(size));std::ifstream input(path,std::ios::binary);
    input.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size()));
    if(!input || input.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Save file read failed or changed during read");
    return data;
}
}
NativeSaveRepository::NativeSaveRepository(std::filesystem::path root,const std::filesystem::path& source_root)
    :root_(std::filesystem::weakly_canonical(std::filesystem::absolute(root))),source_root_(std::filesystem::canonical(source_root)) {guard(root_);}
void NativeSaveRepository::guard(const std::filesystem::path& path) const {
    const auto target=std::filesystem::weakly_canonical(std::filesystem::absolute(path));
    for(auto parent=target;!parent.empty();) {
        if(std::filesystem::exists(parent) && std::filesystem::equivalent(parent,source_root_))
            throw std::runtime_error("Save storage cannot be inside original game assets");
        const auto next=parent.parent_path();if(next==parent) break;parent=next;
    }
}
std::pair<std::uint64_t,std::filesystem::path> NativeSaveRepository::latest(char slot) const {
    const auto key=slot_name(slot);guard(root_);
    std::pair<std::uint64_t,std::filesystem::path> result{};
    if(!std::filesystem::exists(root_)) return result;
    for(const auto& entry:std::filesystem::directory_iterator(root_)) {
        const auto name=entry.path().filename().string();
        if(name.size()!=26 || name[0]!=key || name[1]!='-' || name.substr(22)!=".gbs") continue;
        std::uint64_t generation{};
        const auto [end,error]=std::from_chars(name.data()+2,name.data()+22,generation);
        if(error!=std::errc{} || end!=name.data()+22 || !generation) continue;
        if(entry.is_symlink() || !entry.is_regular_file()) throw std::runtime_error("Invalid save generation file");
        guard(entry.path());
        if(generation>result.first) result={generation,entry.path()};
    }
    return result;
}
bool NativeSaveRepository::occupied(char slot) const {return latest(slot).first!=0;}
Bytes NativeSaveRepository::load(char slot) const {
    const auto [generation,path]=latest(slot);
    if(!generation) throw std::runtime_error("No port save exists in that slot");
    return read_save(path);
}
void NativeSaveRepository::save(char slot,std::span<const std::uint8_t> data) {
    const auto key=slot_name(slot);
    if(data.empty() || data.size()>maximum_save_bytes) throw std::runtime_error("Invalid save payload size");
    const auto [generation,previous]=latest(key);(void)previous;
    if(generation==std::numeric_limits<std::uint64_t>::max()) throw std::runtime_error("Save generation limit reached");
    std::ostringstream name;name<<key<<'-'<<std::setw(20)<<std::setfill('0')<<generation+1;
    const auto committed=root_/(name.str()+".gbs");
    auto temporary=root_/(name.str()+".pending");
    // Incomplete writes never have the committed extension and never hide the
    // previous generation. Rename publishes a whole file to a new path, avoiding
    // platform-dependent replacement of an existing file on Windows.
    guard(committed);guard(temporary);std::filesystem::create_directories(root_);
    if(std::filesystem::exists(committed)) throw std::runtime_error("Save generation already exists");
    unsigned suffix=0;
    while(std::filesystem::exists(temporary)) {
        if(++suffix==1000) throw std::runtime_error("Too many pending save files");
        temporary=root_/(name.str()+".pending-"+std::to_string(suffix));guard(temporary);
    }
    try {
        std::ofstream output(temporary,std::ios::binary);
        output.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
        output.flush();output.close();
        if(!output) throw std::runtime_error("Save file write failed");
        const auto written=read_save(temporary);
        if(written.size()!=data.size() || !std::equal(written.begin(),written.end(),data.begin()))
            throw std::runtime_error("Save file verification failed");
        std::filesystem::rename(temporary,committed);
    } catch(...) {std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
}
