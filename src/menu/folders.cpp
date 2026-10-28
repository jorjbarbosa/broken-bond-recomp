#include "folders.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <SDL3/SDL.h>
#endif

namespace launcher_ui {

std::string PathText(const std::filesystem::path& path) {
  const auto utf8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

std::string FileUrl(const std::filesystem::path& folder) {
  const auto path = folder.generic_u8string();
  // An absolute POSIX path brings its own slash, a drive letter does not, and a
  // network path (//server/share) names its host.
  std::string url = path.starts_with(u8"//") ? "file:" :
      !path.empty() && path.front() == u8'/' ? "file://" : "file:///";
  // Encode reserved URI bytes so paths with spaces, accents or '#' open correctly.
  constexpr char hex[] = "0123456789ABCDEF";
  for (unsigned char c : path) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '/' || c == ':' || c == '-' || c == '_' || c == '.') {
      url += static_cast<char>(c);
    } else {
      url += '%'; url += hex[c >> 4]; url += hex[c & 15];
    }
  }
  return url;
}

void OpenFolder(const std::filesystem::path& folder) {
#ifdef _WIN32
  // The Windows shell takes the native UTF-16 path; an encoded file URI loses
  // non-ASCII names.
  auto native_path = folder;
  native_path.make_preferred();
  ShellExecuteW(nullptr, L"open", native_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
  SDL_OpenURL(FileUrl(folder).c_str());
#endif
}

}  // namespace launcher_ui
