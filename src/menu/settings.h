// The settings pages of the game menu. Reuses the launcher's configuration,
// language packages and key binding rules, without its ImGui UI. Each edit is
// saved to broken_bond.toml before it changes anything.
#pragma once
#include "menu/panel.h"
#include "../menu/config_manager.h"
#include <filesystem>
#include <functional>
#include <unordered_set>
#include <optional>
namespace broken_bond::menu {
class Ui;
class Settings {
 public:
  struct MenuOptions { std::string key = "F1", chord = "Back+Start"; bool shaders = true; };
  const MenuOptions& menu_options() const { return menu_options_; }
  void SetUtilities(std::function<void()> shortcut, std::function<void()> updates) {
    shortcut_ = std::move(shortcut); updates_ = std::move(updates);
  }
  // native_controls: the four players' profiles drive the input (controls/);
  // off, the input keeps the cvars. config_path: the active .toml (the data
  // folder's), else broken_bond.toml in directory; the translation mods are
  // in the mods folder beside it.
  void Initialize(std::filesystem::path directory, bool native_controls = true,
                  std::filesystem::path config_path = {});
  void Open(Ui& ui, int tab, bool in_game, std::function<void()> choose_game,
            std::function<void(const std::string&)> changed, std::function<void()> closed);
  void Reload();
  // The edits made on the setup screen, into the session's cvars before the
  // game starts (the SDK loaded the .toml before the screen showed).
  bool ApplyBeforeGame() const;
  const GameConfig& config() const { return config_; }
  struct LiveResult { uint64_t revision = 0; bool applied = false; };
  void SetLiveSettings(std::function<uint64_t(const std::string&, const GameConfig&)> request,
                       std::function<LiveResult()> result) { request_live_ = std::move(request); result_live_ = std::move(result); }
  void PollLive();
  // The rows that apply while the game runs once SetLiveSettings is set; the
  // others keep RESTART.
  static bool Live(const std::string& key);
 private:
  std::vector<PanelRow> Rows(int group);
  void Change(const std::string& key, std::function<void(GameConfig&)> change);
  void CommitChange(const std::string& key, GameConfig next, std::string focus = {});
  bool Save(const GameConfig& config);
  void Redraw(std::string focus = {});
  // The controls page over the settings (Controls tab).
  void Controls();
  void SaveMenuOptions(const std::string& key, MenuOptions next);
  std::filesystem::path directory_;
  std::filesystem::path config_path_;
  GameConfig config_, initial_;
  std::unordered_set<std::string> edited_flags_;
  MenuOptions menu_options_, initial_menu_options_;
  std::function<void()> shortcut_, updates_;
  Ui* ui_ = nullptr;
  int group_ = 0;
  bool in_game_ = false;
  bool load_failed_ = false;
  bool controls_enabled_ = true;
  std::function<void()> choose_game_, closed_;
  std::function<void(const std::string&)> changed_;
  std::function<uint64_t(const std::string&, const GameConfig&)> request_live_;
  std::function<LiveResult()> result_live_;
  std::optional<GameConfig> pending_config_;
  std::string pending_key_;
  uint64_t pending_ticket_ = 0, panel_revision_ = 0;
};
}  // namespace broken_bond::menu
