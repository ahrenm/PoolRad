#pragma once
#include "gbengine/party/party.hpp"
#include <random>

namespace GBEngine {
enum class NpcStatus {applied,rejected,unresolved};
struct NpcResult {
    NpcStatus status=NpcStatus::rejected;
    std::string message;
    bool appended=false;
    bool applied() const {return status==NpcStatus::applied;}
};
// ECL36 is deliberately split: its second operand is evaluated against the
// selected member AFTER append (or the previous selected member at capacity).
// The session stages both calls and the VM answer as one transaction.
class NpcService {
public:
    static NpcResult append(Party&,const IFileSystem&,unsigned disk,std::uint8_t template_id,std::mt19937&);
    static NpcResult finish(Party&,std::uint8_t template_id,std::uint8_t parameter);
};
}
