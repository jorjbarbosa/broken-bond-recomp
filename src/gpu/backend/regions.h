// The host rectangles of the engine's operations, the same for every backend:
// the guest edges of a request mapped by the resolution scale
// (common/resolution_scale.h) and clipped to the surface they act on.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include "gpu/backend/contracts.h"

namespace gpu {

struct HostRect {
  int32_t left = 0, top = 0, right = 0, bottom = 0;
};

// The rectangle a clear covers on a surface of width x height: the request's
// edges, or the whole surface when they are empty, clipped to the surface.
// False when nothing of the surface is left.
inline bool ClearRect(const ResolutionScale& k, const ClearRequest& request, uint32_t width, uint32_t height,
                      HostRect& rect) {
  rect = {k.X(request.left), k.Y(request.top), k.X(request.right), k.Y(request.bottom)};
  if (rect.right <= rect.left || rect.bottom <= rect.top) rect = {0, 0, int32_t(width), int32_t(height)};
  rect.left = std::max(rect.left, 0);
  rect.top = std::max(rect.top, 0);
  rect.right = std::min(rect.right, int32_t(width));
  rect.bottom = std::min(rect.bottom, int32_t(height));
  return rect.right > rect.left && rect.bottom > rect.top;
}

// A resolve in host pixels: the destination's size, the source rectangle and
// the destination point (the request's, in guest pixels).
struct HostResolve {
  uint32_t dest_width = 0, dest_height = 0, left = 0, top = 0, right = 0, bottom = 0, dest_x = 0, dest_y = 0;
};
inline HostResolve ResolveInHost(const ResolutionScale& k, const ResolveRequest& request) {
  return {k.Width(request.dest_width), k.Height(request.dest_height), uint32_t(k.X(int32_t(request.left))),
          uint32_t(k.Y(int32_t(request.top))), uint32_t(k.X(int32_t(request.right))),
          uint32_t(k.Y(int32_t(request.bottom))), uint32_t(k.X(int32_t(request.dest_x))),
          uint32_t(k.Y(int32_t(request.dest_y)))};
}

// The reason a resolve is refused before anything is recorded: the regions
// and samples no resolve path covers (the deferred cases, which the draw layer
// stops before reaching). Null when covered. same_size: the source and the
// destination have one size.
inline const char* ResolveUncovered(const ResolveRequest& request, bool depth, uint32_t samples, bool same_size) {
  if (depth && (!same_size || request.has_rect || request.dest_x || request.dest_y))
    return "native-depth-resolve-region-uncovered";
  if (depth && (request.sample < -1 || request.sample >= int32_t(samples)))
    return "native-depth-resolve-sample-uncovered";
  if (!depth && samples > 1 && (!same_size || request.has_rect)) return "native-resolve-msaa-region-uncovered";
  return nullptr;
}

// The source box of a copy resolve: the rectangle (or the whole source)
// clipped to the source and to what fits from the destination point on.
// False when nothing is left.
struct HostBox {
  uint32_t left = 0, top = 0, right = 0, bottom = 0;
};
inline bool CopyBox(const HostResolve& resolve, bool has_rect, uint32_t source_width, uint32_t source_height,
                    uint32_t dest_width, uint32_t dest_height, HostBox& box) {
  box.left = has_rect ? resolve.left : 0;
  box.top = has_rect ? resolve.top : 0;
  box.right = std::min(has_rect ? resolve.right : source_width, source_width);
  box.bottom = std::min(has_rect ? resolve.bottom : source_height, source_height);
  box.right = std::min(box.right, box.left + dest_width - std::min(resolve.dest_x, dest_width));
  box.bottom = std::min(box.bottom, box.top + dest_height - std::min(resolve.dest_y, dest_height));
  return box.right > box.left && box.bottom > box.top;
}

// The host position of a draw's viewport and scissor (guest pixels): the
// resolution scale of each axis or, for the screen layer at another aspect
// (DrawRequest::screen_area), one scale for both axes, the smaller, with the
// title's 1280 x 720 space centered on the surface, whose host size is each
// axis's scale times 1280 x 720, at a whole host pixel so the 2D keeps one
// texel per pixel. Without the area it maps as ResolutionScale.
struct ScreenArea {
  float sx, sy, ox = 0.0f, oy = 0.0f;
  ScreenArea(const ResolutionScale& k, bool area) : sx(k.x), sy(k.y) {
    if (!area) return;
    const float s = std::min(k.x, k.y);
    ox = std::round((k.x - s) * 640.0f);
    oy = std::round((k.y - s) * 360.0f);
    sx = sy = s;
  }
  float X(float x) const { return ox + x * sx; }
  float Y(float y) const { return oy + y * sy; }
  int32_t EdgeX(int32_t x) const { return Edge(x, ox, sx); }
  int32_t EdgeY(int32_t y) const { return Edge(y, oy, sy); }

