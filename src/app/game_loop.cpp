#include "gbengine/debug/runtime.hpp"
#include "gbengine/app/game_loop.hpp"
#include <stdexcept>

namespace GBEngine {
void GameLoop::change_mode(std::unique_ptr<IGameMode> mode) {
    if(!mode) throw std::invalid_argument("Cannot enter a null game mode");
    if(transition_!=Transition::none) throw std::logic_error("A game mode transition is already pending");
    next_mode_=std::move(mode);transition_=Transition::replace;
}
void GameLoop::push_mode(std::unique_ptr<IGameMode> mode) {
    change_mode(std::move(mode));transition_=Transition::push;
}
void GameLoop::pop_mode() {
    if(transition_!=Transition::none) throw std::logic_error("A game mode transition is already pending");
    if(suspended_.empty()) throw std::logic_error("No suspended game mode to resume");
    transition_=Transition::pop;
}
void GameLoop::clear_modes() {
    if(auto* trace=Debug::Runtime::current())trace->event("mode_exit","{}");
    audio_.cancel();
    next_mode_.reset();mode_.reset();
    while(!suspended_.empty()) suspended_.pop_back();
    transition_=Transition::none;
}
bool GameLoop::install_terminal_mode() {
    std::unique_ptr<IGameMode> replacement;
    for(auto& owner:suspended_)if((replacement=owner->terminal_mode()))break;
    if(!replacement)replacement=mode_->terminal_mode();
    if(!replacement)return false;
    // The replacement already owns any transferred session. Destroy borrowers
    // before their suspended owners, and discard queued nonterminal navigation.
    if(auto* trace=Debug::Runtime::current())trace->event("mode_exit","{}");
    audio_.cancel();next_mode_.reset();transition_=Transition::none;
    mode_.reset();while(!suspended_.empty())suspended_.pop_back();
    mode_=std::move(replacement);services_.input.set_text_input(mode_->text_input_active());return true;
}
std::size_t GameLoop::run(std::unique_ptr<IGameMode> mode) {
    if(running_) throw std::logic_error("Game loop is already running");
    if(!mode) throw std::invalid_argument("Game loop requires a mode");
    if(options_.resolution_index>=resolutions.size()) throw std::invalid_argument("Invalid resolution preset");
    running_=true;quitting_=false;mode_=std::move(mode);audio_.reset();
    std::size_t presented=0;
    try {
        auto last=services_.clock.milliseconds();
        auto next_frame=last;
        std::uint32_t frame_remainder=0;
        while(!quitting_) {
            if(transition_!=Transition::none) {
                audio_.cancel();
                const auto transition=transition_;transition_=Transition::none;
                if(transition==Transition::push) {mode_->on_suspend();suspended_.push_back(std::move(mode_));mode_=std::move(next_mode_);}
                else if(transition==Transition::pop) {mode_=std::move(suspended_.back());suspended_.pop_back();mode_->on_resume();}
                else mode_=std::move(next_mode_);
                last=services_.clock.milliseconds();
            }
            services_.input.set_text_input(mode_->text_input_active());
            bool terminal_input_fence=false;
            for(auto event:services_.input.poll()) {
                if(event.key==Key::debug_snapshot && !Debug::enabled())event.key=Key::load;
                if(Debug::enabled())Debug::input(event,mode_->title());
                if(event.pressed && event.key==Key::quit) quit();
                else if(event.pressed && event.key==Key::scale) {
                    options_.resolution_index=(options_.resolution_index+1)%resolutions.size();
                    services_.graphics.set_resolution(resolutions[options_.resolution_index]);
                } else if(event.pressed && event.key==Key::debug_snapshot && Debug::enabled()) {}
                else if(!terminal_input_fence && transition_==Transition::none) mode_->handle_input(event,*this);
                if(!quitting_ && !terminal_input_fence && install_terminal_mode())terminal_input_fence=true;
                // Freeze mode-specific input as soon as navigation is pending,
                // while still honoring window quit and F5 in this input batch.
                if(quitting_) break;
            }
            if(quitting_) break;
            const auto now=services_.clock.milliseconds();
            audio_.advance(now>=last?now-last:0);
            const auto elapsed=now>=last?now-last:0;
            mode_->update(elapsed,*this);last=now;
            if(quitting_) break;
            install_terminal_mode();
            Framebuffer frame;frame.fill(0xff000000u);mode_->render(frame);
            auto* trace=Debug::Runtime::current();
            if(trace)trace->draw_feedback(frame);
            services_.graphics.set_title(mode_->title()+(Debug::enabled()?(trace->snapshot_confirmed()?" | SNAPSHOT SAVED":" | DEBUG: F8 win / F9 snapshot / F10 load"):""));
            if(const auto image=mode_->image_presentation())services_.graphics.present_image(frame,*image);
            else services_.graphics.present(frame);
            ++presented;
            mode_->after_present(*this);
            if(Debug::enabled())Debug::presented(frame,mode_->title(),elapsed);
            if(options_.observer) options_.observer->after_frame(frame,*this);
            if(options_.frame_limit && presented>=options_.frame_limit) quit();
            if(!quitting_) {
                // Retain the fractional millisecond: 33,33,34ms gives exactly
                // thirty intervals per second, including input/render work.
                next_frame+=1000/RuntimeTiming::frames_per_second;
                frame_remainder+=1000%RuntimeTiming::frames_per_second;
                if(frame_remainder>=RuntimeTiming::frames_per_second) {
                    ++next_frame;frame_remainder-=RuntimeTiming::frames_per_second;
                }
                const auto completed=services_.clock.milliseconds();
                if(completed<last || completed>next_frame) {
                    // A stall does not create a queue of catch-up updates.
                    next_frame=completed;frame_remainder=0;
                } else if(completed<next_frame) services_.clock.sleep(static_cast<std::uint32_t>(next_frame-completed));
            }
        }
        // Destroy modes while the platform and resources are still alive.
        clear_modes();services_.input.set_text_input(false);running_=false;
        return presented;
    } catch(...) {
        clear_modes();running_=false;
        try {services_.input.set_text_input(false);} catch(...) {}
        throw;
    }
}
}
