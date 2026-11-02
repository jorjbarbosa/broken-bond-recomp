<p align="center">
  <img src="docs/assets/naruto-broken-bond-logo.png" alt="Naruto: The Broken Bond" width="600">
</p>

<h1 align="center">Naruto: The Broken Bond — Recomp</h1>

<p align="center">
  A static recompilation for PC powered by ReXGlue<br>
  <strong>Native Direct3D 12 and Vulkan renderer • Setup screen and in-game menus • Streamed audio fixes</strong>
</p>

<p align="center">
  <a href="#getting-started">Getting started</a> ·
  <a href="#renderers">Renderers</a> ·
  <a href="docs/build-and-run.md">Build and run</a> ·
  <a href="docs/configuration.md">Configuration</a> ·
  <a href="docs/troubleshooting.md">Troubleshooting</a> ·
  <a href="#support">Support</a>
</p>

<p align="center">
  <a href="https://buymeacoffee.com/jorjbarbosa"><img src="https://img.shields.io/badge/Buy%20Me%20a%20Coffee-support-FFDD00?logo=buymeacoffee&logoColor=black" alt="Buy Me a Coffee"></a>
</p>

> [!WARNING]
> This project is a work in progress. You need your own copy of the game, either to build the port from source or to play a release. Expect bugs and crashes, keep a backup of your saves, and report problems on the [Issues page](https://github.com/jorjbarbosa/broken-bond-recomp/issues).

## About

A PC recompilation of **Naruto: The Broken Bond**, released by Ubisoft for Xbox 360 in 2008. The project uses the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) to translate the game's PowerPC code into C++ and compile it for native execution. The SDK runtime provides system services, audio, and input.

