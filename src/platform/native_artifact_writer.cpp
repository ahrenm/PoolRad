#include "gbengine/platform/native_artifact_writer.hpp"
#include <fstream>
#include <stdexcept>
namespace GBEngine {
NativeArtifactWriter::NativeArtifactWriter(const std::filesystem::path& root):source_root_(std::filesystem::canonical(root)) {}
void NativeArtifactWriter::write(const std::filesystem::path& path,std::span<const std::uint8_t> bytes) {
    const auto target=std::filesystem::weakly_canonical(std::filesystem::absolute(path));
    for(auto parent=target;!parent.empty();) {
        if(std::filesystem::exists(parent) && std::filesystem::equivalent(parent,source_root_))
            throw std::runtime_error("Output cannot be written into source assets");
        const auto next=parent.parent_path();if(next==parent) break;parent=next;
    }
    if(!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path());
    std::ofstream output(target,std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));output.close();
    if(!output) throw std::runtime_error("Artifact write failed: "+target.string());
}
}
