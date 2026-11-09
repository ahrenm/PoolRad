#include "gbengine/platform/sdl_platform.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace GBEngine {
namespace {
void check(bool ok) { if (!ok) throw std::runtime_error(std::string("SDL: ")+SDL_GetError()); }
Key map_key(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP: return Key::up; case SDLK_DOWN: return Key::down;
    case SDLK_LEFT: return Key::left; case SDLK_RIGHT: return Key::right;
    case SDLK_RETURN: case SDLK_KP_ENTER: return Key::accept; case SDLK_ESCAPE: return Key::cancel;
    case SDLK_PAGEDOWN: return Key::next; case SDLK_PAGEUP: return Key::previous;
    case SDLK_HOME:return Key::home;case SDLK_END:return Key::end;
    case SDLK_F5: return Key::scale;
    case SDLK_F8: return Key::debug_win;
    case SDLK_F9: return Key::debug_snapshot;
    case SDLK_F10: return Key::load;
    case SDLK_F12: return Key::combat_abort;
    case SDLK_L: return Key::look;
    case SDLK_BACKSPACE:return Key::backspace;
    default: return Key::none;
    }
}
std::uint8_t keypad_digit(const SDL_KeyboardEvent& event) {
    // Physical keypad positions retain their command meaning with Num Lock
    // either way, even if SDL supplies a navigation keycode for that position.
    if(event.scancode>=SDL_SCANCODE_KP_1 && event.scancode<=SDL_SCANCODE_KP_9)
        return static_cast<std::uint8_t>('1'+event.scancode-SDL_SCANCODE_KP_1);
    if(event.scancode==SDL_SCANCODE_KP_0)return '0';
    // Also accept events which supply only the dedicated keypad keycode.
    if(event.key>=SDLK_KP_1 && event.key<=SDLK_KP_9)
        return static_cast<std::uint8_t>('1'+event.key-SDLK_KP_1);
    if(event.key==SDLK_KP_0)return '0';
    return 0;
}
void draw_image_presentation(SDL_Renderer* renderer,SDL_Texture* base,SDL_Texture*& artwork,
                             const Framebuffer& frame,const ImagePresentation& image) {
    if(image.width<=0||image.height<=0||image.width>2048||image.height>2048||
       image.pixels.size()!=static_cast<std::size_t>(image.width)*image.height||
       !std::isfinite(image.left)||!std::isfinite(image.top)||
       !std::isfinite(image.display_width)||!std::isfinite(image.display_height)||
       image.left<0||image.top<0||image.display_width<=0||image.display_height<=0||
       image.left+image.display_width>320||image.top+image.display_height>200)
        throw std::invalid_argument("Invalid image presentation");
    if(artwork&&(artwork->w!=image.width||artwork->h!=image.height)) {
        SDL_DestroyTexture(artwork);artwork=nullptr;
    }
    if(!artwork) {
        artwork=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,image.width,image.height);
        check(artwork!=nullptr);
        check(SDL_SetTextureScaleMode(artwork,SDL_SCALEMODE_LINEAR));
        check(SDL_SetTextureBlendMode(artwork,SDL_BLENDMODE_NONE));
    }
    check(SDL_UpdateTexture(base,nullptr,frame.data(),320*4));
    check(SDL_UpdateTexture(artwork,nullptr,image.pixels.data(),image.width*4));
    // Bypass the logical-resolution render target for original-resolution art.
    // Scale both layers to the same physical letterbox, with separate filters.
    check(SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED));
    int width=0,height=0;check(SDL_GetRenderOutputSize(renderer,&width,&height));
    const float scale=std::min(width/320.0f,height/200.0f);
    const SDL_FRect game{(width-320*scale)/2,(height-200*scale)/2,320*scale,200*scale};
    const SDL_FRect destination{game.x+image.left*scale,game.y+image.top*scale,
                                image.display_width*scale,image.display_height*scale};
    check(SDL_SetRenderDrawColor(renderer,0,0,0,255));check(SDL_RenderClear(renderer));
    check(SDL_RenderTexture(renderer,base,nullptr,&game));
    check(SDL_RenderTexture(renderer,artwork,nullptr,&destination));
}
}
struct SdlPlatform::Impl {
    SDL_Window* window=nullptr;
    SDL_Renderer* renderer=nullptr;
    SDL_Texture* texture=nullptr;
    SDL_Texture* artwork=nullptr;
    SDL_AudioStream* audio=nullptr;
    bool text_input=false;
    ~Impl() {
        if (audio) SDL_DestroyAudioStream(audio);
        if (texture) SDL_DestroyTexture(texture);
        if (artwork) SDL_DestroyTexture(artwork);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};
SdlPlatform::SdlPlatform(Resolution r,bool audio,bool hidden) : impl_(std::make_unique<Impl>()) {
    check(SDL_Init(SDL_INIT_VIDEO));
    impl_->window=SDL_CreateWindow("PoolRad",r.width,r.height,
        SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : 0));
    check(impl_->window != nullptr);
    impl_->renderer=SDL_CreateRenderer(impl_->window,nullptr);
    check(impl_->renderer != nullptr);
    check(SDL_SetRenderLogicalPresentation(impl_->renderer,320,200,SDL_LOGICAL_PRESENTATION_LETTERBOX));
    impl_->texture=SDL_CreateTexture(impl_->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,320,200);
    check(impl_->texture != nullptr);
    check(SDL_SetTextureScaleMode(impl_->texture,SDL_SCALEMODE_NEAREST));
    if (audio && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_AudioSpec spec{SDL_AUDIO_F32,1,48000};
        impl_->audio=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
        if(impl_->audio && !SDL_ResumeAudioStreamDevice(impl_->audio)) {
            SDL_DestroyAudioStream(impl_->audio);impl_->audio=nullptr;
        }
    }
}
SdlPlatform::~SdlPlatform() = default;
void SdlPlatform::present(const Framebuffer& frame) {
    if(impl_->artwork){SDL_DestroyTexture(impl_->artwork);impl_->artwork=nullptr;}
    check(SDL_SetRenderLogicalPresentation(impl_->renderer,320,200,SDL_LOGICAL_PRESENTATION_LETTERBOX));
    check(SDL_UpdateTexture(impl_->texture,nullptr,frame.data(),320*4));
    check(SDL_SetRenderDrawColor(impl_->renderer,0,0,0,255));
    check(SDL_RenderClear(impl_->renderer));
    check(SDL_RenderTexture(impl_->renderer,impl_->texture,nullptr,nullptr));
    check(SDL_RenderPresent(impl_->renderer));
}
void SdlPlatform::present_image(const Framebuffer& frame,const ImagePresentation& image) {
    draw_image_presentation(impl_->renderer,impl_->texture,impl_->artwork,frame,image);
    check(SDL_RenderPresent(impl_->renderer));
}
void SdlPlatform::set_resolution(Resolution r) {
    if (r.width <= 0 || r.height <= 0 || static_cast<std::int64_t>(r.width)*5 != static_cast<std::int64_t>(r.height)*8)
        throw std::invalid_argument("Resolution must preserve 320:200 aspect ratio");
    check(SDL_SetWindowSize(impl_->window,r.width,r.height));
}
void SdlPlatform::set_title(std::string_view title) {
    check(SDL_SetWindowTitle(impl_->window,std::string(title).c_str()));
}
void SdlPlatform::set_text_input(bool active) {
    if(active==impl_->text_input) return;
    check(active?SDL_StartTextInput(impl_->window):SDL_StopTextInput(impl_->window));
    impl_->text_input=active;
}
std::vector<InputEvent> SdlPlatform::poll() {
    std::vector<InputEvent> result;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type==SDL_EVENT_QUIT || event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            result.push_back({Key::quit,true});
        if(event.type==SDL_EVENT_TEXT_INPUT && impl_->text_input && event.text.text) {
            for(const unsigned char* p=reinterpret_cast<const unsigned char*>(event.text.text);*p;++p)
                if(*p>=32 && *p<=122) result.push_back({Key::none,true,*p});
        }
        if (event.type==SDL_EVENT_KEY_DOWN || event.type==SDL_EVENT_KEY_UP) {
            auto key=event.key.scancode==SDL_SCANCODE_KP_ENTER?Key::accept:map_key(event.key.key);
            const auto digit=keypad_digit(event.key);
            std::uint8_t character=0;
            if(!impl_->text_input && event.key.key==SDLK_P && (event.key.mod&SDL_KMOD_CTRL)) {
                key=Key::automatic_party;
            }
            else if(!impl_->text_input && event.key.key==SDLK_M && (event.key.mod&SDL_KMOD_ALT)) {
                key=Key::automatic_magic;
            }
            else if(digit) {
                key=Key::none;
                // Text editors receive characters solely from TEXT_INPUT.
                if(!impl_->text_input)character=digit;
            }
            else if(!impl_->text_input && event.key.key>=SDLK_A && event.key.key<=SDLK_Z)
                character=static_cast<std::uint8_t>('A'+event.key.key-SDLK_A);
            else if(!impl_->text_input && event.key.key>=SDLK_0 && event.key.key<=SDLK_9)
                character=static_cast<std::uint8_t>(event.key.key);
            else if(!impl_->text_input && event.key.key==SDLK_SPACE)character=' ';
            if ((key!=Key::none || character) && !event.key.repeat)
                result.push_back({key,event.type==SDL_EVENT_KEY_DOWN,character});
        }
    }
    return result;
}
void SdlPlatform::queue(std::span<const float> samples) {
    if (!impl_->audio) throw std::runtime_error("Audio is disabled");
    if (samples.size_bytes()>static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("PCM buffer too large");
    check(SDL_PutAudioStreamData(impl_->audio,samples.data(),static_cast<int>(samples.size_bytes())));
}
void SdlPlatform::stop() { if (impl_->audio) check(SDL_ClearAudioStream(impl_->audio)); }
std::uint64_t SdlPlatform::milliseconds() const { return SDL_GetTicks(); }
void SdlPlatform::sleep(std::uint32_t ms) { SDL_Delay(ms); }
}
