# EDGE PC Binary Patching Guide (AI Playbook)

Fast workflow for adding or modifying features/fixes in this repository cleanly and reliably.

## Fast Workflow

Active features and fixes live in the native 32-bit C++ proxy DLL (`dll/*.cpp`, built and verified with `py build_dll.py --stage`, `--apply`, and `--verify`), which applies runtime hooks in memory and avoids code-cave, `.rdata`, and `.bss` slot limits.

1. **Read known maps first**:
   - Check `docs/MEMORY_MAP.md` for PE section mappings, `.rdata`/`.data`/`.bss` addresses, and verified engine structs/signatures.
   - Check `docs/RESEARCH_NOTES.md` before re-investigating D3D9 states, Steam community checks, UI scaling, or 30 Hz UI counters.
2. **Inspect targeted functions**:
   - Use `safe_disasm.py` (`edge.exe.bak` queries hit `.cache/bak_disasm.sqlite` after a one-time index build, and unaligned start addresses automatically snap to the enclosing instruction):
     ```powershell
     py tools\safe_disasm.py 0x407eb0 0x407f00
     py tools\safe_disasm.py --func 0x47af27
     ```
   - On WSL, use `python3 tools/safe_disasm.py ...`.
3. **Check live game state when debugging runtime issues**:
   - Config: `%LocalAppData%\Two Tribes\EDGE\settings.ini`
   - User save JSON: `%LocalAppData%\Two Tribes\EDGE\local_savedata.ttsav`
   - Crash minidumps: `<EDGE_GAME_DIR>\edge_*_crash_*.mdmp` (parse `ExceptionStream` = 6 and `MemoryListStream` = 5 in Python `struct` to get exact `EIP`/`ESP`/`EBP` and stack words).
4. **Write clean modular C++ hooks**:
   - Add or update hook modules under `dll/*.cpp`, register patches with expected original bytes (`RegRelCall` / `RegRelJmp` / `RegBytes`) in `xinput_proxy.cpp` or module init, and wire new `.cpp` files into `DLL_SOURCES` in `build_dll.py`.
   - Always assert expected original bytes at hook sites so conflicts fail loudly in `verify_dll.exe` instead of corrupting machine code.
5. **Stage, verify, and deploy**:
   - Build and verify offline first (never edit `edge.exe.bak`; never overwrite a running `edge.exe`):
     ```powershell
     py build_dll.py --stage
     ```
   - If `edge.exe` is running when deploying, close it cleanly first (falling back to `Stop-Process` if needed), run `--apply` and `--verify`, and launch/reopen EDGE via `steam://rungameid/38740`; verify it runs cleanly without crashing:
     ```powershell
     py build_dll.py --apply
     py build_dll.py --verify
     ```
6. **Update the markdown if needed**:
   - Whenever adding or modifying features, options, DLL modules, save/INI keys, or newly verified engine addresses/structs, update `README.md`, `docs/MEMORY_MAP.md`, `docs/RESEARCH_NOTES.md`, and `docs/OPTIONS_UI.md` so the documentation stays in sync.

## Fast & Token-Efficient Usage (Cache & Search Habits)

Use the persistent disassembly cache under `.cache/` (auto-managed) instead of repeating slow WSL scans or loading large files into context:

1. **Instant Disassembly Index (`.cache/bak_disasm.sqlite`)**:
   - Indexes clean `edge.exe.bak` `.text` (`0x401000..0x5765a0`) once from `objdump -d --no-show-raw-insn -M intel` into SQLite table `insns(va INTEGER PRIMARY KEY, asm TEXT)`.
   - **Whole-function lookup (`--func`)**: When holding any xref or call site, pass `--func <va>` instead of guessing start/stop bounds:
     ```powershell
     py tools\safe_disasm.py --func 0x4a216c --max-lines 80
     ```
     This resolves `int3`-delimited function bounds from `.cache/bak_disasm.sqlite` without spawning `wsl`.
   - **Minimal line budgets**: Pass `--max-lines 40`..`80` when inspecting a single hook site; unaligned `<start>` VAs snap backward automatically.
   - **Targeted xref/offset SQLite search**: To find every call site or struct-offset access in `edge.exe.bak` without disassembling functions, query `insns` with a tight `LIMIT`:
     ```powershell
     py -c 'import sqlite3; c=sqlite3.connect(".cache/bak_disasm.sqlite"); print([(hex(v),a) for v,a in c.execute("SELECT va, asm FROM insns WHERE asm LIKE ? LIMIT 20", ("%4a20f0%",))])'
     ```
   - Only delete `.cache/bak_disasm.sqlite` if you replace `edge.exe.bak`.
