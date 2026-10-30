# Local diagnostics

Every session of the game leaves a record on the computer, whatever the answer to the crash report question: how it ended, its log, and the crash dump when it crashed. Nothing in this record is uploaded; [Crash reports (Sentry)](sentry.md) describes what is sent with consent.

## The supervisor

`broken_bond.exe` (the AppImage on Linux) starts the game as a second process of itself and watches it. The first process, the supervisor, writes the session in the data folder's `incidents/session-<time>-<process>/` and completes it when the game ends, however it ends: a normal exit, an error, a crash, or a forced termination. Crashpad writes the crash dump from its own process, so a crash in the game cannot stop the capture.

If the incident folder cannot be written, a message says that local diagnostics could not start, and the game runs without the supervisor.

## A session's folder

| File | Holds |
| --- | --- |
| `manifest.json` | The session's state and classification, the exit code, the duration, the settings at the start, the first failure, and each file below with its size and SHA-256 |
| `events.jsonl` | The phases of the start and of the native renderer, one JSON line each |
| `session.log` | The session's log, its last 2 MiB |
| `stderr.log` | The game's standard error, its first 2 MiB |
| `native-dumps/*.dmp` | The crash dump, written by Crashpad |
| `guest-context.json` | For a crash in the game code: the faulting address, the guest thread, the registers r0–r31, LR and CTR, the chain of return addresses, and for an address in the game's heap a summary of that heap |
| `snapshot-<n>.dmp` (Windows), `snapshot-<n>.core` (Linux) | Thread snapshots, see below |
| `capture/` | Crashpad's own database and upload queue; the supervisor copies its dumps to `native-dumps/` |
| `channel.bin` | The memory the game and the supervisor share during the session |

The classification in `manifest.json` is one of:

| Classification | Meaning |
| --- | --- |
| `normal-exit` | The game closed normally |
| `explicit-failure` | The game stopped on an error it reported, such as a native renderer stop; `first_failure` names it |
| `native-exception` | The game crashed and Crashpad wrote a dump |
| `interruption-undetermined` | The game ended without a dump or a reported error: a process ended from outside (such as a hang closed in the Task Manager), or a supervisor that did not survive |
| `process-start-failed` | The game process did not start |

## After a session that failed

At the next start, the log names the failed session in one line, `Previous session did not end normally:`, with its classification, its exit code, whether a dump was captured, the first failure, and for a crash in the game code the return chain and the heap summary. The log is what players attach to reports, so this line carries the crash report of the session before. The game also shows a notice once, with a button that opens that session's folder.

## Settings > Advanced > Diagnostics

| Row | Action |
| --- | --- |
| **Open diagnostics folder** | Opens `incidents/` |
| **Capture thread snapshot** | Records the state of every thread while the game keeps running; use it while the game stutters or seems stuck |
| **Export previous session** | Packs the newest completed session, and the newest failed one when it is older, into `incident-exports/<session>.zip` in the data folder, and opens that folder |

The supervisor also takes one snapshot by itself when the game makes no progress for 120 seconds, except while the setup screen is open. On Windows, a separate process writes the snapshot as a minidump. On Linux, the snapshot is a core file written by gdb, which must be installed at `/usr/bin/gdb`; without it the row says so and the game keeps running. Core files can be large.

The export checks each file against the hash in `manifest.json` and stops above 256 MiB; share the session folder itself in that case. It uploads nothing.

## Retention

At each start, the supervisor keeps the newest completed sessions within 32 sessions, 256 MiB and 14 days, and deletes the rest. It never deletes a session whose supervisor is still running; a session left open by a supervisor that no longer exists is completed as `interruption-undetermined` first. Exports in `incident-exports/` are not deleted.

## Privacy

Crash dumps, snapshots and logs can hold personal data: memory of the process, file paths with the user name, and the names of devices. They stay on the computer. Review the files before sharing an export or a folder.

## Tests

`ctest -R incident` in a native build runs the supervisor, the export, the consent rules and a boot that crashes on purpose, with a local endpoint in place of Sentry. `--incident_test=<mode>` (internal) crashes, hangs or exits a disposable process in a given way for those tests.

[Back to the documentation index](README.md)
