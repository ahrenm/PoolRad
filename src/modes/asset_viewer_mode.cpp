#include "gbengine/modes/modes.hpp"
#include "gbengine/assets/decoders.hpp"
#include <sstream>
namespace GBEngine {
namespace {
class AssetViewerMode final : public IGameMode {
    struct View {int id;std::vector<Image> frames;};
    std::vector<View> views_;
    std::string archive_;
    std::size_t record_=0,frame_=0;
public:
    AssetViewerMode(const IFileSystem& files,std::string_view name,std::ostream& errors):archive_(name) {
        const DaxArchive archive(files.read_asset(name));
        for(const auto& entry:archive.entries) {
            try {views_.push_back({entry.id,decode_ega(entry.data,name)});}
            catch(const DecodeError& error) {errors<<"Record "<<int(entry.id)<<": "<<error.what()<<'\n';}
        }
        if(views_.empty()) throw DecodeError("Archive contains no supported EGA image records");
    }
    void handle_input(InputEvent event,IGameFlow&) override {
        if(!event.pressed) return;
        switch(event.key) {
        case Key::right:case Key::next:record_=(record_+1)%views_.size();frame_=0;break;
        case Key::left:case Key::previous:record_=(record_+views_.size()-1)%views_.size();frame_=0;break;
        case Key::up:frame_=(frame_+1)%views_[record_].frames.size();break;
        case Key::down:frame_=(frame_+views_[record_].frames.size()-1)%views_[record_].frames.size();break;
        default:break;
        }
    }
    void update(std::uint64_t,IGameFlow&) override {}
    void render(Framebuffer& buffer) const override {
        const auto& views=views_;const auto record=record_,frame=frame_;
            const auto& image=views[record].frames[frame];
            // Viewer policy: full-screen records fit the reference canvas; raw origins
            // remain on Image for later verification against the original renderer.
            const int origin_x=image.width==320 ? 0 : image.x;
            const int origin_y=image.height==200 ? 0 : image.y;
            for (int y=0;y<image.height;++y) for (int x=0;x<image.width;++x) {
                const auto color=image.pixels[static_cast<std::size_t>(y*image.width+x)];
                if ((color>>24) && y+origin_y<200 && x+origin_x<320)
                    buffer[static_cast<std::size_t>((y+origin_y)*320+x+origin_x)]=color;
            }

    }
    std::string title() const override {
        std::ostringstream title;
        title<<"PoolRad asset viewer | "<<archive_<<" | ID "<<views_[record_].id<<" | frame "<<frame_+1
             <<"/"<<views_[record_].frames.size()<<" | F5 scale | arrows browse";
        return title.str();
    }
};
}
std::unique_ptr<IGameMode> make_asset_viewer_mode(const IFileSystem& files,std::string_view archive,std::ostream& errors) {
    return std::make_unique<AssetViewerMode>(files,archive,errors);
}
}
