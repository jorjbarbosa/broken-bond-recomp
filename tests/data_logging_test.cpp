// The SDK's configuration parser and log sinks under non-ASCII paths
// (patches/rexglue-data-log-directory.patch), one scenario per process: the
// log folder the application names, an explicit log_file, and the default
// beside the working folder. Never touches a real configuration or data folder.
// Usage: broken_bond_data_logging_test <new fixture folder> sequential|explicit|fallback

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(data_storage_probe, 0, "Test", "Isolated config probe");

namespace fs = std::filesystem;
namespace {
void Check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
std::string Read(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  Check(static_cast<bool>(input), "expected log/config file missing");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void Write(const fs::path& path, std::string_view text) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  Check(static_cast<bool>(output), "fixture write failed");
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  try {
    const auto root = fs::absolute(rex::to_path(std::string_view(argv[1])));
    const std::string mode(argv[2]);
    Check(!fs::exists(root), "fixture must be new");
    Check(mode == "sequential" || mode == "explicit" || mode == "fallback", "unknown scenario");
    const auto cwd = root / "arbitrary cwd";
    fs::create_directories(cwd);
    fs::current_path(cwd);
    const auto config_path = root / fs::path(u"config 日本語/broken_bond.toml");
    Write(config_path, "data_storage_probe = 41\nlog_level = 'warn'\n[log.levels]\n'storage.test' = 'trace'\n");
    char argument[] = "logging_test";
    char* arguments[] = {argument};
    rex::cvar::Init(1, arguments);
    rex::InitLoggingEarly();
    rex::cvar::LoadConfig(config_path);
    Check(REXCVAR_GET(data_storage_probe) == 41, "SDK failed to load Unicode TOML");
    Check(rex::cvar::GetFlagSource("data_storage_probe") == rex::cvar::Source::kConfig, "config source lost");
    auto categories = rex::ParseCategoryLevelsFromConfig(config_path);
    Check(categories.at("storage.test") == "trace", "category levels not read from Unicode path");
    const auto dir = mode == "fallback" ? cwd / "logs" : root / fs::path(u"data 日本語/logs");
    Write(dir / "broken_bond_004.log", "keep previous\n");
    Write(dir / "legacy/old/broken_bond_999.log", "historical\n");
    const auto explicit_path = root / fs::path(u"selected 日本語.log");
    const auto file = rex::path_to_utf8(explicit_path);
    auto config = rex::BuildLogConfig(mode == "explicit" ? file.c_str() : nullptr,
                                      rex::cvar::GetFlagByName("log_level"), categories);
    config.app_name = "broken_bond";
    if (mode != "fallback") config.log_dir = rex::path_to_utf8(dir);
    rex::InitLogging(config);
    auto category = rex::RegisterLogCategory("storage.test");
    rex::GetLoggerRaw(category)->trace("category trace retained");
    rex::GetLogger()->debug("filtered root debug");
    rex::GetLogger()->warn("root warning retained");
    rex::FlushLogging();
    const auto expected = mode == "explicit" ? explicit_path : dir / "broken_bond_005.log";
    const auto text = Read(expected);
    Check(text.find("category trace retained") != std::string::npos, "category level changed");
    Check(text.find("root warning retained") != std::string::npos, "global level changed");
    Check(text.find("filtered root debug") == std::string::npos, "global debug was not filtered");
    Check(Read(dir / "broken_bond_004.log") == "keep previous\n", "previous sequential log overwritten");
    Check(Read(dir / "legacy/old/broken_bond_999.log") == "historical\n", "historical log changed");
    if (mode == "explicit") Check(!fs::exists(dir / "broken_bond_005.log"), "explicit log_file did not win");
    if (mode != "fallback") Check(!fs::exists(cwd / "logs"), "unexpected cwd logs");
    rex::ShutdownLogging();
    std::cout << "data_logging PASS: Unicode TOML, " << mode << ", levels and categories, historical logs\n";
  } catch (const std::exception& error) {
    std::cerr << "data_logging FAIL: " << error.what() << '\n';
    return 1;
  }
}
