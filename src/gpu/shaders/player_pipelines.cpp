// The player's pipeline list (player_pipelines.h): parse, merge and write.
#include "gpu/shaders/player_pipelines.h"
#include "gpu/backend/pipeline_keys.h"
#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace gpu::player_pipelines {
namespace {
using json = nlohmann::json;

bool Hex(const json& value, uint64_t& out) {
  if (!value.is_string()) return false;
  const auto& text = value.get_ref<const std::string&>();
  if (text.empty() || text.size() > 16) return false;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out, 16);
  return error == std::errc() && end == text.data() + text.size();
}
bool Unsigned(const json& value, uint32_t& out) {
  if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<int64_t>() >= 0)) return false;
  const uint64_t wide = value.get<uint64_t>();
  if (wide > UINT32_MAX) return false;
  out = uint32_t(wide);
  return true;
}
// A record of Engine::PipelineList, or of a list file: false when malformed.
bool Parse(const json& record, Entry& e) {
  if (!record.is_object()) return false;
  const auto vs = record.find("vs"), ps = record.find("ps"), elements = record.find("elements"),
             state = record.find("state");
  if (vs == record.end() || !vs->is_string() || ps == record.end() || !ps->is_string() ||
      elements == record.end() || !elements->is_array() || state == record.end() || !state->is_array() ||
      state->size() != e.state.size() || !record.contains("key") || !record.contains("layout") ||
      !Hex(record["key"], e.key) || !Hex(record["layout"], e.layout))
    return false;
  e.vs = vs->get<std::string>();
  e.ps = ps->get<std::string>();
  if (e.vs.empty() || e.ps.empty()) return false;
  for (size_t i = 0; i < e.state.size(); ++i)
    if (!Unsigned((*state)[i], e.state[i])) return false;
  e.elements.clear();
  for (const auto& item : *elements) {
    if (!item.is_array() || item.size() < 5 || item.size() > 6 || !item[0].is_string()) return false;
    Element element;
    element.semantic = item[0].get<std::string>();
    if (element.semantic.empty() || !Unsigned(item[1], element.semantic_index) || !Unsigned(item[2], element.format) ||
        !Unsigned(item[3], element.slot) || !Unsigned(item[4], element.offset) ||
        (item.size() == 6 && !Unsigned(item[5], element.instance_step)))
      return false;
    e.elements.push_back(std::move(element));
  }
  if (e.elements.empty()) return false;
  uint64_t world = 0;
  e.world = record.contains("world") && Hex(record["world"], world) && world <= UINT32_MAX ? uint32_t(world) : 0;
  return true;
}
// The key and the layout id the content gives, as the draws compute them.
bool Consistent(const Entry& e) {
  std::vector<VertexElement> elements;
  elements.reserve(e.elements.size());
  for (const auto& element : e.elements)
    elements.push_back({element.semantic.c_str(), element.semantic_index, VertexFormat(element.format), element.slot,
                        element.offset, element.instance_step});
  return ElementsId(elements) == e.layout && PipelineKey(e.vs, e.ps, e.layout, e.state) == e.key;
}
json Record(const Entry& e) {
  json elements = json::array();
  for (const auto& element : e.elements) {
    json item = {element.semantic, element.semantic_index, element.format, element.slot, element.offset};
    if (element.instance_step) item.push_back(element.instance_step);
    elements.push_back(std::move(item));
  }
  return {{"key", fmt::format("{:016x}", e.key)}, {"vs", e.vs}, {"ps", e.ps},
          {"layout", fmt::format("{:016x}", e.layout)}, {"elements", std::move(elements)}, {"state", e.state},
          {"world", fmt::format("{:08x}", e.world)}};
}
}  // namespace

const char* Name(ReadResult result) {
  switch (result) {
    case ReadResult::kRead: return "read";
    case ReadResult::kMismatch: return "mismatch";
    case ReadResult::kCorrupt: return "corrupt";
    default: return "missing";
  }
}

ReadOutcome Read(const std::filesystem::path& file, std::string_view api, List& out) {
  out = {};
  ReadOutcome outcome;
  std::error_code error;
  if (!std::filesystem::is_regular_file(file, error)) return outcome;
  std::ifstream input(file, std::ios::binary);
  std::stringstream text;
  text << input.rdbuf();
  if (!input) {
    outcome.result = ReadResult::kCorrupt;
    outcome.detail = "unreadable";
    return outcome;
  }
  const json document = json::parse(text.str(), nullptr, false);
  if (document.is_discarded() || !document.is_object() || !document.contains("pipelines") ||
      !document["pipelines"].is_array()) {
    outcome.result = ReadResult::kCorrupt;
    outcome.detail = "not-a-list";
    return outcome;
  }
  const auto format = document.value("format", std::string());
  const auto list_api = document.value("api", std::string());
  if (format != kFormat || list_api != api) {
    outcome.result = ReadResult::kMismatch;
    outcome.detail = fmt::format("{}/{}", format, list_api);
    return outcome;
  }
  for (const auto& record : document["pipelines"]) {
    Entry e;
    if (!Parse(record, e) || !Consistent(e) || !out.keys.insert(e.key).second) {
      ++outcome.stale;
      continue;
    }
    out.entries.push_back(std::move(e));
  }
  outcome.result = ReadResult::kRead;
  return outcome;
}

long Merge(List& list, std::string_view records, std::string& detail) {
  const json document = json::parse(records, nullptr, false);
  if (document.is_discarded() || !document.is_array()) {
    detail = "not-an-array";
    return -1;
  }
  long added = 0;
  for (const auto& record : document) {
    Entry e;
    if (!Parse(record, e) || !Consistent(e) || list.keys.contains(e.key)) continue;
    list.keys.insert(e.key);
    list.entries.push_back(std::move(e));
    ++added;
  }
  return added;
}

bool Write(const std::filesystem::path& file, std::string_view api, const List& list, std::string& detail) {
  std::error_code error;
  std::filesystem::create_directories(file.parent_path(), error);
  if (error) {
    detail = "folder: " + error.message();
    return false;
  }
  json records = json::array();
  for (const auto& e : list.entries) records.push_back(Record(e));
  const std::string text = json{{"format", kFormat}, {"api", api}, {"pipelines", std::move(records)}}.dump();
  auto temporary = file;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(text.data(), std::streamsize(text.size()));
    output.close();
    if (!output) {
      std::filesystem::remove(temporary, error);
      detail = "write";
      return false;
    }
  }
  std::filesystem::rename(temporary, file, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    detail = "rename";
    return false;
  }
  return true;
}

std::filesystem::path SetAside(const std::filesystem::path& file) {
  for (int n = 1; n < 100; ++n) {
    auto target = file;
    target += n == 1 ? std::string(".bad") : fmt::format(".bad-{}", n);
    std::error_code error;
    if (std::filesystem::exists(target, error)) continue;
    std::filesystem::rename(file, target, error);
    return error ? std::filesystem::path() : target;
  }
  return {};
}

}  // namespace gpu::player_pipelines
