#pragma once

// The title's screen layer (screen_layer.cpp): what it draws in its own
// 1280 x 720 space instead of through a camera. At another aspect the
// translation puts these draws into the centered 16:9 area of the surface
// (DrawRequest::screen_area), so they keep their proportions while the
// camera fills the surface.
#include <cstdint>

namespace gpu::screen_layer {
enum class Kind : uint8_t { kNone, kFlash, kVideo };
// The producer this thread is drawing for.
Kind Current();
}  // namespace gpu::screen_layer
