# EDGE Mod Release

Copy everything in this folder into the EDGE game folder (e.g. `C:\Program Files (x86)\Steam\steamapps\common\EDGE`), merging folders and overwriting when asked:

- `XINPUT1_3.dll` - the patch (UI scale, camera, audio, smoothing, options, HUD timer, finish effects, mobile color theme, level unlock, widened menus, auto-scrolling text, extended music IDs). Takes effect immediately and requires an unmodified `edge.exe`.
- `mods\JavaMusic.edgemod` - Java OST music mod (needs the DLL above). Apply or toggle it via EdgeTool after copying (11 tracks from the EDGE Java OST YouTube playlist + synthesized `07_03_EDGE` via `Bada_Mobile_0300_base.sf2`, mono, loudness-matched to vanilla).

To uninstall, disable `JavaMusic.edgemod` in EdgeTool (if applied), then delete `XINPUT1_3.dll` and `mods\JavaMusic.edgemod` from the game folder.

`XINPUT1_3.dll` is refreshed automatically by `py build_dll.py --stage/--apply`.
