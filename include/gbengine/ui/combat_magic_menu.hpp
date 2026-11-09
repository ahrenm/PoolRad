#pragma once
#include "gbengine/combat/casting_service.hpp"
#include "gbengine/rendering/screen_chrome.hpp"
#include "gbengine/rendering/presentation.hpp"

namespace GBEngine {
enum class CombatMagicMenuAction { none,select,ready,leave };
struct CombatMagicChoice {
    std::optional<std::size_t> spell_slot,scroll_slot;
    std::optional<ItemId> item;
};
// Pure source-backed list/navigation. Gameplay and resource ownership stay in
// CombatSession; redraw never changes a character, action budget or random state.
class CombatMagicMenu {
    enum class Kind {spells,items,scroll};
    struct Row {CombatMagicChoice choice;std::uint8_t level{};std::optional<SpellId> spell;bool heading=false;};
    Kind kind_=Kind::spells;
    std::vector<Row> rows_;
    std::size_t top_=0,selected_=0;
    std::size_t visible() const {return kind_==Kind::items?6:18;}
    std::size_t bottom() const;
    void normalize(int);
    void move(int);
    void page(int);
public:
    static CombatMagicMenu spells(std::span<const MemorizedSpell>,const SpellCatalog&);
    static CombatMagicMenu items(const Character&);
    static CombatMagicMenu scroll(const Character&,ItemId,const SpellCatalog&);
    bool empty() const {return rows_.empty();}
    bool item_list() const {return kind_==Kind::items;}
    bool scroll_list() const {return kind_==Kind::scroll;}
    CombatMagicChoice choice() const;
    CombatMagicMenuAction input(InputEvent);
    void draw(Framebuffer&,const GameFont&,const ScreenChrome&,const Character&,const SpellCatalog&,std::string_view diagnostic={}) const;
};
}
