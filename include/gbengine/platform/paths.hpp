#pragma once
#include <filesystem>
#include <string_view>
#include <string>
namespace GBEngine {
inline std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(),text.end()));
}
inline std::string path_utf8(const std::filesystem::path& path) {
    const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};
}
}
