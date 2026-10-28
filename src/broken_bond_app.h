// broken_bond - ReXGlue Recompiled Project
//
// Minimal host for the recompiled game using the SDK runtime.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>
#include <string_view>

#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <toml++/toml.hpp>

#include "crash_report.h"
#include "dlc_install.h"
#include "fps_overlay.h"
#include "game_image.h"
#include "graphics_device.h"
#include "launch_arguments.h"
#include "input_replay.h"
#include "mods/translation_mod.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_UINT32(restart_wait_pid, 0, "broken_bond/Internal", "Wait for the previous process during a settings restart");

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
#include "gpu/common/shader_activity.h"
#include "gpu/host/app.h"
#include "gpu/settings.h"
#include "gpu/ui/immediate_drawer.h"
#include "gpu/ui/layer.h"
#include "gpu/ui/screen.h"
#include "intro_skip.h"
#include "menu/dlc.h"
#include "menu/game_choice.h"
#include "menu/input_gate.h"
#include "menu/settings.h"
#include "menu/setup_gate.h"
#include "menu/shortcut.h"
#include "menu/text.h"
#include "menu/ui.h"
#include "menu/update_checker.h"
#include "port_version.h"
#include "wine.h"
#include <SDL3/SDL_misc.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>

REXCVAR_DEFINE_BOOL(setup, false, "broken_bond/UI",
                    "Open the setup screen before the game (also Shift held at the start)");
REXCVAR_DEFINE_BOOL(show_startup_menu, true, "broken_bond/UI",
                    "Show the initial menu before the game (Settings > Advanced)");
REXCVAR_DEFINE_BOOL(check_for_updates, true, "broken_bond/UI", "Notify when a new release is available");
#endif

// Alt+Enter and F11 switch between borderless fullscreen and windowed mode.
class FullscreenShortcuts : public rex::ui::WindowInputListener {
 public:
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    using rex::ui::VirtualKey;
    if (e.virtual_key() != VirtualKey::kF11 &&
        !(e.virtual_key() == VirtualKey::kReturn && e.is_alt_pressed())) {
      return;
    }

    // Consume repeats too, so holding the shortcut neither toggles repeatedly
    // nor sends Enter to the game's keyboard controller or an overlay.
    e.set_handled(true);
    if (!e.prev_state() && e.target()) {
      // ReXApp's fullscreen callback applies the change on this UI thread.
      rex::cvar::SetFlagByName("fullscreen",
                               e.target()->IsFullscreen() ? "false" : "true");
    }
  }
};

class BrokenBondApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BrokenBondApp>(new BrokenBondApp(ctx, "broken_bond",
        PPCImageConfig));
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    // Input recording and replay for test boots (input_replay.cpp); the SDK's
    // default input system when neither is asked for.
    config.input_factory = CreateInputSystem;
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    // The native renderer replaces the Xenos plugin when renderer = "native".
    gpu::host::Configure(config);
    // The game menu neutralizes the guest's input while it shows (input_replay.cpp).
    broken_bond::menu::input_gate_enabled = gpu::host::Enabled();
#endif
    if (!config.graphics) SelectXenosAdapter();
  }

  // The paths without the launcher, which passes --game_data_root and
  // --user_data_root: the command line first; then the game folder or disc
  // image of the .toml's custom_game_root, else the one beside the executable
  // or the AppImage (game_image.h); the saves where the launcher keeps them.
  // Logging starts after this, so OnPostInitLogging reports the choice.
  void OnConfigurePaths(rex::PathConfig& paths) override {
#ifdef _WIN32
    // A restart for saved settings (RestartForSettings): the previous process
    // closes first, so it neither holds the window's device nor writes the .toml.
    if (const auto parent = REXCVAR_GET(restart_wait_pid); parent && parent != GetCurrentProcessId()) {
      if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, parent)) {
        WaitForSingleObject(process, INFINITE);
        CloseHandle(process);
      }
    }
