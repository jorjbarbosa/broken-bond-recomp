// Host test of the precompile pool's threads (Dispatcher::StartPool) against
// what the title is doing (common/title_loading.h). Never enters a guest
// function. On a loading screen every pool thread runs above normal priority;
// with the title's menus loaded last every thread runs at the lowest; in a
// world of the game only kPoolThreadsInGame threads run, at the lowest, and the
// others wait until Shutdown stops the pool.
// Usage: native_engine_pool_test <new output.jsonl>
#include "gpu/backend/dispatcher.h"
#include "gpu/backend/tests/test_backend.h"
#include "gpu/common/title_loading.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <thread>

using namespace gpu;

namespace gpu::sampler {
void RegisterThread(const char*) {}  // The sampler is not linked here.
}  // namespace gpu::sampler

namespace {
constexpr size_t kItems = 120;
constexpr unsigned kThreads = 6;

struct Observed {
  std::mutex mutex;
  std::set<DWORD> threads;
  std::set<int> priorities;
  std::atomic<size_t> done{0};
  std::atomic<int> finished{-1};  // done(stopped): 0 or 1 once the pool ends.
};

std::string Json(Observed& o) {
  std::lock_guard lock(o.mutex);
  std::string priorities;
  for (int p : o.priorities) priorities += (priorities.empty() ? "" : ",") + std::to_string(p);
  return "{\"threads\":" + std::to_string(o.threads.size()) + ",\"priorities\":[" + priorities +
         "],\"items\":" + std::to_string(o.done.load()) + ",\"finished\":" + std::to_string(o.finished.load()) + "}";
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 || std::filesystem::exists(argv[1])) return 2;
  std::ofstream output(argv[1]);
  if (!output) return 3;
  bool passed = true;
  auto check = [&](const std::string& kind, bool ok, const std::string& evidence) {
    passed &= ok;
    output << "{\"kind\":\"" << kind << "\",\"ok\":" << (ok ? "true" : "false")
           << ",\"evidence\":" << (evidence.empty() ? "null" : evidence) << "}\n";
    output.flush();
  };

  // One pool per dispatcher: each state gets its own. Every item records its
  // thread and the priority the pool gave it.
  auto run = [&](const std::string& name, bool loading, uint32_t world, size_t expected_threads,
                 int expected_priority) {
    title_loading.store(loading);
    world_key.store(world);
    std::string evidence;
    Dispatcher dispatcher(true);
    if (!dispatcher.Initialize(evidence, TestRenderMode())) {
      check(name + "-initialize", false, evidence);
      return;
    }
    Observed observed;
    const bool started = dispatcher.StartPool(kItems, kThreads, [&](size_t, Engine&) {
      {
        std::lock_guard lock(observed.mutex);
        observed.threads.insert(GetCurrentThreadId());
        observed.priorities.insert(GetThreadPriority(GetCurrentThread()));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      ++observed.done;
    }, [&](bool stopped) { observed.finished = stopped ? 1 : 0; });
    check(name + "-started", started, "{}");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (observed.done.load() < kItems && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // A pool whose threads all ran ends alone; the waiting threads of a game
    // world end only with the stop.
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (observed.finished.load() < 0 && std::chrono::steady_clock::now() < until)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const int before_shutdown = observed.finished.load();
    const std::string seen = Json(observed);
    bool ok = observed.done.load() == kItems;
    {
      std::lock_guard lock(observed.mutex);
      ok = ok && observed.threads.size() == expected_threads && observed.priorities.size() == 1 &&
           *observed.priorities.begin() == expected_priority;
    }
    check(name, ok, seen);
    const bool waits = expected_threads < kThreads;
    check(name + "-end", waits ? before_shutdown == -1 : before_shutdown == 0, seen);
    check(name + "-shutdown", dispatcher.Shutdown(evidence), evidence);
    if (waits) check(name + "-stopped", observed.finished.load() == 1, Json(observed));
  };

  run("loading-screen", true, 0x12345678, kThreads, THREAD_PRIORITY_ABOVE_NORMAL);
  run("title-menu", false, kTitleMenuWorldKey, kThreads, THREAD_PRIORITY_LOWEST);
  run("game-world", false, 0x1B0600FA, Dispatcher::kPoolThreadsInGame, THREAD_PRIORITY_LOWEST);
  title_loading.store(false);
  world_key.store(0);
  output << "{\"kind\":\"verdict\",\"passed\":" << (passed ? "true" : "false") << ",\"guest_executed\":false}\n";
  return passed && output.good() ? 0 : 1;
}
