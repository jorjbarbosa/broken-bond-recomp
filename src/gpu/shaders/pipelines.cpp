// Precompiled pipelines. The list, shaders/pipeline_cache.inc (optional), is
// generated from the pipelines.json of earlier boots and git-ignored like the
// shader cache: the shader names, the layout and the
// state words of each key. At the first draw (shader cache loaded) the engine
// prepares its root signature and a pool of the dispatcher compiles the list with
// the DXIL of the cache and the spec constant variants the draw takes. A draw takes
// a ready pipeline, or waits for one being created; one missing from the list is
// created by the draw. REX_NATIVE_PSO_PRECOMPILE=0 turns the pool off.
//
// The list also knows the world each pipeline was first created in
// (common/title_loading.h): when the title starts loading a world, its entries
// not compiled yet are posted to the asynchronous threads ahead of the pool,
// which compile them above normal priority while the loading screen is up
// (PrefetchWorldPipelines). REX_NATIVE_PSO_WORLD=0 turns that off.
//
// The player's list (player_pipelines.h) adds the pipelines this machine's
// sessions created: read at the first draw, its entries the built-in list
// lacks go to the same pool after the built-in ones, and to the world
// prefetch; the session's pipelines are merged into it and the file written
// at the world loads, the stop and the close.
#include "gpu/shaders/pipelines.h"
#include "gpu/shaders/player_pipelines.h"
#include "diagnostics/incident.h"
#include "gpu/common/shader_activity.h"
#include "gpu/common/title_loading.h"
#include "gpu/d3d/draw.h"
#include "gpu/shaders/shaders.h"
#include "gpu/d3d/frame.h"
#include "gpu/d3d/d3d.h"
#include "gpu/backend/dispatcher.h"
#include "gpu/backend/pipeline_keys.h"
#include "gpu/backend/pipeline_set.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>
#include <rex/system/xmemory.h>

using namespace gpu;