#endif
    const auto exe_dir = rex::filesystem::GetExecutableFolder();
    const auto utf8_path = [&](std::string_view text) {
      std::filesystem::path path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
      return path.is_relative() ? exe_dir / path : path;
    };
    std::optional<toml::table> config;
    try {
      std::error_code ec;
      if (std::filesystem::is_regular_file(paths.config_path, ec)) {
        std::ifstream input(paths.config_path, std::ios::binary);
        config = toml::parse(input);
      }
    } catch (...) {
    }
    if (REXCVAR_GET(user_data_root).empty()) {
      const auto configured = config ? (*config)["user_data_root"].value_or(std::string()) : std::string();
      if (!configured.empty()) {
        paths.user_data_root = utf8_path(configured);
        user_root_source_ = "user_data_root";
      } else {
        const auto repository = broken_bond::game_image::RepositoryOf(exe_dir);
        paths.user_data_root = repository.empty() ? exe_dir / "user" : repository / "build" / "user";
        user_root_source_ = "default";
      }
      if (REXCVAR_GET(cache_root).empty()) paths.cache_root = paths.user_data_root / "cache";
    }
    command_line_root_ = !paths.game_data_root.empty();
    if (command_line_root_) return;
    const auto custom = config ? (*config)["custom_game_root"].value_or(std::string()) : std::string();
    configured_root_ = !custom.empty();
    if (configured_root_) {
      std::error_code ec;
      const auto root = utf8_path(custom);
      if (std::filesystem::is_directory(root, ec) || std::filesystem::is_regular_file(root, ec)) {
        paths.game_data_root = std::filesystem::canonical(root, ec);
        game_root_source_ = "custom_game_root";
      }
    }
    if (paths.game_data_root.empty()) {
      paths.game_data_root = broken_bond::game_image::FindNearbyGame(exe_dir, broken_bond::game_image::AppImageFolder());
      game_root_source_ = paths.game_data_root.empty() ? "none" : "nearby";
    }
  }

  // Which game the data root holds, once the log exists: the recompiled code
  // only runs with the default.xex it was generated from (game_image.h).
  void OnPostInitLogging() override {
    REXLOG_INFO("[broken_bond] Paths: game data {} ({}), user data {} ({})", game_data_root().string(),
                game_root_source_, user_data_root().string(), user_root_source_);
    if (!game_data_root().empty()) LogGameData(game_data_root());
  }

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
  // The initial menu follows the saved startup preference and the recovery
  // rules (menu/setup_gate.h); it shows on the window before the runtime
  // exists, and its Play resumes the boot. The native mode only: with
  // Xenos the game boots as configured.
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    if (!gpu::host::Enabled()) return defaults;
    SetupUi();
    // The launch marker: there from here until the game's first frame, so a
    // start that dies before it (the engine of a card or API that fails) opens
    // the setup screen next time. The boots that name their game have none.
    if (!command_line_root_ && !user_data_root().empty()) {
      launch_marker_ = user_data_root() / "launch.marker";
      std::error_code ec;
      start_failed_ = std::filesystem::exists(launch_marker_, ec);
      std::filesystem::create_directories(user_data_root(), ec);
      std::ofstream(launch_marker_) << "started\n";
    }
    namespace menu = broken_bond::menu;
    menu::SetupFacts facts;
    facts.command_line_root = command_line_root_;
    facts.configured = configured_root_;
    facts.found = !defaults.game_data_root.empty();
    facts.status = game_identity_.status;
    facts.start_failed = start_failed_;
    facts.requested = REXCVAR_GET(setup) || menu::SetupKeyHeld();
    facts.show_startup_menu = REXCVAR_GET(show_startup_menu);
    const menu::SetupReason reason = menu::SetupReasonFor(facts);
    if (reason == menu::SetupReason::kNone || !OpenSetupScreen(reason, defaults, std::move(resume))) {
      ArmFirstPresentation();
      return defaults;
    }
    return std::nullopt;
  }

  void OnPostLoadXexImage() override {
    // The camera ratio of the aspect_ratio option, before the title runs.
    if (gpu::host::Enabled() && runtime()) gpu::host::ApplyAspectRatio(runtime()->virtual_membase());
  }
