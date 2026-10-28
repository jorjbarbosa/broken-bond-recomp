NARUTO: THE BROKEN BOND - PC PORT (@VERSION@), LINUX APPIMAGE

FIRST LAUNCH:
Make the AppImage executable and open it:
  chmod +x broken-bond-recomp-linux-x86_64.AppImage
  ./broken-bond-recomp-linux-x86_64.AppImage
Put a complete extracted copy of the game in a folder named game beside the
AppImage, or choose your original Xbox 360 ISO or the default.xex of a
complete copy on the setup screen. The ISO is read in place; nothing is
extracted or installed.
Supported Title ID: 55530825 (default.xex SHA-256
8f70e79443e36b38e44b6a105dad51fb8909fb615d982753cabaac9b15f0d576).
A game elsewhere: --game_data_root=/path/to/game (a folder or an ISO).
The startup menu opens before the game; it can be turned off under
Settings > Advanced. Run with --setup to return to the setup screen.

WHERE THE DATA GOES:
The first start copies the game's program files (about 145 MB) into
~/.local/share/broken_bond ($XDG_DATA_HOME/broken_bond when it is set) and
runs them from there. A new AppImage replaces that copy at its first start.
The same folder holds the settings (broken_bond.toml), the saves and caches
(user), the logs (logs), translation packages (mods) and DLC packages (dlc);
updates keep them.

IN GAME:
Press F1 or Back+Start to open Settings, DLC and Exit game.
Translation packages in mods/<name> appear under Settings > Game > Game
language.

UPDATES:
Close the game and replace the AppImage file.

WITHOUT FUSE:
  ./broken-bond-recomp-linux-x86_64.AppImage --appimage-extract-and-run
A check that needs no game: --test-posix-wait (it ends with PASS).

REQUIREMENTS:
Linux x86-64 with glibc 2.43 or newer, the Vulkan loader and a Vulkan 1.2
driver (Mesa or NVIDIA), and an X11 or Wayland desktop. The game runs on
Vulkan only. The file dialog of the setup screen and the DLC page needs the
XDG desktop portal or zenity.

KNOWN LIMITATIONS:
No update check or desktop shortcut on Linux. Holding Shift at start does
nothing; use --setup.

Game files are not included. License and third-party notices:
licenses/README.txt in the data folder.
