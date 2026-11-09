#include "gbengine/platform/hal.hpp"
#include <stdexcept>
#include <cctype>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
namespace GBEngine {
namespace {
std::string upper(std::string_view text) {std::string result(text);for(auto& c:result) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));return result;}
}
NativeFiles::NativeFiles(std::filesystem::path root) : root_(std::filesystem::canonical(root)) {
    if (!std::filesystem::is_directory(root_)) throw std::runtime_error("Asset root is not a directory");
}
Bytes NativeFiles::read_asset(std::string_view name) const {
    const std::filesystem::path leaf(name);
    if (leaf.empty() || leaf.has_parent_path() || leaf == "." || leaf == "..")
        throw std::runtime_error("Asset name must be a single filename");
    // DOS names are case insensitive even when the modern host filesystem is not.
    std::filesystem::path selected;
    for (const auto& p : std::filesystem::directory_iterator(root_)) {
        if (upper(p.path().filename().string()) == upper(name)) {
            if (!selected.empty()) throw std::runtime_error("Ambiguous asset filename");
            selected = p.path();
        }
    }
    if (selected.empty()) throw std::runtime_error("Missing asset: " + std::string(name));
    if (std::filesystem::canonical(selected).parent_path() != root_)
        throw std::runtime_error("Asset resolves outside data root");
    std::ifstream in(selected, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read asset: " + std::string(name));
    Bytes data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad()) throw std::runtime_error("Asset read failed");
    return data;
}
std::vector<std::string> NativeFiles::list_assets() const {
    std::vector<std::string> names;
    for (const auto& p : std::filesystem::directory_iterator(root_))
        if (p.is_regular_file()) names.push_back(p.path().filename().string());
    std::ranges::sort(names);
    return names;
}
}
