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
#include "menu/text.h"
#include "menu/ui.h"
#include <rex/logging.h>
#include <rex/ui/presenter.h>
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
    ui_.EnableGameMenu(kMenuKey, kMenuChord, [this] { ToggleGameMenu(); });
    ui_.SetFpsVisible(rex::cvar::Query<bool>("show_fps_overlay"));
    char hint[512];
    std::snprintf(hint, sizeof(hint), broken_bond::menu::Text("shortcut-format"), kMenuKey, "Back + Start");
    shortcut_hint_ = hint;
    // The hint shows a few seconds after the game's first frame reaches the screen.
    gpu::ui::UiLayer().SetFirstPresentCallback([this] {  // Engine owner.
      if (closing_.load()) return;
      app_context().CallInUIThread([this] {
        if (closing_.load()) return;
        shortcut_hint_pending_ = true;
        shortcut_hint_due_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
      });
    });
    REXLOG_INFO("[broken_bond] Game menu: {} or Back+Start", kMenuKey);
  }
  // On the UI thread with each menu frame: the shortcut hint and the shader indicator.
  void ServiceMenu() {
    if (shortcut_hint_pending_ && ui_.OverlayShown()) shortcut_hint_pending_ = false;
    if (shortcut_hint_pending_ && std::chrono::steady_clock::now() >= shortcut_hint_due_) {
      shortcut_hint_pending_ = false;
      ui_.ShowShortcutHint(shortcut_hint_);
    }
    const auto progress = gpu::CurrentShaderProgress();
    ui_.SetShadersVisible(gpu::ShaderCompilationVisible(), progress.pending, progress.fraction);
  }
  void OpenGameMenu() {
    using broken_bond::menu::Text;
    ui_.ShowOverlay({}, {}, [this] {
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

  static constexpr const char* kMenuKey = "F1";
  static constexpr const char* kMenuChord = "Back+Start";
  std::unique_ptr<rex::ui::ImmediateDrawer> native_drawer_;  // Before ui_: it outlives the Ui.
  broken_bond::menu::Ui ui_;
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
