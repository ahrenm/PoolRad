#pragma once
#include "gbengine/world/exploration_session.hpp"
#include "gbengine/app/game_mode.hpp"
namespace GBEngine {
// Host QoL workflow; only real casts/rest ticks change HP, clock or RNG.
class FixLoadout {
    std::vector<std::array<std::uint8_t,21>> original_;
public:
    explicit FixLoadout(const Party&);
    bool prepare_healing(Party&,const SpellCatalog&);
    void restore(Party&,const SpellCatalog&);
};
enum class FixPhase {healing,restoring,resting,feedback,complete,stopped,blocked,interrupted};
class CampFix {
    ExplorationSession& session_;
    FixLoadout loadout_;
    std::uint64_t revision_;
    FixPhase phase_=FixPhase::healing,resume_=FixPhase::healing;
    bool installed_=false,restoring_=false,stop_requested_=false,rest_notice_=false;
    std::uint32_t elapsed_minutes_=0;
    std::vector<std::string> notices_;
    std::string message_="Preparing healing spells.";
    void finish(FixPhase,std::string);
    void restore();
    void begin_rest();
    bool pending() const;
public:
    explicit CampFix(ExplorationSession&);
    void advance();
    void acknowledge();
    void cancel();
    FixPhase phase() const {return phase_;}
    bool terminal() const {return phase_==FixPhase::complete || phase_==FixPhase::stopped || phase_==FixPhase::blocked || phase_==FixPhase::interrupted;}
    const std::string& message() const {return message_;}
    const std::vector<std::string>& notices() const {return notices_;}
    std::uint32_t elapsed_minutes() const {return elapsed_minutes_;}
};
class ExplorationScreen;
class PicturePresentation;
std::unique_ptr<IGameMode> make_camp_fix_mode(ExplorationSession&,const ExplorationScreen&,const PicturePresentation*);
}
