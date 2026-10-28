// GPU check of the menus (menu/ui.h) over the UI layer of the native Present,
// through a dispatcher as the Swaps deliver it. Over a green image: the frame
// rate document and its hiding; the game menu bar with only Exit, Enter and
// Esc; a dialog whose default is Cancel; a panel's arrows, tabs and Back; the
// menu strings in every language; the controller's navigation and shortcut;
// and the release of the UI's textures with the menus.
#include "gpu/backend/dispatcher.h"
#include "gpu/backend/tests/test_backend.h"
#include "gpu/ui/immediate_drawer.h"
#include "gpu/ui/layer.h"
#include "menu/assets.h"
#include "menu/pad_input.h"
#include "menu/text.h"
#include "menu/ui.h"
#include <windows.h>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <rex/ui/presenter.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>

using namespace gpu;

namespace gpu::sampler {
void RegisterThread(const char*) {}  // The sampler is not linked here.
}  // namespace gpu::sampler

namespace {
constexpr uint32_t kWidth = 640, kHeight = 360, kSurface = 0x1234;
std::array<int, 3> Pixel(const std::vector<uint8_t>& bgra, uint32_t x, uint32_t y) {
  const uint8_t* p = bgra.data() + (size_t(y) * kWidth + x) * 4;
  return {p[2], p[1], p[0]};
}
bool Near(const std::array<int, 3>& got, const std::array<int, 3>& want, int tolerance) {
  for (int c = 0; c < 3; ++c)
    if (std::abs(got[c] - want[c]) > tolerance) return false;
  return true;
}
std::string Json(const std::array<int, 3>& p) {
  return "[" + std::to_string(p[0]) + "," + std::to_string(p[1]) + "," + std::to_string(p[2]) + "]";
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 || std::filesystem::exists(argv[1])) return 2;
  std::ofstream output(argv[1]);
  if (!output) return 3;
  bool passed = true;
  auto check = [&](const std::string& kind, bool ok, const std::string& evidence) {
    passed &= ok;
    output << "{\"kind\":\"" << kind << "\",\"ok\":" << (ok ? "true" : "false")
           << ",\"evidence\":" << (evidence.empty() ? "null" : evidence) << "}\n";
    output.flush();
    return ok;
  };
  WNDCLASSW cls{};
  cls.lpfnWndProc = DefWindowProcW;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"NativeMenuTest";
  RegisterClassW(&cls);
  HWND window = CreateWindowW(cls.lpszClassName, L"Menu UI test", WS_POPUP, 0, 0, int(kWidth), int(kHeight),
                              nullptr, nullptr, cls.hInstance, nullptr);
  if (!check("window", window != nullptr, "{}")) return 4;
  ShowWindow(window, SW_SHOWNOACTIVATE);

  // An engine on the window over a green image, as the game's.
  std::string evidence;
  auto engine = std::make_unique<Dispatcher>(true);
  if (!check("initialize", engine->Initialize(evidence, TestRenderMode()), evidence)) return 5;
  PresentMode present;
  present.vsync = false;
  check("attach", engine->Run([&](Engine& e, std::string& out) {
    return e.AttachWindow(window, out, present) && e.CreateSurface(SurfaceDesc{kSurface, kWidth, kHeight}, out);
  }, evidence), evidence);
  ClearRequest clear;
  clear.color_keys[0] = kSurface;
  clear.clear_color = true;
  clear.color = {0, 1, 0, 1};
  check("green", engine->Run([&](Engine& e, std::string& out) {
    uint64_t fence = 0;
    return e.ClearSurfaces(clear, fence, out);
  }, evidence), evidence);

