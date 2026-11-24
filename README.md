# EDGE PC Mod (`edge-pc-mod`)

Modular C++ proxy DLL mod and reverse-engineering archive for the Steam PC version of **EDGE** (`edge.exe`, clean backup `2,714,624` bytes, SHA-256 `b73a0df8e07d47f3d3e4b57642514033c00449c0070876513323c257c3cabe9b`).

## Repository Layout

- `dll/`: Active 32-bit C++ `XINPUT1_3.dll` proxy & runtime hooks (`449` verified patches)
- `tools/`: Cached disassembly viewer (`safe_disasm.py`) and shared path/hash helpers (`common.py`)
- `docs/`: Memory map, research notes, patching guide, and options UI specification
- `releases/`: Local gitignored packaging folder (`README.txt`, built `XINPUT1_3.dll`, and optional `mods/` packs)
- `.agents/`: Directory patching workflow skill (`edge-pc-patching`)

## Included Fixes

1. **D3D9 Material / Rainbow Color Fix** (`dll/mod_core_fixes.cpp`)
   Restores `D3DTSS_TEXTURETRANSFORMFLAGS = D3DTTFF_COUNT2` across `Renderer::beginScene` and `Material::apply` so prisms, moving platforms, and goal tiles animate colors properly even with third-party overlays (e.g. RTSS).
2. **All Levels & Bonus Modes Unlocked** (`dll/mod_core_fixes.cpp`, `dll/mod_menu_camera_options.cpp`)
   - Removes dependency on the unreliable Steam Community group check when unlocking bonus levels.
   - Unlocks all modes and levels (`normal`, `extended`, and bonus slots) without triggering Steam achievements (`#BFBFBF` unplayed tile corner indicator, score cards shown only for completed levels, preserving `"new game"` vs `"continue"`).
3. **Adjustable UI Scale & Resolution Auto-Scale** (`dll/mod_ui_scale_rt.cpp`, `dll/mod_menu_camera_options.cpp`)
   - Keeps 3D rendering at native physical resolution while scaling 2D menus, HUD, cameras, scissor rects, viewports, and mouse hit-testing.
   - Adds lowercase **`ui scale`** and **`auto scale`** rows under Options -> **Display**:
     - **`ui scale`**: defaults to `1`; interactive `0.5`..`2.5` slider in `0.25` steps (`[0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 2.25, 2.5]`), committed cleanly on release without altering **`auto scale`**.
     - **`auto scale`**: toggles **`on`** (default, resolution-adaptive using `720p` baseline: `virtH = round(720 / scale)`) / **`off`** (fixed `physical_height / scale`).
   - Reads `[graphics]` in `%LocalAppData%\Two Tribes\EDGE\settings.ini`:
     - `ui_scale = <float>` (`0.1`–`2.5`; `0` or `< 0.1` clamps to `0.1`, `> 2.5` clamps to `2.5`; arbitrary floats such as `1.37` are honored directly until changed in-game).
     - `ui_auto_scale = true` / `false` (optional).
4. **Fractional UI Font Sharpening** (`dll/mod_ui_scale_rt.cpp`)
   Renders 2D UI via an integer supersampled render target (`uiRT`) with bilinear downsampling whenever scaling ratios are non-integer (fixing uneven stroke widths at `125%`/`150%`), while bypassing cleanly on exact integer multiples or dimensions above `4096px`.
5. **Orthographic / Dolly Camera Framing & Distance** (`dll/mod_menu_camera_options.cpp`)
   - Adds **`cam distance`** (`mobile`, `0.5`..`2`, default `1` = PC) and **`dolly zoom`** (`pc`, `mobile`, `0`..`50`, default `pc`, INI `camera_fov`, save `camFov`) to Options -> **Gameplay**, with `[graphics] camera_distance` & `camera_fov` in `settings.ini` and `local_savedata.ttsav` persistence.
   - Renders `mobile` and `0` (`<= 0°` authored isometric triggers) using true orthographic projection (`D3DXMatrixOrthoRH`, zero parallax error on Escher illusions such as `"curious land"`), and prevents shadow Z-fighting and skybox near-plane clipping/culling at orthographic and low FOVs.
