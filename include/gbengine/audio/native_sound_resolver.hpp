#pragma once
#include "gbengine/audio/presentation_audio.hpp"
#include <map>
namespace GBEngine {
enum class NativeSoundDevice {tandy,pc_speaker,disabled};
struct NativeSoundFrame {
    std::array<std::uint16_t,4> frequency{},volume{};
    bool operator==(const NativeSoundFrame&)const=default;
};
struct NativeSoundProgram {
    std::vector<NativeSoundFrame> frames;
    std::optional<std::size_t> loop_tick;
};
// Source18AE program interpreter, not execution of an original DOS image.
class NativeSoundResolver final:public IAudioCueResolver {
    Bytes driver_;
    NativeSoundDevice device_=NativeSoundDevice::disabled;
    std::map<std::uint16_t,ResolvedAudioCue> cache_;
public:
    static NativeSoundResolver load(const IFileSystem&,std::optional<NativeSoundDevice> override_device={});
    static NativeSoundDevice configuration(const IFileSystem&);
    NativeSoundDevice device()const{return device_;}
    std::optional<NativeSoundProgram> program(std::uint16_t command)const;
    std::optional<ResolvedAudioCue> resolve_program(std::uint16_t command)override;
    std::optional<std::vector<float>> resolve(std::uint16_t command)override;
};
}
