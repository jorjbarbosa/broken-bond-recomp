#pragma once

// The graphics options of the native renderer, in one place. Names and
// meanings are the SDK's, so the broken_bond.toml of the Xenos path serves this
// path too; the implementation is the renderer's own. The presenter options live
// in rexruntime.dll and are read by name; the others belong to the Xenos plugin,
// which registers them only when it runs, so settings.cpp registers them under
// the same names when the native renderer runs. The defaults keep the title's
// image. No SDK header here: the engine and its host tests take the values as
// plain data.
#include <cstdint>
#include <string>
#include "gpu/common/resolution_scale.h"

namespace gpu {
enum class PresentEffect : uint8_t { kBilinear, kCas, kFsr };
enum class SwapPostEffect : uint8_t { kNone, kFxaa, kFxaaExtreme };

struct Settings {
  bool vsync = true;                    // vsync
  bool allow_tearing = true;            // d3d12_allow_variable_refresh_rate_and_tearing
  int32_t anisotropic_override = -1;    // -1 keeps the title's; 0 disables; 1 to 5 force 1x to 16x
  PresentEffect present_effect = PresentEffect::kBilinear;
  float cas_additional_sharpness = 0.0f;  // [0, 1]
  float fsr_sharpness_reduction = 0.2f;   // RCAS, in stops [0, 2]
  uint32_t fsr_max_passes = 4;           // present_fsr_max_upsampling_passes: EASU passes [1, 4]
  SwapPostEffect swap_post_effect = SwapPostEffect::kNone;
  // Per axis, [1, 4]: resolution_scale and draw_resolution_scale_x/y
  // (integers, the SDK's), or internal_resolution_scale (own, fractions allowed).
  ResolutionScale resolution_scale;
  // Own options, without an SDK counterpart.
  uint32_t max_frame_latency = 1;     // max_frame_latency: Presents queued ahead [1, 3]
  bool triple_buffering = false;      // triple_buffering: three swapchain buffers, not two
  // Own option: 0.5 keeps the title's gamma ramp.
  float brightness = 0.5f;            // brightness [0, 1]
  // Own option: the sample count of the
  // targets the title creates with MSAA; 0 keeps the title's (4x here).
  uint32_t msaa_samples = 0;          // msaa_samples: 0, 2, 4 or 8
  // Own option: alpha-tested
  // draws into multisampled targets use alpha to coverage instead.
  bool transparency_antialiasing = false;  // transparency_antialiasing
  // Own option: above 30, the
  // state interpolation (gpu/interpolation/tick.h) at that frame rate.
  uint32_t frame_rate = 30;           // frame_rate: 30 (the game's) to 240
  // Own option: a batched draw whose pipeline is
  // not ready is left out of its frame while the pipeline compiles, instead of
  // the frame waiting for it (DrawRequest::PipelineTake::kSkip). Off by default.
  bool skip_pipeline_draws = false;   // skip_pipeline_draws
  // Own option: the width over the height of
  // the image; 0 keeps the title's 16:9 (host/aspect.cpp).
  float aspect_ratio = 0.0f;          // aspect_ratio: 0, or 1.25 to 4
  // The native renderer's adapter (settings.cpp defines the option): the adapter by
  // its name; empty keeps the one with the most dedicated video memory.
  std::string graphics_device;        // graphics_device
  std::string native_backend;         // d3d12 or vulkan; fixed for the session.
};

// Registers the options of the Xenos plugin under its names, once, when the
// native mode is configured: with the Xenos renderer the plugin owns them.
void RegisterPluginOptions();
// Reads the options once, when the native mode is configured (the runtime has
// loaded the configuration by then): the initial snapshot. Later edits are
// requested below and applied between frames.
void LoadSettings();
// Value snapshots: the menu, translator and render threads share no mutable
// data. CurrentSettings is the calling thread's own copy of the settings in
// use, refreshed by its next call once a newer one was applied, so the
// per-draw reads take no lock.
const Settings& CurrentSettings();
Settings RequestedSettings();
// Queues the settings for the next frame boundary; returns their revision. The
// API, the adapter and the aspect ratio stay the session's, and the resolution
// scale follows the session's aspect ratio (AnamorphicScale).
uint64_t RequestSettings(Settings settings);
struct SettingsResult {
  uint64_t revision = 0;
  bool applied = false;
  std::string evidence;
};
SettingsResult LastSettingsResult();
class Dispatcher;
// Guest thread, at the end of the Swap. The newest request goes to
// Engine::Reconfigure behind all preceding translation work and is published
// once the engine took it; a refusal restores the request to the settings in use.
void ApplyPendingSettings(Dispatcher& dispatcher);
// The boot options and their sources, as journal fields. Applied edits have
// their own native-live-settings records.
std::string SettingsEvidence();
}  // namespace gpu
