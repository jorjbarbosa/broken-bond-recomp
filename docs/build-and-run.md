# Build and run

[Documentation](README.md) · [Configuration](configuration.md) · [Renderers](renderers.md)

This guide describes the project's **Windows x64 / Release** workflow. Run the commands from the repository root in PowerShell 7. The scripts load the Visual Studio x64 build environment themselves when it is not loaded. Stop the sequence if any command fails.

## 1. Prerequisites

| Tool | Requirement |
| --- | --- |
| Git | With submodule support and symbolic links enabled (Windows Developer Mode, or an elevated shell) |
| Visual Studio 2022 or Build Tools | The C++ workload, with the x64 tools and a Windows SDK |
| LLVM/Clang | `clang` and `clang++` on `PATH`; 19.1.5 is the validated version |
| CMake | **3.26+**, for the version 6 presets |
| Ninja | On `PATH` |
| PowerShell | 7 or newer (`pwsh`); Windows PowerShell 5.1 is not supported |
| Python | 3.x on `PATH`, only for the native renderer |
| Game files | An ISO or an extracted copy of the supported revision (see [the README](../README.md#requirements)) |

The SDK is built for x86-64-v2 processors (SSE4.2). The first SDK configuration and the extraction script download dependencies, so an internet connection is required.

Check your environment:

```powershell
git --version
clang --version
cmake --version
ninja --version
$PSVersionTable.PSVersion
```

## 2. Fetch the SDK

The official ReXGlue SDK is the `sdk` submodule, pinned to a specific commit. The menus of the native build use [RmlUi](https://github.com/mikke89/RmlUi) 6.3 and [FreeType](https://freetype.org) 2.15.0, also pinned submodules, in `third_party/rmlui` and `third_party/freetype`; the build applies a local RmlUi fix to a copy in the build folder (`cmake/rmlui-patches/`), so the submodule stays clean. Fetch the submodules with symbolic links enabled, which some of the SDK's dependencies need:

```powershell
git -c core.symlinks=true submodule update --init --recursive
```

If the submodules were fetched without symbolic links, `build-sdk.ps1` stops with a message about the `libmspack` checkout; see [Troubleshooting](troubleshooting.md#build-issues).

## 3. Prepare the game files

Place the ISO of your copy in the repository root and extract it:

```powershell
./scripts/extract-game.ps1
```

The script downloads a pinned [extract-xiso](https://github.com/XboxDev/extract-xiso) release, verifies its SHA-256, and writes the disc contents to `game/`. It refuses to write into a folder that already has files. Without exactly one ISO in the root, pass it with `-IsoPath`; `-Destination` writes elsewhere. Extraction only needs to run once.

The result is the disc contents directly in `game/`, without an extra enclosing folder:

```text
game/
├── default.xex
├── xeshader.sdb
└── ...            # remaining disc files and folders
```

The build checks the SHA-256 of `default.xex` and stops on any other revision: the recompilation configuration and the native renderer's hooks use addresses of this revision.

Game files, extracted assets, and anything derived from them stay out of Git.

## 4. Build and install the SDK

```powershell
./scripts/build-sdk.ps1
```

The script applies the patches in `patches/` to the `sdk` submodule, in the order of `patches/series`, builds the SDK with Clang, and installs it in `build/sdk-install`. Patches that are already applied are detected and left as they are; a patch that neither applies nor is found applied stops the build without touching the SDK's local changes. Use `-Jobs` to limit parallelism and `-Configuration Debug` or `RelWithDebInfo` to install another configuration next to Release.

| Patch | Purpose |
| --- | --- |
| `rexglue-enable-usbcam.patch` | Enables the USB camera exports the game imports. The camera is reported as disconnected; capture is not implemented |
| `rexglue-discovery.patch` | Discovers functions reached only through pointer tables without RTTI or through addresses built in code, and adds the guest return address to missing-function errors. See [Automatic function discovery](automatic-function-discovery.md) |
| `rexglue-xma-decoder.patch` | XMA decoder fixes ported from Xenia Canary, plus local fixes for frames that cross a buffer boundary. The patch header lists each change |
| `rexglue-mnk-modifiers.patch` | The keyboard and mouse driver: standalone Shift/Control/Alt bindings work alongside movement, and the driver takes the bindings of the player it is assigned to |
| `rexglue-d3d12-fence-check.patch` | Checks the queue signal and the fence event of the Xenos D3D12 command processor separately, so a successful signal no longer skips registering the event before the unbounded wait |
| `rexglue-content-unread-header.patch` | Lists a content package only when its `.header` was read, so a save folder without one is no longer listed with an unread name |
| `rexglue-disc-image-game-root.patch` | Accepts a disc image as the game data root and mounts it in place, so the game reads an ISO without extracting it |
| `rexglue-controls-v4.patch` | Stable device identities and the assignment of controllers and keyboard and mouse to the four players, in the SDL and XInput drivers |
| `rexglue-sdl-initial-gamepads.patch` | The SDL driver also takes the controllers connected before it started, which the setup screen may have opened first |
| `rexglue-content-package-path-case.patch` | Opens a content package or its header under the existing name that matches ignoring case, on case-sensitive file systems |
| `rexglue-linux-procmap-query.patch` | On Linux, looks up the mapping of one address with `PROCMAP_QUERY` instead of reading all of `/proc/self/maps` in the write-fault handler |
| `rexglue-native-window-handle-sdl.patch` | Outside Windows, the native window handle is the SDL window, whose properties give the X11 or Wayland handles |
| `rexglue-present-effect-without-fidelityfx.patch` | Without FidelityFX, `present_effect` still accepts `cas` and `fsr` and their settings, which the native renderer draws itself |
| `rexglue-query-committed-range-access.patch` | A guest memory range check that also requires every page to be committed |
| `rexglue-posix-multiwait-sleep-precision.patch` | POSIX waits keep their sub-millisecond remainder instead of spinning on zero-time waits |

Rebuild the SDK with this script after changing any patch. On Linux, `scripts/build-sdk.sh` applies the same list and builds the SDK with the `linux-amd64` preset into the same folders.

## 5. Generate the C++

```powershell
./scripts/build-game.ps1
```

The script verifies `default.xex` and runs the ReXGlue code generation on `broken_bond_manifest.toml` (output in `generated/`, ignored by Git). The game itself is built in the next step.

Code generation runs again when the installed SDK changes, even if its version string does not, because the local patches can change the generated code. Use `-Regenerate` to force it, `-Jobs` to limit parallelism, and `-SdkPrefix` to use another installed SDK.

## 6. Native renderer

The game, with its [native renderer](renderers.md), setup screen and menus, is built into `build/native`. It reuses the code generated in the previous step.

It needs:

- a clean [Plume](https://github.com/renderbag/plume) checkout at commit `d72379344dacd3dbf9f810f92ddc87e6de1845b1`, with its `contrib/D3D12MemoryAllocator`, `contrib/volk`, `contrib/Vulkan-Headers`, and `contrib/VulkanMemoryAllocator` submodules at the revisions in [`tools/native-renderer/dependencies.json`](../tools/native-renderer/dependencies.json). Configuration stops if one of them is at another commit or has local changes;
- XenosRecomp's dependencies (`fmt`, `dxc-bin`, `smol-v`, `xxHash`, and `zstd`) at the revisions in [`tools/native-renderer/dependencies.json`](../tools/native-renderer/dependencies.json), cloned into `tools/xenosrecomp/thirdparty/`, where Git ignores them;
- `game/xeshader.sdb`, from the extracted game files;
- Python 3 on `PATH`;
- `spirv-val` from [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) (the Vulkan SDK's, or a local build) on `PATH`, or given with `-SpirvVal <path>`.

```powershell
git clone https://github.com/renderbag/plume.git out/plume
git -C out/plume checkout d72379344dacd3dbf9f810f92ddc87e6de1845b1
git -C out/plume submodule update --init contrib/D3D12MemoryAllocator contrib/volk contrib/Vulkan-Headers contrib/VulkanMemoryAllocator

$deps = (Get-Content tools/native-renderer/dependencies.json -Raw | ConvertFrom-Json).xenosrecomp_thirdparty
foreach ($name in 'fmt', 'dxc-bin', 'smol-v', 'xxHash', 'zstd') {
  git clone $deps.$name.url "tools/xenosrecomp/thirdparty/$name"
  git -C "tools/xenosrecomp/thirdparty/$name" checkout $deps.$name.revision
}

./scripts/build-game-native.ps1 -PlumeSource out/plume -XenosRecompThirdparty tools/xenosrecomp/thirdparty
```

The game is built with both graphics APIs, Direct3D 12 and Vulkan. Direct3D 12 is the default; **Settings → Display → Graphics API** switches to the other one. `-Backend d3d12` or `-Backend vulkan` builds only one of them, for development; packaging requires both.

The build translates the game's shaders with the XenosRecomp fork in `tools/xenosrecomp/`, compiles them with DXC to DXIL and to SPIR-V (checked by `spirv-val`), and embeds the result in `broken_bond.exe`, so no shader compiler runs during play. The first build translates every shader of `xeshader.sdb` and takes several minutes; `build/shader-store` keeps the results, and later builds reuse them while the game files, the fork, and DXC do not change. The shader cache derives from your game files and stays out of Git.

### Pipeline list

The list of pipelines compiled at startup, `src/gpu/shaders/pipeline_cache.inc`, is optional and also stays out of Git, because it is generated from local play sessions. Without it, the build succeeds, and each pipeline compiles the first time it appears, with a short pause.

Every native run started by `run-game.ps1` leaves the pipelines it created in `build/pipeline-runs/<run>/pipelines.json`. To add them to the list and embed it:

```powershell
./scripts/update-pipeline-cache.ps1
./scripts/build-game-native.ps1 -PlumeSource out/plume -XenosRecompThirdparty tools/xenosrecomp/thirdparty
```

The script starts from the current list, so its entries and order are kept. `-Sources` builds the list from the given files only.

## 7. Run

### Directly

```powershell
./build/native/broken_bond.exe
```

A build in `build/native` finds the repository's `game/` folder and keeps saves and caches in `build/user/`. It opens on the setup screen: **Play** starts the game, **Settings** and **DLC** open those pages, and the game folder or an ISO can be chosen under **Settings → Game**. Without a supported game, the first entry chooses one instead of playing. The log goes to `build/native/logs/`. See [Configuration](configuration.md).

The startup menu can be turned off under **Settings → Advanced**; hold Shift while opening, or pass `--setup`, to open it again.

### From the repository

```powershell
./scripts/run-game.ps1                    # the build of build-game-native.ps1
./scripts/run-game.ps1 -Keyboard          # with the SDK's keyboard controller emulation
```

The script uses `game/` for the game files and `build/user/` for saves and caches, and writes the log to `build/logs/runtime-<date>.log`. The startup menu shows first unless it is turned off. `-LogLevel debug` or `trace` adds detail. It also writes the renderer's journal next to the log (`.native.jsonl`) and the run's pipelines to `build/pipeline-runs/<run>/`.

Keyboard defaults include Enter for Start, Space for A, Backspace for B, and WASD for the left stick.

### With another game folder

```powershell
./build/native/broken_bond.exe --game_data_root="C:/path/to/game"
./build/native/broken_bond.exe --game_data_root="C:/path/to/game.iso"
```

`--game_data_root` takes an extracted folder or an ISO. Without `--user_data_root`, saves and caches stay in `build/user/` for a build in `build/native`, and in `user/` beside the executable elsewhere.

## Tests

| Command | What it checks |
| --- | --- |
| `./scripts/build-game-native.ps1 ... -BuildDirectory build/native-tests -Tests` | Builds a tree with the host tests and runs them all with `ctest`: the renderer engine on Vulkan, the menus, the configuration, the setup screen, the game data identity, DLC and the controls. A few need the extracted game in `game/` |
| The packaging tests | The ZIP, the release gate and the bridge installer; see [Release packaging](../packaging/README.md#tests) |
| `python -I tools/native-renderer/test_sdb_extraction.py` | The shader container extraction of the native renderer's build, with synthetic data |
| The `[pointer_table]` and `[code_address]` SDK unit tests | The discovery patch; see [Automatic function discovery](automatic-function-discovery.md) for how to build and run them |

A successful build or passing tests do not establish that the game runs; run it and check the log.

## Package

```powershell
./packaging/package-release.ps1          # the ZIP, from build/native
./packaging/package-bridge.ps1           # the bridge for 0.1.0-beta installations (Inno Setup 6.4+)
```

See [Release packaging](../packaging/README.md).
