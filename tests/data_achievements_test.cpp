// The SDK's achievement metadata and unlock state under a non-ASCII data folder
// (patches/rexglue-data-achievements-unicode.patch): restored, saved, migrated
// from the legacy format, and invalid files left as they are.
// Usage: broken_bond_data_achievements_test <new fixture folder>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/achievement_manager.h>

namespace fs = std::filesystem;
using rex::system::AchievementManager;

namespace {
void Check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void Write(const fs::path& path, std::string_view content) {
  std::ofstream output(path, std::ios::binary);
  output << content;
  Check(static_cast<bool>(output), "cannot write fixture");
}
std::string Read(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  Check(static_cast<bool>(input), "cannot read fixture");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const auto base = fs::absolute(rex::to_path(std::string_view(argv[1])));
    Check(!fs::exists(base), "fixture must be new");
    const auto root = base / fs::path(u"data 日本語");
    fs::create_directories(root);
    rex::InitLoggingEarly();
    auto logging = rex::BuildLogConfig(nullptr, "debug", {});
    logging.log_dir = rex::path_to_utf8(root / "logs");
    rex::InitLogging(logging);

    AchievementManager manager;
    const auto metadata = root / "achievements.toml";
    Write(metadata, "[[achievements]]\nid = 7\nlabel = 'Preserved achievement'\n");
    Check(manager.LoadMetadataFile(metadata), "Unicode metadata did not load");
    Check(manager.FindAchievement(7).has_value(), "metadata lost");

    const auto save = root / "55530825.achievements.toml";
    const std::string original = "[unlocked.7]\nfiletime = 123456789\n";
    Write(save, original);
    manager.SetUnlockSavePath(save);
    manager.LoadUnlockState();
    Check(manager.IsUnlocked(7) && manager.GetUnlockTime(7) == 123456789, "Unicode unlock state lost");
    Check(Read(save) == original, "loading rewrote the original save");
    manager.SaveUnlockState();
    AchievementManager reopened;
    reopened.SetUnlockSavePath(save);
    reopened.LoadUnlockState();
    Check(reopened.GetUnlockTime(7) == 123456789, "unlock time did not survive reopening");

    Write(save, "unlocked = [7]\n");
    manager.SetUnlockSavePath(save);
    manager.LoadUnlockState();
    Check(manager.IsUnlocked(7), "legacy unlock array was not restored");
    Check(Read(save).find("[unlocked.7]") != std::string::npos, "legacy format did not migrate");

    Write(save, "invalid = [");
    manager.SetUnlockSavePath(save);
    manager.LoadUnlockState();
    Check(!manager.IsUnlocked(7) && Read(save) == "invalid = [", "invalid save was changed");
    Write(metadata, "invalid = [");
    Check(!manager.LoadMetadataFile(metadata), "invalid metadata was accepted");
    rex::ShutdownLogging();
    std::cout << "data_achievements PASS: Unicode metadata and unlocks, timestamps, legacy format, invalid files kept\n";
  } catch (const std::exception& error) {
    std::cerr << "data_achievements FAIL: " << error.what() << '\n';
    return 1;
  }
}
