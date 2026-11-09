#include "gbengine/qol/journal.hpp"
#include "gbengine/world/exploration_session.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include <algorithm>
#include <sstream>

namespace GBEngine {
namespace {
std::string caption(JournalId id) {
    if(id.kind==JournalKind::entry)return "ENTRY "+std::to_string(id.number);
    if(id.kind==JournalKind::tale)return "TAVERN TALE "+std::to_string(id.number);
    static constexpr std::array<std::string_view,18> roman{"LIX","LXIV","LXXVIII","CI","CIX","CX","CXIV","CXX","CXXVI","CXXIX","CXXXIV","CLIV","CLVI","CLIX","CXC","CCI","CCIV","CCXIV"};
    const auto index=static_cast<std::size_t>(std::find(proclamation_numbers.begin(),proclamation_numbers.end(),id.number)-proclamation_numbers.begin());
    return "PROCLAMATION "+std::string(roman.at(index));
}
struct FitSize {double width{},height{};};
FitSize fitted_size(const Image& source,double maximum_width) {
    const double scale=std::min(maximum_width/source.width,144.0/source.height);
    return {source.width*scale,source.height*scale};
}
Image fit_image(const Image& source,FitSize size) {
    Image result{};
    result.width=std::max(1,static_cast<int>(size.width));
    result.height=std::max(1,static_cast<int>(size.height));
    result.pixels.reserve(static_cast<std::size_t>(result.width*result.height));
    // Integrate each source pixel's area, including fractional edge coverage.
    // Point sampling aliases the printed scan's fine lines and halftone shading.
    const auto area=static_cast<std::uint64_t>(source.width)*source.height;
    for(int y=0;y<result.height;++y)for(int x=0;x<result.width;++x) {
        const int x0=x*source.width,x1=(x+1)*source.width;
        const int y0=y*source.height,y1=(y+1)*source.height;
        std::array<std::uint64_t,3> sum{};
        for(int sy=y0/result.height;sy<(y1+result.height-1)/result.height;++sy) {
            const int dy=std::min(y1,(sy+1)*result.height)-std::max(y0,sy*result.height);
            for(int sx=x0/result.width;sx<(x1+result.width-1)/result.width;++sx) {
                const int dx=std::min(x1,(sx+1)*result.width)-std::max(x0,sx*result.width);
                const auto weight=static_cast<std::uint64_t>(dx)*dy;
                const auto pixel=source.pixels[static_cast<std::size_t>(sy*source.width+sx)];
                for(unsigned c=0;c<3;++c)sum[c]+=((pixel>>(c*8))&255u)*weight;
            }
        }
        std::uint32_t pixel=0xff000000u;
        for(unsigned c=0;c<3;++c)pixel|=static_cast<std::uint32_t>((sum[c]+area/2)/area)<<(c*8);
        result.pixels.push_back(pixel);
    }
    return result;
}
class JournalMode final:public IGameMode {
    GameFont font_;ScreenChrome chrome_;std::optional<JournalCatalog> catalog_;std::vector<JournalRecord> missing_;std::vector<const JournalRecord*> known_;JournalKind kind_=JournalKind::entry;
    std::size_t selection_=0,figure_=0;bool reading_=false,image_=false,presented_=false,left_=false,zoom_=false;
    std::optional<Image> picture_,fit_picture_;int pan_x_=0,pan_y_=0;std::string error_;
    std::optional<double> entry_fit_width_;FitSize page_fit_;
    static constexpr unsigned rows=17;
    const JournalState& state_;
    void list() {known_.clear();for(const auto& record:catalog_?catalog_->records():missing_)if(record.id.kind==kind_&&state_.contains(record.id))known_.push_back(&record);selection_=0;reading_=image_=false;entry_fit_width_.reset();error_.clear();}
    void figure() {
        picture_.reset();fit_picture_.reset();zoom_=false;pan_x_=pan_y_=0;
        try {
            const auto& images=known_.at(selection_)->images;
            if(!catalog_ || images.empty())throw DecodeError("Missing Journal artwork");
            picture_=catalog_->image(images.at(figure_));
            const double maximum_width=known_[selection_]->id.kind==JournalKind::tale?304.0*0.70:304.0;
            if(figure_==0) {
                page_fit_=fitted_size(*picture_,maximum_width);
                entry_fit_width_=page_fit_.width;
            }else {
                // The entry's first page anchors Fit, even on backward wrap.
                if(!entry_fit_width_)entry_fit_width_=fitted_size(catalog_->image(images.front()),maximum_width).width;
                page_fit_=fitted_size(*picture_,*entry_fit_width_);
            }
            fit_picture_=fit_image(*picture_,page_fit_);error_.clear();
        }catch(const std::exception&){picture_.reset();fit_picture_.reset();error_="ENTRY MISSING\nSEE PR_SETUP.LOG FOR SETUP DETAILS";}
    }
    void paint(Framebuffer& frame,std::string_view text,int y,unsigned colour=10)const {font_.draw_text(frame,text,8,y,38,1,ega_ui_palette[colour]);}
    void key(Framebuffer& frame,std::string_view label,int x,int y)const {
        font_.draw_text(frame,label,x,y,static_cast<int>(label.size()),1,ega_ui_palette[10]);font_.draw_text(frame,label.substr(0,1),x,y,1,1,ega_ui_palette[15]);
    }
public:
    JournalMode(ExplorationSession& session,const GameFont& font,const std::filesystem::path& root)
        :font_(font),chrome_(ScreenChrome::load(session.asset_files())),state_(session.journal().state){
        session.prepare_journal();
        try{catalog_.emplace(std::make_shared<NativeFiles>(root));}
        catch(const std::exception&) {
            for(unsigned n=1;n<=58;++n)missing_.push_back({{JournalKind::entry,n},{}});
            for(auto n:proclamation_numbers)missing_.push_back({{JournalKind::proclamation,n},{}});
            for(unsigned n=1;n<=23;++n)missing_.push_back({{JournalKind::tale,n},{}});
        }
        list();
    }
    void handle_input(InputEvent event,IGameFlow& flow)override {
        if(!event.pressed||!presented_||left_)return;
        auto ch=event.character;if(ch>='a'&&ch<='z')ch=static_cast<std::uint8_t>(ch-32);
        if(event.key==Key::cancel||event.key==Key::backspace||ch=='X') {
            if(reading_){reading_=image_=false;picture_.reset();fit_picture_.reset();entry_fit_width_.reset();error_.clear();}else{left_=true;flow.pop_mode();}
            presented_=false;
            return;
        }
        if(!reading_) {
            if(ch=='E'){kind_=JournalKind::entry;list();}else if(ch=='P'){kind_=JournalKind::proclamation;list();}else if(ch=='T'){kind_=JournalKind::tale;list();}
            else if(!known_.empty()) {
                if(event.key==Key::up)selection_=(selection_+known_.size()-1)%known_.size();
                else if(event.key==Key::down)selection_=(selection_+1)%known_.size();
                else if(event.key==Key::home)selection_=0;else if(event.key==Key::end)selection_=known_.size()-1;
                else if(event.key==Key::next)selection_=std::min(selection_+rows,known_.size()-1);else if(event.key==Key::previous)selection_=selection_>rows?selection_-rows:0;
                else if(event.key==Key::accept){figure_=0;entry_fit_width_.reset();reading_=image_=true;figure();}
            }
        }else if(image_) {
            if(ch=='Z')zoom_=!zoom_;
            const bool next=ch=='N'||event.key==Key::next||event.key==Key::accept||(!zoom_&&event.key==Key::right);
            const bool previous=ch=='P'||event.key==Key::previous||(!zoom_&&event.key==Key::left);
            if(next||previous){const auto size=known_[selection_]->images.size();if(size){figure_=(figure_+size+(next?1:size-1))%size;figure();}}
            if(picture_ && zoom_) {
                if(event.key==Key::left)pan_x_-=24;else if(event.key==Key::right)pan_x_+=24;
                else if(event.key==Key::up)pan_y_-=24;else if(event.key==Key::down)pan_y_+=24;
                pan_x_=std::clamp(pan_x_,0,std::max(0,picture_->width-304));pan_y_=std::clamp(pan_y_,0,std::max(0,picture_->height-144));
            }
        }
        presented_=false;
    }
    void update(std::uint64_t,IGameFlow&)override {}
    void render(Framebuffer& frame)const override {
        frame.fill(ega_ui_palette[0]);
        chrome_.draw(frame,ChromeLayout::character_creation());
        paint(frame,reading_?caption(known_[selection_]->id):kind_==JournalKind::entry?"JOURNAL / ENTRIES":kind_==JournalKind::tale?"JOURNAL / TALES":"JOURNAL / PROCLAMATIONS",8,13);
        if(!reading_) {
            key(frame,"Entries",8,24);key(frame,"Proclamations",88,24);key(frame,"Tales",208,24);
            const auto first=(selection_/rows)*rows;
            for(std::size_t i=first;i<known_.size()&&i<first+rows;++i){paint(frame,(i==selection_?"> ":"  ")+caption(known_[i]->id),40+8*static_cast<int>(i-first),i==selection_?15:10);}
            if(known_.empty())paint(frame,"NO ENTRIES RECORDED HERE YET",56);
            paint(frame,"ARROWS SELECT / ENTER READ",192); // guidance has no action-key decoration
            key(frame,"X Exit",256,192);
        }else if(image_) {
            if(picture_) {
                const auto& p=zoom_?*picture_:*fit_picture_;
                const int w=std::min(304,p.width),h=std::min(144,p.height);
                const int left=8+(304-w)/2,top=24+(144-h)/2;
                for(int y=0;y<h;++y)for(int x=0;x<w;++x){const int sx=zoom_?pan_x_+x:x,sy=zoom_?pan_y_+y:y;frame[static_cast<std::size_t>((top+y)*320+left+x)]=p.pixels[static_cast<std::size_t>(sy*p.width+sx)];}
            }
            if(picture_)paint(frame,"PAGE "+std::to_string(figure_+1)+" / "+std::to_string(known_[selection_]->images.size())+(zoom_?"  ARROWS PAN":"  FIT"),172);
            key(frame,"Zoom",0,192);key(frame,"Next",48,192);key(frame,"Prev",96,192);key(frame,"X Back",144,192);
        }
        if(!error_.empty()){for(int y=144;y<180;++y)std::fill_n(frame.begin()+y*320+8,304,ega_ui_palette[0]);font_.draw_text(frame,error_,8,144,38,4,ega_ui_palette[12],true);}
    }
    std::optional<ImagePresentation> image_presentation()const override {
        if(!image_||zoom_||!picture_||!error_.empty())return {};
        const auto& p=*picture_;
        const float w=static_cast<float>(page_fit_.width),h=static_cast<float>(page_fit_.height);
        return ImagePresentation{p.width,p.height,p.pixels,8+(304-w)/2,24+(144-h)/2,w,h};
    }
    void after_present(IGameFlow&)override {presented_=true;}
    void on_suspend()override {presented_=false;}
    void on_resume()override {presented_=false;}
    std::string title()const override {return "PoolRad | Journal | ESC returns";}
};
}
std::unique_ptr<IGameMode> make_journal_mode(ExplorationSession& session,const GameFont& font,const std::filesystem::path& root) {
    if(!session.can_act())throw DecodeError("Journal requires idle exploration");
    return std::make_unique<JournalMode>(session,font,root);
}
}
