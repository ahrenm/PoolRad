#include "gbengine/modes/modes.hpp"
#include "gbengine/party/character_creation_service.hpp"
#include "gbengine/party/training_service.hpp"
#include "gbengine/rendering/character_portrait.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/ui/exploration_screen.hpp"
#include "gbengine/saves/port_save.hpp"
#include "gbengine/saves/save_repository.hpp"
#include <algorithm>
#include <sstream>

namespace GBEngine {
namespace {
const std::string& resident(unsigned at){return StringStore::active().get(pascal_string_id("START.EXE:module",at));}
const std::string& creation(unsigned at){return overlay_string(0x1997f+at);}
struct Credit {unsigned at;int x,y;unsigned colour;};
constexpr std::array<Credit,33> credits{{
    {0x4b,5,1,10},{0x60,26,1,14},{0x6a,15,2,10},{0x73,2,3,10},
    {0x98,1,5,10},{0xa9,18,5,14},{0xbe,14,6,11},{0xca,2,7,14},{0xd7,20,7,14},
    {0xe0,2,8,11},{0xca,20,8,14},{0xed,2,9,11},{0xf8,20,9,11},{0x104,2,10,11},{0xed,20,10,11},
    {0x10f,2,11,11},{0x119,2,13,14},{0x127,20,13,14},{0x139,2,14,11},{0x142,20,14,11},
    {0x14e,2,15,11},{0x104,20,15,11},{0x159,2,16,11},{0x167,20,16,11},{0x175,2,17,11},
    {0x184,20,17,11},{0x191,2,19,14},{0x1a1,20,19,14},{0x167,2,20,11},{0x1ac,20,20,11},
    {0x1be,2,22,14},{0x1c7,11,22,11},{0x1e2,2,23,11}}};
class StartupMode final:public IGameMode {
    enum class Page {title,credits,wheel,cale,race,sex,character_class,alignment,stats,name,portrait,add,remove,exit,icon,save_slot,overwrite,train,learn,feedback,modify,game_title};
    const IFileSystem& files_;
    LaunchOptions options_;std::ostream& output_;
    ISaveRepository* saves_;ISaveRepository* characters_;
    CharacterCreationService rules_;
    TrainingService training_;
    ExplorationSession* owner_=nullptr;
    std::uint64_t owner_revision_=0,owner_token_=0;
    std::optional<TrainingPlan> training_plan_;
    std::vector<std::uint8_t> learn_;
    std::optional<Character> modification_;
    std::size_t modified_member_=0;
    ExplorationScreen screen_;
    Bytes module_;
    Image title_,game_title_;
    Page page_=Page::title;
    std::vector<std::pair<char,Character>> library_;
    std::vector<char> party_slots_;
    std::optional<Party> party_,draft_party_,candidate_view_;
    std::optional<CharacterDraft> draft_;
    std::mt19937 random_;
    CreationChoices choices_;
    std::size_t focus_=0;
    std::string name_,notice_;
    std::uint8_t head_=1,body_=1;
    char save_slot_='A';
    std::optional<CharacterPortrait> portrait_;
    std::uint64_t elapsed_=0;
    bool presented_=false;
    mutable bool rendered_=false;
    void page(Page value){page_=value;focus_=value==Page::stats?1:0;elapsed_=0;presented_=false;rendered_=false;notice_.clear();}
    void next_intro(){page(page_==Page::title?Page::game_title:page_==Page::game_title?Page::credits:page_==Page::credits?Page::wheel:Page::cale);}
    void text(Framebuffer& frame,std::string_view value,int x,int y,unsigned color=10,int columns=38,int lines=1)const {
        screen_.font().draw_text(frame,value,x,y,columns,lines,ega_ui_palette.at(color),true);
    }
    void hotkey(Framebuffer& frame,std::string_view label,int x,int y)const {
        text(frame,label,x,y);
        if(!label.empty())text(frame,label.substr(0,1),x,y,15,1);
    }
    void footer(Framebuffer& frame,std::string_view labels,int x=0,bool confirmation=false)const {
        std::istringstream input{std::string(labels)};std::string label;std::size_t i=0;
        while(input>>label){hotkey(frame,label,x,192);if(confirmation && i==focus_)text(frame,label,x,192,15);x+=static_cast<int>((label.size()+1)*8);++i;}
    }
    void candidate_sheet(Framebuffer& frame)const {
        if(!candidate_view_)return;
        const auto& c=candidate_view_->member(0);const auto s=c.stats();
        screen_.chrome().draw(frame,ChromeLayout::character_view(true));
        if(portrait_)portrait_->draw(frame,224,8);
        if(page_==Page::portrait)text(frame,name_,8,8,15);
        text(frame,std::string(c.sex_name())+" "+std::string(c.race_name())+" "+overlay_string(0x21040)+std::to_string(s.age),8,24,15);
        text(frame,c.alignment_name(),8,32,15);text(frame,c.class_name(),8,40,15);
        for(unsigned i=0;i<6;++i){const auto y=56+static_cast<int>(i)*8;text(frame,overlay_string(0x21045+i*5u),8,y);text(frame,std::to_string(s.abilities[i]),s.abilities[i]>=10?40:48,y);
            if(i==0 && s.abilities[i]==18 && s.exceptional_strength){auto exceptional=std::to_string(s.exceptional_strength);if(s.exceptional_strength==100)exceptional=overlay_string(0x21934);else if(s.exceptional_strength<10)exceptional=overlay_string(0x21932)+exceptional;text(frame,overlay_string(0x21937)+exceptional+overlay_string(0x21939),56,y);}}
        int money_y=56;for(unsigned i=0;i<7;++i){const auto index=6-i;if(!s.money[index])continue;const auto& label=resident(0xd7ba+11*index);text(frame,label,8*(20-static_cast<int>(label.size())),money_y);text(frame,std::to_string(s.money[index]),168,money_y);money_y+=8;}
        std::string levels;for(const auto level:s.levels)if(level && level<128){if(!levels.empty())levels+=overlay_string(0x21069);levels+=std::to_string(level);}
        text(frame,overlay_string(0x21063),8,120,15);text(frame,levels,56,120,15);text(frame,overlay_string(0x2106b)+std::to_string(s.experience),136,120,15);
        text(frame,overlay_string(0x215f8),8,136,15);text(frame,std::to_string(s.armor_class),32,136);
        text(frame,overlay_string(0x21606),72,136,15);text(frame,std::to_string(s.thac0),120,136);
        text(frame,overlay_string(0x21624),176,136,15);text(frame,std::to_string(s.encumbrance),272,136);
        text(frame,overlay_string(0x215ff),8,144,15);text(frame,std::to_string(s.current_hp),32,144);
        auto damage=std::to_string(s.damage_dice)+overlay_string(0x2160f)+std::to_string(s.damage_sides);
        if(s.damage_bonus)damage+=overlay_string(s.damage_bonus>0?0x21611:0x21613)+std::to_string(s.damage_bonus<0?-s.damage_bonus:s.damage_bonus);
        text(frame,overlay_string(0x21615),64,144,15);text(frame,damage,120,144);
        text(frame,overlay_string(0x21636),200,144,15);text(frame,std::to_string(s.movement),272,144);
        text(frame,overlay_string(0x2107d),8,176,15);text(frame,character_status_name(c.status()),64,176);
    }
    void refresh_candidate(){candidate_view_=rules_.assemble(files_,std::span<const Character>(&draft_->character,1));refresh_portrait();}
    void discover() {
        library_.clear();
        if(!characters_)return;
        for(char slot='A';slot<='J';++slot)if(characters_->occupied(slot)) {
            try {
                library_.push_back({slot,rules_.decode_character(files_,characters_->load(slot))});
            }catch(const std::exception& error){output_<<"Character library "<<slot<<": "<<error.what()<<'\n';}
        }
    }
    std::unique_ptr<ExplorationSession> campaign(Party party)const {
        auto session=std::make_unique<ExplorationSession>(files_,std::move(party),random_);session->pump();
        return session;
    }
    std::vector<std::uint8_t> choices()const {
        if(page_==Page::race)return {1,2,3,4,5,7};
        if(page_==Page::sex)return {0,1};
        if(page_==Page::character_class)return rules_.classes(choices_.race);
        if(page_==Page::alignment)return rules_.alignments(choices_.character_class);
        return {};
    }
    std::vector<std::string> labels()const {
        std::vector<std::string> result;
        if(page_==Page::add){for(const auto& item:library_)result.push_back(item.second.name());return result;}
        if(page_==Page::remove){if(party_)for(std::size_t i=0;i<party_->size();++i)result.push_back(party_->member(i).name());return result;}
        if(page_==Page::learn){for(const auto id:learn_)result.push_back(std::string(ItemCatalog::spell_name(id)));return result;}
        for(const auto value:choices()) {
            const unsigned at=page_==Page::race?0xd6c3+value*10u:page_==Page::sex?0xd7ac+value*7u:
                page_==Page::character_class?0xd4f8+value*27u:0xd713+value*17u;
            result.push_back(resident(at));
        }
        return result;
    }
    void select() {
        const auto values=choices();if(values.empty() || focus_>=values.size())return;
        if(page_==Page::race){choices_.race=values[focus_];page(Page::sex);}
        else if(page_==Page::sex){choices_.sex=values[focus_];page(Page::character_class);}
        else if(page_==Page::character_class){choices_.character_class=values[focus_];page(Page::alignment);}
        else if(page_==Page::alignment){choices_.alignment=values[focus_];draft_=rules_.begin(choices_,random_);refresh_candidate();page(Page::stats);}
    }
    void refresh_portrait(){
        if(!draft_)throw DecodeError("Portrait has no owned draft");
        auto c=draft_->character;c.set_byte(0xbb,head_);c.set_byte(0xbc,body_);
        portrait_=CharacterPortrait::load(files_,c);presented_=false;rendered_=false;
    }
    void store_character() {
        if(!characters_ || !draft_party_ || !draft_)throw DecodeError("Character storage is unavailable");
        char slot='A';while(slot<='J' && characters_->occupied(slot))++slot;
        if(slot>'J')throw DecodeError("The character library is full");
        const auto bytes=rules_.encode_character(files_,draft_party_->member(0));
        characters_->save(slot,bytes); // Immutable generation publication/readback belongs to repository.
        random_=draft_->random;discover();page(Page::cale);
    }
    void open_icon(IGameFlow& flow) {
        const auto member=rules_.finish(*draft_,name_,head_,body_);
        draft_party_=rules_.assemble(files_,std::span<const Character>(&member,1));
        page(Page::icon);
        flow.push_mode(make_creation_icon_mode(*draft_party_,files_,screen_,[this]{store_character();}));
    }
    void save_party() {
        if(!saves_ || !party_)throw DecodeError("Campaign storage is unavailable");
        throw DecodeError("Begin adventuring and complete the welcome before saving a campaign");
    }
    void offer_training() {
        if(!owner_ || !party_ || !party_->selected_index())return;
        training_plan_=training_.prepare(*party_,*party_->selected_index(),owner_->vm.read_value(0x6da8));
        if(training_plan_->status==TrainingStatus::ready)page(Page::train);
        else {
            const auto at=training_plan_->status==TrainingStatus::inactive?0x28eeu:training_plan_->status==TrainingStatus::insufficient_money?0x290du:
                training_plan_->status==TrainingStatus::wrong_school?0x2925u:0x2944u;
            page(Page::feedback);notice_=creation(at);
        }
    }
    void train(std::optional<std::uint8_t> spell) {
        if(!party_ || !training_plan_)throw DecodeError("Training confirmation is unavailable");
        training_.train(*party_,*training_plan_,spell,random_);training_plan_.reset();
        page(Page::feedback);notice_=creation(0x299b);
    }
public:
    StartupMode(const IFileSystem& files,const LaunchOptions& options,std::ostream& output,ISaveRepository* saves,ISaveRepository* characters,bool cale_only=false)
        :files_(files),options_(options),output_(output),saves_(saves),characters_(characters),rules_(CharacterCreationService::load(files)),training_(TrainingService::load(files)),
         screen_(ExplorationScreen::load(files)),module_(decode_start_data(files.read_asset("START.EXE"))),random_(static_cast<std::uint32_t>(options.random_seed)) {
        const auto frames=decode_ega(load_record(files,"TITLE.DAX",1),"TITLE.DAX");
        if(frames.size()!=1 || frames[0].width!=320 || frames[0].height!=200)throw DecodeError("Invalid native title presentation");
        title_=frames[0];
        const auto second=decode_ega(load_record(files,"TITLE.DAX",2),"TITLE.DAX");
        if(second.size()!=1 || second[0].width!=320 || second[0].height!=200)throw DecodeError("Invalid native game title presentation");
        game_title_=second[0];discover();if(cale_only)page(Page::cale);
    }
    StartupMode(ExplorationSession& owner,const IFileSystem& files,const LaunchOptions& options,std::ostream& output,ISaveRepository* saves,ISaveRepository* characters)
        :StartupMode(files,options,output,saves,characters) {
        if(!owner.character_services_pending())throw DecodeError("Character menu requires its held ECL38 request");
        owner_=&owner;owner_revision_=owner.world.party.revision();owner_token_=owner.vm.pending_request()->token;
        party_=owner.world.party;party_slots_.assign(party_->size(),0);random_=owner.vm.random_state();page(Page::cale);
        if(!party_->size())party_.reset();
    }
    bool text_input_active()const override{return page_==Page::name;}
    void handle_input(InputEvent event,IGameFlow& flow)override {
        if(!event.pressed || !presented_)return;
        auto key=event.character;if(key>='a' && key<='z')key=static_cast<std::uint8_t>(key-'a'+'A');
        try {
            if(page_==Page::title || page_==Page::game_title || page_==Page::credits || page_==Page::wheel) {
                if((page_!=Page::wheel && (event.key!=Key::none || key)) || event.key==Key::accept)next_intro();
            } else if(page_==Page::cale) {
                if(key=='C'){draft_.reset();draft_party_.reset();name_.clear();head_=body_=1;page(Page::race);}
                else if(key=='A'){discover();page(Page::add);}
                else if(key=='L' && !owner_ && !party_)flow.push_mode(make_load_mode(files_,options_,output_,saves_,[&flow]{flow.pop_mode();},characters_));
                else if(key=='E' || event.key==Key::cancel)page(Page::exit);
                else if(key=='R' && party_)page(Page::remove);
                else if(key=='M' && party_) {
                    modified_member_=party_->selected_index().value();const auto& c=party_->member(modified_member_);
                    if(c.word(0xac) || c.word(0xae))throw DecodeError(c.name()+creation(0x206e));
                    modification_=c;page(Page::modify);
                }
                else if(key=='V' && party_){CharacterViewServices service;service.read_only=true;flow.push_mode(make_character_view_mode(*party_,party_->selected_index().value(),screen_.font(),files_,service));}
                else if(key=='B' && party_ && party_->size()) {
                    if(owner_){owner_->finish_character_services(owner_token_,owner_revision_,*party_,random_);flow.pop_mode();}
                    else {auto session=campaign(*party_);auto options=options_;options.mode=RunMode::exploration;options.disk=3;options.record=0;flow.change_mode(make_exploration_mode(files_,options,output_,saves_,std::move(session),characters_));}
                }
                else if(key=='T' && party_ && owner_ && owner_->vm.read_value(0x6da8))offer_training();
                else if(key=='S' && party_ && saves_ && !owner_)page(Page::save_slot);
                else if(party_ && (event.key==Key::up || event.key==Key::down)){const auto n=party_->size();party_->select((party_->selected_index().value()+n+(event.key==Key::down?1:n-1))%n);}
            } else if(page_==Page::modify) {
                if(event.key==Key::up || event.key==Key::down || event.key==Key::accept)focus_=(focus_+7+(event.key==Key::up?6:1))%7;
                else if(event.key==Key::left || event.key==Key::right)rules_.adjust(*party_,modified_member_,static_cast<unsigned>(focus_),event.key==Key::right);
                else if(key=='K' || key=='E' || event.key==Key::cancel) {
                    rules_.finish_modification(*party_,modified_member_,*modification_,key=='K');modification_.reset();page(Page::cale);
                }
            } else if(page_==Page::feedback) {return;}
            else if(page_==Page::train) {
                if(key=='N' || event.key==Key::cancel){training_plan_.reset();page(Page::cale);}
                else if(key=='Y') {
                    learn_=training_.learning_options(*party_,*training_plan_);
                    if(learn_.empty())train({});else page(Page::learn);
                }
            } else if(page_==Page::learn) {
                if(event.key==Key::up || event.key==Key::down || event.key==Key::home || event.key==Key::end) {
                    const auto n=learn_.size();if(n)focus_=event.key==Key::home?0:event.key==Key::end?n-1:(focus_+n+(event.key==Key::down?1:n-1))%n;
                } else if(event.key==Key::accept && focus_<learn_.size())train(learn_[focus_]);
            } else if(page_==Page::save_slot) {
                if(event.key==Key::cancel)page(Page::cale);
                else if(key>='A' && key<='J'){save_slot_=static_cast<char>(key);if(saves_->occupied(save_slot_))page(Page::overwrite);else save_party();}
            } else if(page_==Page::overwrite) {
                if(key=='Y')save_party();else if(key=='N' || event.key==Key::cancel)page(Page::cale);
            } else if(page_==Page::exit) {
                if(key=='Y')flow.quit();else if(key=='N' || event.key==Key::cancel)page(Page::cale);
            } else if(page_==Page::name) {
                if(event.key==Key::cancel){draft_.reset();page(Page::cale);}
                else if(event.key==Key::backspace){if(!name_.empty())name_.pop_back();}
                else if(event.key==Key::accept && !name_.empty()){refresh_portrait();page(Page::portrait);}
                else if(event.character>=32 && event.character<=126 && name_.size()<15)name_+=static_cast<char>(event.character);
            } else if(page_==Page::stats) {
                if(event.key==Key::left || event.key==Key::right){focus_=event.key==Key::left?0:1;return;}
                if(event.key==Key::accept)key=focus_==0?'Y':'N';
                if(key=='Y'){name_.clear();page(Page::name);}
                else if(key=='N'){rules_.reroll(*draft_);refresh_candidate();focus_=1;presented_=false;rendered_=false;}
                else if(event.key==Key::cancel){draft_.reset();page(Page::cale);}
            } else if(page_==Page::portrait) {
                if(key=='H'){head_=static_cast<std::uint8_t>(head_==14?1:head_+1);refresh_portrait();}
                else if(key=='B'){body_=static_cast<std::uint8_t>(body_==12?1:body_+1);refresh_portrait();}
                else if(key=='K')open_icon(flow);
                else if(event.key==Key::cancel){draft_.reset();page(Page::cale);}
            } else if(event.key==Key::cancel || event.key==Key::backspace){draft_.reset();page(Page::cale);}
            else if(event.key==Key::up || event.key==Key::down || event.key==Key::home || event.key==Key::end) {
                const auto count=labels().size();if(count)focus_=event.key==Key::home?0:event.key==Key::end?count-1:(focus_+count+(event.key==Key::down?1:count-1))%count;
            } else if(event.key==Key::accept) {
                if(page_==Page::add) {
                    if(focus_<library_.size() && party_slots_.size()<6 && std::find(party_slots_.begin(),party_slots_.end(),library_[focus_].first)==party_slots_.end()) {
                        if(party_ && party_->size())rules_.append(*party_,library_[focus_].second);
                        else party_=rules_.assemble(files_,std::span<const Character>(&library_[focus_].second,1));
                        party_slots_.push_back(library_[focus_].first);page(Page::cale);
                    }
                } else if(page_==Page::remove) {
                    if(focus_<party_slots_.size()){
                        auto candidate=*party_;rules_.remove(candidate,focus_);
                        if(const auto slot=party_slots_[focus_];slot && characters_){const auto bytes=rules_.encode_character(files_,party_->member(focus_));characters_->save(slot,bytes);}
                        party_=std::move(candidate);party_slots_.erase(party_slots_.begin()+static_cast<std::ptrdiff_t>(focus_));if(!party_->size())party_.reset();discover();page(Page::cale);
                    }
                } else select();
            }
        }catch(const std::exception& error){notice_=error.what();presented_=false;rendered_=false;}
    }
    void update(std::uint64_t elapsed,IGameFlow&)override {
        if(page_==Page::feedback && presented_) {
            elapsed_=std::min<std::uint64_t>(900,elapsed_+std::min<std::uint64_t>(elapsed,900));
            if(elapsed_>=900)page(Page::cale);
            return;
        }
        if(!presented_ || (page_!=Page::title && page_!=Page::game_title && page_!=Page::credits))return;
        const auto duration=page_==Page::credits?10000u:5000u;
        elapsed_=std::min<std::uint64_t>(duration,elapsed_+std::min<std::uint64_t>(elapsed,duration));
        if(elapsed_>=duration)next_intro();
    }
    void render(Framebuffer& frame)const override {
        frame.fill(ega_ui_palette[0]);
        if(page_==Page::title || page_==Page::game_title){const auto& picture=page_==Page::title?title_:game_title_;std::copy(picture.pixels.begin(),picture.pixels.end(),frame.begin());}
        else if(page_==Page::credits) {
            screen_.chrome().draw(frame,ChromeLayout{{{1,1,38,3},{1,5,38,23}}});
            for(const auto row:credits)text(frame,overlay_string(0xd4+row.at),row.x*8,row.y*8,row.colour,40-row.x);
        } else if(page_==Page::wheel) {
            screen_.chrome().draw(frame,ChromeLayout::character_creation());
            for(const auto row:std::array<std::pair<unsigned,int>,5>{{{1,1},{0x27,2},{0x36,5},{0x5a,8},{0x7c,11}}})text(frame,overlay_string(0x9ac+row.first),8,row.second*8);
            const auto glyph=[&](unsigned at,int x,int y){const auto n=module_.at(0xc7c0+at);if(n!=255)screen_.font().draw_glyph(frame,n+64,x*8,y*8,ega_ui_palette[12]);};
            glyph(9,8,6);glyph(10,9,6);glyph(11,8,9);glyph(12,9,9);
            text(frame,"PRESS ENTER TO CONTINUE",8,192,15); // Retired challenge: deliberate host policy.
        } else {
            screen_.chrome().draw(frame,ChromeLayout::character_creation());
            if(page_==Page::cale) {
                if(party_){text(frame,resident(0x12fb),8,16,15);text(frame,resident(0x1300),264,16,15);
                    for(std::size_t i=0;i<party_->size();++i){const auto& c=party_->member(i);const auto y=32+static_cast<int>(i)*8;text(frame,c.name(),8,y,i==party_->selected_index()?15:10);const auto ac=std::to_string(c.stats().armor_class),hp=std::to_string(c.hit_points());text(frame,ac,280-static_cast<int>(ac.size()*8),y);text(frame,hp,312-static_cast<int>(hp.size()*8),y);}}
                std::string keys=party_?"CMVARSBE":"CALE";
                if(owner_){keys.erase(std::remove(keys.begin(),keys.end(),'S'),keys.end());keys.erase(std::remove(keys.begin(),keys.end(),'L'),keys.end());if(owner_->vm.read_value(0x6da8))keys+='T';}
                const std::string all="CDMTVARLSBE";int row=12;
                for(std::size_t i=0;i<all.size();++i)if(keys.find(all[i])!=std::string::npos)hotkey(frame,resident(0xcdd9+static_cast<unsigned>(i)*42u),16,row++*8);
                text(frame,creation(0xe3),0,192,13);
            } else if(page_==Page::race || page_==Page::sex || page_==Page::character_class || page_==Page::alignment || page_==Page::add || page_==Page::remove || page_==Page::learn) {
                const bool creation_choice=page_==Page::race || page_==Page::sex || page_==Page::character_class || page_==Page::alignment;
                if(!creation_choice)text(frame,page_==Page::learn?std::to_string(learn_.size())+" "+overlay_string(0x239a0):resident(page_==Page::add?0xceab:0xced5),8,8,15);
                const auto rows=labels();const auto first=focus_>17?focus_-17:0;
                for(std::size_t i=first;i<rows.size() && i<first+18;++i)text(frame,rows[i],creation_choice?24:16,(creation_choice?16:24)+static_cast<int>(i-first)*8,i==focus_?15:10);
                text(frame,overlay_string(0x21ac2),8,192,15);
            } else if(page_==Page::stats && draft_) {
                candidate_sheet(frame);const auto& prompt=creation(0x545);text(frame,prompt,0,192,13);footer(frame,overlay_string(0x2d979),static_cast<int>(prompt.size()*8),true);
            } else if(page_==Page::name){candidate_sheet(frame);const auto& prompt=creation(0x55b);text(frame,prompt,0,192,13);text(frame,name_,static_cast<int>(prompt.size()*8),192,15);GameFont::draw_caret(frame,static_cast<int>((prompt.size()+name_.size())*8),192);}
            else if(page_==Page::portrait){candidate_sheet(frame);footer(frame,creation(0x33d8));}
            else if(page_==Page::exit)text(frame,resident(0xcf7d)+"? Y/N",8,24,15);
            else if(page_==Page::save_slot)text(frame,"SAVE GAME: A B C D E F G H I J",8,24,15);
            else if(page_==Page::overwrite)text(frame,"REPLACE SAVE "+std::string(1,save_slot_)+"? Y/N",8,24,15);
            else if(page_==Page::train)text(frame,creation(0x2984)+"Y/N",8,24,15);
            else if(page_==Page::modify && party_) {
                const auto& c=party_->member(modified_member_);text(frame,creation(0x2082)+c.name(),8,16,15);
                for(unsigned i=0;i<7;++i){text(frame,i<6?overlay_string(0x21045+i*5u):std::string("HP"),8,40+static_cast<int>(i)*8,i==focus_?15:10);text(frame,std::to_string(c.byte(i<6?0x10+i:0x32)),64,40+static_cast<int>(i)*8,i==focus_?15:10);}
                text(frame,creation(0x2095),8,192,15);
            }
            if(!notice_.empty())text(frame,notice_,8,160,14,38,3);
        }
        rendered_=true;
    }
    void after_present(IGameFlow&)override{if(rendered_){rendered_=false;presented_=true;}}
    void on_suspend()override{presented_=false;rendered_=false;}
    void on_resume()override{presented_=false;rendered_=false;}
    std::string title()const override {
        static constexpr std::array names{"title","credits","copy protection (retired)","CALE","race","gender","class","alignment","abilities","name","portrait","add character","remove character","exit","icon","save game","replace save","training","learn spell","feedback","modify character","Pool of Radiance title"};
        return std::string("PoolRad | ")+names[static_cast<unsigned>(page_)];
    }
};
}
std::unique_ptr<IGameMode> make_startup_mode(const IFileSystem& files,const LaunchOptions& options,std::ostream& out,ISaveRepository* saves,ISaveRepository* characters) {
    return std::make_unique<StartupMode>(files,options,out,saves,characters);
}
std::unique_ptr<IGameMode> make_cale_mode(const IFileSystem& files,const LaunchOptions& options,std::ostream& out,ISaveRepository* saves,ISaveRepository* characters) {
    auto fresh=options;fresh.mode=RunMode::startup;fresh.resume_slot.clear();fresh.save_slot.clear();fresh.load_menu=false;
    fresh.scenario_fixture.clear();fresh.party_fixture.clear();
    return std::make_unique<StartupMode>(files,fresh,out,saves,characters,true);
}
std::unique_ptr<IGameMode> make_character_services_mode(ExplorationSession& session,const IFileSystem& files,const LaunchOptions& options,std::ostream& out,ISaveRepository* saves,ISaveRepository* characters) {
    return std::make_unique<StartupMode>(session,files,options,out,saves,characters);
}
}
