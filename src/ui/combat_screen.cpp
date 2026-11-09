#include "gbengine/ui/menu_hotkeys.hpp"
#include "gbengine/ui/combat_screen.hpp"
#include "gbengine/rendering/combat_actor_art.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <sstream>

namespace GBEngine {
namespace {
const std::string& resident(std::size_t at) {
    return StringStore::active().get(pascal_string_id("START.EXE:module",at));
}
std::vector<std::string> wrapped_name(std::string_view text) {
    std::istringstream words{std::string(text)};std::string word;std::vector<std::string> lines(1);
    while(words>>word) {
        if(!lines.back().empty() && lines.back().size()+1+word.size()>16)lines.emplace_back();
        if(!lines.back().empty())lines.back()+=' ';
        for(const auto ch:word) {
            if(lines.back().size()==16)lines.emplace_back();
            lines.back()+=ch;
        }
    }
    if(lines.size()>3)lines.resize(3);
    return lines;
}
std::string command_captions(const DeploymentActor& actor,bool casting_forbidden) {
    const auto& character=actor.character;
    std::string result;
    if(actor.extension[6])result+=overlay_string(0x9e00);
    result+=overlay_string(0x9e06);
    if(character.inventory().size())result+=overlay_string(0x9e10);
    bool spells=false;for(std::size_t slot=0;slot<21;++slot)spells=spells || character.byte(0x17+slot)!=0;
    if(spells && actor.extension[1] && !casting_forbidden)result+=overlay_string(0x9e15);
    if(character.byte(0x96)>0 && character.byte(0x96)<128 && !actor.extension[0x11])result+=overlay_string(0x9e1b);
    return result+overlay_string(0x9e21);
}
}
struct CombatVisualResources {
    CombatTerrainAssets terrain;
    CombatActorArt actors;
    CombatMagicArt magic;
    CombatWeaponArt weapons;
    CombatSpatialService spatial;
    std::array<std::uint8_t,4> status_effects{};
};
void draw_combat_actor_panel(Framebuffer& frame,const GameFont& font,const Character& actor,
                             const ItemCatalog& items,std::span<const std::uint8_t> status_effects) {
    const auto color=!actor.active()?12:actor.byte(0x10e)==1?14:11;
    font.draw_text(frame,actor.name(),184,8,16,1,ega_ui_palette[static_cast<std::size_t>(color)]);
    font.draw_text(frame,resident(0x156b),184,24,16,1,GameFont::green);
    font.draw_text(frame,std::to_string(actor.hit_points()),264,24,6,1,
                   ega_ui_palette[actor.hit_points()<actor.byte(0x32)?14:10]);
    font.draw_text(frame,resident(0x1575),184,40,16,1,GameFont::green);
    const int ac=60-static_cast<int>(actor.byte(0x111));
    font.draw_text(frame,(ac<0?resident(0x148d):std::string{})+std::to_string(ac<0?-ac:ac),208,40,13,1,GameFont::green);
    const Item* weapon=nullptr;
    for(std::size_t i=0;i<actor.inventory().size();++i) {
        const auto& item=actor.inventory().item(i);
        if(item.readied() && items.definition(item.type()).category()==0)weapon=&item;
    }
    int status_y=56;
    if(weapon) {
        const auto lines=wrapped_name(weapon->name());
        for(std::size_t i=0;i<lines.size();++i)font.draw_text(frame,lines[i],184,56+8*static_cast<int>(i),16,1,GameFont::green);
        status_y=64+8*static_cast<int>(lines.size());
    }
    if(!actor.active()) {
        if(actor.status()>=9)throw DecodeError("Unsupported native combat status label");
        font.draw_text(frame,character_status_name(actor.status()),184,status_y,16,1,ega_ui_palette[15]);
    } else if(actor.effects_restored() && std::any_of(actor.effects().begin(),actor.effects().end(),[&](const auto& effect) {
        return std::find(status_effects.begin(),status_effects.end(),effect.code())!=status_effects.end();
    }))font.draw_text(frame,resident(0x1578),184,status_y,16,1,ega_ui_palette[15]);
}
CombatScreen::CombatScreen(const GameFont& font,const ScreenChrome& chrome,const IFileSystem& files,
                           const CombatSession& combat,const ItemCatalog& items)
    :font_(font),chrome_(chrome),items_(items) {
    if(!combat.deployment()) {error_=combat.entry().setup_error.empty()?combat.preparation_error():combat.entry().setup_error;return;}
    try {
        const auto& spatial=*combat.deployment();
        auto staged=std::make_unique<CombatVisualResources>();
        staged->terrain=CombatTerrainAssets::load(files,spatial.battlefield.presentation_mode);
        staged->actors=CombatActorArt::load(files,spatial,combat.encounter());
        staged->magic=CombatMagicArt::load(files);
        staged->weapons=CombatWeaponArt::load(files);
        staged->spatial=CombatSpatialService::load(files);
        const auto module=decode_start_data(files.read_asset("START.EXE"));
        constexpr std::size_t at=0xc7c0+0x286c;
        if(module.size()<at+4)throw DecodeError("Truncated native combat status table");
        std::copy_n(module.begin()+at,4,staged->status_effects.begin());
        auto camera=CombatCamera::initial(spatial);
        resources_=std::move(staged);camera_=camera;
    }catch(const DecodeError& error) {error_=error.what();}
}
CombatScreen::~CombatScreen()=default;
bool CombatScreen::move_cursor(const CombatSession& combat,int dx,int dy) {
    if(!ready())return false;
    if(dx< -1 || dx>1 || dy< -1 || dy>1)throw DecodeError("Invalid combat inspection cursor delta");
    if(!dx && !dy)return false;
    const auto& spatial=*combat.deployment();
    auto position=cursor_.value_or(*spatial.actors.at(focused_actor_).position);
    const auto next=CombatPosition{std::clamp(position.x+dx,0,49),std::clamp(position.y+dy,0,24)};
    if(cursor_ && next==position)return false;
    camera_.focus(next.x,next.y,2);cursor_=next;return true;
}
bool CombatScreen::cycle_actor(const CombatSession& combat,int delta) {
    if(!ready() || !delta)return false;
    const auto& actors=combat.deployment()->actors;auto next=focused_actor_;
    for(std::size_t work=0;work<actors.size();++work) {
        next=(next+(delta<0?actors.size()-1:1))%actors.size();
        if(!actors[next].removed && actors[next].position) {
            if(next==focused_actor_)return false;
            focused_actor_=next;cursor_=actors[next].position;camera_.focus(cursor_->x,cursor_->y);return true;
        }
    }
    return false;
}
void CombatScreen::focus_active(const CombatSession& combat) {
    if(!ready())return;
    if(const auto index=combat.active_index()) {
        focused_actor_=*index;const auto position=combat.deployment()->actors[*index].position;
        if(position) {camera_.focus(position->x,position->y,2);cursor_=position;}
    }
}
void CombatScreen::focus_target(const CombatSession& combat,const CombatTarget& target) {
    if(!ready())return;
    if(target.actor) {
        const auto& actors=combat.deployment()->actors;
        const auto found=std::find_if(actors.begin(),actors.end(),[&](const auto& actor){return actor.id==*target.actor;});
        if(found!=actors.end())focused_actor_=static_cast<std::size_t>(found-actors.begin());
    }
    cursor_=target.position;camera_.focus(cursor_->x,cursor_->y,2);
}
void CombatScreen::draw(Framebuffer& frame,const CombatSession& combat,unsigned focused_pose,const CombatUiState& ui) const {
    frame.fill(ega_ui_palette[0]);chrome_.draw(frame,ChromeLayout::combat());
    if(ready()) {
        const auto& spatial=ui.snapshot_override?*ui.snapshot_override:*combat.deployment();
        auto camera=ui.projectile?ui.projectile->camera:camera_;
        if(ui.weapon_projectile)camera=ui.weapon_projectile->motion.camera;
        auto focused=focused_actor_;
        if(ui.actor_override) {
            const auto found=std::find_if(spatial.actors.begin(),spatial.actors.end(),[&](const auto& a){return a.id==*ui.actor_override;});
            if(found!=spatial.actors.end())focused=static_cast<std::size_t>(found-spatial.actors.begin());
        }
        // Recipient effects own their camera even after selection has advanced
        // or the final deployment has already replaced the icon with a body.
        if(ui.action_busy && !ui.projectile && !ui.weapon_projectile && ui.actor_override && spatial.actors.at(focused).position) {
            const auto position=*spatial.actors.at(focused).position;camera.focus(position.x,position.y,2);
        }
        CombatRenderer::draw_terrain(frame,spatial.battlefield,resources_->terrain,camera);
        // Native0127 lays cursor art on its terrain, then redraws occupants.
        // Interactive inspection/aim cursors must not leak the selector's next
        // actor into an immutable action scene or cover its recipient effect.
        if(cursor_ && !ui.action_busy)CombatRenderer::draw_actor(frame,resources_->actors.cursor(),cursor_->x-camera.x(),cursor_->y-camera.y());
        for(std::size_t i=0;i<spatial.actors.size();++i) {
            const auto& actor=spatial.actors[i];
            if(ui.recipient_effect && ui.recipient_effect->slot==24 && i==focused)continue;
            if(actor.character.active() && resources_->terrain.visible(actor,camera))
                CombatRenderer::draw_actor(frame,resources_->actors.image(actor.id,i==focused?focused_pose:0,actor.extension[9]),actor.position->x-camera.x(),actor.position->y-camera.y());
        }
        if(ui.projectile)resources_->magic.draw_raw(frame,*ui.projectile);
        if(ui.weapon_projectile)resources_->weapons.draw_raw(frame,*ui.weapon_projectile);
        if(ui.recipient_effect) {
            const auto& effect=*ui.recipient_effect;const auto& actor=spatial.actors.at(focused);
            auto cells=effect.slot==24?resources_->spatial.footprint(actor):std::vector<CombatPosition>{};
            if(effect.slot!=24 && actor.position)cells.push_back(*actor.position);
            for(const auto cell:cells)resources_->magic.draw_raw(frame,{{cell.x*3,cell.y*3},effect.pose,camera,true,effect.slot});
        }
        CombatRenderer::apply_display_palette(frame);
        const auto& focus=spatial.actors.at(focused);
        draw_combat_actor_panel(frame,font_,focus.character,items_,resources_->status_effects);
        const bool player=combat.runtime_error().empty() && combat.round().phase==CombatRoundPhase::player;
        const auto active=combat.active_index();
        if(ui.magic_busy || ui.action_busy) {
            // Action playback owns both footer rows. Its caption or recipient notice
            // is drawn once below, with room for native wrapped feedback.
        }else if(combat.continuation_pending()) {
            draw_menu_actions(font_,frame,overlay_string(0xa076)+" Y/N",0,192,40,"YN",ega_ui_palette[13]);
        } else if(ui.cancel_spell_question) {
            draw_menu_actions(font_,frame,overlay_string(0x27c1c)+" Y/N",0,192,40,"YN",ega_ui_palette.at(ui.notice_color));
        } else if(ui.magic_targeting) {
            const auto captions=(ui.manual_aim?overlay_string(0x155da):overlay_string(0x15270))+
                (ui.manual_aim?std::string{}:overlay_string(0x15278))+
                (ui.manual_aim?overlay_string(0x155ce):overlay_string(0x1528a));
            draw_menu_actions(font_,frame,captions,0,192,40,ui.friendly_question?"YN":"TNPMCE");
            if(ui.target_range)font_.draw_text(frame,overlay_string(0x15264)+std::to_string(*ui.target_range/2),184,160,16,1,GameFont::green);
        } else if(ui.flight_question) {
            draw_menu_actions(font_,frame,overlay_string(0xa1d8)+" Y/N",0,192,40,"YN");
        } else if(ui.aiming) {
            std::string captions;
            if(ui.friendly_question)captions=overlay_string(0x15185)+" Y/N";
            else {
                if(ui.target_eligible)captions=ui.manual_aim?overlay_string(0x155da):overlay_string(0x15270);
                if(!ui.manual_aim)captions+=overlay_string(0x15278);
                captions+=ui.manual_aim?overlay_string(0x155ce):overlay_string(0x1528a);
            }
            draw_menu_actions(font_,frame,captions,0,192,40,ui.friendly_question?"YN":"TNPMCE");
            if(ui.target_range)font_.draw_text(frame,overlay_string(ui.manual_aim?0x155c2:0x15264)+
                std::to_string(*ui.target_range/2),184,160,16,1,GameFont::green);
        } else if(combat.moving()) {
            font_.draw_text(frame,"ARROWS/1-9 MOVE ENTER KEEP ESC CANCEL",0,192,40,1,ega_ui_palette.at(ui.notice_color));
            font_.draw_text(frame,"ARROWS/1-9",0,192,10,1,menu_action_color);
            font_.draw_text(frame,"ENTER",128,192,5,1,menu_action_color);
            font_.draw_text(frame,"ESC",216,192,3,1,menu_action_color);
        } else if(ui.done_menu) {
            const auto availability=active?TacticalCommandService::available(spatial,spatial.actors[*active].id,items_):TacticalCommandAvailability{};
            const auto captions=(availability.guard?overlay_string(0xa605):std::string{})+overlay_string(0xa60c)+
                (availability.bandage?overlay_string(0xa618):std::string{})+overlay_string(0xa621);
            font_.draw_text(frame,captions,0,192,40,1,GameFont::green);
            bool first=true;
            for(std::size_t i=0;i<captions.size();++i) {
                if(captions[i]!=' ' && first)font_.draw_text(frame,std::string_view(captions).substr(i,1),8*static_cast<int>(i),192,1,1,menu_action_color);
                first=captions[i]==' ';
            }
        } else if(player) {
            const auto& command_actor=active?combat.deployment()->actors[*active]:focus;
            font_.draw_text(frame,command_captions(command_actor,ui.casting_forbidden),0,192,40,1,ega_ui_palette[8]);
            if(player) {
                const auto all=command_captions(command_actor,ui.casting_forbidden);
                // Source command availability and hotkeys share one draw path;
                // the session services own their costs and target validation.
                const std::string_view enabled="MVATQDUC";
                bool first=true;
                for(std::size_t at=0;at<all.size();) {
                    if(all[at]==' ') {first=true;++at;continue;}
                    const auto end=all.find(' ',at);const auto count=(end==std::string::npos?all.size():end)-at;
                    if(first && enabled.find(all[at])!=std::string_view::npos) {
                        font_.draw_text(frame,std::string_view(all).substr(at,count),8*static_cast<int>(at),192,static_cast<int>(count),1,GameFont::green);
                        font_.draw_text(frame,std::string_view(all).substr(at,1),8*static_cast<int>(at),192,1,1,menu_action_color);
                    }
                    first=false;at+=count;
                }
            }
        }
        const auto& error=!combat.runtime_error().empty()?combat.runtime_error():combat.action_message();
        if(ui.magic_busy || ui.action_busy)font_.draw_text(frame,ui.notice.empty()?ui.magic_caption:ui.notice,0,184,40,2,
                                       ui.notice.empty()?GameFont::green:ega_ui_palette.at(ui.notice_color),true);
        else if(!ui.notice.empty())font_.draw_text(frame,ui.notice,0,184,40,1,ega_ui_palette.at(ui.notice_color));
        else if(!error.empty())font_.draw_text(frame,error,0,184,40,1,ega_ui_palette[12]);

    } else {
        font_.draw_text(frame,"Combat presentation blocked",8,8,21,2,GameFont::green,true);
        font_.draw_text(frame,error_,8,32,21,14,GameFont::green,true);
        font_.draw_text(frame,"F12: DEVELOPMENT ABORT",0,192,40,1,menu_action_color);
    }
}
}
