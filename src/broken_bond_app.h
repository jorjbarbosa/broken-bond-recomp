// broken_bond - ReXGlue Recompiled Project
//
// Minimal host for the recompiled game using the SDK runtime.

#pragma once

#include <limits>

#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "crash_report.h"
#include "fps_overlay.h"
#include "graphics_device.h"
#include "input_replay.h"
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
#include "gpu/host/app.h"
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
#endif
    if (!config.graphics) SelectXenosAdapter();
  }

  void OnPostSetup() override {
    broken_bond::crash_report::Install();  // After the SDK's exception handler.
    if (window()) {
      // Window shortcuts take priority over overlays and keyboard emulation.
      window()->AddInputListener(&fullscreen_shortcuts_,
                                 std::numeric_limits<size_t>::max());
    }
    RestoreXenosAdapter();
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    SetGuestFrameStats(ReadGuestFrameStats);
    if (rex::cvar::Query<bool>("show_fps_overlay")) {
      fps_overlay_ = std::make_unique<FpsOverlayDialog>(drawer, ReadGuestFrameStats);
    }
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
    if (window()) window()->RemoveInputListener(&fullscreen_shortcuts_);
    rex::ui::UnregisterBind("bind_fps_overlay");
    fps_overlay_.reset();
    RestoreXenosAdapter();
  }

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
  bool OnWindowCloseRequested() override {
    // The native renderer closes in order (guest threads joined, device
    // owner, resources and engine released) before OnClosing hard-exits.
    return gpu::host::CloseRequested();
  }
#endif

 private:
  FullscreenShortcuts fullscreen_shortcuts_;
  std::unique_ptr<FpsOverlayDialog> fps_overlay_;
};
