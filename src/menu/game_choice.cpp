// The Windows picker belongs to the game window.
#include "menu/game_choice.h"
#include "menu/text.h"
#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#endif

namespace broken_bond::menu {
FileChoice ChooseFile(void* owner, bool folder) {
#ifdef _WIN32
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  struct ComScope { bool uninit; ~ComScope() { if (uninit) CoUninitialize(); } } scope{SUCCEEDED(initialized)};
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return {{}, "The file picker could not initialize"};
  Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    return {{}, "The file picker is unavailable"};
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                      (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
  if (!folder) {
    const auto wide = [](const char* text) {
      const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
      std::wstring value(size_t(length), L'\0');
      MultiByteToWideChar(CP_UTF8, 0, text, -1, value.data(), length);
      return value;
    };
    const auto game = wide(Text("game-filter")), all = wide(Text("all-files"));
    const COMDLG_FILTERSPEC filters[] = {{game.c_str(), L"*.iso;default.xex"}, {all.c_str(), L"*.*"}};
    dialog->SetFileTypes(2, filters);
  }
  const HRESULT shown = dialog->Show(static_cast<HWND>(owner));
  if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
  if (FAILED(shown)) return {{}, "The file picker could not open"};
  Microsoft::WRL::ComPtr<IShellItem> item;
  PWSTR name = nullptr;
  if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)))
    return {{}, "The selected path could not be read"};
  std::filesystem::path path(name);
  CoTaskMemFree(name);
  return {std::move(path), {}};
#else
  return {{}, "The system file picker is unavailable on this platform"};
#endif
}
}  // namespace broken_bond::menu