  // A draw viewport (x, y, width, height of the request, in guest pixels) in
  // host pixels. Its whole guest edges map as the scissor's and the surfaces'
  // do, rounded at a fractional scale, so the viewport covers exactly the host
  // rows and columns of the guest pixels it spans. Scaling the extent instead
  // gives a target one guest row high (two host rows at 1.5x) a viewport 1.5
  // rows high: once the half-pixel offset moves the geometry, no host pixel
  // center is left inside, and a post-processing ramp drawn there stays black.
  // Fractional guest edges, and every edge at an integer scale, map as before.
  struct Viewport {
    float x = 0, y = 0, width = 0, height = 0;
  };
  Viewport ViewportInHost(const std::array<float, 6>& v) const {
    const float x = ViewportEdge(v[0], ox, sx), y = ViewportEdge(v[1], oy, sy);
    return {x, y, ViewportEdge(v[0] + v[2], ox, sx) - x, ViewportEdge(v[1] + v[3], oy, sy) - y};
  }

 private:
  // Each edge alone, exact at 1x, as ResolutionScale maps it.
  static int32_t Edge(int32_t e, float o, float k) {
    return k == 1.0f && o == 0.0f ? e : int32_t(std::lround(double(o) + double(e) * k));
  }
  static float ViewportEdge(float e, float o, float k) {
    const float whole = std::round(e);
    return e != whole ? o + e * k : float(Edge(int32_t(whole), o, k));
  }
};

// The half-pixel offset gap of a scaled resolve. The title draws with the
// D3D9 pixel center (PA_SU_VTX_CNTL.pix_center, guarded in draw.cpp), so
// g_HalfPixelOffset moves its geometry by half a guest pixel: a quad that
// starts on the guest edge 0 leaves the first host columns and rows uncovered
// above 1x (one at 2x), and what was drawn before shows through at the left
// and top of the image (fades and overlays). As the SDK's resolve with
// resolve_resolution_scale_fill_half_pixel_offset, those columns and rows of
// the resolved rectangle repeat the first surely covered one: (k + 1) / 2 at
// an integer scale, the pixels whose centers lie before k / 2 at a fraction.
inline uint32_t HalfPixelGap(float k) { return k > 1.0f ? uint32_t(std::ceil(k * 0.5f - 0.5f + 1e-4f)) : 0; }

// The copies of box to (dest_x, dest_y), with a given top/left gap filled:
// the covered part, the gap rows from the first covered row, the gap columns
// from the first covered column, the corner from the first covered pixel. No
// copy reads a gap pixel and no two write the same texel, so they need no
// order. One copy of the whole box with no gap or when the box is not larger
// than its gap.
struct GapCopy {
  HostBox source;
  uint32_t dest_x = 0, dest_y = 0;
};
inline std::vector<GapCopy> EdgeCopies(uint32_t gx, uint32_t gy, const HostBox& box, uint32_t dest_x,
                                       uint32_t dest_y) {
  if ((!gx && !gy) || box.right - box.left <= gx || box.bottom - box.top <= gy) return {{box, dest_x, dest_y}};
  const uint32_t x = box.left + gx, y = box.top + gy;
  std::vector<GapCopy> copies{{{x, y, box.right, box.bottom}, dest_x + gx, dest_y + gy}};
  for (uint32_t i = 0; i < gy; ++i) copies.push_back({{x, y, box.right, y + 1}, dest_x + gx, dest_y + i});
  for (uint32_t j = 0; j < gx; ++j) copies.push_back({{x, y, x + 1, box.bottom}, dest_x + j, dest_y + gy});
  for (uint32_t i = 0; i < gy; ++i)
    for (uint32_t j = 0; j < gx; ++j) copies.push_back({{x, y, x + 1, y + 1}, dest_x + j, dest_y + i});
  return copies;
}

inline std::vector<GapCopy> ResolveCopies(const ResolutionScale& k, const HostBox& box, uint32_t dest_x,
                                          uint32_t dest_y) {
  return EdgeCopies(HalfPixelGap(k.x), HalfPixelGap(k.y), box, dest_x, dest_y);
}

// The image composited over the title's frame can keep a partly blended edge
// once the half-pixel resolve gap is filled: at 2x, rows and columns 0 and 1
// hold the fringe. The Present's copy extends the first full guest pixel
// (ceil(k) host pixels) over it, before FXAA and sharpening can spread it. The
// guest's render targets and the 1x output stay unchanged; nothing is rescaled
// or shifted.
inline std::vector<GapCopy> PresentCopies(const ResolutionScale& k, uint32_t width, uint32_t height) {
  const uint32_t gx = k.x > 1.0f ? uint32_t(std::ceil(k.x)) : 0;
  const uint32_t gy = k.y > 1.0f ? uint32_t(std::ceil(k.y)) : 0;
  return EdgeCopies(gx, gy, {0, 0, width, height}, 0, 0);
}

}  // namespace gpu
