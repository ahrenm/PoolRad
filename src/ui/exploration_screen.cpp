#include "gbengine/qol/training_indicator.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/ui/dialogue_text.hpp"
#include "gbengine/ui/glyph_text.hpp"
#include <algorithm>
#include <sstream>
namespace GBEngine {
namespace {
const std::string& text(std::size_t offset) {return StringStore::active().get(pascal_string_id("START.EXE:module",offset));}
std::vector<std::string> words(std::string_view value) {
    std::istringstream input{std::string(value)};std::vector<std::string> result;std::string word;
    while(input>>word) result.push_back(word);
    return result;
}
std::vector<std::string> lines(std::string_view value,unsigned width=38) {
    std::vector<std::string> result(1);
    for(std::size_t i=0;i<value.size();++i) {
        if(value[i]=='\n') {result.emplace_back();continue;}
        if(value[i]==' ' && result.back().empty()) continue;
        if(value[i]!=' ' && (i==0 || value[i-1]==' ' || value[i-1]=='\n')) {
            const auto end=value.find_first_of(" \n",i);
            const auto length=(end==std::string_view::npos?value.size():end)-i;
            if(!result.back().empty() && length<=width && result.back().size()+length>width) result.emplace_back();
        }
        if(result.back().size()==width) result.emplace_back();
        result.back().push_back(value[i]);
    }
    return result;
}
void blit(Framebuffer& out,const Framebuffer& source,int sx,int sy,int x,int y,int size) {
    for(int row=0;row<size;++row) for(int col=0;col<size;++col)
        out[static_cast<std::size_t>((y+row)*320+x+col)]=source[static_cast<std::size_t>((sy+row)*320+sx+col)];
}
}
void render_area_map(Framebuffer& frame,const UiTiles& tiles,const GeoMap& map,int x,int y,Direction facing) {
    if(!GeoMap::contains(x,y) || static_cast<unsigned>(facing)>3) throw DecodeError("Invalid Area map position");
    const int left=std::clamp(x-5,0,5),top=std::clamp(y-5,0,5);
    for(int row=0;row<11;++row) for(int col=0;col<11;++col) {
        unsigned mask=0;
        for(unsigned d=0;d<4;++d) if(map.wall(left+col,top+row,static_cast<Direction>(d))) mask|=1u<<d;
        tiles.draw(frame,4+mask,24+8*col,24+8*row);
    }
    tiles.draw(frame,static_cast<unsigned>(facing),24+8*(x-left),24+8*(y-top));
}
ExplorationScreen ExplorationScreen::load(const IFileSystem& files,bool qol_upgrades) {
    ExplorationScreen result;result.chrome_=ScreenChrome::load(files);result.font_=GameFont::load(files);if(qol_upgrades)result.training_indicator_=TrainingService::load(files);return result;
}
void ExplorationScreen::party(Framebuffer& frame,const ExplorationSession& session,const ExplorationUiState& ui) const {
    font_.draw_text(frame,text(0x12fb),136,16,15,1,ega_ui_palette[15]);
    font_.draw_text(frame,text(0x1300),264,16,6,1,ega_ui_palette[15]);
    const auto selected=ui.highlighted_member?ui.highlighted_member:session.selecting_character()?std::optional<std::size_t>{session.character_selection()}:session.active_character();
    for(std::size_t i=0;i<session.world.party.size();++i) {
        const auto& member=session.world.party.member(i);const int y=32+8*static_cast<int>(i);
        const auto native_colour=selected==i?15u:!member.active()?12u:member.byte(0x10e)==1?14u:11u;
        const auto colours=training_indicator_?training_indicator_colours(*training_indicator_,member,selected==i,native_colour):
            TrainingIndicatorColours{native_colour,10};
        font_.draw_text(frame,member.name(),136,y,15,1,ega_ui_palette[colours.name]);
        const auto raw=member.byte(0x111);const int shift=raw<51?1:raw<=60?2:raw<=69?1:0;
        font_.draw_text(frame,std::to_string(60-raw),256+8*shift,y,4-shift,1,ega_ui_palette[colours.armour_class]);
        const auto hp=member.hit_points();const int pad=hp<10?2:hp<100?1:0;
        font_.draw_text(frame,std::to_string(hp),288+8*pad,y,3-pad,1,ega_ui_palette[hp<member.byte(0x32)?14:10]);
    }
    std::string location;
    if(session.vm.read_value(0x49fb)==0) location=std::to_string(session.position_x())+text(0x3494)+std::to_string(session.position_y())+
        text(0x3496)+text(0xed68+6*static_cast<unsigned>(session.world.facing))+text(0x3496);
    if(const auto clock=session.clock()) location+=clock->hour_text()+text(0x3498)+clock->minute_text();
    else location+="--"+text(0x3498)+"--"; // Preview fixture has no campaign clock.
    if(session.vm.read_value(0x6dca)&1) location+=text(0x34a3);
    font_.draw_text(frame,location,136,120,22,1,ega_ui_palette[10]);
}
void ExplorationScreen::commands(Framebuffer& frame,const ExplorationSession& session,const ExplorationUiState& ui) const {
    if(session.door_pending()) {
        int x=0;const auto choices=session.door_choices();
        for(std::size_t i=0;i<choices.size();++i) {
            const auto offset=choices[i]=='B'?0x17417:choices[i]=='P'?0x1741c:choices[i]=='K'?0x17422:0x17429;
            auto label=StringStore::active().get(pascal_string_id("GAME.OVR",offset));
            if(!label.empty() && label.front()==' ')label.erase(0,1);
            font_.draw_text(frame,label,x,192,static_cast<int>(label.size()),1,ega_ui_palette[i==session.door_selection()?15:10]);
            font_.draw_text(frame,label.substr(0,1),x,192,1,1,ega_ui_palette[15]);x+=static_cast<int>(label.size()+1)*8;
        }
        return;
    }
    if(ui.combat_intro) {font_.draw_text(frame,"ENTER TO BEGIN COMBAT",0,192,40,1,ega_ui_palette[15]);return;}
    if(ui.narration_handoff || session.text_page_pending()) {
        font_.draw_text(frame,"ENTER TO CONTINUE",0,192,40,1,ega_ui_palette[15]);return;
    }
    if(const auto* notice=session.command_notice()) {
        font_.draw_text(frame,StringStore::active().get(notice->text_id),0,192,40,1,ega_ui_palette[notice->color]);return;
    }
    if(ui.command_line) {font_.draw_text(frame,*ui.command_line,0,192,40,1,ega_ui_palette[15]);return;}
    if(session.encounter_notice())return;
    if(session.entering_input()) {font_.draw_text(frame,"ENTER TO SUBMIT",0,192,40,1,ega_ui_palette[15]);return;}
    if(session.selecting_character()) {font_.draw_text(frame,"HOME/END OR ARROWS, ENTER TO SELECT",0,192,40,1,ega_ui_palette[15]);return;}
    if(const auto* menu=session.menu()) {
        if(menu->vertical) {font_.draw_text(frame,"ARROWS / PGUP / PGDN / ENTER",0,192,40,1,ega_ui_palette[15]);return;}
        const auto selected=session.menu_selection();std::size_t first=0;
        auto used=std::size_t{};
        for(std::size_t i=0;i<=selected && i<menu->labels.size();++i) {
            if(used+menu->labels[i].size()>40) {first=i;used=0;}
            used+=menu->labels[i].size()+1;
        }
        int x=0;
        for(std::size_t i=first;i<menu->labels.size();++i) {
            if(x>=320) break;
            font_.draw_text(frame,menu->labels[i],x,192,(320-x)/8,1,ega_ui_palette[i==selected?15:10]);
            if(i<menu->hotkeys.size() && !menu->labels[i].empty()) font_.draw_text(frame,menu->labels[i].substr(0,1),x,192,1,1,ega_ui_palette[15]);
            x+=static_cast<int>(menu->labels[i].size()+1)*8;
        }
        return;
    }
    // Automatic picture/clear presentation resumes after present, not a key.
    // Keep the exploration footer stable while the session still blocks input.
    const bool timed=session.vm.pending_request() && session.vm.pending_request()->kind==EclRequestKind::delay;
    // Scripted traversal owns the scene; exploration commands are unavailable.
    // Explicit source menus above still draw their acknowledgement prompt.
    if(timed || (session.campaign_requested() && session.boundary.empty()))return;
    if(!session.can_act() && !timed && !session.picture_token() && !session.effect_notice() && !session.presenting_overland_colour()) {font_.draw_text(frame,"ENTER TO CONTINUE",0,192,40,1,ega_ui_palette[15]);return;}
    auto labels=words(text(session.city_mode()?0xcc9f:0xcc76));
    if(session.qol_upgrades() && labels.size()>=2)labels[labels.size()-2]="Srch";
    const auto focused=static_cast<unsigned>(ui.focus);const unsigned first=session.city_mode()?1:0;
    int x=0;
    for(std::size_t i=0;i<labels.size();++i) {
        font_.draw_text(frame,labels[i],x,192,static_cast<int>(labels[i].size()),1,ega_ui_palette[first+i==focused?15:10]);
        font_.draw_text(frame,labels[i].substr(0,1),x,192,1,1,ega_ui_palette[15]);
        x+=static_cast<int>(labels[i].size()+1)*8;
    }
    if(session.qol_upgrades() && session.can_act()) {
        font_.draw_text(frame,"Journal",x,192,7,1,ega_ui_palette[10]);
        font_.draw_text(frame,"J",x,192,1,1,ega_ui_palette[15]);
    }
}
void ExplorationScreen::dialogue(Framebuffer& frame,const ExplorationSession& session,const ExplorationUiState& ui) const {
    if(const auto* notice=session.encounter_notice()) {
        font_.draw_text(frame,*notice,8,144,38,5,ega_ui_palette[10],true);return;
    }
    if(const auto* notice=session.effect_notice()) {
        const auto& member=session.world.party.member(notice->member);
        font_.draw_text(frame,member.name(),8,144,38,1,ega_ui_palette[member.active()?11:12]);
        font_.draw_text(frame,StringStore::active().get(notice->text_id),8,152,38,4,ega_ui_palette[10],true);return;
    }
    const auto* menu=session.menu();
    if(menu && menu->vertical) {
        const auto prompt=lines(menu->prompt);const auto prompt_rows=std::min<std::size_t>(4,prompt.size());
        for(std::size_t i=0;i<prompt_rows;++i) font_.draw_text(frame,prompt[i],8,136+8*static_cast<int>(i),38,1,ega_ui_palette[10]);
        const std::size_t page_size=prompt_rows<=2?2:1;
        const auto first=(session.menu_selection()/page_size)*page_size;
        for(std::size_t i=first;i<menu->labels.size() && i<first+page_size;++i) {
            const int y=136+8*static_cast<int>(prompt_rows)+16*static_cast<int>(i-first);
            font_.draw_text(frame,i==session.menu_selection()?">":" ",8,y,1,1,ega_ui_palette[15]);
            font_.draw_text(frame,menu->labels[i],24,y,36,2,ega_ui_palette[i==session.menu_selection()?15:10],true);
        }
        return;
    }
    auto value=ui.notice.empty()?session.display_text:ui.notice;
    value=std::string(DialogueText::visible(value));
    if(session.entering_input()) {
        const auto prompt=lines(value);const auto count=std::min<std::size_t>(4,prompt.size());
        for(std::size_t i=0;i<count;++i) font_.draw_text(frame,prompt[i],8,136+8*static_cast<int>(i),38,1,ega_ui_palette[10]);
        for(int y=168;y<184;++y)std::fill_n(frame.begin()+y*320+8,304,ega_ui_palette[0]);
        font_.draw_text(frame,"> "+session.input_buffer(),8,168,38,2,ega_ui_palette[10]);
        GameFont::draw_caret(frame,8+8*static_cast<int>((session.input_buffer().size()+2)%38),168+8*static_cast<int>((session.input_buffer().size()+2)/38),ega_ui_palette[10]);return;
    }
    const bool problem=!ui.combat_intro && !ui.narration_handoff && !session.text_page_pending() && (session.time_blocked() || session.vm.status()==EclStatus::faulted || session.vm.status()==EclStatus::budget_exhausted || session.party_defeated() ||
        (session.vm.pending_request() && !(session.campaign_requested() && session.boundary.empty()) && !session.picture_token() && session.vm.pending_request()->kind!=EclRequestKind::picture && session.vm.pending_request()->kind!=EclRequestKind::delay && !menu && !session.selecting_character()));
    if(problem && !session.boundary.empty()) {if(!value.empty()) value+='\n';value+=session.boundary;}
    auto wrapped=lines(value);
    if(ui.notice.empty() && !problem) {
        // ECL's text service already owns wrapping/cursor/page boundaries.
        // Retain leading spaces and empty rows rather than reflowing pixels.
        wrapped.clear();std::size_t start=0;
        do {const auto end=value.find('\n',start);wrapped.push_back(value.substr(start,end==std::string::npos?end:end-start));
            if(end==std::string::npos)break;
            start=end+1;}while(start<=value.size());
    }
    const auto start=std::size_t{};
    for(std::size_t i=start;i<wrapped.size() && i<6;++i)
        font_.draw_text(frame,wrapped[i],8,136+8*static_cast<int>(i-start),38,1,ega_ui_palette[problem?12:10]);
}
void ExplorationScreen::draw(Framebuffer& frame,const ExplorationSession& session,const ExplorationUiState& ui) const {
    frame.fill(ega_ui_palette[0]);
    const bool viewport=ui.picture_override || !session.overland_view() || session.picture() || session.showing_glyphs();
    chrome_.draw(frame,ChromeLayout::exploration(viewport));
    if(!ui.picture_override && session.area_view() && !session.picture() && !session.showing_glyphs()) render_area_map(frame,chrome_.tiles(),*session.world.map,session.world.x,session.world.y,session.world.facing);
    else {
        staging_->fill(ega_ui_palette[0]);
        if(ui.picture_override) ui.picture_override->draw(*staging_);else session.draw_view(*staging_);
        if(viewport) blit(frame,*staging_,16,16,24,24,88);
        else blit(frame,*staging_,16,0,8,8,120);
    }
    party(frame,session,ui);
    if(!ui.picture_override && session.showing_glyphs()) {
        const auto glyphs=GlyphText(session.glyph_text());
        if(!glyphs.native) {
            const bool old_clear=glyphs.legacy.front()=='\x02';
            const auto value=std::string_view(glyphs.legacy).substr(old_clear?1:0);
            const int left=old_clear?8:24,bottom=old_clear?128:112;
            for(int y=left;y<bottom;++y)for(int x=left;x<bottom;++x)frame[static_cast<std::size_t>(y*320+x)]=ega_ui_palette[0];
            font_.draw_text(frame,value,old_clear?16:24,old_clear?40:24,old_clear?14:11,11,ega_ui_palette[15]);
        } else {
            if(glyphs.canvas)for(int y=8;y<128;++y)for(int x=8;x<128;++x)frame[static_cast<std::size_t>(y*320+x)]=ega_ui_palette[0];
            if(glyphs.frame_restored) {
                ChromeLayout restored{{{1,1,15,15}}};
                if(glyphs.interior_frame)restored.boxes.push_back({3,3,13,13});
                chrome_.draw(frame,restored);
            }
            glyphs.draw_cells(frame,font_);
        }
    }
    dialogue(frame,session,ui);commands(frame,session,ui);
}
}
