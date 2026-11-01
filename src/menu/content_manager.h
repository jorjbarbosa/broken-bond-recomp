#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace launcher_content {
// The game supplies its effective user data root (command line included)
// before any content operation.
void SetUserDataRoot(std::filesystem::path root);

struct ContentInfo {
  std::string name;
  std::filesystem::path path;
  bool is_stfs = false;
};

// Returns the root set by the game; there is no other (the data folder decides it).
std::filesystem::path GetUserDataRoot();

// Returns the DLC content root for this title:
// <user_data_root>/0000000000000000/55530825/00000002
std::filesystem::path GetDlcRoot();

// Ensures the DLC folder hierarchy exists.
bool EnsureDlcFolders(std::string& error);

// Copies loose (already extracted) DLC files into the correct content folder.
bool InstallLooseDlc(const std::filesystem::path& source, std::string& error);

// Installs a DLC folder selected by the user. STFS packages found inside are
// copied into the staging folder; the game installs them on the next launch
// via ContentManager::InstallContent (which writes the content header with
// the license from the package itself). Folders with already-extracted content
// fall back to a direct copy into the user data tree. Returns the number of
// staged STFS packages, or -1 on failure.
int InstallDlc(const std::filesystem::path& source,
               const std::filesystem::path& staging_root,
               std::string& error);

// Lists installed DLC folders (extracted content in the user data tree), named
// by the content header the game wrote when it installed them, else by folder.
std::vector<ContentInfo> ListInstalledDlc();

// Checks whether a file looks like an STFS Xbox 360 package.
bool IsStfsPackage(const std::filesystem::path& path);

// Scans a folder and returns STFS packages found.
// Uses the package's display name when metadata is available.
std::vector<ContentInfo> ScanStfsPackages(const std::filesystem::path& folder);

// Staged packages still awaiting installation in the effective user data root.
// A matching extracted folder and complete runtime header confirm installation.
std::vector<ContentInfo> ListPendingDlc(const std::filesystem::path& folder);

}  // namespace launcher_content
