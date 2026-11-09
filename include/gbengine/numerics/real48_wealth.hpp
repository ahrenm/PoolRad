#pragma once
#include <array>
#include <cstdint>

namespace GBEngine::NativeReal48 {
// This deliberately models only ECL28's (100-byte)/100 retention and unsigned
// WORD multiplication. It is not a general Real48 arithmetic implementation.
class WealthRetention {
public:
    explicit WealthRetention(std::uint8_t loss_percentage);
    std::uint16_t apply(std::uint16_t holding) const;
    std::array<std::uint16_t,3> native_words() const;
private:
    std::uint64_t significand_{}; // Explicit leading bit: 40 significant bits.
    std::uint8_t exponent_{}; // Original bias 129 for [1,2).
    bool negative_{};
};
}
