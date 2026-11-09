#include "gbengine/scripting/ecl_state.hpp"
namespace GBEngine {
std::uint16_t EclStateBanks::raw_value(std::uint16_t address) const {
    const auto loc=locate_script_value(address);
    switch(loc.region) {
    case ScriptRegion::area1:return area1.at(loc.byte_offset/2);
    case ScriptRegion::area2:return area2.at(loc.byte_offset/2);
    case ScriptRegion::area3:return area3.at(loc.byte_offset/2);
    default:throw DecodeError("Raw state query requires a bank address");
    }
}
}
