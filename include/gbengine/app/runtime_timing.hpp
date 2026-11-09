#pragma once
#include <cstdint>

namespace GBEngine {
// Host presentation policy, independent of the action-driven campaign clock.
// Imported DOS Speed values remain save data but do not control this scheduler.
struct RuntimeTiming {
    static constexpr std::uint32_t frames_per_second=30;
    static constexpr std::uint64_t notification_dwell_ms=900;
    // Ambient backdrop cadence; independent of render Hz and readable text.
    static constexpr std::uint32_t ambient_frame_ms=200;
    // Tactical movement is an ordered action, paced separately from ambient
    // art and readable notices. A stalled frame never skips a tactical step.
    static constexpr std::uint32_t combat_step_ms=200;
    // 10BA:1FEB recipient frames use source wait70. Keep the native default
    // two effect cycles, independent of opaque imported DOS Speed49E2.
    static constexpr std::uint64_t combat_recipient_frame_ms=70;
    static constexpr unsigned combat_effect_cycles=2;
    // Source projectile waits distinguish common bolts (30) and rays (50).
    // Map those units to elapsed milliseconds as a host readability policy;
    // this preserves their relative cadence, not DOS hardware-loop timing.
    static constexpr std::uint64_t combat_projectile_ms(std::uint8_t source_wait) {
        return source_wait;
    }
};
}
