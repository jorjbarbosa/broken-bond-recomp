# Build and run

[Documentation](README.md) · [Configuration](configuration.md) · [Renderers](renderers.md)

This guide describes the project's **Windows x64 / Release** workflow. Run the commands from the repository root in PowerShell 7. The scripts load the Visual Studio x64 build environment themselves when it is not loaded. Stop the sequence if any command fails. The native Linux build has its own [section](#linux).

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

The SDK is built for x86-64-v2 processors (SSE4.2). The first SDK configuration, the extraction script and the game's first configuration (Sentry Native, for the [crash reports](sentry.md)) download dependencies, so an internet connection is required.

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
| `rexglue-data-log-directory.patch` | The application can name the folder of its sequential logs (`PathConfig::log_dir`); the logs, the `.toml` and its log levels are read and written under non-ASCII paths, with wide log file names on Windows |
| `rexglue-data-achievements-unicode.patch` | The achievement metadata and unlock files are read and written under non-ASCII paths |
| `rexglue-input-system-lock.patch` | The input system takes one call at a time, so guest threads reading the controllers together never see its device list rebuilt under them |
| `rexglue-sentry-close-hook.patch` | The application can act when the window closes before the SDK's hard exit; the game flushes its crash reports there |
| `rexglue-sentry-signal-chain.patch` | On POSIX, a fatal signal the runtime does not resume reaches the handler installed before it, such as Crashpad's, with its original information and context |
| `rexglue-seh-signal-chain.patch` | On POSIX, the runtime's SEH emulation keeps the handlers it replaces and passes them a fault outside its regions, so Crashpad still writes the dump once the runtime has started |
| `rexglue-physical-heap-release-order.patch` | A physical heap releases its own region before its parent's pages, so another thread that takes those pages in between no longer loses its new region to this release; an address that is not a region start leaves the parent untouched |
| `rexglue-input-user-packet.patch` | Each player keeps one packet number, raised whenever the packet of any of its devices changes, so a controller change still reaches the game after the keyboard changed more often |
| `rexglue-shared-register-calls.patch` | With the non-volatile registers as locals, a call into a function that shares its caller's registers (an SEH funclet) keeps the values the context held before and gives them back after: a funclet further up the stack works in the context, and the save helpers that restored them are elided |

The SDK builds with line tables, and on Windows `rexruntime.dll` gets a PDB, so the crash reports' stacks can be read ([Symbols](sentry.md#symbols)). Rebuild the SDK with this script after changing any patch. On Linux, `scripts/build-sdk.sh` applies the same list and builds the SDK with the `linux-amd64` preset into the same folders.

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

The build also includes the opt-in [crash reports](sentry.md): `sentry.dll`, `crashpad_handler.exe` and `crashpad_wer.dll` go beside `broken_bond.exe`, which also needs them to write the crash dumps of its [local diagnostics](crash-reports.md). `-DBROKENBOND_ENABLE_SENTRY=OFF` at configuration leaves them out.

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

A build in `build/native` finds the repository's `game/` folder and keeps its settings, saves, logs and caches in the repository's data folder, `build/data/`, shared by every build inside the repository. Its first start makes it from `build/user/` and `build/native/broken_bond.toml`, which stay as they are. It opens on the setup screen: **Play** starts the game, **Settings** and **DLC** open those pages, and the game folder or an ISO can be chosen under **Settings → Game**. Without a supported game, the first entry chooses one instead of playing. The log goes to `build/data/logs/`. `--data_root <folder>` runs on another data folder for that session. See [Configuration](configuration.md) and [Data folder](data-folder.md).

The startup menu can be turned off under **Settings → Advanced**; hold Shift while opening, or pass `--setup`, to open it again.

### From the repository

```powershell
./scripts/run-game.ps1                    # the build of build-game-native.ps1
./scripts/run-game.ps1 -Keyboard          # with the SDK's keyboard controller emulation
```

The script uses `game/` for the game files and `build/data/` for settings, saves and caches, and writes the log to `build/logs/runtime-<date>.log`. The startup menu shows first unless it is turned off. `-LogLevel debug` or `trace` adds detail. It also writes the renderer's journal next to the log (`.native.jsonl`) and the run's pipelines to `build/pipeline-runs/<run>/`.

Keyboard defaults include Enter for Start, Space for A, Backspace for B, and WASD for the left stick.

### With another game folder

```powershell
./build/native/broken_bond.exe --game_data_root="C:/path/to/game"
./build/native/broken_bond.exe --game_data_root="C:/path/to/game.iso"
```

`--game_data_root` takes an extracted folder or an ISO. Saves and caches stay in the data folder: `build/data/` for a build in `build/native`, and `data\` beside the executable elsewhere. `--user_data_root` names another saves folder.

## Tests

| Command | What it checks |
| --- | --- |
| `./scripts/build-game-native.ps1 ... -BuildDirectory build/native-tests -Tests` | Builds a tree with the host tests and runs them all with `ctest`: the renderer engine on Vulkan, the menus, the configuration, the setup screen, the game data identity, DLC and the controls. A few need the extracted game in `game/` |
| The packaging tests | The ZIP, the release gate and the bridge installer; see [Release packaging](../packaging/README.md#tests) |
| `python -I tools/native-renderer/test_sdb_extraction.py` | The shader container extraction of the native renderer's build, with synthetic data |
| The `[pointer_table]` and `[code_address]` SDK unit tests | The discovery patch; see [Automatic function discovery](automatic-function-discovery.md) for how to build and run them |

A successful build or passing tests do not establish that the game runs; run it and check the log.

## Linux

The game also builds natively on Linux x86-64, without Wine or Proton: the native renderer on Vulkan only, with the window on Wayland or X11 through SDL3. The build and its host tests were checked on Ubuntu 26.04 with Clang 21 and CMake 4.2 under WSL, where the only Vulkan device is Mesa's software llvmpipe. Running the game on a Linux desktop with a GPU is not validated yet, and there is no Linux release yet.

### Prerequisites

The packages installed on the build system:

```bash
sudo apt install clang cmake ninja-build git pkg-config python3 spirv-tools nasm \
  libssl-dev libgtk-3-dev libsdl2-dev libasound2-dev libpulse-dev libwayland-dev \
  libxkbcommon-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxss-dev \
  libudev-dev libvulkan-dev libcurl4-openssl-dev
