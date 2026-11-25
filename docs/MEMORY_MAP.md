# EDGE Binary Reference & Allocated Slots

Verified against unmodified Steam `edge.exe` (`2,714,624` bytes, SHA-256 `b73a0df8e07d47f3d3e4b57642514033c00449c0070876513323c257c3cabe9b`).

## C++ Proxy DLL Architecture (`dll/` -> `XINPUT1_3.dll`)

When deployed via `py build_dll.py --apply`, `edge.exe` stays 100% clean vanilla (`2,714,624` bytes, SHA-256 `b73a0df8e07d47f3d3e4b57642514033c00449c0070876513323c257c3cabe9b`), and `XINPUT1_3.dll` (built with 32-bit MSVC `cl.exe` x86) forwards all 11 standard/undocumented `XInput` ordinals (`2..8`, `100..103`) to `SysWOW64\xinput1_3.dll` while applying all `521` hooks in memory on `DLL_PROCESS_ATTACH`. None of the `.text` code caves (`Block A`..`Block Z`), `.rdata` slack (`0x5e2840..0x5e2a00`), or `.bss` page tail (`0x5fa800..0x5faa36`) are used; all mod globals, strings, jump tables, and logic live inside `XINPUT1_3.dll`:
- `dll/xinput_proxy.cpp` & `dll/xinput1_3.def` & `dll/patch_utils.h`: XInput forwarding, `EdgeWString`/`EdgeString` helpers, `VirtualProtect` patch installer, and `EdgeMod_VerifyBuffer`.
- `dll/mod_core_fixes.cpp`: Bonus-levels unlock, D3D9 texture-transform color fix, `0..100` Music/SFX volume (`"Default"` XACT category), and `"mute when unfocused"` WndProc focus tracking (`0x44d72f` -> `hk_GameWndProc`).
- `dll/mod_music_playlist.cpp`: XML-backed Java/PC OST playlist (`music/playlist.xml` auto-generation, hot-reload, missing-level append, live switch, and extended `27..255` music ID lookup via `0x473e60` & `0x476205`).
- `dll/mod_ui_scale_rt.cpp`: Physical vs virtual 2D scaling core and offscreen `uiRT` supersampling.
- `dll/mod_menu_camera_options.cpp`: Categorized `OptionsMenu`/`PauseMenu` Page 1 (`Display` / `Graphics` / `Audio` / `UI` -> `Timer` & `Prism Counter` / `Gameplay`, 3-depth Pause swipe) with toggles, sliders, anchored popups, and `>` submenu arrows, `MainMenu` 6-row layout (`edge challenge`, `scores`, `options`, `instructions`, `credits`, `exit game`, with `>` on submenu rows + `InstructionsMenu`/`CreditsMenu -> MainMenu`, `ModeSelect` 5 rows with `>` on `normal`/`extended`/`bonus`, `ScoresSelect` with `>` on `normal`/`extended`/`bonus`), Pause P0 `options >` (row `3`), unlock all levels/modes without achievements (`0x4944fa`, `0x4b5fcd`, `0x4b6cb0`, `0x4b824d`, `0x4b8b02`), global `320px -> 368px` (`0x170`) menu widening + centered `6`-column Level Select (`[38..330]`, uniform `45px` pitch) / Pause `P0` `0x26` alignment + `DrawTextAutoFit` marquee (`17` constant sites + `MainMenu`/`ModeSelect`/`ScoresSelect`/`Pause P0` row wrappers), `> 20` prism `count/total` display across Level Select (`0x4b869c`), Pause (`0x4ae367`), and Scores card flip (`0x4b0991`), standalone `%LocalAppData%\Two Tribes\EDGE\edgemod_settings.ini` persistence (`schema_version = 1`, atomic `.tmp` + `MoveFileExW` save, unconditional startup load via `0x492ef6` `hk_loadUserSettingsLocal`, one-time legacy `local_savedata.ttsav` migration at `0x492558` `hk_loadTtsav`, `0x4926e7` `hk_saveTtsav` removed so mod writes never touch `local_savedata.ttsav`), `settings.ini` override reconciliation (`0x476fea`), `prism vfx`, `chromatic aberration`, `anti-aliasing` (MSAA + blur removal), `cam distance`/`dolly zoom` (true `D3DXMatrixOrthoRH` via `0x4357f7` + `Frustum::setFovAspect` `0x435750` for `mobile`/`0` + skybox/Z-precision fixes), `music start`, `mute when unfocused` (`mute_unfocused`), `time format`, `skip intro`, live corner HUD timer (`type` / `size` / `background` / `drop shadow` / `position`), and live corner HUD prism counter (`type` / `size` / `background` / `drop shadow` / `position`) docked flush to the display edge with minimap overlap avoidance.
- `dll/mod_finish_effects.cpp`: Mobile finish platform renderer (`finishPlatform`) and mobile/Java win particle/cube animations (`finishEffect`).
- `dll/mod_color_theme.cpp`: Runtime Android (`EDGE-v4.0.1-full.apk` / `Edge-EX-v4.0.3-full.apk`) and Java (`edgeextend_eznuvsa7.jar` / `Edge_J2ME_EN_v4341.jar`) color themes (`colorTheme`, 22 hooks): procedural `IDirect3DTexture9` replacement (`levelwall_1..4` from `tileset_res_01..04.png` / `shadowall_01..04.spr`, `planet_1..4` from `shadow.spr`, `fallingplatform_1..4` from `pff.spr` with Java flat-top bevels and `Hook_FallingPlatformRender` `0x47b2da -> 0x486b20` Android live Y-gradient buckets, `switch*` with Android `#B0B8C4` / Java `#AEC3D0` top/pressed color), `Hook_SwitchRender` (`0x47b309 -> 0x485c30`) syncing unpressed switch side vertex colors for Android (`#94A1B4` left / `#838D9C` right) and Java (`#90ACBE` left / `#5A788D` right) while restoring PC colors, separate Java per-entity `levelwall_1..4` overrides and Android/Java moving platform rainbow borders (`SyncMovingPlatEdgeUVs` via `Hook_MovingPlatformRender` `0x47b2aa` / `plateform.spr`), `Hook_BumperRender` (`0x47b269` / `bumpers.spr` + smooth alpha-blended top-tile flash `RenderSmoothBumperRoofFlash` across all themes), Android/Java shrinkers (`Hook_ShrinkerRender` `0x47b359 -> 0x486f50`), Java holo switches (`Hook_HoloSwitchRender` `0x47b330 -> 0x486150`) & dimmed gameplay stars, Android (`205`) / Java (`194`) directional Light 0 + Dark Cube `ScopedMobileDarkCubeLight` (`0x47b3f8 -> 0x48c630` & `0x47b406`) and scoped Java player/ghost cube lighting (`RenderPlayerCubeWithColorTheme` chained from `0x47b37b` `hk_playerCubeRender`, `0x47b406`, `0x490bd5` Diffuse + `0x490c82` Ambient `254`), Java pastel/normal cube palettes (`0x48b98c`, `0x490ccf`), unlit idle bumpers + lit triggered bumper pistons (`0x47b269`), full-level-height `(heightTiles - 0.5) * 30` wall gradients on Mobile vs 4-tile wall depth on Java, plus Android 18-color and Java 9-color prism rendering (`0x47b3b9`, `0x47b3d5`, `0x437289`, `0x4372d9`, `0x40654f`, `0x406565`).
- `dll/mod_ui_smooth_part1.cpp` & `dll/mod_ui_smooth_part2.cpp`: 60/unlimited-FPS interpolation for menus, HUD, minimap, fades, rainbow highlights, `DebriefMenu`, `ScoresMenu`, `CreditsMenu`, `LevelSelectMenu`, `ConfirmExitGameMenu`, and Edge Time counters (plus `GetEdgeTimerSmoothState` shared with `mod_edge_timer.cpp`).
- `dll/mod_edge_timer.cpp`: `"edge timer"` (`2D` / `2D scaled` / `3D`, saved in `edgeTimer`) — intercepts `0x47b648`/`0x47b671` to render `2D` and `2D scaled` in `D3DFVF_XYZRHW` viewport pixels independent of `ui scale` (`ComputeEdgeTimerZoomScale` for camera zoom scaling), or suppresses 2D and renders Android `GameStateInGame::DrawEdgeCounter3D` (`0x47b418` via `mod_finish_effects.cpp`) in world space with frame-rate-independent smoothing, 4-direction `XY`/`YZ` orientation planes, bounce, and `def`/`new` separator formats.
- `dll/mod_menu_hover.cpp`: Menu slot, popup, `Menu::drawButton`, `CreditsMenu`/All Levels Completed `"NEXT"`/`skip` (`minEnter = 0`), and Level Select tile mouse hover detection (`0x49c9a2` `MenuScreen::update`, `0x49c8a9` `MenuScreen::render`, `0x4b9dd9`/`0x4b9412`/`0x4b2181` bar-skip guards, `0x4961dd`/`0x49628d` `Menu::drawButton` hover alpha & border, `0x4b672a` Level Select card slide-reset skip, `0x4b68eb` single-click launch, `0x4b8125` previous-played/pressed tile check, plus NOP'd button press-down SFX `2` at `0x4b67e9`/`0x4af221`/`0x4b3c95`/`0x4a170f`) with subtle (`25%`) vs strong (`100%` pressed) rainbow highlights, 2px `0x495f30` rainbow borders on hovered slots/buttons/tiles, off-slot unhovering, keyboard last-slot restore, and stationary Pause P0 (`0x4ad2ce`) / Debrief (`0x4abce2`) option labels without selected left-shift.

The tables below document both the engine addresses/structs (used directly by the C++ DLL) and the legacy static PE slot allocations (historical reference from the legacy static patcher).

## PE Sections (`va2off`)

| Section | Virtual Address Range | File Offset Base |
| :--- | :--- | :--- |
| `.text` | `0x401000` .. `0x576600` | `0x400 + (va - 0x401000)` |
| `.rdata` | `0x577000` .. `0x5e2a00` | `0x175a00 + (va - 0x577000)` |
| `.data` / `.bss` | `0x5e3000` .. `0x5f8800` | `0x1e1400 + (va - 0x5e3000)` |

## Allocated Padding & Unused Functions

Keeping all mod routines inside verified unused ranges avoids resizing PE sections or breaking relocations.

| Slot Name | Virtual Address Range | Capacity | Owner Module |
| :--- | :--- | :--- | :--- |
| Zero trailer | `0x5765b0` .. `0x576600` | `80` B | `patches/d3d9_color_fix.py` (`Material::apply` & `Renderer::beginScene` helpers) |
| Legacy mode block | `0x407e90` .. `0x408382` | Contiguous region | `ui_scale_core.py`, `ui_rt_sharpening.py`, `ui_scale_menu_ini.py` (`FORMAT_FLOAT_WSTR_VA` at `0x408125`) |
| `Block A` | `0x41f220` .. `0x41f3ca` | `426` B | `ui_rt_sharpening.py` (COM helpers, `syncSepAlpha`, `releaseUiRT`, `calcUiRTSize`) |
| `Block B` | `0x4061c8` .. `0x406404` | `572` B | `ui_rt_sharpening.py` (`begin2D_tail`) |
| `Block C` | `0x45eae0` .. `0x45edc2` | `738` B | `ui_rt_sharpening.py` (`end2D_hook`) |
| `Block D` | `0x484c10` .. `0x484e5f` | `591` B | `ui_scale_menu_ini.py` (`readIniSettings`, `uiScaleAction`, `autoScaleAction`, `pauseDrawText` at `0x484e38`) |
| `Block E` | `0x539770` .. `0x539a19` | `681` B | `ui_scale_menu_ini.py` (`ttsav` load/save, UI text helpers, `drawAutoScaleRow`) |
| `Block F` | `0x479010` .. `0x4791cb` | `443` B | `camera_angle.py` (`checkInLevel`, `pushDeff`, `perspScaleY`, `camDistAction`, `camFovAction`, `cycleFloatList`, `fovPresets`, `c_tbl`) |
| `Block G` | `0x52c560` .. `0x52caa0` | `1,344` B | `camera_angle.py` (`mapFov`, `camFovVertigoHook`, INI/ttsav parsers & loaders, Main/Pause row 7-8 drawers, descriptors & strings) |
| `Block H` | `0x4248e0` .. `0x4249b2` | `210` B | `camera_angle.py` (`saveActiveMul`, `adjustNearFarHook`) |
| `Block H2` | `0x4234d0` .. `0x4235e0` | `272` B | `camera_angle.py` (`skyboxProjWrap` at `0x407805`) |
| Pause Page 1 render | `0x4ade56` .. `0x4adf53` | `253` B | `ui_scale_menu_ini.py` (`PauseMenu` Options rows `ui scale`, `auto scale`, `back` + hook `0x4ade5b`) |
| Audio row 0-1 render | `0x4b2fdf` .. `0x4b307c`, `0x4b3168` .. `0x4b31fc` | `157` B, `148` B | `audio_volume.py` (Main Options `sounds`/`music` `0..100` render, `stepVol10`, `applySfxVolume`, `f64_100`) |
| Pause audio row 0-1 | `0x4ad756` .. `0x4ad80d`, `0x4ad8e7` .. `0x4ad99e` | `183` B, `183` B | `audio_volume.py` (Pause Page 1 `0..100` render, `clampVol`, `fmtInt0to100Wstr`, `scaleMusicVol`, `applyMusicVolume`) |
| `toggleSfx`/`Music` | `0x494610` .. `0x4946c0` | `176` B | `audio_volume.py` (`sfxVolAction`, `musicVolAction`, `applyVolOnLoad`) |
| `Block I` | `0x453fa0` .. `0x4542cd` | `813` B | `ui_smooth_partial.py` (float constants, `sm_trk_tbl`, float trackers, `onSavePrev`, `onInterpolate`) |
| `Block J` | `0x4fca10` .. `0x4fcc34` | `548` B | `ui_smooth_partial.py` (`smooth_counter_pair`, `calcSubFadePct`, `pauseSmoothSlideY`) |
| `Block K` | `0x44df40` .. `0x44e154` | `532` B | `ui_smooth_partial.py` (`getSmoothWsFrame`, `calcSmoothRainbow`, `hudFlashSmooth`, `hudRainbowSmooth`) |
| `Block L` | `0x412f60` .. `0x4130de` | `382` B | `ui_smooth_partial.py` (`pauseSmoothX`, `pauseConfirmSmooth`, `endFlashAlpha`, `endFadePct`) |
| `Block M` | `0x43b060` .. `0x43b1b8` | `344` B | `ui_smooth_partial.py` (`minimapSmoothDraw`, `mmSkipIn`/`Out`, `mmForceVb`, `pauseP0Skip`/`pauseP1Skip`) |
| `Block N` | `0x45cc80` .. `0x45cd9d` | `285` B | `ui_smooth_partial.py` (`toastAlphaSmooth`, `pauseP2Skip`, `pauseP2X`) |
| `Block O` | `0x541b10` .. `0x541c2c` | `284` B | `ui_smooth_partial.py` (`calcSubSlideY` for `MainMenu`, `ModeSelect`, `s_4b28b0`, `s_4b1aa0`) |
| `Block P` | `0x5347d0` .. `0x5348ec` | `284` B | `ui_smooth_partial.py` (`onInterpolate_MS` for `MenuScreen` models/splines & `LevelSelect`) |
| `Block Q` | `0x4f79c0` .. `0x4f7ad6` | `278` B | `ui_smooth_partial.py` (`interpModelNodeT`, `msScrollSmooth`, `lsScrollSmooth`) |
| `Block R` | `0x51bdc0` .. `0x51bed5` | `277` B | `ui_smooth_partial.py` (`onSavePrev_MS` for `MenuScreen` models/marquee & `LevelSelect+0x50`) |
| `Block S` | `0x453520` .. `0x4537a0` | `640` B | `ui_smooth_ext.py` (`snap_sub_state`, `onSavePrev_Ext`, `ext_upd_pair`, `onInterpolate_Ext`) |
| `Block T` | `0x45dc20` .. `0x45df90` | `880` B | `ui_smooth_ext.py` (`scoresRenderWrap`, `creditsRenderWrap`, `scoresCardFlip`, `scoresDotRainbow`, `instrSubScrollY`, `lsThumbEdx`/`Ecx`) |
| `Block U` | `0x510d30` .. `0x510fe0` | `688` B | `ui_smooth_ext.py` (`calcSubExitOverlayAlpha`, `debriefFillExitRgba`, `drawBtnRainbow`, `instrSRainbow`, `cegBoxHeight`, `cegFadePct`, `cegBlackoutAlpha`) |
| `Block V` | `0x44bad0` .. `0x44bc00` | `304` B | `ui_smooth_ext.py` (`lsTileFlashPct`, `lsTileBadgeRainbow`, `lsInfoCardPct7`, `lsInfoCardSlideX`) |
| `time_fmt_data` | `0x44bc00` .. `0x44bd20` | `288` B | `time_format.py` (`Block V` tail: modern format strings, `def`/`new` wstrings, `mainJtbl`) |
| `Block W` | `0x511030` .. `0x511330` | `768` B | `ui_smooth_ext.py` (`get_deb_eff44`, `debColorStage`, `debTitleRb`, `debRankSlideX`, `debBoxHeight`, `debBarWidth`, `debRankOverlay`) |
| `time_fmt_code` | `0x511330` .. `0x5114b0` | `384` B | `time_format.py` (`Block W` tail: `timeFmtPush`, `fmtSelEdge`, `sepScaleYPush`, `sep2WstrPush`, `timeFmtAction`, `drawRow9Main`, `pauseDrawTimeFmt`, `loadTimeTtsav`, `saveTimeTtsav`) |
| `Block X` | `0x4faa80` .. `0x4fabd0` | `336` B | `ui_smooth_ext.py` (`debNrScale`, `debNrMarquee`, `debRankStamp`, `debBtnQuadAlpha`) |
| `skip_intro_data` | `0x4fabd0` .. `0x4face0` | `272` B | `skip_intro.py` (`Block X` tail: `"skipIntro"` key, `u"skip intro"` wstring, `mainJtbl`, `initSkipIntro`, `getCamIntroT`, `getCamIntroT1`, `evalSplineCurve`) |
| `Block Y` | `0x5423f0` .. `0x542610` | `544` B | `ui_smooth_ext.py` (`hudTimerSetup`) |
| `skip_intro_code` | `0x542610` .. `0x542750` | `320` B | `skip_intro.py` (`Block Y` tail: `skipIntroAction`, `drawRow10Main`, `pauseDrawSkipIntro`, `loadSkipTtsav`, `saveSkipTtsav`) |
| `Block Z` | `0x4fa5d0` .. `0x4fa8f0` | `800` B | `ui_smooth_ext.py` (`hudTimerDigitsAndColor`, `hudRestoreMat`, `edgeDemoSetup`) |

## `.rdata` Allocations (`0x5e2840` .. `0x5e29f8`)

| VA | Size | Contents |
| :--- | :--- | :--- |
| `0x5e2840` | `9` B | `"ui_scale\0"` (INI key) |
| `0x5e2849` | `14` B | `"ui_auto_scale\0"` (INI key) |
| `0x5e2857` | `9` B | `"uiScaleF\0"` (save key) |
| `0x5e2860` | `12` B | `"uiScaleFIni\0"` (save key) |
| `0x5e286c` | `7` B | `"uiAuto\0"` (save key) |
| `0x5e2873` | `10` B | `"uiAutoIni\0"` (save key) |
| `0x5e2880` | `18` B | `u"ui scale\0"` (UTF-16LE) |
| `0x5e2894` | `24` B | MSVC `std::wstring` header (`ptr=0x5e2880, len=8, cap=8`) |
| `0x5e28ac` | `22` B | `u"auto scale\0"` (UTF-16LE) |
| `0x5e28c4` | `24` B | MSVC `std::wstring` header (`ptr=0x5e28ac, len=10, cap=10`) |
| `0x5e28e0` | `36` B | `9` IEEE-754 floats (`0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 3.0, 4.0`) |
| `0x5e2904` | `32` B | `distPresets`: `8` IEEE-754 floats (`-1.0` = `mobile`, `0.5, 0.65, 0.75, 1.0, 1.25, 1.5, 2.0`; `0x5e2924..0x5e2926` `0x00` pad) |
| `0x5e2926` | `10` B | `"mobile\0"` (`7` B), `"pc\0"` (`3` B) |
| `0x5e2930` | `24` B | SSO MSVC `std::wstring` `u"pc"` (`len=2, cap=7`) |
| `0x5e2948` | `24` B | SSO MSVC `std::wstring` `u"mobile"` (`len=6, cap=7`) |
| `0x5e2960` | `56` B | D3D9 render-state / texture-stage-state tuple table for `end2D_hook` |
| `0x5e2998` | `40` B | `JTBL_9`: `10`-entry `OptionsMenu` / `PauseMenu` Page 1 action jump table (`8`-entry without `--camera-angle`, with `credits` moved to `MainMenu`) |
| `0x5e29c4` | `24` B | `u"dolly zoom\0"` (UTF-16LE, 22B + 2B pad) |
| `0x5e29dc` | `24` B | MSVC `std::wstring` header (`ptr=0x5e29c4, len=10, cap=10`) |
| `0x5e29f4` | `12` B | `fltP1` (`0.1f`), `flt10` (`10.0f`), `fltTeps` (`0.0005f`) |
| `0x585474` | `8` B | Replaced `"Effects\0"` with `"Default\0"` (`audio/edge.xgs` SFX XACT category) when `--audio-volume` is active |

## `.data` / `.bss` Globals (`0x5f8730` .. `0x5f87a5` & `.bss` Tail `0x5fa800` .. `0x5fa820`)

> **Note**: `0x5f8688..0x5f8728` contains a stock EDGE registration table with zero-gap fields (`0x5f8714`); never allocate inside `0x5f8688..0x5f8728`.

- `0x5f8730` (`DWORD`): `gAutoScale` (`0` = `off`, `1` = `on`, initialized to `1`)
- `0x5f8734..0x5f8740` (`4 x DWORD`): `gUiRT_ptr`, `gUiRT_ctrl`, `gSavedRT`, `gSavedDS`
- `0x5f8798` (`float`): active `gUiScale` (legacy static patcher uses `[0.1f, 10.0f]`; C++ DLL clamps to `[0.1f, 2.5f]` with presets `0.5..2.5`, defaults to `1.0f`)
- `0x5f879c` (`float`): `gIniUiScale` (`0` if absent in `settings.ini`)
- `0x5f87a0` (`float`): `gLastIniScale` (tracks last applied `ui_scale` from INI)
- `0x5fa800` (`float`): `gCamFov` (`-1.0f` = `pc` default, `-2.0f` = `mobile`, or `[0.0f, 90.0f]` degrees; initialized in `iniCamHook`)
- `0x5fa804` (`float`): `gIniCamFov` (`-99.0f` if absent in `settings.ini`, else `-2.0f`/`-1.0f`/`[0.0f, 90.0f]`)
- `0x5fa808` (`float`): `gLastIniFov` (tracks last applied `camera_fov` from INI; `-99.0f` if unset)
- `0x5fa810` (`float`): `gCamDist` (`-1.0f` = `mobile` ortho distance, or `[0.1f, 10.0f]` multiplier around PC default `1.0f`)
- `0x5fa814` (`float`): `gIniCamDist` (`0.0f` if absent in `settings.ini`, else `-1.0f`/`[0.1f, 10.0f]`)
- `0x5fa818` (`float`): `gLastIniDist` (tracks last applied `camera_distance` from INI; `0.0f` if unset)
- `0x5fa81c` (`byte`): `gCamInited` (zero-initialized `.bss` flag ensuring one-time startup defaults)
- `0x5fa820` (`float`): `gActiveMul` (active camera distance multiplier from `GameCamera::Update`, used by `adjustNearFarHook`)
- `0x5fa824` (`float`): `gUiAlpha` (clamped `[0.0f, 1.0f]` render interpolation alpha captured in `ScreenMgr::interpolate`)
- `0x5fa828` (`byte`): `gJustTicked` (set in `onSavePrev`, cleared at end of `onInterpolate`)
- `0x5fa82c..0x5fa89b` (`7 x 16B`): `{ptr, prev, tgt, last}` float trackers (`pp304+0x4c/+0x68`, `ScreenMgr+0x8c/+0x34`, `WorldScreen+0x1c/+0x20/+0x24`)
- `0x5fa89c..0x5fa8d3`: active submenu/WorldScreen/minimap/toast snapshot fields (`gSubPtr`, `gSubPrev4`, `gSubPrev8`, `gSubPrev4c`, `gEndPrev3c`, `gEndPrev40`, `gWsPtr`, `gWsPrev4`, `gWsPrev2108`, `gWsTgt2108`, `gWsBuilt2108`, `gWsMmInterp`, `gWsPrevToast2b`, `gWsPrevToast34`)
- `0x5fa8d4..0x5fa95f`: MenuScreen / rainbow / LevelSelect smooth state (`gRbTmp` at `0x5fa8d4`, `gMsPrevC` at `0x5fa8d8`, `gSubPrev50` at `0x5fa8dc`, and `gMm0[4]` 32-byte MenuScreen model snapshot slots at `0x5fa8e0..0x5fa95f`)
- `0x5fa960..0x5faa35`: extended smooth state, time format, & skip intro (`gExtSubPtr`, `A`/`B` prev/last/eff triplets, `gExtWsPtr`, `gWsPrevEc/Et0..Y1`, `gEtRemTmp`, `gEtTicksTmp`, `gLsFlashPrev[64]`, `gHudMatOrig[16]`, `0x5faa34` `byte` `gTimeFmt`: `0` = `def`, `1` = `new`, `0x5faa35` `byte` `gSkipIntro`: `0` = `off`, `1` = `on`)
- `0x5f87a4` (`int8`): `gIniAutoScale` (`-1` if absent, `0`/`1` otherwise)
- `0x5f87a5` (`int8`): `gLastIniAuto` (tracks last applied `ui_auto_scale` from INI)

## Key Engine Symbols & Struct Offsets

- **`Renderer`**: global ptr `0x5f8770`; `[esi+0x48]` active `Camera*`, `[esi+0x58]` skybox `Model*`, `[esi+0xb8]` physW, `[esi+0xbc]` physH, `[esi+0xc0]` active flag, `[esi+0xcc]` `IDirect3DDevice9*`, `[esi+0x18/+0x1c]` cached blend modes, `0x405b30` virtual width getter, `0x405b60` virtual height getter, `0x406190` `updateCameras`, `0x407010` `update2DCamera`, `0x4075f0` `renderSkybox`, `0x4077c0` `begin3D` (`0x407805` calls `renderSkybox`), `0x405910` `setD3DBlend`.
- **`ScreenMgr`**: global ptr `0x5f9080`; `[esi+0x24/+0x28]` virtual W/H, `[esi+0x2c/+0x30]` secondary W/H, `[esi+0xb3]` left-direction flag in `OptionsMenu` input, `+0x298` `UserSettings` subobject; `0x4815f0` plays menu sound (`0xc`); `0x4927d0` saves user settings.
- **`IniFile` / `ErrorStatus` inside `Settings::loadIni` (`0x476670`)**:
  - `ebp` frame active at hook `0x476fea`: `[ebp-0x110]` `IniFile*`, `[ebp-0x100]` `ErrorStatus*`, `[ebp-0xf8]` `hasError` byte.
  - `0x4458c0`: `ErrorStatus::reset` (`__thiscall`, 0 stack args).
  - `0x4014e0`: `std::string(cstr)` (`__thiscall`, `ret 4`); `0x51e620`: `std::string::~string()`.
  - `0x4f5d70`: `IniFile::getValue(&out_str, &sec_str, &key_str)` (`__thiscall`, `ret 0xc`). Always `call 0x4f5d70; ret` (never tail-jump after pushing 3 args).
  - `0x4f4fd0`: `parseFloat(&str, &ErrorStatus)` (`__cdecl`, `add esp, 8` -> `st(0)`).
  - `0x4f4980`: `parseBool(&str, &ErrorStatus)` (`__cdecl`, `add esp, 8` -> `al`).
- **Localization & UI Text**:
  - `0x49fd70`: `getLocalizedString(&out_wstr, id)` (`__cdecl`, `add esp, 8`), ID `0xb` = `on`, ID `0xc` = `off` (lowercase in English).
  - `0x4a6c80`: `Font::drawText` (`std::wstring*`, `__cdecl`, `add esp, 0x20`); `0x4a6a20`: `Font::drawGlyph`; `0x4a5bb0`: `Font::initCharMap` (`font.bin` loaded via `0x4a6407`).
  - `0x4ba62a`: CRT `free` (`__cdecl`, `add esp, 4`).
- **HUD Edge Timer & Menu Time Displays**:
  - `0x479f00`: `drawHudDigit(glyph, x, y, scale, rgb, alpha1, alpha2)` (used by `WorldScreen::renderHUDTimer` `0x47ac20` and `drawEdgeTimeDemo` `0x47a2d0`); glyph rect count table at `0x586400` (`[0xa]` separator at `0x58640A`), glyph rect table at `0x5f3ac0` (`+0xa*20` = `0x5F3B88`).
  - `MatrixStack` (`0x41f4d0` `loadMatrix`, `0x41f520` `multiply/set2`, `0x41f610` `Pop`, `0x41f750` `Push`, `0x41f660` `SetTransform`).
  - `DebriefMenu::render`: level-time `sprintf` format pushes at `0x4aa139` (`0x58cc58`) / `0x4aa96f` (`0x58cc88`), edge-time `sprintf` branch at `0x4aa5e6` (`0x58cc70` / `0x58cc80`).
  - `ScoresMenu::render` (`0x4af660`, vtable `0x58d118`): `[+0x6c]` `scrollY`, `[+0x74]` `flipTick` (`0x4afdb0..0x4afe40` card intro/flip wave wrapped by `scoresCardFlip`); card time separators `0x4b01f7` (`u":"`, height scale `0x4b022c` `0x1000`) & `0x4b031e` (`0x58d0d8` `u":"`, height scale `0x4b034b` `0x1000`).
  - `LevelSelectMenu::renderInfoCard` card time separators: first separator `0x4b7404` (`u":"`), height scale `0x4b743f` (`0x1000`); second separator `0x4b757c` (`0x58d4f8` `u":"`), height scale `0x4b75aa` (`0x1000`).
- **`CreditsMenu` (`0x58b9d0`)**:
  - Constructor `0x4a20f0(arg0, arg1)` (`0x54` bytes; callers `0x49e973`, `0x49ecef`, `0x49ed5f`, `0x49edcf`); vtable`[1]` `0x4a1940` (`update`), vtable`[2]` `0x4a1a70` (`render`, slot `0x58b9d8` wrapped by `creditsRenderWrap`).
  - `[+0x4]` `introStep` (`< 5` gates scrolling), `[+0x18]` mode flag, `[+0x1c]` / `[+0x20]` config strings (`edge.menu.credits.scroll_speed_normal` / `scroll_speed_fast`), `[+0x48]` `started`, `[+0x49]` `finished`, `[+0x4c]` `float scrollPos`, `[+0x50]` `float scrollSpeed` (negative speed clamped `>= 9.0f` at `0x58b9c8`).
- **Save JSON (`Json::Value`)**:
  - `0x51c660(key)` `isMember`, `0x51c540(key)` lookup (`+1` const / `+0` mut), `0x51b620(type=3)` `isConvertibleTo(real)`, `0x51b3f0()` `asDouble -> st(0)`, `0x51b290(double)` `Value(double)` (`ret 8`), `0x51db80` `operator[]`, `0x51d1b0` `operator=`, `0x51c800` `~Value()`.
- **Camera & `Game` (`0x5f9064`, `0x47b8d0`, `0x4357d0`)**:
  - `0x5f9064`: `Game*` singleton (`BYTE PTR [eax+0x2120]` = `1` while a level is loaded, `0` after unload; `BYTE PTR [eax+0xb5]` = `1` only during the win/outro ending sequence, `0` during normal play; `GameEngine+0x50` = state `>= 0x11` in-level/pause/win vs `< 0x11` in Main Menu).
  - `0x47b8d0` (`GameCamera::Update`): computes `fov` (`[ebp-0x8]` unclamped `cur_fov`, `[ebp-0x4]` final `fovy`, `[ebp-0x10]` camera `aspect`), vertigo zoom (`[ebp-0xc]` flag struct -> `0x481c50`), and axis distance (`[ebp-0x8]` zoom * vertigo -> `[ebp-0x14]`), then calls `Camera::SetFov` (`0x423780`) and `Camera::SetNearFar` (`0x423840`). `ScreenMgr` holds camera slots at `sm + 0x30c` (`0`), `sm + 0x318` (`1`), and `sm + 0x324` (`2 = renderCam`, indexed via `sm + 0x330`); `0x481bb0` (`ScreenMgr::setFov`) starts level-triggered FOV transitions (`sm + 0x78`). `Camera` fields: `+0x1c0` `fovy_rad`, `+0x1c4` `near`, `+0x1c8` `far`, `+0x1dc` `aspect`, `+0x2b0` perspective flag (`1`), `+0x304` `M_proj` (`D3DMATRIX*`, applied via `Camera::SetProj` `0x435780`), `+0x30c` `Frustum` (`16` B: `tan`, `far`, `near`, padding; tested by `Frustum::testSphere` `0x4356a0`).
  - `0x4357d0`: `D3DXMatrixPerspectiveFovRH` wrapper (`0x4bb2d0`); post-call `eax = pOut` (`D3DMATRIX*`), `[eax+0x14] = m[1][1]`.
- **`Level` (`0x5f9040`, loader `0x475540`)**:
  - `[+0x6c]` theme index (`0..3`), `[+0x70]` `uint8_t heightTiles` (`3..34`), `[+0x72]` `uint16_t sizeX`, `[+0x74]` `uint16_t sizeZ`.
  - Entity arrays: `[+0x08]` moving platforms, `[+0x10]` bumpers, `[+0x18]` falling platforms, `[+0x34]` prisms, `[+0x3c]` holoswitches, `[+0x44]` switches, `[+0x54]` checkpoints.
- **`Texture` & Bound Cache**:
  - `Texture::apply` `0x40cf90` (`0x40d02a` `GetD3DDevice` + `[esi+0x2c]` load); `[+0x04]` `hash0`, `[+0x08]` `hash1` (`0x039573F7` = `"textures"`), `[+0x2c]` `IDirect3DTexture9*`.
  - Renderer bound-texture cache: `*(uint8_t*)0x5f87c9` (stage count), `*(void***)0x5f87cc` (array).
- **`Model` / `SubMesh` / `Material`**:
  - `Model::render` `0x421460` (static level model at `[WorldScreen+0x213c]`, rendered at `0x47b220`); scene-node traversal `0x495a70` (`vtable[1]` -> mesh `[+0x1d0..+0x1d4]` `SubMesh` array stride `0xa8`, child `+0xec`, sibling `+0xf4`).
  - `SubMesh`: `[+0x04]` `Material*`, `[+0x10/+0x14]` pos float vector (`numVerts = bytes/12`), `[+0x80]` `vertexFlags` (`1`=normal, `2`=color, `4`=uv), `[+0x84]` vertex stride, `[+0xa0]` `IDirect3DVertexBuffer9*` (`D3DPOOL_MANAGED`).
  - `Material::apply` `0x437220` (`[+0x0c/+0x10]` `Texture` pointer pairs stride `8`, `[+0x1c]` stage ops, `0x4372d9` `D3DTSS_COLOROP` load).
- **World Lighting, Bumpers & Height UVs**:
  - `Light::apply` `0x4e9810` (called at `0x47b245`; `[+0x00]` RGBA, `[+0x08]` `float dir[3]`, `[+0xa4]` D3D light index).
  - `Bumper::render` `0x484fa0` (called at `0x47b269`; `[+0x10]` `int y`, `[+0x54..+0x74]` side & roof models).
  - `MovingPlatform::render` `0x483fd0` (called at `0x47b2aa`; `[+0x50/+0x58]` inactive/active body models, `[+0x60]` edge model, `[+0x68]` `animTick`, `[+0x6c]` active state).
  - `FallingPlatform::render` `0x486b20` (called at `0x47b2da`; `[+0x0c]` `int y`), `HoloSwitch::render` `0x486150` (called at `0x47b330`), `Shrinker::render` `0x486f50` (called at `0x47b359`).
  - `Renderer::setVertexFormat` `0x4064a0` (`0x40654f` -> `0x405c20` `enableLighting`/`useVertexColor` for `D3DRS_DIFFUSEMATERIALSOURCE`, `0x406565` lit stage-0 `D3DTSS_COLOROP`).
  - `SetHeightUV` `0x495bb0(Model*, int y)` (used by `MovingPlatform::render` `0x483fd0` & `Bumper::render`); `Cube::computeMaterialColor` `0x490e30` (diffuse call at `0x490bd5`, ambient call at `0x490c82`); `D3DRS_AMBIENT` = `0x80808080` (`0x406bad` / `0x406bb2`).
- **`Finish`, `Cube` & Win Sequence (`mod_finish_effects.cpp`)**:
  - `Finish` (`[Level+0x0]`): `[+0x0/+0x4/+0x8]` `int32_t gridX/gridY/gridZ`; `Finish::render` `0x4836e0` (called at `0x47b283`).
  - `Cube` (`[Game+0x34]` player): `[+0x0]` cube index (`0` = player), `[+0x4c/+0x50/+0x54]` world pos, `[+0x134]` fixed-point scale inverse (`0x1000` normal / `0x3000` mini), `[+0x16c]` pending `CubeScreenEffect` trigger (`0x403010`), `[+0x1a0]` `hide_delay_after_win`; `Cube::render` `0x48c630` (called at `0x47b37b` player via `hk_playerCubeRender` -> `RenderPlayerCubeWithColorTheme`, `0x47b3f8` solid dark cube, `0x47b3ff` + `0x47b406` ghost dark cube transparents), `Cube::onWin` `0x48e060` (calls `Game::onWin` `0x47c200` at `0x48e096`), `Cube::ComputePalette` `0x48b980` (`0x589b50` `rainbow_cube`, `0x589b84` `rainbow_cube_dark`, live player cube RGB at `0x5f914c` `g_cubePalette[15]`).
  - Win & scene render hooks: `WorldScreen::update` `0x47f540` (`[+0x4]` 30 Hz frame counter), `Game::onWin` `0x47c200` (`[+0xb5]` `isWinner`, `[+0xb8]` `winAnimTick`, `[+0xbc]` `zoomoutDelay`, `"level_finished"` particle spawn `0x4eb750` `ret 0x10` at `0x47c2f1`), `Game::unloadLevel` `0x47e3fc`, `SPARK::render` `0x4d0fa0` (called at `0x47b418`, before `PostProcessor::apply` `0x4ea210` at `0x47b434`).
- **Audio & Window Focus (`mod_core_fixes.cpp`)**:
  - `AudioPlayer` (`0x5f903c`): `playSfx` `0x4740d0`, `playMusic` `0x474160`, `setVolume(channel, vol)` `0x474480` (`0` = XACT `"Default"` SFX category at `0x585474`, `1` = `OggStream` music gain scaled at `0x4744a3`).
  - Window & WndProc subclassing: `0x5f8bc8` (`HWND`), `0x5f8bcc` (`DXUTStaticWndProc` `0x41a420` saved before subclassing), `0x44d4e0` (`KeyboardWndProc`), `0x44d72e` (`push 0x44d4e0` before `SetWindowLongW(GWL_WNDPROC)`, patched at `0x44d72f` -> `hk_GameWndProc`; teardown at `0x44d2f0`), `0x414820` (`DXUTGetHWND`).
- **Music Lookup, Widened Menus & Progression Unlock (`mod_core_fixes.cpp`, `mod_music_playlist.cpp`, `mod_menu_camera_options.cpp`)**:
  - `MusicManager::getMusicFilename` `0x473e60`: vanilla maps `0..24` (`26`/`0x1a` = uninitialized sentinel); `mod_music_playlist.cpp` resolves `0` (`title`), `13` (`debrief`), virtual active-level sentinel `0x1000` (injected at `Level::load` `0x476205` when `playMusic != 0`, capturing `[ebp+0x8]` `LevelInfo`'s leading `EdgeString` filename), and extended IDs `27..255` (`music/<id>_*.ogg` or `music/<id>.ogg`) using `music/playlist.xml` and `g_musicPlaylist`.
  - Shared menu width/border/strip: `0x589ddc` (`Menu::baseWidth`, `320 -> 368 = 0x170`), `Menu::getBasePt` `0x496030`, `Menu::drawStrip` `0x496070`, `Menu::drawRainbowBorder` `0x4960c0`.
  - **Level/Mode Unlock & Pause/LevelSelect/Scores Prisms**: `ProgressMgr::isModeAndBonusSlotUnlocked` `0x4944fa`, `LevelSelect::refreshTiles` `0x4b5fcd`, `LevelSelect::renderCard` `0x4b6be0`/`0x4b6cb0`, `LevelSelect` grid & prism strip (`0x4b8090`, `0x4b869c` `> 20` hook; `LevelInfo+0x70` total, `LevelSaveInfo+0x8` collected), `ModeSelect::getRowStringId` `0x4b8b02`, `ScoresMenu::render` card back (`0x4b0991` `> 20` hook; `ScoresCardEntry+0x78` total, `+0x7c` collected), Pause `P0` render (`0x4ad291`..`0x4ad403`) & prism strip (`0x4ae367` `> 20` hook, `0x4ae39c`; `Level+0x7c` total, `WorldScreen+0x11c` collected).
