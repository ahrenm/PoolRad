#include "gbengine/modes/view_spells.hpp"
#include "gbengine/modes/modes.hpp"
#include "gbengine/rendering/presentation.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
namespace GBEngine {
bool character_has_view_spells(const Character& member) {
    for(std::size_t slot=0;slot<21;++slot) if(member.byte(0x17+slot)) return true;
    return false;
}
namespace {
const std::string& module_text(std::size_t offset) {
    return StringStore::active().get(pascal_string_id("START.EXE:module",offset));
}
class ViewSpellsMode final:public IGameMode {
    struct Row {std::optional<SpellId> spell;std::uint8_t level;};
    const Party& party_;
    std::size_t member_;
    const SpellCatalog& catalog_;
    const GameFont& font_;
    const ScreenChrome& chrome_;
    std::vector<Row> rows_;
    std::size_t top_=0,selected_=0,command_=0;
    bool leaving_=false;
    std::shared_ptr<CombatViewAbort> combat_abort_;
    static constexpr std::size_t visible=18;
    std::size_t bottom() const {return std::min(rows_.size(),top_+visible);}
    void normalize(int direction) {
        const auto count=bottom()-top_;
        for(std::size_t attempt=0;attempt<count && !rows_[selected_].spell;++attempt)
            selected_=top_+(selected_-top_+(direction<0?count-1:1))%count;
    }
    void move(int direction) {
        const auto count=bottom()-top_;
        selected_=top_+(selected_-top_+(direction<0?count-1:1))%count;
        normalize(direction);
    }
    void page(int direction) {
        const auto offset=selected_-top_;
        if(direction>0) {
            if(top_+visible>=rows_.size()) return;
            top_=std::min(top_+visible,rows_.size()-visible);
        } else {
            if(!top_) return;
            top_=top_>visible?top_-visible:0;
        }
        selected_=std::min(top_+offset,bottom()-1);normalize(direction);
    }
    struct Command {std::size_t offset;std::uint8_t key;};
    std::vector<Command> commands() const {
        std::vector<Command> result;
        if(top_+visible<rows_.size())result.push_back({0x2d657,'N'});
        if(top_)result.push_back({0x2d65d,'P'});
        result.push_back({0x2d663,'E'});return result;
    }
public:
    ViewSpellsMode(const Party& party,std::size_t member,const SpellCatalog& catalog,
            const GameFont& font,const ScreenChrome& chrome,std::shared_ptr<CombatViewAbort> combat_abort):
        party_(party),member_(member),catalog_(catalog),font_(font),chrome_(chrome),combat_abort_(std::move(combat_abort)) {
        std::optional<std::uint8_t> previous;
        // Mode0 rejects pending bytes before decoding IDs. Occupied slots are
        // appended in original slot order; duplicates remain independent rows.
        for(std::size_t slot=0;slot<21;++slot) {
            const auto raw=party.member(member).byte(0x17+slot);
            if(!raw || raw>=128)continue;
            const SpellId id(raw);const auto level=catalog.definition(id).level();
            if(previous!=level){rows_.push_back({{},level});previous=level;}
            rows_.push_back({id,level});
        }
        if(rows_.empty())throw DecodeError("Cannot open an empty memorized spell view");
        // Native generic-list initialization backs over its first disabled
        // header, wrapping to the last selectable row in the first window.
        normalize(-1);
    }
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed || leaving_)return;
        if(combat_abort_ && event.key==Key::combat_abort) {combat_abort_->requested=true;return;}
        if(combat_abort_ && combat_abort_->requested)return;
        auto letter=event.character;
        if(letter>='a' && letter<='z')letter=static_cast<std::uint8_t>(letter-'a'+'A');
        if(event.key==Key::accept)letter='E';
        if(letter=='E' || event.key==Key::cancel || event.key==Key::backspace) {
            leaving_=true;flow.pop_mode();return;
        }
        if(letter==',' || letter=='.') {
            const auto count=commands().size();command_=(command_+(letter==','?count-1:1))%count;return;
        }
        if(event.key==Key::home || event.key==Key::up)move(-1);
        else if(event.key==Key::end || event.key==Key::down)move(1);
        else if(letter=='N' || event.key==Key::next)page(1);
        else if(letter=='P' || event.key==Key::previous)page(-1);
        command_=0;
    }
    void update(std::uint64_t,IGameFlow&) override {}
    void after_present(IGameFlow& flow) override {
        if(combat_abort_ && combat_abort_->requested && !leaving_) {leaving_=true;flow.pop_mode();}
    }
    void render(Framebuffer& frame) const override {
        frame.fill(ega_ui_palette[0]);chrome_.draw(frame,{{{1,1,38,1},{1,3,38,22}}});
        const auto& member=party_.member(member_);
        const auto color=!member.active()?12u:member.byte(0x10e)==1?14u:11u;
        font_.draw_text(frame,member.name()+module_text(0x23b3),8,8,38,1,ega_ui_palette[color]);
        const auto x=8*static_cast<int>(member.name().size()+4);
        font_.draw_text(frame,overlay_string(0x239ce)+overlay_string(0x23973),x,8,std::max(0,(312-x)/8),1,ega_ui_palette[10]);
        for(std::size_t i=top_;i<bottom();++i) {
            const auto y=40+8*static_cast<int>(i-top_);
            if(rows_[i].spell)font_.draw_text(frame,overlay_string(0x272a2)+std::string(catalog_.definition(*rows_[i].spell).name()),8,y,38,1,ega_ui_palette[i==selected_?15:10]);
            else font_.draw_text(frame,module_text(0xd239+41*rows_[i].level),8,y,38,1,ega_ui_palette[13]);
        }
        int x_command=0;const auto entries=commands();
        for(std::size_t i=0;i<entries.size();++i) {
            const auto& text=overlay_string(entries[i].offset);
            font_.draw_text(frame,text,x_command,192,(320-x_command)/8,1,ega_ui_palette[i==command_%entries.size()?15:10]);
            for(std::size_t j=0;j<text.size();++j)if(text[j]>='A' && text[j]<='Z')
                font_.draw_text(frame,std::string_view(text).substr(j,1),x_command+8*static_cast<int>(j),192,1,1,ega_ui_palette[15]);
            x_command+=8*static_cast<int>(text.size());
        }
    }
    std::string title() const override {return "PoolRad | character spells";}
};
}
std::unique_ptr<IGameMode> make_view_spells_mode(const Party& party,std::size_t member,
        const SpellCatalog& catalog,const GameFont& font,const ScreenChrome& chrome,std::shared_ptr<CombatViewAbort> combat_abort) {
    bool visible=false;
    for(std::size_t slot=0;slot<21;++slot) {
        const auto raw=party.member(member).byte(0x17+slot);
        if(raw && raw<128)visible=true;
    }
    if(!visible)return {};
    return std::make_unique<ViewSpellsMode>(party,member,catalog,font,chrome,std::move(combat_abort));
}
}
