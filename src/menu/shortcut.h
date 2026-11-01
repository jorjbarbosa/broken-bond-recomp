// Shell links target the game directly and keep only the session's explicit
// storage overrides; the saved data folder selection stays authoritative.
#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "data_paths.h"
namespace broken_bond::menu {
std::filesystem::path DesktopShortcut();
bool WriteShortcut(const std::filesystem::path& link, const std::filesystem::path& executable,
                   const data::DataPaths& paths, const std::vector<std::string>& extra_arguments = {});
}  // namespace broken_bond::menu
