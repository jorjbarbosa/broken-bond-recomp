# Release packaging

The release is `broken-bond-recomp-win-amd64.zip`. Extract it to a writable
folder and open `broken_bond.exe`. On the first launch, choose your original
Xbox 360 ISO, which is read in place, or the `default.xex` of a complete
extracted copy. Hold Shift while opening, or pass `--setup`, to return to the
setup screen. Press F1 or Back+Start during play for Settings, DLC and Exit
game.

On Linux the package is `broken-bond-recomp-linux-x86_64.AppImage`
([below](#build-the-linux-appimage)). There is no Linux release yet.

See [Build and run](../docs/build-and-run.md), [Configuration](../docs/configuration.md)
and [DLC](../docs/dlc.md).

## Build the ZIP

Use PowerShell 7 with the Visual Studio 2022 x64 tools, Clang and
`llvm-readobj.exe`. First build the game in `build/native` with
`scripts/build-game-native.ps1`, which builds both graphics APIs (see the
[build guide](../docs/build-and-run.md#6-native-renderer)). Then:

```powershell
./packaging/package-release.ps1
# Another output folder, or a version other than packaging/version.iss:
./packaging/package-release.ps1 -OutputRootDir dist/candidate -Version 0.2.0-beta
```

The version is `major.minor.patch` with an optional pre-release suffix, such as
`0.1.0-beta`; update checks compare the numeric part only. The game shows it in
its window title and on the setup screen, read from the packaged
`broken_bond.version`.

The script requires a Release build of the native game with Direct3D 12 and
Vulkan, and rejects any other configuration before it builds or stages
anything. It then refreshes `broken_bond`, checks that the SPIR-V package was
built, and rejects debug CRT imports in the binaries. The package is assembled
in a unique staging folder. Existing artifacts are moved to
`previous-release-*` only once the new ones are complete, and are restored if
a destination is locked. `-Jobs` limits the build's parallelism. Nothing is
published remotely.

Outputs, in `dist/` or `-OutputRootDir`:

- `broken-bond-recomp-win-amd64/`, the payload;
- `broken-bond-recomp-win-amd64.zip`, the same files at the root of the archive;
- `SHA256SUMS.txt`.

The payload is `broken_bond.exe`, `rexruntime.dll` (and
`amd_fidelityfx_dx12.dll` when the SDK provides it), the default
`broken_bond.toml`, `broken_bond.version`, a quick-start `README.txt`, and
`licenses/`. `third-party-licenses.psd1` lists the components; their notices
and the source revisions of the LGPL libraries go to `licenses/README.txt`.
The menus, their fonts, the shaders and the icon are embedded in the
executable; the icon's source is `ui/broken_bond.ico`.

The ZIP has no launcher, setup helper, `extract-xiso` or installer. Game data,
personal settings, saves, logs and mods are never copied into it. The game
needs the Microsoft Visual C++ x64 runtime. `scripts/extract-game.ps1` remains
a developer tool for the code generation inputs, outside the release.

## Build the Linux AppImage

On Linux, after the [Linux build](../docs/build-and-run.md#linux) of
`build/linux` with `-DBROKENBOND_NATIVE_TESTS=ON`:

```bash
python3 packaging/package-linux-appimage.py
# Another output folder, or a version other than packaging/version.iss:
python3 packaging/package-linux-appimage.py --output-root dist/candidate --version 0.2.0-beta
```

The script requires a Release build of the native game on Vulkan with the host
tests, since the image carries the POSIX wait check, and builds `broken_bond`
and that check first. It downloads appimagetool 1.9.1 and the AppImage type2
runtime once into `out/appimage-tools/` (`--tools-dir`), each pinned by
SHA-256; a file that does not match stops the build. The runtime's URL is
upstream's `continuous` release, which is republished from time to time: its
pin is then checked and moved in the script. appimagetool is extracted for each
build, so building needs no FUSE. `--jobs` limits the build's parallelism.

Outputs, in `dist/` or `--output-root`: `broken-bond-recomp-linux-x86_64.AppImage`,
its `README.txt` and `SHA256SUMS.txt`. Existing ones move to
`previous-release-*`. The image is assembled in a unique staging folder.

The image holds `broken_bond`, `librexruntime.so`, the wait check,
`broken_bond.version`, the README, `defaults/broken_bond.toml` (the ZIP's
defaults on Vulkan) and `licenses/` (the components of
`third-party-licenses.psd1` without the Windows-only ones), with the
`AppRun`, the desktop entry, the icon `ui/broken_bond.png` and
`packaging-info.json` (the binaries' hashes, the build id, the commit, the glibc
the binaries need and the host libraries they load). No system library is
bundled: the script stops if the binaries need anything beyond glibc, the C++
runtime, the X11 and xcb client libraries and the Wayland client. The binaries
need the build system's glibc or newer (2.43 on Ubuntu 26.04).

The game writes beside its executable, and the image is read only. So at each
start the `AppRun` copies the binaries, the version, the README and the
licenses into `$XDG_DATA_HOME/broken_bond` (`~/.local/share/broken_bond`) when
the image's build differs from the copy's, seeds `broken_bond.toml` there on
the first start, and runs the copy with the caller's arguments and working
folder. The settings, the saves and caches (`user/`), the logs, mods and DLC
stay in that folder across updates. The game finds a `game/` folder beside the
AppImage by itself. `--test-posix-wait` runs the wait check from the image
without copying anything.

## Bridge for 0.1.0-beta installations

Version 0.1.0-beta shipped a launcher and an Inno Setup installer.
`BrokenBondInstaller.exe`, built from `installer.iss`, moves those
installations to the ZIP's layout. Build it after the ZIP, with Inno Setup 6.4
or later installed:

```powershell
./packaging/package-bridge.ps1
# From a candidate payload:
./packaging/package-bridge.ps1 -OutputRootDir dist/candidate
```

It uses the published payload and its `broken_bond.version`, so the bridge has
exactly the ZIP's version, and adds its line to `SHA256SUMS.txt`. It does not
build the game. A failed compilation leaves the previous bridge in place.
`-PackageDir` and `-IsccPath` select another payload or compiler.

The bridge keeps 0.1.0-beta's AppId, default folder
(`C:\Games\Naruto The Broken Bond`) and Start menu group:

- **What it updates:** the registered installation is preselected, and a
  portable copy can be chosen when it has `broken_bond.exe` and
  `rexruntime.dll`.
- **Before replacing files:** it reserves `broken_bond_launcher.exe`, and
  reports an error if the old launcher is running.
- **After installing:**
  - it removes the launcher, its artwork and fonts in `assets\`, and the Lato
    and extract-xiso notices, by name;
  - it points the Start menu and desktop shortcuts of this copy at
    `broken_bond.exe`;
  - shortcuts to other copies are left alone.
- **What it keeps:** `game\`, `user\`, the existing `broken_bond.toml` byte for
  byte, DLC, mods and other files, also on uninstall.
- **What it refuses:** numeric downgrades. There is no ISO page, helper or
  extraction.
- **New copies:** the bridge can also install one, and new copies take the
  bridge's language as the menu language. New players should use the ZIP.

```powershell
./dist/BrokenBondInstaller.exe /MODE=update /DIR="C:\Games\Naruto The Broken Bond"
```

`/MODE=update` and `/MODE=install` are for automation. Invalid modes and
missing update destinations fail before any file is replaced.

## Tests

```powershell
# The ZIP against its published folder:
python -I packaging/tests/test_package.py dist/broken-bond-recomp-win-amd64.zip
# The configuration gate, with a stub build:
pwsh -NoProfile -File packaging/tests/test_release_gate.ps1
# Real Inno installs of the bridge:
$env:BROKENBOND_TEST_PACKAGE = "$PWD/dist/broken-bond-recomp-win-amd64"
$env:BROKENBOND_TEST_OLD_PACKAGE = "$PWD/dist/v0.1.0-beta/package/broken-bond-recomp-win-amd64"
$env:BROKENBOND_TEST_OUTPUT = "$PWD/build/bridge-tests"
python -I packaging/tests/test_bridge.py -v
```

On Linux, the AppImage:

```bash
python3 -I packaging/tests/test_appimage.py dist/broken-bond-recomp-linux-x86_64.AppImage
```

It runs the image's wait check extracted and, when the host has FUSE, mounted;
extracts the image and checks its payload, notices and seed; and runs the
`AppRun` over a stub game in a scratch home, for the copy, the seed, the
arguments, the working folder and an update. It never starts the game.

`BROKENBOND_TEST_OLD_PACKAGE` is the extracted v0.1.0-beta ZIP, from the
project's release page.

The bridge tests use a unique AppId, private shortcut folders and synthetic
saves, and never start the game. With the old payload, they compile the
0.1.0-beta installer from the `v0.1.0-beta` tag with isolated locations and
install it, then update that installation through the bridge. Its unused ISO
tools are placeholders. Without the old payload, that case is skipped; the
portable, locked-launcher, downgrade, language and shortcut cases still run.
Use a new output folder for each run; the install logs stay there.
`BROKENBOND_TEST_ISCC` selects another Inno compiler. Python 3.11 or later is
required.

A successful package or installer test does not establish that the game runs.
