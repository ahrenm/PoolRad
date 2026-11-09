#include "gbengine/ui/menu_hotkeys.hpp"
#include "gbengine/modes/modes.hpp"
#include "gbengine/campaign/shop_inventory.hpp"
#include "gbengine/party/party.hpp"
#include "gbengine/rendering/presentation.hpp"
#include "gbengine/rendering/screen_chrome.hpp"
#include "gbengine/rendering/character_portrait.hpp"
#include "gbengine/modes/view_spells.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/party/inventory_service.hpp"
#include "gbengine/magic/magic_effect_service.hpp"
#include <algorithm>
#include <bit>
#include <charconv>

namespace GBEngine {
namespace {
std::string native_label(std::size_t offset) {
    auto text=overlay_string(offset);
    while(!text.empty() && text.back()==' ') text.pop_back();
    while(!text.empty() && text.front()==' ') text.erase(text.begin());
    return text;
}
std::string confirmation_label(bool yes) {
    auto text=StringStore::active().get(pascal_string_id("START.EXE:module",yes?0xfcd:0xfd4));
    while(!text.empty() && text.back()==' ') text.pop_back();
    while(!text.empty() && text.front()==' ') text.erase(text.begin());
    return text;
}
class CharacterViewMode final : public IGameMode {
    Party& party_;
    const GameFont& font_;
    const IFileSystem& files_;
    ScreenChrome chrome_;
    std::optional<CharacterPortrait> portrait_;
    std::optional<SpellCatalog> spell_catalog_;
    std::size_t member_,selected_item_=0;
    std::size_t command_=0;
    bool inventory_=false;
    bool combat_ready_presented_=false;
    mutable bool combat_ready_rendered_=false;
    void invalidate_combat_ready() {combat_ready_presented_=false;combat_ready_rendered_=false;}
    enum class MoneyPage {none,recipient,currency,amount};
    MoneyPage money_page_=MoneyPage::none;
    bool money_trade_=false,money_changed_=false;
    std::vector<std::size_t> money_denominations_;
    std::size_t money_selection_=0;
    std::string money_amount_;
    std::uint64_t money_revision_=0;
    CampaignResources discarded_money_;
    CharacterViewServices services_;
    enum class Prompt {none,recipient,drop,trade_confirm,use_target,use_confirm,spell,identify,sell,unavailable};
    Prompt prompt_=Prompt::none;
    ItemId pending_item_=0;
    std::size_t recipient_=0;
    std::optional<std::size_t> scroll_slot_;
    std::optional<ItemEffectRequest> pending_use_;
    std::uint64_t use_revision_{};
    std::uint64_t confirmation_revision_{};
    CampaignResources& money_resources() {return services_.money_pool?*services_.money_pool:discarded_money_;}
    void money_currencies() {
        money_denominations_.clear();for(std::size_t reverse=7;reverse>0;--reverse)
            if(party_.member(member_).word(0x88+2*(reverse-1)))money_denominations_.push_back(reverse-1);
        money_selection_=0;money_page_=MoneyPage::currency;money_amount_.clear();feedback_.clear();
    }
    void finish_money() {
        money_page_=MoneyPage::none;money_amount_.clear();command_=0;
        if(money_changed_) {
            const auto answer=EconomyService(party_,money_resources()).refresh_money_view(member_);
            if(!answer.applied()){feedback_=answer.message;prompt_=Prompt::unavailable;}
        }
        money_changed_=false;
    }
    void begin_money(bool trade) {
        if((services_.execution_mode==1 || services_.execution_mode==6) && !services_.money_pool) {
            feedback_="This character money operation requires the campaign pool.";prompt_=Prompt::unavailable;return;
        }
        money_trade_=trade;money_changed_=false;money_revision_=party_.revision();recipient_=member_;feedback_.clear();
        if(trade)money_page_=MoneyPage::recipient;else money_currencies();
    }
    std::string money_prompt() const {
        static constexpr std::array<std::size_t,7> plurals{0x2580b,0x25803,0x257f9,0x257e9,0x257ef,0x257e3,0x25813};
        return overlay_string(money_trade_?0x23175:0x234c7)+overlay_string(plurals.at(money_denominations_.at(money_selection_)))+
            overlay_string(money_trade_?0x2317f:0x234d1);
    }
    void money_input(InputEvent event,int letter) {
        if(party_.revision()!=money_revision_) {
            money_page_=MoneyPage::none;money_changed_=false;feedback_="Character money selection is stale.";prompt_=Prompt::unavailable;return;
        }
        const bool cancel=event.key==Key::cancel || (money_page_!=MoneyPage::amount && (letter=='E' || letter=='L' || event.key==Key::backspace || letter==' '));
        if(money_page_==MoneyPage::recipient) {
            if(cancel){finish_money();return;}
            if(event.key==Key::home || event.key==Key::up)recipient_=(recipient_+party_.size()-1)%party_.size();
            else if(event.key==Key::end || event.key==Key::down)recipient_=(recipient_+1)%party_.size();
            else if(letter>='1' && letter<='8' && static_cast<std::size_t>(letter-'1')<party_.size()) {recipient_=static_cast<std::size_t>(letter-'1');money_currencies();}
            else if(event.key==Key::accept)money_currencies();
            return;
        }
        if(money_page_==MoneyPage::currency) {
            if(cancel) {
                if(money_trade_) {money_page_=MoneyPage::recipient;feedback_.clear();}else finish_money();
                return;
            }
            if(event.key==Key::home || event.key==Key::up)money_selection_=(money_selection_+money_denominations_.size()-1)%money_denominations_.size();
            else if(event.key==Key::end || event.key==Key::down)money_selection_=(money_selection_+1)%money_denominations_.size();
            else if(event.key==Key::accept || letter=='S') {money_page_=MoneyPage::amount;money_amount_.clear();feedback_.clear();}
            return;
        }
        if(event.key==Key::backspace){if(!money_amount_.empty())money_amount_.pop_back();return;}
        if(event.key==Key::accept || cancel) {
            unsigned value=0;if(!cancel && !money_amount_.empty())(void)std::from_chars(money_amount_.data(),money_amount_.data()+money_amount_.size(),value);
            const auto denomination=money_denominations_.at(money_selection_);EconomyService service(party_,money_resources());
            const auto answer=money_trade_?service.trade_money(member_,recipient_,denomination,static_cast<std::uint16_t>(value),services_.combat?5:services_.execution_mode):
                service.drop_money(member_,denomination,static_cast<std::uint16_t>(value),services_.execution_mode);
            if(answer.applied()) {money_changed_=money_changed_ || value!=0;money_revision_=party_.revision();money_currencies();if(money_denominations_.empty())finish_money();}
            else {money_page_=MoneyPage::currency;feedback_=answer.status==EconomyStatus::rejected && money_trade_?overlay_string(0x250ca):answer.message;}
            return;
        }
        if(letter>='0' && letter<='9' && money_amount_.size()<6) {
            money_amount_.push_back(static_cast<char>(letter));unsigned value=0;(void)std::from_chars(money_amount_.data(),money_amount_.data()+money_amount_.size(),value);
            const auto maximum=party_.member(member_).word(0x88+2*money_denominations_.at(money_selection_));
            if(value>maximum)money_amount_=std::to_string(maximum);
        }
    }
    void draw_money(Framebuffer& frame) const {
        // The original generic currency list occupies columns12..25,rows7..13.
        const auto clear=[&](int x,int y,int width,int height){for(int row=y;row<y+height;++row)std::fill_n(frame.begin()+row*screen_width+x,width,ega_ui_palette[0]);};
        clear(0,192,320,8);
        if(money_page_==MoneyPage::recipient) {
            clear(8,24,304,160);text(frame,overlay_string(0x23154),8,24);
            for(std::size_t i=0;i<party_.size();++i)font_.draw_text(frame,std::to_string(i+1)+" "+party_.member(i).name(),8,48+16*static_cast<int>(i),37,1,ega_ui_palette[i==recipient_?15:10]);
            font_.draw_text(frame,overlay_string(0x21ac2),0,192,40,1,ega_ui_palette[15]);return;
        }
        clear(96,56,112,56);
        for(std::size_t i=0;i<money_denominations_.size();++i) {
            const auto denomination=money_denominations_[i];const auto& label=StringStore::active().get(pascal_string_id("START.EXE:module",0xd7ba+11*denomination));
            const auto line=std::string(8-label.size(),' ')+label+overlay_string(money_trade_?0x2315e:0x234b0)+std::to_string(party_.member(member_).word(0x88+2*denomination));
            font_.draw_text(frame,line,96,56+8*static_cast<int>(i),14,1,ega_ui_palette[i==money_selection_?15:10]);
        }
        if(money_page_==MoneyPage::amount) {
            const auto prompt=money_prompt();font_.draw_text(frame,prompt,0,192,40,1,ega_ui_palette[10]);
            font_.draw_text(frame,money_amount_,8*static_cast<int>(prompt.size()),192,6,1,ega_ui_palette[15]);
        } else {
            const auto& select=overlay_string(money_trade_?0x23160:0x234b2);text(frame,select,0,192,32);
            font_.draw_text(frame,std::string_view(select).substr(0,1),0,192,1,1,ega_ui_palette[15]);
            font_.draw_text(frame,overlay_string(0x2d663),8*static_cast<int>(select.size()),192,10,1,ega_ui_palette[15]);
            if(!feedback_.empty())font_.draw_text(frame,feedback_,8,160,38,2,ega_ui_palette[12],true);
        }
    }
    void report(EconomyResult result) {
        if(result.status==EconomyStatus::confirmation_required && result.gold_quote) {
            const auto& items=party_.member(member_).inventory();std::string name;
            for(std::size_t i=0;i<items.size();++i) if(items.item(i).id()==pending_item_) name=items.item(i).name();
            feedback_=overlay_string(0x2103a+0x1c8b)+std::to_string(*result.gold_quote)+overlay_string(0x2103a+0x1c9a)+name+" "+overlay_string(0x2103a+0x1cb1);
        } else if(result.applied() && result.gold_quote) feedback_=overlay_string(0x2103a+0x1cc0);
        else feedback_=result.message;
        const auto count=party_.member(member_).inventory().size();
        selected_item_=count?std::min(selected_item_,count-1):0;
    }
    std::string feedback_;
    void report(ItemActionResult result) {
        feedback_=result.status==ItemActionStatus::unresolved && result.effect?
            StringStore::active().runtime_text("This effect is not available yet."):std::move(result.message);
        const auto& items=party_.member(member_).inventory();
        if(items.size()) {
            selected_item_=std::min(selected_item_,items.size()-1);
            for(std::size_t i=0;i<items.size();++i) if(items.item(i).id()==result.item) selected_item_=i;
        } else selected_item_=0;
    }
    void begin_use(ItemActionResult result) {
        if(!result.effect) {report(std::move(result));return;}
        pending_use_=*result.effect;use_revision_=party_.revision();feedback_.clear();
        const auto mode=services_.activations?services_.activations->noncombat_item_target_mode(*pending_use_):std::nullopt;
        if(mode && *mode==0) {
            pending_use_.reset();prompt_=Prompt::none;
            report({ItemActionStatus::unresolved,"This item spell requires the combat service",pending_item_,{}});
        } else prompt_=mode && (*mode==1 || *mode==4)?Prompt::use_confirm:Prompt::use_target;
    }
    void activate_use(std::size_t target) {
        prompt_=Prompt::none;
        if(!pending_use_) {report({ItemActionStatus::rejected,"Item selection is stale",pending_item_,{}});return;}
        auto request=*pending_use_;pending_use_.reset();request.target=target;
        InventoryService service(party_);
        if(services_.activations) report(services_.activations->activate_item(request));
        else if(services_.effects) report(service.use(request,*services_.effects));
        else report({ItemActionStatus::unresolved,"Item activation requires its effect service",pending_item_,request});
    }
    void text(Framebuffer& frame,std::string_view value,int x,int y,int columns=38,int lines=1) const {
        font_.draw_text(frame,value,x,y,columns,lines);
    }
    void shortcut(Framebuffer& frame,std::string_view value,int x,int y) const {
        text(frame,StringStore::active().runtime_text(value),x,y);
        font_.draw_text(frame,value.substr(0,1),x,y,1,1,menu_action_color);
    }
    void stats(Framebuffer& frame,const Character& character) const {
        const auto s=character.stats();
        const auto white=[&](std::string_view value,int x,int y){font_.draw_text(frame,value,x,y,(312-x)/8,1,ega_ui_palette[15]);};
        white(std::string(character.sex_name())+" "+std::string(character.race_name())+" "+overlay_string(0x21040)+std::to_string(s.age),8,24);
        white(character.alignment_name(),8,32);
        white(character.class_name(),8,40);
        for(std::size_t i=0;i<6;++i) {
            text(frame,overlay_string(0x21045+5*i),8,56+8*static_cast<int>(i),4);
            text(frame,std::to_string(s.abilities[i]),s.abilities[i]>=10?40:48,56+8*static_cast<int>(i),3);
            if(i==0 && s.abilities[i]==18 && s.exceptional_strength) {
                auto exceptional=std::to_string(s.exceptional_strength);
                if(s.exceptional_strength==100) exceptional=overlay_string(0x21934);
                else if(s.exceptional_strength<10) exceptional=overlay_string(0x21932)+exceptional;
                text(frame,overlay_string(0x21937)+exceptional+overlay_string(0x21939),56,56,5);
            }
        }
        int money_y=56;
        for(std::size_t i=0;i<7;++i) {
            const auto index=6-i;if(!s.money[index])continue;
            const auto& label=StringStore::active().get(pascal_string_id("START.EXE:module",0xd7ba+11*index));
            text(frame,label,8*(20-static_cast<int>(label.size())),money_y,10);
            text(frame,std::to_string(s.money[index]),168,money_y,5);money_y+=8;
        }
        std::string levels;
        for(auto level:s.levels) if(level && level<128) {if(!levels.empty()) levels+=overlay_string(0x21069);levels+=std::to_string(level);}
        white(overlay_string(0x21063),8,120);white(levels,56,120);
        white(overlay_string(0x2106b)+std::to_string(std::bit_cast<std::int32_t>(s.experience)),136,120);
        white(overlay_string(0x215f8),8,136);text(frame,std::to_string(s.armor_class),32,136,4);
        white(overlay_string(0x21606),72,136);text(frame,std::to_string(static_cast<std::uint8_t>(s.thac0)),120,136,4);
        white(overlay_string(0x21624),176,136);text(frame,std::to_string(s.encumbrance),272,136,5);
        white(overlay_string(0x215ff),8,144);
        font_.draw_text(frame,std::to_string(s.current_hp),32,144,3,1,ega_ui_palette[s.current_hp<s.maximum_hp?14:10]);
        std::string damage=std::to_string(s.damage_dice)+native_label(0x2160f)+std::to_string(s.damage_sides);
        if(s.damage_bonus) damage+=(s.damage_bonus>0?native_label(0x21611):native_label(0x21613))+std::to_string(s.damage_bonus<0?-s.damage_bonus:s.damage_bonus);
        white(overlay_string(0x21615),64,144);text(frame,damage,120,144,9);
        auto movement=s.movement;
        const auto marker=[&](std::uint8_t code){return std::any_of(character.effects().begin(),character.effects().end(),[&](const auto& effect){return effect.code()==code;});};
        if(marker(0x2a))movement=static_cast<std::uint8_t>(movement*2);
        if(marker(0x27))movement=static_cast<std::uint8_t>(movement/2);
        white(overlay_string(0x21636),200,144);text(frame,std::to_string(movement),272,144,4);
        const auto equipped=EquipmentRules::inspect(character,party_.item_catalog());
        const auto detected=MagicEffectService::party_detects_magic(party_);
        if(const auto item=equipped.slots[0]) {
            white(overlay_string(0x21070),8,160);
            const auto& weapon=character.inventory().item(*item);text(frame,detected?weapon.name(*detected):weapon.name(),64,160,31);
        }
        if(const auto item=equipped.slots[2]) {
            white(overlay_string(0x21077),16,168);
            const auto& armor=character.inventory().item(*item);text(frame,detected?armor.name(*detected):armor.name(),64,168,31);
        }
        white(overlay_string(0x2107d),8,176);text(frame,character_status_name(character.status()),64,176,31);
        draw_view_commands(frame);
    }
    std::vector<std::string> view_commands() const {
        const auto& character=party_.member(member_);std::vector<std::string> result;
        if(character.inventory().size())result.push_back(overlay_string(0x21aa6));
        if(character_has_view_spells(character))result.push_back(overlay_string(0x21aad));
        bool money=false;for(std::size_t i=0;i<7;++i)money=money || character.word(0x88+2*i)!=0;
        if(money && !services_.read_only && !services_.combat && services_.execution_mode!=5 && (character.byte(0x84)<128 || !character.active() || character.status()==1))result.push_back(overlay_string(0x21ab5));
        if(money && !services_.read_only)result.push_back(overlay_string(0x21abc));
        result.push_back(overlay_string(0x21ac2));return result;
    }
    void draw_view_commands(Framebuffer& frame) const {
        const auto& prefix=overlay_string(0x21ac7);font_.draw_text(frame,prefix,0,192,40,1,ega_ui_palette[13]);
        int x=8*static_cast<int>(prefix.size());const auto commands=view_commands();
        for(std::size_t i=0;i<commands.size();++i) {
            font_.draw_text(frame,commands[i],x,192,(320-x)/8,1,ega_ui_palette[i==command_%commands.size()?15:10]);
            for(std::size_t j=0;j<commands[i].size();++j)if(commands[i][j]>='A' && commands[i][j]<='Z')
                font_.draw_text(frame,std::string_view(commands[i]).substr(j,1),x+8*static_cast<int>(j),192,1,1,ega_ui_palette[15]);
            x+=8*static_cast<int>(commands[i].size());
        }
    }
    void inventory(Framebuffer& frame,const Character& character) const {
        const auto& items=character.inventory();
        const auto detected=MagicEffectService::party_detects_magic(party_);
        text(frame,native_label(0x21ef2),16,24);
        const auto first=(selected_item_/6)*6;
        int y=40;
        for(std::size_t i=first;i<items.size() && i<first+6;++i) {
            const auto& item=items.item(i);
            const auto name=detected?item.name(*detected):item.name();
            text(frame,i==selected_item_?">":" ",8,y,1);
            text(frame,item.ready_label(),16,y,5);
            font_.draw_text(frame,name,64,y,31,2,GameFont::green,true);
            y+=name.size()>31?16:8;
        }
        if(!items.size()) text(frame,StringStore::active().runtime_text("Empty"),16,40);
        else text(frame,std::to_string(selected_item_+1)+" / "+std::to_string(items.size()),8,168);
        font_.draw_text(frame,feedback_,8,136,38,4,GameFont::green,true);
        if(!detected && feedback_.empty()) text(frame,"Effect queries unavailable in this fixture.",8,136,38,2);
        if(services_.read_only) {
            if(services_.combat_ready) {
                shortcut(frame,native_label(0x21ebd),8,192);
                shortcut(frame,overlay_string(0x21ac2),72,192);
            } else {
                text(frame,"View only",8,176,38);
                shortcut(frame,overlay_string(0x21ac2),8,192);
            }
            return;
        }
        shortcut(frame,native_label(0x21ebd),8,176);shortcut(frame,native_label(0x21ec3),72,176);
        shortcut(frame,native_label(0x21ec8),120,176);shortcut(frame,native_label(0x21ecf),184,176);
        shortcut(frame,native_label(0x21ed5),8,192);shortcut(frame,native_label(0x21edc),64,192);
        shortcut(frame,native_label(0x21ee8),112,192);
        shortcut(frame,"Leave",216,192);
        if(services_.shop) shortcut(frame,overlay_string(0x26609),160,192);
        else text(frame,StringStore::active().runtime_text("ARROWS / PGUP / PGDN"),8,160,26);
    }
public:
    CharacterViewMode(Party& party,std::size_t member,const GameFont& font,const IFileSystem& files,CharacterViewServices services):party_(party),font_(font),files_(files),chrome_(ScreenChrome::load(files)),member_(member),services_(services) {
        (void)party_.member(member_);
        inventory_=services_.initial_inventory;
        if(services_.portrait_enabled)portrait_=CharacterPortrait::load(files,party_.member(member_));
    }
    bool text_input_active() const override {return money_page_==MoneyPage::amount;}
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(services_.activations && services_.activations->activation_notice())return;
        if(!event.pressed) return;
        if(services_.combat_abort && event.key==Key::combat_abort) {services_.combat_abort->requested=true;return;}
        if(services_.combat_abort && services_.combat_abort->requested)return;
        auto letter=event.character>='a' && event.character<='z'?event.character-'a'+'A':event.character;
        if(money_page_!=MoneyPage::none){money_input(event,letter);return;}
        InventoryService service(party_);
        if(prompt_!=Prompt::none) {
            if(prompt_==Prompt::unavailable) {
                if(event.key==Key::accept || event.key==Key::cancel || event.key==Key::backspace || letter=='E' || letter=='L') {prompt_=Prompt::none;feedback_.clear();}
                return;
            }
            if(letter=='L' || letter=='N' || event.key==Key::backspace || event.key==Key::cancel) {prompt_=Prompt::none;feedback_.clear();scroll_slot_.reset();pending_use_.reset();return;}
            if(prompt_==Prompt::spell) {
                if(letter>='1' && letter<='3') {
                    if(party_.revision()!=use_revision_) {prompt_=Prompt::none;report({ItemActionStatus::rejected,"Scroll selection is stale",pending_item_,{}});return;}
                    const auto index=static_cast<std::size_t>(letter-'1');
                    const auto& items=party_.member(member_).inventory();
                    for(std::size_t i=0;i<items.size();++i) if(items.item(i).id()==pending_item_ && items.item(i).effect_bytes()[index]) {
                        scroll_slot_=index;begin_use(service.prepare_use(member_,pending_item_,member_,scroll_slot_));break;
                    }
                }
            } else if(prompt_==Prompt::use_confirm) {
                if(event.key==Key::accept || letter=='Y') activate_use(member_);
            } else if(prompt_==Prompt::recipient || prompt_==Prompt::use_target) {
                if(letter>='1' && letter<='8' && static_cast<std::size_t>(letter-'1')<party_.size()) {
                    recipient_=static_cast<std::size_t>(letter-'1');const auto previous=prompt_;prompt_=Prompt::none;
                    if(previous==Prompt::use_target) {activate_use(recipient_);return;}
                    auto result=service.trade(member_,pending_item_,recipient_);
                    if(result.status==ItemActionStatus::confirmation_required) prompt_=Prompt::trade_confirm;
                    report(std::move(result));
                }
            } else if(letter=='Y') {
                const auto previous=prompt_;prompt_=Prompt::none;
                if(services_.shop && (previous==Prompt::identify || previous==Prompt::sell)) {
                    if(party_.revision()!=confirmation_revision_) {feedback_="Inventory confirmation is stale.";return;}
                    report(previous==Prompt::sell?services_.shop->sell_item(member_,pending_item_,true):services_.shop->identify_item(member_,pending_item_,true));
                }
                else if(previous==Prompt::identify) report(service.identify(member_,pending_item_,{services_.identification_available,services_.pooled_coins},true));
                else report(previous==Prompt::drop?service.drop(member_,pending_item_,true):service.trade(member_,pending_item_,recipient_,true));
            }
            return;
        }
        if(!inventory_ && (letter==',' || letter=='.' || event.key==Key::left || event.key==Key::right)) {
            const auto count=view_commands().size();command_=(command_+(letter==',' || event.key==Key::left?count-1:1))%count;return;
        }
        if(!inventory_ && event.key==Key::accept)letter=view_commands().at(command_%view_commands().size())[0];
        if(letter=='L' || letter=='E' || event.key==Key::backspace || event.key==Key::cancel) {
            if(inventory_) {inventory_=false;command_=0;invalidate_combat_ready();}
            else flow.pop_mode();
        } else if(!inventory_) {
            const auto commands=view_commands();
            const bool offered=std::any_of(commands.begin(),commands.end(),[&](const auto& value){return !value.empty() && value[0]==letter;});
            if(!offered)return;
            if(letter=='I') {inventory_=true;feedback_.clear();invalidate_combat_ready();}
            else if(letter=='S') {
                if(!spell_catalog_)spell_catalog_=SpellCatalog::load(files_);
                if(auto spells=make_view_spells_mode(party_,member_,*spell_catalog_,font_,chrome_,services_.combat_abort))flow.push_mode(std::move(spells));
            } else if(letter=='T' || letter=='D') {
                begin_money(letter=='T');
            }
        }
        else if(inventory_) {
            const auto count=party_.member(member_).inventory().size();
            if(!count) return;
            const auto previous_item=selected_item_;
            if(event.key==Key::up && selected_item_) --selected_item_;
            else if(event.key==Key::down) selected_item_=std::min(selected_item_+1,count-1);
            else if(event.key==Key::next) selected_item_=std::min(selected_item_+6,count-1);
            else if(event.key==Key::previous) selected_item_=selected_item_>6?selected_item_-6:0;
            else if(services_.combat_ready && letter=='R') {
                if(!combat_ready_presented_)return;
                invalidate_combat_ready();
                pending_item_=party_.member(member_).inventory().item(selected_item_).id();
                report(services_.combat_ready(member_,pending_item_));
                if(services_.combat_ready_feedback_pending && services_.combat_ready_feedback_pending())flow.pop_mode();
                return;
            }
            else if(services_.read_only) {feedback_="Items cannot be changed right now.";}
            else {
                pending_item_=party_.member(member_).inventory().item(selected_item_).id();
                if(letter=='R') report(services_.activations?services_.activations->ready_item(member_,pending_item_):service.ready(member_,pending_item_,services_.effects));
                else if(letter=='H') report(service.halve(member_,pending_item_));
                else if(letter=='J') report(service.join(member_,pending_item_));
                else if(letter=='S' && services_.shop) {
                    auto result=services_.shop->sell_item(member_,pending_item_);confirmation_revision_=party_.revision();
                    if(result.status==EconomyStatus::confirmation_required) prompt_=Prompt::sell;
                    report(std::move(result));
                }
                else if(letter=='I') {
                    if(services_.shop) {
                        auto result=services_.shop->identify_item(member_,pending_item_);confirmation_revision_=party_.revision();
                        if(result.status==EconomyStatus::confirmation_required) prompt_=Prompt::identify;
                        report(std::move(result));
                    } else {auto result=service.identify(member_,pending_item_,{services_.identification_available,services_.pooled_coins});if(result.status==ItemActionStatus::confirmation_required) prompt_=Prompt::identify;report(std::move(result));}
                }
                else if(letter=='T') {prompt_=Prompt::recipient;feedback_.clear();}
                else if(letter=='D') {auto result=service.drop(member_,pending_item_);if(result.status==ItemActionStatus::confirmation_required) prompt_=Prompt::drop;report(std::move(result));}
                else if(letter=='U') {
                    scroll_slot_.reset();pending_use_.reset();
                    const auto& item=party_.member(member_).inventory().item(selected_item_);
                    if(party_.item_catalog().definition(item.type()).scroll() && item.hidden_components()) {
                        const auto reading=service.read_scroll(member_,pending_item_);
                        if(!reading.applied()) {report(reading);return;}
                    }
                    const auto result=service.prepare_use(member_,pending_item_,member_);
                    use_revision_=party_.revision();
                    if(result.status==ItemActionStatus::choice_required) {prompt_=Prompt::spell;feedback_.clear();}
                    else begin_use(result);
                }
            }
            if(selected_item_!=previous_item)invalidate_combat_ready();
        }
    }
    void update(std::uint64_t elapsed,IGameFlow&) override {
        if(services_.activations)services_.activations->advance_activation_notice(elapsed);
    }
    void after_present(IGameFlow& flow) override {
        if(services_.combat_abort && services_.combat_abort->requested) {flow.pop_mode();return;}
        if(combat_ready_rendered_) {combat_ready_presented_=true;combat_ready_rendered_=false;}
        if(services_.activations)services_.activations->activation_notice_presented();
    }
    void on_resume() override {invalidate_combat_ready();}
    void render(Framebuffer& frame) const override {
        combat_ready_rendered_=inventory_ && prompt_==Prompt::none && bool(services_.combat_ready);
        frame.fill(0xff000000u);
        const auto& character=party_.member(member_);
        chrome_.draw(frame,ChromeLayout::character_view(false));
        if(!inventory_ && prompt_==Prompt::none) {
            const bool show_portrait=money_page_!=MoneyPage::recipient;
            chrome_.draw(frame,ChromeLayout::character_view(show_portrait && portrait_ && portrait_->visible()));
            if(show_portrait && portrait_)portrait_->draw(frame,224,8);
        }
        const auto color=!character.active()?12u:character.byte(0x10e)==1?14u:11u;
        font_.draw_text(frame,character.name(),8,8,26,1,ega_ui_palette[color]);
        if(character.byte(0x84)>127)font_.draw_text(frame,overlay_string(0x2103a),8*static_cast<int>(character.name().size()+3),8,5,1,GameFont::green);
        if(money_page_!=MoneyPage::none) {stats(frame,character);draw_money(frame);}
        else if(prompt_==Prompt::unavailable) {
            font_.draw_text(frame,feedback_,8,40,38,5,ega_ui_palette[12],true);
            font_.draw_text(frame,overlay_string(0x21ac2),0,192,40,1,ega_ui_palette[15]);
        } else if(prompt_==Prompt::spell) {
            text(frame,StringStore::active().runtime_text("Choose a spell"),8,24);
            const auto& items=character.inventory();
            for(std::size_t item=0;item<items.size();++item) if(items.item(item).id()==pending_item_) {
                for(std::size_t slot=0;slot<3;++slot) {
                    const auto spell=static_cast<std::uint8_t>(items.item(item).effect_bytes()[slot]&0x7f);if(!spell) continue;
                    const int y=48+16*static_cast<int>(slot);text(frame,std::to_string(slot+1)+" "+std::string(ItemCatalog::spell_name(spell)),8,y);
                    font_.draw_text(frame,std::to_string(slot+1),8,y,1,1,menu_action_color);
                }
            }
            shortcut(frame,"Leave",8,184);
        } else if(prompt_==Prompt::use_confirm) {
            const auto mode=services_.activations && pending_use_?services_.activations->noncombat_item_target_mode(*pending_use_):std::nullopt;
            text(frame,mode && *mode==4?"Use on all party members?":"Use on this character?",8,40);
            font_.draw_text(frame,"ENTER TO CONFIRM",8,64,38,1,menu_action_color);shortcut(frame,"Leave",8,184);
        } else if(prompt_==Prompt::recipient || prompt_==Prompt::use_target) {
            text(frame,StringStore::active().runtime_text(prompt_==Prompt::recipient?"Choose recipient":"Choose target"),8,24);
            for(std::size_t i=0;i<party_.size();++i) {
                const int y=48+16*static_cast<int>(i);text(frame,std::to_string(i+1)+" "+party_.member(i).name(),8,y);
                font_.draw_text(frame,std::to_string(i+1),8,y,1,1,menu_action_color);
            }
            shortcut(frame,"Leave",8,184);
        } else if(prompt_==Prompt::drop || prompt_==Prompt::trade_confirm || prompt_==Prompt::identify || prompt_==Prompt::sell) {
            font_.draw_text(frame,feedback_,8,40,38,14,GameFont::green,true);
            shortcut(frame,confirmation_label(true),8,192);shortcut(frame,confirmation_label(false),64,192);
        } else if(inventory_) inventory(frame,character);else stats(frame,character);
        if(services_.activations)if(const auto* notice=services_.activations->activation_notice()) {
            for(int y=136;y<184;++y)std::fill_n(frame.begin()+y*320+8,304,ega_ui_palette[0]);
            const auto& recipient=party_.member(notice->member);
            font_.draw_text(frame,recipient.name(),8,144,38,1,ega_ui_palette[recipient.active()?11:12]);
            font_.draw_text(frame,StringStore::active().get(notice->text_id),8,152,38,4,ega_ui_palette[10],true);
        }
    }
    std::string title() const override {return "PoolRad | "+party_.member(member_).name()+(inventory_?" | Inventory":" | View");}
};
}
std::unique_ptr<IGameMode> make_character_view_mode(Party& party,std::size_t member,const GameFont& font,const IFileSystem& files,CharacterViewServices services) {
    return std::make_unique<CharacterViewMode>(party,member,font,files,services);
}
}
