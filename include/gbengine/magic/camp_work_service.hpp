#pragma once
#include "gbengine/magic/spellbook.hpp"

namespace GBEngine {
enum class CampWorkStatus { applied,rejected,unresolved };
struct CampWorkResult {
    CampWorkStatus status=CampWorkStatus::applied;
    std::string message;
};
struct CampWorkChoices : CampWorkResult { std::vector<SpellId> spells; };
struct ScribeChoice { ItemId item{};std::size_t slot{};SpellId spell;bool pending{}; };
struct CampScribeChoices : CampWorkResult { std::vector<ScribeChoice> spells; };
// Native resident6DD0..6DD7 and4305:0BB2 local hourly accumulator, not VM cells.
struct CampWorkState {
    std::array<std::uint8_t,8> counters{};
    std::uint8_t hourly_ticks{};
    bool operator==(const CampWorkState&) const =default;
};
struct CampWorkNotice {std::size_t member{};SpellId spell;bool scribed{};};
struct CampWorkPreparation : CampWorkResult { std::uint16_t minutes{}; };
struct CampWorkTick : CampWorkResult { std::vector<CampWorkNotice> notices; };
class CampWorkService {
public:
    static CampWorkChoices memorize_choices(const Party&,std::size_t,const SpellCatalog&);
    static CampScribeChoices scribe_choices(const Party&,std::size_t,const SpellCatalog&);
    static CampWorkResult schedule_memorize(Party&,std::size_t,const SpellCatalog&,SpellId);
    static CampWorkResult clear_memorize(Party&,std::size_t);
    static CampWorkResult schedule_scribe(Party&,std::size_t,const SpellCatalog&,ItemId,std::size_t slot);
    static CampWorkResult clear_scribe(Party&,std::size_t);
    static CampWorkResult cleanup_camp(Party&);
    // Staged whole-party operations: any unresolved work leaves all inputs intact.
    static CampWorkPreparation prepare_rest(Party&,const SpellCatalog&);
    static CampWorkTick tick_work(Party&,const SpellCatalog&,CampWorkState&);
};
}
