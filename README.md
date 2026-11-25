# EDGE PC Enhanced

A drop-in enhancement mod (`XINPUT1_3.dll`) for the Steam PC version of **EDGE**. It applies all fixes and new options at runtime without permanently modifying `edge.exe`.

Mod settings are stored separately in `%LocalAppData%\Two Tribes\EDGE\edgemod_settings.ini` (automatically migrated from `local_savedata.ttsav` on first launch), keeping your vanilla save files untouched.

## Features

### Graphics & Visuals
- **Rainbow & Prism Color Fix**: Restores animated rainbow colors on prisms, moving platforms, and goal tiles, even when using overlays like RTSS.
- **Color Themes (`color theme`)**: Switch live in-game between **`pc`**, **`mobile`** (Android), and **`java`** (J2ME/Extended) under **Options → Graphics → `color theme`** for platform-authentic textures, lighting, cube palettes, full-height wall gradients, and Mobile/Java prism styles.
- **Finish Effects & Finish Platform**: Choose between **`pc`**, **`mobile`**, and **`java`** level-complete cube effects (`finish effect`), plus **`pc`** or **`mobile`** finish platform colors (`finish platform`) under **Options → Graphics**.
- **Post-Processing & Anti-Aliasing**: Customize **`prism vfx`** (`off` / `short` / `medium` / `long`), toggle **`chromatic aberration`** (`off` / `on`), select **`anti-aliasing`** (`off` or `2x`–`16x` MSAA), and remove background blur on Pause and Level Complete screens.
- **Sharp UI Fonts**: Keeps 2D menu and HUD text crisp when using fractional UI scales (such as `1.25` or `1.5`) without changing native 3D game resolution.

### Display, Menus & HUD
- **UI Scale & Auto-Scale**: Under **Options → Display**, adjust **`ui scale`** (`0.5`–`2.5` in `0.25` steps) and toggle **`auto scale`** (`on` adapts to your display height using a 720p baseline; `off` uses your exact screen height). Custom values can also be set via `ui_scale` (`0.1`–`2.5`) and `ui_auto_scale` under `[graphics]` in `%LocalAppData%\Two Tribes\EDGE\settings.ini`.
- **60 FPS+ Smooth Menus & HUD**: Smoothly animates menus, fades, rainbow borders, 3D logo motion, minimap transitions, credits scrolling, the `"EDGE TIME"` counter, and a smooth two-layer slide-down/slide-up pause menu (including its background) on high-refresh-rate displays.
- **Reorganized & Widened Menus**: Categorizes **Options** into **Display**, **Graphics**, **Audio**, **UI** (with nested **Timer** and **Prism Counter** submenus), and **Gameplay** with sliders, popups, and `>` submenu arrows; moves **`instructions`** directly onto the Main Menu below **`options`**; widens menus to `368px`; auto-scrolls overflowing labels; and refines Level Select, Scores, and Pause layouts.
- **Mouse Hover & Subtle Rainbow Highlights**: Menu slots, popup choices, `back`/`go`/`skip`/`next` buttons, and Level Select tiles detect mouse hover with a subtle rainbow tint, a 2px animated rainbow border, and hover sound (leaving no slot hovered or neutral-outlined when the cursor moves off-slot, restoring the last slot on keyboard input, previewing hovered Level Select level info without sliding and launching on click while keeping the previously played level highlighted as pressed down, and switching hovered elements to strong rainbow while pressed).
- **Live Corner HUD Timer**: Under **Options → UI → Timer**, configure **`type`** (`off` / `level` / `rank` / `both`), **`size`** (`0.25`–`2`), **`background`** (`0`–`100`), **`drop shadow`** (`0`–`100`), and **`position`** (`TL` / `TR` / `BL` / `BR`) with a live preview (docked flush to the display edge during gameplay; hidden while paused unless previewing in **Timer** options, with the full timer embedded directly in the Pause menu header).
- **Corner HUD Prism Counter**: Under **Options → UI → Prism Counter**, configure **`type`** (`off` / `short` `count/total` / `full`), **`size`** (`0.25`–`2`), **`background`** (`0`–`100`), **`drop shadow`** (`0`–`100`), and **`position`** (`TL` / `TR` / `BL` / `BR`) with a live preview (docked flush to the display edge during gameplay and automatically dodging the minimap when visible; hidden while paused unless previewing in **Prism Counter** options). In **`full`** mode (and across Level Select, Pause Menu, and Scores cards), levels with more than 20 prisms automatically display `count/total` instead of individual prism squares.
- **Customizable Time Format**: Switch between **`def`** (`0'00"00`) and **`new`** (`0:00.00`) under **Options → UI → `time format`**.
- **Edge Timer Modes (`edge timer`)**: Under **Options → UI → `edge timer`**, choose **`2D`** (default), **`2D scaled`** (scales proportionally with camera zoom/dolly level), or **`3D`** (Android-style 3D floating edge timer anchored in world space next to the cube).

