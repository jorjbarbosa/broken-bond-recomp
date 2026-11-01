# Project architecture

[Documentation](README.md)

This page describes the code and scripts in the checkout. Features found in the code are not a validation of every part of the game; see the [README](../README.md#current-state) for what has been played.

## Overview

```text
Xbox 360 files + manifest + codegen config
                  │
           ReXGlue codegen
                  │
           C++ in generated/
                  │
        Clang + CMake + Ninja
                  │
     broken_bond.exe + SDK runtime
                  │
  Setup screen → game execution
```

PowerPC code is translated ahead of time into C++. The ReXGlue runtime provides the services the original game needs, including guest memory, kernel, content, audio, and input. The game draws with the native renderer in `src/gpu/`, which issues the game's Direct3D calls with Direct3D 12 or Vulkan through Plume. The setup screen and the game menu are RmlUi documents drawn by the same renderer.

## Structure

| Path | Responsibility |
| --- | --- |
| `broken_bond_manifest.toml` | The executable to recompile and the code generation output |
| `config/codegen.toml` | Function boundaries the analysis cannot find by itself |
| `generated/` | Local code generation output, ignored by Git |
| `src/main.cpp`, `src/broken_bond_app.h` | Runtime integration: the app, its input system, and the native renderer's setup and close |
| `src/input_replay.cpp` | Controller input recording and replay for repeatable test runs |
| `src/app_entry.cpp`, `src/diagnostics/` | The entry point, which runs the game under the session supervisor, and the local session records; see [Diagnostics](#diagnostics) |
| `src/sentry_reporting.*`, `src/crash_report.*`, `cmake/sentry.cmake` | The opt-in crash reports to Sentry, and the report of a crash in the game code |
| `src/gpu/` | Native renderer (built with `BROKENBOND_NATIVE_RENDERER=ON`); see [Native renderer](#native-renderer) |
| `src/studies/` | Study hooks, inactive unless their environment variable is set; see [Studies](#studies) |
| `src/menu/` | The setup screen and the game menu over RmlUi, and the settings code: the configuration file, languages, graphics cards, DLC, the update check |
| `src/controls/` | The four players' controls: profiles, devices, and the drivers they configure |
| `src/game_image.cpp` | Identifies the game data in a folder or an ISO |
| `ui/` | The menus' documents, styles and fonts, embedded in the executable, and the icon |
| `tests/` | Host tests of the menus, the configuration, the game data and the controls |
| `third_party/` | RmlUi, FreeType and nlohmann/json, pinned submodules |
| `packaging/` | The release ZIP, the bridge installer for 0.1.0-beta, and their tests |
| `launcher/` | The retired launcher's sources and artwork, outside the build |
| `sdk/` | The ReXGlue SDK submodule, pinned to a commit |
| `patches/` | Local SDK changes, applied in the order of `patches/series` by `scripts/build-sdk.ps1` and `scripts/build-sdk.sh` |
| `scripts/` | Extraction, SDK, game and native renderer builds; running; the pipeline list |
| `tools/native-renderer/` | Native renderer build support: Plume integration, shader cache generation, and the pipeline list |
| `tools/xenosrecomp/` | XenosRecomp fork that translates the game's shaders for the native renderer |
| `game/` | Local game files, ignored by Git |
| `build/` | Local builds, logs, user data, and caches, ignored by Git |

## Code generation

`scripts/build-game.ps1` runs `rexglue codegen` on the manifest. The configuration in `config/codegen.toml` has only two function boundaries, each one a shared control-flow tail that the analysis would otherwise split; the [recompilation notes](recompilation-notes.md) give the evidence for both.

Every other function is found automatically. The SDK finds functions through calls, the gaps between known functions, and virtual tables with RTTI metadata; `rexglue-discovery.patch` adds two passes: pointer tables without RTTI in `.rdata`, and functions whose address is built in code (such as `qsort` comparators). Both reject labels inside existing functions and exception handler ranges. [Automatic function discovery](automatic-function-discovery.md) explains the rules and their tests.

The build script records the hashes of the installed `rexglue.exe` and `rexruntime.dll`, and generates the code again when they change, because a local SDK patch can change the output without changing the SDK's version string.

## SDK patches

| Patch | Purpose |
| --- | --- |
| `rexglue-enable-usbcam.patch` | Enables the USB camera exports the game imports; the camera is reported as disconnected |
| `rexglue-discovery.patch` | The discovery passes above, their unit tests, and the guest return address and stack pointer in missing-function errors |
| `rexglue-xma-decoder.patch` | XMA decoder fixes |

The XMA decoder patch ports fixes from Xenia Canary (the work loop's exit when it makes no progress, the drain of the current frame, output buffer invalidation, and two packet parsing fixes) and adds local ones for this game's streamed audio. The game feeds its voices and music one packet per buffer, and sends the next packet only while fewer than 2,400 of the samples it sent are still unplayed. The local fixes keep every frame that crosses a buffer boundary, its body or its header, and complete it from the next buffer, and queue each decoded block at once. Before them, a dropped frame left those samples unplayed forever, so the stream was never fed again: cutscenes waited for a voice line that never ended, and the music stopped. The patch header lists every change.

`scripts/build-sdk.ps1` (and `scripts/build-sdk.sh` on Linux) applies the patches in the order of `patches/series` and detects patches that are already applied. Rebuild the SDK after changing them. The other patches are listed in [Build and run](build-and-run.md#4-build-and-install-the-sdk).

## Native renderer

The native renderer hooks the game's Direct3D layer instead of emulating the Xbox 360 GPU from its command buffers. It comes from the Rise of a Ninja port, rewritten to this game's addresses and device offsets, with the differences of this game handled in its own code. User-facing behavior and known issues are in [Renderers](renderers.md).

| Path | Responsibility |
| --- | --- |
| `gpu/host/` | Configuration, the guest kernel's video and threading exports, loading screen and world hooks, and the frame lifecycle |
| `gpu/d3d/` | Hooks of the game's Direct3D functions: device, resources, render states, passes, draws, queries, presents, and shutdown |
| `gpu/backend/` | Direct3D 12 engine on Plume, its pipeline set, and the dispatcher that translates draws on its own thread |
| `gpu/shaders/` | Embedded shader cache and pipelines, including the list compiled at startup |
| `gpu/textures/`, `gpu/buffers/` | Texture formats, tiling, write watching, and uploads; vertex and index buffers, including instanced draws |
| `gpu/common/` | Shared helpers: guest memory access, hashes, and the loading state |
| `gpu/diagnostics/` | Draw census, records, frame captures, measurements, and crash reports |
| `gpu/settings.cpp` | Graphics options, under the SDK's names where the SDK has one |

When a draw or operation is not covered, the renderer skips it and counts it in a census instead of stopping the game. A stop closes the game with a message and records its reason in the log and in the journal (`.native.jsonl`); `tools/native-renderer/summarize-native-journal.py` summarizes a journal.

The shader cache is generated at build time from the local `xeshader.sdb` by `tools/native-renderer/shader-cache.cmake`, using the XenosRecomp fork and DXC, and embedded in the executable. It derives from the game files, so it is never committed.

## Studies

`src/studies/` holds hooks written to investigate specific problems. They are compiled into native renderer builds and do nothing unless their environment variable names an output file:

| Variable | Records |
| --- | --- |
| `REX_RAND_TRACE` | The C runtime's random seeds and draws, and the world's loading conditions, such as the title's time of day |
| `REX_AUDIO_TRACE` | The cadence of the game's audio frames |
| `REX_XMA_STREAM_TRACE` | The sample accounting of the game's XMA streams |
| `REX_EXPORT_TRACE` | File reads and XMA voice calls (native renderer only) |

## Setup screen and menus

`broken_bond.exe` starts without a launcher. First `SetupEnvironment` prepares the [data folder](data-folder.md) (`src/data_paths.*` chooses it; `src/data_migration.*` locks it and copies earlier copies' files into it). Then, before the runtime starts, `OnConfigurePaths` takes the settings, logs, saves and cache folders from it and finds the game (the command line, `custom_game_root`, or a `game/` folder beside the executable or in the repository), and `OnFinalizePaths` opens the setup screen when it is needed. The screen runs on a temporary engine of the native renderer and hands the window to the game on **Play**. An ISO is mounted in place by an SDK patch. In game, the menu draws over the frame through the renderer's UI layer, and graphics settings reach the renderer between frames. The [packaging](../packaging/README.md) builds the ZIP and the bridge for 0.1.0-beta installations, which used a launcher. `scripts/extract-game.ps1` remains available for source builds.

## Diagnostics

In the native build, `src/app_entry.cpp` replaces the SDK's entry point. The first process becomes the supervisor (`src/diagnostics/supervisor.cpp`): it creates the session's folder in the data folder's `incidents/` and its shared memory, `channel.bin` (`channel.h`), starts the game as a second process of the same executable, and completes the record when the game ends. The game publishes its phases, failures and the context of a guest crash to the channel (`incident.cpp`); `src/crash_report.cpp` writes the registers, the return chain and the heap summary of a guest fault as fixed text, without allocation, locks or the logger. `src/sentry_reporting.cpp` starts Crashpad for every session, so the crash dump is written locally whatever the player answered, and lets uploads through only with consent. `export.cpp` packs a session into a ZIP. Three SDK patches, `rexglue-sentry-close-hook`, `rexglue-sentry-signal-chain` and `rexglue-seh-signal-chain`, let the reports flush at close and let Crashpad receive fatal signals on Linux. See [Local diagnostics](crash-reports.md) and [Crash reports](sentry.md).

## Areas to address

1. **Native renderer validation.** It has been played through the start of the story, Fight Mode and the open world on one graphics card, at 30 and 60 fps; broader gameplay and graphics-card coverage still require validation.
2. **Menu coverage.** The menus' host tests run on virtual devices; physical controllers, the bridge on users' installations and Wine need hand testing.
3. **External dependencies.** Plume and XenosRecomp's dependencies are checkouts pinned by commit outside the submodules, and the build checks the commits of Plume and its allocator only.
4. **Pipeline list.** It is generated from local play sessions and stays out of Git, so a clean clone builds without it.
5. **One game revision.** Code generation and the native renderer's hooks use addresses of one executable revision; other revisions are refused.
6. **Ported comments.** Some comments in `src/gpu/` still cite addresses of the Rise of a Ninja executable.

## Maintenance guidance

Fix code generation in the manifest, `config/codegen.toml`, or the SDK patches, not in the generated C++. Keep game files and anything derived from them out of version control, as `.gitignore` specifies. When changing the SDK, update its patch and check that it applies to a clean checkout of the pinned commit.

Runtime reports should include the renderer, the graphics card and driver, the configuration, and the log, to tell content, runtime, and presentation failures apart.
