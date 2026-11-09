#pragma once
#include "gbengine/party/party.hpp"
#include <random>
namespace GBEngine {
enum class ScriptDamageSaveContext : std::uint8_t { inherited,ordinary_poison };
class ScriptDamageService {
public:
    // Reviewed source-handler semantics, independent of coordinates or quest state.
    static ScriptDamageSaveContext save_context(unsigned disk,std::uint8_t ecl,std::uint16_t instruction,
        std::span<const std::uint8_t> script);
    static std::vector<DamageOutcome> attacks(Party&,std::span<const std::uint16_t>,std::mt19937&);
    static std::vector<DamageOutcome> attacks(Party&,std::span<const std::uint16_t>,std::mt19937&,ScriptDamageSaveContext);
};
}
