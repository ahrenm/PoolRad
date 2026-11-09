#pragma once
#include "gbengine/world/world_state.hpp"
#include "gbengine/rendering/exploration_assets.hpp"
namespace GBEngine {
void render_exploration(Framebuffer&,const WorldState&,const ExplorationAssets&);
}
