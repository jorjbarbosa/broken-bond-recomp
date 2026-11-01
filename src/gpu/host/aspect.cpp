// The aspect ratio of the image on the title's side (app.h, ApplyAspectRatio).
//
// The title's cameras take the ratio of the height to the width of the image
// from the float at 0x82E26800, 9/16. Neither IDA nor the generated code shows
// a direct load of it (it is read through a base register); writing 0.421875
// there at load narrowed every object of the title scene to 0.75 of its width
// with its height unchanged: the vertical field of view is fixed and the
// horizontal one follows the ratio. So 1/aspect widens the view at 21:9 and
// 32:9 and narrows it at 16:10. The other 9/16 float of the image, at
// 0x8209F4D0, changed nothing.
//
// The renderer draws the title's 1280 x 720 surfaces at the image's aspect,
// which gives the narrowed geometry back its proportions. Nothing is written
// at 16:9, and nothing when the float does not hold 9/16 (another revision).
#include "gpu/host/app.h"
#include "gpu/host/services.h"
#include "gpu/settings.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <fmt/format.h>
#include <rex/logging.h>
#include <rex/memory.h>

namespace gpu::host {
namespace {
constexpr uint32_t kCameraRatio = 0x82E26800;
constexpr uint32_t kTitleRatio = 0x3F100000;  // 9/16
}  // namespace

void ApplyAspectRatio(uint8_t* base) {
  const float aspect = gpu::CurrentSettings().aspect_ratio;
  if (!base || aspect <= 0.0f) return;
  uint8_t* host = base + kCameraRatio;
  uint32_t raw = 0;
  std::memcpy(&raw, host, sizeof(raw));
  const uint32_t found = std::byteswap(raw);
  if (found != kTitleRatio) {
    REXLOG_ERROR("[broken_bond] aspect ratio {:.4f} not applied: the camera ratio at 0x{:08X} is 0x{:08X}, not 9/16",
                 aspect, kCameraRatio, found);
    return;
  }
  const float ratio = 1.0f / aspect;
  // The protection is changed for the whole page: mprotect takes a page-aligned
  // address (VirtualProtect rounds it down itself).
  const uintptr_t page_mask = ~uintptr_t(rex::memory::page_size() - 1);
  auto* page = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(host) & page_mask);
  const size_t length = size_t(host - page) + sizeof(raw);
  rex::memory::PageAccess access{};
  const bool writable = rex::memory::Protect(page, length, rex::memory::PageAccess::kReadWrite, &access);
  raw = std::byteswap(std::bit_cast<uint32_t>(ratio));
  std::memcpy(host, &raw, sizeof(raw));
  if (writable) rex::memory::Protect(page, length, access);
  REXLOG_INFO("[broken_bond] aspect ratio {:.4f}: camera ratio {:.6f} at 0x{:08X}", aspect, ratio, kCameraRatio);
  Record("native-aspect-ratio",
         fmt::format("\"aspect_ratio\":{},\"camera_ratio\":{},\"address\":\"{:08X}\"", aspect, ratio, kCameraRatio));
}
}  // namespace gpu::host
