#pragma once
#include "gbengine/party/party.hpp"
#include <random>
namespace GBEngine {
enum class TrainingStatus {ready,inactive,insufficient_money,wrong_school,insufficient_experience};
struct TrainingPlan {
    TrainingStatus status=TrainingStatus::insufficient_experience;
    std::uint64_t revision{};
    std::size_t member{};
    std::uint8_t identity{},eligible{},available{};
    std::uint32_t experience{};
};
class TrainingService {
    Bytes module_;
    std::uint8_t byte(unsigned)const;
    std::uint32_t dword(unsigned)const;
    struct Advancement {std::uint8_t existing{},eligible{};std::uint32_t experience{};};
    Advancement advancement(const Character&)const;
    TrainingPlan review(const Party&,std::size_t,std::uint16_t)const;
public:
    static TrainingService load(const IFileSystem&);
    // Read-only source XP/race-cap eligibility; independent of purse and school.
    std::uint8_t eligible_classes(const Character&)const;
    // Native XP clipping precedes the question; it remains committed on No.
    // Fees, levels, HP, learning and RNG remain staged until confirmation.
    TrainingPlan prepare(Party&,std::size_t,std::uint16_t available_classes)const;
    std::vector<std::uint8_t> learning_options(const Party&,const TrainingPlan&)const;
    void train(Party&,const TrainingPlan&,std::optional<std::uint8_t> learned_spell,std::mt19937&)const;
};
}
