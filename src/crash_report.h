// broken_bond - ReXGlue Recompiled Project
//
// Crash report of a guest fault (crash_report.cpp): a handler in the SDK's
// exception chain, after its own, for a fault the SDK does not resume. It
// publishes into the session's incident channel (diagnostics/incident.h),
// which the supervisor keeps when the game dies: first a fixed snapshot of the
// fault and the guest registers, then the chain of return addresses of the
// guest stack and, for a fault in guest memory, the title's heap that holds
// the address. Nothing in it formats with fmt, allocates, locks or logs, so a
// fault inside the heap or the logger cannot stop it before Crashpad's dump.

#pragma once

#include <cstdint>

namespace broken_bond::crash_report {
// Installs the handler once; from OnPostSetup, after the SDK's own handler.
void Install();
// A heap the title allocates from or frees to (the mspace in r3 of the
// allocator's calls), for the heap part of the report. Cheap enough for every
// call: a thread repeating its last heap returns at once.
void NoteHeap(uint32_t heap);
// The developer fault of --incident_test=guest-av: from this swap on, the
// game's Swap thread writes to guest page 0 (GuestFaultDue).
void ArmGuestFault(uint64_t swap);
bool GuestFaultDue(uint64_t swap);
}  // namespace broken_bond::crash_report
