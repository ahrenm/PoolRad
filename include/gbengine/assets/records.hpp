#pragma once
#include "gbengine/assets/decoders.hpp"
#include <optional>
namespace GBEngine {
// Absence in a successfully decoded archive is distinct from file/decode errors.
std::optional<Bytes> find_record(const IFileSystem&,std::string_view,std::uint8_t);
Bytes load_record(const IFileSystem&,std::string_view,std::uint8_t);
}
