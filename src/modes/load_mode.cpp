#include "gbengine/ui/menu_hotkeys.hpp"
#include "gbengine/modes/modes.hpp"
#include "gbengine/rendering/screen_chrome.hpp"
#include "gbengine/assets/records.hpp"
#include "gbengine/saves/port_save.hpp"
#include "gbengine/saves/save_repository.hpp"
#include <algorithm>

namespace GBEngine {
namespace {
class LoadMode final:public IGameMode {
    struct Slot {char letter;std::string state,zone="Unreadable save",time="---:--:--",detail;bool available=false;};
    const IFileSystem& files_;
    LaunchOptions options_;
    std::ostream& output_;
    ISaveRepository* saves_;
    ISaveRepository* characters_;
    std::function<void()> cancel_;
    ScreenChrome chrome_;
    GameFont font_;
    std::vector<Slot> slots_;
    std::size_t row_=0,scanned_=0;
    bool pending_=false,presented_=false,browser_presented_=false;
    std::string notice_;
    const Slot* selected() const {
        return slots_.empty()?nullptr:&slots_[row_];
    }
    void discover_one() {
        const char letter=static_cast<char>('A'+scanned_++);
        Slot slot{letter,"INVALID","Unreadable save","---:--:--",{},false};
        std::uint32_t schema=0;
        try {
            if(!saves_ || !saves_->occupied(letter))return;
            const auto bytes=saves_->load(letter);
            if(bytes.size()>=12)schema=read_u32(bytes,8);
            const auto summary=PortSave::inspect(bytes);slot.state="READY";
            slot.zone=summary.zone_title();slot.time=summary.elapsed_time();slot.available=true;
            if(slot.time.size()>9)slot.detail="Elapsed game time: "+slot.time;
        } catch(const std::exception& error) {
            slot.detail=error.what();
            if(slot.detail.find("Unsupported port save version")!=std::string::npos)
                slot.state=schema<PortSave::format_version?"OBSOLETE":"FUTURE";
        }
        slots_.push_back(std::move(slot));
    }
public:
    LoadMode(const IFileSystem& files,LaunchOptions options,std::ostream& out,ISaveRepository* saves,std::function<void()> cancel,ISaveRepository* characters)
        :files_(files),options_(std::move(options)),output_(out),saves_(saves),characters_(characters),cancel_(std::move(cancel)),
         chrome_(ScreenChrome::load(files)),font_(GameFont::load(files)) {}
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed || pending_)return;
        auto key=event.character;if(key>='a' && key<='z')key=static_cast<std::uint8_t>(key-'a'+'A');
        if(event.key==Key::cancel || event.key==Key::backspace) {
            if(cancel_)cancel_();else flow.quit();return;
        }
        auto& row=row_;const auto& slots=slots_;
        if(slots.empty())return;
        if(event.key==Key::up)row=(row+slots.size()-1)%slots.size();
        else if(event.key==Key::down)row=(row+1)%slots.size();
        else if(event.key==Key::home)row=0;
        else if(event.key==Key::end)row=slots.size()-1;
        else if(key>='A' && key<='J') {
            const auto found=std::find_if(slots.begin(),slots.end(),[&](const auto& slot){return slot.letter==key;});
            if(found!=slots.end())row=static_cast<std::size_t>(found-slots.begin());
        } else if(event.key==Key::accept) {
            const auto& slot=slots[row];
            if(!slot.available){notice_=slot.detail;return;}
            pending_=true;presented_=false;notice_="Loading selected campaign...";return;
        }
        notice_.clear();
    }
    void update(std::uint64_t,IGameFlow& flow) override {
        if(pending_) {
            if(!presented_)return;
            try {
                auto next=options_;next.load_menu=false;next.save_slot.clear();next.resume_slot.clear();
                next.party_fixture.clear();next.scenario_fixture.clear();next.mode=RunMode::exploration;
                next.resume_slot=std::string(1,selected()->letter);
                // Fresh full validation stages the replacement before publishing.
                // Preview metadata never authorizes a campaign replacement.
                auto mode=make_exploration_mode(files_,next,output_,saves_,characters_);
                flow.change_mode(std::move(mode));
            } catch(const std::exception& error){notice_=error.what();pending_=false;presented_=false;}
            return;
        }
        if(!browser_presented_ || scanned_==10)return;
        // First paint precedes I/O; bounded discovery keeps cancellation/input
        // responsive and never constructs sessions or hashes game resources.
        for(unsigned i=0;i<2 && scanned_<10;++i)discover_one();
    }
    void render(Framebuffer& frame) const override {
        frame.fill(ega_ui_palette[0]);chrome_.draw(frame,ChromeLayout::merchant());
        font_.draw_text(frame,"LOAD CAMPAIGN",104,8,26,1,ega_ui_palette[13]);
        font_.draw_text(frame,"SAVED GAMES",8,24,38,1,ega_ui_palette[15]);
        font_.draw_text(frame,"LOCATION",24,40,26,1,ega_ui_palette[15]);
        font_.draw_text(frame,"ELAPSED",240,40,9,1,ega_ui_palette[15]);
        const auto& slots=slots_;
        for(std::size_t i=0;i<slots.size();++i) {
            const auto y=56+static_cast<int>(i)*8;
            const auto colour=ega_ui_palette[i==row_?15:10];
            font_.draw_text(frame,std::string(1,slots[i].letter),8,y,1,1,menu_action_color);
            font_.draw_text(frame,slots[i].zone,24,y,26,1,colour);
            font_.draw_text(frame,slots[i].time.size()>9?">999 DAYS":slots[i].time,240,y,9,1,colour);
        }
        if(slots.empty() && scanned_==10)
            font_.draw_text(frame,"No saved games.",8,56,38,1,ega_ui_palette[10]);
        const auto* slot=selected();
        const auto detail=notice_.empty()?(slot?slot->detail:std::string{}):notice_;
        font_.draw_text(frame,detail,8,144,38,4,ega_ui_palette[14]);
        if(scanned_<10 && !pending_)
            font_.draw_text(frame,"Reading saved games...",8,176,38,1,ega_ui_palette[10]);
        font_.draw_text(frame,"ARROWS SELECT  ENTER LOAD  ESC EXIT",0,192,40,1,ega_ui_palette[10]);
    }
    void after_present(IGameFlow&) override {browser_presented_=true;if(pending_)presented_=true;}
    std::string title() const override {
        const auto* slot=selected();
        return "PoolRad | load | port "+
            (slot?std::string(1,slot->letter)+" | "+slot->state:scanned_<10?"SCANNING":"EMPTY")+
            (pending_?" | loading":notice_.empty()?"":" | "+notice_);
    }
};
}
std::unique_ptr<IGameMode> make_load_mode(const IFileSystem& files,const LaunchOptions& options,std::ostream& out,ISaveRepository* saves,std::function<void()> cancel,ISaveRepository* characters) {
    return std::make_unique<LoadMode>(files,options,out,saves,std::move(cancel),characters);
}
}