namespace gpu {
struct PipelineCacheElement {
  const char* semantic;  // Static string (XenosRecomp semantics), as VertexElement.
  uint32_t semantic_index, format, slot, offset;
  uint32_t instance_step = 0;  // As VertexElement; written only when not 0.
};
struct PipelineCacheEntry {
  const char* vs;
  const char* ps;  // With "+specN", or "depth-only".
  uint64_t key, layout;
  uint32_t first_element, element_count;
  std::array<uint32_t, 21> state;
};
// The entries first created while a world was the one loaded last, as a
// range of kPipelineCacheWorldEntries (entry indices); a sentinel of key 0
// ends the table.
struct PipelineCacheWorld {
  uint32_t key, first, count;
};
#if __has_include("gpu/shaders/pipeline_cache.inc")
#include "gpu/shaders/pipeline_cache.inc"
constexpr std::span<const PipelineCacheEntry> kPipelineCache{kPipelineCacheEntries};
constexpr std::span<const PipelineCacheElement> kPipelineElements{kPipelineCacheElements};
constexpr std::span<const PipelineCacheWorld> kPipelineWorlds{kPipelineCacheWorlds};
constexpr std::span<const uint16_t> kPipelineWorldEntries{kPipelineCacheWorldEntries};
#else
constexpr std::span<const PipelineCacheEntry> kPipelineCache{};
constexpr std::span<const PipelineCacheElement> kPipelineElements{};
constexpr std::span<const PipelineCacheWorld> kPipelineWorlds{};
constexpr std::span<const uint16_t> kPipelineWorldEntries{};
constexpr const char* kPipelineCacheIdentity = "";
#endif

struct PipelinePrecompile {
  std::once_flag once;
  std::chrono::steady_clock::time_point started;
  unsigned threads = 0;
  std::vector<VertexElements> layouts;  // Per entry, shared by layout id.
  std::atomic<bool> pool_started{false};  // The layouts are built and the pool runs or ran.
  std::atomic<uint64_t> created{0}, present{0}, failed{0}, unresolved{0}, mismatched{0};
  // The world prefetches (PrefetchWorldPipelines) since the start.
  std::atomic<uint64_t> world_posted{0}, world_present{0};
  // Entries taken as the alpha to coverage variant (transparency antialiasing
  // on), and alpha to coverage entries taken back as the alpha test (off).
  std::atomic<uint64_t> alpha_to_coverage{0}, alpha_test{0};
  std::mutex mutex;
  std::string first_failure;  // JSON value.
} pipeline_precompile;

// The player's list. `extra` (the entries the built-in list lacks, pool
// indices from kPipelineCache.size()) and `extra_worlds` are set once, before
// the pool starts, and only read afterwards; the rest under the mutex.
struct PlayerPipelines {
  std::mutex mutex;
  std::filesystem::path folder;
  std::string api;  // "d3d12" or "vulkan": the file's name.
  bool read = false;
  player_pipelines::List list;
  player_pipelines::ReadOutcome outcome;
  std::string set_aside;  // Where an unusable file went.
  uint64_t written = 0, write_failures = 0;
  std::vector<player_pipelines::Entry> extra;
  std::unordered_map<uint32_t, std::vector<size_t>> extra_worlds;  // World key -> pool indices.
} player;

const char* ApiFileName(GraphicsApi api) { return api == GraphicsApi::kVulkan ? "vulkan" : "d3d12"; }
// The vertex elements name their semantic by a static string.
const char* InternSemantic(const std::string& name) {
  static std::mutex mutex;
  static std::set<std::string> names;
  std::lock_guard lock(mutex);
  return names.insert(name).first->c_str();
}
// Reads the player's list of the API once (under player.mutex); a file that
// cannot be used is set aside and the list starts empty.
void ReadPlayerList(GraphicsApi api) {
  if (player.read || player.folder.empty()) return;
  player.read = true;
  player.api = ApiFileName(api);
  const auto file = player.folder / (player.api + ".json");
  player.outcome = player_pipelines::Read(file, player.api, player.list);
  if (player.outcome.result == player_pipelines::ReadResult::kMismatch ||
      player.outcome.result == player_pipelines::ReadResult::kCorrupt) {
    player.set_aside = player_pipelines::SetAside(file).generic_string();
    player.list = {};
  }
}
// Merges a JSON array of Engine::PipelineList into the player's list and
// writes the list when it grew.
void MergePlayerPipelines(const std::string& records, GraphicsApi api, const char* reason) {
  long added = 0;
  bool written = false;
  size_t entries = 0;
  std::string detail;
  {
    std::lock_guard lock(player.mutex);
    if (player.folder.empty()) return;
    ReadPlayerList(api);
    added = player_pipelines::Merge(player.list, records, detail);
    if (added > 0) {
      written = player_pipelines::Write(player.folder / (player.api + ".json"), player.api, player.list, detail);
      ++(written ? player.written : player.write_failures);
    }
    entries = player.list.entries.size();
  }
  host::Record("native-player-pipelines", fmt::format(
      "\"reason\":\"{}\",\"added\":{},\"entries\":{},\"written\":{},\"detail\":\"{}\"", reason, added, entries,
      written, detail));
  if (!host::Journaled() && (added > 0 || !detail.empty()))
    REXLOG_INFO("[broken_bond] Player pipeline list ({}): {} added, {} in all, written={} {}", reason, added, entries,
                written, detail);
}

// An entry of the pool: the built-in list's, then the player's extra ones.
struct ListedEntry {
  std::string_view vs, ps;
  uint64_t key, layout;
  const std::array<uint32_t, 21>* state;
};
size_t ListedCount() { return kPipelineCache.size() + player.extra.size(); }
ListedEntry Listed(size_t i) {
  if (i < kPipelineCache.size()) {
    const auto& e = kPipelineCache[i];
    return {e.vs, e.ps, e.key, e.layout, &e.state};
  }
  const auto& e = player.extra[i - kPipelineCache.size()];
  return {e.vs, e.ps, e.key, e.layout, &e.state};
}

void PrecompileNote(std::atomic<uint64_t>& counter, std::string failure) {
  ++counter;
  std::lock_guard lock(pipeline_precompile.mutex);
  if (pipeline_precompile.first_failure.empty()) pipeline_precompile.first_failure = std::move(failure);
}

// The description of one entry of the list, on any thread: its shaders by
// name, the key checked against the engine's. False for an entry the cache
// cannot resolve or whose key does not match; note: the pool's counters and
// first failure take it (the pool only: the world prefetch counts its own).
// linked: the spec constants of the pixel shader taken; variant: the draw asks
// for another variant than the list holds (its key replaced the entry's).
bool DescribeEntry(size_t i, PipelineDesc& desc, uint32_t& linked, bool& variant, bool note) {
  const ListedEntry entry = Listed(i);
  auto unresolved = [&](const char* why) {
    if (note)
      PrecompileNote(pipeline_precompile.unresolved, fmt::format("{{\"reason\":\"{}\",\"vs\":\"{}\",\"ps\":\"{}\"}}",
                                                                 why, entry.vs, entry.ps));
    return false;
  };
  auto vs = shader_cache.by_name.find(std::string(entry.vs));
  if (vs == shader_cache.by_name.end()) return unresolved("vs-not-in-cache");
  desc.vs.key = std::string(entry.vs);
  desc.vs.format = desc.ps.format = shader_cache.format;
  desc.vs.spec_mask = vs->second->spec_mask;
  desc.ps.key = std::string(entry.ps);
  desc.vs.bytes = LoadModule(*vs->second);
  if (!desc.vs.bytes) return unresolved("vs-module-unreadable");
  std::string variant_key;  // The pixel shader key the draw asks for now, when the list holds the other.
  linked = 0;
  variant = false;
  if (desc.ps.key != "depth-only") {
    std::string name = desc.ps.key;
    uint32_t spec = 0;
    if (const size_t plus = name.find("+spec"); plus != std::string::npos) {
      spec = uint32_t(std::stoul(name.substr(plus + 5)));
      name.resize(plus);
    }
    auto ps = shader_cache.by_name.find(name);
    if (ps == shader_cache.by_name.end()) return unresolved("ps-not-in-cache");
    // The list holds the variants of the boots it came from, with the
    // transparency antialiasing those boots had. The draw asks for the alpha
    // test (only TransparencySpec turns it into alpha to coverage), so an
    // entry goes back to the alpha test and through TransparencySpec as the
    // draw does: alpha to coverage in a multisampled target when the option
    // is on, the alpha test otherwise.
    const uint32_t requested = spec == kSpecAlphaToCoverage ? kSpecAlphaTest : spec;
    linked = TransparencySpec(requested, ps->second->spec_mask, (*entry.state)[8], (*entry.state)[0]);
    if (linked != spec) variant_key = fmt::format("{}+spec{}", name, linked);
    std::string error;
    desc.ps.bytes = LoadShader(*ps->second, linked, error);
    if (!desc.ps.bytes) return unresolved("ps-module-unavailable");
    desc.ps.spec_mask = ps->second->spec_mask;
    desc.ps.spec_value = linked;
  }
  desc.elements = pipeline_precompile.layouts[i];
  desc.layout_id = entry.layout;
  desc.state = *entry.state;
  if (PipelineKey(desc.vs.key, desc.ps.key, desc.layout_id, desc.state) != entry.key) {
    if (note)
      PrecompileNote(pipeline_precompile.mismatched, fmt::format("{{\"reason\":\"key\",\"key\":\"{:016x}\"}}",
                                                                 entry.key));
    return false;
  }
  // As the draw: the variant's key; alpha to coverage turns state word 11 on,
  // and the alpha test keeps the word of the entry (the title's alpha to mask).
  if (!variant_key.empty()) {
    variant = true;
    desc.ps.key = variant_key;
    if (linked == kSpecAlphaToCoverage) desc.state[11] = 1;
  }
  return true;
}

// One entry of the list on a pool thread: its description, then the engine's
// PrecompilePipeline.
void PrecompileEntry(size_t i, Engine& engine) {
  broken_bond::incident::Activity activity(broken_bond::incident::Role::kCompiler);
  broken_bond::incident::Progress(broken_bond::incident::Role::kCompiler);
  thread_local auto last_incident_progress = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (now - last_incident_progress > std::chrono::seconds(1)) {
    last_incident_progress = now;
    broken_bond::incident::Event("pipeline-job", "progress", std::to_string(i));
  }
  ShaderBatchEntry counted;
  PipelineDesc desc;
  uint32_t linked = 0;
  bool variant = false;
  if (!DescribeEntry(i, desc, linked, variant, true)) return;
  bool present = false;
  std::string evidence;
  if (!engine.PrecompilePipeline(desc, present, evidence))
    return PrecompileNote(pipeline_precompile.failed, evidence);
  ++(present ? pipeline_precompile.present : pipeline_precompile.created);
  if (variant) ++(linked == kSpecAlphaToCoverage ? pipeline_precompile.alpha_to_coverage
                                                 : pipeline_precompile.alpha_test);
}

// REX_NATIVE_PSO_WORLD=0 leaves the pipelines of a world to the pool and the draws.
bool worldPrefetchOption() {
  static const bool value = [] {
    const char* text = std::getenv("REX_NATIVE_PSO_WORLD");
    return !(text && std::string_view(text) == "0");
  }();
  return value;
}

void StartPipelinePrecompile() {
  const auto incident_operation = broken_bond::incident::BeginOperation("pipeline-precompile");
  auto& p = pipeline_precompile;
  const char* off = std::getenv("REX_NATIVE_PSO_PRECOMPILE");
  auto* dispatcher = host::ActiveDispatcher();
  // The player's list: the entries the built-in list lacks join the pool after it.
  std::string player_record = "null";
  if (dispatcher) {
    std::lock_guard lock(player.mutex);
    ReadPlayerList(dispatcher->Api());
    if (player.read) {
      std::unordered_set<uint64_t> built_in;
      for (const auto& e : kPipelineCache) built_in.insert(e.key);
      for (const auto& e : player.list.entries) {
        if (built_in.contains(e.key)) continue;
        player.extra_worlds[e.world].push_back(kPipelineCache.size() + player.extra.size());
        player.extra.push_back(e);
      }
      player_record = fmt::format("{{\"result\":\"{}\",\"entries\":{},\"extra\":{},\"stale\":{},\"set_aside\":\"{}\"}}",
                                  player_pipelines::Name(player.outcome.result), player.list.entries.size(),
                                  player.extra.size(), player.outcome.stale, player.set_aside);
    }
  }
  if (!host::Journaled() && player_record != "null")
    REXLOG_INFO("[broken_bond] Player pipeline list: {}", player_record);
  const char* skip = off && std::string_view(off) == "0" ? "disabled" : ListedCount() == 0 ? "list-absent"
                     : !shader_cache.loaded ? "shader-cache-unavailable" : nullptr;
  std::string prepared;
  if (!skip && (!dispatcher || !dispatcher->Run([](Engine& engine, std::string& out) {
        return engine.PreparePipelines(out);
      }, prepared))) skip = "prepare-failed";
  if (skip) {
    broken_bond::incident::Event("pipeline-precompile", "end", skip, incident_operation);
    host::Record("native-d3d-pipeline-precompile-start", fmt::format(
        "\"started\":false,\"reason\":\"{}\",\"list\":{},\"identity\":\"{}\",\"prepare\":{},\"player\":{}", skip,
        kPipelineCache.size(), kPipelineCacheIdentity, prepared.empty() ? "null" : prepared, player_record));
    return;
  }
  std::unordered_map<uint64_t, VertexElements> shared;
  for (const auto& entry : kPipelineCache) {
    auto& layout = shared[entry.layout];
    if (!layout) {
      std::vector<VertexElement> elements;
      for (uint32_t e = entry.first_element; e < entry.first_element + entry.element_count; ++e) {
        const auto& c = kPipelineElements[e];
        elements.push_back({c.semantic, c.semantic_index, VertexFormat(c.format), c.slot, c.offset, c.instance_step});
      }
      layout = std::make_shared<const std::vector<VertexElement>>(std::move(elements));
    }
    p.layouts.push_back(layout);
  }
  for (const auto& entry : player.extra) {
    auto& layout = shared[entry.layout];
    if (!layout) {
      std::vector<VertexElement> elements;
      for (const auto& c : entry.elements)
        elements.push_back({InternSemantic(c.semantic), c.semantic_index, VertexFormat(c.format), c.slot, c.offset,
                            c.instance_step});
      layout = std::make_shared<const std::vector<VertexElement>>(std::move(elements));
    }
    p.layouts.push_back(layout);
  }
  p.threads = PipelineThreadsPerPool();
  p.started = std::chrono::steady_clock::now();
  p.pool_started.store(true, std::memory_order_release);
  BeginShaderBatch(uint32_t(ListedCount()));
  const bool started = dispatcher->StartPool(ListedCount(), p.threads, PrecompileEntry, [incident_operation](bool stopped) {
    EndShaderBatch();
    auto& p = pipeline_precompile;
    std::string failure;
    {
      std::lock_guard lock(p.mutex);
      failure = p.first_failure.empty() ? "null" : p.first_failure;
    }
    const std::string summary = fmt::format(
        "\"list\":{},\"player\":{},\"threads\":{},\"created\":{},\"present\":{},\"failed\":{},\"unresolved\":{},"
        "\"mismatched\":{},\"alpha_to_coverage\":{},\"alpha_test\":{},\"world_posted\":{},\"world_present\":{},"
        "\"stopped\":{},\"ms\":{},\"swap\":{},\"first_failure\":{}",
        kPipelineCache.size(), player.extra.size(),
        p.threads, p.created.load(), p.present.load(), p.failed.load(), p.unresolved.load(), p.mismatched.load(),
        p.alpha_to_coverage.load(), p.alpha_test.load(), p.world_posted.load(), p.world_present.load(), stopped,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - p.started).count(),
        swaps.load(), failure);
    host::Record("native-d3d-pipeline-precompile", summary);
    broken_bond::incident::Event("pipeline-precompile", "end", summary, incident_operation);
    // The product has no journal: how long the list took goes to the SDK log.
    if (!host::Journaled()) REXLOG_INFO("[broken_bond] Pipeline precompile: {{{}}}", summary);
  });
  if (!started) {
    EndShaderBatch(); broken_bond::incident::Event("pipeline-precompile", "end", "pool-not-started", incident_operation);
  }
  host::Record("native-d3d-pipeline-precompile-start", fmt::format(
      "\"started\":{},\"list\":{},\"layouts\":{},\"worlds\":{},\"threads\":{},\"identity\":\"{}\",\"swap\":{},"
      "\"player\":{}", started, kPipelineCache.size(), shared.size(),
      kPipelineWorlds.empty() ? 0 : kPipelineWorlds.size() - 1, p.threads, kPipelineCacheIdentity, swaps.load(),
      player_record));
}

