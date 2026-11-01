# Troubleshooting

[Documentation](README.md) · [Build and run](build-and-run.md) · [Renderers](renderers.md#known-issues)

## Build issues

| Symptom | What to check |
| --- | --- |
| `Get-FileHash` or other commands are not recognized | The script ran in Windows PowerShell 5.1. Run it from PowerShell 7 (`pwsh`) |
| `Visual Studio Build Tools with the C++ workload are required` | Install Visual Studio 2022 or its Build Tools with the C++ workload, the x64 tools, and a Windows SDK |
| `clang` or `ninja` not found | Put LLVM and Ninja on `PATH`; the scripts load the Visual Studio environment, not these tools |
| Preset parsing error | Use CMake 3.26+; the presets use schema version 6 |
| `The libmspack checkout contains symlink placeholders` | The SDK submodules were fetched without symbolic links. Enable them (Windows Developer Mode, or an elevated shell), then fetch the SDK again: `git submodule deinit -f sdk` and `git -c core.symlinks=true submodule update --init --recursive`. The SDK build reapplies the patches |
| `SDK patch ... cannot be applied or verified` | The `sdk` checkout is at another commit or has other changes. Compare it with the pinned commit and the patch; the script keeps your changes instead of forcing the patch |
| `The game executable does not match the supported revision` | `game/default.xex` is another revision. Only the revision in the [README](../README.md#requirements) is supported |
| `Build the SDK with scripts/build-sdk.ps1 ...` | Build the SDK first, or pass `-SdkPrefix` with an installed SDK |
| Out of memory during compilation | Lower the parallelism, for example `-Jobs 2` |

### Native renderer

| Symptom | What to check |
| --- | --- |
| `plume source must be clean at ...` or `allocator source must be clean at ...` | Check out the pinned commits in Plume and its `contrib/D3D12MemoryAllocator` submodule, without local changes; see [Build and run](build-and-run.md#6-native-renderer) |
| `The shader cache needs game/xeshader.sdb` | Extract the game files into `game/` first |
| `The native renderer mapping only supports the documented Broken Bond executable` | Same as the revision check above |
| Python not found during configuration | Install Python 3 and put it on `PATH` |
| DXC, fmt, or another XenosRecomp dependency not found | Clone the five dependencies into `tools/xenosrecomp/thirdparty/` and pass that folder to `-XenosRecompThirdparty` |
| An updated pipeline list is not embedded | `update-pipeline-cache.ps1` marks `src/gpu/shaders/pipelines.cpp` for rebuilding. After copying a list by hand, change that file's timestamp, or the build keeps the old list |

## Runtime issues

### The setup screen cannot find the game

Choose your ISO, or the `default.xex` of the extracted copy, with the setup screen's first entry or under **Settings → Game**. In an extracted copy, `default.xex` must sit directly in the chosen folder. The screen names what is wrong with a game it refuses, such as another revision or missing files. You can also set `custom_game_root` in `broken_bond.toml`, or start the game with `--game_data_root`.

### The setup screen opens at every start

It is the startup menu: turn it off under **Settings → Advanced**. The screen also opens on its own after a start that failed before its first frame, which often means the graphics API or card cannot start; pick the other API under **Settings → Display**.

### A copy of 0.1.0-beta still opens the old launcher

Update it with the bridge installer, `BrokenBondInstaller.exe`, which removes the launcher and points the shortcuts at `broken_bond.exe`, or replace the files with those of the ZIP and start `broken_bond.exe`. See [Release packaging](../packaging/README.md#bridge-for-010-beta-installations).

### The game closes at startup or during play

`run-game.ps1` reports the exit code and the log path. The session's record in the data folder's `incidents/` classifies how it ended, and the next start's log names a failed session in a `Previous session did not end normally` line; see [Local diagnostics](crash-reports.md). Look for these lines near the end of the log:

- **A missing function** (an indirect call to an address with no recompiled function). The message has the guest target, return address, and stack pointer. Discovery is conservative, and such a miss is worth an issue with the log.
- **`Unhandled guest access violation`**: a crash in the game code. The session's `guest-context.json` names the faulting address, the guest thread, its registers, and the chain of return addresses, and for an address in the game's heap it also summarizes that heap; the next start's log repeats the return chain and the heap summary. Report it with the session's export and what you were doing.
- **A native renderer stop**: the renderer closes the game with a message and writes the reason in the log, the journal and the session's record. Report it with the session's export.

### No sound

The SDK opens the Windows default playback device when the game starts. The log names it in the `audio endpoint` line. If sound plays on another device, such as a monitor's speakers, make the intended device the Windows default and start the game again.

### The title screen looks different on each launch

Expected: the game picks Konoha's time of day at random each time the title loads.

### Native renderer

| Symptom | What to check |
| --- | --- |
| Short pauses in a new area | Pipelines compile the first time they appear on your driver; later visits do not pause. See [Pipelines](renderers.md#pipelines) |
| A native option has no effect | Set it in the game menu, where most options apply at once. Edited by hand, options are read at startup: close the game, edit `broken_bond.toml` in the data folder, and start again; see [Native renderer options](configuration.md#native-renderer-options) |
| The game stops during startup with `REX_NATIVE_DIAGNOSTICS=1` | Known; leave the variable unset |

## Collecting diagnostics

| Started with | Log |
| --- | --- |
| `scripts/run-game.ps1` | `build/logs/runtime-<date>.log`, and the native renderer's journal next to it (`.native.jsonl`) |
| `broken_bond.exe` directly | The data folder's `logs/`: `data\logs\` next to the executable, `build/data/logs/` in a repository build, `~/.local/share/broken_bond/logs/` for the AppImage |
| A start that stops before the game, naming the data folder | `%TEMP%\broken_bond\bootstrap\` (`/tmp/broken_bond/bootstrap/` on Linux); see [Data folder](data-folder.md) |

For more detail, use `run-game.ps1 -LogLevel debug`, or set `log_level = "debug"` in the data folder's `broken_bond.toml` and restore `"info"` afterwards.

Every session also leaves a record in the data folder's `incidents/`: the log, the crash dump when the game crashes, and how the session ended. **Settings → Advanced → Diagnostics → Export previous session** packs the latest one in a ZIP to attach to a report, and **Capture thread snapshot** records the threads of a game that stutters or seems stuck. See [Local diagnostics](crash-reports.md).

Crashpad writes the crash dump whenever its handler starts, whether or not reports are sent; `REX_NATIVE_CRASH_DUMP` is then ignored, and the session's `events.jsonl` records `legacy-full-dump` as skipped. In a build without crash reports (`-DBROKENBOND_ENABLE_SENTRY=OFF`) or without `crashpad_handler.exe` on Windows, set `REX_NATIVE_CRASH_DUMP=stack` before starting the game: the crash then writes `crash-stack.txt`, the host stack, which follows the game's call chain. `REX_NATIVE_CRASH_DUMP=1` also writes a full minidump (`crash.dmp`). The files go to the run's folder in `build/pipeline-runs/` with `run-game.ps1`, or to the folder in `REX_NATIVE_FRAME_DIR`, or to the working directory.

`python -I tools/native-renderer/summarize-native-journal.py <journal>` summarizes a native renderer journal.

When reporting a problem, include:

- the commit you built, Windows version, CPU, GPU, and driver version;
- the renderer, and the native renderer's options if you changed them;
- reproduction steps and the expected result;
- the voice language and the game language;
- the log, or for a crash, a hang or a stop, the session's export, reviewing personal paths before sharing.
