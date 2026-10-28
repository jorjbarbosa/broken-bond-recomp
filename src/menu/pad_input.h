// SDL gamepad navigation before the runtime. The SDK owns its own SDL reference
// later; this client never consumes the shared event queue.
#pragma once
#include <SDL3/SDL.h>
#include <rex/ui/virtual_key.h>
#include <array>
#include <chrono>
#include <functional>
#include <vector>
#include <string>
namespace broken_bond::menu {
class PadInput {
 public:
  bool Initialize();
  void Shutdown();
  void Poll(bool takes_input, const std::function<void(rex::ui::VirtualKey)>& key);
  void SetShortcut(std::string chord, std::function<void()> toggle) { chord_ = std::move(chord); toggle_ = std::move(toggle); }
  void Shortcut(bool held);
  // The same navigation path is used by physical and virtual controllers.
  void Navigate(uint32_t buttons, std::chrono::steady_clock::time_point now,
                const std::function<void(rex::ui::VirtualKey)>& key);
 private:
  bool initialized_ = false;
  uint32_t previous_ = 0;
  std::vector<SDL_Gamepad*> pads_;
  std::string chord_ = "Back+Start";
  std::function<void()> toggle_;
  bool held_ = false;
  std::array<std::chrono::steady_clock::time_point, 9> repeat_{};
};
}  // namespace broken_bond::menu
