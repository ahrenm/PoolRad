#pragma once
#include "gbengine/scripting/ecl_state.hpp"
#include "gbengine/app/game_mode.hpp"
#include "gbengine/rendering/presentation.hpp"
#include <filesystem>
#include <memory>

namespace GBEngine {
enum class JournalKind {entry,proclamation,tale};
struct JournalId {JournalKind kind;unsigned number;bool operator==(const JournalId&)const=default;};
inline constexpr std::array<unsigned,18> proclamation_numbers{59,64,78,101,109,110,114,120,126,129,134,154,156,159,190,201,204,214};
std::string journal_id(JournalId);
struct JournalState {
    std::array<std::uint8_t,13> known{};
    bool stored=false,occupied=false;
    bool contains(JournalId)const;
    void unlock(JournalId);
    void recover(const EclStateBanks&);
    bool operator==(const JournalState&)const=default;
};
// Only the wire copy carries JR1; the live VM's bank words remain untouched.
JournalState extract_journal(EclStateBanks&);
void pack_journal(EclStateBanks&,const JournalState&);
struct JournalReceipt {
    std::uint64_t token{};
    unsigned disk{},record{};
    std::uint16_t address{};
    std::string text_id,text,status;
    std::vector<JournalId> ids;
    bool operator==(const JournalReceipt&)const=default;
};
class JournalRules {
    struct Rule {unsigned disk,record,address;std::string resolver;std::vector<JournalId> ids;};
    std::vector<Rule> rules_;
public:
    explicit JournalRules(const IFileSystem& source);
    JournalRules(const IFileSystem& source,const IFileSystem& content);
    JournalReceipt resolve(std::uint64_t,unsigned,unsigned,std::uint16_t,std::string_view,std::string_view)const;
};
struct JournalTracker {
    JournalState state;
    std::optional<JournalReceipt> pending;
    void stage(JournalReceipt);
    JournalReceipt completed(std::uint64_t);
    bool operator==(const JournalTracker&)const=default;
};
struct JournalRecord {JournalId id;std::vector<std::string> images;};
class JournalCatalog {
    std::shared_ptr<const IFileSystem> files_;
    std::vector<JournalRecord> records_;
public:
    explicit JournalCatalog(std::shared_ptr<const IFileSystem>);
    const std::vector<JournalRecord>& records()const{return records_;}
    Image image(std::string_view)const;
};
// Generated content uses canonical RGB8 PNGs (filter0, stored DEFLATE).
Image decode_journal_png(std::span<const std::uint8_t>);
class ExplorationSession;
std::unique_ptr<IGameMode> make_journal_mode(ExplorationSession&,const GameFont&,const std::filesystem::path&);
}
