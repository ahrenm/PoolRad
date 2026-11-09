#include "gbengine/rendering/ending_presentation.hpp"
#include "gbengine/assets/records.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/rendering/screen_chrome.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
void blit(Framebuffer& target,const Image& image,int left,int top) {
    // FINAL composes a 120x120 scene; place its origin inside the chrome.
    left+=8+image.x;top+=8+image.y;
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
        const int px=left+x,py=top+y;
        if(px>=0&&px<320&&py>=0&&py<200)
            target[static_cast<std::size_t>(py*320+px)]=image.pixels[static_cast<std::size_t>(y*image.width+x)];
    }
}
}
EndingPresentation::EndingPresentation(const IFileSystem& files,std::size_t count,unsigned speed)
    :party_count_(count),speed_(speed) {
    if(!count || count>8 || speed>9)throw DecodeError("Invalid native ending party/speed profile");
    for(unsigned id=1;id<=8;++id) {
        const auto raw=load_record(files,"FINAL5.DAX",static_cast<std::uint8_t>(id));
        auto& record=records_[id];record.images=decode_ega(raw,"FINAL5.DAX");
        if(id==2 || id==7 || id==8) {
            const unsigned expected=id==2?4:6;
            if(raw.empty() || raw[0]!=expected || record.images.size()!=expected)
                throw DecodeError("Unsupported FINAL5 animation profile");
            std::size_t at=1;
            for(unsigned f=0;f<expected;++f) {
                if(at>raw.size() || raw.size()-at<21)throw DecodeError("Truncated FINAL5 timing header");
                const auto duration=static_cast<std::uint32_t>(read_u16(raw,at)) |
                    static_cast<std::uint32_t>(read_u16(raw,at+2))<<16;
                record.times.push_back(duration);
                const auto size=static_cast<std::size_t>(read_u16(raw,at+4))*read_u16(raw,at+6)*4;
                if(size>raw.size()-at-21)throw DecodeError("Truncated FINAL5 animation pixels");
                at+=21+size;
            }
            if(at!=raw.size())throw DecodeError("Trailing FINAL5 animation bytes");
        }else if(record.images.size()!=1)throw DecodeError("Unsupported FINAL5 still profile");
    }
}
std::vector<EndingPresentation::Step> EndingPresentation::steps() const {
    std::vector<Step> result;
    const auto cycles=((10-speed_)*5)>>1;
    for(unsigned scene=0;scene<3;++scene) {
        const unsigned id=scene==0?2:scene==1?7:8;
        for(unsigned loop=0;loop<(scene==2?1:cycles);++loop)
            for(unsigned f=0;f<records_[id].images.size();++f)
                // 1709:0C3F counts hundredths; strict native > adds one tick.
                result.push_back({scene,f,(static_cast<std::uint64_t>(records_[id].times[f])*(speed_+3)+1)*10});
    }
    // Three 1709:0DBE waits: speed*225 milliseconds apiece.
    // Keep the last caption readable without slowing its animation frames.
    constexpr std::uint64_t minimum_caption_ms=8000;
    std::uint64_t animation_ms=0;
    for(const auto& step:result)if(step.scene==2)animation_ms+=step.milliseconds;
    const auto reading_pause=animation_ms<minimum_caption_ms?minimum_caption_ms-animation_ms:0;
    result.push_back({3,static_cast<unsigned>(records_[8].images.size()-1),
        std::max<std::uint64_t>(speed_*675u,reading_pause)});
    return result;
}
void EndingPresentation::draw(Framebuffer& frame,const GameFont& font,Step step) const {
    blit(frame,records_[step.scene==0?1:3].images[0],0,0);
    if(step.scene) {
        if(party_count_>1)blit(frame,records_[4].images[0],0,88);
        if(party_count_>2)blit(frame,records_[5].images[0],104,72);
        if(party_count_>3)blit(frame,records_[6].images[0],0,72);
    }
    const unsigned record=step.scene==0?2:step.scene==1?7:8;
    // Native 1464:0D71 takes vertical units before horizontal units.
    // Helper arguments5,3 become top32,left16 after subtracting one.
    blit(frame,records_[record].images.at(step.frame),16,step.scene==0?32:0);
    for(int y=136;y<184;++y)std::fill(frame.begin()+y*320+8,frame.begin()+y*320+312,ega_ui_palette[0]);
    constexpr std::array<unsigned,3> first{0x207b1,0x20818,0x208d6};
    constexpr std::array<unsigned,3> counts{3,6,4};
    constexpr std::array<unsigned,13> strings{0x207b1,0x207d5,0x207fa,0x20818,0x2083c,0x20861,0x20888,0x208a8,0x208cd,0x208d6,0x208fd,0x20925,0x20938};
    const unsigned scene=std::min(step.scene,2u);
    const auto begin=std::find(strings.begin(),strings.end(),first[scene]);
    for(unsigned row=0;row<counts[scene];++row)
        font.draw_text(frame,overlay_string(*(begin+row)),8,136+static_cast<int>(row)*8,38,1,ega_ui_palette[10]);
}
}
