// GPU check of the menus (menu/ui.h) over the UI layer of the native Present,
// through a dispatcher as the Swaps deliver it. Over a green image: the frame
// rate document and its hiding; the game menu bar with only Exit, Enter and
// Esc; a dialog whose default is Cancel; a panel's arrows, tabs and Back; the
// settings pages saving to broken_bond.toml and their live rows waiting for
// the renderer's result; the DLC page; the setup screen, its own engine and
// the hand-over to the game's; the menu strings in
// every language; the controller's navigation and shortcut; and the release of
// the UI's textures with the menus.
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
#include <rex/filesystem/devices/stfs_xbox.h>
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

    // The settings pages: each edit is saved at once, unknown keys survive.
    const auto settings_dir = std::filesystem::path(argv[1]).parent_path() / "settings";
    std::filesystem::create_directories(settings_dir);
    const auto settings_path = settings_dir / "broken_bond.toml";
    std::ofstream(settings_path) << "unknown_option = 42\nuser_language = 1\nfullscreen = false\n"
                                    "show_fps_overlay = false\nmnk_mode = true\nmnk_mouse = true\n";
    broken_bond::menu::Settings settings_model;
    settings_model.Initialize(settings_dir);
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
    key(rex::ui::VirtualKey::kDown); key(rex::ui::VirtualKey::kDown); key(rex::ui::VirtualKey::kRight);
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
        return e.AttachWindow(window, out, present) && e.CreateSurface(SurfaceDesc{kSurface, kWidth, kHeight}, out);
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
