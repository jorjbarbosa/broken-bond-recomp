// The title's screen layer (screen_layer.h).
//
// Every HUD, menu, subtitle and loading screen draw of the title comes from
// sub_821700D8, the draw function of its Flash UI material (material type 6):
// its vertex shaders read g_vFlashColorMul and g_vFlashTexTransformU/V and
// place each vertex with g_mWorldViewProj (c0..c3), an orthographic matrix
// set per draw, over a 1280 x 720 viewport without depth. Where those draws
// land varies: into the final target after the color grading in the menus,
// the open world, the pause and the cutscenes, into the scene target before
// the post-processing in a fight. The videos come from sub_8216EF18 (its
// textures: Y 1280 x 720, U and V 640 x 360). Each function issues one draw
// through the dispatcher sub_82169930 on the calling thread, so a thread flag
// around the original marks the draws the capture takes (draw.cpp). Both are
// reached through tables only; the hook replaces the entry the tables name.
#include "gpu/d3d/screen_layer.h"
#include <rex/hook.h>
#include <rex/ppc/context.h>

namespace gpu::screen_layer {
namespace {
thread_local Kind current = Kind::kNone;
struct Scope {
  Kind previous;
  explicit Scope(Kind kind) : previous(current) { current = kind; }
  ~Scope() { current = previous; }
};
}  // namespace

Kind Current() { return current; }
}  // namespace gpu::screen_layer

extern "C" void __imp__sub_821700D8(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821700D8) {
  const gpu::screen_layer::Scope scope(gpu::screen_layer::Kind::kFlash);
  __imp__sub_821700D8(ctx, base);
}

extern "C" void __imp__sub_8216EF18(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_8216EF18) {
  const gpu::screen_layer::Scope scope(gpu::screen_layer::Kind::kVideo);
  __imp__sub_8216EF18(ctx, base);
}
