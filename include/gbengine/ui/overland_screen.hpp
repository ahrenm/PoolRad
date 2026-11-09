#pragma once
#include "gbengine/rendering/city_assets.hpp"
#include "gbengine/rendering/presentation.hpp"
#include "gbengine/rendering/screen_chrome.hpp"
#include "gbengine/party/party.hpp"
#include "gbengine/world/overland.hpp"

namespace GBEngine {
// Rendering-only stage1 consumer. Owns source tiles and the six native party
// marker poses; no ECL, clock, campaign overlays or movement side effects.
class OverlandScreen {
    CityAssets map_;
    ScreenChrome chrome_;
    GameFont font_;
public:
    static OverlandScreen load(const IFileSystem&);
    const std::array<std::uint8_t,44*36>& cells() const {return map_.cells;}
    void draw(Framebuffer&,const OverlandPosition&,unsigned frame,const Party* party=nullptr) const;
};
}
