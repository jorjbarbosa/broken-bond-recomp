# Configuration

[Documentation](README.md) · [Renderers](renderers.md) · [Build and run](build-and-run.md)

## Where to configure settings

Use the setup screen or the game menu. Both save `broken_bond.toml` in the **data folder** (`data\` beside `broken_bond.exe`, `build/data/` for a build in the repository; see [Data folder](data-folder.md)) at each change. Close the game before editing the file by hand.

Every option can also be set as an environment variable named `REX_` plus the key in capitals (such as `REX_MSAA_SAMPLES=4`), or on the command line (`--msaa_samples 4`; a boolean needs `=`, as in `--fullscreen=false`). The command line takes precedence over environment variables, and both over the file.

## Setup screen

The game opens on the setup screen, with the game's logo read from your game files, or a text title when they have none. **Play** starts the game; **Settings** and **DLC** open those pages; **Quit** closes it. When no supported game is found, the first entry chooses one: an ISO, read in place, or the `default.xex` of a complete extracted copy. The choice is checked against the supported revision and saved as `custom_game_root`; your original files are never moved or changed.

The screen also opens when the last start failed before its first frame, or when the chosen game is missing or unsupported. **Settings → Advanced → Startup menu** turns off the screen for normal starts; holding Shift while opening, or `--setup`, brings it back. Changing the graphics API or card offers to restart the game.

## Game menu

In game, F1 or Back + Start opens the game menu over the native renderer: **Settings**, **DLC** and **Exit game**. The game keeps running, but its controller, keyboard and mouse input stay neutral while the menu shows; Esc or B closes it. Both shortcuts can be changed under **Settings → Controls**. With `show_fps_overlay = true` the frame rate also shows in a corner of the screen.

**Settings** has five pages, in seven interface languages: English (default), Brazilian Portuguese, French, German, Spanish, Italian and Russian.

| Page | Options |
| --- | --- |
| Game | Game folder (on the setup screen), game language, Ubisoft boot-logo skip, menu language |
| Display | Graphics API, graphics card, resolution, aspect ratio, brightness, frame rate, VSync, fullscreen |
| Quality | MSAA, FXAA, post-processing, texture filtering, transparency AA |
| Controls | Configure controls, SDL or XInput, the menu's keyboard key and controller buttons |
| Advanced | General: startup menu, update check, desktop shortcut. On-screen indicators: FPS overlay, pipeline notice. Performance: frame latency, triple buffering. Data folder: the folder in use, Open, Choose another folder, Use portable folder, Restore default folder |

In game, resolution, brightness, frame rate, VSync, MSAA, FXAA, post-processing, texture filtering, transparency AA, frame latency, triple buffering and the boot-logo skip apply at once. The menu waits for the renderer to accept each change and restores the previous value if it is refused. Resolution and MSAA changes can pause the image briefly. Options marked RESTART apply at the next start.

## Controls

**Settings → Controls → Configure controls** sets up four players. Each player has a device (automatic by default, or a chosen controller or the keyboard and mouse), the game commands mapped to keys and mouse buttons and to controller inputs, the mouse sensitivity, dead zones, and vibration with a motor test. Controllers are assigned to the players as they connect. Changes are saved at once in the `[controls]` table of `broken_bond.toml`.

On the first start, the older single-player keys (`mnk_mode`, `mnk_mouse`, `mnk_sensitivity` and `keybind_*`) are moved into player 1; they stay in the file. Mouse movement emulates the right stick; there is no direct mouse camera.

## Game language

Open **Settings → Game → Game language**. The selection is applied on the next start. Text and voice availability depends on the language assets in your game files.

| Language | `user_language` |
| --- | --- |
| English (default) | `1` |
| French | `4` |
| German | `3` |
| Spanish | `5` |
| Italian | `6` |

A code set by hand outside this list is kept and shown as **Custom (code)** until you select a listed language.

Translation packages installed in the data folder's `mods/` also appear in this list, by their own name. Selecting one sets English as the base language and `translation_mod` to the package folder; selecting an official language clears it. See [Translation mods](translation-mods.md).

The voices (English or Japanese) are chosen in the game's own options menu.

## Main settings

Defaults of a new configuration (`defaults/broken_bond.toml` in a package, from `packaging/broken_bond.toml`, and the game's own):

| Key | Default | Effect |
| --- | --- | --- |
| `user_language` | `1` | Game language; see the codes above |
| `translation_mod` | `""` | Translation package folder under `mods/`, applied over English; empty disables it. See [Translation mods](translation-mods.md) |
| `resolution_scale` | `1` | Internal resolution: 1 (1280×720, the original) to 4 (5120×2880) |
| `draw_resolution_scale_x`, `draw_resolution_scale_y` | `1`, `1` | Draw scales, kept equal to `resolution_scale` by the menus |
| `anisotropic_override` | `5` | Texture filtering in the SDK's encoding: `-1` the game's, `0` off, `1` to `5` for 1× to 16×; `5` is 16× |
| `swap_post_effect` | `"fxaa"` | `"none"`, `"fxaa"`, or `"fxaa_extreme"` for edge smoothing |
| `present_effect` | `"cas"` | `"bilinear"`, `"cas"` (sharpen), or `"fsr"` (upscale) when presenting |
| `fullscreen` | `true` | Fullscreen mode; Alt+Enter or F11 switches between borderless fullscreen and a window while playing |
| `vsync` | `true` | Synchronize presentation with the display |
| `input_backend` | `"sdl"` | `"sdl"` for Xbox and PlayStation controllers through SDL, `"xinput"` for XInput only |
| `[controls]` | Automatic devices | The four players' devices, bindings, dead zones and vibration; see [Controls](#controls) |
| `graphics_device` | `""` | Graphics adapter name; empty keeps automatic selection |
| `skip_intro_videos` | `true` | Skip only the Ubisoft boot logo; story and attract videos use their original open path |
| `launcher_language` | `"en"` | The menus' language, independent of the game language |
| `custom_game_root` | `""` | The game chosen on the setup screen, a folder or an ISO; empty uses a `game/` folder beside the executable, or the repository's |
| `show_startup_menu` | `true` | Opens the setup screen before the game |

Other SDK options can be added to the file by hand; the menus keep the keys they do not know when they save. `log_level = "debug"` adds detail to the log.

## Renderer

The game draws with the native renderer: `renderer` is always `"native"`.
`native_backend` selects its graphics API, `"d3d12"` (the default) or
`"vulkan"`. Under Wine (Proton), the game uses Vulkan whatever `native_backend`
says and logs it, and the menus offer only Vulkan. If the selected API
cannot start on the graphics card or driver, the game tries the other one and
logs the refusal. Graphics cards are enumerated through the selected API; a
saved card that is no longer present falls back to the automatic choice. The frame rate and frame time are reported
in the game window title.

The menus also use these keys:

| Key | Default | Effect |
| --- | --- | --- |
| `bind_game_menu` | `"F1"` | Keyboard key that opens the game menu |
| `game_menu_chord` | `"Back+Start"` | Controller buttons that open it: `Back+Start`, `LB+RB+Start` or `L3+R3` |
| `show_shader_indicator` | `true` | Shows a notice while pipelines compile |
| `check_for_updates` | `true` | Checks the project's latest release at start and notifies when it is newer; Settings > Advanced also checks on request |
| `crash_reporting` | `false` | Sends crash reports to Sentry ([Crash reports](sentry.md)); set by the question at the first start or under Settings > Advanced > Diagnostics |
| `crash_reporting_asked` | `false` | The crash report question was answered; `true` keeps it from showing |
| `dlc_source_path` | `"dlc"` | Folder whose DLC packages the game installs at start; `"dlc"` is the data folder's `dlc/`; the game menu's **DLC** page stages packages there ([DLC](dlc.md)) |

### Native renderer options

These keys exist only in native renderer builds. Set in the menus, most apply at once (see [Game menu](#game-menu)); edited by hand, they take effect on the next start.

| Key | Default | Effect |
| --- | --- | --- |
| `msaa_samples` | `0` | Samples of the targets the game creates with MSAA: `0` keeps the game's, or `2`, `4`, `8`, limited to what the graphics card supports. Single-sample targets stay single-sample |
| `transparency_antialiasing` | `false` | In multisampled targets, alpha-tested draws use alpha to coverage instead of the alpha test, smoothing the edges of cut-out surfaces |
| `internal_resolution_scale` | `0.0` | Internal resolution from 1 to 4 with fractions (`1.5` turns 1280×720 into 1920×1080); `0` uses `resolution_scale` |
| `brightness` | `0.5` | Output brightness from 0 to 1; `0.5` is the game's own |
| `max_frame_latency` | `1` | Frames queued ahead of the display, 1 to 3; 1 has the lowest latency. Vulkan always uses 1 |
| `triple_buffering` | `false` | Three swapchain buffers instead of two |
| `graphics_device` | `""` | The graphics card, by the selected API's name; empty is automatic. The native log names both the backend and card |
| `frame_rate` | `30` | Frame rate from 30 to 240. `30` keeps the game's own; above 30 the renderer draws frames between the game's logic steps. See [Renderers](renderers.md#frame-rate) |
| `aspect_ratio` | `-1` | The image's shape, width over height: `-1` follows the monitor, `0` keeps the game's 16:9, or a ratio such as `1.6` (16:10), `2.3889` (21:9, 3440×1440) or `3.5556` (32:9). The 3D view widens or narrows to fill the screen without stretching; the HUD, menus and videos keep 16:9 proportions in the center. See [Aspect ratio](#aspect-ratio) |

For example, in `build/data/broken_bond.toml`:

```toml
renderer = "native"
msaa_samples = 4
transparency_antialiasing = true
internal_resolution_scale = 1.5
frame_rate = 60
```

## Aspect ratio

On a monitor that is not 16:9, **Settings → Display → Aspect ratio** fills the screen. **Automatic**, the default, uses the shape of the primary monitor, where the game opens, also when the game runs in a window; a 16:9 monitor keeps the original image.

| Monitor | 3D view | HUD, menus and videos |
| --- | --- | --- |
| 21:9, 32:9 | Wider, filling the screen | 16:9 area in the center; the sides show the 3D |
| 16:10 | About 5% less on each side, filling the screen | 16:9 area in the center, with thin bands above and below |

Full-screen effects such as fades and the pause dim cover the whole screen. HUD elements that sit at the edges of a 16:9 screen stay at the edges of the central area. The internal resolution follows the monitor's shape: at 2× a 21:9 monitor renders 3440×1440. The option needs a restart.

## User files

| Started with | Game files | Settings, saves and caches (the data folder) | Logs |
| --- | --- | --- | --- |
| `build/native/broken_bond.exe` | `custom_game_root`, else the repository's `game/` | `build/data/` | `build/data/logs/` |
| `scripts/run-game.ps1` | `game/` | `build/data/` | `build/logs/runtime-<date>.log` |
| A copy elsewhere, such as the ZIP | `custom_game_root` (a folder or an ISO), else a `game/` folder next to it | `data\` next to it | `data\logs\` |
| The Linux AppImage | `custom_game_root`, else a `game/` folder next to the AppImage | `~/.local/share/broken_bond/` (caches in `~/.cache/broken_bond/`) | its `logs/` |

The saves are in the data folder's `user/`. `user_data_root` in the file, or `--user_data_root`, names another saves folder in every case. A build in `build/native` and `run-game.ps1` share `build/data/`, so settings and saves carry over between them; its first start copies the earlier `build/user/` and `build/native/broken_bond.toml` into it. Installations of 0.1.0-beta kept their settings and saves beside the executable (`broken_bond.toml`, `user/`); the first start copies them into `data\` and keeps the originals. [Data folder](data-folder.md) has the details, moving the folder, and backups. Native runs started by `run-game.ps1` also write the renderer's journal next to the log (`.native.jsonl`) and the pipelines they created to `build/pipeline-runs/<run>/`.
