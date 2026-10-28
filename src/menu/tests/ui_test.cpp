// GPU check of the menus (menu/ui.h) over the UI layer of the native Present,
// through a dispatcher as the Swaps deliver it. Over a green image: the frame
// rate document and its hiding; the game menu bar with only Exit, Enter and
// Esc; a dialog whose default is Cancel; a panel's arrows, tabs and Back; the
// settings pages saving to broken_bond.toml and their live rows waiting for
// the renderer's result; the DLC page; the setup screen, its own engine and
// the hand-over to the game's; the controls page with the keyboard, the mouse
// and SDL virtual gamepads; the menu strings in every language; the
// controller's navigation and shortcut; and the release of the UI's textures
// with the menus.
#include "controls/service.h"
#include "gpu/backend/dispatcher.h"
#include "gpu/backend/tests/test_backend.h"
#include "gpu/ui/immediate_drawer.h"
#include "gpu/ui/layer.h"
#include "gpu/ui/screen.h"
#include "menu/assets.h"
#include "menu/content_manager.h"
#include "menu/dlc.h"
#include "menu/pad_input.h"
#include "menu/settings.h"
#include "menu/text.h"
#include "menu/ui.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <utility>
#include <SDL3/SDL.h>
#include <rex/filesystem/devices/stfs_xbox.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/system/xam/content_manager.h>
#include <rex/ui/presenter.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <toml++/toml.hpp>
#include <RmlUi/Core.h>

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
  // Only the SDL virtual gamepads take part: the controllers on the desktop
  // are never assigned or rumbled.
  for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_JOYSTICK_RAWINPUT,
                           SDL_HINT_JOYSTICK_WGI, SDL_HINT_JOYSTICK_GAMEINPUT, SDL_HINT_XINPUT_ENABLED})
    SDL_SetHintWithPriority(hint, "0", SDL_HINT_OVERRIDE);
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
    return e.AttachWindow(platform::WindowHandles(window), out, present) && e.CreateSurface(SurfaceDesc{kSurface, kWidth, kHeight}, out);
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

    // The settings pages: each edit is saved at once, unknown keys survive.
    const auto settings_dir = std::filesystem::path(argv[1]).parent_path() / "settings";
    std::filesystem::create_directories(settings_dir);
    const auto settings_path = settings_dir / "broken_bond.toml";
    std::ofstream(settings_path) << "unknown_option = 42\nuser_language = 1\nfullscreen = false\n"
                                    "show_fps_overlay = false\nmnk_mode = true\nmnk_mouse = true\n";
    broken_bond::menu::Settings settings_model;
    settings_model.Initialize(settings_dir);
    check("first-run-controls-automatic", settings_model.config().controls.ports[0].device ==
          broken_bond::controls::kAutomaticDevice, "{}");
    std::vector<std::string> changed_keys;
    int settings_closed = 0;
    settings_model.Open(menu, 0, false, {}, [&](const std::string& key) { changed_keys.push_back(key); },
                        [&] { ++settings_closed; });
    frame("settings-game", modal);
    key(rex::ui::VirtualKey::kRight);
    frame("settings-language", modal);
    auto saved = toml::parse_file(settings_path.string());
    check("settings-save-language", saved["user_language"].value<int>() == 4 && saved["unknown_option"].value<int>() == 42,
          "{\"user_language\":" + std::to_string(saved["user_language"].value_or(0)) + "}");
    key(rex::ui::VirtualKey::kNext); frame("settings-display", modal);
    key(rex::ui::VirtualKey::kDown); key(rex::ui::VirtualKey::kDown); key(rex::ui::VirtualKey::kRight);
    frame("settings-resolution", modal);
    saved = toml::parse_file(settings_path.string());
    check("settings-fractional-resolution", saved["internal_resolution_scale"].value<double>() == 1.5 &&
          saved["resolution_scale"].value<int>() == 1, "{}");
    key(rex::ui::VirtualKey::kDown); key(rex::ui::VirtualKey::kRight);
    frame("settings-aspect", modal);
    saved = toml::parse_file(settings_path.string());
    check("settings-aspect-ratio", saved["aspect_ratio"].value<double>() == 0.0,
          "{\"aspect_ratio\":" + std::to_string(saved["aspect_ratio"].value_or(-9.0)) + "}");
    key(rex::ui::VirtualKey::kDown); key(rex::ui::VirtualKey::kReturn); frame("settings-number-description", modal);
    int numeric_choices = 0;
    for (int i = 0; i < Rml::GetContext("game")->GetNumDocuments(); ++i) {
      auto* document = Rml::GetContext("game")->GetDocument(i);
      if (auto* detail = document->IsVisible() ? document->GetElementById("detail") : nullptr) {
        Rml::ElementList choices; detail->GetElementsByClassName(choices, "choice"); numeric_choices += int(choices.size());
      }
    }
    check("numeric-value-keeps-description-panel", numeric_choices == 0, "{}");
    key(rex::ui::VirtualKey::kRight); frame("settings-number-adjusted", modal);
    saved = toml::parse_file(settings_path.string());
    check("numeric-arrow-adjustment-saved", std::abs(saved["brightness"].value_or(0.0) - .51) < .0001, "{}");
    for (int i = 0; i < 3; ++i) { key(rex::ui::VirtualKey::kNext); frame("settings-next-" + std::to_string(i), modal); }
    key(rex::ui::VirtualKey::kLeft); frame("settings-startup-menu-off", modal);
    saved = toml::parse_file(settings_path.string());
    check("startup-menu-disabled-in-advanced", saved["show_startup_menu"].value<bool>() == false, "{}");
    key(rex::ui::VirtualKey::kRight); frame("settings-startup-menu-on", modal);
    saved = toml::parse_file(settings_path.string());
    check("startup-menu-enabled-in-advanced", saved["show_startup_menu"].value<bool>() == true, "{}");
    key(rex::ui::VirtualKey::kDown); key(rex::ui::VirtualKey::kDown); key(rex::ui::VirtualKey::kDown);
    key(rex::ui::VirtualKey::kRight);
    frame("settings-fps", modal);
    saved = toml::parse_file(settings_path.string());
    check("settings-fps-saved", saved["show_fps_overlay"].value<bool>() == true, "{}");
    const auto edited = [&](const char* name) {
      return std::find(changed_keys.begin(), changed_keys.end(), name) != changed_keys.end();
    };
    check("settings-changes-reported", edited("user_language") && edited("resolution_scale") &&
          edited("aspect_ratio") && edited("brightness") && edited("show_fps_overlay"), "{}");
    key(rex::ui::VirtualKey::kEscape); frame("settings-closed", modal);
    check("settings-escape-closes", settings_closed == 1 && !menu.PanelShown(), "{}");

    // The DLC page: a copy that finishes after its page closed changes no UI;
    // a staged package shows its name, then shows once as installed.
    const auto visible_document = [&](const char* id) -> Rml::ElementDocument* {
      auto* context = Rml::GetContext("game");
      for (int i = 0; i < context->GetNumDocuments(); ++i) {
        auto* doc = context->GetDocument(i);
        if (doc->IsVisible() && doc->GetElementById(id)) return doc;
      }
      return nullptr;
    };
    const auto focus_id = [&](const char* id) {
      auto* context = Rml::GetContext("game");
      for (int i = 0; i < context->GetNumDocuments(); ++i) {
        auto* doc = context->GetDocument(i);
        if (!doc->IsVisible()) continue;
        if (auto* element = doc->GetElementById(id)) { element->Focus(); return true; }
      }
      return false;
    };

    // The live settings: the live rows lose RESTART; an edit is saved but waits
    // for the renderer's result; a confirmation commits it with the focus kept;
    // a refusal restores the saved value; a late result reopens nothing.
    {
      const auto live_dir = settings_dir / "live";
      std::filesystem::create_directories(live_dir);
      const auto live_path = live_dir / "broken_bond.toml";
      std::ofstream(live_path) << "brightness = 0.5\n";
      broken_bond::menu::Settings live;
      live.Initialize(live_dir);
      broken_bond::menu::Settings::LiveResult result;
      uint64_t ticket = 0;
      int changes = 0;
      live.SetLiveSettings([&](const std::string& key, const GameConfig&) { return key == "brightness" ? ++ticket : 0; },
                           [&] { return result; });
      live.Open(menu, 1, true, {}, [&](const std::string&) { ++changes; }, [] {});
      frame("live-settings-open", modal);
      auto* panel = visible_document("panel-title");
      auto* brightness = panel ? panel->GetElementById("brightness") : nullptr;
      auto* backend = panel ? panel->GetElementById("native_backend") : nullptr;
      check("live-restart-labels", brightness && backend &&
            brightness->GetInnerRML().find("restart") == std::string::npos &&
            backend->GetInnerRML().find("restart") != std::string::npos, "{}");
      const auto saved_brightness = [&] { return toml::parse_file(live_path.string())["brightness"].value_or(0.0); };
      focus_id("brightness"); key(rex::ui::VirtualKey::kRight); frame("live-settings-pending", modal);
      check("live-value-waits-for-renderer", ticket == 1 && changes == 0 && live.config().brightness == .5 &&
            saved_brightness() > .5, "{}");
      focus_id("frame_rate"); result = {ticket, true}; live.PollLive(); frame("live-settings-confirmed", modal);
      check("live-confirmation-keeps-focus", changes == 1 && std::abs(live.config().brightness - .51) < .0001 &&
            menu.PanelFocus() == "frame_rate", "{\"focus\":\"" + menu.PanelFocus() + "\"}");
      focus_id("brightness"); key(rex::ui::VirtualKey::kRight); frame("live-settings-refused-pending", modal);
      result = {ticket, false}; live.PollLive(); frame("live-settings-refused", modal);
      check("live-refusal-restores-saved-value", changes == 1 && std::abs(live.config().brightness - .51) < .0001 &&
            std::abs(saved_brightness() - .51) < .0001 && visible_document("dialog-title"), "{}");
      menu.HideDialog();
      live.Open(menu, 1, true, {}, [&](const std::string&) { ++changes; }, [] {});
      frame("live-settings-reopen", modal);
      focus_id("brightness"); key(rex::ui::VirtualKey::kRight); frame("live-settings-close-pending", modal);
      menu.HidePanel();
      const auto closed_revision = menu.ViewRevision();
      result = {ticket, true}; live.PollLive(); frame("live-settings-completed-after-close", modal);
      check("late-live-result-does-not-reopen-panel", !menu.PanelShown() && menu.ViewRevision() == closed_revision &&
            changes == 2, "{}");
    }
    {
      const auto source = settings_dir / "async-source";
      const auto user = settings_dir / "async-user";
      std::filesystem::remove_all(user);
      std::filesystem::create_directories(source);
      std::ofstream(source / "content.bin") << "late DLC content";
      menu.ShowOverlay({}, {}, {}, {});
      broken_bond::menu::Dlc dlc;
      dlc.Open(menu, settings_dir / "async-product/dlc", user, nullptr, [](auto action) { action(); }, {});
      dlc.InstallFolder(source);
      menu.HideOverlay();
      dlc.Wait(); dlc.Poll();
      frame("async-dlc-finished-after-close", modal);
      check("late-dlc-result-does-not-reopen-ui", !menu.OverlayShown() &&
            !menu.PanelShown() && !visible_document("dialog-title"), "{}");
      check("closed-menu-still-finishes-copy", std::filesystem::is_regular_file(
            user / "0000000000000000/55530825/00000002/async-source/content.bin"), "{}");
      menu.ShowOverlay({}, {}, {}, {});
      dlc.Open(menu, settings_dir / "async-product/dlc", user, nullptr, [](auto action) { action(); }, {});
      dlc.InstallFolder(source);
      settings_model.Open(menu, 0, true, {}, {}, {});
      dlc.Wait(); dlc.Poll();
      frame("async-dlc-finished-on-another-page", modal);
      check("late-dlc-result-does-not-replace-another-page", menu.PanelShown() &&
            !visible_document("dialog-title"), "{}");
      menu.HidePanel();
      menu.HideOverlay();
    }
    {
      const auto staging = settings_dir / "named-dlc-product/dlc";
      const auto user = settings_dir / "named-dlc-user";
      std::filesystem::remove_all(user);
      const std::string package = "3D56F90400DE6FBCED7852DF085F04AD75728F8F55";
      std::filesystem::create_directories(staging);
      rex::filesystem::StfsHeader metadata{};
      metadata.header.magic = rex::filesystem::XContentPackageType::kLive;
      metadata.metadata.set_display_name(rex::system::XLanguage::kEnglish, u"Chunin outfit Sasuke & co");
      std::ofstream(staging / package, std::ios::binary)
          .write(reinterpret_cast<const char*>(&metadata), sizeof(metadata));
      broken_bond::menu::Dlc dlc;
      dlc.Open(menu, staging, user, nullptr, [](auto action) { action(); }, {});
      frame("dlc-pending-name", modal);
      auto* document = visible_document("options");
      auto* pending = document ? document->GetElementById("staged-1") : nullptr;
      check("dlc-pending-shows-package-name", pending &&
            pending->GetInnerRML().find("Chunin outfit Sasuke &amp; co") != std::string::npos &&
            pending->GetInnerRML().find(package) == std::string::npos, "{}");
      menu.HidePanel();
      // Reopen after the game has extracted the content and written its header.
      const auto root = launcher_content::GetDlcRoot();
      std::filesystem::create_directories(root / package);
      std::ofstream(root / package / "content.bin") << "installed content";
      std::filesystem::create_directories(root.parent_path() / "Headers/00000002");
      rex::system::xam::XCONTENT_AGGREGATE_DATA header{};
      header.content_type = rex::system::XContentType::kMarketplaceContent;
      header.title_id = 0x55530825;
      header.set_file_name(package); header.set_display_name(u"Chunin outfit Sasuke & co");
      std::ofstream(root.parent_path() / "Headers/00000002" / (package + ".header"), std::ios::binary)
          .write(reinterpret_cast<const char*>(&header), sizeof(header));
      broken_bond::menu::Dlc reopened;
      reopened.Open(menu, staging, user, nullptr, [](auto action) { action(); }, {});
      frame("dlc-installed-name", modal);
      document = visible_document("options");
      auto* installed = document ? document->GetElementById("dlc-0") : nullptr;
      check("dlc-installed-keeps-name-without-pending-duplicate", installed && installed->IsClassSet("good") &&
            installed->GetInnerRML().find("Chunin outfit Sasuke &amp; co") != std::string::npos &&
            !document->GetElementById("staged-1") && !document->GetElementById("dlc-empty"), "{}");
      menu.HidePanel();
    }
    // The game menu bar shows DLC once it has an action.
    {
      int dlc_opened = 0;
      menu.ShowOverlay([] {}, [&] { ++dlc_opened; }, [] {}, [&] { menu.HideOverlay(); });
      frame("overlay-with-dlc", modal);
      auto* bar = visible_document("overlay-dlc");
      auto* entry = bar ? bar->GetElementById("overlay-dlc") : nullptr;
      check("overlay-dlc-entry-shown", entry && entry->IsVisible(), "{}");
      key(rex::ui::VirtualKey::kEscape);
      frame("overlay-with-dlc-closed", modal);
    }

    // The setup screen (setup_screen.h) at 1280 x 720 (one dp a pixel; the
    // window grows for it): the text title, the focus, its keys and mouse, the
    // frame rate hidden under it. Then the game's engine goes, the screen's own
    // engine on the window asks for frames as the Swaps do, and once it closes
    // a new game engine gets the setup document back.
    {
      constexpr uint32_t kSetupWidth = 1280, kSetupHeight = 720;
      SetWindowPos(window, nullptr, 0, 0, int(kSetupWidth), int(kSetupHeight), SWP_NOZORDER | SWP_NOACTIVATE);
      rex::ui::AppUIDrawContext setup_context(kSetupWidth, kSetupHeight);
      const auto at = [](const std::vector<uint8_t>& bgra, uint32_t x, uint32_t y) {
        const uint8_t* p = bgra.data() + (size_t(y) * kSetupWidth + x) * 4;
        return std::array<int, 3>{p[2], p[1], p[0]};
      };
      const auto setup_frame = [&](const std::string& kind, std::vector<uint8_t>& pixels) {
        menu.Draw(setup_context);
        check(kind + "-deliver", layer.Deliver(*engine), "{}");
        uint32_t w = 0, h = 0;
        return check(kind + "-present", engine->Run([&](Engine& e, std::string& out) {
          uint32_t slot = 0;
          return e.StageFront(kSurface, slot, out) && e.Present(slot, true, pixels, w, h, out);
        }, evidence) && w == kSetupWidth && h == kSetupHeight, evidence);
      };
      int played = 0, quits = 0, dlcs = 0;
      broken_bond::menu::SetupScreen::State state;
      state.reason = broken_bond::menu::SetupReason::kRequested;
      state.game.status = broken_bond::game_image::Status::kReady;
      state.game.disc_image = true;
      state.version = "0.0.0-test";
      broken_bond::menu::SetupScreen::Actions actions;
      actions.play = [&] { ++played; };
      actions.quit = [&] { ++quits; };
      actions.dlc = [&] { ++dlcs; };
      const std::array<int, 3> lit{245, 138, 53}, night{8, 15, 27};
      menu.SetFpsVisible(true);
      check("setup-show", menu.ShowSetup(state, actions) && menu.Shown() && menu.SetupShown(), "{}");
      std::vector<uint8_t> shown;
      if (setup_frame("setup", shown)) {
        const auto background = at(shown, 20, 20), fps = at(shown, 6, 25), play = at(shown, 500, 353),
                   settings = at(shown, 500, 407);
        check("setup-background", Near(background, night, 4), Json(background));
        check("setup-hides-fps", Near(fps, night, 6), Json(fps));
        check("setup-focus", Near(play, lit, 4), Json(play));
        check("setup-unfocused", !Near(settings, lit, 40), Json(settings));
        uint32_t title = 0;
        for (uint32_t y = 120; y < 225; ++y)
          for (uint32_t x = 450; x < 830; ++x) {
            const auto p = at(shown, x, y);
            title += p[0] > 150 && p[1] > 150 && p[2] > 150;
          }
        check("setup-text-title", title > 500, "{\"pixels\":" + std::to_string(title) + "}");
      }
      // Down moves the focus to Settings; Enter on Quit runs it.
      check("setup-key-taken", key(rex::ui::VirtualKey::kDown), "{}");
      std::vector<uint8_t> moved;
      if (setup_frame("setup-down", moved)) {
        const auto play = at(moved, 500, 353), settings = at(moved, 500, 407);
        check("setup-down-focus", Near(settings, lit, 4) && !Near(play, lit, 40),
              "{\"play\":" + Json(play) + ",\"settings\":" + Json(settings) + "}");
      }
      key(rex::ui::VirtualKey::kDown);
      key(rex::ui::VirtualKey::kDown);
      key(rex::ui::VirtualKey::kReturn);
      check("setup-quit", quits == 1 && played == 0, "{\"quits\":" + std::to_string(quits) + "}");
      // The mouse over DLC focuses it; a click runs it.
      rex::ui::MouseEvent move(nullptr, rex::ui::MouseEvent::Button::kNone, 500, 455);
      rex::ui::MouseEvent press(nullptr, rex::ui::MouseEvent::Button::kLeft, 500, 455);
      menu.OnMouseMove(move);
      menu.OnMouseDown(press);
      menu.OnMouseUp(press);
      check("setup-click", dlcs == 1 && move.is_handled(), "{\"dlcs\":" + std::to_string(dlcs) + "}");

      check("first-release", engine->Run([](Engine& e, std::string& out) {
        return e.ReleaseRemaining(out) && e.DetachWindow(out);
      }, evidence), evidence);
      check("first-shutdown", engine->Shutdown(evidence), evidence);
      engine.reset();
      std::atomic<int> requests{0};
      layer.SetFrameRequest([&] { ++requests; });
      {
        ui::Screen screen(layer);
        std::string opened;
        if (check("screen-open", screen.Open(window, TestRenderMode(), opened), opened.empty() ? "{}" : opened)) {
          for (int i = 0; i < 50 && requests < 10; ++i) Sleep(20);
          check("screen-frames", requests >= 10, "{\"requests\":" + std::to_string(requests.load()) + "}");
          screen.Close();
          check("screen-closed", !screen.IsOpen(), "{}");
        }
      }
      layer.SetFrameRequest({});
      engine = std::make_unique<Dispatcher>(true);
      check("second-initialize", engine->Initialize(evidence, TestRenderMode()), evidence);
      check("second-attach", engine->Run([&](Engine& e, std::string& out) {
        return e.AttachWindow(platform::WindowHandles(window), out, present) && e.CreateSurface(SurfaceDesc{kSurface, kWidth, kHeight}, out);
      }, evidence), evidence);
      check("second-green", engine->Run([&](Engine& e, std::string& out) {
        uint64_t fence = 0;
        return e.ClearSurfaces(clear, fence, out);
      }, evidence), evidence);
      std::vector<uint8_t> back;
      if (setup_frame("setup-handover", back)) {
        const auto dlc = at(back, 500, 455), art = at(back, 20, 20);
        check("setup-handover-ui", Near(dlc, lit, 4) && Near(art, night, 4),
              "{\"dlc\":" + Json(dlc) + ",\"art\":" + Json(art) + "}");
      }
      menu.HideSetup();
      menu.SetFpsVisible(false);
      check("setup-hidden", !menu.Shown() && !menu.SetupShown(), "{}");
      SetWindowPos(window, nullptr, 0, 0, int(kWidth), int(kHeight), SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // The controls page (controls.h) at 1280 x 720, as the setup screen: the
    // Controls tab, the keyboard's route through the page, modifier conflicts,
    // the inline capture saving each player's own bank, the device transfer
    // between players; then an SDL virtual gamepad (the only one the test sees:
    // the hints at the start hide the desktop's) for the pad capture, the live
    // preview, the dead zones, identify, rumble, the restore and hot-plugging;
    // then the menu shortcuts and a save the configuration file refuses.
    {
      constexpr uint32_t kControlsWidth = 1280, kControlsHeight = 720;
      SetWindowPos(window, nullptr, 0, 0, int(kControlsWidth), int(kControlsHeight), SWP_NOZORDER | SWP_NOACTIVATE);
      rex::ui::AppUIDrawContext controls_context(kControlsWidth, kControlsHeight);
      const auto at = [](const std::vector<uint8_t>& bgra, uint32_t x, uint32_t y) {
        const uint8_t* p = bgra.data() + (size_t(y) * kControlsWidth + x) * 4;
        return std::array<int, 3>{p[2], p[1], p[0]};
      };
      const auto frame = [&](const std::string& kind, std::vector<uint8_t>& pixels) {
        menu.Draw(controls_context);
        check(kind + "-deliver", layer.Deliver(*engine), "{}");
        uint32_t w = 0, h = 0;
        return check(kind + "-present", engine->Run([&](Engine& e, std::string& out) {
          uint32_t slot = 0;
          return e.StageFront(kSurface, slot, out) && e.Present(slot, true, pixels, w, h, out);
        }, evidence) && w == kControlsWidth && h == kControlsHeight, evidence);
      };
      const auto focused_id = [] {
        auto* e = Rml::GetContext("game")->GetFocusElement();
        return e ? e->GetId() : std::string{};
      };
      const auto dump = [&](const std::string& name) {
        std::ofstream(std::string(argv[1]) + "." + name + "-1280x720.bgra", std::ios::binary)
            .write(reinterpret_cast<const char*>(modal.data()), std::streamsize(modal.size()));
      };
      const auto settings_bytes = [&] {
        std::ifstream in(settings_path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
      };
      const auto open_settings = [&](const std::string& kind) {
        settings_model.Open(menu, 3, false, {}, [](const std::string&) {}, [] {});
        frame(kind, modal);
      };
      auto current = [] { return broken_bond::controls::Current(); };
      using broken_bond::controls::Hardware;

      // The Controls tab: full-width rows, labels before their values, no
      // text past the divider.
      open_settings("settings-controls");
      {
        auto* context = Rml::GetContext("game");
        Rml::Element* options = nullptr;
        for (int i = 0; context && i < context->GetNumDocuments(); ++i) {
          auto* document = context->GetDocument(i);
          if (document->IsVisible() && document->GetElementById("options")) options = document->GetElementById("options");
        }
        int rows = 0;
        bool valid = options != nullptr;
        for (int i = 0; options && i < options->GetNumChildren(); ++i) {
          auto* row = options->GetChild(i);
          if (!row->HasAttribute("row")) continue;
          ++rows;
          const auto size = row->GetBox().GetSize(Rml::BoxArea::Border);
          auto* label = row->GetChild(0);
          auto* value = row->GetChild(1);
          const auto label_right = label->GetAbsoluteOffset().x + label->GetBox().GetSize().x;
          valid &= size.x >= 530 && size.x <= 540 && size.y >= 39 && size.y <= 41;
          valid &= label_right <= value->GetAbsoluteOffset().x + 1;
        }
        check("controls-rows-have-full-width", valid && rows > 0, "{\"rows\":" + std::to_string(rows) + "}");
      }
      uint32_t overflow_text = 0;
      for (uint32_t y = 218; y < 246; ++y)
        for (uint32_t x = 678; x < 691; ++x) {
          const auto p = at(modal, x, y);
          overflow_text += p[0] + p[1] + p[2] > 300;
        }
      check("controls-text-clipped-before-divider", overflow_text == 0,
            "{\"overflow_pixels\":" + std::to_string(overflow_text) + "}");
      dump("controls");

      // The keyboard alone: categories, their fields, the alternatives, a
      // capture, and back out of the page.
      {
        menu.HidePanel();
        auto nav_config = broken_bond::controls::Config{};
        nav_config.version = 1;
        broken_bond::controls::Assign(nav_config, 0, "keyboard", "Keyboard and mouse");
        int nav_saves = 0;
        bool nav_closed = false;
        menu.ShowControls(nav_config, [&](const auto& next) { nav_config = next; ++nav_saves; return true; },
                          [&] { nav_closed = true; menu.HidePanel(); }, "F2");
        frame("focus-route-open", modal);
        check("focus-route-initial-category", focused_id() == "device-category", "{}");
        key(rex::ui::VirtualKey::kReturn); frame("focus-route-enter-device", modal);
        check("focus-route-category-enters-content", focused_id() == "device-auto" && nav_saves == 0, "{}");
        if (focused_id() == "device-auto") {
          key(rex::ui::VirtualKey::kEscape); frame("focus-route-back-sidebar", modal);
          check("focus-route-back-retains-window", !nav_closed && focused_id() == "device-category", "{}");
          key(rex::ui::VirtualKey::kDown); frame("focus-route-buttons-category", modal);
          check("focus-route-down-selects-category", focused_id() == "category-1", "{}");
          key(rex::ui::VirtualKey::kReturn); frame("focus-route-enter-buttons", modal);
          check("focus-route-buttons-first-field", focused_id() == "bind-0-0" && nav_saves == 0, "{}");
          key(rex::ui::VirtualKey::kRight); frame("focus-route-secondary-field", modal);
          check("focus-route-alternatives-accessible", focused_id() == "bind-0-1", "{}");
          key(rex::ui::VirtualKey::kReturn); frame("focus-route-inline-capture", modal);
          key(rex::ui::VirtualKey::kF5); frame("focus-route-binding-changed", modal);
          const auto alternatives = broken_bond::controls::Alternatives(nav_config.ports[0].keys[0]);
          check("focus-route-edits-without-mouse", nav_saves == 1 && alternatives.size() > 1 && alternatives[1] == "F5", "{}");
          key(rex::ui::VirtualKey::kEscape); frame("focus-route-buttons-back", modal);
          check("focus-route-return-selected-category", !nav_closed && focused_id() == "category-1", "{}");
          key(rex::ui::VirtualKey::kDown); key(rex::ui::VirtualKey::kDown); frame("focus-route-movement-category", modal);
          key(rex::ui::VirtualKey::kReturn); frame("focus-route-enter-movement", modal);
          check("focus-route-movement-first-field", focused_id() == "bind-12-0", "{}");
          key(rex::ui::VirtualKey::kEscape); frame("focus-route-exit-content", modal);
          key(rex::ui::VirtualKey::kUp); key(rex::ui::VirtualKey::kRight); frame("focus-route-enter-triggers", modal);
          check("focus-route-right-enters-triggers", focused_id() == "bind-6-0", "{}");
          key(rex::ui::VirtualKey::kDown); frame("focus-route-second-trigger", modal);
          check("focus-route-both-triggers-accessible", focused_id() == "bind-7-0", "{}");
          key(rex::ui::VirtualKey::kEscape); frame("focus-route-triggers-back", modal);
          key(rex::ui::VirtualKey::kUp); key(rex::ui::VirtualKey::kReturn); frame("focus-route-buttons-again", modal);
          for (int i = 0; i < 8; ++i) key(rex::ui::VirtualKey::kDown);
          frame("focus-route-direction-link", modal);
          check("focus-route-direction-link-accessible", focused_id() == "mapping-more", "{}");
          key(rex::ui::VirtualKey::kReturn); frame("focus-route-enter-directions", modal);
          check("focus-route-directions-fields", focused_id() == "bind-20-0", "{}");
          key(rex::ui::VirtualKey::kEscape); frame("focus-route-directions-back", modal);
          check("focus-route-directions-return-to-sidebar", focused_id() == "category-1", "{}");
          key(rex::ui::VirtualKey::kEscape); frame("focus-route-exit-window", modal);
          check("focus-route-close-from-sidebar", nav_closed, "{}");
        }
        menu.HidePanel();
        open_settings("focus-route-restore-settings");
      }

      // A modifier alone and a chord with it cannot share a bank; a binding may
      // still replace its own chord with the modifier.
      for (bool standalone_first : {false, true}) {
        broken_bond::controls::Config bindings;
        bindings.version = 1;
        broken_bond::controls::Assign(bindings, 0, "keyboard", "Keyboard and mouse");
        bindings.ports[0].keys = {};
        bindings.ports[0].keys[0] = standalone_first ? "Shift" : "Shift+G";
        int binding_saves = 0;
        menu.ShowControls(bindings, [&](const auto& next) { bindings = next; ++binding_saves; return true; }, {}, "F2");
        frame("modifier-conflict-open", modal);
        focus_id("category-1"); key(rex::ui::VirtualKey::kReturn); frame("modifier-conflict-buttons", modal);
        focus_id("bind-1-0"); key(rex::ui::VirtualKey::kReturn); frame("modifier-conflict-armed", modal);
        rex::ui::KeyEvent shift(nullptr, rex::ui::VirtualKey::kShift, 1, false, false, false, false, false);
        rex::ui::KeyEvent g(nullptr, rex::ui::VirtualKey::kG, 1, false, false, false, false, false);
        menu.OnKeyDown(shift);
        if (standalone_first) { menu.OnKeyDown(g); menu.OnKeyUp(g); }
        menu.OnKeyUp(shift);
        frame("modifier-conflict-rejected", modal);
        auto* doc = visible_document("controls-window");
        check(standalone_first ? "new-chord-preserves-standalone" : "new-standalone-preserves-chord",
              binding_saves == 0 && bindings.ports[0].keys[1].empty() && doc &&
              doc->GetElementById("bind-1-0")->IsClassSet("editing") &&
              doc->GetElementById("controls-status")->GetInnerRML().find(broken_bond::menu::Text("c-modifier-conflict")) !=
                  std::string::npos, "{}");
        key(rex::ui::VirtualKey::kEscape); frame("modifier-conflict-cancel", modal);
        if (!standalone_first) {
          focus_id("bind-0-0"); key(rex::ui::VirtualKey::kReturn); frame("modifier-replace-own-binding", modal);
          key(rex::ui::VirtualKey::kShift); frame("modifier-replace-own-binding-saved", modal);
          check("replacing-own-chord-with-modifier-is-allowed",
                binding_saves == 1 && bindings.ports[0].keys[0] == "Shift", "{}");
        }
        menu.HidePanel();
      }
      open_settings("modifier-conflict-restore-settings");

      // From the settings, with the keyboard assigned by hand (an automatic
      // owner yields to explicit assignments): the page, its capture saving
      // the player's own bank, and the keyboard moved through the players.
      auto manual_controls = settings_model.config();
      broken_bond::controls::Assign(manual_controls.controls, 0, "keyboard", "Keyboard and mouse");
      check("manual-controls-fixture", ConfigManager::Save(settings_path, manual_controls), "{}");
      settings_model.Reload();
      open_settings("manual-controls-settings");
      focus_id("configure-controls"); key(rex::ui::VirtualKey::kReturn); frame("controls-dedicated", modal);
      auto* controls_doc = visible_document("controls-window");
      check("controls-dedicated-window", controls_doc != nullptr, "{}");
      if (controls_doc) {
        check("controls-four-player-tabs", controls_doc->GetElementById("players")->GetNumChildren() == 4, "{}");
        focus_id("category-1"); key(rex::ui::VirtualKey::kReturn); frame("controls-keyboard-buttons", modal);
        check("controls-keyboard-bank", controls_doc->GetElementById("bind-0-0") != nullptr &&
              controls_doc->GetElementById("category-1")->IsClassSet("selected"), "{}");
        check("controls-long-key-name-has-space",
              controls_doc->GetElementById("bind-1-1")->GetBox().GetSize(Rml::BoxArea::Border).x >= 94, "{}");
        dump("controls-dedicated");
        focus_id("bind-0-0"); key(rex::ui::VirtualKey::kReturn); frame("controls-inline-wait", modal);
        dump("capture-inline");
        check("capture-stays-in-own-line", controls_doc->GetElementById("bind-0-0")->IsClassSet("editing") &&
              !visible_document("dialog-title"), "{}");
        key(rex::ui::VirtualKey::kNext); frame("capture-block-player", modal);
        check("capture-blocks-player-navigation", controls_doc->GetElementById("player-0")->IsClassSet("selected"), "{}");
        // Page Down is itself a valid binding: it binds without changing the
        // player, then a new capture replaces it with H.
        focus_id("bind-0-0"); key(rex::ui::VirtualKey::kReturn); frame("capture-key-rearm", modal);
        key(rex::ui::VirtualKey::kH); frame("capture-key-saved", modal);
        saved = toml::parse_file(settings_path.string());
        check("capture-saves-own-port-bank", saved["controls"]["player1"]["keys"]["keybind_a"].value<std::string>() == "H" &&
              saved["keybind_a"].value<std::string>() == "Space", "{}");
        focus_id("bind-0-0"); key(rex::ui::VirtualKey::kReturn); frame("capture-cancel-start", modal);
        key(rex::ui::VirtualKey::kEscape); frame("capture-canceled", modal);
        saved = toml::parse_file(settings_path.string());
        check("capture-Escape-preserves-binding",
              saved["controls"]["player1"]["keys"]["keybind_a"].value<std::string>() == "H", "{}");
        focus_id("bind-0-0"); key(rex::ui::VirtualKey::kReturn); frame("capture-delete-start", modal);
        key(rex::ui::VirtualKey::kDelete); frame("capture-deleted", modal);
        saved = toml::parse_file(settings_path.string());
        check("capture-Delete-removes-binding",
              saved["controls"]["player1"]["keys"]["keybind_a"].value<std::string>() == "", "{}");
        focus_id("bind-0-0"); key(rex::ui::VirtualKey::kReturn); frame("capture-reserved-start", modal);
        key(rex::ui::VirtualKey::kF1); frame("capture-reserved-key", modal);
        check("capture-reserved-stays-in-line", controls_doc->GetElementById("bind-0-0")->IsClassSet("editing"), "{}");
        key(rex::ui::VirtualKey::kEscape); frame("capture-reserved-cancel", modal);
        focus_id("bind-0-0"); key(rex::ui::VirtualKey::kReturn); frame("capture-mouse-start", modal);
        rex::ui::MouseEvent mouse_binding(nullptr, rex::ui::MouseEvent::Button::kLeft, 900, 250);
        menu.OnMouseDown(mouse_binding); menu.OnMouseUp(mouse_binding); frame("capture-mouse-saved", modal);
        saved = toml::parse_file(settings_path.string());
        check("capture-mouse-conflict-local", saved["controls"]["player1"]["keys"]["keybind_a"].value<std::string>() == "LMB" &&
              saved["controls"]["player1"]["keys"]["keybind_x"].value<std::string>() == ",J" &&
              saved["controls"]["player2"]["keys"]["keybind_x"].value<std::string>() == "LMB,J", "{}");
        focus_id("mapping-more"); key(rex::ui::VirtualKey::kReturn); frame("controls-keyboard-directions", modal);
        check("controls-directions-own-bank", controls_doc->GetElementById("bind-20-0") != nullptr, "{}");
        focus_id("mapping-more"); key(rex::ui::VirtualKey::kReturn); frame("controls-buttons-return", modal);
        focus_id("category-4"); key(rex::ui::VirtualKey::kReturn); frame("controls-movement-camera", modal);
        check("controls-keyboard-movement-camera", controls_doc->GetElementById("bind-16-0") &&
              controls_doc->GetElementById("plus-sensitivity"), "{}");
        auto* camera_second = controls_doc->GetElementById("bind-16-1");
        check("camera-binding-geometry", camera_second && camera_second->GetBox().GetSize().x >= 54,
              camera_second ? "{\"second\":" + std::to_string(camera_second->GetBox().GetSize(Rml::BoxArea::Border).x) + "}"
                            : "{}");
        dump("movement-camera");
        rex::ui::MouseEvent camera_baseline(nullptr, rex::ui::MouseEvent::Button::kNone, 900, 300);
        menu.OnMouseMove(camera_baseline);
        focus_id("bind-19-1"); key(rex::ui::VirtualKey::kReturn); frame("keyboard-camera-inline-capture", modal);
        rex::ui::MouseEvent camera_motion(nullptr, rex::ui::MouseEvent::Button::kNone, 880, 300, 0, 0, -20, 0);
        menu.OnMouseMove(camera_motion); frame("keyboard-camera-motion-bound", modal);
        saved = toml::parse_file(settings_path.string());
        check("keyboard-camera-motion-own-bank",
              saved["controls"]["player1"]["keys"]["keybind_rstick_right"].value<std::string>() == "Right,MouseLeft" &&
              saved["controls"]["player1"]["keys"]["keybind_rstick_left"].value<std::string>() == "Left" &&
              controls_doc->GetElementById("bind-16-0") && focused_id() == "bind-19-1", "{}");
        check("controls-keyboard-rumble-disabled", controls_doc->GetElementById("category-5")->HasAttribute("disabled"), "{}");
        key(rex::ui::VirtualKey::kNext); frame("controls-player-two", modal);
        check("controls-next-player", controls_doc->GetElementById("player-1")->IsClassSet("selected"), "{}");
        const auto keyboard_button = "device-" + std::to_string(Hardware().Devices()->devices.size() - 1);
        const auto before_transfer = settings_bytes();
        focus_id(keyboard_button.c_str()); key(rex::ui::VirtualKey::kReturn); frame("device-transfer-confirmation", modal);
        check("device-transfer-confirmation-before-change", controls_doc->GetElementById("transfer-confirm") &&
              current()->ports[0].device == "keyboard" && current()->ports[1].device.empty(), "{}");
        dump("transfer");
        focus_id("transfer-cancel"); key(rex::ui::VirtualKey::kReturn); frame("device-transfer-canceled", modal);
        check("device-transfer-cancel-does-not-save", settings_bytes() == before_transfer, "{}");
        focus_id(keyboard_button.c_str()); key(rex::ui::VirtualKey::kReturn); frame("device-transfer-reopen", modal);
        focus_id("transfer-confirm"); key(rex::ui::VirtualKey::kReturn); frame("device-transfer-committed", modal);
        saved = toml::parse_file(settings_path.string());
        check("device-transfer-preserves-both-profiles", saved["controls"]["player1"]["device"].value<std::string>() == "" &&
              saved["controls"]["player2"]["device"].value<std::string>() == "keyboard" &&
              saved["controls"]["player1"]["keys"]["keybind_a"].value<std::string>() == "LMB" &&
              saved["controls"]["player2"]["keys"]["keybind_a"].value<std::string>() == "Space", "{}");
        // The same keyboard through players three and four, without four pads.
        for (int player = 2; player < 4; ++player) {
          const auto n = std::to_string(player);
          key(rex::ui::VirtualKey::kNext); frame("device-transfer-port-" + n, modal);
          focus_id(keyboard_button.c_str()); key(rex::ui::VirtualKey::kReturn); frame("device-transfer-prompt-" + n, modal);
          focus_id("transfer-confirm"); key(rex::ui::VirtualKey::kReturn); frame("device-transfer-apply-" + n, modal);
          check("device-transfer-isolated-port-" + n, current()->ports[player].device == "keyboard" &&
                current()->ports[player - 1].device.empty(), "{}");
        }
        key(rex::ui::VirtualKey::kNext); frame("device-return-player-one", modal);
        focus_id(keyboard_button.c_str()); key(rex::ui::VirtualKey::kReturn); frame("device-return-keyboard-prompt", modal);
        focus_id("transfer-confirm"); key(rex::ui::VirtualKey::kReturn); frame("device-return-keyboard", modal);
        focus_id("device-none"); key(rex::ui::VirtualKey::kReturn); frame("device-none-selected", modal);
        check("device-none-effective", current()->ports[0].device.empty() && !rex::input::mnk::GetProfile()->enabled &&
              controls_doc->GetElementById("category-1")->HasAttribute("disabled"), "{}");
        focus_id(keyboard_button.c_str()); key(rex::ui::VirtualKey::kReturn); frame("device-keyboard-available-selected", modal);
        check("device-available-no-transfer", !controls_doc->GetElementById("transfer-confirm") &&
              current()->ports[0].device == "keyboard", "{}");
        key(rex::ui::VirtualKey::kNext); frame("controls-return-player-two", modal);
        key(rex::ui::VirtualKey::kPrior); frame("controls-player-one", modal);
        check("controls-previous-player", controls_doc->GetElementById("player-0")->IsClassSet("selected"), "{}");
      }
      menu.HidePanel();

      // An SDL virtual gamepad exercises the hardware's own polling path; its
      // rumble callback records what the page asks of the motors.
      SDL_VirtualJoystickDesc capture_pad;
      SDL_INIT_INTERFACE(&capture_pad);
      capture_pad.type = SDL_JOYSTICK_TYPE_GAMEPAD;
      capture_pad.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
      capture_pad.naxes = SDL_GAMEPAD_AXIS_COUNT;
      capture_pad.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
      capture_pad.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
      capture_pad.name = "Controls capture fixture";
      struct RumbleProbe { Uint16 low = 0, high = 0; int calls = 0; bool fail = false; } rumble_probe;
      capture_pad.userdata = &rumble_probe;
      capture_pad.Rumble = +[](void* data, Uint16 low, Uint16 high) -> bool {
        auto& p = *static_cast<RumbleProbe*>(data);
        p.low = low; p.high = high; ++p.calls;
        return !p.fail;
      };
      auto capture_id = SDL_AttachVirtualJoystick(&capture_pad);
      auto* capture_joystick = capture_id ? SDL_OpenJoystick(capture_id) : nullptr;
      check("capture-virtual-device-attached", capture_joystick != nullptr, "{}");
      if (capture_joystick) {
        Hardware().Poll("sdl", true);
        auto inventory = Hardware().Devices();
        const broken_bond::controls::Device* device = nullptr;
        for (const auto& d : inventory->devices)
          if (d.pad && SDL_GetGamepadID(d.pad) == capture_id) device = &d;
        check("capture-virtual-device-real-inventory", device != nullptr, "{}");
        if (device) {
          auto virtual_config = settings_model.config().controls;
          broken_bond::controls::Assign(virtual_config, 0, device->key, device->name);
          const auto publish = [&](const auto& next) {
            virtual_config = next; broken_bond::controls::Publish(next); return true;
          };
          const auto pad_key = [&](SDL_GamepadButton button, const std::string& label) {
            SDL_SetJoystickVirtualButton(capture_joystick, button, true); frame(label + "-down", modal);
            SDL_SetJoystickVirtualButton(capture_joystick, button, false); frame(label + "-up", modal);
          };
          menu.ShowControls(virtual_config, publish, [] {}, "F2");
          frame("capture-pad-window", modal);
          pad_key(SDL_GAMEPAD_BUTTON_DPAD_DOWN, "pad-focus-buttons-category");
          check("pad-focus-category-without-mouse", focused_id() == "category-1", "{}");
          pad_key(SDL_GAMEPAD_BUTTON_SOUTH, "pad-focus-enter-buttons");
          auto* pad_doc = visible_document("controls-window");
          check("pad-focus-content-without-capturing", focused_id() == "bind-0-0" &&
                !pad_doc->GetElementById("bind-0-0")->IsClassSet("editing"), "{}");
          // The press that starts a capture is not its binding.
          SDL_SetJoystickVirtualButton(capture_joystick, SDL_GAMEPAD_BUTTON_SOUTH, true);
          frame("capture-initiator-press", modal); frame("capture-initiator-held", modal);
          check("capture-does-not-bind-initiator", virtual_config.ports[0].pad[0] == broken_bond::controls::DefaultPad()[0] &&
                pad_doc->GetElementById("bind-0-0")->IsClassSet("editing"), "{}");
          SDL_SetJoystickVirtualButton(capture_joystick, SDL_GAMEPAD_BUTTON_SOUTH, false); frame("capture-initiator-release", modal);
          const std::pair<SDL_GamepadButton, int> buttons[] = {
              {SDL_GAMEPAD_BUTTON_EAST, 0x2000}, {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0x100},
              {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0x200}, {SDL_GAMEPAD_BUTTON_DPAD_UP, 1},
              {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 2}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 4},
              {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 8}, {SDL_GAMEPAD_BUTTON_LEFT_STICK, 0x40},
              {SDL_GAMEPAD_BUTTON_RIGHT_STICK, 0x80}};
          bool first_binding = true;
          for (auto [button, bit] : buttons) {
            const auto n = std::to_string(bit);
            if (!first_binding) { focus_id("bind-0-0"); key(rex::ui::VirtualKey::kReturn); frame("capture-pad-arm-" + n, modal); }
            first_binding = false;
            SDL_SetJoystickVirtualButton(capture_joystick, button, true); frame("capture-pad-source-" + n, modal);
            frame("capture-pad-held-" + n, modal);
            check("capture-pad-bound-no-menu-action-" + n, virtual_config.ports[0].pad[0].code == bit &&
                  pad_doc->GetElementById("player-0")->IsClassSet("selected") && menu.PanelShown(), "{}");
            SDL_SetJoystickVirtualButton(capture_joystick, button, false); frame("capture-pad-release-" + n, modal);
          }
          // Both directions of the four stick axes, then the two triggers.
          for (int sample = 0; sample < 10; ++sample) {
            const int axis = sample < 8 ? sample / 2 : sample - 4;
            const int input_value = axis < 4 && (sample % 2) ? -28000 : 28000;
            const int action = axis < 4 ? 12 : 6;
            const char* field = axis < 4 ? "bind-12-0" : "bind-6-0";
            const auto n = std::to_string(axis);
            focus_id(axis < 4 ? "category-4" : "category-3"); key(rex::ui::VirtualKey::kReturn); frame("capture-axis-page-" + n, modal);
            focus_id(field); key(rex::ui::VirtualKey::kReturn); frame("capture-axis-arm-" + n, modal);
            SDL_SetJoystickVirtualAxis(capture_joystick, axis, input_value); frame("capture-axis-value-" + n, modal);
            const auto& bound = virtual_config.ports[0].pad[action];
            const int sign = ((axis == 1 || axis == 3) ? -input_value : input_value) < 0 ? -1 : 1;
            check("capture-signed-axis-" + n, bound.kind == broken_bond::controls::SourceKind::Axis && bound.code == axis &&
                  bound.sign == sign && !pad_doc->GetElementById(field)->IsClassSet("editing"), "{}");
            SDL_SetJoystickVirtualAxis(capture_joystick, axis, axis < 4 ? 0 : SDL_JOYSTICK_AXIS_MIN);
            frame("capture-axis-release-" + n, modal);
          }
          // The pad's real readings drive the preview; held sticks neither
          // move the focus nor change the profile.
          virtual_config.ports[0].pad = broken_bond::controls::DefaultPad();
          virtual_config.ports[0].rumble = {.4, .7};
          broken_bond::controls::Publish(virtual_config);
          menu.HidePanel();
          {
            auto nav_config = virtual_config;
            int saves = 0;
            menu.ShowControls(nav_config, [&](const auto& next) { nav_config = next; ++saves; return true; }, [] {}, "F2");
            frame("pad-preview-route-open", modal);
            for (int i = 0; i < 5; ++i) pad_key(SDL_GAMEPAD_BUTTON_DPAD_DOWN, "pad-preview-route-category-" + std::to_string(i));
            check("pad-preview-category-reached", focused_id() == "category-deadzones", "{}");
            pad_key(SDL_GAMEPAD_BUTTON_SOUTH, "pad-preview-enter");
            check("pad-preview-fields-reached", focused_id() == "deadzone-toggle" && saves == 0, "{}");
            auto* preview_doc = visible_document("controls-window");
            const auto unchanged = nav_config;
            const int samples[] = {28000, -28000, -28000, 28000, 12000, 18000};
            for (int axis = 0; axis < 6; ++axis) SDL_SetJoystickVirtualAxis(capture_joystick, axis, samples[axis]);
            frame("pad-preview-full-range", modal); frame("pad-preview-held", modal);
            check("pad-preview-inputs-preserve-focus-and-config", focused_id() == "deadzone-toggle" && nav_config == unchanged &&
                  saves == 0 && preview_doc->GetElementById("player-0")->IsClassSet("selected"), "{}");
            const auto left = [&](const char* id) { return preview_doc->GetElementById(id)->GetProperty("left")->Get<float>(); };
            const auto width = [&](const char* id) { return preview_doc->GetElementById(id)->GetProperty("width")->Get<float>(); };
            check("pad-preview-both-sticks-and-triggers-live", left("raw-0") > 85 && left("raw-1") < 15 &&
                  width("trigger-raw-0") > 60 && width("trigger-raw-1") > 70, "{}");
            pad_key(SDL_GAMEPAD_BUTTON_DPAD_DOWN, "pad-preview-navigate-adjustment");
            check("pad-preview-dpad-reaches-adjustment", focused_id() == "minus-deadzone-0" || focused_id() == "plus-deadzone-0",
                  "{\"focus\":\"" + focused_id() + "\"}");
            pad_key(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "pad-preview-adjust");
            check("pad-preview-adjusts-with-stick-held", saves == 1 &&
                  std::abs(nav_config.ports[0].deadzone[0] - unchanged.ports[0].deadzone[0] - .01) < .001, "{}");
            pad_key(SDL_GAMEPAD_BUTTON_EAST, "pad-preview-back");
            check("pad-preview-back-to-category", focused_id() == "category-deadzones", "{}");
            pad_key(SDL_GAMEPAD_BUTTON_DPAD_UP, "pad-preview-leave-category");
            pad_key(SDL_GAMEPAD_BUTTON_SOUTH, "pad-preview-enter-rumble");
            const auto held_focus = focused_id();
            frame("pad-preview-exit-stick-held", modal);
            check("pad-preview-exit-no-stray-stick-navigation", held_focus == "minus-rumble-0" &&
                  focused_id() == held_focus && saves == 1, "{}");
            for (int axis = 0; axis < 6; ++axis)
              SDL_SetJoystickVirtualAxis(capture_joystick, axis, axis < 4 ? 0 : SDL_JOYSTICK_AXIS_MIN);
            frame("pad-preview-neutral", modal);
            SDL_SetJoystickVirtualAxis(capture_joystick, SDL_GAMEPAD_AXIS_LEFTY, 28000); frame("pad-preview-navigation-restored", modal);
            check("pad-preview-stick-navigation-after-neutral", focused_id() == "minus-rumble-1" && saves == 1,
                  "{\"focus\":\"" + focused_id() + "\"}");
            SDL_SetJoystickVirtualAxis(capture_joystick, SDL_GAMEPAD_AXIS_LEFTY, 0); frame("pad-preview-route-release", modal);
            menu.HidePanel();
          }
          // The dead zones on real drift and movement, their toggle and one
          // stick's adjustment alone.
          menu.ShowControls(virtual_config, publish, [] {}, "F2"); frame("adjustments-window", modal);
          pad_doc = visible_document("controls-window");
          focus_id("category-deadzones"); key(rex::ui::VirtualKey::kReturn); frame("deadzone-preview", modal);
          SDL_SetJoystickVirtualAxis(capture_joystick, SDL_GAMEPAD_AXIS_LEFTX, 2000); frame("deadzone-drift-reading", modal);
          auto* raw_dot = pad_doc->GetElementById("raw-0");
          auto* effective_dot = pad_doc->GetElementById("effective-0");
          const auto left_of = [](Rml::Element* e) { return e->GetProperty("left")->Get<float>(); };
          check("deadzone-preview-ignores-real-drift", raw_dot && effective_dot && left_of(raw_dot) > 50 &&
                std::abs(left_of(effective_dot) - 50) < .01, "{}");
          if (raw_dot) {
            const auto size = raw_dot->GetParentNode()->GetBox().GetSize();
            const auto parent = raw_dot->GetParentNode()->GetAbsoluteOffset();
            const auto point = raw_dot->GetAbsoluteOffset();
            check("deadzone-graph-layout-bounded", size.x > 100 && size.x < 120 && size.y > 100 && size.y < 120 &&
                  point.x >= parent.x && point.x < parent.x + size.x && point.y >= parent.y && point.y < parent.y + size.y, "{}");
          }
          SDL_SetJoystickVirtualAxis(capture_joystick, SDL_GAMEPAD_AXIS_LEFTX, 12000);
          SDL_SetJoystickVirtualAxis(capture_joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -25000);
          frame("deadzone-movement-reading", modal);
          check("deadzone-preview-real-movement", left_of(effective_dot) > 50 && left_of(raw_dot) > left_of(effective_dot), "{}");
          dump("deadzone-preview");
          focus_id("deadzone-toggle"); key(rex::ui::VirtualKey::kReturn); frame("deadzone-disabled", modal);
          raw_dot = pad_doc->GetElementById("raw-0");
          effective_dot = pad_doc->GetElementById("effective-0");
          check("deadzone-toggle-effective", !virtual_config.ports[0].deadzones &&
                std::abs(left_of(raw_dot) - left_of(effective_dot)) < .01, "{}");
          focus_id("deadzone-toggle"); key(rex::ui::VirtualKey::kReturn); frame("deadzone-enabled", modal);
          const auto right_deadzone = virtual_config.ports[0].deadzone[1];
          focus_id("plus-deadzone-0"); key(rex::ui::VirtualKey::kReturn); frame("deadzone-left-adjusted", modal);
          check("deadzone-adjust-isolated", std::abs(virtual_config.ports[0].deadzone[0] - .16) < .001 &&
                virtual_config.ports[0].deadzone[1] == right_deadzone, "{}");
          SDL_SetJoystickVirtualAxis(capture_joystick, SDL_GAMEPAD_AXIS_LEFTX, 0);
          SDL_SetJoystickVirtualAxis(capture_joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
          frame("deadzone-restored-rest", modal);
          // Identify rumbles briefly without assigning; the rumble test uses
          // the player's intensities and stops on its button, on leaving the
          // page, on another player, on focus loss, at its deadline, on a
          // failure and on closing.
          focus_id("device-category"); key(rex::ui::VirtualKey::kReturn); frame("identify-device-list", modal);
          auto before_identify = virtual_config;
          focus_id("identify-0"); key(rex::ui::VirtualKey::kReturn); frame("identify-running", modal);
          check("identify-brief-real-backend-no-assignment", Hardware().RumbleActive() && rumble_probe.low > 0 &&
                virtual_config == before_identify, "{}");
          Sleep(300); frame("identify-finished", modal);
          check("identify-stops", !Hardware().RumbleActive() && rumble_probe.low == 0 && rumble_probe.high == 0, "{}");
          const auto motors_stopped = [&] { return !rumble_probe.low && !rumble_probe.high && !Hardware().RumbleActive(); };
          focus_id("category-5"); key(rex::ui::VirtualKey::kReturn); frame("rumble-page", modal);
          focus_id("rumble-test"); key(rex::ui::VirtualKey::kReturn); frame("rumble-start", modal);
          check("rumble-backend-intensities", Hardware().RumbleActive() && std::abs(int(rumble_probe.low) - 26214) <= 1 &&
                std::abs(int(rumble_probe.high) - 45874) <= 1, "{}");
          check("rumble-adjustments-disabled", pad_doc->GetElementById("plus-rumble-0")->HasAttribute("disabled"), "{}");
          focus_id("rumble-test"); key(rex::ui::VirtualKey::kReturn); frame("rumble-stopped", modal);
          check("rumble-stop-button", motors_stopped(), "{}");
          focus_id("rumble-test"); key(rex::ui::VirtualKey::kReturn); frame("rumble-restart", modal);
          focus_id("category-1"); key(rex::ui::VirtualKey::kReturn); frame("rumble-stop-on-category", modal);
          check("rumble-stops-on-leave", motors_stopped(), "{}");
          focus_id("category-5"); key(rex::ui::VirtualKey::kReturn); frame("rumble-player-page", modal);
          focus_id("rumble-test"); key(rex::ui::VirtualKey::kReturn); frame("rumble-player-start", modal);
          key(rex::ui::VirtualKey::kNext); frame("rumble-player-changed", modal);
          check("rumble-stops-on-player-change", motors_stopped(), "{}");
          key(rex::ui::VirtualKey::kPrior); frame("rumble-player-return", modal);
          focus_id("category-5"); key(rex::ui::VirtualKey::kReturn); frame("rumble-focus-page", modal);
          focus_id("rumble-test"); key(rex::ui::VirtualKey::kReturn); frame("rumble-focus-start", modal);
          bool input_focused = false;
          menu.SetInputActive([&] { return input_focused; }); frame("rumble-focus-lost", modal);
          check("rumble-stops-on-focus-loss", motors_stopped(), "{}");
          input_focused = true;
          menu.SetInputActive({});
          focus_id("rumble-test"); key(rex::ui::VirtualKey::kReturn); frame("rumble-expiry-start", modal);
          Sleep(5100); frame("rumble-expired", modal);
          check("rumble-has-deadline", motors_stopped(), "{}");
          rumble_probe.fail = true;
          focus_id("rumble-test"); key(rex::ui::VirtualKey::kReturn); frame("rumble-backend-failure", modal);
          check("rumble-failure-visible", !Hardware().RumbleActive() &&
                pad_doc->GetElementById("controls-status")->GetInnerRML() == broken_bond::menu::Text("c-rumble-failed"), "{}");
          rumble_probe.fail = false;
          focus_id("rumble-test"); key(rex::ui::VirtualKey::kReturn); frame("rumble-close-start", modal);
          menu.HidePanel();
          check("rumble-stops-on-close", motors_stopped(), "{}");
          // The restore puts one player's both banks back to the defaults and
          // keeps the device; cancel keeps everything.
          const auto hardware_key = virtual_config.ports[0].device, hardware_name = virtual_config.ports[0].device_name;
          for (int restored_player : {0, 2}) {
            const auto n = std::to_string(restored_player);
            auto& port = virtual_config.ports[restored_player];
            port.pad[0] = {broken_bond::controls::SourceKind::Button, 0x8000};
            port.keys[0] = "F5";
            port.sensitivity = 2.42;
            port.deadzone[1] = .42;
            port.rumble = {.12, .34};
            broken_bond::controls::Assign(virtual_config, size_t(restored_player), hardware_key, hardware_name);
            broken_bond::controls::Publish(virtual_config);
            int restore_saves = 0;
            menu.ShowControls(virtual_config, [&](const auto& next) {
              ++restore_saves; virtual_config = next; broken_bond::controls::Publish(next); return true;
            }, [] {}, "F2");
            frame("restore-window-" + n, modal);
            if (restored_player) { focus_id("player-2"); key(rex::ui::VirtualKey::kReturn); frame("restore-player-three", modal); }
            pad_doc = visible_document("controls-window");
            const auto before_restore = virtual_config;
            focus_id("category-restore"); key(rex::ui::VirtualKey::kReturn); frame("restore-confirmation-" + n, modal);
            key(rex::ui::VirtualKey::kReturn); frame("restore-default-cancel-" + n, modal);
            check("restore-cancel-preserves-state-" + n, virtual_config == before_restore && restore_saves == 0, "{}");
            focus_id("category-restore"); key(rex::ui::VirtualKey::kReturn); frame("restore-reopen-" + n, modal);
            focus_id("restore-confirm"); key(rex::ui::VirtualKey::kReturn); frame("restore-applied-" + n, modal);
            auto expected = broken_bond::controls::Port{};
            expected.device = hardware_key;
            expected.device_name = hardware_name;
            bool isolated = virtual_config.ports[restored_player] == expected;
            for (int i = 0; i < 4; ++i)
              if (i != restored_player) isolated &= virtual_config.ports[i] == before_restore.ports[i];
            check("restore-both-banks-isolated-keeps-device-" + n, isolated && restore_saves == 1, "{}");
            menu.HidePanel();
          }
          // Two pads of the same model stay distinct; disconnecting stops the
          // rumble, cancels a capture and invalidates a pending transfer; a
          // reconnected pad is a new device.
          broken_bond::controls::Assign(virtual_config, 0, hardware_key, hardware_name);
          broken_bond::controls::Publish(virtual_config);
          menu.ShowControls(virtual_config, publish, [] {}, "F2"); frame("hotplug-return-controls", modal);
          pad_doc = visible_document("controls-window");
          focus_id("category-3"); key(rex::ui::VirtualKey::kReturn); frame("hotplug-return-triggers", modal);
          const auto twin_id = SDL_AttachVirtualJoystick(&capture_pad);
          auto* twin = SDL_OpenJoystick(twin_id);
          frame("same-model-second-connected", modal);
          const auto twins = Hardware().Devices();
          const broken_bond::controls::Device* second_device = nullptr;
          for (const auto& d : twins->devices)
            if (d.pad && SDL_GetGamepadID(d.pad) == twin_id) second_device = &d;
          check("same-model-distinct-identities", second_device && second_device->key != virtual_config.ports[0].device &&
                second_device->name != virtual_config.ports[0].device_name, "{}");
          if (second_device) {
            broken_bond::controls::Assign(virtual_config, 1, second_device->key, second_device->name);
            broken_bond::controls::Publish(virtual_config);
            menu.HidePanel();
            menu.ShowControls(virtual_config, publish, [] {}, "F2"); frame("same-model-controls", modal);
            pad_doc = visible_document("controls-window");
            focus_id("category-1"); key(rex::ui::VirtualKey::kReturn); frame("same-model-buttons", modal);
            key(rex::ui::VirtualKey::kNext); frame("same-model-next-player", modal);
            check("player-switch-preserves-available-category", pad_doc->GetElementById("bind-0-0") &&
                  pad_doc->GetElementById("player-1")->IsClassSet("selected"), "{}");
            key(rex::ui::VirtualKey::kPrior); frame("same-model-return-player", modal);
            focus_id("category-3"); key(rex::ui::VirtualKey::kReturn); frame("same-model-return-triggers", modal);
          }
          SDL_CloseJoystick(twin); SDL_DetachVirtualJoystick(twin_id); frame("same-model-second-disconnected", modal);
          focus_id("category-5"); key(rex::ui::VirtualKey::kReturn); frame("rumble-disconnect-page", modal);
          focus_id("rumble-test"); key(rex::ui::VirtualKey::kReturn); frame("rumble-disconnect-start", modal);
          check("rumble-active-before-disconnect", Hardware().RumbleActive(), "{}");
          SDL_CloseJoystick(capture_joystick); SDL_DetachVirtualJoystick(capture_id); capture_joystick = nullptr;
          frame("rumble-disconnected", modal);
          // SDL retires the disconnected device, so no motor write reaches it:
          // the check is the cancellation and the invalidation.
          check("rumble-stops-on-disconnect", !Hardware().RumbleActive() &&
                !broken_bond::controls::FindDevice(*Hardware().Devices(), virtual_config.ports[0].device), "{}");
          capture_id = SDL_AttachVirtualJoystick(&capture_pad);
          capture_joystick = SDL_OpenJoystick(capture_id);
          frame("rumble-reconnected", modal);
          focus_id("device-category"); key(rex::ui::VirtualKey::kReturn); frame("rumble-reassign-device-page", modal);
          focus_id("device-0"); key(rex::ui::VirtualKey::kReturn); frame("rumble-reassign-device", modal);
          focus_id("category-3"); key(rex::ui::VirtualKey::kReturn); frame("rumble-return-triggers", modal);
          auto before_disconnect = virtual_config;
          focus_id("bind-6-0"); key(rex::ui::VirtualKey::kReturn); frame("hotplug-capture-start", modal);
          SDL_CloseJoystick(capture_joystick); SDL_DetachVirtualJoystick(capture_id); capture_joystick = nullptr;
          frame("hotplug-capture-disconnect", modal);
          check("hotplug-cancels-capture-preserves-profile", virtual_config == before_disconnect &&
                !pad_doc->GetElementById("bind-6-0")->IsClassSet("editing"), "{}");
          const auto reconnected_id = SDL_AttachVirtualJoystick(&capture_pad);
          auto* reconnected = SDL_OpenJoystick(reconnected_id);
          frame("hotplug-reconnected-device", modal);
          check("hotplug-does-not-alias-old-handle",
                broken_bond::controls::FindDevice(*Hardware().Devices(), virtual_config.ports[0].device) == nullptr, "{}");
          focus_id("device-category"); key(rex::ui::VirtualKey::kReturn); frame("hotplug-device-page", modal);
          focus_id("device-0"); key(rex::ui::VirtualKey::kReturn); frame("hotplug-device-reassigned", modal);
          key(rex::ui::VirtualKey::kNext); frame("hotplug-target-player", modal);
          focus_id("device-0"); key(rex::ui::VirtualKey::kReturn); frame("hotplug-transfer-start", modal);
          check("hotplug-transfer-prompt", pad_doc->GetElementById("transfer-confirm") != nullptr, "{}");
          auto before_stale = virtual_config;
          SDL_CloseJoystick(reconnected); SDL_DetachVirtualJoystick(reconnected_id); frame("hotplug-transfer-invalidated", modal);
          check("hotplug-invalidates-stale-transfer", !pad_doc->GetElementById("transfer-confirm") &&
                virtual_config == before_stale, "{}");
          menu.HidePanel();
        }
        if (capture_joystick) { SDL_CloseJoystick(capture_joystick); SDL_DetachVirtualJoystick(capture_id); }
        Hardware().Poll("sdl", true);
      }
      menu.HidePanel();

      // The menu key and the controller chord on the Controls tab, then the
      // shader indicator on the Advanced one.
      open_settings("menu-bind-controls");
      focus_id("bind_game_menu"); key(rex::ui::VirtualKey::kReturn); frame("menu-bind-capture", modal);
      key(rex::ui::VirtualKey::kF2); frame("menu-bind-saved", modal);
      focus_id("game_menu_chord"); key(rex::ui::VirtualKey::kRight); frame("menu-chord-saved", modal);
      saved = toml::parse_file(settings_path.string());
      check("menu-shortcuts-persisted", saved["bind_game_menu"].value<std::string>() == "F2" &&
            saved["game_menu_chord"].value<std::string>() == "LB+RB+Start", "{}");
      key(rex::ui::VirtualKey::kNext); frame("menu-advanced", modal);
      focus_id("show_shader_indicator"); key(rex::ui::VirtualKey::kRight); frame("menu-shaders-saved", modal);
      saved = toml::parse_file(settings_path.string());
      check("shader-indicator-option-persisted", saved["show_shader_indicator"].value<bool>() == false, "{}");
      menu.HidePanel();

      // A configuration file that no longer parses is neither overwritten nor
      // published by a capture.
      const auto preserved_configuration = settings_bytes();
      const auto effective_before_failure = *current();
      std::ofstream(settings_path) << "malformed = [";
      open_settings("invalid-config-settings");
      focus_id("configure-controls"); key(rex::ui::VirtualKey::kReturn); frame("invalid-config-controls", modal);
      focus_id("category-1"); key(rex::ui::VirtualKey::kReturn); frame("invalid-config-bindings", modal);
      focus_id("bind-0-0"); key(rex::ui::VirtualKey::kReturn); frame("invalid-config-capture", modal);
      key(rex::ui::VirtualKey::kF5); frame("invalid-config-save-refused", modal);
      check("invalid-config-not-overwritten-or-published", settings_bytes() == "malformed = [" &&
            *current() == effective_before_failure && visible_document("dialog-title"), "{}");
      menu.HideDialog();
      menu.HidePanel();
      std::ofstream(settings_path, std::ios::binary) << preserved_configuration;
      settings_model.Reload();
      SetWindowPos(window, nullptr, 0, 0, int(kWidth), int(kHeight), SWP_NOZORDER | SWP_NOACTIVATE);
    }

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
    // The navigation over the hardware inventory: the chord needs both buttons
    // and opens once a press; on the preview the sticks do not navigate, and a
    // stick still held when it closes waits for neutral.
    {
      broken_bond::menu::PadInput shortcut_navigation;
      broken_bond::controls::Inventory inputs;
      inputs.devices.emplace_back();
      auto& input = inputs.devices.front().raw;
      int chords = 0;
      shortcut_navigation.SetShortcut("Back+Start", [&] { ++chords; });
      input.buttons = 0x10; shortcut_navigation.PollDevices(inputs, false, emit);
      input.buttons = 0; shortcut_navigation.PollDevices(inputs, false, emit);
      input.buttons = 0x20; shortcut_navigation.PollDevices(inputs, false, emit);
      check("pad-devices-shortcut-needs-both-buttons", chords == 0, "{}");
      input.buttons = 0x30; shortcut_navigation.PollDevices(inputs, false, emit);
      check("pad-devices-shortcut-opens-on-press", chords == 1, "{}");
      shortcut_navigation.PollDevices(inputs, true, emit);
      shortcut_navigation.PollDevices(inputs, true, emit);
      check("pad-devices-shortcut-held-does-not-repeat", chords == 1, "{}");
      input.buttons = 0x10; shortcut_navigation.PollDevices(inputs, true, emit);
      input.buttons = 0x30; shortcut_navigation.PollDevices(inputs, true, emit);
      check("pad-devices-shortcut-release-allows-next-press", chords == 2, "{}");
    }
    {
      broken_bond::menu::PadInput preview_navigation;
      broken_bond::controls::Inventory inputs;
      inputs.devices.emplace_back();
      auto& input = inputs.devices.front().raw;
      input.axes = {.9, -.9, .8, -.8, .7, .6};
      navigation_keys.clear();
      preview_navigation.PollDevices(inputs, true, emit, false);
      check("pad-preview-axes-do-not-navigate", navigation_keys.empty(), "{}");
      input.buttons = 1;
      preview_navigation.PollDevices(inputs, true, emit, false);
      check("pad-preview-dpad-navigates", navigation_keys == std::vector{rex::ui::VirtualKey::kUp}, "{}");
      input.buttons = 0;
      navigation_keys.clear();
      preview_navigation.PollDevices(inputs, true, emit);
      check("pad-preview-exit-held-stick-suppressed", navigation_keys.empty(), "{}");
      input.axes = {};
      preview_navigation.PollDevices(inputs, true, emit);
      input.axes[0] = .9;
      preview_navigation.PollDevices(inputs, true, emit);
      check("pad-preview-neutral-restores-stick-navigation",
            navigation_keys == std::vector{rex::ui::VirtualKey::kRight}, "{}");
    }
    // An SDL virtual gamepad: its D-pad navigates and a remapped chord opens
    // once a press.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_VirtualJoystickDesc virtual_pad;
    SDL_INIT_INTERFACE(&virtual_pad);
    virtual_pad.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    virtual_pad.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    virtual_pad.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    virtual_pad.name = "Menu navigation test";
    const auto pad_id = SDL_AttachVirtualJoystick(&virtual_pad);
    auto* joystick = pad_id ? SDL_OpenJoystick(pad_id) : nullptr;
    check("pad-virtual-attached", joystick != nullptr, "{}");
    if (joystick) {
      navigation.Initialize();
      navigation_keys.clear();
      SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
      navigation.Poll(true, emit);
      check("pad-sdl-navigation", std::find(navigation_keys.begin(), navigation_keys.end(), rex::ui::VirtualKey::kDown) !=
            navigation_keys.end(), "{}");
      int remapped = 0;
      navigation.SetShortcut("L3+R3", [&] { ++remapped; });
      SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
      SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_LEFT_STICK, true);
      SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
      navigation.Poll(false, emit);
      check("pad-remapped-shortcut-on-press", remapped == 1, "{}");
      navigation.Poll(false, emit);
      navigation.Poll(false, emit);
      check("pad-remapped-held-shortcut-once", remapped == 1, "{}");
      SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
      navigation.Poll(false, emit);
      SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
      navigation.Poll(false, emit);
      check("pad-remapped-shortcut-next-press", remapped == 2, "{}");
      navigation.Shutdown();
      SDL_CloseJoystick(joystick);
      SDL_DetachVirtualJoystick(pad_id);
    }
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
