#pragma once
#include <cstdint>
#include <optional>
#include <span>
namespace GBEngine {
enum class EclDamageTarget { attacks, random_member, selected_member, whole_roster };
struct EclDamageMode {
    EclDamageTarget target;
    std::optional<std::uint8_t> save_category;
    std::uint8_t modifier{},attack_count{};
    bool initial_ordinal=false;
};
// Native2182:2775 byte fields. Decode targeting independently of dice/bonus.
// Categories5..7 (selected6..7) require undefined native table rows and stay held.
inline std::optional<EclDamageMode> decode_ecl_damage_mode(std::span<const std::uint16_t> v) {
    if(v.size()!=5)return std::nullopt;
    for(const auto n:v)if(n>255)return std::nullopt;
    if(v[1] && !v[2])return std::nullopt;
    const auto mode=v[0],save=v[4];
    EclDamageMode result{EclDamageTarget::attacks,std::nullopt,static_cast<std::uint8_t>(mode&31),0,(mode&64)==0};
    if(!(mode&128)) {result.attack_count=static_cast<std::uint8_t>(mode);return result;}
    if(mode&64) {
        result.target=EclDamageTarget::whole_roster;
        if(!(mode&32))result.save_category=static_cast<std::uint8_t>(save&7);
    } else if(save&128) {
        result.target=EclDamageTarget::selected_member;
        if(save&7)result.save_category=static_cast<std::uint8_t>((save&7)-1);
    } else {
        result.target=EclDamageTarget::random_member;
        result.save_category=static_cast<std::uint8_t>(save&7);
    }
    if(result.save_category && *result.save_category>4)return std::nullopt;
    return result;
}
}
