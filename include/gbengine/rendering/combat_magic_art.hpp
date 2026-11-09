#pragma once
#include "gbengine/rendering/combat_actor_art.hpp"
#include "gbengine/rendering/combat_renderer.hpp"
namespace GBEngine {
struct CombatMagicFrame {
    // Absolute battlefield coordinates in8px subtiles (one tile=3subtiles).
    CombatPosition subtile;
    unsigned pose{};
    CombatCamera camera;
    // Native camera/redraw presents are retained as independent barriers.
    bool draw_projectile=true;
    unsigned effect_slot=18;
    bool operator==(const CombatMagicFrame&) const=default;
};
class CombatMagicArt {
    std::array<Image,4> frames_;
    std::array<Image,4> lightning_;
    std::array<Image,4> effects_,damage_;
    Image defeat_;
public:
    static CombatMagicArt load(const IFileSystem&);
    // Native slots18/19 projectiles,22 recipient effect,23 damage,24 defeat.
    const Image& image(unsigned pose,unsigned effect_slot=18) const;
    // Pure10BA:1A29 ordering, including skipped first generated direction and
    // explicit final destination frame. No RNG, clocks or gameplay callbacks.
    static std::vector<CombatMagicFrame> projectile(CombatPosition origin,CombatPosition destination,CombatCamera initial,
                                                   unsigned effect_slot=18,bool allow_outside=false,unsigned frame_count=4);
    // Compose masked raw EGA before CombatRenderer::apply_display_palette.
    void draw_raw(Framebuffer&,const CombatMagicFrame&) const;
};
}
