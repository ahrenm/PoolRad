#pragma once
#include "gbengine/saves/save_repository.hpp"
#include <memory>
namespace GBEngine::Debug {
// Records successful storage operations without changing the save format.
std::unique_ptr<ISaveRepository> trace_repository(std::unique_ptr<ISaveRepository>,bool characters=false);
}
