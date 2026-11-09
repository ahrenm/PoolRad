#pragma once
#include "gbengine/party/party.hpp"

namespace GBEngine {
struct CombatIconAppearance {
    std::uint8_t head{},weapon{},style{};
    std::array<std::uint8_t,6> colors{};
    bool operator==(const CombatIconAppearance&) const=default;
};
enum class CombatIconPart {head,weapon};
struct CombatIconPlan {
    std::uint64_t revision{};
    std::size_t member{};
    std::uint8_t native_identity{};
    CombatIconAppearance appearance;
};
struct CombatIconResult {
    bool applied=false;
    std::string message;
};
// Native393E:382D cosmetic edits. A preview owns its candidate; Keep publishes
// only these nine appearance bytes to the revision-bound selected character.
class CombatIconService {
public:
    static CombatIconAppearance read(const Character&);
    static CombatIconPlan prepare(const Party&,std::size_t member);
    static CombatIconResult keep(Party&,const CombatIconPlan&,const CombatIconAppearance&);
    static void cycle_part(CombatIconAppearance&,CombatIconPart,bool next);
    static void cycle_color(CombatIconAppearance&,std::size_t component,bool secondary,bool next);
    static void set_style(CombatIconAppearance&,std::uint8_t);
    static Character preview(const Character&,const CombatIconAppearance&);
};
}
