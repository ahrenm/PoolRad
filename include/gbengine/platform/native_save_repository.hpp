#pragma once
#include "gbengine/saves/save_repository.hpp"
#include <utility>
namespace GBEngine {
class NativeSaveRepository final:public ISaveRepository {
    std::filesystem::path root_,source_root_;
    void guard(const std::filesystem::path&) const;
    std::pair<std::uint64_t,std::filesystem::path> latest(char) const;
public:
    NativeSaveRepository(std::filesystem::path root,const std::filesystem::path& source_root);
    bool occupied(char) const override;
    Bytes load(char) const override;
    void save(char,std::span<const std::uint8_t>) override;
};
}
