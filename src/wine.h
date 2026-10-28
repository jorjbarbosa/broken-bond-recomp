#pragma once

// Whether the process runs under Wine (Proton, or Wine on another system):
// Wine's ntdll exports wine_get_version, Windows' does not. There the native
// renderer runs on Vulkan only (gpu/settings.cpp), and the launcher offers and
// saves Vulkan (src/menu/config_manager.h).

namespace broken_bond {
#ifdef _WIN32
// Wine's version ("9.0", ...), or null on Windows.
const char* WineVersion();
#else
// A native build runs without Wine.
inline const char* WineVersion() { return nullptr; }
#endif
}  // namespace broken_bond
