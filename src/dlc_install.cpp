// DLC installation at start (dlc_install.h). Packages are installed by the
// SDK's ContentManager::InstallContent, which extracts them into
// <user_root>/0000000000000000/55530825/00000002/<file name> and writes the
// content header with the license of the package.
#include "dlc_install.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/xam/content_manager.h>

REXCVAR_DEFINE_STRING(dlc_source_path, "dlc", "Content",
                      "Folder scanned for DLC packages to install at start, relative to the "
                      "executable. If empty or missing, the game runs without new DLC.");

namespace broken_bond::dlc {
namespace {

constexpr uint32_t kTitleId = 0x55530825;
constexpr char kTitleIdText[] = "55530825";
constexpr char kCommonXuid[] = "0000000000000000";
constexpr char kMarketplaceContent[] = "00000002";
// XCONTENT_DATA holds file names of up to 42 bytes.
constexpr size_t kMaxFileName = 42;

bool IsHexString(std::string_view s) {
  return !s.empty() && s.find_first_not_of("0123456789abcdefABCDEF") == std::string_view::npos;
}

bool IsStfsFile(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return false;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size < 0x344) return false;

  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::uint32_t magic = 0;
  file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  // Packages start with the ASCII magic 'CON ', 'LIVE' or 'PIRS'. Those
  // bytes are big-endian in the file, so a native (little-endian) uint32
  // read yields the byte-swapped values below.
  return magic == 0x204E4F43u || magic == 0x4556494Cu || magic == 0x53524950u;
}

// STFS package files inside a directory tree.
std::vector<std::filesystem::path> CollectStfsFiles(const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(
           dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
    std::error_code entry_ec;
    if (entry.is_regular_file(entry_ec) && IsStfsFile(entry.path())) files.push_back(entry.path());
  }
  return files;
}

// Mirrors already-extracted (loose) content into the user data tree. STFS
// packages are skipped: they are installed from their source location with
// InstallContent, which derives the destination folder from the package file
// name (a package copied here could collide with its own extraction).
bool MirrorDirectory(const std::filesystem::path& source, const std::filesystem::path& destination) {
  std::error_code ec;
  std::filesystem::create_directories(destination, ec);
  bool ok = true;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(
           source, std::filesystem::directory_options::skip_permission_denied, ec)) {
    std::error_code entry_ec;
    if (!entry.is_regular_file(entry_ec) || IsStfsFile(entry.path())) continue;
    const auto rel = std::filesystem::relative(entry.path(), source, entry_ec);
    if (entry_ec) {
      ok = false;
      continue;
    }
    const auto dst = destination / rel;
    std::filesystem::create_directories(dst.parent_path(), entry_ec);
    std::filesystem::copy_file(entry.path(), dst, std::filesystem::copy_options::update_existing,
                               entry_ec);
    if (entry_ec) ok = false;
  }
  return ok;
}

// The .header the ContentManager keeps for a marketplace package, given the
// package root (<user_root>/<xuid>/<title>/00000002).
std::filesystem::path HeaderPath(const std::filesystem::path& dlc_root, std::string_view file_name) {
  return dlc_root.parent_path() / "Headers" / kMarketplaceContent / (std::string(file_name) + ".header");
}

bool Installed(rex::system::xam::ContentManager* content_manager, const std::string& file_name) {
  rex::system::xam::XCONTENT_AGGREGATE_DATA probe{};
  probe.content_type = rex::system::XContentType::kMarketplaceContent;
  probe.title_id = kTitleId;
  probe.set_file_name(file_name);
  rex::system::xam::XCONTENT_AGGREGATE_DATA header{};
  return content_manager->ContentExists(0, probe) &&
         XSUCCEEDED(content_manager->ReadContentHeaderFile(
             file_name, 0, kTitleId, rex::system::XContentType::kMarketplaceContent, header));
}

