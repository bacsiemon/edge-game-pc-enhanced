# EDGE PC Enhanced

A drop-in enhancement mod (`XINPUT1_3.dll`) for the Steam PC version of **EDGE**. It applies all fixes and new options at runtime without permanently modifying `edge.exe`.

Mod settings are stored separately in `%LocalAppData%\Two Tribes\EDGE\edgemod_settings.ini` (automatically migrated from `local_savedata.ttsav` on first launch), keeping your vanilla save files untouched.

## Features

### Graphics & Visuals
- **Rainbow & Prism Color Fix**: Restores animated rainbow colors on prisms, moving platforms, and goal tiles, even when using overlays like RTSS.
- **Live Pause Color Cycling & Animations**: Pausing the game keeps color cycling (and select decorative animations) running smoothly across the Edge Timer (and `"EDGE TIME"` toast), moving platform borders, hint tiles, player cube, hint ghost (including hologram VFX, while preserving active hint replay progress across Pause instead of resetting it), cube idle distortion, prisms (including spinning), shrinkers (including animations), finish platform (including animations), and finish effect colors (Mobile/Java) while keeping gameplay and timers paused.
- **Themes & Theme Customization (`theme`, `customize theme`)**: Switch live in-game between **`pc`**, **`mobile`** (Android), **`java`** (J2ME/Extended), and **`custom`** under **Options → Graphics → `theme`**. Presets apply platform-authentic terrain palettes, wall gradients, planets, stars, cubes, hint ghosts, cube shatter effects, prisms, platforms, bumpers, switches, hint tiles, shrinkers, finish platforms, finish effects, and cube idle distortion all at once; selecting **`custom`** reveals the scrollable **`customize theme >`** submenu to mix and match all 19 individual visual options.
- **Main Menu Graphics Live Preview (`preview level`)**: Opening **Options → Graphics** from the Main Menu replaces the EDGE logo background with a live 3D showcase level (`levels/graphics_preview.bin`) featuring every game entity (including a hint ghost northwest of the `?` hint tile), with a **`preview level`** selector (**`light 1`**, **`light 2`**, **`dark 1`**, **`dark 2`**) at the top of the menu, live hover-to-preview on popup choices, and interactive triggers for **`prism vfx`**, **`edge timer`** (shown on hover), **`cube idle distortion`** (fast-forwarded on hover), **`cube shatter`** (plays on change or popup hover), and **`finish effect`** (plays on change or popup hover).
- **Edge Timer Modes (`edge timer`)**: Under **Options → Graphics → `edge timer`**, choose **`2D`** (default), **`2D scaled`** (scales proportionally with camera zoom/dolly level), or **`3D`** (Android-style 3D floating edge timer anchored in world space next to the cube).
- **Post-Processing & Anti-Aliasing**: Customize **`prism vfx`** (`off` / `short` / `medium` / `long`), toggle **`chromatic aberration`** (`off` / `on`), enable **`motion blur`** (`off` / `low` / `medium` / `high`) with selectable **`blur mode`** (`camera` / `object` / `both`, combining camera velocity and per-object velocity for rolling cubes, moving platforms, bumpers, and prisms), select **`anti-aliasing`** (`off` or `2x`–`16x` MSAA), and remove background blur on Pause and Level Complete screens.
- **Sharp UI Fonts**: Keeps 2D menu and HUD text crisp when using fractional UI scales (such as `1.25` or `1.5`) without changing native 3D game resolution.

