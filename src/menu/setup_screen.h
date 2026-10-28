// The first page of the setup screen (setup.rml), shown before the game when
// the setup gate asks for it (setup_gate.h): the centered title or local logo, the menu
// (play, or choose the game when there is none ready; settings; DLC; quit)
// and the state of the game data, through the data model "setup". The app
// says what each choice does. RmlUi's thread (the SDK's UI thread).
#pragma once

#include "game_image.h"
#include "menu/setup_gate.h"
#include "menu/game_logo.h"
#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <RmlUi/Core/DataModelHandle.h>

namespace Rml {
class Context;
class ElementDocument;
}  // namespace Rml

namespace broken_bond::menu {
class RenderInterface;

class SetupScreen {
 public:
  struct State {
    SetupReason reason = SetupReason::kNone;
    game_image::Identity game;  // Of the game data root found, if any.
    std::string version;
    std::filesystem::path game_root;  // Validated folder or ISO, never an embedded artwork path.
  };
  // What the choices do; an empty one does nothing. They run inside RmlUi's
  // event handling: whatever closes the screen belongs after it.
  struct Actions {
    std::function<void()> play, choose_game, settings, dlc, quit;
  };
  bool Show(Rml::Context& context, const State& state, Actions actions);
  bool Refresh(Rml::Context& context, const State& state) {
    auto actions = actions_;
    Hide();
    return Show(context, state, std::move(actions));
  }
  void Hide();
  void SetGamepadHints(bool gamepad);
  // The logo of a ready game, read on a worker; the render interface gets it
  // once it is there (UI thread, each frame).
  void UpdateLogo(RenderInterface& render);
  bool Shown() const { return document_ != nullptr; }

 private:
  bool CreateModel(Rml::Context& context);
  void Run(const std::function<void()>& action);
  Rml::ElementDocument* document_ = nullptr;
  Rml::DataModelHandle model_;
  Actions actions_;
  bool game_ready_ = false, start_failed_ = false;
  bool logo_ready_ = false, logo_complete_ = false, disc_image_ = false;
  bool gamepad_hints_ = false;
  std::filesystem::path logo_root_, render_root_, pending_root_;
  std::future<std::optional<game_logo::Image>> logo_task_;
  std::string first_label_, status_title_, status_detail_, version_;
};

}  // namespace broken_bond::menu
