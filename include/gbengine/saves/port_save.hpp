#pragma once
#include "gbengine/platform/hal.hpp"
#include <memory>
#include "gbengine/saves/save_summary.hpp"

namespace GBEngine {
class ExplorationSession;
struct PortSaveState;
// Current-schema host snapshot; no migration of development saves.
// Original DOS files are immutable import resources.
// Only a stable, fully restored exploration session can be captured.
class PortSave {
    std::shared_ptr<const PortSaveState> state_;
    explicit PortSave(std::shared_ptr<const PortSaveState>);
    void restore_into(ExplorationSession&) const;
    friend class ExplorationSession;
public:
    static constexpr std::uint32_t format_version=6;
    static PortSave capture(const ExplorationSession&);
    static SaveSummary inspect(std::span<const std::uint8_t>);
    static PortSave decode(std::span<const std::uint8_t>);
    Bytes encode() const;
    void validate_sources(const IFileSystem&) const;
    unsigned disk() const;
    std::uint8_t record() const;
};
}