### Camera & Gameplay
- **Camera Distance & Dolly Zoom**: Under **Options → Gameplay**, adjust **`cam distance`** (`mobile`, `0.5`–`2`) and **`dolly zoom`** (`pc`, `mobile`, `0`–`50`), including true orthographic framing (`mobile` / `0`) for isometric levels like *Curious Land* without skybox or shadow artifacts.
- **All Levels & Bonus Modes Unlocked**: Unlocks Normal, Extended, and Bonus levels without requiring the broken Steam Community group check and without triggering Steam achievements. Unplayed levels show a light gray corner indicator and keep `"new game"` vs `"continue"` intact.
- **Skip Startup Intro**: Toggle **Options → Gameplay → `skip intro`** (`off` / `on`) to jump straight to the Main Menu.

### Audio
- **0–100 Volume Sliders**: Adjust **`sounds`** and **`music`** smoothly from `0` to `100` under **Options → Audio** (instead of just `off` / `on`), with working sound effects attenuation.
- **Music Start Timing**: Choose **`land`** or **`fall`** under **Options → Audio → `music start`**.
- **Java / PC OST Toggle (`music playlist`)**: Switch live between **`pc`** and **`java`** soundtracks under **Options → Audio → `music playlist`**, driven by an editable `music/playlist.xml` file (auto-generated on first launch if missing, with per-level auto-generation for custom or newly added levels) alongside extended custom music ID lookup (`music/<id>_*.ogg`).
- **Mute When Unfocused (`mute when unfocused`)**: Toggle **`off`** or **`on`** under **Options → Audio → `mute when unfocused`** to automatically mute game sounds and music whenever the window loses focus or is minimized.

---

## Installation

1. Make sure your game's `edge.exe` is unmodified.
2. Download the latest release `.zip` from the [Releases](https://github.com/bacsiemon/edge-game-pc-enhanced/releases) page and extract its contents (`XINPUT1_3.dll` and the `music/` folder containing `100_menus.ogg`..`111_planant.ogg`) into your EDGE installation folder (for example, `C:\Program Files (x86)\Steam\steamapps\common\EDGE`), merging folders when prompted.
3. Launch EDGE normally through Steam (`music/playlist.xml` is auto-generated automatically on launch).

To uninstall, delete `XINPUT1_3.dll` (and optionally `music/100_menus.ogg`..`music/111_planant.ogg` and `music/playlist.xml`) from the game folder.

---

## Building from Source

**Requirements:** Windows, Python 3, Visual Studio 2022 (with C++ Desktop / x86 build tools), and an unmodified `edge.exe` in your EDGE folder.

If EDGE is installed outside standard Steam library folders, set the `EDGE_GAME_DIR` environment variable or pass `--game-dir <path>`:

```powershell
# Build XINPUT1_3.dll and verify against unmodified edge.exe (updates releases\XINPUT1_3.dll)
py build_dll.py --stage

# Verify file hashes of edge.exe and XINPUT1_3.dll
py build_dll.py --verify

# Close EDGE first, verify edge.exe is unmodified, and install XINPUT1_3.dll
py build_dll.py --apply

# Remove XINPUT1_3.dll
py build_dll.py --restore
```
