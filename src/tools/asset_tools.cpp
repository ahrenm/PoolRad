#include "gbengine/tools/asset_tools.hpp"
#include "gbengine/assets/decoders.hpp"
namespace GBEngine {
int inspect_assets(const IFileSystem& files,std::ostream& out,std::ostream& errors) {
    std::size_t archives=0,records=0,images=0,ecl=0,failures=0;
    for (const auto& name:files.list_assets()) {
        auto ext=std::filesystem::path(name).extension().string();
        if (ext!=".DAX" && ext!=".dax") continue;
        ++archives;
        try {
            const DaxArchive archive(files.read_asset(name));
            records+=archive.entries.size();
            std::size_t pixels=0;
            for (const auto& entry:archive.entries) {
                if (classify_asset(name)==AssetKind::ecl) {
                    inspect_ecl_header(entry.data); ++ecl;
                } else {
                    try { const auto decoded=decode_ega(entry.data,name); images+=decoded.size(); pixels+=decoded.size(); }
                    catch (const DecodeError&) { /* Not every DAX family contains images. */ }
                }
            }
            out<<name<<": "<<archive.entries.size()<<" records, "<<pixels<<" supported image frames\n";
        } catch (const std::exception& ex) { ++failures; errors<<name<<": "<<ex.what()<<'\n'; }
    }
    out<<"TOTAL archives="<<archives<<" records="<<records<<" image_frames="<<images
             <<" ecl_headers="<<ecl<<" failures="<<failures<<'\n';
    return failures ? 1 : 0;
}
}
