#pragma once

#include <filesystem>
#include <string>

namespace launcher_ui {

// A path as UTF-8 text, for display.
std::string PathText(const std::filesystem::path& path);

// The file:// URI of a local path, percent-encoded (OpenFolder outside Windows).
std::string FileUrl(const std::filesystem::path& folder);

// Opens a folder in the system file manager.
void OpenFolder(const std::filesystem::path& folder);

}  // namespace launcher_ui
