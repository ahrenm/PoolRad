#pragma once
#include "gbengine/platform/hal.hpp"
#include <memory>
#include <iosfwd>
#include <string>
namespace GBEngine {
class ExplorationSession;
struct JournalReceipt;
namespace Debug {
// A thread-bound, App-owned optional facility. No debug state enters saves/VM.
class Runtime {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Runtime* previous_{};
public:
    Runtime(const std::filesystem::path&,const IFileSystem&,IArtifactWriter&,std::ostream&);
    ~Runtime();
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    void attach(ExplorationSession*);
    void detach(ExplorationSession*);
    bool event(std::string_view,std::string_view);
    bool healthy() const;
    void input(InputEvent,std::string_view);
    void presented(const Framebuffer&,std::string_view,std::uint64_t);
    void request_snapshot();
    void draw_feedback(Framebuffer&) const;
    bool snapshot_confirmed() const;
    static Runtime* current();
};
class SessionAttachment {
    Runtime* runtime_{};
    ExplorationSession* session_{};
public:
    explicit SessionAttachment(ExplorationSession&);
    ~SessionAttachment();
    SessionAttachment(const SessionAttachment&)=delete;
    SessionAttachment& operator=(const SessionAttachment&)=delete;
};
bool enabled();
void journal_receipt(const JournalReceipt&) noexcept;
void input(InputEvent,std::string_view);
void presented(const Framebuffer&,std::string_view,std::uint64_t);
// Pure snapshots: raw banks are copied, never evaluated through VM getters.
std::string inspect(const ExplorationSession&);
class Access {
public:
    static bool quick_win(ExplorationSession&,std::uint64_t token,std::uint64_t revision);
};
}
}
