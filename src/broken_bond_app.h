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
#include <rex/system.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <toml++/toml.hpp>

#include "crash_report.h"
#include "data_migration.h"
#include "diagnostics/incident.h"
#include "dlc_install.h"
#include "fps_overlay.h"
#include "game_image.h"
#include "gpu/common/platform.h"
#include "graphics_device.h"
#include "launch_arguments.h"
#include "input_replay.h"
#include "mods/translation_mod.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

REXCVAR_DEFINE_UINT32(restart_wait_pid, 0, "broken_bond/Internal", "Wait for the previous process during a settings restart");
REXCVAR_DEFINE_STRING(data_root, "", "broken_bond/Storage",
                      "Data folder for this session (--data_root or REX_DATA_ROOT; nothing is imported into it)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
#include "gpu/common/shader_activity.h"
#include "gpu/host/app.h"
#include "gpu/settings.h"
#include "gpu/shaders/pipelines.h"
#include "gpu/ui/immediate_drawer.h"
#include "gpu/ui/layer.h"
#include "gpu/ui/screen.h"
#include "intro_skip.h"
#include "menu/folders.h"
#include "menu/game_choice.h"
#include "menu/input_gate.h"
#include "menu/settings.h"
#include "menu/setup_gate.h"
#include "menu/shortcut.h"
#include "menu/text.h"
#include "menu/ui.h"
#include "menu/update_checker.h"
#include "port_version.h"
#include "sentry_reporting.h"
#include "wine.h"
#include <SDL3/SDL_misc.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>

REXCVAR_DEFINE_BOOL(setup, false, "broken_bond/UI",
                    "Open the setup screen before the game (also Shift held at the start)");
REXCVAR_DEFINE_BOOL(show_startup_menu, true, "broken_bond/UI",
                    "Show the initial menu before the game (Settings > Advanced)");
REXCVAR_DEFINE_BOOL(check_for_updates, true, "broken_bond/UI", "Notify when a new release is available");
REXCVAR_DEFINE_BOOL(crash_reporting, false, "broken_bond/Reporting",
                    "Allow crash reports and startup timing to be sent to Sentry");
REXCVAR_DEFINE_BOOL(crash_reporting_asked, false, "broken_bond/Reporting",
                    "The crash report question at the first start was answered");
REXCVAR_DEFINE_STRING(sentry_test, "", "broken_bond/Internal",
                      "Developer verification: error or crash; requires crash_reporting=true")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(incident_test, "", "broken_bond/Internal",
                      "Disposable-process local capture verification; no game data required")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
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

  // The data folder, before the SDK reads its .toml (data_migration.h): the
  // command line's or environment's, the saved selection, else the default
  // (data_paths.h); earlier locations copied into it; its lock held for the
  // session. A failed preparation stops the start with its error instead of
  // starting on a partial folder.
  bool SetupEnvironment() override {
    broken_bond::incident::Event("data-and-configuration", "begin");
#ifdef _WIN32
    // A restart for saved settings (RestartForSettings): the previous process
    // closes first, so it neither holds the data folder's lock nor writes the .toml.
    if (const auto parent = REXCVAR_GET(restart_wait_pid); parent && parent != GetCurrentProcessId()) {
      if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, parent)) {
        WaitForSingleObject(process, INFINITE);
        CloseHandle(process);
      }
    }
