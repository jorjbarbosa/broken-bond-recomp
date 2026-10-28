// The version of the port (port_version.h).
#include "port_version.h"
#include "gpu/common/platform.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace broken_bond {
namespace {
bool IsVersion(const std::string& text) {
  return !text.empty() && text.size() <= 64 &&
         std::all_of(text.begin(), text.end(), [](unsigned char c) {
           return std::isalnum(c) || c == '.' || c == '-' || c == '+';
         });
}
}  // namespace

std::string PortVersion() {
  static const std::string version = [] {
    std::ifstream file(gpu::platform::ExecutablePath().parent_path() / "broken_bond.version");
    std::string line;
    if (file && std::getline(file, line)) {
      const auto first = line.find_first_not_of(" \t\r\n");
      const auto last = line.find_last_not_of(" \t\r\n");
      if (first != std::string::npos) line = line.substr(first, last - first + 1);
      if (IsVersion(line)) return line;
    }
#ifdef BROKENBOND_VERSION
    return std::string(BROKENBOND_VERSION);
#else
    return std::string("dev");
#endif
  }();
  return version;
}

std::string WindowTitle() {
  const std::string version = PortVersion();
  const bool numbered = !version.empty() && std::isdigit(static_cast<unsigned char>(version[0]));
  return "Naruto: The Broken Bond - " + (numbered ? "v" + version : version);
}
}  // namespace broken_bond
