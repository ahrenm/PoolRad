#include "gbengine/modes/modes.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/saves/port_save.hpp"
#include "gbengine/saves/save_repository.hpp"
#include "gbengine/modes/effect_display.hpp"
#include "gbengine/modes/camp_recovery.hpp"
#include "gbengine/modes/combat_icon.hpp"
#include "gbengine/app/runtime_timing.hpp"
#include <algorithm>
#include "gbengine/qol/fix.hpp"
#include <sstream>
namespace GBEngine {
namespace {
const std::string& text(std::size_t offset) {return StringStore::active().get(pascal_string_id("START.EXE:module",offset));}
std::vector<std::string> words(std::string_view value) {
    std::istringstream input{std::string(value)};std::vector<std::string> result;std::string word;
    while(input>>word) result.push_back(word);
    return result;
}
class EncampMode final:public IGameMode {
    enum class Page {camp,magic,alter,pictures,speed,order_select,order_place,drop_query,drop_feedback,slots,writing,save_exit,quit_query,error,leaving};
    ExplorationSession& session_;
    const ExplorationScreen& screen_;
    ISaveRepository* saves_;
    bool qol_upgrades_;
    PicturePresentation art_;
    Page page_=Page::camp;
    std::size_t focus_=0;
    char slot_='A';
    bool art_presented_=false,write_presented_=false,save_left_camp_=false,save_exit_presented_=false;
    bool drop_presented_=false,drop_confirmed_=false;
    std::uint64_t drop_elapsed_=0;
    std::string drop_name_;
    bool drop_active_=false;
    std::string notice_;
    void quit_query() {page_=Page::quit_query;focus_=1;}
    bool ordering() const {return page_==Page::order_select || page_==Page::order_place;}
    std::vector<std::string> labels() const {
        if(page_==Page::speed) {
            std::vector<std::string> result;
            if(session_.game_speed())result.push_back(words(overlay_string(0x19286)).front());
            if(session_.game_speed()<9)result.push_back(words(overlay_string(0x1928e)).front());
            result.push_back(words(overlay_string(0x19296)).front());return result;
        }
        if(page_==Page::slots) return words(overlay_string(0x1fcbd));
        if(page_==Page::quit_query || page_==Page::drop_query || page_==Page::save_exit) return words(overlay_string(0x2d979));
        if(ordering()) return words(text(page_==Page::order_select?0xcd44:0xcd6d));
        if(page_==Page::alter) return words(text(0xcd1a));
        if(page_==Page::pictures) return {overlay_string(session_.monster_pictures_enabled()?0x19449:0x19457),
            overlay_string(session_.character_portraits_enabled()?0x19466:0x19475),overlay_string(0x19485)};
        auto result=words(text(page_==Page::magic?0xccf1:0xccc8));
        if(page_==Page::camp && qol_upgrades_)result.insert(result.end()-1,"Fix");
        return result;
    }
    std::string prefix() const {
        if(page_==Page::speed)return overlay_string(0x1929c)+" ";
        if(page_==Page::slots) return overlay_string(0x1fcab);
        if(page_==Page::quit_query) return overlay_string(0x19693);
        if(page_==Page::drop_query) return overlay_string(session_.world.party.size()==1?0x190a8:0x190c3);
        if(ordering()) return overlay_string(0x18f71);
        if(page_==Page::alter) return overlay_string(0x19441);
        if(page_==Page::pictures) return {};
        if(page_==Page::camp && qol_upgrades_)return "Camp ";
        return page_==Page::magic?std::string{}:overlay_string(0x1968c);
    }
    void commands(Framebuffer& frame) const {
        const auto& font=screen_.font();const auto prompt=prefix();
        font.draw_text(frame,prompt,0,192,40,1,ega_ui_palette[page_==Page::drop_query?14:13]);
        int x=static_cast<int>(prompt.size())*8;const auto options=labels();
        for(std::size_t i=0;i<options.size();++i) {
            const auto& label=options[i];
            font.draw_text(frame,label,x,192,(320-x)/8,1,ega_ui_palette[i==focus_?15:10]);
            if(i!=focus_) for(std::size_t j=0;j<label.size();++j) {
                const auto c=label[j];
                if((c>='A' && c<='Z') || (c>='0' && c<='9'))
                    font.draw_text(frame,std::string_view(label).substr(j,1),x+8*static_cast<int>(j),192,1,1,ega_ui_palette[15]);
            }
            x+=static_cast<int>(label.size()+1)*8;
        }
    }
    void activate(std::uint8_t key,IGameFlow& flow) {
        if(page_==Page::speed) {
            const auto value=session_.game_speed();
            if(key=='E'){page_=Page::alter;focus_=2;}
            else if(key=='F' && value) {session_.set_game_speed(static_cast<std::uint8_t>(value-1));focus_=0;}
            else if(key=='S' && value<9) {session_.set_game_speed(static_cast<std::uint8_t>(value+1));focus_=0;}
            return;
        }
        if(page_==Page::slots) {
            if(key<'A' || key>'J') return;
            slot_=static_cast<char>(key);focus_=key-'A';page_=Page::writing;write_presented_=false;notice_.clear();return;
        }
        if(page_==Page::save_exit) {
            if(!save_exit_presented_)return;
            if(key=='Y') {page_=Page::slots;focus_=0;notice_="Saving will leave the inn camp.";}
            else if(key=='N') {page_=Page::camp;focus_=0;notice_.clear();}
            return;
        }
        if(page_==Page::quit_query) {
            if(key=='Y') {page_=Page::leaving;flow.quit();}
            else if(key=='N') {page_=Page::camp;focus_=0;}
            return;
        }
        if(page_==Page::drop_query) {
            if(key!='Y' && key!='N') return;
            if(session_.world.party.size()==1) {
                if(key=='Y') {
                    const auto result=session_.drop_camp_member(true);
                    if(result.status==RosterStatus::quit_required) {page_=Page::leaving;flow.quit();return;}
                    notice_=result.message;
                } else {session_.cancel_camp_drop();notice_.clear();}
                session_.cancel_camp_drop();page_=Page::alter;focus_=1;return;
            }
            drop_confirmed_=key=='Y';drop_presented_=false;drop_elapsed_=0;
            notice_=drop_name_+" "+overlay_string(drop_confirmed_?(drop_active_?0x190d5:0x190e7):0x190fc);
            page_=Page::drop_feedback;return;
        }
        if(ordering() && key!='E' && key!='S' && key!='P' && key!=13) return;
        notice_.clear();
        if(ordering()) {
            if(key=='E') {page_=Page::alter;focus_=0;}
            else if(key=='S' || key=='P' || key==13) {
                page_=page_==Page::order_select?Page::order_place:Page::order_select;focus_=0;
                if(page_==Page::order_place && session_.active_character())
                    notice_=session_.world.party.member(*session_.active_character()).name()+" "+overlay_string(0x18f9f);
            }
            return;
        }
        if(key=='E') {
            if(page_==Page::magic) {page_=Page::camp;focus_=2;}
            else if(page_==Page::pictures) {page_=Page::alter;focus_=4;}
            else if(page_==Page::alter) {page_=Page::camp;focus_=4;}
            else if(session_.end_camp()) {page_=Page::leaving;flow.pop_mode();}
            else notice_="Camp work must finish or stop before leaving.";
        } else if(key=='A' && page_==Page::camp) {page_=Page::alter;focus_=0;}
        else if(key=='O' && page_==Page::alter) {page_=Page::order_select;focus_=0;}
        else if(key=='D' && page_==Page::alter) {
            if(session_.begin_camp_drop()) {
                const auto& member=session_.world.party.member(*session_.active_character());
                drop_name_=member.name();drop_active_=member.active();
                notice_=session_.world.party.size()==1?std::string{}:drop_name_+" "+overlay_string(0x190b6);
                page_=Page::drop_query;focus_=1;
            } else notice_="Party drop requires an idle restored camp.";
        }
        else if(key=='S' && page_==Page::alter) {page_=Page::speed;focus_=0;}
        else if(key=='I' && page_==Page::alter) {
            try {flow.push_mode(make_combat_icon_mode(session_,screen_));}
            catch(const DecodeError& error) {notice_=error.what();}
        }
        else if(key=='P' && page_==Page::alter) {page_=Page::pictures;focus_=0;}
        else if(key=='P' && page_==Page::pictures) {
            if(!session_.set_character_portraits_enabled(!session_.character_portraits_enabled()))
                notice_="Picture preferences require an idle restored campaign.";
        } else if(key=='M' && page_==Page::pictures) {
            if(!session_.set_monster_pictures_enabled(!session_.monster_pictures_enabled()))
                notice_="Picture preferences require an idle restored campaign.";
        } else if(key=='V' && page_==Page::camp) {
            if(const auto selected=session_.active_character()) flow.push_mode(make_character_view_mode(session_.world.party,*selected,screen_.font(),session_.asset_files(),CharacterViewServices::for_exploration(session_,session_.character_portraits_enabled())));
            else notice_="Load a party to view a character.";
        } else if(key=='S' && page_==Page::camp) {page_=session_.nested_camp_active()?Page::save_exit:Page::slots;focus_=0;if(page_==Page::save_exit)save_exit_presented_=false;if(page_==Page::save_exit)notice_="Leave the inn camp and save? Y/N";}
        else if(key=='F' && page_==Page::camp && qol_upgrades_) {
            try {flow.push_mode(make_camp_fix_mode(session_,screen_,&art_));}
            catch(const DecodeError& error){notice_=error.what();}
        }
        else if(key=='M' && page_==Page::camp) {page_=Page::magic;focus_=0;}
        else if(key=='R' && (page_==Page::camp || page_==Page::magic)) {
            try {flow.push_mode(make_camp_rest_mode(session_,screen_,&art_));}
            catch(const DecodeError& error) {notice_=error.what();}
        } else if((key=='M' || key=='S') && page_==Page::magic) {
            try {flow.push_mode(make_camp_work_mode(session_,screen_,&art_,key=='M'?CampWorkKind::memorize:CampWorkKind::scribe));}
            catch(const DecodeError& error) {notice_=error.what();}
        }
        else if(key=='C' && page_==Page::magic) {
            try {flow.push_mode(make_spell_mode(session_,screen_,&art_));}
            catch(const DecodeError& error) {notice_=error.what();}
        } else if(key=='D' && page_==Page::magic) {
            try {flow.push_mode(make_effect_display_mode(session_.world.party,session_.spell_catalog(),screen_.font(),screen_.chrome()));}
            catch(const DecodeError& error) {notice_=error.what();}
        } else {
            const auto options=labels();
            if(std::any_of(options.begin(),options.end(),[&](const auto& label){return !label.empty() && label[0]==key;}))
                notice_="This service is not implemented.";
        }
    }
public:
    EncampMode(ExplorationSession& session,const ExplorationScreen& screen,const IFileSystem& files,ISaveRepository* saves,bool qol_upgrades)
        :session_(session),screen_(screen),saves_(saves),qol_upgrades_(qol_upgrades),art_(PicturePresentation::load_ambient_loop(files,session.disk(),29)) {
        if(!session_.can_act() && !session_.nested_camp_ready()) throw DecodeError("Camp requires an idle exploration session");
        if(!session_.begin_camp())throw DecodeError("Camp recovery context is unavailable");
    }
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed || page_==Page::leaving || page_==Page::writing || page_==Page::drop_feedback) return;
        if(page_==Page::error) {
            if(event.key==Key::accept || event.key==Key::cancel || event.key==Key::backspace) {notice_.clear();if(save_left_camp_) {page_=Page::leaving;flow.pop_mode();}else quit_query();}
            return;
        }
        if(event.key==Key::cancel || event.key==Key::backspace) {
            if(page_==Page::slots) {if(session_.nested_camp_active()) {page_=Page::camp;focus_=0;notice_.clear();}else quit_query();}
            else if(page_==Page::save_exit) {page_=Page::camp;notice_.clear();}
            else if(page_!=Page::quit_query && page_!=Page::drop_query) activate('E',flow);
            return; // Native quit query reprompts on Escape.
        }
        auto key=event.character;if(key>='a' && key<='z') key=static_cast<std::uint8_t>(key-'a'+'A');
        if(page_==Page::drop_query) {activate(key,flow);return;}
        if(ordering()) {
            if(event.key==Key::home || event.key==Key::end || event.key==Key::up || event.key==Key::down) {
                const bool earlier=event.key==Key::home || event.key==Key::up;
                if(page_==Page::order_select) session_.cycle_character(earlier?-1:1);
                else {
                    const auto result=session_.move_camp_member(earlier?RosterDirection::earlier:RosterDirection::later);
                    if(!result.applied()) notice_=result.message;
                }
            } else if(event.key==Key::accept) activate(13,flow);
            else activate(key,flow);
            return;
        }
        if((page_==Page::camp || page_==Page::magic || page_==Page::alter) && (event.key==Key::home || event.key==Key::end)) {
            session_.cycle_character(event.key==Key::home?-1:1);return;
        }
        const auto count=labels().size();
        if(key==',' || event.key==Key::left || event.key==Key::up) focus_=(focus_+count-1)%count;
        else if(key=='.' || event.key==Key::right || event.key==Key::down) focus_=(focus_+1)%count;
        else if(event.key==Key::accept) activate(static_cast<std::uint8_t>(labels().at(focus_)[0]),flow);
        else activate(key,flow);
    }
    void update(std::uint64_t elapsed,IGameFlow& flow) override {
        if(page_!=Page::leaving && session_.camp_interrupted()) {
            if(session_.end_camp()){page_=Page::leaving;flow.pop_mode();}
            return;
        }
        if(art_presented_) art_.advance(elapsed);
        if(page_==Page::drop_feedback) {
            if(!drop_presented_) return;
            // A farewell is visible before its roster transaction, and UI frames
            // never become campaign time. Ignore input until the timed notice ends.
            drop_elapsed_+=std::min<std::uint64_t>(elapsed,RuntimeTiming::notification_dwell_ms);
            if(drop_elapsed_<RuntimeTiming::notification_dwell_ms) return;
            const auto result=session_.drop_camp_member(drop_confirmed_);
            if(drop_confirmed_ && !result.applied()) notice_=result.message;
            else notice_.clear();
            session_.cancel_camp_drop();page_=Page::alter;focus_=1;return;
        }
        if(page_!=Page::writing || !write_presented_) return;
        write_presented_=false;
        try {
            if(!saves_) throw DecodeError("Port save storage is not available");
            if(session_.nested_camp_active()) {
                if(!session_.end_camp())throw DecodeError("Camp work must finish before saving outside the inn.");
                save_left_camp_=true;
                session_.resume_after_camp();
            }
            const auto bytes=PortSave::capture(session_).encode();
            saves_->save(slot_,bytes);
            if(save_left_camp_) {page_=Page::leaving;flow.pop_mode();}else quit_query();
        } catch(const std::exception& error) {notice_=error.what();page_=Page::error;}
    }
    void render(Framebuffer& frame) const override {
        ExplorationUiState ui;ui.picture_override=&art_;ui.command_line=std::string{};
        screen_.draw(frame,session_,ui);
        for(int y=136;y<184;++y) std::fill_n(frame.begin()+y*320+8,304,ega_ui_palette[0]);
        const auto message=page_==Page::speed?overlay_string(0x19261)+std::to_string(session_.game_speed())+overlay_string(0x1926f):
            page_==Page::writing?overlay_string(0x1fd11):
            notice_.empty()?(ordering()?std::string{}:overlay_string(0x19654)):notice_;
        screen_.font().draw_text(frame,message,8,144,38,5,ega_ui_palette[page_==Page::error?12:10],true);
        if(page_==Page::error) screen_.font().draw_text(frame,"ENTER OR ESC TO CONTINUE",0,192,40,1,ega_ui_palette[15]);
        else if(page_!=Page::writing && page_!=Page::leaving && page_!=Page::drop_feedback) commands(frame);
    }
    void after_present(IGameFlow&) override {art_presented_=true;if(page_==Page::save_exit)save_exit_presented_=true;if(page_==Page::writing) write_presented_=true;if(page_==Page::drop_feedback) drop_presented_=true;}
    void on_suspend() override {art_presented_=false;}
    void on_resume() override {art_presented_=false;}
    std::string title() const override {
        switch(page_) {
        case Page::speed:return "PoolRad | camp speed";
        case Page::save_exit:return "PoolRad | leave inn camp and save";
        case Page::slots:return "PoolRad | camp save slots";
        case Page::writing:return "PoolRad | camp saving";
        case Page::quit_query:return "PoolRad | camp quit query";
        case Page::magic:return "PoolRad | camp magic";
        case Page::alter:return "PoolRad | camp alter";
        case Page::pictures:return "PoolRad | camp pictures";
        case Page::order_select:return "PoolRad | camp order select";
        case Page::order_place:return "PoolRad | camp order place";
        case Page::drop_query:return "PoolRad | camp drop query";
        case Page::drop_feedback:return "PoolRad | camp drop feedback";
        case Page::error:return "PoolRad | camp save error";
        default:return "PoolRad | camp";
        }
    }
};
}
std::unique_ptr<IGameMode> make_encamp_mode(ExplorationSession& session,const ExplorationScreen& screen,const IFileSystem& files,ISaveRepository* saves,bool qol_upgrades) {
    return std::make_unique<EncampMode>(session,screen,files,saves,qol_upgrades);
}
}
