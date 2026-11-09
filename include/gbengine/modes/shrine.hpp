#pragma once
#include "gbengine/app/game_mode.hpp"
namespace GBEngine {
class ExplorationSession;
class ExplorationScreen;
std::unique_ptr<IGameMode> make_shrine_healing_mode(ExplorationSession&,const ExplorationScreen&);
}
