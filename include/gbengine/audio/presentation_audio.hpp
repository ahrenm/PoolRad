#pragma once
#include "gbengine/platform/hal.hpp"
#include "gbengine/rendering/combat_presentation.hpp"
#include <optional>

namespace GBEngine {
struct ResolvedAudioCue {
    std::vector<float> samples;
    std::optional<std::size_t> loop_start;
};
enum class AudioCueDomain {combat,script};
struct PresentationAudioCue {
    CombatPresentationToken frame;
    std::uint16_t command{};
    // A frame can start a cue and later finish it. These are independent events.
    std::uint16_t ordinal{};
    bool native_blocking=false; //18AE:00DC nonzero5374: fixed10 hundredths.
    AudioCueDomain domain=AudioCueDomain::combat;
    bool operator==(const PresentationAudioCue&) const=default;
};
// The native commands select sound programs, not frequencies. A renderer must
// actually understand that program before supplying48kHz mono floating PCM.
// Missing synthesis remains explicit; no guessed beep stands in for a sound.
class IAudioCueResolver {
public:
    virtual ~IAudioCueResolver()=default;
    virtual std::optional<std::vector<float>> resolve(std::uint16_t command)=0;
    virtual std::optional<ResolvedAudioCue> resolve_program(std::uint16_t command) {
        auto samples=resolve(command);if(!samples)return {};return ResolvedAudioCue{std::move(*samples),{}};
    }
};
enum class AudioCueResult {queued,stopped,disabled,unresolved,stale};
struct PresentationAudioStats {
    std::size_t queued{},stopped{},disabled{},unresolved{},stale{};
};
class PresentationAudio {
    IAudio* audio_{};
    IAudioCueResolver* resolver_{};
    std::array<std::optional<PresentationAudioCue>,2> last_;
    bool playing_=false;
    PresentationAudioStats stats_;
    std::vector<float> loop_;
    std::uint64_t remaining_samples_=0,millisecond_remainder_=0;
public:
    PresentationAudio(IAudio* audio=nullptr,IAudioCueResolver* resolver=nullptr):audio_(audio),resolver_(resolver) {}
    // The caller supplies events only after the corresponding graphics present.
    // Cosmetic re-present, host stalls and nested modes cannot replay a cue.
    AudioCueResult submit(PresentationAudioCue);
    void advance(std::uint64_t elapsed_ms) noexcept;
    void cancel() noexcept;
    void reset() noexcept;
    const auto& stats() const {return stats_;}
};
enum class NativeSoundCue {spell_projectile,damage,effect,defeat,weapon_hit,weapon_miss,spell47,spell51,weapon_projectile,weapon_projectile_end,ecl_call8,ecl_call10};
class NativeSoundCues {
    std::array<std::uint16_t,14> commands_{};
public:
    static NativeSoundCues load(const IFileSystem&);
    std::uint16_t command(NativeSoundCue) const;
    std::uint16_t stop_command() const {return commands_[0];}
};
}
