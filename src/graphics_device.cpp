// broken_bond - graphics card choice (graphics_device.h): the option of the
// Xenos host. The native build defines it with the other graphics options
// (gpu/settings.cpp); the Xenos side is in graphics_device_win32.cpp.

#include "graphics_device.h"

#ifndef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
#include <rex/cvar.h>

REXCVAR_DEFINE_STRING(graphics_device, "", "broken_bond/Graphics",
                      "Graphics card by its name as the system lists it (empty = automatic)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
#endif
