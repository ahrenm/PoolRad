#pragma once
#include "gbengine/party/party.hpp"
#include <random>
namespace GBEngine {
struct EffectExpiryEdits;
class NoncombatDamageService {
public:
    // Ordinary flag0 periodic damage and flag8 alignment damage. Ambient
    // native spell/code scratch is deliberately unbound, never fabricated.
    static void apply(Character&,std::uint8_t amount,std::uint8_t flags,std::mt19937* =nullptr,EffectExpiryEdits* =nullptr);
};
class NoncombatRecoveryService {
public:
    static bool revive(Character&,std::uint8_t hp,std::uint8_t execution_mode=1);
    static bool heal(Character&,std::uint8_t amount,bool force=false,std::uint8_t execution_mode=1);
};
}
