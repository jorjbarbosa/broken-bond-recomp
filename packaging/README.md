# Release packaging

The release is `broken-bond-recomp-win-amd64.zip`. Extract it to a writable
folder and open `broken_bond.exe`. On the first launch, choose your original
Xbox 360 ISO, which is read in place, or the `default.xex` of a complete
extracted copy. Hold Shift while opening, or pass `--setup`, to return to the
setup screen. Press F1 or Back+Start during play for Settings, DLC and Exit
game.

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
