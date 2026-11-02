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
// Reads of guest memory check the host page first (platform::ReadableRegion).
// A __try does not guard a plain load in this build (clang without
// -fasync-exceptions covers only the calls inside it), so a walk that reached
// a guard page would end the process inside the handler.
//
// The texts are written into a buffer on the handler's stack with fixed-width
// hex and decimal digits: the handler runs before Crashpad's, and a fault in
// the heap or with the logger's lock held must not stop it.

#include "crash_report.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <string_view>

#include <rex/exception_handler.h>
#include <rex/ppc/context.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>

#include "diagnostics/channel.h"
#include "diagnostics/incident.h"
#include "gpu/common/platform.h"

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
thread_local bool observing = false;
thread_local uint32_t last_heap = 0;

uint32_t Load32(const uint8_t* base, uint32_t address) {
  return std::byteswap(*reinterpret_cast<const uint32_t*>(base + address));
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
    if (!gpu::platform::ReadableRegion(host, begin_, end_)) {
      begin_ = end_ = 0;
      return false;
    }
    return end <= end_;
  }
  const uint8_t* base_;
  uintptr_t begin_ = 0, end_ = 0;
};

// A text in a fixed buffer, as long as the channel's slot holds; what does
// not fit ends in "...".
class Text {
 public:
  Text& operator<<(std::string_view text) {
    for (char c : text) Put(c);
    return *this;
  }
  // Eight hex digits, the width of a guest address.
  Text& Hex(uint32_t value) {
    constexpr char digits[] = "0123456789ABCDEF";
    for (int n = 7; n >= 0; --n) Put(digits[(value >> (n * 4)) & 15]);
    return *this;
  }
  Text& Dec(uint32_t value) {
    char digits[10];
    int count = 0;
    do {
      digits[count++] = char('0' + value % 10);
      value /= 10;
    } while (value);
    while (count) Put(digits[--count]);
    return *this;
  }
  std::string_view View() const { return {data_.data(), size_}; }