6. **Customizable 0–100 Music & SFX Volume, Music Start & Extended Music IDs** (`dll/mod_core_fixes.cpp`, `dll/mod_menu_camera_options.cpp`)
   - Changes `sounds` and `music` from `off` / `on` to interactive `0`..`100` sliders (`5`-step Left/Right increments and mouse scrubbing with hover SFX `2`) and fixes XACT `"Effects"` -> `"Default"` so SFX volume attenuation works cleanly.
   - Adds **`music start`** (`land` / `fall`, `musicStart` in `local_savedata.ttsav`) under Options -> **Audio**.
   - Resolves extended music IDs `27..255` dynamically from `music/<id>_*.ogg` or `music/<id>.ogg` (used by packs such as `releases/mods/JavaMusic.edgemod`).
7. **60fps+ UI, Menu & HUD Timer Smoothing** (`dll/mod_ui_smooth_part1.cpp`, `dll/mod_ui_smooth_part2.cpp`)
   - Interpolates 30 Hz menu/pause/debrief/level-select slides, fades, rainbow cycles, 3D menu logo splines, minimap opacity, credits scroll, and the `"EDGE TIME"` counter cleanly across high refresh rates.
8. **Customizable Time Format** (`dll/mod_menu_camera_options.cpp`)
   - Adds **`time format`** (`def` = `0'00"00` / `new` = `0:00.00`) under Options -> **UI** across HUD, Demo, Scores, Level Select, and Debrief screens (`timeFmt` in `local_savedata.ttsav`).
9. **Skip Startup Intro & Categorized Options UI** (`dll/mod_menu_camera_options.cpp`)
   - Adds **`skip intro`** (`off` / `on`, `skipIntro` in `local_savedata.ttsav`): when `on`, opens the main menu immediately while the 3D `"EDGE"` logo rises on the right side.
   - Moves **`instructions`** from **edge challenge** (`ModeSelect`) to `Main Menu` row `3` (below `options`, shifting `credits` to row `4` and `exit game` to row `5`, with `InstructionsMenu` returning to `Main Menu`), organizes both Main and Pause `Options` into **Display**, **Graphics**, **Audio**, **UI** (with nested **Timer** submenu), and **Gameplay** categories with interactive toggles, sliders, and anchored popups, and draws a right-side `>` indicator on all submenu rows (`Main Menu`, Pause `options`, and nested `Options` categories).
10. **Post-Processing & Anti-Aliasing Controls** (`dll/mod_menu_camera_options.cpp`)
    - Adds **`prism vfx`** (`off` / `short` / `medium` / `long`), **`chromatic aberration`** (`off` / `on`), and **`anti-aliasing`** (`off` / `2x`..`16x` MSAA) to Options -> **Graphics**, and disables background blur on Pause/Debrief screens.
11. **Live Corner HUD Timer** (`dll/mod_menu_camera_options.cpp`)
    - Adds **`type`** (`off` / `level` / `rank` / `both`), **`size`** (`0.25`..`2`), and **`position`** (`TL` / `TR` / `BL` / `BR`) under Options -> **UI** -> **Timer** (with live sample preview in Main Options).
12. **Mobile & Java Finish Effects + Mobile Finish Platform** (`dll/mod_finish_effects.cpp`)
    - Adds **`finish effect`** (`pc` / `mobile` / `java`, `finishEffect` in `local_savedata.ttsav`) and **`finish platform`** (`pc` / `mobile`, `finishPlatform` in `local_savedata.ttsav`) under Options -> **Graphics**.
    - Renders Mobile's 23-color cycling outer/inner finish platform with 10-layer shimmering center glow squares, Mobile's 3D twisting rainbow pillar, or Java's tapered sky-stretching waving ribbon.
