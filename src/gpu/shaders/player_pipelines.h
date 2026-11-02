// The player's pipeline list: the pipelines the sessions on this machine
// created, kept in the data folder's cache and precompiled at the next start
// after the built-in list (shaders/pipelines.cpp). One file per graphics API,
// pipelines/<api>.json: a header object around the records of
// Engine::PipelineList. A record's key is checked against its content when the
// list is read, so a change of the key's representation drops the records it
// affects instead of the whole list.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace gpu::player_pipelines {

inline constexpr std::string_view kFormat = "broken-bond-player-pipelines-1";

struct Element {
  std::string semantic;
  uint32_t semantic_index = 0, format = 0, slot = 0, offset = 0, instance_step = 0;
};
struct Entry {
  uint64_t key = 0, layout = 0;
  std::string vs, ps;  // ps with "+specN", or "depth-only".
  std::vector<Element> elements;
  std::array<uint32_t, 21> state{};
  uint32_t world = 0;  // The world loaded last when the pipeline was created.
};
struct List {
  std::vector<Entry> entries;
  std::unordered_set<uint64_t> keys;
};

enum class ReadResult { kMissing, kRead, kMismatch, kCorrupt };
struct ReadOutcome {
  ReadResult result = ReadResult::kMissing;
  size_t stale = 0;  // Records left out: malformed, or a key that does not match the content.
  std::string detail;
};
const char* Name(ReadResult result);

// Reads the list file of api into out (cleared first). kMismatch: another
// format or API; kCorrupt: not a list.
ReadOutcome Read(const std::filesystem::path& file, std::string_view api, List& out);
// Adds the records of a JSON array of Engine::PipelineList the list lacks.
// The number added, or -1 when the array does not parse (detail).
long Merge(List& list, std::string_view records, std::string& detail);
// Writes the list: to a temporary file beside it, then renamed over it, so a
// failure leaves the previous list whole. Creates the folder.
bool Write(const std::filesystem::path& file, std::string_view api, const List& list, std::string& detail);
// Renames a file that could not be used aside (<file>.bad, then .bad-2, ...)
// and returns the new name, or an empty path when the rename failed.
std::filesystem::path SetAside(const std::filesystem::path& file);

}  // namespace gpu::player_pipelines