 private:
  void Put(char c) {
    if (size_ < data_.size() - 4) {
      data_[size_++] = c;
    } else if (!full_) {
      full_ = true;
      for (char dot : {'.', '.', '.'}) data_[size_++] = dot;
    }
  }
  std::array<char, incident::kPayload - 1> data_{};
  size_t size_ = 0;
  bool full_ = false;
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
void ReturnChain(Reader& reader, uint32_t lr, uint32_t sp, uint32_t stack_end, uint32_t stack_base, Text& text) {
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
  text.Dec(frames) << " return addresses from the top (lr, then the saved ones):";
  for (uint32_t i = 0; i < run_count; ++i) {
    text << " ";
    text.Hex(runs[i].address);
    if (runs[i].count > 1) {  // A repeated address with its count: 8221ABCDx12.
      text << "x";
      text.Dec(runs[i].count);
    }
  }
  if (beyond) {
    text << " ...+";
    text.Dec(beyond);
  }
}

// The segments of a heap's state, with the one that holds the fault marked;
// false when the state is unreadable.
bool Segments(Reader& reader, uint32_t state, uint32_t fault, Text* text, bool* holds_fault) {
  uint32_t segment = state + 444;
  for (uint32_t i = 0; i < kMaxSegments && segment; ++i) {
    uint32_t sbase = 0, size = 0, next = 0, flags = 0;
    if (!reader.Get32(segment, &sbase) || !reader.Get32(segment + 4, &size) || !reader.Get32(segment + 8, &next) ||
        !reader.Get32(segment + 12, &flags))
      return i > 0;
    if (!sbase && !size) break;
    const bool holds = fault >= sbase && fault - sbase < size;
    *holds_fault = *holds_fault || holds;
    if (text) {
      if (i) *text << " ";
      text->Hex(sbase) << "..";
      text->Hex(sbase + size) << "/";
      text->Hex(flags) << (holds ? "<-fault" : "");
    }
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
void HeapReport(Reader& reader, uint32_t fault, Text& text) {
  uint32_t seen = 0, holder = 0;
  for (auto& slot : heaps) {
    const uint32_t heap = slot.load(std::memory_order_relaxed);
    if (!heap) continue;
    ++seen;
    bool holds = false;
    if (!holder && Segments(reader, heap + kHeapState, fault, nullptr, &holds) && holds) holder = heap;
  }
  if (!holder) {
    text << "fault outside the segments of the ";
    text.Dec(seen) << " heaps seen" << (seen ? ":" : " (the allocator is followed in the native build on Windows)");
    for (auto& slot : heaps) {
      if (const uint32_t heap = slot.load(std::memory_order_relaxed)) {
        text << " ";
        text.Hex(heap);
      }
    }
  } else {
    const uint32_t state = holder + kHeapState;
    uint32_t smallmap = 0, treemap = 0, dvsize = 0, topsize = 0, least = 0, dv = 0, top = 0;
    text << "heap 0x";
    text.Hex(holder) << " (of ";
    text.Dec(seen) << " seen)";
    if (reader.Get32(state, &smallmap) && reader.Get32(state + 4, &treemap) && reader.Get32(state + 8, &dvsize) &&
        reader.Get32(state + 12, &topsize) && reader.Get32(state + 16, &least) && reader.Get32(state + 20, &dv) &&
        reader.Get32(state + 24, &top)) {
      text << ": smallmap 0x";
      text.Hex(smallmap) << " treemap 0x";
      text.Hex(treemap) << " least_addr 0x";
      text.Hex(least) << " dv 0x";
      text.Hex(dv) << " (";
      text.Dec(dvsize) << ") top 0x";
      text.Hex(top) << " (";
      text.Dec(topsize) << ")";
    }
    text << "; segments: ";
    bool holds = false;
    Segments(reader, state, fault, &text, &holds);
    text << "; small bins (size:fd/bk):";
    bool any = false;
    for (uint32_t index = 0; index < kSmallBins; ++index) {
      const uint32_t head = state + 36 + index * 8;
      uint32_t fd = 0, bk = 0;
      if (!reader.Get32(head + 8, &fd) || !reader.Get32(head + 12, &bk)) break;
      if (fd == head && bk == head) continue;
      any = true;
      const bool names = fd == fault - 12 || fd == fault - 8 || bk == fault - 12 || bk == fault - 8;
      text << " ";
      text.Dec(index * 8) << ":";
      text.Hex(fd) << "/";
      text.Hex(bk) << (names ? "<-fault" : "");
    }
    if (!any) text << " none";
  }
  for (uint32_t offset : {8u, 12u}) {
    const uint32_t chunk = fault - offset;
    uint32_t foot = 0, head = 0, fd = 0, bk = 0;
    text << "; chunk at fault-";
    text.Dec(offset) << " 0x";
    text.Hex(chunk);
    if (reader.Get32(chunk, &foot) && reader.Get32(chunk + 4, &head) && reader.Get32(chunk + 8, &fd) &&
        reader.Get32(chunk + 12, &bk)) {
      text << ": prev_foot 0x";
      text.Hex(foot) << " head 0x";
      text.Hex(head) << " fd 0x";
      text.Hex(fd) << " bk 0x";
      text.Hex(bk);
    } else {
      text << ": no memory";
    }
  }
}

// In the SDK's handler chain (rex::arch::ExceptionHandler: a vectored handler on
// Windows, the signal handlers elsewhere), after its own handlers, which
// resume the faults they handle (the write watch, the MMIO windows); the
// report sees only the rest and never handles the fault, which goes on to
// Crashpad. The chain gives access violations and illegal instructions only.
bool Handler(rex::arch::Exception* ex, void*) {
  using Code = rex::arch::Exception::Code;
  if (!ex || (ex->code() != Code::kAccessViolation && ex->code() != Code::kIllegalInstruction)) return false;
  if (observing) return false;  // A fault of the report itself passes on.
  observing = true;
  const bool violation = ex->code() == Code::kAccessViolation;
  const uint64_t address = violation ? ex->fault_address() : 0;
  const uint32_t access = violation ? uint32_t(ex->access_violation_operation()) : 0;
  // The fault first, before anything reads possibly damaged guest state.
  incident::GuestFault(ex->pc(), address, uint32_t(ex->code()), 0, 0, {}, 0, 0, access);
  auto* state = rex::runtime::ThreadState::Get();
  const PPCContext* ctx = state ? state->context() : nullptr;
  const uint8_t* base = ctx && state->memory() ? state->memory()->virtual_membase() : nullptr;
  if (ctx && base) {
    // CTR and r14-r31 are locals of each recompiled function
    // (broken_bond_manifest.toml), so their context fields are stale; only
    // r0-r13 and LR are published, and the supervisor records the rest as
    // unavailable.
    std::array<uint64_t, 32> gpr{};
    const PPCRegister* registers[14] = {&ctx->r0, &ctx->r1, &ctx->r2,  &ctx->r3,  &ctx->r4,  &ctx->r5,  &ctx->r6,
                                        &ctx->r7, &ctx->r8, &ctx->r9, &ctx->r10, &ctx->r11, &ctx->r12, &ctx->r13};
    for (size_t i = 0; i < std::size(registers); ++i) gpr[i] = registers[i]->u64;
    incident::GuestFault(ex->pc(), address, uint32_t(ex->code()), state->thread_id(),
                         reinterpret_cast<uint64_t>(base), gpr, ctx->lr, 0, access);
    if (reports.fetch_add(1) < kMaxReports) {
      const uint64_t host_base = reinterpret_cast<uint64_t>(base);
      const bool guest = violation && address >= host_base && address < host_base + kGuestArena;
      Reader reader(base);
      uint32_t stack_end = 0, stack_base = 0;
      StackRange(reader, *ctx, &stack_end, &stack_base);
      Text stack;
      stack << (violation ? (access == 1 ? "read" : access == 2 ? "write" : "access") : "illegal instruction");
      if (guest) {
        stack << " of guest 0x";
        stack.Hex(uint32_t(address - host_base));
      }
      stack << " on guest thread 0x";
      stack.Hex(state->thread_id()) << ": r1 0x";
      stack.Hex(ctx->r1.u32) << ", lr 0x";
      stack.Hex(uint32_t(ctx->lr)) << ", stack 0x";
      stack.Hex(stack_end) << "..0x";
      stack.Hex(stack_base) << "; ";
      ReturnChain(reader, uint32_t(ctx->lr), ctx->r1.u32, stack_end, stack_base, stack);
      incident::GuestStack(stack.View());
      if (guest) {
        Text heap;
        HeapReport(reader, uint32_t(address - host_base), heap);
        incident::GuestHeap(heap.View());
      }
    }
  }
  observing = false;
  return false;
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

namespace {
std::atomic<uint64_t> guest_fault_swap{0};
}  // namespace
void ArmGuestFault(uint64_t swap) { guest_fault_swap.store(swap, std::memory_order_relaxed); }
bool GuestFaultDue(uint64_t swap) {
  const uint64_t due = guest_fault_swap.load(std::memory_order_relaxed);
  return due && swap >= due;
}

void Install() {
  static bool installed = false;
  if (installed) return;
  installed = true;
  rex::arch::ExceptionHandler::Install(Handler, nullptr);  // After the SDK's own.
}
}  // namespace broken_bond::crash_report
