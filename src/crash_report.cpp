// broken_bond - ReXGlue Recompiled Project
//
// Crash report of a guest fault (crash_report.h).
//
// The guest stack: a function saves its return address at -8 of the caller's
// r1 and links the frames with the back chain at [r1]. From the r1 of the
// faulting thread, the caller's r1 is [r1] and the return address that frame
// saved is [[r1] - 8]; the walk stays inside the thread's stack (read from its
// PCR) and stops at the thread's entry or at anything that is not a frame
// above the last one. Equal addresses in a row are counted (a recursion shows
// as one address times its depth).
//
// Reads of guest memory check the host page first. A __try does not guard a
// plain load in this build (clang without -fasync-exceptions covers only the
// calls inside it), so a walk that reached a guard page would end the process
// inside the handler.

#include "crash_report.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include <fmt/format.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>

namespace broken_bond::crash_report {
namespace {
constexpr uint64_t kGuestArena = 0x100000000ull;  // The virtual base and the 4 GB after it.
constexpr uint32_t kMaxFrames = 65536;
constexpr uint32_t kMaxRuns = 96;
constexpr uint32_t kMaxReports = 4;
// The title's heaps are dlmalloc mspaces. The heap object the allocator takes
// (mspace_malloc 82200470, mspace_free 822007C8) holds the malloc state at
// +44 (the internal realloc 82201088 passes heap + 44, mspace_malloc reads
// heap[11..17]). The state: smallmap +0, treemap +4, dvsize +8, topsize +12,
// least_addr +16, dv +20, top +24, the 32 small bins from +36 (8 bytes each:
// the head is a pseudo chunk whose fd is at +8 and bk at +12), the segment
// list from +444 (base, size, next, flags; sys_alloc 82200198).
constexpr uint32_t kHeapState = 44;
constexpr uint32_t kSmallBins = 32;
constexpr uint32_t kMaxSegments = 32;
constexpr uint32_t kMaxHeaps = 16;

std::atomic<uint32_t> reports{0};
std::atomic<uint32_t> heaps[kMaxHeaps]{};
thread_local bool reporting = false;
thread_local uint32_t last_heap = 0;

uint32_t Load32(const uint8_t* base, uint32_t address) {
  return _byteswap_ulong(*reinterpret_cast<const uint32_t*>(base + address));
}

// Guest reads that cannot fault: the host page must be committed and
// readable. The last readable region is kept, so a walk over nearby addresses
// costs few queries; a Reader lives for one report.
class Reader {
 public:
  explicit Reader(const uint8_t* base) : base_(base) {}
  bool Get32(uint32_t address, uint32_t* out) {
    if (!Readable(base_ + address, 4)) return false;
    *out = Load32(base_, address);
    return true;
  }

