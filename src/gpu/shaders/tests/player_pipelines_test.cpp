// The player's pipeline list (shaders/player_pipelines.h): merging the
// session's records, writing, reading back, and the files it sets aside. Also
// the skip take's small-target rule (backend/pipeline_set.h).
#include "gpu/shaders/player_pipelines.h"
#include "gpu/backend/pipeline_keys.h"
#include "gpu/backend/pipeline_set.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <fmt/format.h>
#include <fmt/ranges.h>

using namespace gpu;
namespace pp = gpu::player_pipelines;

static_assert(TakeForTarget(DrawRequest::PipelineTake::kSkip, 128, 1) == DrawRequest::PipelineTake::kPrefetch);
static_assert(TakeForTarget(DrawRequest::PipelineTake::kSkip, 1280, 359) == DrawRequest::PipelineTake::kPrefetch);
static_assert(TakeForTarget(DrawRequest::PipelineTake::kSkip, 640, 360) == DrawRequest::PipelineTake::kSkip);
static_assert(TakeForTarget(DrawRequest::PipelineTake::kSkip, 2560, 1440) == DrawRequest::PipelineTake::kSkip);
static_assert(TakeForTarget(DrawRequest::PipelineTake::kPrefetch, 128, 1) == DrawRequest::PipelineTake::kPrefetch);
static_assert(TakeForTarget(DrawRequest::PipelineTake::kSync, 2560, 1440) == DrawRequest::PipelineTake::kSync);

namespace {
bool passed = true;
void Check(const char* name, bool ok, const std::string& detail = {}) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << (detail.empty() ? "" : " " + detail) << "\n";
  passed &= ok;
}
// A record as Engine::PipelineList writes it; wrong_key: a key that does not
// match the content.
std::string Record(const std::string& vs, const std::string& ps, uint32_t blend, bool wrong_key = false,
                   uint32_t world = 0x01002e36) {
  const std::vector<VertexElement> elements{{"POSITION", 0, VertexFormat(7), 0, 0},
                                            {"TEXCOORD", 0, VertexFormat(17), 0, 12}};
  std::array<uint32_t, 21> state{};
  state[0] = blend;
  state[1] = 15;
  state[10] = kTopologyClassTriangle;
  const uint64_t layout = ElementsId(elements);
  const uint64_t key = PipelineKey(vs, ps, layout, state) ^ (wrong_key ? 1 : 0);
  return fmt::format("{{\"key\":\"{:016x}\",\"vs\":\"{}\",\"ps\":\"{}\",\"layout\":\"{:016x}\","
                     "\"elements\":[[\"POSITION\",0,7,0,0],[\"TEXCOORD\",0,17,0,12]],\"state\":[{}],"
                     "\"origin\":\"created\",\"create_us\":120,\"world\":\"{:08x}\"}}",
                     key, vs, ps, layout, fmt::join(state, ","), world);
}
void WriteText(const std::filesystem::path& file, const std::string& text) {
  std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}
}  // namespace

int main() {
  const auto folder = std::filesystem::temp_directory_path() /
      fmt::format("broken-bond-player-pipelines-{}", std::chrono::steady_clock::now().time_since_epoch().count());
  const auto file = folder / "pipelines" / "d3d12.json";
  std::string detail;

  pp::List list;
  const std::string session = "[" + Record("vs-000C5339", "ps-002D8104", 65537) + "," +
                              Record("vs-0011E05C", "ps-002E10CC+spec8", 1) + "," +
                              Record("vs-0011E05C", "depth-only", 1, true) + "]";
  Check("merge-adds", pp::Merge(list, session, detail) == 2 && list.entries.size() == 2, detail);
  Check("merge-again-adds-nothing", pp::Merge(list, session, detail) == 0 && list.entries.size() == 2);
  Check("merge-rejects-garbage", pp::Merge(list, "{not json", detail) == -1);
  Check("merge-entry-fields", list.entries[1].ps == "ps-002E10CC+spec8" && list.entries[1].world == 0x01002e36 &&
        list.entries[0].elements.size() == 2 && list.entries[0].elements[1].semantic == "TEXCOORD" &&
        list.entries[0].state[0] == 65537);

  pp::List read;
  Check("missing", pp::Read(file, "d3d12", read).result == pp::ReadResult::kMissing);
  Check("write", pp::Write(file, "d3d12", list, detail), detail);
  Check("no-temporary-left", !std::filesystem::exists(file.string() + ".tmp"));
  const auto outcome = pp::Read(file, "d3d12", read);
  Check("read-back", outcome.result == pp::ReadResult::kRead && outcome.stale == 0 && read.entries.size() == 2 &&
        read.keys == list.keys, pp::Name(outcome.result));
  Check("other-api-mismatch", pp::Read(file, "vulkan", read).result == pp::ReadResult::kMismatch);

  // A grown list replaces the file whole.
  Check("merge-grows", pp::Merge(list, "[" + Record("vs-00119C06", "ps-002EBB0F", 1) + "]", detail) == 1);
  Check("rewrite", pp::Write(file, "d3d12", list, detail), detail);
  Check("read-grown", pp::Read(file, "d3d12", read).result == pp::ReadResult::kRead && read.entries.size() == 3);

  // A record whose key no longer matches its content is left out, not the list.
  WriteText(file, std::string("{\"format\":\"") + std::string(pp::kFormat) + "\",\"api\":\"d3d12\",\"pipelines\":[" +
                      Record("vs-000C5339", "ps-002D8104", 65537) + "," + Record("vs-000C5339", "ps-002D8104", 3, true) +
                      ",{\"key\":\"zz\"}]}");
  const auto stale = pp::Read(file, "d3d12", read);
  Check("stale-records", stale.result == pp::ReadResult::kRead && stale.stale == 2 && read.entries.size() == 1,
        fmt::format("stale {}", stale.stale));

  WriteText(file, "{\"format\":\"broken-bond-player-pipelines-0\",\"api\":\"d3d12\",\"pipelines\":[]}");
  Check("old-format-mismatch", pp::Read(file, "d3d12", read).result == pp::ReadResult::kMismatch);

  WriteText(file, "\x00\x01garbage");
  Check("corrupt", pp::Read(file, "d3d12", read).result == pp::ReadResult::kCorrupt);
  const auto aside = pp::SetAside(file);
  Check("set-aside", !aside.empty() && std::filesystem::exists(aside) && !std::filesystem::exists(file),
        aside.string());
  WriteText(file, "[]");
  const auto aside2 = pp::SetAside(file);
  Check("set-aside-second", !aside2.empty() && aside2 != aside && std::filesystem::exists(aside2), aside2.string());

  std::error_code error;
  std::filesystem::remove_all(folder, error);
  std::cout << (passed ? "player pipelines: all passed" : "player pipelines: FAILED") << "\n";
  return passed ? 0 : 1;
}
