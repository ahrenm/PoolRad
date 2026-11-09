#include "gbengine/modes/combat_icon.hpp"
#include "gbengine/party/combat_icon_service.hpp"
#include "gbengine/rendering/combat_actor_art.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include "gbengine/world/exploration_session.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <sstream>

namespace GBEngine {
namespace {
const std::string& source(std::size_t offset) {return StringStore::active().get(pascal_string_id("START.EXE:module",offset));}
std::vector<std::string> words(std::string_view row) {
    std::istringstream input{std::string(row)};std::vector<std::string> result;std::string word;
    while(input>>word) {result.push_back(word);}return result;
}
std::uint8_t shortcut(std::string_view label) {
    for(const auto value:label)if((value>='A' && value<='Z') || (value>='0' && value<='9'))return static_cast<std::uint8_t>(value);
    return 0;
}
class CombatIconMode final:public IGameMode {
    enum class Page {root,parts,colors,style,edit,confirm,leaving};
    ExplorationSession* session_=nullptr;
    Party& party_;
    const IFileSystem& files_;
    std::function<void()> completed_;
    const ExplorationScreen& screen_;
    CombatIconPlan plan_;
    CombatIconAppearance kept_,candidate_;
    CombatActorArt old_art_,new_art_;
    Page page_=Page::root,return_page_=Page::parts;
    CombatIconPart part_=CombatIconPart::head;
    std::size_t component_=0,focus_=0;
    bool secondary_=false,presented_=false;
    mutable bool rendered_=false;
    std::string feedback_;
    static constexpr CombatActorIdentity identity{CombatActorOrigin::party,0};
    std::vector<std::string> labels() const {
        if(page_==Page::root)return words(source(0xcfa8));
        if(page_==Page::parts)return words(source(0xcfd1));
        if(page_==Page::colors) {
            auto row=source(0xcffa);row.replace(12,4,overlay_string(secondary_?0x1d170:0x1d16b));return words(row);
        }
        if(page_==Page::style)return words(overlay_string(candidate_.style==2?0x1d15f:0x1d165)+source(0xd023));
        if(page_==Page::edit)return words(source(0xd04c));
        if(page_==Page::confirm)return words(overlay_string(0x2d979));
        return {};
    }
    void changed() {presented_=false;rendered_=false;focus_=0;}
    bool stage(const CombatIconAppearance& value) {
        try {
            auto art=CombatActorArt::load_character(files_,
                CombatIconService::preview(party_.member(plan_.member),value));
            candidate_=value;new_art_=std::move(art);feedback_.clear();changed();return true;
        }catch(const DecodeError& error) {feedback_=error.what();changed();return false;}
    }
    void keep() {
        if(session_ && (!session_->can_act() || !session_->camp_open())) {feedback_="Combat icon editing requires an idle camp";changed();return;}
        const auto result=CombatIconService::keep(party_,plan_,candidate_);
        if(!result.applied) {feedback_=result.message;changed();return;}
        plan_=CombatIconService::prepare(party_,plan_.member);kept_=plan_.appearance;
        page_=page_==Page::style?Page::root:return_page_;changed();
    }
    void exit_editor() {
        if(!stage(kept_))return;
        if(page_==Page::edit)page_=return_page_;
        else if(page_==Page::style || page_==Page::parts || page_==Page::colors)page_=Page::root;
        else if(page_==Page::root) {page_=Page::confirm;focus_=1;}
    }
    void activate(std::uint8_t key,IGameFlow& flow) {
        if(page_==Page::confirm) {
            if(key=='Y') {
                try {if(completed_)completed_();page_=Page::leaving;flow.pop_mode();}
                catch(const std::exception& error){feedback_=error.what();changed();}
            }
            else if(key=='N') {
                plan_=CombatIconService::prepare(party_,plan_.member);kept_=plan_.appearance;
                if(stage(kept_)) {old_art_=new_art_;page_=Page::root;}
            }
            return;
        }
        if(key=='E' || !key) {exit_editor();return;}
        if(page_==Page::root) {
            if(key=='P')page_=Page::parts;
            else if(key=='1' || key=='2') {secondary_=key=='2';page_=Page::colors;}
            else if(key=='S')page_=Page::style;
            else return;
            feedback_.clear();changed();
        }else if(page_==Page::parts) {
            if(key!='H' && key!='W')return;
            part_=key=='H'?CombatIconPart::head:CombatIconPart::weapon;
            return_page_=page_;page_=Page::edit;changed();
        }else if(page_==Page::colors) {
            if(key=='W')component_=5;else if(key=='B')component_=0;
            else if(key==(secondary_?'F':'H'))component_=3;
            else if(key=='S')component_=4;else if(key=='A')component_=1;else if(key=='L')component_=2;else return;
            return_page_=page_;page_=Page::edit;changed();
        }else if(page_==Page::style) {
            if(key=='K') {keep();return;}
            if(key=='L' || key=='S') {auto value=candidate_;CombatIconService::set_style(value,key=='L'?2:1);stage(value);}
        }else if(page_==Page::edit) {
            if(key=='K') {keep();return;}
            if(key=='N' || key=='P') {
                auto value=candidate_;
                if(return_page_==Page::parts)CombatIconService::cycle_part(value,part_,key=='N');
                else CombatIconService::cycle_color(value,component_,secondary_,key=='N');
                stage(value);
            }
        }
    }
    static std::uint32_t display(std::uint32_t color) {
        if(color==ega_ui_palette[0])return ega_ui_palette[8];
        if(color==ega_ui_palette[8])return ega_ui_palette[0];
        return color;
    }
    static void blit(Framebuffer& frame,const Image& image,int left,int top) {
        for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
            const auto color=image.pixels[static_cast<std::size_t>(y*image.width+x)];
            if(color)frame[static_cast<std::size_t>((top+y)*320+left+x)]=display(color);
        }
    }
    void pair(Framebuffer& frame,const CombatActorArt& art,int y) const {
        for(unsigned pose=0;pose<2;++pose) {
            const int x=32+72*static_cast<int>(pose);
            for(int row=y;row<y+24;++row)std::fill_n(frame.begin()+row*320+x,24,ega_ui_palette[8]);
            blit(frame,art.cursor(pose),x,y);blit(frame,art.image(identity,pose,0),x,y);
        }
    }
public:
    CombatIconMode(ExplorationSession& session,const ExplorationScreen& screen)
        :CombatIconMode(session.world.party,session.asset_files(),screen,{}) {session_=&session;}
    CombatIconMode(Party& party,const IFileSystem& files,const ExplorationScreen& screen,std::function<void()> completed)
        :party_(party),files_(files),completed_(std::move(completed)),screen_(screen),plan_(CombatIconService::prepare(party,party.selected_index().value())),
         kept_(plan_.appearance),candidate_(kept_),
         old_art_(CombatActorArt::load_character(files,party.member(plan_.member))),new_art_(old_art_) {}
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed || page_==Page::leaving || !presented_)return;
        auto key=event.character;if(key>='a' && key<='z')key=static_cast<std::uint8_t>(key-'a'+'A');
        if(page_==Page::confirm) {
            if(key=='Y' || key=='N')activate(key,flow);
            else if(event.key==Key::left || key==',')focus_=0;
            else if(event.key==Key::right || key=='.')focus_=1;
            return;
        }
        if(event.key==Key::cancel || event.key==Key::backspace) {activate('E',flow);return;}
        // Native menu returns CR/Space unchanged. Do not translate footer focus
        // into a selection, especially the source invalid Parts substate.
        if(event.key==Key::accept || key==' ')return;
        const auto options=labels();
        if(event.key==Key::left || key==',') {focus_=(focus_+options.size()-1)%options.size();return;}
        if(event.key==Key::right || key=='.') {focus_=(focus_+1)%options.size();return;}
        if(std::any_of(options.begin(),options.end(),[&](const auto& label){return shortcut(label)==key;}))activate(key,flow);
    }
    void update(std::uint64_t,IGameFlow&) override {}
    void render(Framebuffer& frame) const override {
        frame.fill(ega_ui_palette[8]);screen_.chrome().draw(frame,ChromeLayout::character_creation());
        pair(frame,old_art_,56);pair(frame,new_art_,104);
        const auto& font=screen_.font();
        font.draw_text(frame,overlay_string(0x1d148),64,48,3,1,ega_ui_palette[15]);
        font.draw_text(frame,overlay_string(0x1d14c),24,80,16,1,ega_ui_palette[15]);
        font.draw_text(frame,overlay_string(0x1d15b),64,96,3,1,ega_ui_palette[15]);
        font.draw_text(frame,overlay_string(0x1d14c),24,128,16,1,ega_ui_palette[15]);
        if(!feedback_.empty())font.draw_text(frame,feedback_,8,152,38,4,ega_ui_palette[10],true);
        const auto prefix=page_==Page::confirm?overlay_string(0x1d19a):std::string{};
        font.draw_text(frame,prefix,0,192,40,1,ega_ui_palette[13]);
        auto x=static_cast<int>(prefix.size()*8);const auto options=labels();
        for(std::size_t i=0;i<options.size();++i) {
            const auto& label=options[i];font.draw_text(frame,label,x,192,(320-x)/8,1,ega_ui_palette[page_==Page::confirm && i==focus_?15:10]);
            for(std::size_t n=0;n<label.size();++n)if(shortcut(label)==static_cast<std::uint8_t>(label[n])) {
                font.draw_text(frame,std::string_view(label).substr(n,1),x+static_cast<int>(n*8),192,1,1,ega_ui_palette[15]);break;
            }
            x+=static_cast<int>((label.size()+1)*8);
        }
        rendered_=true;
    }
    void after_present(IGameFlow&) override {if(rendered_){presented_=true;rendered_=false;}}
    void on_suspend() override {presented_=false;rendered_=false;}
    void on_resume() override {presented_=false;rendered_=false;}
    std::string title() const override {
        static constexpr std::array names{"root","parts","colors","size","edit","confirm","leaving"};
        return std::string("PoolRad | combat icon | ")+names[static_cast<unsigned>(page_)];
    }
};
}
std::unique_ptr<IGameMode> make_combat_icon_mode(ExplorationSession& session,const ExplorationScreen& screen) {
    if(!session.can_act() || !session.camp_open() || !session.active_character())
        throw DecodeError("Combat icon editing requires an idle camp and selected character");
    return std::make_unique<CombatIconMode>(session,screen);
}
std::unique_ptr<IGameMode> make_creation_icon_mode(Party& party,const IFileSystem& files,const ExplorationScreen& screen,std::function<void()> completed) {
    if(party.size()!=1 || !party.selected_index())throw DecodeError("Creation icon requires its owned draft character");
    return std::make_unique<CombatIconMode>(party,files,screen,std::move(completed));
}
}
