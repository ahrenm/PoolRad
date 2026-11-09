#pragma once
#include "gbengine/rendering/screen_chrome.hpp"
#include "gbengine/world/exploration_session.hpp"
#include <memory>
#include "gbengine/party/training_service.hpp"
namespace GBEngine {
enum class ExplorationCommand : unsigned {area,cast,view,encamp,search,look};
struct ExplorationUiState {
    ExplorationCommand focus=ExplorationCommand::area;
    std::string notice;
    // Modal presentation can highlight a recipient without selecting the caster
    // in Party or changing the ECL selection register.
    std::optional<std::size_t> highlighted_member;
    std::optional<std::string> command_line;
    bool narration_handoff=false; // Unread dialogue before another modal opens.
    bool combat_intro=false; // Host acknowledgement before opening the prepared battle.
    const PicturePresentation* picture_override=nullptr; // Borrowed modal backdrop.
};
// Composes native chrome, a viewport, party HUD, dialogue and command row.
// It owns no gameplay state; active character is ExplorationSession/Party's selection.
class ExplorationScreen {
    ScreenChrome chrome_;
    GameFont font_;
    std::optional<TrainingService> training_indicator_;
    std::unique_ptr<Framebuffer> staging_=std::make_unique<Framebuffer>();
    void party(Framebuffer&,const ExplorationSession&,const ExplorationUiState&) const;
    void commands(Framebuffer&,const ExplorationSession&,const ExplorationUiState&) const;
    void dialogue(Framebuffer&,const ExplorationSession&,const ExplorationUiState&) const;
public:
    static ExplorationScreen load(const IFileSystem&,bool qol_upgrades=false);
    const GameFont& font() const {return font_;}
    const ScreenChrome& chrome() const {return chrome_;}
    void draw(Framebuffer&,const ExplorationSession&,const ExplorationUiState&) const;
};
void render_area_map(Framebuffer&,const UiTiles&,const GeoMap&,int x,int y,Direction);
}
