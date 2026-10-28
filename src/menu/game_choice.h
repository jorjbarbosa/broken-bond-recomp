// The system file picker, owned by the game window.
#pragma once
#include <filesystem>
#include <string>

namespace broken_bond::menu {
struct FileChoice {
  std::filesystem::path path;
  std::string error;
};
// An empty path without an error: the player canceled.
FileChoice ChooseFile(void* owner, bool folder = false);
}  // namespace broken_bond::menu
