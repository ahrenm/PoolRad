#include "gbengine/world/exploration_session.hpp"
#include <algorithm>

namespace GBEngine {
void ExplorationSession::refresh_world_art() {
    picture_.reset();sprite_.reset();glyph_text_.clear();
    overland_presented_=false;
    // Native world redraw clears visible art, not C8/C9 cache flags or the
    // retained6F81/82 selectors. EXIT/PICTURE FF handle those separately.
}
bool ExplorationSession::stage_sprite(const EclRequest& request,bool encounter_context) {
    if(picture_token_) {boundary="Presenting exploration sprite";return false;}
    try {
        const bool overhead=city_mode();
        if(!overhead && (vm.read_value(0x49e6)!=1 || !world.map))
            throw DecodeError("Exploration sprite requires a supported perspective or overland context");
        auto selection=sprite_selection_;auto cache=sprite_cache_;auto visible=sprite_;auto picture=picture_;
        auto closeup=sprite_closeup_shown_;auto selector=sprite_closeup_selector_;auto active=sprite_active_;
        bool area=area_view_;
        std::uint8_t distance{};
        if(request.kind==EclRequestKind::setup_sprite) {
            if(request.values.size()!=3)throw DecodeError("Malformed sprite setup request");
            selection=ExplorationSpriteSelection{static_cast<std::uint8_t>(request.values[0]),static_cast<std::uint8_t>(request.values[2])};
            int x=world.x,y=world.y;const auto facing=static_cast<unsigned>(world.facing);
            constexpr std::array dx{0,1,0,-1},dy{-1,0,1,0};
            if(facing>=4)throw DecodeError("Invalid sprite viewing direction");
            // Native0482 counts clear wall edges, not traversable doors.
            if(overhead)distance=2; // Native27A9:0482 skips wall probes for VM49E6=0.
            else {
                auto geometry=*world.map;geometry.id=record_;
                while(distance<2 && geometry.wall(x,y,world.facing)==0) {++distance;x+=dx[facing];y+=dy[facing];}
            }
            distance=static_cast<std::uint8_t>(std::min<std::uint16_t>(distance,request.values[1]));
        } else if(request.kind==EclRequestKind::approach_sprite) {
            if(request.values.size()!=2 || request.values[0]>3 || !request.values[0] || request.values[1]!=request.values[0]-1)
                throw DecodeError("Unsupported sprite approach distance");
            distance=static_cast<std::uint8_t>(request.values[1]);
        } else throw DecodeError("Invalid exploration sprite request");
        if(!selection)throw DecodeError("Sprite approach requires retained source selectors");
        if(!closeup) {
            if(!cache) {
                cache=ExplorationSpritePresentation::load_script(files_,disk(),selection->record,overhead?1:distance);
                active=true;area=false; // Native first SPRIT load exits Area.
            }
            // Native27A9:054E fixes one-based frame2 for every overhead
            // distance. Logical VM6DC1 still tracks the encounter distance.
            visible=cache->at_distance(overhead?1:distance);picture.reset();
        }
        if(!overhead && distance==0 && (!encounter_context || monster_pictures_enabled())) {
            const auto head=vm.read_value(0x6de1);
            if(closeup && !selector)throw DecodeError("Sprite close-up cache selector is unavailable");
            if(!closeup || *selector!=head) {
                // Native compares full VM6DE1 with a cached byte. FF selects
                // PIC; otherwise its low byte names HEAD and operand3 BODY.
                if(head>255)throw DecodeError("Unsupported sprite close-up selector width");
                auto loaded=PicturePresentation::load_sprite_closeup(files_,disk(),selection->body,static_cast<std::uint8_t>(head));
                // 1802:0716 reports an absent record with a null descriptor;
                // 4CE2:0000 then blits nothing. Keep the already drawn sprite
                // (or previous close-up), but retain the native attempted cache.
                if(loaded) {picture=std::move(*loaded);visible.reset();}
                closeup=true;selector=static_cast<std::uint8_t>(head);
            }
        }
        sprite_selection_=selection;sprite_cache_=std::move(cache);sprite_=std::move(visible);picture_=std::move(picture);
        sprite_active_=active;sprite_closeup_shown_=closeup;sprite_closeup_selector_=selector;area_view_=area;
        glyph_text_.clear();pending_sprite_distance_=distance;picture_token_=request.token;
        boundary="Presenting exploration sprite";return true;
    }catch(const DecodeError& error) {boundary="Exploration sprite unresolved: "+std::string(error.what());return false;}
}
}