 private:
  bool Readable(const uint8_t* host, size_t size) {
    const uintptr_t begin = reinterpret_cast<uintptr_t>(host), end = begin + size;
    if (begin >= begin_ && end <= end_) return true;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(host, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    if (!info.Protect || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    begin_ = reinterpret_cast<uintptr_t>(info.BaseAddress);
    end_ = begin_ + info.RegionSize;
    return end <= end_;
  }
  const uint8_t* base_;
  uintptr_t begin_ = 0, end_ = 0;
};

struct Run {
  uint32_t address, count;
};

// The thread's stack from its PCR (r13): +0x70 the base (high), +0x74 the end
// (low); zeros when unreadable.
void StackRange(Reader& reader, const PPCContext& ctx, uint32_t* end, uint32_t* base) {
  if (!reader.Get32(ctx.r13.u32 + 0x70, base)) *base = 0;
  if (!reader.Get32(ctx.r13.u32 + 0x74, end)) *end = 0;
}

// The return addresses of a guest thread, from its lr and then the ones its
// frames saved, inside [stack_end, stack_base) when that range is known.
std::string ReturnChain(Reader& reader, uint32_t lr, uint32_t sp, uint32_t stack_end, uint32_t stack_base,
                        uint32_t* frames_out) {
  Run runs[kMaxRuns]{};
  uint32_t run_count = 0, frames = 0, beyond = 0;
  auto add = [&](uint32_t address) {
    ++frames;
    if (run_count && runs[run_count - 1].address == address) {
      ++runs[run_count - 1].count;
      return;
    }
    if (run_count < kMaxRuns) runs[run_count++] = {address, 1};
    else ++beyond;
  };
  add(lr);
  const bool bounded = stack_base > stack_end;
  for (uint32_t i = 0; i < kMaxFrames; ++i) {
    if (bounded && (sp < stack_end || sp >= stack_base)) break;
    uint32_t next = 0, address = 0;
    if (!reader.Get32(sp, &next) || next <= sp || next - sp > 0x100000 || (next & 7)) break;
    if (bounded && next > stack_base) break;
    if (!reader.Get32(next - 8, &address)) break;
    add(address);
    sp = next;
  }
  std::string text;
  for (uint32_t i = 0; i < run_count; ++i) {
    if (!text.empty()) text += ' ';
    text += runs[i].count > 1 ? fmt::format("{:08X}x{}", runs[i].address, runs[i].count)
                              : fmt::format("{:08X}", runs[i].address);
  }
  if (beyond) text += fmt::format(" ...+{}", beyond);
  *frames_out = frames;
  return text;
}

// The segments of a heap's state, with the one that holds the fault marked;
// false when the state is unreadable.
bool Segments(Reader& reader, uint32_t state, uint32_t fault, std::string* text, bool* holds_fault) {
  uint32_t segment = state + 444;
  for (uint32_t i = 0; i < kMaxSegments && segment; ++i) {
    uint32_t sbase = 0, size = 0, next = 0, flags = 0;
    if (!reader.Get32(segment, &sbase) || !reader.Get32(segment + 4, &size) || !reader.Get32(segment + 8, &next) ||
        !reader.Get32(segment + 12, &flags))
      return i > 0;
    if (!sbase && !size) break;
    const bool holds = fault >= sbase && fault - sbase < size;
    *holds_fault = *holds_fault || holds;
    *text += fmt::format("{}{:08X}..{:08X}/{:X}{}", i ? " " : "", sbase, sbase + size, flags, holds ? "<-fault" : "");
    segment = next;
  }
  return true;
}

// The title's heap at the fault: of the heaps seen so far, the one whose
// segments hold the fault, with its state and the small bins that hold
// something. The allocator only checks a bin pointer against least_addr; a
// failed check traps, and a later pointer above least_addr is written through
// (fd at +8, bk at +12), so a fault at +12 of a garbage pointer names the bin
// whose head holds it. Then the chunk fields around the fault.
void HeapReport(Reader& reader, uint32_t fault) {
  uint32_t seen = 0, holder = 0;
  std::string listed, segments;
  for (auto& slot : heaps) {
    const uint32_t heap = slot.load(std::memory_order_relaxed);
    if (!heap) continue;
    ++seen;
    listed += fmt::format("{}{:08X}", listed.empty() ? "" : " ", heap);
    std::string text;
    bool holds = false;
    if (!holder && Segments(reader, heap + kHeapState, fault, &text, &holds) && holds) {
      holder = heap;
      segments = text;
    }
  }
  if (!holder) {
    REXLOG_ERROR("[broken_bond] crash report: fault outside the segments of the {} heaps seen{}{}", seen,
                 seen ? ": " : " (the allocator is followed in the native build)", listed);
  } else {
    const uint32_t state = holder + kHeapState;
    uint32_t smallmap = 0, treemap = 0, dvsize = 0, topsize = 0, least = 0, dv = 0, top = 0;
    if (reader.Get32(state, &smallmap) && reader.Get32(state + 4, &treemap) && reader.Get32(state + 8, &dvsize) &&
        reader.Get32(state + 12, &topsize) && reader.Get32(state + 16, &least) && reader.Get32(state + 20, &dv) &&
        reader.Get32(state + 24, &top)) {
      REXLOG_ERROR(
          "[broken_bond] crash report: heap 0x{:08X} (of {} seen): smallmap 0x{:08X} treemap 0x{:08X} least_addr "
          "0x{:08X} dv 0x{:08X} ({}) top 0x{:08X} ({}); segments: {}",
          holder, seen, smallmap, treemap, least, dv, dvsize, top, topsize, segments);
    }
    std::string bins;
    for (uint32_t index = 0; index < kSmallBins; ++index) {
      const uint32_t head = state + 36 + index * 8;
      uint32_t fd = 0, bk = 0;
      if (!reader.Get32(head + 8, &fd) || !reader.Get32(head + 12, &bk)) break;
      if (fd == head && bk == head) continue;
      const bool names = fd == fault - 12 || fd == fault - 8 || bk == fault - 12 || bk == fault - 8;
      bins += fmt::format("{}{}:{:08X}/{:08X}{}", bins.empty() ? "" : " ", index * 8, fd, bk, names ? "<-fault" : "");
    }
    REXLOG_ERROR("[broken_bond] crash report: small bins (size:fd/bk): {}", bins.empty() ? "none" : bins);
  }
  for (uint32_t offset : {8u, 12u}) {
    const uint32_t chunk = fault - offset;
    uint32_t foot = 0, head = 0, fd = 0, bk = 0;
    if (reader.Get32(chunk, &foot) && reader.Get32(chunk + 4, &head) && reader.Get32(chunk + 8, &fd) &&
        reader.Get32(chunk + 12, &bk))
      REXLOG_ERROR("[broken_bond] crash report: chunk at fault-{} 0x{:08X}: prev_foot 0x{:08X} head 0x{:08X} fd "
                   "0x{:08X} bk 0x{:08X}",
                   offset, chunk, foot, head, fd, bk);
    else
      REXLOG_ERROR("[broken_bond] crash report: chunk at fault-{} 0x{:08X}: no memory", offset, chunk);
  }
}

void Report(const char* kind, uint32_t code, uint64_t host_fault) {
  auto* state = rex::runtime::ThreadState::Get();
  if (!state || !state->context() || !state->memory()) {
    REXLOG_ERROR("[broken_bond] crash report: {} (code 0x{:08X}) on a host thread without a guest context", kind,
                 code);
    return;
  }
  PPCContext* ctx = state->context();
  const uint8_t* base = state->memory()->virtual_membase();
  const uint64_t host_base = reinterpret_cast<uint64_t>(base);
  const bool guest = host_fault >= host_base && host_fault < host_base + kGuestArena;
  const std::string fault = guest ? fmt::format("guest 0x{:08X}", uint32_t(host_fault - host_base))
                                  : fmt::format("host 0x{:016X}", host_fault);
  Reader reader(base);
  uint32_t stack_end = 0, stack_base = 0;
  StackRange(reader, *ctx, &stack_end, &stack_base);
  REXLOG_ERROR(
      "[broken_bond] crash report: {} of {} (code 0x{:08X}) on guest thread 0x{:X}: r1 0x{:08X}, lr 0x{:08X}, "
      "r3 0x{:08X}, r4 0x{:08X}, r12 0x{:08X}, stack 0x{:08X}..0x{:08X}",
      kind, fault, code, state->thread_id(), ctx->r1.u32, uint32_t(ctx->lr), ctx->r3.u32, ctx->r4.u32, ctx->r12.u32,
      stack_end, stack_base);
  rex::FlushLogging();
  uint32_t frames = 0;
  const std::string chain = ReturnChain(reader, uint32_t(ctx->lr), ctx->r1.u32, stack_end, stack_base, &frames);
  REXLOG_ERROR("[broken_bond] crash report: {} return addresses from the top (lr, then the saved ones): {}", frames,
               chain);
  rex::FlushLogging();
  if (guest) HeapReport(reader, uint32_t(host_fault - host_base));
  rex::FlushLogging();
}

// After the SDK's handler (first in the order), which resumes the faults it
// handles (the write watch, the MMIO windows) and leaves the rest to the
// search, so the report sees only those. A fault of the report itself passes.
LONG WINAPI Handler(EXCEPTION_POINTERS* pointers) {
  if (!pointers || !pointers->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
  const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
  const char* kind = nullptr;
  switch (record.ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
      kind = record.ExceptionInformation[0] == 1 ? "write" : record.ExceptionInformation[0] == 0 ? "read" : "access";
      break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      kind = "illegal instruction";
      break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      kind = "integer division by zero";
      break;
    default:  // A host stack overflow has no stack for the report.
      return EXCEPTION_CONTINUE_SEARCH;
  }
  if (reporting || reports.fetch_add(1) >= kMaxReports) return EXCEPTION_CONTINUE_SEARCH;
  reporting = true;
  Report(kind, record.ExceptionCode,
         record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION ? uint64_t(record.ExceptionInformation[1]) : 0);
  reporting = false;
  return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

void NoteHeap(uint32_t heap) {
  if (!heap || heap == last_heap) return;
  last_heap = heap;
  for (auto& slot : heaps) {
    uint32_t seen = slot.load(std::memory_order_relaxed);
    while (!seen && !slot.compare_exchange_weak(seen, heap, std::memory_order_relaxed)) {
    }
    if (!seen || seen == heap) return;
  }
}

void Install() {
  static bool installed = false;
  if (installed) return;
  installed = true;
  AddVectoredExceptionHandler(0, Handler);  // Last.
}
}  // namespace broken_bond::crash_report
