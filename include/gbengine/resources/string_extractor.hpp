#pragma once
#include "gbengine/resources/string_store.hpp"
namespace GBEngine {
// Selected-build extraction only. No game text or generated TOML is compiled in.
class StringExtractor {
public:
    static StringStore extract(const IFileSystem&,StringSourceScope=StringSourceScope::full_profile);
};
}