### Display, Menus & HUD
- **Display Mode & Resizable Window (`display mode`)**: Under **Options → Display → `display mode`**, switch between **`fullscreen`**, **`windowed`**, and **`borderless`** (or press `Alt+Enter` to toggle between `windowed` and your last fullscreen/borderless mode). In **`windowed`** mode, drag any window edge or corner to resize freely; your window size is saved in `edgemod_settings.ini`, while `%LocalAppData%\Two Tribes\EDGE\settings.ini` (`screensize_windowed_width` / `screensize_windowed_height`) remains untouched and resumes taking effect if edited.
- **UI Scale & Auto-Scale**: Under **Options → Display**, adjust **`ui scale`** (`0.5`–`2.5` in `0.25` steps) and toggle **`auto scale`** (`on` adapts to your display height using a 720p baseline; `off` uses your exact screen height). Custom values can also be set via `ui_scale` (`0.1`–`2.5`) and `ui_auto_scale` under `[graphics]` in `%LocalAppData%\Two Tribes\EDGE\settings.ini`.
- **60 FPS+ Smooth Menus & HUD**: Smoothly animates menus, fades, rainbow borders, 3D logo motion, minimap rendering (including smooth moving platforms and sub-tile cube tracking), HUD toasts, scrolling text marquees, credits scrolling, loading/confirm screens, the `"EDGE TIME"` counter, and a smooth two-layer slide-down/slide-up pause menu (including its background) on high-refresh-rate displays.
- **Reorganized & Widened Menus**: Categorizes **Options** into **Display**, **Graphics** (with conditional scrollable **`customize theme`** submenu), **Audio**, **UI** (with nested **Timer**, **Prism Counter**, **Minimap**, and **Speedometer** submenus), and **Gameplay** with sliders, popups, and `>` submenu arrows; moves **`instructions`** directly onto the Main Menu below **`options`**; replaces the category rows in **`edge challenge`** and **`scores`** with Android-style category cards (featuring a 3-layer progress bar for completed, `S`-rank rainbow, and `S+`-rank main-menu-border colors, a next-level subtitle under **`continue`**, and a **`turbo mode`** toggle right above **`back`**); widens menus to `368px`; auto-scrolls overflowing labels; and refines Level Select (showing `1` rainbow dot for all prisms collected, `2` for `S` rank, and `3` for `S+` rank on level tiles), Scores, and Pause layouts.
- **Minimap Customization**: Under **Options → UI → Minimap**, toggle **`enable`** (`off` / `on`), adjust **`width`** (`0.5`–`2.5`), **`height`** (`0.5`–`2.5`), **`range`** (`0.5`–`2.5`, zooming coverage relative to the default `settings.ini` scale), and **`background`** (`0`–`100` opacity), and pick **`position`** (`TL` / `TR` / `BL` / `BR`) with a live/sample preview. Values in `%LocalAppData%\Two Tribes\EDGE\settings.ini` stay untouched and are only overridden when changed in the menu (and editing `settings.ini` later resumes taking effect).
- **Mouse Hover & Subtle Rainbow Highlights**: Menu slots, popup choices, `back`/`go`/`skip`/`next` buttons, and Level Select tiles detect mouse hover with a subtle rainbow tint, a 2px animated rainbow border, and hover sound (leaving no slot hovered or neutral-outlined when the cursor moves off-slot, restoring the last slot on keyboard input, previewing hovered Level Select level info without sliding and launching on click while keeping the previously played level highlighted as pressed down, and switching hovered elements to strong rainbow while pressed).
- **Live Corner HUD Timer**: Under **Options → UI → Timer**, configure **`type`** (`off` / `level` / `rank` / `both`), **`size`** (`0.25`–`2`), **`background`** (`0`–`100`), **`drop shadow`** (`0`–`100`), and **`position`** (`TL` / `TR` / `BL` / `BR`) with a live preview (docked flush to the display edge during gameplay; hidden while paused unless previewing in **Timer** options, with the full timer embedded directly in the Pause menu header). For completed levels, a gold-shimmer **`PB`** (Personal Best) rank automatically appears on the rank timer at your saved best time.
- **Corner HUD Prism Counter**: Under **Options → UI → Prism Counter**, configure **`type`** (`off` / `short` `count/total` / `full`), **`size`** (`0.25`–`2`), **`background`** (`0`–`100`), **`drop shadow`** (`0`–`100`), and **`position`** (`TL` / `TR` / `BL` / `BR`) with a live preview (docked flush to the display edge during gameplay and automatically dodging the minimap when visible; hidden while paused unless previewing in **Prism Counter** options). In **`full`** mode (and across Level Select, Pause Menu, and Scores cards), levels with more than 20 prisms automatically display `count/total` instead of individual prism squares.
- **Corner HUD Speedometer**: Under **Options → UI → Speedometer**, toggle **`enable`** (`off` / `on`) and customize **`size`** (`0.25`–`2`), **`background`** (`0`–`100`), **`drop shadow`** (`0`–`100`), and **`position`** (`TL` / `TR` / `BL` / `BR`, defaulting to `BR`) with a live/sample preview. Displays horizontal world-space cube speed (calibrated so `100` is max normal directional rolling speed, exceeding `100` on bumpers or moving platforms) alongside prism buff indicators (`>>` + fast rainbow during the active buff, `>` + slow rainbow during the trailing boost) and a 2-layer prism buff duration bar underneath.
- **Customizable Time Format**: Switch between **`def`** (`0'00"00`) and **`new`** (`0:00.00`) under **Options → UI → `time format`**.

