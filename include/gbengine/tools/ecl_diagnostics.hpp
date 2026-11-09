#pragma once
#include "gbengine/scripting/ecl_vm.hpp"
#include <ostream>
namespace GBEngine {
int trace_ecl(const IFileSystem&,std::string_view archive,std::uint8_t record,
              std::size_t entry,std::size_t budget,std::uint32_t seed,bool trace,std::ostream&);
int inspect_ecl_corpus(const IFileSystem&,std::size_t budget,std::uint32_t seed,std::ostream&);
}
