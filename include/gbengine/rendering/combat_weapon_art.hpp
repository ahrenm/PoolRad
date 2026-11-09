#pragma once
#include "gbengine/combat/attack_service.hpp"
#include "gbengine/rendering/combat_magic_art.hpp"
namespace GBEngine {
struct CombatWeaponFrame {
    CombatMagicFrame motion;
    std::uint8_t item_type{},direction{};
};
struct CombatWeaponForm {unsigned slot{},paired_pose{},frames{},wait{};bool mirrored=false;};
class CombatWeaponArt {
    std::array<std::array<Image,2>,9> slots_;
public:
    static CombatWeaponArt load(const IFileSystem&);
    static CombatWeaponForm form(std::uint8_t item_type,unsigned direction);
    static std::vector<CombatWeaponFrame> projectile(const CombatStrike::Travel&,CombatCamera);
    Image image(const CombatWeaponFrame&) const;
    void draw_raw(Framebuffer&,const CombatWeaponFrame&) const;
};
}
