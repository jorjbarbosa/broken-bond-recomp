// broken_bond - ReXGlue Recompiled Project
//
// Crash report of a guest fault, in the log (crash_report.cpp): the vectored
// exception handler after the SDK's writes, for a fault the SDK does not
// resume (the "Unhandled guest access violation" line), the guest thread, its
// registers and the chain of return addresses of the guest stack with the
// repetitions counted, so a crash explains itself without a dump. For a fault
// in guest memory it also describes the title's heap that holds the address.

#pragma once

#include <cstdint>

namespace broken_bond::crash_report {
// Installs the handler once; from OnPostSetup, after the SDK's own handler.
void Install();
// A heap the title allocates from or frees to (the mspace in r3 of the
// allocator's calls), for the heap part of the report. Cheap enough for every
// call: a thread repeating its last heap returns at once.
void NoteHeap(uint32_t heap);
}  // namespace broken_bond::crash_report