### Camera & Gameplay
- **Camera Distance & Dolly Zoom**: Under **Options → Gameplay**, adjust **`cam distance`** (`mobile`, `0.5`–`2`) and **`dolly zoom`** (`pc`, `mobile`, `0`–`50`), including true orthographic framing (`mobile` / `0`) for isometric levels like *Curious Land* without skybox or shadow artifacts.
- **All Levels & Bonus Modes Unlocked**: Unlocks Normal, Extended, and Bonus levels without requiring the broken Steam Community group check and without triggering Steam achievements. Unplayed levels show a light gray corner indicator and keep `"new game"` vs `"continue"` intact.
- **Steam Leaderboard Fix for Modded Level Lists**: Maps official levels by level identity rather than `mapping.xml` list index so Steam leaderboard score submissions and links stay accurate even with EDGE Epic or custom level mods installed, and hides the Debrief **`leaderboard`** button on levels that do not have a Steam leaderboard.
- **Skip Startup Intro**: Toggle **Options → Gameplay → `skip intro`** (`off` / `on`) to jump straight to the Main Menu.
- **Turbo Mode (`turbo mode`)**: Toggle **`turbo mode`** (`off` / `on`) directly at the bottom of **`edge challenge`** or **`scores`** (available from the Main Menu only—cannot be toggled during gameplay), for Android's higher-speed Turbo physics (`2x` angular speed cap, faster acceleration, and bumper fall protection). While active, `"level"` labels in Level Select, Scores cards, Pause, and Debrief display as **`turbo`**. Turbo Mode uses a separate score, rank, and prism history (`%LocalAppData%\Two Tribes\EDGE\edgemod_turbo_scores.ini`, cleared separately via **Options → Gameplay → `reset turbo score`**, while Normal progress is cleared via **`reset normal score`**), does not upload results to Steam leaderboards, and hides the **`leaderboards`** button in both **`scores`** and Debrief.

### Audio
- **0–100 Volume Sliders**: Adjust **`sounds`** and **`music`** smoothly from `0` to `100` under **Options → Audio** (instead of just `off` / `on`), with working sound effects attenuation.
- **Music Start Timing**: Choose **`land`** or **`fall`** under **Options → Audio → `music start`**.
- **Java / PC OST Toggle (`music playlist`) & Multi-Format Music**: Switch live between **`pc`** and **`java`** soundtracks under **Options → Audio → `music playlist`**, driven by an editable `music/playlist.xml` file (auto-generated on first launch if missing, with per-level auto-generation for newly added levels not yet listed in the XML) alongside extended custom music ID lookup (`music/<id>_*.*`). Supports **`.ogg`**, **`.mp3`**, **`.wav`**, **`.flac`**, **`.mid`/`.midi`** (synthesized via SF2 SoundFonts), and additional Windows Media Foundation formats (**`.m4a`**, **`.aac`**, **`.wma`**, **`.aif`/`.aiff`**, **`.opus`**). Configure a custom SoundFont via `<soundfont path="..." />` in `music/playlist.xml` (or `[soundfont] path=` in `music/midi.ini`, defaulting to `Bada_Mobile_0300_base.sf2`), and tune MIDI synthesis quality, interpolation, ModWheel vibrato, stereo reverb/chorus, auto-leveling/gain, and seamless/`CC 111` looping in the auto-generated, hot-reloaded `music/midi.ini`. Leaving a `<level>`, `<menu name="title">`, or `<menu name="debrief">` entry's track empty/omitted keeps that level or menu silent (and when debrief has no music track, the level's music continues playing uninterrupted without ducking).
- **Instant Debrief Music & Refined Hint Ghost SFX**: Debrief music starts immediately at 100% volume instead of fading in, and the hint ghost sound effects are attenuated and looped seamlessly.
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
