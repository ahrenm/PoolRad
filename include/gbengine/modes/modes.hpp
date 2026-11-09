#pragma once
#include "gbengine/app/game_mode.hpp"
#include "gbengine/app/options.hpp"
#include <ostream>
#include <functional>
namespace GBEngine {
class Party;
class GameFont;
class IItemEffectResolver;
class IItemActivationService;
class ExplorationSession;
class ExplorationScreen;
class ISaveRepository;
class PicturePresentation;
class IShopInventory;
struct CampaignResources;
struct ItemActionResult;
// Shared only by a combat mode and its nested character/spell views.
// Each child leaves after presentation; the combat owner performs rollback.
struct CombatViewAbort {bool requested=false;};
struct CharacterViewServices {
    IItemEffectResolver* effects=nullptr;
    bool identification_available=false;
    std::array<std::uint32_t,5>* pooled_coins=nullptr;
    IItemActivationService* activations=nullptr;
    IShopInventory* shop=nullptr;
    bool portrait_enabled=true;
    bool combat=false;
    CampaignResources* money_pool=nullptr;
    std::uint8_t execution_mode=0;
    bool read_only=false,initial_inventory=false;
    std::shared_ptr<CombatViewAbort> combat_abort{};
    // Narrow tactical capability; all other inventory mutations remain guarded.
    std::function<ItemActionResult(std::size_t,std::uint64_t)> combat_ready{};
    // A damaging Ready returns its borrower before tactical recipient feedback.
    std::function<bool()> combat_ready_feedback_pending{};
    // Borrowed capabilities; the session outlives the suspended character view.
    static CharacterViewServices for_exploration(IItemActivationService& service,bool portraits) {
        CharacterViewServices result;result.activations=&service;result.portrait_enabled=portraits;return result;
    }
};
std::unique_ptr<IGameMode> make_exploration_mode(const IFileSystem&,const LaunchOptions&,std::ostream&,ISaveRepository* =nullptr,ISaveRepository* characters=nullptr);
std::unique_ptr<IGameMode> make_exploration_mode(const IFileSystem&,const LaunchOptions&,std::ostream&,ISaveRepository*,std::unique_ptr<ExplorationSession>,ISaveRepository* characters=nullptr);
std::unique_ptr<IGameMode> make_startup_mode(const IFileSystem&,const LaunchOptions&,std::ostream&,ISaveRepository*,ISaveRepository*);
std::unique_ptr<IGameMode> make_cale_mode(const IFileSystem&,const LaunchOptions&,std::ostream&,ISaveRepository*,ISaveRepository*);
std::unique_ptr<IGameMode> make_game_over_mode(std::unique_ptr<ExplorationSession>,const ExplorationScreen&,const IFileSystem&,const LaunchOptions&,std::ostream&,ISaveRepository*,ISaveRepository*);
std::unique_ptr<IGameMode> make_character_services_mode(ExplorationSession&,const IFileSystem&,const LaunchOptions&,std::ostream&,ISaveRepository*,ISaveRepository*);
std::unique_ptr<IGameMode> make_creation_icon_mode(Party&,const IFileSystem&,const ExplorationScreen&,std::function<void()>);
// Standalone browser, also hosted inline by idle exploration. Cancel preserves
// the owner; successful selection replaces it only after candidate validation.
std::unique_ptr<IGameMode> make_load_mode(const IFileSystem&,const LaunchOptions&,std::ostream&,ISaveRepository*,std::function<void()> cancel={},ISaveRepository* characters=nullptr);
std::unique_ptr<IGameMode> make_overland_mode(const IFileSystem&,const LaunchOptions&,std::ostream&);
std::unique_ptr<IGameMode> make_asset_viewer_mode(const IFileSystem&,std::string_view archive,std::ostream&);
std::unique_ptr<IGameMode> make_spell_mode(ExplorationSession&,const ExplorationScreen&,const PicturePresentation* backdrop=nullptr);
std::unique_ptr<IGameMode> make_campaign_mode(ExplorationSession&,const ExplorationScreen&);
std::unique_ptr<IGameMode> make_combat_mode(ExplorationSession&,const ExplorationScreen&);
std::unique_ptr<IGameMode> make_ending_mode(ExplorationSession&,const IFileSystem&,const ExplorationScreen&);
std::unique_ptr<IGameMode> make_encamp_mode(ExplorationSession&,const ExplorationScreen&,const IFileSystem&,ISaveRepository*,bool qol_upgrades=false);
// The suspended exploration mode owns these resources until this view leaves.
std::unique_ptr<IGameMode> make_character_view_mode(Party&,std::size_t member,const GameFont&,const IFileSystem&,CharacterViewServices={});
}
