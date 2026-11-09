#pragma once
#include "gbengine/combat/combat_session.hpp"
#include "gbengine/rendering/presentation.hpp"
#include "gbengine/rendering/screen_chrome.hpp"
#include "gbengine/rendering/combat_renderer.hpp"
#include "gbengine/items/item_catalog.hpp"
#include "gbengine/rendering/combat_magic_art.hpp"
#include "gbengine/rendering/combat_weapon_art.hpp"

namespace GBEngine {
void draw_combat_actor_panel(Framebuffer&,const GameFont&,const Character&,const ItemCatalog&,
                             std::span<const std::uint8_t> status_effects);
struct CombatVisualResources;
struct CombatRecipientEffect {unsigned slot{},pose{};};
struct CombatUiState {bool done_menu=false;std::string notice;bool casting_forbidden=true;bool aiming=false;
    bool manual_aim=false,friendly_question=false,flight_question=false,target_eligible=false;
    bool magic_targeting=false,cancel_spell_question=false;
    std::optional<std::uint8_t> target_range;
    std::optional<CombatMagicFrame> projectile;
    std::optional<CombatWeaponFrame> weapon_projectile;
    std::optional<CombatRecipientEffect> recipient_effect;
    const DeploymentState* snapshot_override=nullptr;
    std::string magic_caption;
    std::optional<CombatActorIdentity> actor_override;
    bool magic_busy=false;
    bool action_busy=false;
    unsigned notice_color=14;
};
// Source art and inspection state belong to the suspended combat mode, not
// durable campaign state. Missing art remains an explicit, abortable boundary.
class CombatScreen {
    const GameFont& font_;
    const ScreenChrome& chrome_;
    const ItemCatalog& items_;
    std::unique_ptr<CombatVisualResources> resources_;
    CombatCamera camera_;
    std::size_t focused_actor_{};
    std::optional<CombatPosition> cursor_;
    std::string error_;
public:
    CombatScreen(const GameFont&,const ScreenChrome&,const IFileSystem&,const CombatSession&,const ItemCatalog&);
    ~CombatScreen();
    bool ready() const {return resources_!=nullptr;}
    const auto& error() const {return error_;}
    const CombatCamera& camera() const {return camera_;}
    std::size_t focused_actor() const {return focused_actor_;}
    const auto& cursor() const {return cursor_;}
    bool move_cursor(const CombatSession&,int dx,int dy);
    bool cycle_actor(const CombatSession&,int delta);
    void focus_active(const CombatSession&);
    void focus_target(const CombatSession&,const CombatTarget&);
    void draw(Framebuffer&,const CombatSession&,unsigned focused_pose=0,const CombatUiState& ={}) const;
};
}