#endif

  void OnPostSetup() override {
    broken_bond::translation::Initialize(rex::filesystem::GetExecutableFolder());
    broken_bond::crash_report::Install();  // After the SDK's exception handler.
    if (window()) {
      // Window shortcuts take priority over overlays and keyboard emulation.
      window()->AddInputListener(&fullscreen_shortcuts_,
                                 std::numeric_limits<size_t>::max());
    }
    RestoreXenosAdapter();
    // Before the title enumerates its marketplace content.
    broken_bond::dlc::InstallAtStart(
        runtime() && runtime()->kernel_state() ? runtime()->kernel_state()->content_manager() : nullptr,
        user_data_root());
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    // The menus came up before the runtime (OnFinalizePaths); the game menu's
    // shortcuts belong to the running game.
    if (gpu::host::Enabled()) {
      game_running_ = true;
      UpdateMenuShortcut();
      REXLOG_INFO("[broken_bond] Game menu: {} or {}", menu_settings_.menu_options().key,
                  menu_settings_.menu_options().chord);
    }
#endif
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    SetGuestFrameStats(ReadGuestFrameStats);
    if (rex::cvar::Query<bool>("show_fps_overlay")) {
      fps_overlay_ = std::make_unique<FpsOverlayDialog>(drawer, ReadGuestFrameStats);
    }
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    if (gpu::host::Enabled()) return;  // F1 opens the game menu there.
#endif
    rex::ui::RegisterBind("bind_fps_overlay", "F1", "Toggle FPS overlay", [this] {
      if (fps_overlay_) {
        fps_overlay_.reset();
      } else if (imgui_drawer()) {
        fps_overlay_ = std::make_unique<FpsOverlayDialog>(imgui_drawer(), ReadGuestFrameStats);
      }
      rex::cvar::SetFlagByName("show_fps_overlay", fps_overlay_ ? "true" : "false");
    });
  }

  void OnShutdown() override {
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    game_running_ = false;
    broken_bond::menu::input_blocked = false;
    CloseSetupScreen();
    if (window()) window()->RemoveInputListener(&ui_);
    gpu::ui::UiLayer().SetFrameRequest({});  // No Swap asks for a UI frame from here on.
    gpu::ui::UiLayer().SetFirstPresentCallback({});
    ui_.Shutdown();
#endif
    if (window()) window()->RemoveInputListener(&fullscreen_shortcuts_);
    rex::ui::UnregisterBind("bind_fps_overlay");
    fps_overlay_.reset();
    RestoreXenosAdapter();
  }

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
  bool OnWindowCloseRequested() override {
    if (!gpu::host::Enabled() || close_ready_) return true;
    // The ordered close runs after this event: SDL holds its event lock while
    // the event runs, and the Swap thread posts the menu's frames through SDL,
    // so joining the guest threads from here could wait for it forever.
    if (!closing_.exchange(true)) {
      gpu::ui::UiLayer().SetFrameRequest({});  // No Swap draws the menus from here on.
      gpu::ui::UiLayer().SetFirstPresentCallback({});
      broken_bond::menu::input_blocked = true;
      broken_bond::controls::Hardware().StopAllRumble();  // No pad keeps rumbling while the game closes.
      app_context().CallInUIThreadDeferred([this] {
        menu_dlc_.Wait();  // Finish the DLC page's copies before the process can exit.
        // Closed on the setup screen: the start ends cleanly there.
        if (setup_resume_) {
          setup_resume_ = nullptr;
          CloseSetupScreen();
          RemoveLaunchMarker();
        }
        // The native renderer closes in order (guest threads joined, device
        // owner, resources and engine released) before OnClosing hard-exits.
        close_ready_ = gpu::host::CloseRequested();
        if (close_ready_ && window()) window()->RequestClose();
      });
    }
    return false;
  }
