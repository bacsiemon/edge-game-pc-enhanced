# EDGE PC Mod

A drop-in enhancement mod (`XINPUT1_3.dll`) for the Steam PC version of **EDGE**. It applies all fixes and new options at runtime without permanently modifying `edge.exe`.

## Features

### Graphics & Visuals
- **Rainbow & Prism Color Fix**: Restores animated rainbow colors on prisms, moving platforms, and goal tiles, even when using overlays like RTSS.
- **Mobile Color Theme**: Switch between **`pc`** and **`mobile`** under **Options → Graphics → `color theme`** (live in-game) for Android-style cube lighting, textures, and full-height wall gradients.
- **Finish Effects & Finish Platform**: Choose between **`pc`**, **`mobile`**, and **`java`** level-complete cube effects (`finish effect`), plus **`pc`** or **`mobile`** finish platform colors (`finish platform`) under **Options → Graphics**.
- **Post-Processing & Anti-Aliasing**: Customize **`prism vfx`** (`off` / `short` / `medium` / `long`), toggle **`chromatic aberration`** (`off` / `on`), select **`anti-aliasing`** (`off` or `2x`–`16x` MSAA), and remove background blur on Pause and Level Complete screens.
- **Sharp UI Fonts**: Keeps 2D menu and HUD text crisp when using fractional UI scales (such as `1.25` or `1.5`) without changing native 3D game resolution.

### Display, Menus & HUD
- **UI Scale & Auto-Scale**: Under **Options → Display**, adjust **`ui scale`** (`0.5`–`2.5` in `0.25` steps) and toggle **`auto scale`** (`on` adapts to your display height using a 720p baseline; `off` uses your exact screen height). Custom values can also be set via `ui_scale` (`0.1`–`2.5`) and `ui_auto_scale` under `[graphics]` in `%LocalAppData%\Two Tribes\EDGE\settings.ini`.
- **60 FPS+ Smooth Menus & HUD**: Smoothly animates menus, fades, rainbow borders, 3D logo motion, minimap transitions, credits scrolling, and the `"EDGE TIME"` counter on high-refresh-rate displays.
- **Reorganized & Widened Menus**: Categorizes **Options** into **Display**, **Graphics**, **Audio**, **UI** (with a nested **Timer** submenu), and **Gameplay** with sliders, popups, and `>` submenu arrows; moves **`instructions`** directly onto the Main Menu below **`options`**; widens menus to `368px`; auto-scrolls overflowing labels; and refines Level Select, Scores, and Pause layouts.
- **Live Corner HUD Timer**: Under **Options → UI → Timer**, configure **`type`** (`off` / `level` / `rank` / `both`), **`size`** (`0.25`–`2`), and **`position`** (`TL` / `TR` / `BL` / `BR`) with a live preview.
- **Customizable Time Format**: Switch between **`def`** (`0'00"00`) and **`new`** (`0:00.00`) under **Options → UI → `time format`**.

### Camera & Gameplay
- **Camera Distance & Dolly Zoom**: Under **Options → Gameplay**, adjust **`cam distance`** (`mobile`, `0.5`–`2`) and **`dolly zoom`** (`pc`, `mobile`, `0`–`50`), including true orthographic framing (`mobile` / `0`) for isometric levels like *Curious Land* without skybox or shadow artifacts.
- **All Levels & Bonus Modes Unlocked**: Unlocks Normal, Extended, and Bonus levels without requiring the broken Steam Community group check and without triggering Steam achievements. Unplayed levels show a light gray corner indicator and keep `"new game"` vs `"continue"` intact.
- **Skip Startup Intro**: Toggle **Options → Gameplay → `skip intro`** (`off` / `on`) to jump straight to the Main Menu.

### Audio
- **0–100 Volume Sliders**: Adjust **`sounds`** and **`music`** smoothly from `0` to `100` under **Options → Audio** (instead of just `off` / `on`), with working sound effects attenuation.
- **Music Start Timing**: Choose **`land`** or **`fall`** under **Options → Audio → `music start`**.
- **Java / PC OST Toggle (`music playlist`)**: Switch live between **`pc`** and **`java`** soundtracks under **Options → Audio → `music playlist`**, driven by an editable `music/playlist.xml` file (auto-generated on first launch if missing, with per-level auto-generation for custom or newly added levels) alongside extended custom music ID lookup (`music/<id>_*.ogg`).

---

## Installation

1. Make sure your game's `edge.exe` is unmodified.
2. Copy everything from `releases/` (`XINPUT1_3.dll` and the `music/` folder containing `100_menus.ogg`..`111_planant.ogg`) into your EDGE installation folder (for example, `C:\Program Files (x86)\Steam\steamapps\common\EDGE`).
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
