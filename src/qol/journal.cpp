#include "gbengine/qol/journal.hpp"
#include <algorithm>
#include <charconv>
#include <sstream>

namespace GBEngine {
namespace {
unsigned bit(JournalId id) {
    if(id.kind==JournalKind::entry && id.number>=1 && id.number<=58)return id.number-1;
    if(id.kind==JournalKind::tale && id.number>=1 && id.number<=23)return 76+id.number-1;
    const auto found=std::find(proclamation_numbers.begin(),proclamation_numbers.end(),id.number);
    if(id.kind==JournalKind::proclamation && found!=proclamation_numbers.end())return 58+static_cast<unsigned>(found-proclamation_numbers.begin());
    throw DecodeError("Invalid journal ID");
}
constexpr std::size_t first=0x3f8;
}
std::string journal_id(JournalId id) {
    (void)bit(id);const auto number=std::to_string(id.number);
    return std::string(id.kind==JournalKind::entry?"entry-":id.kind==JournalKind::tale?"tale-":"proclamation-")+std::string(3-number.size(),'0')+number;
}
bool JournalState::contains(JournalId id)const {const auto n=bit(id);return (known[n/8]&(1u<<(n%8)))!=0;}
void JournalState::unlock(JournalId id) {
    if(occupied)throw DecodeError("Journal storage is occupied by game data");
    const auto n=bit(id);known[n/8]|=static_cast<std::uint8_t>(1u<<(n%8));stored=true;
}
void JournalState::recover(const EclStateBanks& banks) {
    if(occupied)throw DecodeError("Journal storage is occupied by game data");
    if(std::any_of(banks.area1.begin()+first,banks.area1.end(),[](auto word){return word!=0;}))throw DecodeError("Journal storage collides with live game data");
    const auto entry=[&](unsigned number){unlock({JournalKind::entry,number});};
    const auto library=banks.raw_value(0x4a2f);
    for(const auto [mask,number]:std::array<std::pair<unsigned,unsigned>,5>{{{1,7},{2,19},{4,37},{8,8},{16,21}}})if(library&mask)entry(number);
    if(banks.raw_value(0x4aea)==1){entry(23);entry(14);}
    const auto kovel=banks.raw_value(0x4a48);
    if(kovel&1){entry(38);entry(51);}if(kovel&4)entry(41);if(kovel&8)entry(29);
    // Bit2 is set before combat's outcome; it cannot recover Entry48.
    // Kuto4A24 also marks capture/release, so never infer Entry50 from it.
    constexpr std::array<unsigned,9> current{101,126,134,154,114,204,129,201,114};
    const auto stage=banks.raw_value(0x4ac1);
    if(stage==0)for(unsigned n:{59u,64u,78u,109u})unlock({JournalKind::proclamation,n});
    else if(stage<=current.size()) {unlock({JournalKind::proclamation,current[stage-1]});if(stage==2)unlock({JournalKind::proclamation,110});}
    stored=true;
}
JournalState extract_journal(EclStateBanks& banks) {
    std::array<std::uint8_t,16> bytes{};
    for(unsigned i=0;i<8;++i){const auto w=banks.area1[first+i];bytes[2*i]=static_cast<std::uint8_t>(w);bytes[2*i+1]=static_cast<std::uint8_t>(w>>8);}
    JournalState result;
    if(std::all_of(bytes.begin(),bytes.end(),[](auto b){return b==0;}))return result;
    if(bytes[0]!='J' || bytes[1]!='R'){result.occupied=true;return result;}
    if(bytes[2]!=1)throw DecodeError("Unsupported journal metadata version");
    if(bytes[15]&0xf8)throw DecodeError("Invalid reserved journal bits");
    std::copy(bytes.begin()+3,bytes.end(),result.known.begin());result.stored=true;
    std::fill_n(banks.area1.begin()+first,8,std::uint16_t{0});return result;
}
void pack_journal(EclStateBanks& banks,const JournalState& journal) {
    if(!journal.stored)return;
    if(journal.occupied || (journal.known.back()&0xf8))throw DecodeError("Invalid journal metadata state");
    if(std::any_of(banks.area1.begin()+first,banks.area1.end(),[](auto w){return w!=0;}))throw DecodeError("Journal storage collides with game data");
    std::array<std::uint8_t,16> bytes{'J','R',1};std::copy(journal.known.begin(),journal.known.end(),bytes.begin()+3);
    for(unsigned i=0;i<8;++i)banks.area1[first+i]=static_cast<std::uint16_t>(bytes[2*i]|(bytes[2*i+1]<<8));
}
void JournalTracker::stage(JournalReceipt receipt) {
    if(pending && *pending!=receipt)throw DecodeError("Journal receipt owner changed before completion");
    pending=std::move(receipt);
}
JournalReceipt JournalTracker::completed(std::uint64_t token) {
    if(!pending || pending->token!=token)throw DecodeError("Stale Journal receipt completion");
    auto receipt=std::move(*pending);pending.reset();
    if(state.occupied) {receipt.ids.clear();receipt.status="occupied-storage";return receipt;}
    for(auto id:receipt.ids)state.unlock(id);
    return receipt;
}
}
