// The system file picker, owned by the game window, and the asynchronous
// identification of the game chosen in it. The future owns its work (no
// detached callbacks into an app that may have closed).
#pragma once
#include "game_image.h"
#include <chrono>
#include <filesystem>
#include <future>
#include <optional>
#include <string>

namespace broken_bond::menu {
struct FileChoice {
  std::filesystem::path path;
  std::string error;
};
// An empty path without an error: the player canceled.
FileChoice ChooseFile(void* owner, bool folder = false);
// Writes custom_game_root into the .toml, keeping its other keys.
bool SaveGameRoot(const std::filesystem::path& config, const std::filesystem::path& root,
                  std::string& error);
class GameChoice {
 public:
  bool Start(const std::filesystem::path& path);
  bool Busy() const { return result_.valid(); }
  bool Slow() const;
  // The root to play (a default.xex chosen gives its folder) and its identity.
  struct Result { std::filesystem::path root; game_image::Identity game; };
  std::optional<Result> Poll();
 private:
  std::chrono::steady_clock::time_point started_{};
  std::future<Result> result_;
};
}  // namespace broken_bond::menu
