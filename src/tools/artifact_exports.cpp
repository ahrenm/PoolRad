#include "gbengine/tools/asset_tools.hpp"
#include "gbengine/assets/decoders.hpp"
namespace GBEngine {
int extract_assets(const IFileSystem& files,IArtifactWriter& writer,const std::filesystem::path& directory,std::ostream& out) {
    std::size_t count=0;
    for(const auto& name:files.list_assets()) {
        const auto extension=std::filesystem::path(name).extension().string();
        if(extension!=".DAX" && extension!=".dax") continue;
        const DaxArchive archive(files.read_asset(name));
        for(const auto& entry:archive.entries) {
            writer.write(directory/(name+"-"+std::to_string(entry.id)+".bin"),entry.data);++count;
        }
    }
    out<<"Extracted "<<count<<" decoded records\n";return 0;
}
void write_snapshot(IArtifactWriter& writer,const std::filesystem::path& path,const Framebuffer& frame) {
    const std::string header="P6\n320 200\n255\n";Bytes bytes(header.begin(),header.end());bytes.reserve(bytes.size()+frame.size()*3);
    for(const auto color:frame) {bytes.push_back(static_cast<std::uint8_t>(color>>16));bytes.push_back(static_cast<std::uint8_t>(color>>8));bytes.push_back(static_cast<std::uint8_t>(color));}
    writer.write(path,bytes);
}
}