void StartPipelinePrecompileOnce() { std::call_once(pipeline_precompile.once, StartPipelinePrecompile); }

// The entries of a world (the table ends with a sentinel of key 0), described
// on the calling thread and posted to the asynchronous threads of the pipeline
// set, which skip the ones the set has (compiled by the pool, or taken by a
// draw already). Nothing before the pool started: its layouts are the
// entries', and the first world of a boot loads before the first draw.
WorldPrefetch PrefetchWorldPipelines(uint32_t world_key) {
  WorldPrefetch out;
  auto& p = pipeline_precompile;
  if (!worldPrefetchOption() || !p.pool_started.load(std::memory_order_acquire)) return out;
  if (!world_key) return out;  // The sentinel of key 0 is not a world.
  // The built-in list's entries of the world, then the player's extra ones.
  std::vector<size_t> indices;
  for (const auto& w : kPipelineWorlds)
    if (w.key == world_key) {
      for (uint32_t e = w.first; e < w.first + w.count; ++e) indices.push_back(kPipelineWorldEntries[e]);
      break;
    }
  if (const auto it = player.extra_worlds.find(world_key); it != player.extra_worlds.end())
    indices.insert(indices.end(), it->second.begin(), it->second.end());
  if (indices.empty()) return out;
  out.listed = indices.size();
  std::vector<PipelineDesc> descs;
  descs.reserve(indices.size());
  for (const size_t i : indices) {
    PipelineDesc desc;
    uint32_t linked = 0;
    bool variant = false;
    if (DescribeEntry(i, desc, linked, variant, false)) descs.push_back(std::move(desc));
    else ++out.unresolved;
  }
  auto* dispatcher = host::ActiveDispatcher();
  if (!dispatcher || !dispatcher->WithEngine([&](Engine& engine) {
        for (const auto& desc : descs) ++(engine.PostPipeline(desc) ? out.posted : out.present);
      })) return out;
  p.world_posted.fetch_add(out.posted, std::memory_order_relaxed);
  p.world_present.fetch_add(out.present, std::memory_order_relaxed);
  return out;
}
}  // namespace gpu

