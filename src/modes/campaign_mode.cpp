#include "gbengine/ui/menu_hotkeys.hpp"
#include "gbengine/modes/modes.hpp"
#include "gbengine/modes/shrine.hpp"
#include "gbengine/world/exploration_session.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/app/runtime_timing.hpp"
#include <algorithm>
#include <charconv>
#include <sstream>
namespace GBEngine {
namespace {
std::vector<std::string> words(std::string_view value) {
    std::istringstream input{std::string(value)};std::vector<std::string> result;std::string word;
    while(input>>word) {std::erase(word,'~');result.push_back(word);}return result;
}
std::string denomination(std::size_t i) {return StringStore::active().get(pascal_string_id("START.EXE:module",0xd7ba+11*i));}
class CampaignMode final:public IGameMode {
    enum class Page {npc_shares,summary,home,items,take,money,amount,appraise,quote,leave,done};
    ExplorationSession& session_;
    const ExplorationScreen& screen_;
    CampaignServiceKind kind_;
    std::uint64_t token_;
    Page page_=Page::home;
    std::size_t selection_=0,focus_=0,item_top_=0;
    std::vector<CampaignItemId> shown_items_;
    std::vector<std::size_t> shown_money_;
    std::string feedback_,amount_;
    bool summary_presented_=false;
    std::optional<CombatToken> combat_token_;
    std::shared_ptr<CombatViewAbort> abort_;
    std::optional<PicturePresentation> treasure_picture_;
    std::vector<MagicFeedback> detect_messages_;
    std::size_t detect_message_=0;
    std::uint64_t detect_elapsed_=0;
    bool detect_presented_=false;
    static constexpr std::size_t shop_rows=19;
    static constexpr std::size_t stock_next_text=0x2d657,stock_previous_text=0x2d65d,stock_exit_text=0x2d663;
    bool shop() const {return kind_==CampaignServiceKind::shop;}
    bool shrine() const {return kind_==CampaignServiceKind::shrine;}
    bool merchant() const {return shop() || shrine();}
    bool stock_items() const {return !shrine() && page_==Page::items;}
    bool next_stock_page() const {return shown_items_.size()>shop_rows && item_top_<shown_items_.size()-shop_rows;}
    bool previous_stock_page() const {return item_top_!=0;}
    bool money_left() const {
        const auto& amounts=session_.campaign_resources().amounts;
        return std::any_of(amounts.begin(),amounts.end(),[](auto value){return value!=0;});
    }
    bool can_detect() const {
        if(merchant() || session_.campaign_resources().items.empty() || !session_.active_character())return false;
        const auto& character=session_.world.party.member(*session_.active_character());
        for(std::size_t slot=0;slot<21;++slot)if(character.byte(0x17+slot)==5 || character.byte(0x17+slot)==11)return true;
        return false;
    }
    std::vector<std::string> commands() const {
        if(page_==Page::appraise) {
            const auto member=session_.active_character();if(!member)return {};
            const auto& character=session_.world.party.member(*member);std::string labels;
            if(character.word(0x92))labels+=overlay_string(0x265cb);
            if(character.word(0x94))labels+=overlay_string(0x265d2);
            return words(labels+overlay_string(0x265dc));
        }
        if(page_==Page::quote && session_.appraisal_quote())
            return words(overlay_string(session_.appraisal_quote()->can_keep()?0x2660e:0x26609));
        if(stock_items()) {
            auto result=words(overlay_string(shop()?0x6d00:0x608d));
            if(next_stock_page()) {auto next=words(overlay_string(stock_next_text));result.insert(result.end(),next.begin(),next.end());}
            if(previous_stock_page()) {auto previous=words(overlay_string(stock_previous_text));result.insert(result.end(),previous.begin(),previous.end());}
            auto exit=words(overlay_string(stock_exit_text));result.insert(result.end(),exit.begin(),exit.end());
            return result;
        }
        if(page_==Page::leave) return words(overlay_string(shrine()?20988:shop()?0x718b:0x63f2));
        if(page_==Page::take) return words(overlay_string(0x62a5));
        if(page_==Page::home) {
            if(shrine())return words(overlay_string(money_left()?20918:20958));
            if(shop()) return words(overlay_string(money_left()?0x7148:0x716f));
            if(!money_left() && session_.campaign_resources().items.empty()) return words(overlay_string(0x63ac));
            return words(overlay_string(money_left()?0x63ce:0x63e3)+overlay_string(can_detect()?0x63c1:0x63bb));
        }
        return {};
    }
    void show_items() {
        shown_items_.clear();for(const auto& item:session_.campaign_resources().items) shown_items_.push_back(item.id);
        selection_=0;item_top_=0;focus_=0;page_=Page::items;feedback_.clear();
    }
    void show_money() {
        shown_money_.clear();for(std::size_t i=0;i<7;++i)if(session_.campaign_resources().amounts[i])shown_money_.push_back(i);
        selection_=0;page_=Page::money;feedback_.clear();
    }
    void report(EconomyResult result) {feedback_=result.message;}
    void stock_page(bool next) {
        if((next && !next_stock_page()) || (!next && !previous_stock_page())) return;
        const auto row=selection_-item_top_;
        item_top_=next?std::min(item_top_+shop_rows,shown_items_.size()-shop_rows):item_top_>shop_rows?item_top_-shop_rows:0;
        selection_=item_top_+row;feedback_.clear();focus_=0;
    }
    void stock_row(bool next) {
        if(shown_items_.empty())return;
        const auto count=std::min(shop_rows,shown_items_.size()-item_top_);
        selection_=item_top_+(selection_-item_top_+(next?1:count-1))%count;feedback_.clear();
    }
    void take_stock() {
        if(shown_items_.empty())return;
        const auto result=shop()?session_.buy_item(shown_items_.at(selection_)):session_.take_campaign_item(shown_items_.at(selection_));
        report(result);
        if(result.applied() && shop()) {
            const auto& inventory=session_.world.party.member(*session_.active_character()).inventory();
            for(std::size_t i=0;i<inventory.size();++i)if(inventory.item(i).id()==result.item)
                feedback_="Bought "+inventory.item(i).name()+".";
        }
        if(result.applied() && !shop()) {
            shown_items_.erase(shown_items_.begin()+static_cast<std::ptrdiff_t>(selection_));
            if(shown_items_.empty()) {page_=Page::home;focus_=0;return;}
            selection_=std::min(selection_,shown_items_.size()-1);
            item_top_=std::min(item_top_,shown_items_.size()>shop_rows?shown_items_.size()-shop_rows:0);
        }
    }
    void stock_input(InputEvent event,std::uint8_t key) {
        if(event.key==Key::cancel || event.key==Key::backspace || key=='E' || key==' ') {page_=Page::home;focus_=0;feedback_.clear();return;}
        if(shop() && (key=='G' || key=='O')) {session_.select_campaign_member(key=='G'?-1:1);feedback_.clear();return;}
        if(event.key==Key::accept || key==(shop()?'B':'T')) {take_stock();return;}
        if(event.key==Key::home || event.key==Key::up) {stock_row(false);return;}
        if(event.key==Key::end || event.key==Key::down) {stock_row(true);return;}
        if(event.key==Key::next || key=='N') {stock_page(true);return;}
        if(event.key==Key::previous || key=='P') {stock_page(false);return;}
        const auto options=commands();
        if(key==',' || event.key==Key::left)focus_=(focus_+options.size()-1)%options.size();
        else if(key=='.' || event.key==Key::right)focus_=(focus_+1)%options.size();
    }
    void render_stock(Framebuffer& frame) const {
        frame.fill(ega_ui_palette[0]);screen_.chrome().draw(frame,ChromeLayout::merchant());
        const auto& font=screen_.font();const auto& title=overlay_string(shop()?0x6cf3:0x6080);
        font.draw_text(frame,title,8*((39-static_cast<int>(title.size()))/2),8,38,1,ega_ui_palette[15]);
        if(shop())if(const auto member=session_.active_character()) {
            const auto& character=session_.world.party.member(*member);
            font.draw_text(frame,character.name()+"  GOLD "+std::to_string(WealthRules::gold_value(character,session_.world.party.item_catalog())),8,16,38,1,GameFont::green);
            font.draw_text(frame,"POOL "+std::to_string(WealthRules::gold_value(session_.campaign_resources().amounts,session_.world.party.item_catalog()))+" GOLD",8,24,21,1,GameFont::green);
            draw_menu_actions(font,frame,"G/O CHARACTER",184,24,16,"GO");
        }
        const auto& items=session_.campaign_resources().items;
        for(std::size_t i=item_top_;i<shown_items_.size() && i<item_top_+shop_rows;++i) {
            const auto item=std::find_if(items.begin(),items.end(),[&](const auto& entry){return entry.id==shown_items_[i];});
            if(item==items.end())continue;
            // Native272F:0030 packs price first, then name, into string[30].
            std::string line;
            if(shop()) {
                line.assign(30,' ');const auto price=std::to_string(std::max<unsigned>(1,item->item.value()));
                line.replace(line.size()-price.size(),price.size(),price);
                const auto name=item->item.name();line.replace(0,std::min(name.size(),line.size()),name.substr(0,line.size()));
            } else {
                const auto detected=MagicEffectService::party_detects_magic(session_.world.party);
                line=item->item.name(detected.value_or(false));
            }
            const auto member=session_.active_character();
            const bool usable=!shop() || (member && (session_.world.party.item_catalog().definition(item->item.type()).class_mask() & session_.world.party.member(*member).byte(0xb0)));
            font.draw_text(frame,line,8,32+8*static_cast<int>(i-item_top_),shop()?30:38,1,ega_ui_palette[shop()?(usable?10:14):(i==selection_?15:10)]);
            if(shop() && i==selection_)font.draw_text(frame,">",256,32+8*static_cast<int>(i-item_top_),1,1,ega_ui_palette[15]);
        }
        if(!feedback_.empty()) {
            for(int y=152;y<184;++y)std::fill_n(frame.begin()+y*320+8,304,ega_ui_palette[0]);
            font.draw_text(frame,feedback_,8,152,38,4,GameFont::green,true);
        }
        const auto& prefix=overlay_string(shop()?0x6cf8:0x6085);font.draw_text(frame,prefix,0,192,40,1,ega_ui_palette[13]);
        int x=8*static_cast<int>(prefix.size());const auto options=commands();
        for(std::size_t i=0;i<options.size();++i) {
            font.draw_text(frame,options[i],x,192,(320-x)/8,1,ega_ui_palette[i==focus_%options.size()?15:10]);
            font.draw_text(frame,std::string_view(options[i]).substr(0,1),x,192,1,1,ega_ui_palette[15]);
            x+=8*static_cast<int>(options[i].size()+1);
        }
    }
    void leave(IGameFlow& flow) {
        try {session_.finish_campaign(token_);page_=Page::done;flow.pop_mode();}
        catch(const DecodeError& error) {feedback_=error.what();}
    }
    void activate(std::uint8_t key,IGameFlow& flow) {
        if(page_==Page::leave) {
            if(key=='Y') {page_=Page::home;focus_=0;feedback_.clear();}
            else if(key=='N') leave(flow);
            return;
        }
        if(page_==Page::take) {
            if(key=='M')show_money();else if(key=='I')show_items();else if(key=='E')page_=Page::home;
            return;
        }
        if(key=='E') {
            if(money_left() || (!merchant() && !session_.campaign_resources().items.empty())) {page_=Page::leave;focus_=0;}
            else leave(flow);
        } else if(key=='B' && shop()) show_items();
        else if(key=='H' && shrine())flow.push_mode(make_shrine_healing_mode(session_,screen_));
        else if(key=='T') {
            if(merchant() || (money_left() && session_.campaign_resources().items.empty()))show_money();
            else if(!money_left())show_items();
            else {page_=Page::take;focus_=0;}
        }
        else if(key=='P') report(session_.pool_money());
        else if(key=='S') report(session_.share_money());
        else if(key=='V' && session_.active_character()) {
            auto services=CharacterViewServices::for_exploration(session_,session_.character_portraits_enabled());
            services.shop=shop()?&session_:nullptr;
            services.money_pool=session_.money_pool_for_view();
            services.execution_mode=static_cast<std::uint8_t>(merchant()?1:6);
            services.combat_abort=abort_;
            flow.push_mode(make_character_view_mode(session_.world.party,*session_.active_character(),screen_.font(),session_.asset_files(),
                services));
        } else if(key=='A' && merchant()) {
            const auto member=session_.active_character();if(!member)return;
            const auto& character=session_.world.party.member(*member);
            if(!character.word(0x92) && !character.word(0x94))feedback_=overlay_string(0x26569);
            else {page_=Page::appraise;focus_=0;feedback_.clear();}
        }
        else if(key=='D' && can_detect()) {
            const auto result=session_.detect_treasure();feedback_=result.applied()?std::string{}:result.message;
            if(result.applied()) {detect_messages_=result.feedback;detect_message_=0;detect_elapsed_=0;detect_presented_=false;}
        }
        else if(key=='G' || key=='O') session_.select_campaign_member(key=='G'?-1:1);
    }
public:
    CampaignMode(ExplorationSession& session,const ExplorationScreen& screen)
        :session_(session),screen_(screen),kind_(session.campaign_kind().value()),token_(session.campaign_token().value()),
         combat_token_(session.combat_collection_token()),abort_(combat_token_?std::make_shared<CombatViewAbort>():nullptr) {
        if(!merchant()) {
            treasure_picture_=PicturePresentation::load(session_.asset_files(),session_.world.resource_disk,1,255);
            page_=session_.campaign_npc_members().empty()?Page::summary:Page::npc_shares;
        }
    }
    bool text_input_active() const override {return page_==Page::amount;}
    void handle_input(InputEvent event,IGameFlow& flow) override {
        if(!event.pressed || page_==Page::done)return;
        if(abort_ && event.key==Key::combat_abort) {abort_->requested=true;return;}
        if(abort_ && abort_->requested)return;
        if(detect_message_<detect_messages_.size())return;
        auto key=event.character;if(key>='a' && key<='z')key=static_cast<std::uint8_t>(key-'a'+'A');
        if(page_==Page::summary || page_==Page::npc_shares) {
            if(summary_presented_ && event.key==Key::accept){page_=page_==Page::npc_shares?Page::summary:Page::home;feedback_.clear();summary_presented_=false;}return;
        }
        if(page_==Page::amount) {
            if(event.key==Key::cancel){page_=Page::money;amount_.clear();return;}
            if(event.key==Key::backspace){if(!amount_.empty())amount_.pop_back();return;}
            if(event.key==Key::accept) {
                unsigned value{};const auto [end,error]=std::from_chars(amount_.data(),amount_.data()+amount_.size(),value);
                if(error!=std::errc{} || end!=amount_.data()+amount_.size() || value>65535){feedback_="Enter an amount from 0 to 65535.";return;}
                report(session_.take_money(shown_money_.at(selection_),static_cast<std::uint16_t>(value)));
                page_=Page::home;amount_.clear();return;
            }
            if(key>='0' && key<='9' && amount_.size()<5)amount_.push_back(static_cast<char>(key));
            return;
        }
        if(page_==Page::appraise || page_==Page::quote) {
            const auto options=commands();
            if(key==',' || event.key==Key::left) {if(!options.empty())focus_=(focus_+options.size()-1)%options.size();return;}
            if(key=='.' || event.key==Key::right) {if(!options.empty())focus_=(focus_+1)%options.size();return;}
            if(page_==Page::quote) {
                // Generic native menu returns 0/13/32 for Esc/Enter/Space;
                // 19B2 keeps only explicit K and sells every other return.
                if(event.key==Key::cancel || event.key==Key::accept || key==' ')key='S';
                if(key=='S' || key=='K') {
                    const auto result=session_.resolve_appraisal(key=='K');report(result);
                    if(result.applied()) {
                        const auto& character=session_.world.party.member(*session_.active_character());
                        page_=character.word(0x92) || character.word(0x94)?Page::appraise:Page::home;focus_=0;
                    }
                }
                return;
            }
            if(event.key==Key::cancel || event.key==Key::backspace || key=='E') {page_=Page::home;focus_=0;feedback_.clear();return;}
            if(key=='G' || key=='J') {
                const auto result=key=='J'?session_.appraise_jewelry():session_.appraise_gem();report(result);
                if(result.applied()){page_=Page::quote;focus_=0;}
            }
            return;
        }
        if(stock_items()) {stock_input(event,key);return;}
        if(page_==Page::items || page_==Page::money) {
            if(event.key==Key::cancel || event.key==Key::backspace || key=='E'){page_=Page::home;feedback_.clear();return;}
            const auto count=page_==Page::items?shown_items_.size():shown_money_.size();if(!count)return;
            if(event.key==Key::up)selection_=selection_?selection_-1:count-1;
            else if(event.key==Key::down)selection_=(selection_+1)%count;
            else if(event.key==Key::previous)selection_=selection_>=4?selection_-4:0;
            else if(event.key==Key::next)selection_=std::min(selection_+4,count-1);
            else if(event.key==Key::accept || key==(shop() && page_==Page::items?'B':'T')) {
                if(page_==Page::money){page_=Page::amount;amount_.clear();feedback_.clear();}
                else {
                    const auto id=shown_items_.at(selection_);const auto result=shop()?session_.buy_item(id):session_.take_campaign_item(id);report(result);
                    if(result.applied() && !shop()) {
                        shown_items_.erase(shown_items_.begin()+static_cast<std::ptrdiff_t>(selection_));
                        selection_=shown_items_.empty()?0:std::min(selection_,shown_items_.size()-1);
                    }
                }
            }return;
        }
        if(event.key==Key::cancel || event.key==Key::backspace){if(page_==Page::home)activate('E',flow);else if(page_==Page::take)page_=Page::home;return;}
        if(page_==Page::home && (event.key==Key::home || event.key==Key::end)) {session_.select_campaign_member(event.key==Key::home?-1:1);return;}
        const auto options=commands();
        if((key==',' || event.key==Key::left) && !options.empty())focus_=(focus_+options.size()-1)%options.size();
        else if((key=='.' || event.key==Key::right) && !options.empty())focus_=(focus_+1)%options.size();
        else if(event.key==Key::accept && !options.empty() && (merchant() || page_==Page::leave))activate(static_cast<std::uint8_t>(options.at(focus_%options.size())[0]),flow);
        else activate(key,flow);
    }
    void update(std::uint64_t elapsed,IGameFlow&) override {
        if(detect_message_<detect_messages_.size()) {
            if(!detect_presented_)return;
            const auto duration=RuntimeTiming::notification_dwell_ms;
            detect_elapsed_+=std::min(elapsed,duration-std::min(duration,detect_elapsed_));
            if(detect_elapsed_==duration) {++detect_message_;detect_elapsed_=0;detect_presented_=false;}
            return;
        }
        if(treasure_picture_ && page_!=Page::summary && page_!=Page::npc_shares && !stock_items())treasure_picture_->advance(elapsed);
    }
    void after_present(IGameFlow& flow) override {
        if(abort_ && abort_->requested) {
            if(!session_.abort_combat(*combat_token_))throw DecodeError("Combat reward rollback lost its source checkpoint");
            page_=Page::done;flow.pop_mode();return;
        }
        if(page_==Page::summary || page_==Page::npc_shares)summary_presented_=true;
        if(detect_message_<detect_messages_.size())detect_presented_=true;
    }
    void render(Framebuffer& frame) const override {
        if(stock_items()) {render_stock(frame);return;}
        if(page_==Page::summary) {
            frame.fill(ega_ui_palette[0]);screen_.chrome().draw(frame,ChromeLayout::merchant());
            const auto& font=screen_.font();
            const auto& outcome=session_.campaign_result_text_id();const auto& prefix=session_.campaign_experience_prefix_id();
            font.draw_text(frame,outcome.empty()?overlay_string(0x5e11):StringStore::active().get(outcome),8,24,38,1,GameFont::green);
            font.draw_text(frame,(prefix.empty()?overlay_string(0x5e45):StringStore::active().get(prefix))+
                std::to_string(session_.campaign_experience().value_or(0)),8,40,38,1,GameFont::green);
            font.draw_text(frame,overlay_string(0x5e5e),8,56,38,1,GameFont::green);
            font.draw_text(frame,overlay_string(0x5e71),0,192,40,1,menu_action_color);return;
        }
        if(page_==Page::npc_shares) {
            frame.fill(ega_ui_palette[0]);screen_.chrome().draw(frame,ChromeLayout::merchant());
            // Native25C7:12E8 clears the full content area, starts at row/col5,
            // and advances two text rows per NPC. Do not overlay the HUD frame.
            const auto& font=screen_.font();const auto prefix=overlay_string(0x6826);
            std::size_t row=0;
            for(const auto member:session_.campaign_npc_members()) {
                font.draw_text(frame,prefix+session_.world.party.member(member).name()+overlay_string(0x6827),
                    40,40+16*static_cast<int>(row++),30,2,GameFont::green,true);
            }
            font.draw_text(frame,overlay_string(0x6843),0,192,40,1,menu_action_color);return;
        }
        ExplorationUiState ui;ui.command_line=std::string{};ui.picture_override=treasure_picture_?&*treasure_picture_:nullptr;screen_.draw(frame,session_,ui);
        if(!merchant())for(int y=120;y<128;++y)std::fill_n(frame.begin()+y*320+136,176,ega_ui_palette[0]);
        for(int y=136;y<184;++y)std::fill_n(frame.begin()+y*320+8,304,ega_ui_palette[0]);
        const auto& font=screen_.font();
        const auto draw=[&](std::string_view value,int y,int lines=1){font.draw_text(frame,value,8,y,38,lines,GameFont::green,true);};
        if(detect_message_<detect_messages_.size()) {
            const auto& notice=detect_messages_[detect_message_];
            draw(session_.world.party.member(notice.recipient).name(),144);
            draw(StringStore::active().get(notice.text_id),152,4);return;
        }
        if(page_==Page::appraise || page_==Page::quote) {
            const auto member=session_.active_character();
            if(member) {
                const auto& character=session_.world.party.member(*member);
                draw(overlay_string(0x265ac),136);
                if(character.word(0x92))draw(std::to_string(character.word(0x92))+overlay_string(character.word(0x92)==1?0x2657c:0x26581),144);
                if(character.word(0x94))draw(std::to_string(character.word(0x94))+overlay_string(character.word(0x94)==1?0x26587:0x26599),152);
            }
            if(page_==Page::quote && session_.appraisal_quote())
                draw(overlay_string(session_.appraisal_quote()->jewelry()?0x26623:0x265ee)+std::to_string(session_.appraisal_quote()->gold())+overlay_string(0x26604),160);
            if(!feedback_.empty())draw(feedback_,168,2);
        }
        else if(page_==Page::leave) {draw(overlay_string(shrine()?20997:shop()?0x7194:0x63fb)+overlay_string(shrine()?21072:shop()?0x71e7:0x641b),136,5);}
        else if(page_==Page::items) {
            draw(overlay_string(shop()?0x6cf3:0x6080)+" "+std::to_string(shown_items_.empty()?0:selection_+1)+"/"+std::to_string(shown_items_.size()),136);
            const auto first=selection_/4*4;
            for(std::size_t i=first;i<shown_items_.size() && i<first+4;++i) {
                const auto id=shown_items_[i];
                const auto& items=session_.campaign_resources().items;
                const auto item=std::find_if(items.begin(),items.end(),[&](const auto& entry){return entry.id==id;});
                if(item==items.end())continue;
                const auto line=(i==selection_?">":" ")+item->item.name()+(shop()?" "+std::to_string(std::max<unsigned>(1,item->item.value())):"");
                draw(line,144+8*static_cast<int>(i-first));
            }
        } else if(page_==Page::money) {
            draw(overlay_string(0x25946),136);
            const auto first=selection_/4*4;
            for(std::size_t i=first;i<shown_money_.size() && i<first+4;++i) {
                const auto index=shown_money_[i];draw(std::string(i==selection_?">":" ")+denomination(index)+" "+std::to_string(session_.campaign_resources().amounts[index]),144+8*static_cast<int>(i-first));
            }
        } else if(page_==Page::amount)draw(overlay_string(0x2595a)+denomination(shown_money_.at(selection_))+" "+overlay_string(0x25964)+amount_,144,4);
        else if(!feedback_.empty())draw(feedback_,136,5);
        else if(shop())draw(overlay_string(0x6cf3),136);
        if((page_==Page::items || page_==Page::money) && !feedback_.empty())draw(feedback_,176);
        const auto options=commands();int x=0;
        if(page_==Page::appraise || page_==Page::quote) {
            const auto& prefix=overlay_string(page_==Page::appraise?0x265e2:0x26618);
            font.draw_text(frame,prefix,0,192,40,1,ega_ui_palette[13]);x=static_cast<int>(prefix.size())*8;
        }
        if(page_==Page::take) {const auto& prefix=overlay_string(0x629e);font.draw_text(frame,prefix,0,192,40,1,ega_ui_palette[13]);x=static_cast<int>(prefix.size())*8;}
        for(std::size_t i=0;i<options.size();++i) {
            font.draw_text(frame,options[i],x,192,(320-x)/8,1,i==focus_%options.size()?ega_ui_palette[15]:GameFont::green);
            font.draw_text(frame,std::string_view(options[i]).substr(0,1),x,192,1,1,
                menu_action_color);
            x+=static_cast<int>(options[i].size()+1)*8;
        }
        if(options.empty() && (page_==Page::items || page_==Page::money || page_==Page::amount))
            font.draw_text(frame,"ARROWS / ENTER / ESC",0,192,40,1,menu_action_color);
    }
    std::string title() const override {
        const auto base=std::string("PoolRad | ")+(shrine()?"shrine":shop()?"shop":"treasure");
        if(page_==Page::items)return base+" | items";
        if(page_==Page::leave)return base+" | return query";
        if(page_==Page::money)return base+" | money";
        if(page_==Page::amount)return base+" | amount";
        if(page_==Page::take)return base+" | take";
        if(page_==Page::appraise)return base+" | appraise";
        if(page_==Page::quote)return base+" | appraisal quote";
        if(page_==Page::summary)return base+" | summary";
        if(page_==Page::npc_shares)return base+" | NPC shares";
        return base;
    }
};
}
std::unique_ptr<IGameMode> make_campaign_mode(ExplorationSession& session,const ExplorationScreen& screen) {
    if(!session.campaign_kind() || !session.campaign_token())throw DecodeError("Campaign mode requires a staged service");
    return std::make_unique<CampaignMode>(session,screen);
}
}
