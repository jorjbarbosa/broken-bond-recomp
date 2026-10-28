// The DLC content operations (menu/content_manager.h) honor the game's user
// data root, without touching Documents: loose installation, package staging,
// package names, and the installed and pending states across processes.
#include "menu/content_manager.h"
#include <rex/filesystem/devices/stfs_xbox.h>
#include <rex/system/xam/content_manager.h>
#include <algorithm>
#include <fstream>
#include <iostream>

namespace {
constexpr char kPackage[] = "3D56F90400DE6FBCED7852DF085F04AD75728F8F55";
constexpr char kLongPackage[] = "123456789012345678901234567890123456789012-overflow";
constexpr char kShortPackage[] = "123456789012345678901234567890123456789012";

void WritePackage(const std::filesystem::path& path, std::u16string_view name) {
  rex::filesystem::StfsHeader header{};
  header.header.magic = rex::filesystem::XContentPackageType::kLive;
  header.metadata.set_display_name(rex::system::XLanguage::kEnglish, name);
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(&header), sizeof(header));
}

void WriteInstalledHeader(const std::filesystem::path& root, const std::string& filename,
                          std::u16string_view name, bool complete = true) {
  const auto headers = root.parent_path() / "Headers/00000002";
  std::filesystem::create_directories(headers);
  rex::system::xam::XCONTENT_AGGREGATE_DATA header{};
  header.content_type = rex::system::XContentType::kMarketplaceContent;
  header.title_id = 0x55530825;
  header.set_file_name(filename);
  header.set_display_name(name);
  std::ofstream(headers / (filename + ".header"), std::ios::binary)
      .write(reinterpret_cast<const char*>(&header), complete ? sizeof(header) : 264);
}

bool CheckInstalled(const std::filesystem::path& staging) {
  const auto installed = launcher_content::ListInstalledDlc();
  const auto named = [&](const char* filename, const char* name) {
    return std::any_of(installed.begin(), installed.end(), [&](const auto& item) {
      return item.path.filename() == filename && item.name == name;
    });
  };
  const auto pending = launcher_content::ListPendingDlc(staging);
  return installed.size() == 3 && named(kPackage, "Chunin outfit Sasuke") &&
         named(kShortPackage, "Roupa \xc3\xa0 venda \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \xf0\x9f\x8e\xae") &&
         pending.size() == 1 && pending[0].name == "addon";
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) return 2;
  const auto base = std::filesystem::absolute(argv[1]);
  const auto staging = base / "product/dlc";
  launcher_content::SetUserDataRoot(base / "user");
  if (argc == 3) {
    if (std::string(argv[2]) != "--reopened" || !CheckInstalled(staging)) return 16;
    std::cout << "PASS: names and installed/pending state survive a new process\n";
    return 0;
  }
  std::error_code ignored;
  std::filesystem::remove_all(base, ignored);  // Each run starts from an empty root.
  std::filesystem::create_directories(base / "loose/addon");
  const auto expected = base / "user/0000000000000000/55530825/00000002";
  if (launcher_content::GetDlcRoot() != expected) return 3;
  std::string error;
  if (!launcher_content::EnsureDlcFolders(error)) return 4;
  std::ofstream(base / "loose/addon/content.bin") << "test content";
  if (launcher_content::InstallDlc(base / "loose", staging, error) != 0) return 5;
  if (!std::filesystem::exists(expected / "loose/addon/content.bin")) return 6;
  const auto installed = launcher_content::ListInstalledDlc();
  if (installed.size() != 1 || installed[0].path != expected / "loose") return 7;
  std::filesystem::create_directories(base / "packages");
  std::string header(0x344, '\0'); header.replace(0, 4, "LIVE");
  std::ofstream(base / "packages/addon", std::ios::binary).write(header.data(), header.size());
  WritePackage(base / "packages" / kPackage, u"Chunin outfit Sasuke");
  WritePackage(base / "packages" / kLongPackage, u"Roupa à venda 日本語 \U0001f3ae");
  const auto packages = launcher_content::ScanStfsPackages(base / "packages");
  if (packages.size() != 3 || std::none_of(packages.begin(), packages.end(), [](const auto& item) {
        return item.path.filename() == kPackage && item.name == "Chunin outfit Sasuke";
      })) return 9;
  if (launcher_content::InstallDlc(base / "packages", staging, error) != 3 ||
      !std::filesystem::exists(staging / "addon")) return 8;
  if (launcher_content::ListPendingDlc(staging).size() != 3) return 10;
  // An extracted folder alone, or a truncated header, must remain pending.
  std::filesystem::create_directories(expected / kPackage);
  std::ofstream(expected / kPackage / "content.bin") << "extracted content";
  if (launcher_content::ListPendingDlc(staging).size() != 3) return 11;
  WriteInstalledHeader(expected, kPackage, u"Chunin outfit Sasuke", false);
  if (launcher_content::ListPendingDlc(staging).size() != 3) return 12;
  WriteInstalledHeader(expected, kPackage, u"Chunin outfit Sasuke");
  // Matching display names do not identify packages: addon has a header but
  // no extracted folder and must still be offered for installation.
  WriteInstalledHeader(expected, "addon", u"Chunin outfit Sasuke");
  if (launcher_content::ListPendingDlc(staging).size() != 2) return 13;
  // A package name longer than 42 bytes is extracted into a truncated folder.
  std::filesystem::create_directories(expected / kShortPackage);
  std::ofstream(expected / kShortPackage / "content.bin") << "extracted outfit";
  WriteInstalledHeader(expected, kShortPackage, u"Roupa à venda 日本語 \U0001f3ae");
  if (!CheckInstalled(staging)) return 14;
  launcher_content::SetUserDataRoot(base / "other-user");
  if (launcher_content::ListPendingDlc(staging).size() != 3) return 15;
  launcher_content::SetUserDataRoot(base / "user");
  std::cout << "PASS: effective user root, loose installation, package names, pending/installed state, long IDs, UTF-16 names\n";
  return 0;
}
