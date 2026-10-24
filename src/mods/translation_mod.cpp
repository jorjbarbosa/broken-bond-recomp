// broken_bond - ReXGlue Recompiled Project
//
// External translation at the TXU resource callback, sub_8275D500(resource).
// That callback calls sub_82BFD450(reader, resource, &size, 1, 0), receiving
// the original bytes in r3 and a big-endian byte count at the guest r5 address.
// Only that read, in the same thread and for the same resource, is replaced.
// Other resource reads and the original allocation remain game-owned.
//
// Ownership after the callback returns:
// - 8275D268 allocates the table's descriptor array (4 bytes per entry) and a
//   separate string pool of the size the header gives.
// - 82BCE958 copies the whole pool from the input into that pool.
// - The 12-byte entries and their attributes hold pointers into the game's
//   pool or plain values, never pointers into the input.
// - 82BD07B0 swaps wide strings to guest byte order inside the copied pool.
// Thus the temporary SystemHeap table can be freed after the callback returns;
// the game neither retains nor frees this input. A thread-local scope handles
// nesting and restores its predecessor.
//
// Verified against the default XEX (SHA-256
// 8f70e79443e36b38e44b6a105dad51fb8909fb615d982753cabaac9b15f0d576) with IDA
// through REA (8275D268, 82BCE958, 82BFD450, 8275D500) and the generated C++.
// Guest pointers come from the completed resource read, its caller's size
// slot, and SystemHeapAlloc. Access them through the SDK's virtual translation;
// do not change page protection or route self-process access through Win32.

#include "mods/translation_mod.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>

#include <atomic>
#include <cstring>
#include <mutex>

#include "mods/translation_data.h"

REXCVAR_DEFINE_STRING(translation_mod, "", "broken_bond/Mods",
                      "Translation mod folder under mods/ next to the executable; empty disables "
                      "it. Requires restart and English game language.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

extern "C" void __imp__sub_8275D500(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82BFD450(PPCContext& __restrict, uint8_t*);

namespace broken_bond::translation {
namespace {
// The English table; the other languages are AB00018B-AB00018E.
constexpr uint32_t kEnglishTable = 0xAB00018A;
std::once_flag initialized;
Package package;
std::atomic<bool> enabled{false};

bool ValidGuestRange(uint32_t address, size_t size) {
  return address != 0 && size <= 0x100000000ull - address;
}

struct Scope;
thread_local Scope* active = nullptr;
struct Scope {
  Scope* previous = active;
  rex::memory::Memory* memory;
  uint32_t resource;
  uint32_t temporary = 0;

  Scope(rex::memory::Memory* memory, uint32_t resource) : memory(memory), resource(resource) {
    active = this;
  }
  ~Scope() {
    active = previous;
    if (temporary) {
      memory->SystemHeapFree(temporary);
      REXLOG_INFO("[translation] temporary released: resource={:08X} address={:08X}", resource,
                  temporary);
    }
  }
};

void ReplaceRead(Scope& scope, uint32_t input, uint32_t size_address, PPCContext& ctx) {
  if (scope.temporary || !ValidGuestRange(size_address, sizeof(uint32_t)))
    return;
  auto* size_slot = scope.memory->TranslateVirtual(size_address);
  const uint32_t original_size = rex::memory::load_and_swap<uint32_t>(size_slot);
  if (original_size < sizeof(uint32_t) || original_size > kMaxTableBytes ||
      !ValidGuestRange(input, original_size))
    return;

  try {
    const auto* original = scope.memory->TranslateVirtual(input);
    const auto patched = PatchTable(std::span(original, original_size), package.texts);
    for (const auto& rejected : patched.rejected) {
      REXLOG_WARN("[translation] original text retained: resource={:08X} id={}: {}", scope.resource,
                  rejected.id, rejected.reason);
    }
    if (patched.status == PatchStatus::kInvalid) {
      REXLOG_ERROR("[translation] original table retained: resource={:08X}: {}", scope.resource,
                   patched.error);
      return;
    }
    if (patched.status != PatchStatus::kRebuilt)
      return;

    const auto translated_size = uint32_t(patched.bytes.size());
    scope.temporary = scope.memory->SystemHeapAlloc(translated_size);
    if (!scope.temporary) {
      REXLOG_ERROR("[translation] cannot allocate temporary table; original retained");
      return;
    }
    std::memcpy(scope.memory->TranslateVirtual(scope.temporary), patched.bytes.data(),
                translated_size);
    // Publish only after the replacement has been completely written.
    rex::memory::store_and_swap<uint32_t>(size_slot, translated_size);
    ctx.r3.u64 = scope.temporary;
    REXLOG_INFO(
        "[translation] table rebuilt: resource={:08X} replaced={} rejected={} original_bytes={} "
        "translated_bytes={}",
        scope.resource, patched.replacements, patched.rejected.size(), original_size,
        translated_size);
  } catch (const std::exception& exception) {
    REXLOG_ERROR("[translation] cannot rebuild table: {}", exception.what());
  }
}
}  // namespace

void Initialize(const std::filesystem::path& executable_root) {
  std::call_once(initialized, [&] {
    const std::string selected = REXCVAR_GET(translation_mod);
    if (selected.empty())
      return;
    if (!ValidModId(selected)) {
      REXLOG_ERROR("[translation] invalid mod folder name: {}", selected);
      return;
    }
    if (rex::cvar::Query<uint32_t>("user_language") != 1) {
      REXLOG_ERROR(
          "[translation] {} requires English as the game language; original texts retained",
          selected);
      return;
    }
    std::string error;
    if (!LoadPackage(executable_root / "mods" / selected, package, error)) {
      REXLOG_ERROR("[translation] cannot load {}: {}", selected, error);
      return;
    }
    REXLOG_INFO("[translation] loaded {} ({}) with {} text IDs; restart required for changes",
                package.name, package.id, package.texts.size());
    enabled.store(!package.texts.empty(), std::memory_order_release);
  });
}
}  // namespace broken_bond::translation

REX_HOOK_RAW(sub_8275D500) {
  using namespace broken_bond::translation;
  const uint32_t resource = ctx.r3.u32;
  if (!enabled.load(std::memory_order_acquire) || resource != kEnglishTable) {
    __imp__sub_8275D500(ctx, base);
    return;
  }
  auto* thread = rex::runtime::ThreadState::Get();
  Scope scope(thread ? thread->memory() : nullptr, resource);
  __imp__sub_8275D500(ctx, base);
}

REX_HOOK_RAW(sub_82BFD450) {
  using namespace broken_bond::translation;
  Scope* scope = active;
  const uint32_t resource = ctx.r4.u32;
  const uint32_t size_address = ctx.r5.u32;
  __imp__sub_82BFD450(ctx, base);
  if (scope && scope->memory && resource == scope->resource) {
    ReplaceRead(*scope, ctx.r3.u32, size_address, ctx);
  }
}