#endif

 private:
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
  // The menus (menu/ui.h) through the UI layer of the native Present, from
  // before the runtime on (the setup screen): each Swap, or each Present of the
  // setup screen, asks the UI thread for the next frame while a document is
  // shown, and polls the menu shortcuts otherwise. F1 or Back+Start opens the
  // game menu once the game runs.
  void SetupUi() {
    menu_settings_.Initialize(rex::filesystem::GetExecutableFolder());
    native_drawer_ = std::make_unique<gpu::ui::ImmediateDrawer>(gpu::ui::UiLayer());
    gpu::ui::UiLayer().SetFrameRequest([this] {  // The Swap's thread.
      if (closing_.load()) return;
      ui_.CountFrame();
      if ((!ui_.Shown() && !ui_.GameMenuEnabled()) || ui_draw_pending_.exchange(true)) return;
      app_context().CallInUIThread([this] {
        ui_draw_pending_ = false;
        if (closing_.load()) return;
        const bool visible = ui_.Shown();
        if (!visible) ui_.PollInput();
        if (visible || ui_.Shown()) DrawNativeUi();
      });
    });
    if (!ui_.Initialize(*native_drawer_, nullptr)) {
      REXLOG_WARN("[broken_bond] UI unavailable: the menus did not initialize");
      return;
    }
    // Below the window shortcuts (fullscreen), above the SDK's overlays.
    if (window()) window()->AddInputListener(&ui_, std::numeric_limits<size_t>::max() - 1);
    ui_.SetInputActive([this] { return window() && window()->HasFocus(); });
    ui_.SetTick([this] { ServiceMenu(); });
    menu_settings_.SetUtilities([this] { CreateDesktopShortcut(); }, [this] { CheckUpdates(); });
    if (REXCVAR_GET(check_for_updates))
      updates_ = std::make_unique<launcher_updates::Checker>(broken_bond::PortVersion(), user_data_root() / "menu-updates.json");
    ui_.SetFpsVisible(rex::cvar::Query<bool>("show_fps_overlay"));
  }
  // The menu key and controller chord of the settings, and the hint that names them.
  void UpdateMenuShortcut() {
    const auto& options = menu_settings_.menu_options();
    ui_.EnableGameMenu(options.key, options.chord, [this] { ToggleGameMenu(); });
    std::string chord;
    for (char c : options.chord) chord += c == '+' ? " + " : std::string(1, c);
    char hint[512];
    std::snprintf(hint, sizeof(hint), broken_bond::menu::Text("shortcut-format"), options.key.c_str(), chord.c_str());
    shortcut_hint_ = hint;
  }
  // On the UI thread with each menu frame: the game chosen on the setup
  // screen, the DLC page's copies, the live settings' results, the shortcut
  // hint and the shader indicator.
  void ServiceMenu() {
    PollGameChoice();
    menu_dlc_.Poll();
    menu_settings_.PollLive();
    if (shortcut_hint_pending_ && ui_.OverlayShown()) shortcut_hint_pending_ = false;
    if (shortcut_hint_pending_ && std::chrono::steady_clock::now() >= shortcut_hint_due_) {
      shortcut_hint_pending_ = false;
      ui_.ShowShortcutHint(shortcut_hint_);
    }
    const auto progress = gpu::CurrentShaderProgress();
    ui_.SetShadersVisible(menu_settings_.menu_options().shaders && gpu::ShaderCompilationVisible(), progress.pending,
                          progress.fraction);
    if (!updates_) return;
    using broken_bond::menu::Text;
    updates_->Poll();
    if (updates_->status() == launcher_updates::Status::Available && !updates_->IsIgnored() && !update_notified_) {
      update_notified_ = true;
      const auto release = updates_->release();
      char message[512];
      std::snprintf(message, sizeof(message), Text("update-detail"), release.version.c_str());
      ui_.Notify(message, Text("open-release"), [url = release.page_url] { SDL_OpenURL(url.c_str()); },
                 Text("update-available"));
    }
    if (manual_update_check_ && updates_->status() != launcher_updates::Status::Checking) {
      manual_update_check_ = false;
      if (updates_->status() != launcher_updates::Status::Available)
        ui_.Notify(Text(updates_->status() == launcher_updates::Status::Current ? "updates-current" : "updates-failed"));
    }
  }
  // The settings pages (menu/settings.h). The graphics rows without RESTART go
  // to the renderer, which applies them between frames, and keep their old
  // value until it confirms; the intro videos, fullscreen, the FPS overlay, the
  // menu language and the menu shortcuts apply at once; the options marked
  // RESTART apply at the next start, which a notice recalls when the pages
  // close. Opened from the setup screen, everything applies when the game starts.
  void OpenSettings() {
    menu_settings_.SetLiveSettings([](const std::string& key, const GameConfig& config) -> uint64_t {
      auto next = gpu::RequestedSettings();
      if (key == "vsync") next.vsync = config.vsync;
      else if (key == "brightness") next.brightness = float(config.brightness);
      else if (key == "frame_rate") next.frame_rate = uint32_t(config.frame_rate);
      else if (key == "resolution_scale") {
        // The renderer widens one axis at the session's aspect ratio.
        const float scale = float(config.internal_resolution_scale > 0 ? config.internal_resolution_scale
                                                                         : config.resolution_scale);
        next.resolution_scale = {scale, scale};
      } else if (key == "msaa_samples") next.msaa_samples = uint32_t(config.msaa_samples);
      else if (key == "anisotropic_override") next.anisotropic_override = config.anisotropic_override;
      else if (key == "transparency_antialiasing") next.transparency_antialiasing = config.transparency_antialiasing;
      else if (key == "max_frame_latency") next.max_frame_latency = uint32_t(config.max_frame_latency);
      else if (key == "triple_buffering") next.triple_buffering = config.triple_buffering;
      else if (key == "swap_post_effect") next.swap_post_effect = config.swap_post_effect == "fxaa_extreme"
          ? gpu::SwapPostEffect::kFxaaExtreme : config.swap_post_effect == "fxaa" ? gpu::SwapPostEffect::kFxaa
          : gpu::SwapPostEffect::kNone;
      else if (key == "present_effect") next.present_effect = config.present_effect == "fsr"
          ? gpu::PresentEffect::kFsr : config.present_effect == "cas" ? gpu::PresentEffect::kCas
          : gpu::PresentEffect::kBilinear;
      else return 0;
      return gpu::RequestSettings(std::move(next));
    }, [] {
      const auto result = gpu::LastSettingsResult();
      return broken_bond::menu::Settings::LiveResult{result.revision, result.applied};
    });
    menu_settings_.Open(ui_, 0, !setup_resume_, [this] {
      app_context().CallInUIThreadDeferred([this] { ChooseGame(); });
    }, [this](const std::string& key) {
      const auto& config = menu_settings_.config();
      if (key == "show_fps_overlay") {
        rex::cvar::SetFlagByName(key, config.show_fps_overlay ? "true" : "false");
        ui_.SetFpsVisible(config.show_fps_overlay);
      } else if (key == "fullscreen") {
        rex::cvar::SetFlagByName(key, config.fullscreen ? "true" : "false");
      } else if (key == "skip_intro_videos") {
        broken_bond::SetSkipIntroVideos(config.skip_intro_videos);
      } else if (key == "native_backend" || key == "graphics_device") {
        app_context().CallInUIThreadDeferred([this] { OfferSettingsRestart(); });
      } else if (key == "bind_game_menu" || key == "game_menu_chord") {
        UpdateMenuShortcut();
      } else if (!setup_resume_ && key != "launcher_language" && key != "show_shader_indicator" &&
                 !broken_bond::menu::Settings::Live(key)) {
        settings_restart_ = true;
      }
    }, [this] {
      if (settings_restart_) ui_.Notify(broken_bond::menu::Text("changed"));
      settings_restart_ = false;
      if (setup_resume_) RefreshSetup();
      else if (ui_.OverlayShown()) OpenGameMenu();
    });
  }
  // The DLC page (menu/dlc.h): packages are staged into the folder the game
  // installs from at the next start; extracted folders are copied at once.
  void OpenDlc() {
    auto staging = broken_bond::dlc::SourceFolder();
    if (staging.empty()) staging = rex::filesystem::GetExecutableFolder() / "dlc";
    menu_dlc_.Open(ui_, std::move(staging), user_data_root(), window() ? window()->GetNativeWindowHandle() : nullptr,
        [this](std::function<void()> action) { app_context().CallInUIThreadDeferred(std::move(action)); }, [this] {
          if (setup_resume_) RefreshSetup();
          else if (ui_.OverlayShown()) OpenGameMenu();
        });
  }
  // The setup screen (menu/setup_screen.h) for reason, on the window, which a
  // temporary engine presents. Play resumes the boot with paths; false when it
  // cannot show.
  bool OpenSetupScreen(broken_bond::menu::SetupReason reason, const rex::PathConfig& paths,
                       std::function<void(rex::PathConfig)> resume) {
    REXLOG_INFO("[broken_bond] Setup screen: {}", broken_bond::menu::SetupReasonName(reason));
    if (!OpenNativeScreen()) return false;
    // The choices run inside RmlUi's event handling; the screen closes after it
    // (deferred: CallInUIThread runs at once on the UI thread).
    const auto later = [this](void (BrokenBondApp::*step)()) {
      return [this, step] { app_context().CallInUIThreadDeferred([this, step] { (this->*step)(); }); };
    };
    broken_bond::menu::SetupScreen::State state{reason, game_identity_, broken_bond::PortVersion(),
                                                paths.game_data_root};
    broken_bond::menu::SetupScreen::Actions actions;
    actions.play = later(&BrokenBondApp::FinishSetup);
    actions.quit = later(&BrokenBondApp::QuitFromSetup);
    actions.choose_game = later(&BrokenBondApp::ChooseGame);
    actions.settings = later(&BrokenBondApp::OpenSettings);
    actions.dlc = later(&BrokenBondApp::OpenDlc);
    if (!ui_.ShowSetup(state, std::move(actions))) {
      REXLOG_WARN("[broken_bond] Setup screen unavailable; the game boots as configured");
      CloseSetupScreen();
      return false;
    }
    setup_paths_ = paths;
    setup_reason_ = reason;
    setup_resume_ = std::move(resume);
    return true;
  }
  void RefreshSetup() {
    ui_.RefreshSetup({setup_reason_, game_identity_, broken_bond::PortVersion(), setup_paths_.game_data_root});
  }
  // The temporary engine of the setup screen: the configured API and card, or
  // the other API when they fail (the screen is where they change). Under Wine
  // the native renderer runs on Vulkan only.
  bool OpenNativeScreen() {
    const auto& settings = gpu::CurrentSettings();
    gpu::RenderMode mode;
    mode.device = settings.graphics_device;
    mode.api = settings.native_backend == "vulkan" ? gpu::GraphicsApi::kVulkan : gpu::GraphicsApi::kD3D12;
    void* hwnd = window() ? window()->GetNativeWindowHandle() : nullptr;
    std::string evidence;
    if (hwnd && setup_screen_.Open(hwnd, mode, evidence)) return true;
    REXLOG_WARN("[broken_bond] Setup screen: the {} engine did not start: {}", settings.native_backend, evidence);
    if (!hwnd || (mode.api == gpu::GraphicsApi::kVulkan && broken_bond::WineVersion())) return false;
    mode.api = mode.api == gpu::GraphicsApi::kVulkan ? gpu::GraphicsApi::kD3D12 : gpu::GraphicsApi::kVulkan;
    mode.device.clear();
    if (setup_screen_.Open(hwnd, mode, evidence)) return true;
    REXLOG_WARN("[broken_bond] Setup screen: the other engine did not start either: {}", evidence);
    return false;
  }
  // The screen's engine goes before the game's; the UI layer keeps the frame
  // of what still shows (the frame rate) for it.
  void CloseSetupScreen() {
    ui_.HideSetup();
    if (!setup_screen_.IsOpen()) return;
    if (native_drawer_) DrawNativeUi();
    setup_screen_.Close();
  }
  void FinishSetup() {
    if (!setup_resume_ || game_choice_.Busy() || game_identity_.status != broken_bond::game_image::Status::kReady) return;
    if (!menu_settings_.ApplyBeforeGame()) {
      ui_.ShowDialog(broken_bond::menu::Text("settings"), broken_bond::menu::Text("save-failed"),
                     {{broken_bond::menu::Text("cancel"), {}}});
      return;
    }
    auto resume = std::exchange(setup_resume_, nullptr);
    CloseSetupScreen();
    gpu::LoadSettings();  // The edits of the setup screen's settings.
    if (setup_paths_.game_data_root != game_data_root()) LogGameData(setup_paths_.game_data_root);
    ArmFirstPresentation();
    resume(setup_paths_);
  }
  // The system picker for a disc image or default.xex; the choice is identified
  // on a worker (PollGameChoice). From the setup screen or the game's settings.
  void ChooseGame() {
    if ((!setup_resume_ && !ui_.OverlayShown()) || game_choice_.Busy()) return;
    const auto selected = broken_bond::menu::ChooseFile(window() ? window()->GetNativeWindowHandle() : nullptr);
    if (!selected.error.empty()) {
      ui_.ShowDialog(broken_bond::menu::Text("picker-failed"), selected.error, {{broken_bond::menu::Text("cancel"), {}}});
    } else if (!selected.path.empty()) {
      if (!setup_resume_ && !ui_.OverlayShown()) return;
      game_choice_.Start(selected.path);
      game_choice_view_ = ui_.ViewRevision();
      checking_game_ = false;
    }
  }
  void PollGameChoice() {
    if (!game_choice_.Busy()) return;
    using broken_bond::menu::Text;
    if (auto result = game_choice_.Poll()) {
      if (game_choice_view_ != ui_.ViewRevision()) { checking_game_ = false; return; }
      ui_.HideDialog();
      checking_game_ = false;
      using broken_bond::game_image::Status;
      if (result->game.status == Status::kReady) {
        std::string error;
        if (!broken_bond::menu::SaveGameRoot(rex::filesystem::GetExecutableFolder() / "broken_bond.toml", result->root,
                                             error)) {
          ui_.ShowDialog(Text("save-failed"), error, {{Text("cancel"), {}}});
          return;
        }
        if (!setup_resume_) {
          // In game: the new game plays at the next start.
          menu_settings_.Reload();
          OpenSettings();
          OfferSettingsRestart();
          return;
        }
        setup_paths_.game_data_root = result->root;
        game_identity_ = result->game;
        configured_root_ = true;
        setup_reason_ = broken_bond::menu::SetupReason::kRequested;
        ui_.HidePanel();
        // Reuse the current callbacks and refresh the ready state without
        // replacing the screen's engine or its resume continuation.
        RefreshSetup();
        REXLOG_INFO("[broken_bond] Game selected: {}", result->root.string());
      } else {
        const char* title = Text(result->game.status == Status::kOtherVersion ? "unsupported" :
                            result->game.status == Status::kIncomplete ? "incomplete" :
                            result->game.status == Status::kUnreadable ? "unreadable" : "unrecognized");
        ui_.ShowDialog(title, Text(result->game.status == Status::kOtherVersion ? "unsupported-detail" :
                                  result->game.status == Status::kIncomplete ? "incomplete-detail" : "game-hint"),
            {{Text("choose-another"), [this] { app_context().CallInUIThreadDeferred([this] { ChooseGame(); }); }},
             {Text("cancel"), {}}});
      }
    } else if (!checking_game_ && game_choice_.Slow() && game_choice_view_ == ui_.ViewRevision()) {
      checking_game_ = true;
      ui_.ShowDialog(Text("checking"), Text("wait"));
    }
  }
  // The API and the card belong to the session: a restart starts a new
  // process on the setup screen with the saved choice.
  void OfferSettingsRestart() {
    if (closing_.load()) return;
    using broken_bond::menu::Text;
    std::string message = Text("changed");
    if (game_running_) message += std::string("\n\n") + Text("exit-confirm");
    ui_.ShowDialog(Text("restart-required"), message, {
        {Text("restart-now"), [this] {
          app_context().CallInUIThreadDeferred([this] { RestartForSettings(); });
        }},
        {Text("later"), {}}});
  }
  // A new process with the saved API and card, on the setup screen; this one
  // closes in its normal order first.
  void RestartForSettings() {
    const auto folder = rex::filesystem::GetExecutableFolder();
    if (menu_settings_.config().custom_game_root.empty() && !game_data_root().empty()) {
      // The game in use (the command line's or the one found nearby) for the next start.
      std::string error;
      if (!broken_bond::menu::SaveGameRoot(folder / "broken_bond.toml", game_data_root(), error)) return;
    }
    bool started = false;
#ifdef _WIN32
    // The new process waits for this one (restart_wait_pid).
    const auto executable = folder / L"broken_bond.exe";
    const std::wstring command = broken_bond::launch::QuoteArgument(executable.wstring()) + L" --setup " +
        broken_bond::launch::UserDataArgument(user_data_root()) + L" --restart_wait_pid=" +
        std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    auto writable = command;
    if (CreateProcessW(executable.c_str(), writable.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       folder.c_str(), &startup, &process)) {
      REXLOG_INFO("[broken_bond] Restarting for saved settings: process {}", process.dwProcessId);
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
      started = true;
    }
#else
    // The AppImage the game was started from, so its AppRun prepares the
    // next start, else this executable. The new process starts once this one
    // has exited (StartAfterExit), with the inherited working folder.
    const char* image = std::getenv("APPIMAGE");
    const auto executable = image && *image ? std::filesystem::absolute(rex::to_path(image))
                                            : rex::filesystem::GetExecutablePath();
    const auto root = std::filesystem::absolute(user_data_root()).lexically_normal();
    restart_wait_fd_ = broken_bond::launch::StartAfterExit(
        executable, {"--setup", "--user_data_root=" + rex::path_to_utf8(root)});
    if (restart_wait_fd_ >= 0) {
      REXLOG_INFO("[broken_bond] Restarting for saved settings: {} after this process exits",
                  rex::path_to_utf8(executable));
      started = true;
    }
#endif
    if (started) {
      RemoveLaunchMarker();
      if (window()) window()->RequestClose();
    } else {
      ui_.ShowDialog(broken_bond::menu::Text("restart"), broken_bond::menu::Text("save-failed"),
                     {{broken_bond::menu::Text("cancel"), {}}});
    }
  }
  void QuitFromSetup() {
    if (!setup_resume_) return;
    setup_resume_ = nullptr;
    CloseSetupScreen();
    RemoveLaunchMarker();
    if (window()) window()->RequestClose();
  }
  void RemoveLaunchMarker() {
    std::error_code ec;
    if (!launch_marker_.empty()) std::filesystem::remove(launch_marker_, ec);
  }
  // The game's first frame on the screen: the start succeeded (the launch
  // marker goes), and the menu's shortcut hint shows a few seconds later.
  void ArmFirstPresentation() {
    gpu::ui::UiLayer().SetFirstPresentCallback([this] {  // Engine owner, after a game Present.
      if (closing_.load()) return;
      app_context().CallInUIThread([this] {
        if (closing_.load()) return;
        RemoveLaunchMarker();
        if (game_running_) {
          shortcut_hint_pending_ = true;
          shortcut_hint_due_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        }
      });
    });
  }
  void CreateDesktopShortcut() {
    const bool ok = broken_bond::menu::WriteShortcut(broken_bond::menu::DesktopShortcut(),
        rex::filesystem::GetExecutableFolder() / "broken_bond.exe", user_data_root());
    ui_.Notify(broken_bond::menu::Text(ok ? "shortcut-done" : "save-failed"));
  }
  void CheckUpdates() {
    using broken_bond::menu::Text;
    if (!updates_)
      updates_ = std::make_unique<launcher_updates::Checker>(broken_bond::PortVersion(), user_data_root() / "menu-updates.json");
    if (updates_->status() == launcher_updates::Status::Available) {
      const auto release = updates_->release();
      char title[512];
      std::snprintf(title, sizeof(title), Text("updates-available"), release.version.c_str());
      ui_.ShowDialog(title, Text("update-hint"), {
          {Text("open-release"), [url = release.page_url] { SDL_OpenURL(url.c_str()); }}, {Text("cancel"), {}}});
    } else {
      if (updates_->CanCheck()) updates_->CheckNow();
      manual_update_check_ = true;
      ui_.Notify(Text("updates-checking"));
    }
  }
  void OpenGameMenu() {
    using broken_bond::menu::Text;
    ui_.ShowOverlay([this] { OpenSettings(); }, [this] { OpenDlc(); }, [this] {
      ui_.ShowDialog(std::string(Text("exit-game")) + "?", Text("exit-confirm"), {
          {Text("exit-game"), [this] { app_context().CallInUIThreadDeferred([this] {
             if (window()) window()->RequestClose();
           }); }},
          {Text("cancel"), {}}});
    }, [this] { CloseGameMenu(); });
    broken_bond::menu::input_blocked = ui_.OverlayShown();
    REXLOG_INFO("[broken_bond] Game menu opened; guest input neutral");
  }
  void CloseGameMenu() {
    ui_.HideOverlay();
    broken_bond::menu::input_blocked = false;
    DrawNativeUi();  // Replace the persistent GPU frame when nothing remains.
    REXLOG_INFO("[broken_bond] Game menu closed; guest input resumes after release");
  }
  void ToggleGameMenu() {
    if (ui_.OverlayShown()) CloseGameMenu();
    else OpenGameMenu();
  }
  // A frame of the UI over the native renderer's whole back buffer (the window's client area).
  void DrawNativeUi() {
    if (!window()) return;
    rex::ui::AppUIDrawContext context(window()->GetActualPhysicalWidth(), window()->GetActualPhysicalHeight());
    ui_.Draw(context);
  }

  std::unique_ptr<rex::ui::ImmediateDrawer> native_drawer_;  // Before ui_: it outlives the Ui.
  broken_bond::menu::Ui ui_;
  broken_bond::menu::Settings menu_settings_;
  broken_bond::menu::Dlc menu_dlc_;  // After ui_, so it is destroyed first.
  std::unique_ptr<launcher_updates::Checker> updates_;
  bool update_notified_ = false, manual_update_check_ = false, settings_restart_ = false;
  std::string shortcut_hint_;
  bool shortcut_hint_pending_ = false;
  std::chrono::steady_clock::time_point shortcut_hint_due_{};
  std::atomic<bool> ui_draw_pending_{false};
  std::atomic<bool> closing_{false};
  bool close_ready_ = false;  // The ordered close finished: the window may close.
  bool game_running_ = false;
  // The setup screen (menu/setup_gate.h) and its temporary engine.
  gpu::ui::Screen setup_screen_{gpu::ui::UiLayer()};
  bool start_failed_ = false;
  std::filesystem::path launch_marker_;
  rex::PathConfig setup_paths_;
  broken_bond::menu::SetupReason setup_reason_ = broken_bond::menu::SetupReason::kNone;
  std::function<void(rex::PathConfig)> setup_resume_;  // While the setup screen shows.
  broken_bond::menu::GameChoice game_choice_;
  uint64_t game_choice_view_ = 0;
  bool checking_game_ = false;
#ifndef _WIN32
  int restart_wait_fd_ = -1;  // Held open until this process exits (RestartForSettings).
#endif
#endif
  void LogGameData(const std::filesystem::path& root) {
    using broken_bond::game_image::Status;
    game_identity_ = broken_bond::game_image::Identify(root);
    const auto line = fmt::format("[broken_bond] Game data: {} ({}); Title ID {:08X}, default.xex {}: {}",
                                  root.string(), game_identity_.disc_image ? "disc image" : "folder",
                                  game_identity_.title_id, game_identity_.xex_digest,
                                  broken_bond::game_image::StatusName(game_identity_.status));
    if (game_identity_.status == Status::kReady)
      REXLOG_INFO("{}", line);
    else
      REXLOG_WARN("{}", line);
  }

  // The game data and where its paths came from (OnConfigurePaths).
  broken_bond::game_image::Identity game_identity_;
  bool command_line_root_ = false, configured_root_ = false;
  const char* game_root_source_ = "command line";
  const char* user_root_source_ = "command line";
  FullscreenShortcuts fullscreen_shortcuts_;
  std::unique_ptr<FpsOverlayDialog> fps_overlay_;
};