```

Running the game needs a Vulkan 1.2 driver (Mesa or the NVIDIA driver) and the Vulkan loader, which desktop distributions install by default. DXC for Linux comes with `dxc-bin`, one of XenosRecomp's dependencies, so the shaders are translated and compiled on Linux too; the SPIR-V is the same as on Windows.

### Build

The game files go in `game/` as in [section 3](#3-prepare-the-game-files): extract the ISO with [extract-xiso](https://github.com/XboxDev/extract-xiso), or copy an extracted `game/`. `sha256sum game/default.xex` must give the revision's hash from [the README](../README.md#requirements). Then, from the repository root:

```bash
git -c core.symlinks=true submodule update --init --recursive
bash scripts/build-sdk.sh

git clone https://github.com/renderbag/plume.git out/plume
git -C out/plume checkout d72379344dacd3dbf9f810f92ddc87e6de1845b1
git -C out/plume submodule update --init contrib/volk contrib/Vulkan-Headers contrib/VulkanMemoryAllocator

for name in fmt dxc-bin smol-v xxHash zstd; do
  read -r url revision < <(python3 -c "import json,sys; d=json.load(open('tools/native-renderer/dependencies.json'))['xenosrecomp_thirdparty'][sys.argv[1]]; print(d['url'], d['revision'])" "$name")
  git clone "$url" "tools/xenosrecomp/thirdparty/$name"
  git -C "tools/xenosrecomp/thirdparty/$name" checkout "$revision"
done

./build/sdk-install/bin/rexglue codegen broken_bond_manifest.toml
cmake --preset linux-release -DNATIVE_RENDERER_PLUME_SOURCE="$PWD/out/plume" \
  -Drexglue_DIR="$PWD/build/sdk-install/lib/cmake/rexglue"
cmake --build build/linux
```

`build-sdk.sh` applies the patches of `patches/series` and builds the SDK with its `linux-amd64` preset into `build/sdk-install`, as `build-sdk.ps1` does on Windows. The `linux-release` preset builds the native renderer, which is required on Linux, with Vulkan only; Plume needs only its volk, Vulkan Headers and Vulkan Memory Allocator submodules there. Add `-DBROKENBOND_NATIVE_TESTS=ON` to the configuration to build the host tests, then run them with `ctest --test-dir build/linux`. The renderer engine's tests run on any Vulkan device, Mesa's llvmpipe included; the tests that open a Win32 window or read Win32 thread priorities, and the shortcut test, run on Windows only.

### Run

`build/linux/` holds `broken_bond`, the SDK runtime `librexruntime.so` and Crashpad's `crashpad_handler`. Copy them together: the executable finds the library and the handler next to itself. Both executables need the system's `libcurl.so.4` (the `libcurl4` package on Ubuntu).

```bash
./build/linux/broken_bond --game_data_root="$PWD/game"
```

`--game_data_root` takes an extracted folder or an ISO; without it, the game looks for one next to the executable, or in the repository's `game/` for a build inside it, as on Windows. The setup screen's game choice and the DLC page's folder choice open the desktop's file dialog through SDL (the XDG desktop portal, or zenity when it is installed). Settings, saves, logs and caches go to the data folder: `build/data/` for a build inside the repository, `data/` next to the executable elsewhere, and the XDG data folder for the AppImage (see [Data folder](data-folder.md)). SDL picks Wayland or X11; `SDL_VIDEO_DRIVER=x11` forces X11.

### Known limitations

- The diagnostics that need Windows (the stack sampler, the heap watch, the swap-chain proof, `REX_NATIVE_CRASH_DUMP`) are not built. Crashpad writes the crash dumps and the session records work as on Windows; a thread snapshot needs gdb at `/usr/bin/gdb` ([Local diagnostics](crash-reports.md)).
- The update check and the desktop shortcut are Windows-only.
- Without the XDG desktop portal or zenity, the file dialog cannot open; choose the game with `--game_data_root` or `custom_game_root` in the data folder's `broken_bond.toml`.
- Holding Shift at start does not open the setup screen; pass `--setup`.
- On a build without Direct3D 12 the game runs on Vulkan whatever `native_backend` says, and the log says so.

## Package

```powershell
./packaging/package-release.ps1          # the ZIP, from build/native
./packaging/package-bridge.ps1           # the bridge for 0.1.0-beta installations (Inno Setup 6.4+)
```

On Linux, from `build/linux` built with `-DBROKENBOND_NATIVE_TESTS=ON`:

```bash
python3 packaging/package-linux-appimage.py   # the AppImage
```

See [Release packaging](../packaging/README.md).