#endif
    using namespace broken_bond::data;
    try {
      const auto exe_dir = rex::filesystem::GetExecutableFolder();
      auto environment = CaptureEnvironment(exe_dir);
      if (!environment) return FailDataBootstrap(environment.error);
      environment.value->repository_dir = broken_bond::game_image::RepositoryOf(exe_dir);
      Overrides cli;
      for (auto [name, value] : {std::pair{"data_root", &cli.data_root},
                                 std::pair{"user_data_root", &cli.user_data_root},
                                 std::pair{"cache_root", &cli.cache_root}}) {
        if (rex::cvar::GetFlagSource(name) == rex::cvar::Source::kCommandLine)
          *value = rex::cvar::GetFlagByName(name);
      }
      auto paths = ResolvePaths(*environment.value, cli);
      if (!paths) return FailDataBootstrap(paths.error);
      auto prepared = PrepareData(*paths.value, *environment.value, cli);
      if (!prepared) return FailDataBootstrap(prepared.error);
      auto writable = ValidateWritablePaths(prepared.value->paths);
      if (!writable) return FailDataBootstrap(writable.error);
      prepared_data_ = std::move(*prepared.value);
      // For a location chosen in Advanced (RequestDataLocation).
      data_environment_ = std::move(*environment.value);
      data_cli_ = std::move(cli);
    } catch (const std::exception& e) {
      return FailDataBootstrap(e.what());
    }
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    // The SDK's environment starts the log, and with it the crash reports
    // (OnPostInitLogging); --sentry_test checks them and ends the process.
    if (!rex::ReXApp::SetupEnvironment()) {
      broken_bond::reporting::Close();
      return false;
    }
    if (const auto test = REXCVAR_GET(incident_test); !test.empty()) {
      // Each mode ends the process, except guest-av, which lets the game run.
      if (!broken_bond::incident::VerifyIncident(test) || test != "guest-av") {
        broken_bond::reporting::Close();
        std::_Exit(2);
      }
    }
    if (const auto test = REXCVAR_GET(sentry_test); !test.empty()) {
      const bool verified = broken_bond::reporting::Verify(test);
      broken_bond::reporting::Close();
      if (verified) broken_bond::incident::ExpectedExit("sentry-verification");
      rex::FlushLogging();
      std::_Exit(verified ? 0 : 2);
    }
    return true;
#else
    return rex::ReXApp::SetupEnvironment();
#endif
  }

  // The prepared data folder's .toml, logs, saves and cache. The game: the
  // command line's first; then the game folder or disc image of the .toml's
  // custom_game_root, else the one beside the executable or the AppImage
  // (game_image.h). Logging starts after this, so OnPostInitLogging reports
  // the choice.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto& active = prepared_data_->paths;
    paths.user_data_root = active.user_data_root;
    paths.cache_root = active.cache_root;
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    // The player's pipeline list, which grows with each session.
    if (!active.cache_root.empty()) gpu::SetPlayerPipelineFolder(active.cache_root / "pipelines");
#endif
    paths.config_path = active.config_path;
    paths.log_dir = active.log_dir;
    user_root_source_ = SourceName(active.user_source);
    command_line_root_ = !paths.game_data_root.empty();
    if (command_line_root_) return;
    // Relative paths of the .toml keep the folder it was written for.
    const auto utf8_path = [&](std::string_view text) {
      std::filesystem::path path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
      return path.is_relative() ? prepared_data_->config_base / path : path;
    };
    std::optional<toml::table> config;
    try {
      std::ifstream input(active.config_path, std::ios::binary);
      config = toml::parse(input);
    } catch (...) {
    }
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
      paths.game_data_root = broken_bond::game_image::FindNearbyGame(active.install_dir,
                                                                     broken_bond::game_image::AppImageFolder());
      game_root_source_ = paths.game_data_root.empty() ? "none" : "nearby";
    }
  }

  // The data folder and what its preparation reported, then which game the
  // data root holds, once the log exists: the recompiled code only runs with
  // the default.xex it was generated from (game_image.h).
  void OnPostInitLogging() override {
    const auto& active = prepared_data_->paths;
    broken_bond::incident::Event("data-and-configuration", "end");
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    broken_bond::incident::SetMetadata("renderer_requested", rex::cvar::GetFlagByName("renderer"));
    broken_bond::incident::SetMetadata("native_backend_requested", rex::cvar::GetFlagByName("native_backend"));
    const auto consent = CrashReportingConsent();
    ask_crash_reporting_ = consent == broken_bond::reporting::Consent::kPending;
    broken_bond::reporting::Initialize(rex::filesystem::GetExecutableFolder(), active.cache_root,
                                       broken_bond::PortVersion(), consent);
#endif
    broken_bond::incident::Event("runtime-and-xex", "begin");
    for (const auto* key : {"renderer", "native_backend", "graphics_device", "resolution_scale", "msaa_samples",
                            "frame_rate", "aspect_ratio", "translation_mod", "skip_intro_videos", "log_level"})
      broken_bond::incident::SetMetadata(key, rex::cvar::GetFlagByName(key));
    // A failed previous session in this log too: the log is what players attach.
    if (const auto previous = broken_bond::incident::Latest(true, true); !previous.empty())
      REXLOG_WARN("[broken_bond] Previous session did not end normally: {}", broken_bond::incident::Summary(previous));
    REXLOG_INFO("[broken_bond] Data folder: {} ({}); settings {}, cache {} ({})", rex::path_to_utf8(active.data_root),
                SourceName(active.data_source), rex::path_to_utf8(active.config_path),
                rex::path_to_utf8(active.cache_root), SourceName(active.cache_source));
    for (const auto& message : prepared_data_->diagnostics) REXLOG_INFO("[broken_bond] Data folder: {}", message);
    REXLOG_INFO("[broken_bond] Paths: game data {} ({}), user data {} ({})", rex::path_to_utf8(game_data_root()),
                game_root_source_, rex::path_to_utf8(user_data_root()), user_root_source_);
    if (!game_data_root().empty()) LogGameData(game_data_root());
  }

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
  // Asked over the first frame of the game, unless the configuration or the
  // command line (tests, scripted boots) already holds an answer. An opt-in
  // from before the question counts as one.
  static broken_bond::reporting::Consent CrashReportingConsent() {
    using broken_bond::reporting::Consent;
    if (REXCVAR_GET(crash_reporting)) return Consent::kGiven;
    const auto source = rex::cvar::GetFlagSource("crash_reporting");
    const bool answered = REXCVAR_GET(crash_reporting_asked) || source == rex::cvar::Source::kCommandLine ||
                          source == rex::cvar::Source::kEnvironment;
    return answered ? Consent::kRefused : Consent::kPending;
  }

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
    if (!command_line_root_) {
      launch_marker_ = prepared_data_->paths.data_root / "launch.marker";
      std::error_code ec;
      start_failed_ = std::filesystem::exists(launch_marker_, ec);
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
    broken_bond::incident::Event("runtime-and-xex", "end");
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    broken_bond::reporting::FinishStartup();
#endif
    broken_bond::translation::Initialize(prepared_data_->paths.mods_dir);
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
        DlcSource(), user_data_root());
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
    broken_bond::reporting::Close();
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

  void OnClosing(rex::ui::UIEvent& event) override {
    // The SDK ends the process with _Exit here: neither destructors nor
    // OnShutdown run, so the crash reports flush first.
    broken_bond::reporting::Close();
    broken_bond::incident::ExpectedExit("window-close");
    rex::ReXApp::OnClosing(event);
  }
