#pragma once
#include "gbengine/scripting/ecl_state.hpp"
namespace GBEngine {
// Selected DOS build's sequential SAVGAM?.DAT format, not a serialized C++ struct.
// The roster's unused bytes and unclassified location/mode bytes remain raw.
class OriginalSave {
    EclStateBanks banks_;
    Bytes script_;
    std::array<std::uint8_t,5> location_{};
    std::array<std::uint8_t,328> roster_storage_{};
    std::vector<std::string> roster_;
    std::uint8_t header_disk_{},presentation_mode_{},execution_mode_{};
public:
    static constexpr std::size_t file_size=0x3351;
    static OriginalSave decode(std::span<const std::uint8_t>);
    static OriginalSave load(const IFileSystem&,char slot);
    const EclStateBanks& banks() const { return banks_; }
    const Bytes& script_image() const { return script_; }
    const auto& location_bytes() const { return location_; }
    const auto& roster_storage() const { return roster_storage_; }
    const auto& roster() const { return roster_; }
    std::uint8_t header_disk() const { return header_disk_; }
    std::uint16_t resource_disk() const { return banks_.raw_value(0x6e12); }
    std::uint16_t ecl_record() const { return banks_.raw_value(0x49f2); }
    std::uint16_t geometry_record() const { return banks_.raw_value(0x49c5); }
    int x() const { return location_[0]<128?location_[0]:static_cast<int>(location_[0])-256; }
    int y() const { return location_[1]<128?location_[1]:static_cast<int>(location_[1])-256; }
    std::uint8_t native_direction() const { return location_[2]; }
    std::uint8_t presentation_mode() const { return presentation_mode_; }
    std::uint8_t execution_mode() const { return execution_mode_; }
};
}
