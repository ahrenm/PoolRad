#pragma once
#include "gbengine/world/world_state.hpp"
namespace GBEngine {
// Native3635 door actions. Staged on owned copies by ExplorationSession.
class DoorService {
public:
    static std::vector<char> choices(const WorldState&);
    static bool apply(WorldState&,std::mt19937&,char action);
};
}
