# Data folder

The game keeps everything it writes in one data folder, apart from the program and the game files:

| What | Where in the data folder |
| --- | --- |
| Settings | `broken_bond.toml` |
| Saves, profiles, installed DLC, title data and achievements (the guest's content) | `user/` |
| Logs | `logs/` (logs imported from an earlier copy in `logs/legacy/`) |
| Translation packages | `mods/` |
| DLC packages waiting to be installed | `dlc/` |
| Shader and pipeline caches (Windows and Linux builds) | `cache/` |
| The pipelines this machine has drawn, compiled ahead at the next start (one list per graphics API) | `cache/pipelines/d3d12.json`, `cache/pipelines/vulkan.json` |
| Session records and their exports ([Local diagnostics](crash-reports.md)) | `incidents/`, `incident-exports/` |
| The start marker and the update check's state | `launch.marker`, `menu-updates.json` |
| The import journal and the lock | `.broken_bond-migration/`, `.broken_bond.lock` |

**Settings → Advanced → Data folder** shows the folder in use and opens it.

## Where it is

| Copy | Data folder | Cache |
| --- | --- | --- |
| Windows ZIP, or any copy outside the repository | `data\` beside `broken_bond.exe` | `data\cache\` |
| Linux AppImage | `${XDG_DATA_HOME:-~/.local/share}/broken_bond/` | `${XDG_CACHE_HOME:-~/.cache}/broken_bond/` |
| Linux build outside the repository | `data/` beside `broken_bond` | `data/cache/` |
| A build inside the repository (`build/native`, `build/game`, `build/linux`) | `build/data/`, shared by all of them | `build/data/cache/` |
| The portable folder, when chosen | `data` beside the executable, or beside the AppImage | `data/cache/` |

The test tree `build/native-vulkan` keeps a `data\` folder beside its executable.

The folder is chosen in this order, the first one set wins:

1. `--data_root <folder>` on the command line;
2. the `REX_DATA_ROOT` environment variable;
3. `data_location.json` beside the executable (beside the AppImage on Linux): the portable choice, or
   a choice made from it;
4. the user's `data_location.json`: `%LOCALAPPDATA%\broken_bond\` on Windows,
   `${XDG_CONFIG_HOME:-~/.config}/broken_bond/` on Linux;
5. the default of the table above.

`defaults/broken_bond.toml` beside the executable (inside the AppImage) holds the settings a new
data folder starts from. The game copies it once and never writes it.

## The first start

The first start copies what earlier copies kept elsewhere into the data folder, and leaves the
originals in place:

- the settings, saves, logs, translation packages and DLC packages that version 0.1.0-beta kept beside
  `broken_bond.exe` (`broken_bond.toml`, `user\`, `logs\`, `mods\`, `dlc\`); in the repository, the
  shared `build/user/` and `build/native/broken_bond.toml`;
- on Windows, the saves that 0.1.0-beta started without its launcher kept in
  `Documents\broken_bond`;
- the guest trees that an earlier data folder kept at its root, which move into `user/`.

Caches and the update check's state are not imported; the caches are rebuilt. Files that are equal
in two sources are copied once. When two sources hold different copies of the same file, or a tree
exists both at the root and in `user/`, the start stops and names both: keep the one you want
(back up the other) and start the game again. Nothing is chosen by date.

If the data folder cannot be prepared, the game does not start. It shows the error, and writes it
to a log in the temporary folder (`%TEMP%\broken_bond\bootstrap\` on Windows,
`/tmp/broken_bond/bootstrap/` on Linux), or to `--log_file` when one is given. A damaged
`broken_bond.toml`, `data_location.json` or journal is reported, never replaced with defaults.

Only one game uses a data folder at a time: a second one started meanwhile says the folder is in
use and closes. `.broken_bond.lock` stays in the folder after the game closes; that is normal.

## Choosing another folder

**Settings → Advanced → Data folder** has:

- **Open data folder**;
- **Choose another folder…**, with the system's folder picker;
- **Use portable folder** (`data` beside the executable or the AppImage);
- **Restore default folder**.

The new folder must be writable and neither inside the folder in use nor its parent. The choice is
saved at once in `data_location.json` and shown as **Folder after restart**. The game keeps
writing to the folder in use until it closes. The next start copies the data into the new folder,
checks it, and only then uses it; the earlier folder is kept. The notice offers to restart now.

**Use portable folder**, and any choice made while the portable folder is in use, is saved in the
`data_location.json` beside the executable (beside the AppImage on Linux); other choices in the
user's.

## For one session

`--data_root` and `REX_DATA_ROOT` choose a folder for that session only. Nothing is imported
into it from earlier copies, and the menu cannot change the saved location while one is set.

Other locations can be set separately, on the command line, in the environment or in the settings:

| Location | Command line | Environment | Settings |
| --- | --- | --- | --- |
| The guest's content (saves, installed DLC) | `--user_data_root` | `REX_USER_DATA_ROOT` | `user_data_root` |
| The cache | `--cache_root` | `REX_CACHE_ROOT` | `cache_root` |
| The log file | `--log_file` | | |

`user_data_root` names the guest folder itself; Advanced then lists it on its own row (**Saves and
content folder**), and changing the data folder does not move it. Without a cache location, the cache
follows it to `<user_data_root>/cache`. Relative paths on the command line or in the environment
start from the folder the game was started in; relative paths in the settings start from the folder
the settings were written for.

A restart from the menu and the desktop shortcut keep the locations given on the command line or in
the environment, and nothing else.

## Updates and backups

Close the game before copying anything. A backup is the whole data folder, a separate
`user_data_root` folder if one is set, and `data_location.json` if there is one. Keep
`.broken_bond-migration/`: it records what was already imported, so the import is not repeated.

- **Windows ZIP:** extract the new ZIP over the old files. It holds no `data`, no
  `data_location.json` and no settings in use.
- **Bridge (`BrokenBondInstaller.exe`) over a 0.1.0-beta install:** it keeps `broken_bond.toml`,
  `user`, `logs` and `mods` beside the executable; the next start copies them into `data`.
- **AppImage:** replace the AppImage file. AppImages before this one copied the program (about
  145 MB) into the data folder and ran it from there; the AppImage removes that copy at its first
  start (the files `broken_bond`, `librexruntime.so`, `broken_bond.version`, `README.txt`,
  `licenses/`, `.appimage.lock` and `.build-id`, only when `.build-id` is there) and keeps
  everything else.

## In the repository

Every build inside the repository uses `build/data`. Its first start copies `build/user/` and
`build/native/broken_bond.toml` into it; the originals stay. `scripts/run-game.ps1` uses it too,
with its log in `build/logs/`. The boot scripts of `build/tools` take `-DataRoot <folder>` to run
on another folder.
