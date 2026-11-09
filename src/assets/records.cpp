#include "gbengine/assets/records.hpp"
#include <algorithm>
namespace GBEngine {
std::optional<Bytes> find_record(const IFileSystem& f,std::string_view file,std::uint8_t id) {
    const DaxArchive a(f.read_asset(file));
    const auto it=std::find_if(a.entries.begin(),a.entries.end(),[id](const auto& e){return e.id==id;});
    if(it==a.entries.end()) return std::nullopt;
    return it->data;
}
Bytes load_record(const IFileSystem& f,std::string_view file,std::uint8_t id) {
    auto record=find_record(f,file,id);
    if(!record) throw DecodeError("Missing record "+std::to_string(id)+" in "+std::string(file));
    return std::move(*record);
}
}
