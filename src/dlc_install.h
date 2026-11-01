// The DLC packages are installed at start, before the title enumerates its
// marketplace content (content type 2): the title mounts each package found
// in the user data tree and reads its files with the game's own.
#pragma once

#include <filesystem>

namespace rex::system::xam {
class ContentManager;
}  // namespace rex::system::xam

namespace broken_bond::dlc {

// Installs the packages of source (the folder of the dlc_source_path option,
// none when empty) that are not installed yet, then repairs the marketplace
// content of the user data tree: packages copied into it are extracted, and
// extracted folders without a header get one.
void InstallAtStart(rex::system::xam::ContentManager* content_manager, const std::filesystem::path& source,
                    const std::filesystem::path& user_root);

}  // namespace broken_bond::dlc
