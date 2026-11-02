#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

// Precompiled pipelines of the D3D boundary and the record of the
// pipelines created (shaders/pipelines.cpp).
namespace gpu {
// The folder of the player's pipeline list (shaders/player_pipelines.h): the
// data folder's cache, set once before the first draw. Empty: no list.
void SetPlayerPipelineFolder(std::filesystem::path folder);
// Merges the pipelines created since the last save into the player's list and
// writes the list when it grew, on a thread of its own: at the world loads
// under the loading screen (common/title_loading.h). The stop and the close
// merge the whole list through RecordPipelines.
void SavePlayerPipelines(const char* reason);
// The precompile pool, started at the first draw.
void StartPipelinePrecompileOnce();
// The pipelines the list knows for a world (common/title_loading.h), posted to
// the asynchronous threads when the title starts loading it: how many the list
// has for the key, how many were posted, how many the set had already and how
// many could not be resolved. Any thread; nothing before the pool started.
struct WorldPrefetch {
  size_t listed = 0, posted = 0, present = 0, unresolved = 0;
};
WorldPrefetch PrefetchWorldPipelines(uint32_t world_key);
// The pipelines created with their stable identities, also as
// pipelines.json in REX_NATIVE_FRAME_DIR, on the stop and on the close.
void RecordPipelines();
}  // namespace gpu
