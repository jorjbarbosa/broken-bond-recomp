// broken_bond - ReXGlue Recompiled Project
//
// Local incidents: the executable supervises the game as its child process
// (supervisor.cpp), and each session keeps its events, stderr, exit status,
// classification and crash dumps in its own folder under <data>/incidents.
// The game publishes through incident.cpp; export.cpp packs a session.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace broken_bond::incident {
enum class Role : uint32_t { kUi, kGuest, kTranslator, kCompiler, kGpu, kCount };
// These calls never wait on an application mutex. Events are bounded; overflow
// and truncation are exposed in the manifest. Progress is allocation-free.
void Event(std::string_view operation, std::string_view state, std::string_view detail = {}, uint64_t operation_id = 0) noexcept;
uint64_t BeginOperation(std::string_view operation, std::string_view detail = {}) noexcept;
void Progress(Role role) noexcept;
void Active(Role role, bool active) noexcept;
class Activity {
 public:
  explicit Activity(Role role) : role_(role) { Active(role_, true); }
  ~Activity() { Progress(role_); Active(role_, false); }
  Activity(const Activity&) = delete;
  Activity& operator=(const Activity&) = delete;
 private:
  Role role_;
};
void FirstFailure(std::string_view operation, std::string_view evidence = {}) noexcept;
void ExpectedExit(std::string_view reason) noexcept;
void Collector(bool ready, std::string_view reason) noexcept;
bool CollectorReady() noexcept;
void SetMetadata(std::string_view key, std::string_view value) noexcept;
void SetLog(const std::filesystem::path& path) noexcept;
void RequestSnapshot() noexcept;
// Whether a live thread snapshot can be taken: on Linux it needs /usr/bin/gdb.
bool LiveSnapshotAvailable();
std::filesystem::path Directory();
std::filesystem::path ReportsDirectory();
std::string SessionId();
// The newest completed session other than this one; failures_only skips the
// normal exits, unseen_only also a failure Acknowledge already recorded.
std::filesystem::path Latest(bool failures_only = false, bool unseen_only = false);
// One line about a completed session: classification, exit status, whether an
// exception dump was captured, and the guest fault's return chain and heap.
std::string Summary(const std::filesystem::path& session);
void Acknowledge(const std::filesystem::path& session) noexcept;
bool VerifyIncident(const std::string& mode);
// A fixed snapshot of the published guest state. Safe to call from the SDK
// fault chain: no formatter, allocation, lock or logger.
void GuestFault(uint64_t pc, uint64_t address, uint32_t kind, uint32_t thread,
                uint64_t base, const std::array<uint64_t, 32>& gpr,
                uint64_t lr, uint64_t ctr, uint32_t access_operation = 0) noexcept;
// The guest fault's return chain and the title's heap at the fault, as text
// the crash report wrote into its own stack buffer.
void GuestStack(std::string_view text) noexcept;
void GuestHeap(std::string_view text) noexcept;
struct SupervisorOptions {
  // The repository a build inside it belongs to (data_paths.h's Environment).
  std::filesystem::path repository_dir;
  // A message to the player when local diagnostics cannot start.
  std::function<void(const std::string&)> warn;
};
// Parent stays outside SDL/runtime. Child uses the unchanged SDK app lifecycle.
int RunSupervised(std::vector<std::string> arguments, const std::filesystem::path& executable,
                  const SupervisorOptions& options,
                  const std::function<int(std::vector<std::string>&)>& run);
// ZIP export operates on a completed, immutable session; no private game data.
bool Export(const std::filesystem::path& session, const std::filesystem::path& destination,
            std::string& error);
void Prune(const std::filesystem::path& reports);
}  // namespace broken_bond::incident
