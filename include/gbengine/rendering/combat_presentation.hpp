#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace GBEngine {
// A visual frame identifier belongs to its renderer/action owner. This queue
// never resolves an attack, spends an action or invokes a gameplay callback.
struct CombatVisualStep {
    std::uint32_t frame{};
    std::uint64_t duration_ms{};
    bool operator==(const CombatVisualStep&) const = default;
};
struct CombatPresentationToken {
    std::uint64_t owner{},sequence{};
    std::size_t step{};
    bool operator==(const CombatPresentationToken&) const = default;
};
// Ordered, non-looping visual work. Every step must actually reach the graphics
// HAL before its dwell can run. It is separate from ambient animation, campaign
// time and native gameplay state; the owning mode supplies elapsed host time.
class CombatPresentation {
    std::uint64_t owner_{},sequence_{};
    std::vector<CombatVisualStep> steps_;
    std::size_t step_{};
    std::uint64_t elapsed_{};
    bool presented_=false,paused_=false;
    void finish_step();
public:
    explicit CombatPresentation(std::uint64_t owner);
    void start(std::vector<CombatVisualStep>);
    const CombatVisualStep* current() const;
    std::optional<CombatPresentationToken> token() const;
    bool presented(CombatPresentationToken);
    void advance(std::uint64_t elapsed_ms);
    void pause();
    void resume();
    bool paused() const {return paused_;}
    bool complete() const {return step_==steps_.size();}
};
}
