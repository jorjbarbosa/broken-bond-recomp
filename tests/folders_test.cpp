// The file:// URIs of OpenFolder outside Windows (src/menu/folders.h): an
// absolute POSIX path, a drive letter and, on Windows, a network path,
// percent-encoded.

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "menu/folders.h"

namespace {
unsigned checks = 0;
void Check(const std::string& actual, const std::string& expected) {
  ++checks;
  if (actual != expected)
    throw std::runtime_error("expected " + expected + ", got " + actual);
}
}  // namespace

int main() {
  try {
    using launcher_ui::FileUrl;
    using std::filesystem::path;
    Check(FileUrl(path(u8"/home/user/.local/share/broken_bond/dlc")),
          "file:///home/user/.local/share/broken_bond/dlc");
    Check(FileUrl(path(u8"/home/José/My DLC#1")), "file:///home/Jos%C3%A9/My%20DLC%231");
    Check(FileUrl(path(u8"C:/Users/José/dlc")), "file:///C:/Users/Jos%C3%A9/dlc");
#ifdef _WIN32
    // A network root name exists on Windows only (POSIX's generic form folds //).
    Check(FileUrl(path(u8"//server/share/dlc")), "file://server/share/dlc");
    Check(FileUrl(path(LR"(C:\Games\Broken Bond\user)")), "file:///C:/Games/Broken%20Bond/user");
#endif
    std::cout << "menu_folders PASS checks=" << checks << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "menu_folders FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