#endif

 private:
  // The data folder could not be prepared: the error in a bootstrap log in the
  // temporary folder (never in another data folder) and in a message box, on
  // every system.
  bool FailDataBootstrap(const std::string& error) {
    std::string message = "Could not prepare the data folder.\n" + Utf8Text(error);
    try {
      const std::string log_file = REXCVAR_GET(log_file);
      auto config = rex::BuildLogConfig(log_file.empty() ? nullptr : log_file.c_str(), REXCVAR_GET(log_level), {});
#ifdef _WIN32
      const auto process = GetCurrentProcessId();
#else
      const auto process = getpid();
#endif
      const auto directory = std::filesystem::temp_directory_path() / "broken_bond" / "bootstrap";
      config.app_name = "broken_bond_bootstrap_" + std::to_string(process);
      config.log_dir = rex::path_to_utf8(directory);
      rex::InitLogging(config);
      REXLOG_ERROR("[broken_bond] {}", message);
      rex::FlushLogging();
      message += "\nDiagnostic log: " + (log_file.empty() ? config.log_dir : log_file);
    } catch (const std::exception& e) {
      message += "\nCould not open the diagnostic log: " + Utf8Text(e.what());
    }
    message += "\nResolve the reported issue and start again. --data_root or REX_DATA_ROOT selects another "
               "writable folder for one session.";
    std::fprintf(stderr, "%s\n", message.c_str());
    std::fflush(stderr);
    // The platform's box: the SDK's only writes stderr outside Windows.
    gpu::platform::ShowError("Naruto: The Broken Bond", message);
    return false;
  }
  // On Windows the system's error texts come in the ANSI code page, mixed with
  // the data folder's UTF-8 paths; the log and the message box take UTF-8 only.
  // Each byte that does not start a UTF-8 character is read in the code page.
  static std::string Utf8Text(std::string_view text) {
#ifdef _WIN32
    std::string result;
    for (size_t i = 0; i < text.size();) {
      const auto byte = static_cast<unsigned char>(text[i]);
      const size_t length = byte < 0x80 ? 1 : (byte >> 5) == 6 ? 2 : (byte >> 4) == 14 ? 3 : (byte >> 3) == 30 ? 4 : 0;
      if (length && i + length <= text.size() &&
          MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data() + i, int(length), nullptr, 0)) {
        result.append(text.substr(i, length));
        i += length;
        continue;
      }
      wchar_t wide = 0xFFFD;
      MultiByteToWideChar(CP_ACP, 0, text.data() + i, 1, &wide, 1);
      char utf8[4];
      const int size = WideCharToMultiByte(CP_UTF8, 0, &wide, 1, utf8, sizeof(utf8), nullptr, nullptr);
      result.append(utf8, size > 0 ? size_t(size) : 0);
      ++i;
    }
    return result;
