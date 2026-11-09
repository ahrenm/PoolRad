#pragma once
#include "gbengine/platform/native_installation.hpp"
namespace GBEngine {
bool journal_images_ready(const std::filesystem::path&);
// Known PDF fingerprint + precomputed JPEG ranges/crops. No OCR or PDF engine.
// Returns false when a ready cache was reused, true after a fresh extraction.
bool prepare_journal_images(const std::filesystem::path& pdf,const std::filesystem::path& output,const SetupProgress& ={});
}
