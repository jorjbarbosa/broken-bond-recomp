#pragma once

#include <filesystem>

namespace broken_bond::translation {
// Called once after SDK setup and before the first guest thread is launched.
// Loaded dictionaries remain immutable for the session; changes require restart.
void Initialize(const std::filesystem::path& mods_dir);
}  // namespace broken_bond::translation
