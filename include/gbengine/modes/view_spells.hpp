#pragma once
#include "gbengine/app/game_mode.hpp"
#include "gbengine/magic/spellbook.hpp"
#include "gbengine/rendering/screen_chrome.hpp"
namespace GBEngine {
class GameFont;
struct CombatViewAbort;
// Native View offers Spells for any occupied slot, including pending entries.
bool character_has_view_spells(const Character&);
// A pending-only book leaves View unchanged, just as the native empty builder.
std::unique_ptr<IGameMode> make_view_spells_mode(const Party&,std::size_t,
    const SpellCatalog&,const GameFont&,const ScreenChrome&,std::shared_ptr<CombatViewAbort> ={});
}
