#include "gbengine/numerics/real48_wealth.hpp"
#include <bit>

namespace GBEngine::NativeReal48 {
WealthRetention::WealthRetention(std::uint8_t loss_percentage) {
    const int numerator=100-static_cast<int>(loss_percentage);
    if(numerator==0) return;
    negative_=numerator<0;
    const auto magnitude=static_cast<std::uint64_t>(negative_?-numerator:numerator);
    // For this domain the unbiased exponent is -7..0, and every shift fits
    // uint64_t. Native 1038's quotient loop plus 1016's guard add produces
    // nearest magnitude with ties upward for all 256 coefficients.
    int binary_exponent=0;
    while((magnitude<<static_cast<unsigned>(-binary_exponent))<100) --binary_exponent;
    const auto scaled=magnitude<<static_cast<unsigned>(39-binary_exponent);
    significand_=scaled/100;
    if((scaled%100)*2>=100) ++significand_;
    if(significand_==(std::uint64_t{1}<<40)) {significand_>>=1;++binary_exponent;}
    exponent_=static_cast<std::uint8_t>(129+binary_exponent);
}

std::array<std::uint16_t,3> WealthRetention::native_words() const {
    if(!exponent_) return {};
    return {static_cast<std::uint16_t>(((significand_&255)<<8)|exponent_),
            static_cast<std::uint16_t>(significand_>>8),
            static_cast<std::uint16_t>(((significand_>>24)&0x7fff)|(negative_?0x8000:0))};
}

std::uint16_t WealthRetention::apply(std::uint16_t holding) const {
    if(!holding || !exponent_) return 0;
    const auto integer_bits=std::bit_width(holding);
    const auto integer_top=static_cast<std::uint32_t>(holding)<<(16-integer_bits);
    const auto coefficient_top=static_cast<std::uint32_t>(significand_>>24);
    const auto coefficient_middle=static_cast<std::uint32_t>((significand_>>8)&0xffff);
    const auto coefficient_low=static_cast<std::uint32_t>(significand_&255);
    // Preserve native 0f6a's fast-path omission: its first MUL is 8x8,
    // using the top byte of integer_top. A generic rounded 40-bit multiply
    // can therefore disagree even with exact integer/rational inputs.
    const std::uint64_t middle=integer_top*coefficient_middle+(integer_top>>8)*coefficient_low;
    std::uint64_t extended=(static_cast<std::uint64_t>(integer_top)*coefficient_top<<16)+middle;
    int product_exponent=static_cast<int>(integer_bits)+exponent_;
    if(!(extended&(std::uint64_t{1}<<47))) {extended<<=1;--product_exponent;}
    extended+=0x80; // 1016: one guard byte, nearest magnitude, ties upward.
    if(extended&(std::uint64_t{1}<<48)) {extended>>=1;++product_exponent;}
    const auto product_significand=extended>>8;
    // 119e sets CH=0: truncate magnitude before applying sign. Domain's
    // largest magnitude is 101579, so no native signed-LONG overflow occurs.
    const auto magnitude=static_cast<std::int32_t>(product_significand>>(168-product_exponent));
    const auto signed_result=negative_?-magnitude:magnitude;
    return static_cast<std::uint16_t>(signed_result); // Native stores only AX.
}
}
