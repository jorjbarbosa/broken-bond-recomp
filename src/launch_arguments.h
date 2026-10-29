// Process arguments shared by shell links and process restarts: the session's
// storage arguments, Windows argv quoting, and on POSIX a start that waits for
// this process to exit.
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include "data_paths.h"
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace broken_bond::launch {
inline std::wstring QuoteArgument(std::wstring_view value) {
  std::wstring result(1, L'"');
  size_t slashes = 0;
  for (const auto c : value) {
    if (c == L'\\') { ++slashes; continue; }
    result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    result += c;
    slashes = 0;
  }
  result.append(slashes * 2, L'\\');
  result += L'"';
  return result;
}

inline bool Explicit(data::Source source) {
  return source == data::Source::kCommandLine || source == data::Source::kEnvironment;
}
inline std::string Utf8(const std::filesystem::path& path) {
  const auto value = path.u8string(); return {value.begin(), value.end()};
}
// Only the locations the command line or the environment chose: the next
// start resolves the saved selection, the .toml and the defaults by itself.
inline std::vector<std::string> StorageArguments(const data::DataPaths& paths) {
  std::vector<std::string> result;
  const auto add = [&](const char* flag, const std::filesystem::path& root, data::Source source) {
    if (Explicit(source)) result.push_back(std::string("--") + flag + "=" + Utf8(root));
  };
  add("data_root", paths.data_root, paths.data_source);
  add("user_data_root", paths.user_data_root, paths.user_source);
  add("cache_root", paths.cache_root, paths.cache_source);
  return result;
}
#ifdef _WIN32
inline std::wstring CommandLine(const std::vector<std::string>& arguments) {
  std::wstring result;
  for (const auto& argument : arguments) {
    if (!result.empty()) result += L" ";
    result += QuoteArgument(std::filesystem::path(std::u8string(argument.begin(), argument.end())).wstring());
  }
  return result;
}
#endif

#ifndef _WIN32
// Starts executable with arguments (argv[0] is the executable) once this
// process has exited, as Windows' restart_wait_pid does: the child blocks on a
// pipe whose write end only this process keeps, and the exit closes it.
// Returns that write end, to keep open until the exit, or -1.
inline int StartAfterExit(const std::filesystem::path& executable, std::vector<std::string> arguments) {
  if (access(executable.c_str(), X_OK) != 0) return -1;
  std::vector<std::string> values{executable.string()};
  values.insert(values.end(), std::make_move_iterator(arguments.begin()), std::make_move_iterator(arguments.end()));
  std::vector<char*> argv;
  for (auto& value : values) argv.push_back(value.data());
  argv.push_back(nullptr);
  int wait_pipe[2];
  if (pipe2(wait_pipe, O_CLOEXEC) != 0) return -1;
  const pid_t child = fork();
  if (child == 0) {
    // Only async-signal-safe calls until execv. The other descriptors this
    // process holds (logs, devices) close at the exec instead of leaking.
    close(wait_pipe[1]);
    close_range(3, ~0U, CLOSE_RANGE_CLOEXEC);
    char byte;
    while (read(wait_pipe[0], &byte, 1) < 0 && errno == EINTR) {}
    execv(argv[0], argv.data());
    _exit(127);
  }
  close(wait_pipe[0]);
  if (child < 0) {
    close(wait_pipe[1]);
    return -1;
  }
  return wait_pipe[1];
}
#endif
}  // namespace broken_bond::launch