13. **Mobile Color Theme & Full-Height Wall Gradients** (`dll/mod_color_theme.cpp`)
    - Adds **`color theme`** (`pc` / `mobile`, `colorTheme` in `local_savedata.ttsav`, live switch in-game) under Options -> **Graphics**.
    - Procedurally synthesizes Android `levelwall_1..4`, `planet_1..4`, `fallingplatform_1..4`, and `switch*` textures in memory, applies Android single-light + `1.0×` ambient cube lighting and unlit bumpers, and scales wall gradients across each level's full `(heightTiles - 0.5) * 30` span on static level meshes, moving platforms, and bumpers across all 144 levels.
14. **Widened `368px` Menus, Layout Alignment & Marquee Auto-Fit** (`dll/mod_menu_camera_options.cpp`, `dll/mod_ui_smooth_part1.cpp`, `dll/mod_ui_smooth_part2.cpp`)
    - Widens the shared menu frame leftward from `320px` to `368px` (`0x170`) while preserving the right edge and rainbow border, aligns Pause root rows (`0x26` stride) with Options, and restores Pause collected-in-run prism colors.
    - Re-aligns Level Select (centered `6`-column level grid `[38..330]` uniform `45px` pitch, right-aligned score card `+271`, right-centered prism dots `16px` left of card & vertically centered for `<= 10` prisms), Scores (`3`-col cards), Instructions, Debrief, Confirm Exit, and Credits layouts.
    - Scrolls overflowing text cleanly via `DrawTextAutoFit` horizontal marquee across `21` option, button, world-name, and menu sites, and refines hover SFX feedback across sliders and Left/Right changes.

## Quick Usage (Active C++ Proxy DLL: `XINPUT1_3.dll`)

All 8 source modules in `dll/*.cpp` (`449` verified runtime hooks) compile via Visual Studio 2022 (`cl.exe` x86) into `XINPUT1_3.dll` (leaving `edge.exe` clean vanilla `edge.exe.bak` on disk and applying all hooks in memory at startup with no `.text`/`.rdata`/`.bss` cave limits).

If EDGE is installed outside standard Steam library folders, set `EDGE_GAME_DIR` in your environment or pass `--game-dir <path>` to `build_dll.py`.

```powershell
# Build XINPUT1_3.dll + verify_dll.exe and run offline expected-bytes checks
py build_dll.py --stage

# Verify hashes of edge.exe.bak, live edge.exe, and XINPUT1_3.dll
py build_dll.py --verify

# Close EDGE first, restore clean edge.exe, and install XINPUT1_3.dll into the game folder
py build_dll.py --apply

# Remove XINPUT1_3.dll and restore clean unmodified edge.exe
py build_dll.py --restore
```

## For Future Patches & AI Agents

- **Patching Playbook & Skill**: Read [docs/PATCHING_GUIDE.md](docs/PATCHING_GUIDE.md) and [.agents/skills/edge-pc-patching/SKILL.md](.agents/skills/edge-pc-patching/SKILL.md) for the step-by-step build/verification workflow and engine x86 pitfalls (`ret N` tail jumps, x87 FPU discipline, `std::wstring` SSO, INI/`ttsav` rules).
- **Pre-Verified Maps & Research**: [docs/MEMORY_MAP.md](docs/MEMORY_MAP.md) documents all verified PE addresses, tables, and globals; [docs/RESEARCH_NOTES.md](docs/RESEARCH_NOTES.md) summarizes past investigations; [docs/OPTIONS_UI.md](docs/OPTIONS_UI.md) documents the Options UI control specs.
- **Developer Tools & Caches**:
  - [AGENTS.md](AGENTS.md): repository workflow, Git hygiene, and caching rules.
  - [tools/safe_disasm.py](tools/safe_disasm.py): cached disassembly helper using persistent index `.cache/bak_disasm.sqlite`, `--func <va>` function-bound lookup, and auto start-alignment.
  - [tools/common.py](tools/common.py): shared backup hash/size constants and `EDGE_GAME_DIR` / WSL path helpers.
