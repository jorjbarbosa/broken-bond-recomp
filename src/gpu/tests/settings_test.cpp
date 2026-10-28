// Host test of the settings queue (gpu/settings.cpp) through a dispatcher on
// the engine of REX_NATIVE_TEST_API. Never enters a guest function. The scale
// rule alone (the sizes of its comment, the 4x limit, the boot values of the
// rule it replaced, a scale that follows it comes back bit for bit); then the
// boot snapshot at 21:9, a request applied between frames with the new scale
// in the engine's resolves, the session's API, adapter and aspect kept, a
// request made from the settings in use that keeps the scale, a refused
// request that restores the request, and another thread's copy.
// Usage: native_engine_settings_test <new output.jsonl>
#include "gpu/settings.h"
#include "gpu/backend/dispatcher.h"
#include "gpu/backend/tests/test_backend.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>
#include <rex/cvar.h>

namespace {
std::string recorded;  // The last journal record of the queue.
}  // namespace

// The journal of the host (host/host.cpp), which the test does not link.
namespace gpu::host {
void Record(std::string_view kind, std::string_view fields) {
  recorded = std::string(kind) + " " + std::string(fields);
}
}  // namespace gpu::host
namespace gpu::sampler {
void RegisterThread(const char*) {}  // The sampler is not linked here.
}  // namespace gpu::sampler

using namespace gpu;
namespace {
bool Same(ResolutionScale a, ResolutionScale b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }
std::string Size(ResolutionScale k) {
  return "{\"scale\":[" + std::to_string(k.x) + "," + std::to_string(k.y) + "],\"size\":[" +
      std::to_string(k.Width(1280)) + "," + std::to_string(k.Height(720)) + "]}";
}
// The rule as LoadSettings had it before AnamorphicScale.
ResolutionScale Before(ResolutionScale k, float aspect) {
  const float wide = aspect / (16.0f / 9.0f);
  if (wide > 1.0f) {
    k.x = std::min(k.y * wide, 4.0f);
    k.y = k.x / wide;
  } else {
    k.y = std::min(k.x / wide, 4.0f);
    k.x = k.y * wide;
  }
  return k;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 || std::filesystem::exists(argv[1])) return 2;
  std::ofstream output(argv[1]);
  if (!output) return 3;
  bool passed = true;
  auto check = [&](const std::string& kind, bool ok, const std::string& detail = "{}") {
    passed &= ok;
    output << "{\"kind\":\"" << kind << "\",\"ok\":" << (ok ? "true" : "false")
           << ",\"evidence\":" << (detail.empty() ? "null" : detail) << "}\n";
    output.flush(); return ok;
  };

  const float ultrawide = 3440.0f / 1440.0f;
  const ResolutionScale wide = AnamorphicScale({2, 2}, ultrawide), narrow = AnamorphicScale({2, 2}, 1.6f);
  check("rule-21-9", wide.Width(1280) == 3440 && wide.Height(720) == 1440, Size(wide));
  check("rule-16-10", narrow.Width(1280) == 2560 && narrow.Height(720) == 1600, Size(narrow));
  const ResolutionScale limited = AnamorphicScale({4, 4}, ultrawide);
  check("rule-limit", limited.x == 4.0f && limited.y < 4.0f && Same(AnamorphicScale(limited, ultrawide), limited),
        Size(limited));
  check("rule-title-aspect", Same(AnamorphicScale({1.5f, 1.5f}, 0.0f), {1.5f, 1.5f}));
  bool stable = true, unchanged = true;
  for (const float aspect : {1.25f, 1.6f, 2.0f, 2.37f, ultrawide, 3.2f, 3.5556f, 4.0f})
    for (float k = 1.0f; k <= 4.0f; k += 0.125f)
      for (const ResolutionScale base : {ResolutionScale{k, k}, ResolutionScale{k, 1.0f}, ResolutionScale{1.0f, k}}) {
        const ResolutionScale once = AnamorphicScale(base, aspect), old = Before(base, aspect);
        stable &= Same(AnamorphicScale(once, aspect), once) && once.x >= 1 && once.x <= 4 && once.y >= 1 &&
            once.y <= 4;
        unchanged &= once.Width(1280) == old.Width(1280) && once.Height(720) == old.Height(720);
      }
  check("rule-applies-once", stable);
  check("rule-boot-sizes-unchanged", unchanged);

  // The boot snapshot at 21:9 with 1.5x.
  check("aspect-option", rex::cvar::SetFlagByName("aspect_ratio", "2.3888889"));
  check("scale-option", rex::cvar::SetFlagByName("internal_resolution_scale", "1.5"));
  LoadSettings();
  const Settings boot = CurrentSettings();
  check("boot-snapshot", boot.aspect_ratio > 2.38f && boot.aspect_ratio < 2.39f &&
        Same(boot.resolution_scale, AnamorphicScale({1.5f, 1.5f}, boot.aspect_ratio)) &&
        Same(RequestedSettings().resolution_scale, boot.resolution_scale) && LastSettingsResult().revision == 0,
        Size(boot.resolution_scale));

  WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"SettingsQueueTest"; RegisterClassW(&cls);
  HWND window = CreateWindowW(cls.lpszClassName, L"Settings queue test", WS_POPUP, 0, 0, 128, 72,
      nullptr, nullptr, cls.hInstance, nullptr);
  Dispatcher dispatcher(true);
  std::string evidence;
  if (!check("initialize", dispatcher.Initialize(evidence, TestRenderMode()), evidence)) return 4;
  PresentMode present;
  present.vsync = false;
  SurfaceDesc color{0x1000, 64, 36, 0, 1};
  check("attach", dispatcher.Run([&](Engine& e, std::string& out) {
    return e.AttachWindow(window, out, present) && e.CreateSurface(color, out);
  }, evidence), evidence);
  // The size of a new resolve of the surface: the engine's scale.
  const auto resolved = [&](uint32_t key, uint32_t& w, uint32_t& h) {
    return dispatcher.Run([&](Engine& e, std::string& out) {
      ClearRequest clear; clear.color_keys[0] = color.key; clear.clear_color = true; clear.color = {0, 1, 0, 1};
      ResolveRequest resolve; resolve.source_key = color.key; resolve.dest_key = key;
      resolve.dest_width = 64; resolve.dest_height = 36; resolve.dest_format = 6;
      uint64_t fence = 0;
      std::vector<uint8_t> rgba;
      return e.ClearSurfaces(clear, fence, out) && e.ResolveSurface(resolve, fence, out) &&
          e.ReadTexture(key, rgba, w, h, out);
    }, evidence);
  };

  ApplyPendingSettings(dispatcher);
  check("nothing-requested", LastSettingsResult().revision == 0 && recorded.empty());

  auto next = RequestedSettings();
  next.brightness = 0.75f;
  next.resolution_scale = {2, 2};
  next.vsync = false;
  next.triple_buffering = true;
  next.native_backend = "elsewhere";
  next.graphics_device = "Another adapter";
  next.aspect_ratio = 0;
  const uint64_t first = RequestSettings(next);
  const Settings requested = RequestedSettings();
  check("request-revision", first == 1);
  check("request-session-kept", requested.native_backend == boot.native_backend &&
        requested.graphics_device == boot.graphics_device && requested.aspect_ratio == boot.aspect_ratio);
  check("request-scale-rule", Same(requested.resolution_scale, AnamorphicScale({2, 2}, boot.aspect_ratio)) &&
        requested.resolution_scale.Width(1280) == 3440 && requested.resolution_scale.Height(720) == 1440,
        Size(requested.resolution_scale));
  check("not-applied-before-boundary", CurrentSettings().brightness == boot.brightness);
  ApplyPendingSettings(dispatcher);
  SettingsResult result = LastSettingsResult();
  check("applied", result.revision == 1 && result.applied, result.evidence);
  check("published", CurrentSettings().brightness == 0.75f && !CurrentSettings().vsync &&
        CurrentSettings().triple_buffering && Same(CurrentSettings().resolution_scale, requested.resolution_scale));
  check("recorded", recorded.starts_with("native-live-settings ") &&
        recorded.find("\"revision\":1,\"applied\":true") != std::string::npos, "\"" + recorded.substr(0, 60) + "\"");
  uint32_t w = 0, h = 0;
  const auto size = [&] { return "{\"size\":[" + std::to_string(w) + "," + std::to_string(h) + "]}"; };
  bool read = resolved(0x3000, w, h);
  check("engine-scale", read && w == requested.resolution_scale.Width(64) &&
        h == requested.resolution_scale.Height(36), size());
  float other = 0;
  std::thread([&] { other = CurrentSettings().brightness; }).join();
  check("other-thread-copy", other == 0.75f);

  next = RequestedSettings();
  next.frame_rate = 60;
  const uint64_t second = RequestSettings(next);
  check("request-from-settings-in-use", second == 2 &&
        Same(RequestedSettings().resolution_scale, CurrentSettings().resolution_scale));
  ApplyPendingSettings(dispatcher);
  result = LastSettingsResult();
  check("applied-again", result.revision == 2 && result.applied && CurrentSettings().frame_rate == 60,
        result.evidence);

  next = RequestedSettings();
  next.resolution_scale.x = next.resolution_scale.y = std::numeric_limits<float>::quiet_NaN();
  next.brightness = 0.25f;
  const uint64_t third = RequestSettings(next);
  ApplyPendingSettings(dispatcher);
  result = LastSettingsResult();
  check("invalid-refused", third == 3 && result.revision == 3 && !result.applied, result.evidence);
  const Settings kept = CurrentSettings(), restored = RequestedSettings();
  check("refusal-keeps-settings", kept.brightness == 0.75f && kept.frame_rate == 60 &&
        Same(kept.resolution_scale, requested.resolution_scale));
  check("refusal-restores-request", restored.brightness == kept.brightness &&
        Same(restored.resolution_scale, kept.resolution_scale));
  check("recorded-refusal", recorded.find("\"revision\":3,\"applied\":false") != std::string::npos,
        "\"" + recorded.substr(0, 60) + "\"");

  next = RequestedSettings();
  next.resolution_scale = {1, 1};
  const uint64_t fourth = RequestSettings(next);
  ApplyPendingSettings(dispatcher);
  result = LastSettingsResult();
  check("applied-after-refusal", fourth == 4 && result.revision == 4 && result.applied, result.evidence);
  read = resolved(0x4000, w, h);
  check("engine-scale-after-refusal", read && w == CurrentSettings().resolution_scale.Width(64) &&
        h == CurrentSettings().resolution_scale.Height(36), size());

  check("release", dispatcher.Run([](Engine& e, std::string& out) {
    return e.ReleaseRemaining(out) && e.DetachWindow(out);
  }, evidence), evidence);
  check("shutdown", dispatcher.Shutdown(evidence), evidence);
  DestroyWindow(window);
  output << "{\"kind\":\"verdict\",\"passed\":" << (passed ? "true" : "false") << ",\"guest_executed\":false}\n";
  return passed && output.good() ? 0 : 1;
}
