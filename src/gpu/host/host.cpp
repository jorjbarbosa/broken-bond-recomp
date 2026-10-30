// The host of the native build: the runtime configuration that puts the
// graphics system of the D3D boundary in place of the Xenos plugin, the window
// close and the guest memory reads of the services. The other responsibilities
// of the host live in host/*.cpp (internal.h).
#include "gpu/host/app.h"
#include "diagnostics/incident.h"
#include "gpu/host/internal.h"
#include "gpu/d3d/d3d.h"
#include "gpu/common/guest_memory.h"
#include "gpu/common/platform.h"
#include "gpu/settings.h"
#include "gpu/diagnostics/crash_dump.h"
#include "gpu/diagnostics/heap_watch.h"
#include "wine.h"
#include <cstdlib>
#include <fmt/format.h>
#include <rex/logging.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/xmemory.h>

REXCVAR_DECLARE(std::string, native_backend);  // gpu/settings.cpp

// Accept the old selector for configuration compatibility; this build always
// uses a native backend, including when launched directly with an old TOML.
REXCVAR_DEFINE_STRING(renderer, "native", "broken_bond/Graphics",
                      "Native renderer (Direct3D 12 or Vulkan); legacy xenos values are migrated")
    .allowed({"xenos", "native"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
// The diagnostic cases: the native mode with the journal and both options below.
REXCVAR_DEFINE_BOOL(native_renderer_bootstrap, false, "broken_bond/Diagnostics",
                    "Isolate graphics and stop at the first unknown service");
REXCVAR_DEFINE_STRING(native_renderer_bootstrap_path, "", "broken_bond/Diagnostics",
                      "Exclusive JSONL checkpoint output; required in bootstrap mode");
REXCVAR_DEFINE_BOOL(native_renderer_bootstrap_initialize_engines, false, "broken_bond/Diagnostics",
                    "Initialize the native engines");
REXCVAR_DEFINE_BOOL(native_renderer_bootstrap_register_interrupt_callback, false, "broken_bond/Diagnostics",
                    "Retain the reviewed interrupt registration");

namespace gpu::host {
std::atomic<bool> enabled{false};

bool Enabled() { return enabled.load(); }
bool Read(rex::memory::Memory* memory, uint32_t address, void* target, size_t size) {
  if (!address || uint64_t(address) + size > UINT32_MAX) return false;
  return CopyFromGuest(memory, address, target, size);  // guest_memory.h
}

// The window close of the native mode, on the UI thread, before
// ReXApp::OnClosing (TerminateTitle, std::_Exit(0)). The close always proceeds.
bool CloseRequested() {
  if (!enabled.load()) return true;
  return gpu::CloseInOrder();
}

void Configure(rex::RuntimeConfig& config) {
  const bool bootstrap = REXCVAR_GET(native_renderer_bootstrap);
  if (REXCVAR_GET(renderer) != "native")
    REXLOG_INFO("[broken_bond] Migrating legacy renderer selection to native.");
  // The native mode is the D3D boundary on the own engines and the own
  // interrupt registration; the diagnostic cases name both and the journal.
  const auto path = REXCVAR_GET(native_renderer_bootstrap_path);
  if (bootstrap && (!REXCVAR_GET(native_renderer_bootstrap_initialize_engines) ||
                    !REXCVAR_GET(native_renderer_bootstrap_register_interrupt_callback) || path.empty()))
    { broken_bond::incident::FirstFailure("native-bootstrap-configuration"); std::_Exit(88); }
  if (config.graphics) { broken_bond::incident::FirstFailure("native-graphics-already-configured"); std::_Exit(88); }
  if (!path.empty() && !OpenJournal(path)) { broken_bond::incident::FirstFailure("native-journal-open", path); std::_Exit(88); }
  enabled = true;
  // The graphics options, read once before the graphics system exists.
  gpu::RegisterPluginOptions();
  gpu::LoadSettings();
  const auto& backend = gpu::CurrentSettings().native_backend;
  bool available = false;
#if !defined(BROKENBOND_NATIVE_D3D12_DISABLED)
  available |= backend == "d3d12";
#endif
#if defined(BROKENBOND_NATIVE_VULKAN)
  available |= backend == "vulkan";
#endif
  if (!available) {
    REXLOG_ERROR("[broken_bond] Requested native_backend '{}' is unavailable in this build. No backend fallback.", backend);
    broken_bond::incident::FirstFailure("native-backend-unavailable", backend);
    rex::FlushLogging();
    platform::ShowError("broken_bond", fmt::format("The graphics API '{}' is not available in this build.\n\n"
        "Choose another one in Settings > Display and start the game again.", backend));
    std::_Exit(88);
  }
#if defined(BROKENBOND_NATIVE_VULKAN)
  if (const char* wine = broken_bond::WineVersion())
    REXLOG_INFO("[broken_bond] Wine {} detected: the native renderer runs on Vulkan", wine);
#endif
#if defined(BROKENBOND_NATIVE_D3D12_DISABLED)
  if (const auto requested = REXCVAR_GET(native_backend); requested != backend)
    REXLOG_INFO("[broken_bond] native_backend '{}' is not in this build: the native renderer runs on {}", requested, backend);
#endif
  REXLOG_INFO("[broken_bond] Renderer: native; backend: {}{}", backend, path.empty() ? "" : ", with the journal");
  gpu::crash_dump::Install();  // REX_NATIVE_CRASH_DUMP=1 (crash_dump.h).
  gpu::heap_watch::Install();  // REX_NATIVE_HEAP_WATCH=1 (heap_watch.h).
  config.gpu_plugin.clear();
  config.graphics = CreateGraphicsSystem();
  {
    std::lock_guard lock(event_mutex);
    Record("host-setup", "OnPreSetup-own-system-plugin-cleared", nullptr);
  }
  Record("native-d3d-settings", gpu::SettingsEvidence());
}
}  // namespace gpu::host
