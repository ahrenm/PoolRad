#pragma once
#include "gbengine/effects/effect_lifecycle.hpp"
#include <functional>
namespace GBEngine {
// These operations must work only on the caller's staged character/RNG.
// Combat placement and unbound native scratch are not supplied implicitly.
struct PeriodicEffectBindings {
    std::function<void(Character&,std::uint8_t)> damage;
    std::function<bool(Character&,std::uint8_t,bool)> heal;
    std::function<bool(Character&,std::uint8_t)> recover;
    std::function<void(Character&,std::uint8_t,EffectExpiryEdits&)> set_status;
    std::function<void(Character&)> refresh_inventory;
};
class NoncombatPeriodicEffects {
public:
    static bool handles(std::uint8_t);
    static bool reverse(const EffectExpiryRequest&,Character&,EffectExpiryEdits&,const PeriodicEffectBindings&);
};
}