  ui::Layer layer;
  {
    ui::ImmediateDrawer drawer(layer);
    broken_bond::menu::Ui menu;
    check("ui-initialize", menu.Initialize(drawer, nullptr), "{}");
    check("no-embedded-game-art", broken_bond::menu::Asset("images/cover.png").empty() &&
          !broken_bond::menu::Asset("fonts/RobotoCondensed-Regular.ttf").empty(), "{}");
    rex::ui::AppUIDrawContext context(kWidth, kHeight);
    // One Swap: the delivery, then the Present read back.
    auto swap = [&](const std::string& kind, std::vector<uint8_t>& pixels) {
      check(kind + "-deliver", layer.Deliver(*engine), "{}");
      uint32_t w = 0, h = 0;
      return check(kind + "-present", engine->Run([&](Engine& e, std::string& out) {
        uint32_t slot = 0;
        return e.StageFront(kSurface, slot, out) && e.Present(slot, true, pixels, w, h, out);
      }, evidence) && w == kWidth && h == kHeight, evidence);
    };
    auto frame = [&](const std::string& kind, std::vector<uint8_t>& pixels) {
      menu.Draw(context);
      return swap(kind, pixels);
    };
    auto key = [&](rex::ui::VirtualKey value) {
      rex::ui::KeyEvent down(nullptr, value, 1, false, false, false, false, false);
      rex::ui::KeyEvent up(nullptr, value, 1, true, false, false, false, false);
      menu.OnKeyDown(down);
      menu.OnKeyUp(up);
      return down.is_handled();
    };
    auto changed_pixels = [](const std::vector<uint8_t>& pixels, uint32_t rows) {
      uint32_t count = 0;
      for (uint32_t y = 0; y < rows; ++y)
        for (uint32_t x = 0; x < kWidth; ++x) count += !Near(Pixel(pixels, x, y), {0, 255, 0}, 8);
      return count;
    };

    // The frame rate document in its corner.
    menu.SetFpsVisible(true);
    for (int i = 0; i < 30; ++i) menu.CountFrame();
    std::vector<uint8_t> shown;
    if (frame("fps", shown)) {
      // The panel (its left padding): #0E1220 at 0xD9 over green; the border: #F2701F; the text:
      // brighter than the panel.
      const auto panel = Pixel(shown, 6, 25), border = Pixel(shown, 1, 25), outside = Pixel(shown, 300, 200);
      check("fps-panel", Near(panel, {12, 53, 27}, 4), Json(panel));
      check("fps-border", Near(border, {242, 112, 31}, 4), Json(border));
      check("fps-outside", Near(outside, {0, 255, 0}, 1), Json(outside));
      uint32_t text = 0;
      for (uint32_t y = 8; y < 60; ++y)
        for (uint32_t x = 14; x < 120; ++x) {
          const auto p = Pixel(shown, x, y);
          text += p[0] + p[1] + p[2] > 300;
        }
      check("fps-text", text > 40, "{\"bright_pixels\":" + std::to_string(text) + "}");
    }
    menu.SetFpsVisible(false);
    std::vector<uint8_t> hidden;
    if (frame("hidden", hidden))
      check("hidden-green", changed_pixels(hidden, kHeight) == 0, "{}");

    // The game menu: F1 asks for it; the bar shows only the entries with an
    // action (Exit here), Enter runs the focused one and Esc closes.
    int toggles = 0, exits = 0, closes = 0;
    menu.EnableGameMenu("F1", "Back+Start", [&] { ++toggles; });
    check("menu-key-toggles", key(rex::ui::VirtualKey::kF1) && toggles == 1, "{}");
    menu.ShowOverlay({}, {}, [&] { ++exits; }, [&] { ++closes; menu.HideOverlay(); });
    std::vector<uint8_t> bar;
    if (frame("overlay", bar))
      check("overlay-drawn", menu.OverlayShown() && changed_pixels(bar, 80) > 1000,
            "{\"pixels\":" + std::to_string(changed_pixels(bar, 80)) + "}");
    check("overlay-takes-keys", key(rex::ui::VirtualKey::kReturn), "{}");
    frame("overlay-exit", bar);
    check("overlay-exit-focused-first", exits == 1 && closes == 0, "{}");
    key(rex::ui::VirtualKey::kEscape);
    frame("overlay-close", bar);
    check("overlay-escape-closes", closes == 1 && !menu.OverlayShown(), "{}");

    // A dialog: Cancel is focused by default.
    int confirmed = 0, canceled = 0;
    menu.ShowDialog("Unsupported version", "Choose another <game> & keep the current path",
                    {{"Choose another", [&] { ++confirmed; }}, {"Cancel", [&] { ++canceled; }}});
    std::vector<uint8_t> modal;
    frame("dialog", modal);
    key(rex::ui::VirtualKey::kReturn);
    frame("dialog-cancel", modal);
    check("dialog-cancel-default", canceled == 1 && confirmed == 0, "{}");

    // A panel: the arrows change a value, Page Down the tab, Esc closes.
    int changed = -1, tab = -1, closed = 0;
    broken_bond::menu::PanelRow option{"setting", "Option", "Off", "A reusable setting", false, true,
                                       {"Off", "On"}, 0, [&](int value) { changed = value; }, {}};
    menu.ShowPanel("Settings", {"Game", "Display"}, 0, {option}, [&](int value) { tab = value; },
                   [&] { ++closed; menu.HidePanel(); });
    frame("panel", modal);
    check("panel-shown", menu.PanelShown(), "{}");
    key(rex::ui::VirtualKey::kRight);
    frame("panel-change", modal);
    check("panel-arrow-changes-value", changed == 1, "{}");
    key(rex::ui::VirtualKey::kNext);
    frame("panel-tab", modal);
    check("panel-next-tab", tab == 1, "{}");
    key(rex::ui::VirtualKey::kEscape);
    frame("panel-back", modal);
    check("panel-back-closes", closed == 1 && !menu.PanelShown(), "{}");

    // The menu strings in each language.
    broken_bond::menu::TextSystem translations;
    for (const auto& language : Localization::GetLanguages()) {
      Localization::SetLanguage(language.language);
      std::string translated;
      check("language-" + std::string(language.code), translations.TranslateString(translated, "@exit-game") == 1 &&
            !translated.empty() && translated != "@exit-game" && !std::string(broken_bond::menu::Text("cancel")).empty(),
            "{}");
    }
    Localization::SetLanguage(Localization::Language::English);

    // The controller: repeated directions, one confirm per press, and the
    // shortcut once per press of the whole chord.
    broken_bond::menu::PadInput navigation;
    std::vector<rex::ui::VirtualKey> navigation_keys;
    const auto emit = [&](rex::ui::VirtualKey value) { navigation_keys.push_back(value); };
    const auto now = std::chrono::steady_clock::now();
    navigation.Navigate(2, now, emit);
    navigation.Navigate(2, now + std::chrono::milliseconds(100), emit);
    navigation.Navigate(2, now + std::chrono::milliseconds(400), emit);
    navigation.Navigate(0, now + std::chrono::milliseconds(450), emit);
    navigation.Navigate(16, now + std::chrono::milliseconds(500), emit);
    navigation.Navigate(16, now + std::chrono::milliseconds(1000), emit);
    check("pad-repeat-and-confirm-edge", navigation_keys == std::vector<rex::ui::VirtualKey>{
        rex::ui::VirtualKey::kDown, rex::ui::VirtualKey::kDown, rex::ui::VirtualKey::kReturn}, "{}");
    int shortcuts = 0;
    navigation.SetShortcut("Back+Start", [&] { ++shortcuts; });
    navigation.Shortcut(false);
    navigation.Shortcut(true);
    check("pad-shortcut-opens-on-press", shortcuts == 1, "{}");
    navigation.Shortcut(true);
    check("pad-shortcut-held-does-not-repeat", shortcuts == 1, "{}");
    navigation.Shortcut(false);
    navigation.Shortcut(true);
    check("pad-shortcut-release-allows-next-press", shortcuts == 2, "{}");
    menu.Shutdown();
  }
  layer.Deliver(*engine);  // The releases of the UI's textures.
  std::string counts;
  engine->Run([](Engine& e, std::string& out) { out = e.ResourceCounts(); return true; }, counts);
  check("released", counts.find("\"ui_textures\":0,") != std::string::npos, counts);
  check("release", engine->Run([](Engine& e, std::string& out) {
    return e.ReleaseRemaining(out) && e.DetachWindow(out);
  }, evidence), evidence);
  check("shutdown", engine->Shutdown(evidence), evidence);
  DestroyWindow(window);
  output << "{\"kind\":\"verdict\",\"passed\":" << (passed ? "true" : "false") << "}\n";
  return passed ? 0 : 1;
}
