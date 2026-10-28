// broken_bond - ReXGlue Recompiled Project
//
// Minimal host for the recompiled game using the SDK runtime.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <limits>

#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "crash_report.h"
#include "fps_overlay.h"
#include "graphics_device.h"
#include "input_replay.h"
#include "mods/translation_mod.h"
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
#include "gpu/common/shader_activity.h"
#include "gpu/host/app.h"
#include "gpu/ui/immediate_drawer.h"
#include "gpu/ui/layer.h"
#include "menu/input_gate.h"
#include "menu/settings.h"
#include "menu/shortcut.h"
#include "menu/text.h"
#include "menu/ui.h"
#include "menu/update_checker.h"
#include "port_version.h"
#include <SDL3/SDL_misc.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>

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

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
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
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    if (gpu::host::Enabled()) SetupUi();
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
    broken_bond::menu::input_blocked = false;
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
      app_context().CallInUIThreadDeferred([this] {
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
  // The menus (menu/ui.h) through the UI layer of the native Present: each
  // Swap asks the UI thread for the next frame while a document is shown, and
  // polls the menu shortcuts otherwise. F1 or Back+Start opens the game menu.
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
    UpdateMenuShortcut();
    ui_.SetFpsVisible(rex::cvar::Query<bool>("show_fps_overlay"));
    // The hint shows a few seconds after the game's first frame reaches the screen.
    gpu::ui::UiLayer().SetFirstPresentCallback([this] {  // Engine owner.
      if (closing_.load()) return;
      app_context().CallInUIThread([this] {
        if (closing_.load()) return;
        shortcut_hint_pending_ = true;
        shortcut_hint_due_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
      });
    });
    REXLOG_INFO("[broken_bond] Game menu: {} or {}", menu_settings_.menu_options().key, menu_settings_.menu_options().chord);
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
  // On the UI thread with each menu frame: the shortcut hint and the shader indicator.
  void ServiceMenu() {
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
  // The settings pages (menu/settings.h). The frame rate, fullscreen, the menu
  // language and the menu shortcuts apply at once; the options marked RESTART
  // apply at the next start, which a notice recalls when the pages close.
  void OpenSettings() {
    menu_settings_.Open(ui_, 0, true, {}, [this](const std::string& key) {
      const auto& config = menu_settings_.config();
      if (key == "show_fps_overlay") {
        rex::cvar::SetFlagByName(key, config.show_fps_overlay ? "true" : "false");
        ui_.SetFpsVisible(config.show_fps_overlay);
      } else if (key == "fullscreen") {
        rex::cvar::SetFlagByName(key, config.fullscreen ? "true" : "false");
      } else if (key == "bind_game_menu" || key == "game_menu_chord") {
        UpdateMenuShortcut();
      } else if (key != "launcher_language" && key != "show_shader_indicator") {
        settings_restart_ = true;
      }
    }, [this] {
      if (settings_restart_) ui_.Notify(broken_bond::menu::Text("changed"));
      settings_restart_ = false;
      if (ui_.OverlayShown()) OpenGameMenu();
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
  std::chrono::steady_clock::time_point shortcut_hint_due_{};
  std::atomic<bool> ui_draw_pending_{false};
  std::atomic<bool> closing_{false};
  bool close_ready_ = false;  // The ordered close finished: the window may close.
#endif
  FullscreenShortcuts fullscreen_shortcuts_;
  std::unique_ptr<FpsOverlayDialog> fps_overlay_;
};
