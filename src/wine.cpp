// Wine detection (wine.h), shared by the game and the launcher.
#include "wine.h"
#include <windows.h>

namespace broken_bond {
const char* WineVersion() {
  static const char* const version = []() -> const char* {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    using GetVersion = const char*(__cdecl*)();
    const auto get = ntdll ? reinterpret_cast<GetVersion>(GetProcAddress(ntdll, "wine_get_version")) : nullptr;
    return get ? get() : nullptr;
  }();
  return version;
}
}  // namespace broken_bond
