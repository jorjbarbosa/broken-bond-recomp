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
The game runs from the AppImage. Its data folder is ~/.local/share/broken_bond
($XDG_DATA_HOME/broken_bond when it is set): the settings (broken_bond.toml),
the saves (user), the logs (logs), translation packages (mods) and DLC
packages (dlc). Caches go to ~/.cache/broken_bond ($XDG_CACHE_HOME).
Settings > Advanced shows the data folder, opens it and can choose another
folder, the portable one (data beside the AppImage) or the default: the
change happens at the next start, which copies the data there and keeps the
old folder. --data_root or REX_DATA_ROOT chooses a folder for one session.
Earlier AppImages copied the program files (about 145 MB) into the data
folder and ran them from there; the first start of this one removes that
copy and keeps everything else.

CRASH REPORTS:
Each start keeps a local record in the data folder's incidents, with a crash
dump when the game crashes; nothing leaves the computer unless you allow it.
The first game frame asks whether to send crash reports to the developers
through Sentry: a memory dump, the session log, system and graphics details,
file paths that may include your user name, and the startup time of one start
in ten. Saves and game files are never sent. Settings > Advanced > Diagnostics
changes the answer, opens the local records and exports a session to share
(the thread snapshot there needs gdb).

IN GAME:
Press F1 or Back+Start to open Settings, DLC and Exit game.
Translation packages in mods/<name> appear under Settings > Game > Game
language.

UPDATES:
Close the game and replace the AppImage file. The data folder, and
data_location.json beside the AppImage when the portable folder is used,
stay as they are; back them up before updating.

WITHOUT FUSE:
  ./broken-bond-recomp-linux-x86_64.AppImage --appimage-extract-and-run
A check that needs no game: --test-posix-wait (it ends with PASS).

REQUIREMENTS:
Linux x86-64 with glibc 2.43 or newer, the Vulkan loader and a Vulkan 1.2
driver (Mesa or NVIDIA), libcurl (libcurl.so.4, for the crash reports), and an
X11 or Wayland desktop. The game runs on
Vulkan only. The file dialog of the setup screen and the DLC page needs the
XDG desktop portal or zenity.

KNOWN LIMITATIONS:
No update check or desktop shortcut on Linux. Holding Shift at start does
nothing; use --setup.

Game files are not included. License and third-party notices are inside the
AppImage: run it with --appimage-extract and read
squashfs-root/usr/lib/broken_bond/licenses/README.txt.