namespace gpu {
// The pipelines created with their stable identities, also as
// pipelines.json in REX_NATIVE_FRAME_DIR (the input of the precompiled list),
// on the stop and on the close.
void RecordPipelines() {
  auto* dispatcher = host::ActiveDispatcher();
  std::string pipelines, takes;
  if (!dispatcher || !dispatcher->Run([&takes](Engine& engine, std::string& o) {
        o = engine.PipelineList();
        takes = engine.PipelineSummary();
        return true;
      }, pipelines) || pipelines.empty()) return;
  // How the draws took their pipelines (pipeline_set.h): the record and, in
  // the product (no journal), the SDK log.
  host::Record("native-d3d-pipeline-takes", takes);
  if (!host::Journaled()) REXLOG_INFO("[broken_bond] Native renderer pipelines: {{{}}}", takes);
  size_t count = 0;
  for (size_t at = pipelines.find("{\"key\""); at != std::string::npos; at = pipelines.find("{\"key\"", at + 1)) ++count;
  std::string file = "null";
  if (const char* dir = std::getenv("REX_NATIVE_FRAME_DIR"); dir && *dir) {
    std::string path = std::string(dir) + "/pipelines.json";
    std::replace(path.begin(), path.end(), '\\', '/');  // A JSON-safe path in the record.
    if (std::FILE* out = std::fopen(path.c_str(), "wb")) {
      std::fwrite(pipelines.data(), 1, pipelines.size(), out);
      std::fclose(out);
      file = "\"" + path + "\"";
    }
  }
  host::Record("native-d3d-pipelines", fmt::format("\"count\":{},\"file\":{},\"pipelines\":{}", count, file, pipelines));
  MergePlayerPipelines(pipelines, dispatcher->Api(), "record");
}

void SetPlayerPipelineFolder(std::filesystem::path folder) {
  std::lock_guard lock(player.mutex);
  if (!player.read) player.folder = std::move(folder);
}

void SavePlayerPipelines(const char* reason) {
  {
    std::lock_guard lock(player.mutex);
    if (player.folder.empty()) return;
  }
  auto* dispatcher = host::ActiveDispatcher();
  std::string pipelines;
  if (!dispatcher || !dispatcher->Run([](Engine& engine, std::string& o) {
        o = engine.PipelineList();
        return true;
      }, pipelines) || pipelines.empty()) return;
  MergePlayerPipelines(pipelines, dispatcher->Api(), reason);
}
}  // namespace gpu
