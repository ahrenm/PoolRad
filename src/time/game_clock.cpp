#include "gbengine/time/game_clock.hpp"
#include "gbengine/assets/decoders.hpp"
namespace GBEngine {
namespace {constexpr std::array<std::uint16_t,7> radix{10,10,6,24,30,12,256};}
ClockAdvance GameClock::advance(TimeIncrement increment) const {
    const auto field=static_cast<unsigned>(increment.unit);
    if(field>=radix.size()) throw DecodeError("Invalid game clock field");
    ClockAdvance result{*this,0,increment.amount};
    for(unsigned i=field;i>1;--i)
        result.effect_ticks=static_cast<std::uint16_t>(result.effect_ticks*radix[i-1]);
    for(unsigned count=0;count<increment.amount;++count) {
        auto& words=result.clock.words_;
        words[field]=static_cast<std::uint16_t>(words[field]+1);
        for(std::size_t i=0;i<words.size();++i) if(words[i]>=radix[i]) {
            // The native last field is deliberately NOT reduced on overflow.
            if(i==6) ++result.age_increments;
            else {
                words[i]=static_cast<std::uint16_t>(words[i]-radix[i]);
                words[i+1]=static_cast<std::uint16_t>(words[i+1]+1);
            }
        }
    }
    return result;
}
std::string GameClock::hour_text() const {return (words_[3]<10?"0":"")+std::to_string(words_[3]);}
std::string GameClock::minute_text() const {
    const auto minute=10u*words_[2]+words_[1];
    return (minute<10?"0":"")+std::to_string(minute);
}
}
