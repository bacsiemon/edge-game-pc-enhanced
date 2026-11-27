---
trigger: always_on
---

# Repository Rules for `edge-game-pc-enhanced`

These rules apply whenever working in this repository to keep workflows fast, token-efficient, and reliable:

1. **Reuse maps & caches first (save time & tokens)**:
   - Read [PATCHING_GUIDE.md](../PATCHING_GUIDE.md), [MEMORY_MAP.md](../../docs/MEMORY_MAP.md), and [RESEARCH_NOTES.md](../../docs/RESEARCH_NOTES.md) before opening binaries or re-discovering known addresses/ABIs.
   - New features and fixes live in the native 32-bit C++ proxy DLL (`dll/*.cpp` + `dll/patch_utils.h` + `dll/xinput1_3.def`), built and verified via **`py build_dll.py --stage`** (`< 5`s via MSVC `cl.exe` x86). All globals, strings, and helpers live inside `XINPUT1_3.dll` (no code-cave or `.rdata`/`.bss` budgets needed).
   - Use **`py tools/safe_disasm.py --func <va> [--max-lines N]`** as the primary way to inspect functions around any VA or xref: it queries the pre-indexed SQLite DB `.cache/bak_disasm.sqlite` directly without spawning `wsl`/`objdump` and resolves `int3` function bounds automatically.
   - To find all `.text` instructions calling/referencing a VA or struct offset across unmodified `edge.exe`, run a bounded SQLite query against `.cache/bak_disasm.sqlite` (`SELECT va, asm FROM insns WHERE ... LIMIT 20`) instead of writing binary scanners or running repeated disassembly calls.
   - Locate symbols inside `dll/*.cpp` with `Select-String` / `rg -n` and read targeted `<= 50`-line slices rather than loading whole modules. Edit files with structured file-edit tools (`edit_file`/`write_file`), never bash heredocs.
2. **Keep `edge.exe` unmodified & auto-restart running game when deploying**: Never modify `edge.exe`. When running `py build_dll.py --apply` or `--restore`, if `edge.exe` is running, automatically close it cleanly (falling back to `Stop-Process` if needed), deploy and verify (`--verify`), and launch/reopen the game via `steam://rungameid/38740`; if the game runs fine after patching, keep it open. Use `--stage` when testing offline builds without restarting the game.
3. **Releases**: `releases/` mirrors the game folder (`XINPUT1_3.dll`, `mods/*.edgemod`) for copy-paste install; `build_dll.py` refreshes the DLL there. Copy any newly built `.edgemod` into `releases/mods/`.
4. **Update the markdown if needed**: Whenever adding or changing features, options, DLL modules, save/INI keys, or newly verified engine addresses/structs, update user-facing docs in `README.md` (keeping it strictly end-user-friendly) and technical docs in `docs/MEMORY_MAP.md`, `docs/RESEARCH_NOTES.md`, and `docs/OPTIONS_UI.md` to stay in sync.
5. **Git hygiene**: Never commit compiled build outputs, game binaries, local SQLite caches, crash dumps, or release packages (`edge.exe`, `edge_patched_staged.exe`, `.cache/`, `dll/build/`, `releases/`).

## Target Binary & Repository Map

- **Target binary**: Steam PC `edge.exe` (`2,714,624` bytes, SHA-256 `b73a0df8e07d47f3d3e4b57642514033c00449c0070876513323c257c3cabe9b`).
- **Repository layout**:
  - `dll/`: 15 C++ proxy DLL source modules (`563` verified runtime hooks) compiled into `XINPUT1_3.dll` + `verify_dll.cpp` offline expected-bytes verifier.
  - `tools/`: Cached disassembly viewer ([safe_disasm.py](../../tools/safe_disasm.py)), preview showcase builder ([build_preview_level.py](../../tools/build_preview_level.py)), and shared backup hash/size + `EDGE_GAME_DIR` / WSL path helpers ([common.py](../../tools/common.py)).
  - `docs/`: Technical reverse-engineering reference ([MEMORY_MAP.md](../../docs/MEMORY_MAP.md), [RESEARCH_NOTES.md](../../docs/RESEARCH_NOTES.md), [OPTIONS_UI.md](../../docs/OPTIONS_UI.md)).
  - `.agents/`: Always-on rules ([rules/edge-game-pc-enhanced.md](edge-game-pc-enhanced.md)), AI patching playbook ([PATCHING_GUIDE.md](../PATCHING_GUIDE.md)), and skill ([edge-pc-patching/SKILL.md](../skills/edge-pc-patching/SKILL.md)).
  - `releases/` & `.cache/`: Local gitignored packaging and SQLite disassembly cache folders.