#else
    return std::string(text);
#endif
  }
  static const char* SourceName(broken_bond::data::Source source) {
    using broken_bond::data::Source;
    switch (source) {
      case Source::kConfig: return "the .toml";
      case Source::kUserDescriptor: return "saved selection";
      case Source::kPortableDescriptor: return "portable selection";
      case Source::kEnvironment: return "environment";
      case Source::kCommandLine: return "command line";
      default: return "default";
    }
  }
  // The folder the game installs DLC packages from at start: the data
  // folder's dlc/ by default (data_paths.h).
  std::filesystem::path DlcSource() const {
    const auto base = rex::cvar::GetFlagSource("dlc_source_path") == rex::cvar::Source::kConfig
        ? prepared_data_->config_base : prepared_data_->paths.install_dir;
    return broken_bond::data::ResolveDlcSource(prepared_data_->paths, rex::cvar::GetFlagByName("dlc_source_path"),
                                               base);
  }
  // The arguments a restart and a shortcut keep besides the storage overrides.
  std::vector<std::string> SessionExtraArguments() const {
    std::vector<std::string> arguments;
    const auto source = rex::cvar::GetFlagSource("log_file");
    if (source == rex::cvar::Source::kCommandLine || source == rex::cvar::Source::kEnvironment) {
      const auto value = rex::to_path(rex::cvar::GetFlagByName("log_file"));
      if (!value.empty()) arguments.push_back("--log_file=" + rex::path_to_utf8(
          (value.is_absolute() ? value : prepared_data_->paths.initial_cwd / value).lexically_normal()));
    }
    return arguments;
  }

  // The data folder of the session (SetupEnvironment). Declared first, so its
  // lock outlives every member that writes into the folder.
  std::optional<broken_bond::data::PreparedData> prepared_data_;
  std::optional<broken_bond::data::Environment> data_environment_;
  broken_bond::data::Overrides data_cli_;
  // A location chosen in Advanced, used from the next start.
  std::optional<std::filesystem::path> pending_data_root_;

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
  // The menus (menu/ui.h) through the UI layer of the native Present, from
  // before the runtime on (the setup screen): each Swap, or each Present of the
  // setup screen, asks the UI thread for the next frame while a document is
  // shown, and polls the menu shortcuts otherwise. F1 or Back+Start opens the
  // game menu once the game runs.
  void SetupUi() {
    menu_settings_.Initialize(rex::filesystem::GetExecutableFolder(), true, prepared_data_->paths.config_path);
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
    // A previous session that did not end normally: its folder, once.
    if (const auto previous = broken_bond::incident::Latest(true, true); !previous.empty()) {
      ui_.Notify(broken_bond::menu::Text("diagnostics-recovered"), broken_bond::menu::Text("diagnostics-open"),
                 [previous] { launcher_ui::OpenFolder(previous); });
      broken_bond::incident::Acknowledge(previous);
    }
    ui_.SetTick([this] { ServiceMenu(); });
    menu_settings_.SetUtilities([this] { CreateDesktopShortcut(); }, [this] { CheckUpdates(); });
    if (REXCVAR_GET(check_for_updates))
      updates_ = std::make_unique<launcher_updates::Checker>(broken_bond::PortVersion(), UpdatesFile());
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
  // screen, the live settings' results, the shortcut hint and the shader
  // indicator.
  void ServiceMenu() {
    PollGameChoice();
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
    menu_settings_.SetDataFolder(prepared_data_->paths, pending_data_root_, [this](broken_bond::data::Mode mode) {
      app_context().CallInUIThreadDeferred([this, mode] { RequestDataLocation(mode); });
    });
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
      else if (key == "skip_pipeline_draws") next.skip_pipeline_draws = config.skip_pipeline_draws;
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
      } else if (key == "crash_reporting") {
        // Turning it off revokes the consent at once; turning it on starts the
        // reports at the next start unless they run in this session.
        rex::cvar::SetFlagByName(key, config.crash_reporting ? "true" : "false");
        if (!broken_bond::reporting::SetConsent(config.crash_reporting) && config.crash_reporting)
          app_context().CallInUIThreadDeferred([this] { OfferSettingsRestart(); });
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
    // No DLC entry: the menus do not show the DLC page; packages in the data
    // folder's dlc/ are still installed at start (DlcSource).
    if (!ui_.ShowSetup(state, std::move(actions))) {
      REXLOG_WARN("[broken_bond] Setup screen unavailable; the game boots as configured");
      CloseSetupScreen();
      return false;
    }
    setup_paths_ = paths;
    setup_reason_ = reason;
    setup_resume_ = std::move(resume);
    setup_operation_ = broken_bond::incident::BeginOperation("setup-screen", broken_bond::menu::SetupReasonName(reason));
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
    if (setup_operation_)
      broken_bond::incident::Event("setup-screen", "end", {}, std::exchange(setup_operation_, uint64_t{0}));
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
        if (!broken_bond::menu::SaveGameRoot(prepared_data_->paths.config_path, result->root, error)) {
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
  // Another data folder, from Advanced: a folder chosen in the system picker,
  // the portable folder or the default. The choice is saved for the next
  // start, which copies this folder's files into it (data_migration.h); this
  // session keeps writing here. Choosing the session's own location again
  // clears a pending one.
  void RequestDataLocation(broken_bond::data::Mode mode) {
    if (closing_.load() || prepared_data_->paths.session_override) return;
    std::filesystem::path target;
    if (mode == broken_bond::data::Mode::kCustom) {
      const auto choice = broken_bond::menu::ChooseFile(window() ? window()->GetNativeWindowHandle() : nullptr, true);
      if (!choice.error.empty()) {
        REXLOG_WARN("[broken_bond] Data folder picker: {}", choice.error);
        ShowDataLocationError(); return;
      }
      if (choice.path.empty() || closing_.load()) return;
      target = choice.path;
    }
    const auto result = broken_bond::data::RequestLocation(*prepared_data_, *data_environment_, mode, target, data_cli_);
    if (!result) {
      REXLOG_WARN("[broken_bond] Data location selection: {}", result.error);
      ShowDataLocationError(); return;
    }
    if (result.value->pending_source || result.value->mode != prepared_data_->paths.mode)
      pending_data_root_ = result.value->data_root;
    else pending_data_root_.reset();
    REXLOG_INFO("[broken_bond] Data folder for the next start: {}",
                pending_data_root_ ? rex::path_to_utf8(*pending_data_root_) : std::string("unchanged"));
    menu_settings_.SetPendingDataLocation(pending_data_root_);
    if (pending_data_root_) OfferSettingsRestart();
  }
  void ShowDataLocationError() {
    using broken_bond::menu::Text;
    ui_.ShowDialog(Text("storage-error"), Text("storage-error-help"), {{Text("cancel"), {}}});
  }
  // The API, the card and the data folder belong to the session: a restart
  // starts a new process on the setup screen with the saved choice.
  void OfferSettingsRestart() {
    if (closing_.load()) return;
    using broken_bond::menu::Text;
    std::string message = Text(pending_data_root_ ? "storage-pending-help" : "changed");
    if (game_running_) message += std::string("\n\n") + Text("exit-confirm");
    ui_.ShowDialog(Text("restart-required"), message, {
        {Text("restart-now"), [this] {
          app_context().CallInUIThreadDeferred([this] { RestartForSettings(); });
        }},
        {Text("later"), {}}});
  }
  // A new process with the saved API and card, on the setup screen; this one
  // closes in its normal order first. It finds the same data folder by itself:
  // only the session's explicit locations go on its command line.
  void RestartForSettings() {
    const auto& active = prepared_data_->paths;
    if (menu_settings_.config().custom_game_root.empty() && !game_data_root().empty()) {
      // The game in use (the command line's or the one found nearby) for the next start.
      std::string error;
      if (!broken_bond::menu::SaveGameRoot(active.config_path, game_data_root(), error)) return;
    }
    auto arguments = broken_bond::launch::StorageArguments(active);
    const auto extra = SessionExtraArguments();
    arguments.insert(arguments.end(), extra.begin(), extra.end());
    arguments.push_back("--setup");
    bool started = false;
#ifdef _WIN32
    // The new process waits for this one (restart_wait_pid).
    const auto executable = active.install_dir / L"broken_bond.exe";
    arguments.push_back("--restart_wait_pid=" + std::to_string(GetCurrentProcessId()));
    const std::wstring command = broken_bond::launch::QuoteArgument(executable.wstring()) + L" " +
        broken_bond::launch::CommandLine(arguments);
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    auto writable = command;
    if (CreateProcessW(executable.c_str(), writable.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       active.initial_cwd.c_str(), &startup, &process)) {
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
    restart_wait_fd_ = broken_bond::launch::StartAfterExit(executable, std::move(arguments));
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
    const auto incident_operation = broken_bond::incident::BeginOperation("first-game-present");
    gpu::ui::UiLayer().SetFirstPresentCallback([this, incident_operation] {  // Engine owner, after a game Present.
      broken_bond::incident::Event("first-game-present", "end", {}, incident_operation);
      if (closing_.load()) return;
      app_context().CallInUIThread([this] {
        if (closing_.load()) return;
        RemoveLaunchMarker();
        if (game_running_ && ask_crash_reporting_) AskCrashReporting();
        else ScheduleShortcutHint();
      });
    });
  }
  void ScheduleShortcutHint() {
    if (game_running_) {
      shortcut_hint_pending_ = true;
      shortcut_hint_due_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    }
  }
  // The crash report question (reporting::Consent::kPending). The guest input
  // waits for the answer; B/Esc and Enter pick the last action, local reports only.
  void AskCrashReporting() {
    using broken_bond::menu::Text;
    ask_crash_reporting_ = false;
    crash_question_open_ = true;
    broken_bond::menu::input_blocked = true;
    const auto answer = [this](bool consent) {
      crash_question_open_ = false;
      if (!menu_settings_.AnswerCrashReporting(consent))
        REXLOG_WARN("[broken_bond] Crash reporting answer not saved; the question returns at the next start");
      rex::cvar::SetFlagByName("crash_reporting", consent ? "true" : "false");
      rex::cvar::SetFlagByName("crash_reporting_asked", "true");
      broken_bond::reporting::SetConsent(consent);
      broken_bond::menu::input_blocked = ui_.OverlayShown();
      REXLOG_INFO("[broken_bond] Crash reporting answered: {}", consent ? "uploads enabled" : "local reports only");
      ScheduleShortcutHint();
    };
    ui_.ShowDialog(Text("crash-reporting-ask"), Text("crash-reporting-ask-help"),
                   {{Text("crash-reporting-send"), [answer] { answer(true); }},
                    {Text("crash-reporting-local"), [answer] { answer(false); }}});
    REXLOG_INFO("[broken_bond] Crash reporting question shown; guest input neutral");
  }
  void CreateDesktopShortcut() {
    const bool ok = broken_bond::menu::WriteShortcut(broken_bond::menu::DesktopShortcut(),
        prepared_data_->paths.install_dir / "broken_bond.exe", prepared_data_->paths, SessionExtraArguments());
    ui_.Notify(broken_bond::menu::Text(ok ? "shortcut-done" : "save-failed"));
  }
  // The update check's state, at the data folder's root.
  std::filesystem::path UpdatesFile() const { return prepared_data_->paths.data_root / "menu-updates.json"; }
  void CheckUpdates() {
    using broken_bond::menu::Text;
    if (!updates_)
      updates_ = std::make_unique<launcher_updates::Checker>(broken_bond::PortVersion(), UpdatesFile());
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
    ui_.ShowOverlay([this] { OpenSettings(); }, {}, [this] {
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
    if (crash_question_open_) return;  // Opening the menu would close the question unanswered.
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
  std::unique_ptr<launcher_updates::Checker> updates_;
  bool update_notified_ = false, manual_update_check_ = false, settings_restart_ = false;
  std::string shortcut_hint_;
  bool shortcut_hint_pending_ = false;
  bool ask_crash_reporting_ = false;  // Shown over the first frame of the game.
  bool crash_question_open_ = false;  // The game menu waits for the answer.
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
  // The setup screen as an incident operation: the supervisor reads no stall
  // in the time the runtime waits for the player there.
  uint64_t setup_operation_ = 0;
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