The game draws with a native renderer on **Direct3D 12** or **Vulkan**, starts on its own setup screen, and keeps its settings in in-game menus; there is no separate launcher. The native renderer, the setup screen and the menus are adapted from the [Naruto: Rise of a Ninja recomp](https://github.com/jorjbarbosa/naruto-rise-recomp).

Releases ship a portable ZIP for **Windows x64** and an AppImage for **Linux x86-64**, which runs on Vulkan. Both also build from source; see the [build guide](docs/build-and-run.md) and the [Linux build](docs/build-and-run.md#linux).

> [!NOTE]
> AI tools are used to assist the development of this project. The project's direction, technical decisions, and testing are the maintainer's, who reviews every change before it is published.

## Current state

The port has been played on Windows 11 with an AMD Radeon RX 9070 XT and a DualSense controller: the menus, the start of Story Mode with its first fights and cutscenes, Fight Mode, and the open world, in sessions of up to about 30 minutes, with English and Japanese voices, at the game's 30 fps and at 60 fps, without missing draws. Later sessions of up to about three and a half hours ran at 120 fps on Direct3D 12.

The Xbox Live features and most of the story have not been tested yet. Other graphics cards, drivers, and game revisions have not been tested either.

## Features

**Graphics**

- **Native renderer:** the game's Direct3D calls run on Direct3D 12 (the default) or Vulkan, with the shaders translated when the port is built. See [Renderers](#renderers).
- **Internal resolution from 1× to 4×**, up to 16× anisotropic filtering, FXAA, FidelityFX CAS or FSR, fullscreen, and VSync.
- **Native renderer options:** frame rates above the game's own (the frames between its logic steps are interpolated), MSAA (2×, 4×, 8×), transparency antialiasing, fractional internal resolution (such as 1.5×), brightness, frame latency, triple buffering, graphics card selection, and wide aspect ratios. Most graphics options apply while playing.

**Audio**

- **Streamed voices and music that play to the end.** Local fixes to the SDK's XMA decoder keep the frames that cross a buffer boundary, so cutscenes no longer wait forever for a voice line, the music no longer stops, and the crackles at buffer boundaries are gone.

**Menus, controls and distribution**

- **Setup screen:** the game opens on a screen with its own logo, where you play, choose your ISO (read in place, without extraction) or an extracted copy, and change settings. The supported revision is checked before the game starts. See [Configuration](docs/configuration.md).
- **In-game menu:** F1 or Back + Start opens Settings and Exit game over the game, in seven interface languages.
- **Four players' controls:** each player has a controller or keyboard and mouse, with its own bindings and dead zones. Controllers are assigned automatically.
- **DLC:** marketplace packages copied into the data folder's `dlc/` are installed at start. See [DLC](docs/dlc.md).
- **Data folder:** settings, saves, logs, translation packages and DLC packages live in one data folder, apart from the program: `data\` beside `broken_bond.exe`, or the XDG data folder for the AppImage. The first start copies a 0.1.0-beta copy's files into it and keeps the originals; Settings → Advanced shows it and can move it. See [Data folder](docs/data-folder.md).
- **Translation mods:** packages in the data folder's `mods/` replace the game's English texts by ID without changing the game files, and the menus list them as game languages. See [Translation mods](docs/translation-mods.md).
- **Diagnostics:** each session leaves a record in the data folder, with the crash dump when the game crashes, and Settings → Advanced exports it to share. Crash reports go to the developers through Sentry only if you agree when the game asks, at its first start. See [Local diagnostics](docs/crash-reports.md) and [Crash reports](docs/sentry.md).
- **Portable ZIP:** the release is a ZIP to extract and run. Installations of 0.1.0-beta, which used a launcher and an installer, move to it with a bridge installer that keeps game files, saves and settings. See [Release packaging](packaging/README.md).
- **Automatic function discovery:** a ReXGlue patch finds the functions the game reaches only through pointer tables or addresses built in code, so the port needs no hand-written function list. See [Automatic function discovery](docs/automatic-function-discovery.md).

## Renderers

The game draws with the native renderer: it intercepts the game's Direct3D
calls and issues them with Direct3D 12 (the default) or Vulkan, with the
shaders translated at build time and embedded in the executable. See [Renderers](docs/renderers.md)
for the frame rate option, validation limits and known issues.

## Requirements

- Windows x64, or Linux x86-64 with glibc 2.43 or newer for the AppImage.
- A GPU supporting Direct3D 12 or Vulkan 1.2.
- Your Xbox 360 copy of the game, as an ISO or extracted files, in the supported revision below. The source tree and the releases do not include game data.
- The build tools listed in the [build guide](docs/build-and-run.md#1-prerequisites).
- An SDL-compatible controller is recommended. The SDK's keyboard emulation is also available.

The port supports one executable revision; the build and the setup screen refuse any other:

| Field | Value |
| --- | --- |
| Title ID | `55530825` |
| Media ID | `09FFE047` |
| Executable version | `0.0.0.3` |
| `default.xex` SHA-256 | `8f70e79443e36b38e44b6a105dad51fb8909fb615d982753cabaac9b15f0d576` |

## Getting started

From a checkout, in PowerShell 7 with the Visual Studio C++ tools installed:

```powershell
git -c core.symlinks=true submodule update --init --recursive
./scripts/extract-game.ps1          # extracts your ISO into game/
./scripts/build-sdk.ps1             # builds and installs the patched SDK
./scripts/build-game.ps1            # generates the C++ code
# Prepare pinned native dependencies as described in the build guide, then:
./scripts/build-game-native.ps1 -PlumeSource out/plume `
  -XenosRecompThirdparty tools/xenosrecomp/thirdparty
./build/native/broken_bond.exe
```

The [build guide](docs/build-and-run.md) explains each step, the native renderer build, and the ways to run the game.

## Documentation

| Guide | Contents |
| --- | --- |
| [Build and run](docs/build-and-run.md) | Toolchain, SDK, game files, code generation, the native renderer, and running |
| [Configuration](docs/configuration.md) | The setup screen, the game menu's settings, the controls, the configuration file, and user files |
| [Data folder](docs/data-folder.md) | Where settings, saves, logs and packages live, the first start's import, moving the folder, and backups |
| [Translation mods](docs/translation-mods.md) | Translation packages, their format, validation, and limits |
| [DLC](docs/dlc.md) | Installing marketplace packages, their folders, and diagnostics |
| [Renderers](docs/renderers.md) | The native renderer, the frame rate, the pipeline list, and known issues |
| [Release packaging](packaging/README.md) | The ZIP, the bridge for 0.1.0-beta installations, and their tests |
| [Architecture](docs/architecture.md) | Project structure, SDK patches, and areas to address |
| [Troubleshooting](docs/troubleshooting.md) | Build failures, runtime issues, and diagnostics |
| [Local diagnostics](docs/crash-reports.md) | The session records, thread snapshots, exports, and retention |
| [Crash reports](docs/sentry.md) | The consent question, what is sent to Sentry, the build, and the symbols |
| [Recompilation notes](docs/recompilation-notes.md) | Executable identity, function boundaries, and validation evidence |
| [Automatic function discovery](docs/automatic-function-discovery.md) | The discovery patch, its rules, and its tests |

## Roadmap

Planned work, in no particular order:

- **Broader native renderer validation,** across the whole game and on other graphics cards.
- **Accessibility options.**
- **Multiplayer.**
- **Achievements.**

## Contributing

Read the [architecture notes](docs/architecture.md) before changing the runtime or code generation files. When reporting an issue, include reproduction steps, the commit you built, CPU, GPU, driver, the renderer you use, your configuration, and the log of the run (`build/logs/` with `run-game.ps1`, otherwise the data folder's `logs/`: `data\logs\` beside `broken_bond.exe`, or `build/data/logs/` for a build in the repository).

Work happens on the `develop` branch; `main` is the primary branch. Keep game files, derived shader data, pipeline lists, and generated code out of commits. SDK changes go into reproducible patches in `patches/`. Code, comments, documentation, and commit messages are written in English, as [AGENTS.md](AGENTS.md) requires.

## Support

If you enjoy the project, you can [buy me a coffee](https://buymeacoffee.com/jorjbarbosa). It's optional and unlocks nothing; releases stay free for everyone.

## Credits

**Projects and libraries**

- [ReXGlue](https://github.com/rexglue/rexglue-sdk), recompilation SDK and runtime.
- [Xenia](https://github.com/xenia-project/xenia) and [XenonRecomp](https://github.com/hedge-dev/XenonRecomp), foundations and references for the Xbox 360 recompilation ecosystem. Several of the XMA decoder fixes are ports of [Xenia Canary](https://github.com/xenia-canary/xenia-canary) commits.
- [XenosRecomp](https://github.com/hedge-dev/XenosRecomp), shader translation for the native renderer (forked in `tools/xenosrecomp/`), and [Plume](https://github.com/renderbag/plume) with [D3D12 Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator) for its Direct3D 12 backend, and [volk](https://github.com/zeux/volk), [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator), [Vulkan Headers](https://github.com/KhronosGroup/Vulkan-Headers) and [SMOL-V](https://github.com/aras-p/smol-v) for its Vulkan backend.
- [RmlUi](https://github.com/mikke89/RmlUi) and [FreeType](https://freetype.org) for the menus, and [SDL](https://github.com/libsdl-org/SDL), [Dear ImGui](https://github.com/ocornut/imgui), [toml++](https://github.com/marzer/tomlplusplus), and [extract-xiso](https://github.com/XboxDev/extract-xiso).

**References**

- [Naruto: Rise of a Ninja Recomp](https://github.com/jorjbarbosa/naruto-rise-recomp), source of the native renderer, the setup screen and the menus.
- [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp), the model for the native renderer's design and its pipeline handling.
- Ubisoft Montreal, for the original game. [Logo source](docs/assets/README.md).

## License

Copyright (C) 2026 Jorge Barbosa. The source code of this port is licensed under version 3 of the [GNU General Public License](LICENSE) (GPL-3.0-only).

Third-party code keeps its own license: the ReXGlue SDK (`sdk/`, BSD-3-Clause) and its dependencies, the XenosRecomp fork (`tools/xenosrecomp/`, MIT), Plume, D3D12 Memory Allocator, volk and Vulkan Memory Allocator (MIT), SMOL-V (MIT and Apache-2.0) and the Vulkan Headers (Apache-2.0), built into the executable with the native renderer, RmlUi (MIT) and FreeType (FTL), built into it for the menus, and the Roboto fonts (`ui/fonts/`, OFL-1.1). The retired launcher's Lato fonts (`launcher/assets/fonts/`) are OFL-1.1.

Naruto: The Broken Bond, its content, and its artwork and trademarks, including the icon in `ui/` and the artwork in `launcher/assets/` and `docs/assets/`, belong to their respective owners and are not covered by this license. The license does not cover the original game executable, its assets, or the code translated from it. No game data is distributed with this project.
