# DLC

[Documentation](README.md) · [Configuration](configuration.md)

The game installs and reads Xbox 360 marketplace content (content type `00000002`) for Title ID **`55530825`**. You must supply the content files yourself. The game runs without DLC.

Broken Bond's marketplace packages hold game data only; no package carries code, so nothing has to be recompiled. The game mounts every installed package at start and reads its files together with its own data. For example, the "Chunin outfit Sasuke" package holds `Naruto_DLCUnlockBlackSasuke.bf`, which the game opens right after `naruto.bf`.

The DLC page needs the native renderer build; the install at start runs with either renderer.

## Install through the game menu

1. Press **F1** or **Back+Start** in game and select **DLC**.
2. Select **Add DLC...**, then the folder that holds your packages or already-extracted content.
3. Restart the game to install the packages.

The page searches the folder for files with a `CON `, `LIVE` or `PIRS` signature, whatever their extension. It copies the packages it finds into the data folder's `dlc/`, and the game installs them at the next start. A folder without packages is treated as extracted content and copied into the user data folder at once.

The page lists installed content by the name in its header, and staged packages as **Installs on restart** until the game has installed them. **Open DLC folder** opens the installed content folder.

## Install by hand

Copy the packages, or a download that keeps the Xbox 360 layout (`55530825/00000002/<package>`), into the data folder's `dlc/` (`data\dlc\` beside `broken_bond.exe`; see [Data folder](data-folder.md)), then start the game. Each package is installed once: a package counts as installed when its extracted folder and its header exist in the user data folder.

```text
<data folder>/
└── dlc/                          # dlc_source_path
    └── <your packages>

<user data folder>/               # the data folder's user/, or user_data_root
└── 0000000000000000/
    └── 55530825/
        ├── 00000002/
        │   └── <contents of each package>/
        └── Headers/
            └── 00000002/
                └── <package>.header
```

At start the game also repairs the installed content folder:
- A package copied straight into `00000002/` (for example through **Open DLC folder**), or a folder of packages there, is extracted.
- An extracted folder without a header gets one. Without a header the game would mount the package without its license.

Folder names longer than 42 bytes cannot be opened by the game and are reported in the log.

`dlc_source_path` (default `"dlc"`, the data folder's `dlc/`) sets the folder scanned at start. Another relative path starts at the folder the settings were written for, or at the executable's folder when it comes from the command line; an empty value stops new installs but keeps installed content.

## Diagnostics

Search the log for `DLC install`, `Installing DLC package`, `DLC install failed` and `ContentManager lists`. A successful start lists each installed package by its file name, for example:

```text
[broken_bond] DLC install: 1 new package(s), 1 mirrored dir(s)
[broken_bond] ContentManager lists 1 marketplace package(s); 0 repaired
[broken_bond]   - file_name='3D56F90400DE6FBCED7852DF085F04AD75728F8F55'
```

To compare runs with and without DLC, start the game with another `--user_data_root` and `--dlc_source_path ""`.
