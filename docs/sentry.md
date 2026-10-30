# Crash reports (Sentry)

The game can send crash reports to the project's developers through [Sentry](https://sentry.io), with the player's consent. Every session also keeps a local record, whatever the answer; see [Local diagnostics](crash-reports.md).

## The question and the setting

Nothing is sent until the player answers. With the native renderer, the first frame of the game asks **Send crash reports?** with **Don't send** (selected; also Enter and B/Esc) and **Send reports**. The game input and the game menu wait for the answer.

- **Send reports** turns uploads on in the same session; that session sends only what it captured itself.
- **Don't send** keeps the reports on the computer.

The answer is saved in the data folder's `broken_bond.toml` as `crash_reporting` with `crash_reporting_asked = true`, and the question does not return. `crash_reporting = true` from an earlier version, or `--crash_reporting` on the command line or `REX_CRASH_REPORTING` in the environment (tests and scripted starts), counts as an answer. **Settings > Advanced > Diagnostics > Send crash reports** changes it later: turning it off stops new uploads at once (a report already on its way may still arrive); turning it on after a refusal needs a restart, and never sends the sessions from before.

## What is sent, and where

With consent, a crash report holds:

- the crash dump of the game process, written by Crashpad from its own process: the thread stacks from their stack pointers and the memory they point to, which can contain personal information from the process;
- the session log, the session's diagnostic channel (`channel.bin`, the events of the incident record), the system version, the processor, the graphics card and its driver;
- file paths, which may include the user name.

When the native renderer stops on a failure, the game sends an error event with the failed operation and up to 4 KiB of its evidence text. It also sends the startup time of one start in ten, and the start and end of each session (Sentry's release health). Saves, game files and the settings file are never sent.

The reports go to the Sentry organization `naruto-rise`, project `brokenbond`, in Sentry's US region (`ingest.us.sentry.io`). Sentry receives the connection's IP address; the project is set not to store IP addresses. The user ID in a report is a random value that Sentry Native makes for each session. They carry the release `broken_bond@<version>+<commit>` and the environment `production`. To stop sending, choose **Don't send** at the question or turn the setting off; local reports stay on the computer either way.

## Build

`cmake/sentry.cmake` builds the reports into the native game only (the Xenos investigation host has none). The first configuration downloads the official Sentry Native 0.17.4 release archive, with its SHA-256 checked:

- **Windows:** the game's `clang++` driver builds Sentry Native apart with `clang-cl` (Crashpad needs the MSVC frontend there), in RelWithDebInfo, and stages `sentry.dll`, `crashpad_handler.exe` and `crashpad_wer.dll` with their PDBs beside the executable.
- **Linux:** Sentry Native and Crashpad build static in the game's tree, with the curl transport; install the libcurl development package (`libcurl4-openssl-dev` on Ubuntu). The game and `crashpad_handler` then need the system's `libcurl.so.4`.

`sentry-notices.txt`, the notices of Sentry Native and what it bundles, is written in the build folder; the packages put it in `licenses/sentry-native.txt`.

| CMake option | Default | Purpose |
| --- | --- | --- |
| `BROKENBOND_ENABLE_SENTRY` | `ON` | Builds the reports (the player's consent is still needed) |
| `BROKENBOND_SENTRY_DSN` | the project's public DSN | The ingestion destination; it is not a credential |
| `BROKENBOND_SENTRY_ENVIRONMENT` | `production` | The environment of the events |

At run time, `SENTRY_DSN`, `SENTRY_RELEASE` and `SENTRY_ENVIRONMENT` replace the build's values; none of them bypasses the consent. The release's commit is read at every build, with `-dirty` when tracked files other than Markdown differ from it.

Three SDK patches support the reports: `rexglue-sentry-close-hook` (the game flushes the reports before the SDK's hard exit), `rexglue-sentry-signal-chain` and `rexglue-seh-signal-chain` (on Linux, a fatal signal the SDK does not resume reaches Crashpad, also once the runtime's SEH handler is installed).

## Symbols

Readable stacks need the exact debug files of the distributed build: the PDBs on Windows, the ELF line tables on Linux. The SDK builds with line tables (`scripts/build-sdk.ps1` and `build-sdk.sh`), and on Windows `rexruntime.dll` gets its PDB; the game's code and its generated guest code have line tables too.

`packaging/package-release.ps1` archives each binary with its matching PDB (GUID and age checked) in `out/release-symbols/<version>-<id>`, outside the ZIP, and stops the release when one is missing. On Linux, run the archive by hand:

```bash
python3 scripts/archive-native-symbols.py build/linux out/release-symbols/<version>-linux
```

Upload an archive to the project with [sentry-cli](https://docs.sentry.io/cli/installation/) and an organization token in `SENTRY_AUTH_TOKEN`, set in the environment, never in a file, a command line or a chat:

```powershell
python scripts/upload-sentry-symbols.py out/release-symbols/<archive> --dry-run
python scripts/upload-sentry-symbols.py out/release-symbols/<archive>
```

The script uploads only that archive's binaries and debug files (`debug-files upload --no-sources --wait`), never source files or game data. Upload before publishing the release.

## Checking a build

`--sentry_test=error` sends a test event (a caught `std::runtime_error`, "Broken Bond Sentry verification error") through the normal start, and `--sentry_test=crash` aborts the process for a crash report. Both need `--crash_reporting=true`, stop before the game, and send to the configured project, so use a new data folder and the environment `sentry-setup`:

```powershell
$env:SENTRY_ENVIRONMENT = 'sentry-setup'
build/native/broken_bond.exe --data_root=<new folder> --crash_reporting=true --sentry_test=error
```

Without `--crash_reporting=true` it exits with code 2 and sends nothing. For development without sending, point `SENTRY_DSN` at a local endpoint (`http://public@127.0.0.1:<port>/1`), as the incident tests do. An event ID in the log means the event was queued, not that Sentry received and symbolicated it: check the event in Sentry.

[Back to the documentation index](README.md)
