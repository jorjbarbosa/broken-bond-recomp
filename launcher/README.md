# Retired launcher sources

The supported executable is `broken_bond.exe`, with its own setup screen and
in-game menus. CMake no longer configures this directory or builds the old
ImGui launcher, the installer's ISO helper or their tests.

The shared configuration, content, localization, key names, graphics
enumeration and update-check code lives in `src/menu`, and its tests are in
`tests/`. The game's icon is `ui/broken_bond.ico`.

The remaining sources and assets are historical material, outside the build and
the release package. See [release packaging](../packaging/README.md) for the
ZIP.
