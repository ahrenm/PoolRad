#pragma once
#include "gbengine/app/game_mode.hpp"
#include "gbengine/magic/spell_catalog.hpp"
#include "gbengine/party/party.hpp"
#include "gbengine/rendering/screen_chrome.hpp"
namespace GBEngine {
class GameFont;
struct EffectDisplayRow {
    std::string text;
    bool heading=false;
};
// Native Camp Display interprets owned SPC records, not memorized spell slots.
// Unknown effect codes are omitted as in the source; no callback executes.
std::vector<EffectDisplayRow> build_effect_display(const Party&,const SpellCatalog&);
std::unique_ptr<IGameMode> make_effect_display_mode(const Party&,const SpellCatalog&,
    const GameFont&,const ScreenChrome&);
}
