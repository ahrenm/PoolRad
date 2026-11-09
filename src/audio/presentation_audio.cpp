#include "gbengine/audio/presentation_audio.hpp"
#include "gbengine/assets/decoders.hpp"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace GBEngine {
AudioCueResult PresentationAudio::submit(PresentationAudioCue cue) {
    if(!cue.frame.owner || !cue.frame.sequence)throw DecodeError("Audio cue requires an owned presentation frame");
    const auto order=[](const PresentationAudioCue& value) {
        return std::tuple{value.frame.owner,value.frame.sequence,value.frame.step,value.ordinal};
    };
    const auto domain=static_cast<unsigned>(cue.domain);
    if(domain>=last_.size())throw DecodeError("Unknown audio cue domain");
    auto& last=last_[domain];
    if(last && order(cue)<=order(*last)) {++stats_.stale;return AudioCueResult::stale;}
    last=cue;
    if(!audio_) {++stats_.disabled;return AudioCueResult::disabled;}
    if(cue.command==0 || cue.command==255) {cancel();++stats_.stopped;return AudioCueResult::stopped;}
    if(!resolver_) {++stats_.unresolved;return AudioCueResult::unresolved;}
    try {
        auto pcm=resolver_->resolve_program(cue.command);
        if(!pcm || pcm->samples.empty() || pcm->samples.size()>48000u*120u ||
           (pcm->loop_start && (*pcm->loop_start>=pcm->samples.size() || pcm->samples.size()-*pcm->loop_start<48)) ||
           std::any_of(pcm->samples.begin(),pcm->samples.end(),[](float sample){return !std::isfinite(sample) || sample < -1 || sample > 1;})) {
            ++stats_.unresolved;return AudioCueResult::unresolved;
        }
        if(cue.native_blocking) {
            pcm->samples.resize(std::min<std::size_t>(pcm->samples.size(),4800));pcm->loop_start.reset();
        }
        // Source1742/17AE initialize all four channels for a new command.
        // Replace the old owned queue instead of appending behind stale PCM.
        cancel();
        // A HAL implementation can fail after accepting part of a buffer.
        // Mark ownership first so failure still cancels that partial queue.
        loop_.clear();remaining_samples_=pcm->samples.size();millisecond_remainder_=0;
        if(pcm->loop_start)loop_.assign(pcm->samples.begin()+static_cast<std::ptrdiff_t>(*pcm->loop_start),pcm->samples.end());
        playing_=true;audio_->queue(pcm->samples);++stats_.queued;return AudioCueResult::queued;
    }catch(const std::exception&) {
        // Audio availability must not change gameplay, resource or RNG results.
        cancel();++stats_.unresolved;return AudioCueResult::unresolved;
    }
}
void PresentationAudio::advance(std::uint64_t elapsed_ms) noexcept {
    if(!playing_ || !audio_ || loop_.empty())return;
    // Catch up arithmetically; even a multi-day stall never appends a backlog.
    const auto elapsed=elapsed_ms>1000000?std::uint64_t{48000000}:elapsed_ms*48;
    remaining_samples_=elapsed>=remaining_samples_?0:remaining_samples_-elapsed;
    if(remaining_samples_>=4800)return;
    try {
        // At most one bounded buffer per host frame. Short native loops are
        // tiled to100ms once, avoiding thousands of device queue operations.
        std::vector<float> next;next.reserve(std::max<std::size_t>(4800,loop_.size()));
        do {next.insert(next.end(),loop_.begin(),loop_.end());}while(next.size()<4800);
        audio_->queue(next);remaining_samples_+=next.size();
    }catch(...){cancel();++stats_.unresolved;}
}
void PresentationAudio::cancel() noexcept {
    if(audio_ && playing_) {try {audio_->stop();}catch(...) {}}
    playing_=false;
    loop_.clear();remaining_samples_=0;millisecond_remainder_=0;
}
void PresentationAudio::reset() noexcept {cancel();last_={};stats_={};}
NativeSoundCues NativeSoundCues::load(const IFileSystem& files) {
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    constexpr std::size_t start=0xc7c0+0x2612;
    if(module.size()<start+28)throw DecodeError("Original sound command table is truncated");
    NativeSoundCues result;
    for(std::size_t i=0;i<result.commands_.size();++i) {
        result.commands_[i]=read_u16(module,start+2*i);
        if(result.commands_[i]!=static_cast<std::uint16_t>(i?i-1:255))throw DecodeError("Unsupported original sound command table");
    }
    return result;
}
std::uint16_t NativeSoundCues::command(NativeSoundCue cue) const {
    switch(cue) {
    case NativeSoundCue::spell_projectile:return commands_[3];
    case NativeSoundCue::damage:return commands_[4];
    case NativeSoundCue::effect:return commands_[5];
    case NativeSoundCue::defeat:return commands_[6];
    case NativeSoundCue::weapon_hit:return commands_[8];
    case NativeSoundCue::weapon_miss:return commands_[10];
    case NativeSoundCue::spell47:return commands_[12];
    case NativeSoundCue::spell51:return commands_[9];
    case NativeSoundCue::weapon_projectile:return commands_[7];
    case NativeSoundCue::weapon_projectile_end:return commands_[13];
    case NativeSoundCue::ecl_call8:return commands_[11];
    case NativeSoundCue::ecl_call10:return commands_[12];
    }
    throw DecodeError("Unknown original sound cue");
}
}