// Phase 1: the packages of the source folder.
//   * Folders following the Xbox 360 content layout (named after a Title ID
//     or a XUID) have their loose files mirrored into the user data tree,
//     while any STFS packages inside are installed from their source location.
//   * Other entries are scanned recursively for STFS packages.
// A package counts as installed when both its extracted folder and its header
// exist, so this runs once per package and heals a wiped user data tree.
void InstallSource(rex::system::xam::ContentManager* content_manager,
                   const std::filesystem::path& user_root) {
  const auto root = SourceFolder();
  if (root.empty()) {
    REXLOG_INFO("[broken_bond] DLC install disabled (dlc_source_path is empty)");
    return;
  }
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec)) {
    REXLOG_INFO("[broken_bond] DLC folder not found ({}); no new DLC", rex::path_to_utf8(root));
    return;
  }
  const auto content_root = user_root / kCommonXuid;
  std::vector<std::filesystem::path> packages;
  int mirrored_dirs = 0;
  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    const auto filename = rex::path_to_utf8(entry.path().filename());
    if (entry.is_regular_file(ec)) {
      if (IsStfsFile(entry.path())) packages.push_back(entry.path());
      else REXLOG_WARN("[broken_bond] Ignoring non-STFS file in the DLC folder: {}", filename);
      continue;
    }
    if (!entry.is_directory(ec)) continue;
    const auto stfs_files = CollectStfsFiles(entry.path());
    std::filesystem::path mirror;
    if (filename.size() == 8 && IsHexString(filename)) mirror = content_root / filename;   // A Title ID.
    else if (filename.size() == 16 && IsHexString(filename)) mirror = user_root / filename;  // A XUID.
    if (!mirror.empty()) {
      if (MirrorDirectory(entry.path(), mirror)) {
        REXLOG_INFO("[broken_bond] Mirrored DLC content directory: {}", filename);
        ++mirrored_dirs;
      } else {
        REXLOG_WARN("[broken_bond] Failed to mirror DLC directory: {}", filename);
      }
    }
    packages.insert(packages.end(), stfs_files.begin(), stfs_files.end());
  }

  int new_packages = 0;
  for (const auto& package : packages) {
    std::error_code rel_ec;
    auto rel = rex::path_to_utf8(std::filesystem::relative(package, root, rel_ec));
    if (rel.empty()) rel = rex::path_to_utf8(package.filename());
    if (Installed(content_manager, rex::path_to_utf8(package.filename()))) continue;
    REXLOG_INFO("[broken_bond] Installing DLC package: {}", rel);
    const auto result = content_manager->InstallContent(package);
    if (XSUCCEEDED(result)) {
      ++new_packages;
    } else {
      REXLOG_WARN("[broken_bond] DLC install failed for {}: 0x{:08X}", rel, static_cast<uint32_t>(result));
    }
  }
  if (packages.empty() && mirrored_dirs == 0) {
    REXLOG_INFO("[broken_bond] DLC folder empty ({}); no new DLC", rex::path_to_utf8(root));
  } else {
    REXLOG_INFO("[broken_bond] DLC install: {} new package(s), {} mirrored dir(s)", new_packages,
                mirrored_dirs);
  }
}

