#include "gbengine/modes/effect_display.hpp"
#include "gbengine/rendering/presentation.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
std::optional<std::string> effect_label(std::uint8_t code,const SpellCatalog& catalog) {
    const bool spell_name=code==1 || code==2 || code==5 || (code>=8 && code<=14) ||
        code==16 || code==17 || (code>=19 && code<=25) || code==28 || code==29 ||
        (code>=32 && code<=34) || (code>=36 && code<=39) || code==41 || code==42 ||
        code==45 || code==46 || code==49 || (code>=51 && code<=53);
    if(spell_name) {
        for(const auto& definition:catalog.definitions())
            if(definition.effect_code()==code) return std::string(definition.name());
        return overlay_string(0x186f1)+std::to_string(code);
    }
    std::optional<std::size_t> offset;
    switch(code) {
    case 4:offset=0x186f9;break;case 7:offset=0x1871a;break;
    case 27:offset=0x18740;break;case 31:offset=0x1874d;break;
    case 35:offset=0x18756;break;case 44:offset=0x1875d;break;
    case 50:offset=0x1876b;break;case 54:offset=0x18781;break;
    case 55:offset=0x1878a;break;case 59:offset=0x18793;break;
    case 61:offset=0x187a0;break;case 71:offset=0x187b0;break;
    case 72:offset=0x187ba;break;case 89:offset=0x187c6;break;
    default:return {};
    }
    return overlay_string(*offset);
}
class EffectDisplayMode final:public IGameMode {
    std::vector<EffectDisplayRow> rows_;
    const GameFont& font_;
    const ScreenChrome& chrome_;
    std::size_t top_=0,selected_=0,focus_=0;
    bool leaving_=false;
    static constexpr std::size_t visible=19;
    std::size_t bottom() const {return std::min(top_+visible,rows_.size());}
    void normalize(int direction) {
        const auto count=bottom()-top_;
        for(std::size_t i=0;i<count && rows_[selected_].heading;++i)
            selected_=top_+(selected_-top_+(direction<0?count-1:1))%count;
    }
    void move(int direction) {
        const auto count=bottom()-top_;
        selected_=top_+(selected_-top_+(direction<0?count-1:1))%count;normalize(direction);
    }
    void page(int direction) {
        const auto row=selected_-top_;
        if(direction>0) {
            if(top_+visible>=rows_.size()) return;
            top_=std::min(top_+visible,rows_.size()-visible);
        } else {
            if(!top_) return;
            top_=top_>visible?top_-visible:0;
        }
        selected_=std::min(top_+row,bottom()-1);normalize(direction);focus_=0;
    }
    std::vector<std::size_t> commands() const {
        std::vector<std::size_t> result;
        if(top_+visible<rows_.size()) result.push_back(0x2d657);
        if(top_) result.push_back(0x2d65d);
        result.push_back(0x2d663);return result;
    }
public:
    EffectDisplayMode(const Party& party,const SpellCatalog& catalog,const GameFont& font,const ScreenChrome& chrome)
        :rows_(build_effect_display(party,catalog)),font_(font),chrome_(chrome) {
        if(rows_.empty()) throw DecodeError("Effect display requires an imported party");
        normalize(-1);
    }
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed || leaving_) return;
        auto letter=event.character;
        if(letter>='a' && letter<='z') letter=static_cast<std::uint8_t>(letter-'a'+'A');
        if(event.key==Key::cancel || event.key==Key::backspace || event.key==Key::accept || letter=='E' || letter==' ') {
            leaving_=true;flow.pop_mode();return;
        }
        if(event.key==Key::home || event.key==Key::up) move(-1);
        else if(event.key==Key::end || event.key==Key::down) move(1);
        else if(letter=='N' || event.key==Key::next) page(1);
        else if(letter=='P' || event.key==Key::previous) page(-1);
        else if(letter==',' || letter=='.') {
            const auto count=commands().size();focus_=(focus_+(letter==','?count-1:1))%count;
        }
    }
    void update(std::uint64_t,IGameFlow&) override {}
    void render(Framebuffer& frame) const override {
        frame.fill(ega_ui_palette[0]);chrome_.draw(frame,{{{1,1,38,22}}});
        const auto& title=overlay_string(0x187e4);
        const int title_x=8*((39-static_cast<int>(title.size()))/2);
        font_.draw_text(frame,title,title_x,8,38,1,ega_ui_palette[15]);
        for(std::size_t i=top_;i<bottom();++i)
            font_.draw_text(frame,rows_[i].text,8,32+8*static_cast<int>(i-top_),38,1,
                ega_ui_palette[rows_[i].heading?11:i==selected_?15:10]);
        int x=0;const auto entries=commands();
        for(std::size_t i=0;i<entries.size();++i) {
            const auto& label=overlay_string(entries[i]);
            font_.draw_text(frame,label,x,192,(320-x)/8,1,ega_ui_palette[i==focus_%entries.size()?15:10]);
            for(std::size_t j=0;j<label.size();++j) if(label[j]>='A' && label[j]<='Z')
                font_.draw_text(frame,std::string_view(label).substr(j,1),x+8*static_cast<int>(j),192,1,1,ega_ui_palette[15]);
            x+=8*static_cast<int>(label.size());
        }
    }
    std::string title() const override {return "PoolRad | camp effect display";}
};
}
std::vector<EffectDisplayRow> build_effect_display(const Party& party,const SpellCatalog& catalog) {
    std::vector<EffectDisplayRow> rows{{overlay_string(0x186ef),false}};
    if(!party.size()) throw DecodeError("Effect display requires an imported party");
    for(std::size_t i=0;i<party.size();++i) {
        const auto& member=party.member(i);
        if(!member.effects_restored()) throw DecodeError("Effect display requires restored effect records");
        rows.push_back({member.name(),true});bool any=false;
        for(const auto& effect:member.effects()) if(const auto label=effect_label(effect.code(),catalog)) {
            rows.push_back({overlay_string(0x186ef)+*label,false});any=true;
        }
        if(!any) rows.push_back({overlay_string(0x187d0),false});
        rows.push_back({overlay_string(0x186ef),false});
    }
    return rows;
}
std::unique_ptr<IGameMode> make_effect_display_mode(const Party& party,const SpellCatalog& catalog,
        const GameFont& font,const ScreenChrome& chrome) {
    return std::make_unique<EffectDisplayMode>(party,catalog,font,chrome);
}
}
