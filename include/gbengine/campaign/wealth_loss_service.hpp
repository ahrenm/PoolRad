#pragma once
#include "gbengine/party/party.hpp"
#include <random>

namespace GBEngine {
enum class WealthLossStatus { applied,unresolved };
struct WealthLossResult {
    WealthLossStatus status=WealthLossStatus::unresolved;
    std::string message;
    std::size_t items_lost{};
    bool applied() const {return status==WealthLossStatus::applied;}
};
// ECL28's full byte-percentage Real48 money conversion. The complete party and
// shared RNG commit together; unsupported native deletion effects roll back.
class WealthLossService {
public:
    static WealthLossResult apply(Party&,std::uint8_t target,std::uint8_t percent,std::mt19937&);
};
}