// Phase 2: the marketplace content already in the user data tree (placed
// there by hand through the DLC page's folder).
//   * A package file in the content root, or folders holding packages, are
//     extracted by the SDK, which writes the header with the real license.
//   * An extracted folder without a header gets one with a full license mask:
//     without it the title would mount the package unlicensed.
int RepairUserTree(rex::system::xam::ContentManager* content_manager,
                   const std::filesystem::path& user_root, const std::filesystem::path& dlc_root) {
  int fixed = 0;
  std::error_code ec;
  // Snapshot the entries first: installing and removing content changes the tree.
  std::vector<std::filesystem::path> entries;
  for (const auto& entry : std::filesystem::directory_iterator(dlc_root, ec)) entries.push_back(entry.path());

  for (const auto& package_path : entries) {
    std::error_code entry_ec;
    const auto package_name = rex::path_to_utf8(package_path.filename());
    if (std::filesystem::is_regular_file(package_path, entry_ec)) {
      // A package directly in the content root. Stage it outside the content
      // tree first: InstallContent would extract it onto its own path.
      if (!IsStfsFile(package_path)) continue;
      const auto staged = user_root / ".stfs_stage" / package_path.filename();
      std::filesystem::create_directories(staged.parent_path(), entry_ec);
      std::filesystem::copy_file(package_path, staged, std::filesystem::copy_options::skip_existing, entry_ec);
      if (entry_ec) {
        REXLOG_WARN("[broken_bond] Failed to stage DLC package '{}': {}", package_name, entry_ec.message());
        continue;
      }
      REXLOG_INFO("[broken_bond] Installing DLC package from the content folder: {}", package_name);
      const auto result = content_manager->InstallContent(staged);
      if (XSUCCEEDED(result)) {
        std::error_code remove_ec;
        std::filesystem::remove(package_path, remove_ec);
        std::filesystem::remove(staged, remove_ec);
        ++fixed;
      } else {
        REXLOG_WARN("[broken_bond] DLC install failed: 0x{:08X}", static_cast<uint32_t>(result));
      }
      continue;
    }
    if (!std::filesystem::is_directory(package_path, entry_ec)) continue;

    // A folder holding packages: let the SDK extract them, then drop the
    // folder once every package inside was extracted (it may hold the only copy).
    std::vector<std::filesystem::path> stfs_files;
    for (const auto& sub : std::filesystem::directory_iterator(package_path, entry_ec))
      if (sub.is_regular_file(entry_ec) && IsStfsFile(sub.path())) stfs_files.push_back(sub.path());
    if (!stfs_files.empty()) {
      bool all_installed = true;
      for (const auto& stfs : stfs_files) {
        REXLOG_INFO("[broken_bond] Installing DLC package: {}", rex::path_to_utf8(stfs.filename()));
        const auto result = content_manager->InstallContent(stfs);
        if (XSUCCEEDED(result)) {
          ++fixed;
        } else {
          REXLOG_WARN("[broken_bond] DLC install failed: 0x{:08X}", static_cast<uint32_t>(result));
          all_installed = false;
        }
      }
      if (all_installed) {
        std::error_code remove_ec;
        std::filesystem::remove_all(package_path, remove_ec);
        std::filesystem::remove(HeaderPath(dlc_root, package_name), remove_ec);
      } else {
        REXLOG_WARN("[broken_bond] Keeping DLC folder '{}' for retry", package_name);
      }
      continue;
    }

    // Loose content: its folder name must fit the header's file name.
    if (package_name.size() > kMaxFileName) {
      REXLOG_WARN("[broken_bond] DLC folder '{}' is longer than {} bytes; the game cannot open it",
                  package_name, kMaxFileName);
      continue;
    }
    // Headers written by InstallContent carry the real license mask.
    rex::system::xam::XCONTENT_AGGREGATE_DATA existing{};
    if (XSUCCEEDED(content_manager->ReadContentHeaderFile(
            package_name, 0, kTitleId, rex::system::XContentType::kMarketplaceContent, existing))) {
      continue;
    }
    rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
    data.device_id = 0;
    data.content_type = rex::system::XContentType::kMarketplaceContent;
    data.title_id = kTitleId;
    data.xuid = 0;
    data.set_display_name(rex::path_to_utf16(package_path.filename()));
    data.set_file_name(package_name);
    const auto result = content_manager->WriteContentHeaderFile(0, data, 0xFFFFFFFF);
    if (XSUCCEEDED(result)) {
      REXLOG_INFO("[broken_bond] Wrote DLC header for '{}'", package_name);
      ++fixed;
    } else {
      REXLOG_WARN("[broken_bond] Failed to write DLC header for '{}': 0x{:08X}", package_name,
                  static_cast<uint32_t>(result));
    }
  }
  return fixed;
}

}  // namespace

std::filesystem::path SourceFolder() {
  const std::string path = REXCVAR_GET(dlc_source_path);
  if (path.empty()) return {};
  return rex::filesystem::GetExecutableFolder() / rex::to_path(path);
}

void InstallAtStart(rex::system::xam::ContentManager* content_manager,
                    const std::filesystem::path& user_root) {
  if (!content_manager) {
    REXLOG_WARN("[broken_bond] DLC install skipped: content manager unavailable");
    return;
  }
  InstallSource(content_manager, user_root);

  const auto dlc_root = user_root / kCommonXuid / kTitleIdText / kMarketplaceContent;
  std::error_code ec;
  if (!std::filesystem::is_directory(dlc_root, ec)) {
    REXLOG_INFO("[broken_bond] No installed DLC content");
    return;
  }
  const int fixed = RepairUserTree(content_manager, user_root, dlc_root);
  const auto listed = content_manager->ListContent(0, 0, rex::system::XContentType::kMarketplaceContent,
                                                   kTitleId);
  REXLOG_INFO("[broken_bond] ContentManager lists {} marketplace package(s); {} repaired", listed.size(),
              fixed);
  for (const auto& item : listed) REXLOG_INFO("[broken_bond]   - file_name='{}'", item.file_name());
}

}  // namespace broken_bond::dlc