2. **Minimal File Reads**:
   - Check `docs/MEMORY_MAP.md` & `docs/RESEARCH_NOTES.md` first before inspecting binaries or DLL hook internals.
   - Search `dll/*.cpp` with `Select-String` / `rg -n` and read `<= 50`-line slices instead of full files; edit via file-edit tools (`edit_file`/`write_file`), never bash heredocs.

## Engine-Specific x86 Pitfalls (Avoid Re-Learning These)

1. **`ret N` (`__thiscall` / `__stdcall`) tail-jump trap**:
   - Never `jmp` into a callee that cleans stack arguments (`ret 4`, `ret 8`, `ret 0xc`) if your helper pushed new stack arguments on top of its own return address. Always use `call fn; ret` in helpers (like `queryIniKey -> call 0x4f5d70; ret`). Tail `jmp` is safe only when the caller already pushed the arguments before calling your wrapper (e.g., `0x408060 -> jmp 0x51c660`).
2. **x87 FPU stack discipline & `fdiv` clarity**:
   - Always prefer explicit memory operands (`fdiv DWORD PTR [esp]`) over x87 register-to-register `fdivp`/`fdivrp` forms in GNU `as` `.intel_syntax noprefix`.
   - Always `fstp`/`fistp` return values from `0x4f4fd0` (`parseFloat`) or `0x51b3f0` (`asDouble`), even when an error flag is set, so x87 never overflows across repeated lookups.
3. **Hook-site callee-saved registers & flags**:
   - `OptionsMenu::render` hook `0x4b35d3`: must preserve `ebx` (`0xf`), `edi` (`0`), `ebp`, and exit with `eax = [sel]`, `cmp eax, 7` (`ZF` intact for row 7).
   - `computeVirtHeight` (`0x405b60` -> `0x407eb0`): callers rely on `ecx, ebx, esi, edi, ebp` preserved (`eax` out, `edx` scratch).
   - `Settings::loadIni` hook `0x476fea`: must preserve `ebx, esi, edi, ebp` and leave `ErrorStatus` (`[ebp-0x100]`) reset via `0x4458c0`.
4. **MSVC `std::wstring` SSO layout (`24` bytes)**:
   - `[+0..+15]`: inline `8 x wchar_t` buffer (when `capacity == 7`) or heap `wchar_t*` pointer (when `capacity >= 8`); `[+16]` length; `[+20]` capacity.
   - Localized strings returned by `0x49fd70` (`getLocalizedString`) must check `DWORD PTR [wstr+20] >= 8` and free `[wstr]` via `0x4ba62a`.
5. **Save key & INI migration**:
   - When changing a saved value's semantic type or range in `local_savedata.ttsav`, introduce a new key name (e.g. `uiScaleF` instead of integer `uiScale`) so existing saves cannot collide with new parsers.
   - `IniFile::getValue` (`0x4f5d70`) returns an empty `std::string` (`[str+0x10] == 0`) without raising an error when an optional key is missing; check length `!= 0` before parsing.
6. **`OptionsMenu` bounds & PE `VirtualSize`**:
   - Keeping minimum `virtH >= 512` (`0x200`) with 11 rows (or `450` / `0x1c2` with 9 rows) prevents `credits` from touching `back` (positioned at `virtH - 57`).
   - `.text` PE `VirtualSize` stops before the `0x5765b0..0x576600` zero trailer inside `SizeOfRawData`; inspect those 80 trailer bytes via Python rather than `objdump`.
