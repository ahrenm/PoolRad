#pragma once
#include "gbengine/platform/hal.hpp"
#include <ostream>
namespace GBEngine {
int inspect_assets(const IFileSystem&,std::ostream& out,std::ostream& errors);
int extract_assets(const IFileSystem&,IArtifactWriter&,const std::filesystem::path&,std::ostream&);
void write_snapshot(IArtifactWriter&,const std::filesystem::path&,const Framebuffer&);
}
