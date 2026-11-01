// The settings restart outside Windows (src/launch_arguments.h,
// StartAfterExit): the new process waits until the old one lets go of the
// pipe, gets its arguments unchanged, and inherits none of the old process's
// other descriptors.
// Usage: broken_bond_launch_restart_test <scratch dir>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "launch_arguments.h"

namespace fs = std::filesystem;
using namespace std::chrono_literals;
namespace {
unsigned checks = 0;
void Check(bool value, const std::string& label) {
  ++checks;
  if (!value)
    throw std::runtime_error(label);
}

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

// The child's shell renames its output into place, so a file seen is complete.
bool WaitFor(const fs::path& path, std::chrono::milliseconds limit) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < end) {
    if (fs::exists(path))
      return true;
    std::this_thread::sleep_for(20ms);
  }
  return false;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2)
      throw std::runtime_error("Pass a scratch directory");
    const fs::path dir = fs::absolute(argv[1]);
    fs::remove_all(dir);
    fs::create_directories(dir);

    // sh -c <script> $0 $1 $2: the script writes $0 and $1, one per line, to $2.
    const auto arguments = dir / "arguments.txt";
    const std::string write_arguments = "printf '%s\\n' \"$0\" \"$1\" > \"$2.tmp\" && mv \"$2.tmp\" \"$2\"";
    const int wait_fd = broken_bond::launch::StartAfterExit(
        "/bin/sh", {"-c", write_arguments, "--setup", "--user_data_root=/home/a b/Jos\xC3\xA9 'q'", arguments.string()});
    Check(wait_fd >= 0, "the new process starts");
    std::this_thread::sleep_for(300ms);
    Check(!fs::exists(arguments), "it waits while the old process holds the pipe");
    close(wait_fd);
    Check(WaitFor(arguments, 5000ms), "it runs once the pipe closes");
    Check(Read(arguments) == "--setup\n--user_data_root=/home/a b/Jos\xC3\xA9 'q'\n", "its arguments arrive unchanged");

    // A descriptor of the old process without close-on-exec (a log file, say).
    const int held = open((dir / "held.log").c_str(), O_CREAT | O_WRONLY, 0644);
    Check(held >= 0, "a descriptor to leave behind");
    const auto descriptor = dir / "descriptor.txt";
    const std::string probe = "if [ -e /proc/$$/fd/$1 ]; then r=inherited; else r=closed; fi; "
                              "printf '%s' \"$r\" > \"$0.tmp\" && mv \"$0.tmp\" \"$0\"";
    const int probe_fd = broken_bond::launch::StartAfterExit(
        "/bin/sh", {"-c", probe, descriptor.string(), std::to_string(held)});
    Check(probe_fd >= 0, "the probe starts");
    close(probe_fd);
    Check(WaitFor(descriptor, 5000ms), "the probe runs");
    Check(Read(descriptor) == "closed", "the old process's descriptors close at the exec");
    close(held);

    Check(broken_bond::launch::StartAfterExit(dir / "missing", {}) < 0, "a missing executable is refused");
    while (waitpid(-1, nullptr, 0) > 0) {
    }
    std::cout << "launch_restart PASS checks=" << checks << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "launch_restart FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
