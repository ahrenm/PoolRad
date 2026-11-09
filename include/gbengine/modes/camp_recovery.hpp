#pragma once
#include "gbengine/app/game_mode.hpp"
namespace GBEngine {
class ExplorationSession;
class ExplorationScreen;
class PicturePresentation;
enum class CampWorkKind {memorize,scribe};
// All borrowed resources belong to the suspended Encamp/Exploration parents.
std::unique_ptr<IGameMode> make_camp_work_mode(ExplorationSession&,const ExplorationScreen&,
    const PicturePresentation*,CampWorkKind);
std::unique_ptr<IGameMode> make_camp_rest_mode(ExplorationSession&,const ExplorationScreen&,
    const PicturePresentation*);
}
