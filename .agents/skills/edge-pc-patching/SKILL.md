---
name: edge-pc-patching
description: >-
  Follows the established workflow, memory map, and build/verification pipeline
  when debugging, extending, or creating PC binary patches for EDGE inside
  edge-pc-mod. Use when adding features, fixing bugs, or updating patch modules
  in this repository.
---

# EDGE PC Patching Workflow

Follow this checklist whenever creating or modifying patches inside this repository:

1. **Consult pre-verified maps first (skip redundant discovery)**:
   - Read [MEMORY_MAP.md](../../../docs/MEMORY_MAP.md) and [RESEARCH_NOTES.md](../../../docs/RESEARCH_NOTES.md) before scanning binaries or re-disassembling known functions.
   - Follow [PATCHING_GUIDE.md](../../../docs/PATCHING_GUIDE.md) for ABI, x87 FPU, `std::wstring`, INI, save-key, and layout pitfalls.
2. **Use cached inspection tools (save time & tokens)**:
   - Prefer `py tools/safe_disasm.py --func <va>` (queries `.cache/bak_disasm.sqlite` with no `wsl` spawn and auto-resolves enclosing `int3` function bounds).
   - For sub-ranges, `py tools/safe_disasm.py <start> <stop> [--max-lines N]` auto-snaps unaligned `start` VAs backward to the enclosing instruction.
   - To find all `.text` callers or struct-offset accesses in `edge.exe.bak` without dumping functions, query `.cache/bak_disasm.sqlite` (`SELECT va, asm FROM insns WHERE ... LIMIT 20`) instead of invoking `objdump`.
   - Locate exact helpers in `patches/*.py` via `Select-String` / `rg -n` and read `<= 50`-line slices only as needed.
   - Check remaining `free_tail` in existing blocks via `py tools/find_free_slots.py` before running `--scan-padding` / `--check-refs`.
3. **Implement patches modularly with guards**:
   - For active changes, edit or add C++ hook modules under `dll/*.cpp` and wire hook registration into [build_dll.py](../../../build_dll.py) / [xinput_proxy.cpp](../../../dll/xinput_proxy.cpp) (`RegRelCall`/`RegRelJmp` with expected original byte verification).
   - For legacy static binary reference under `patches/`, use `assemble_at`, `link_block_with_symbols`, and `expect_replace` from `patches/common.py`, and wire options into [patch_edge.py](../../../patch_edge.py).
4. **Stage (cached incremental rebuild) and verify before applying**:
   - Never modify `edge.exe.bak` and never overwrite locked binaries while `edge.exe` is running.
   - Test directly via `py patch_edge.py --stage` (or `py build_dll.py --stage` for the DLL build): `patches/common.py` caches compiled blocks in `.cache/asm_cache.sqlite` and pipelines cache misses in a single `wsl sh -c` call, so unchanged blocks skip compilation (`< 1`s warm).
   - Inspect new routines only if needed with `tools/safe_disasm.py --exe edge_patched_staged.exe`, and verify with `py patch_edge.py --verify`.
   - When deploying via `py build_dll.py --apply` / `--restore` (or `py patch_edge.py --apply`), if `edge.exe` is running, automatically close it cleanly first (falling back to `Stop-Process` if needed), deploy and verify hashes (`--verify`), and automatically reopen the game via `steam://rungameid/38740` afterward.
5. **Update the markdown if needed**:
   - Whenever adding or modifying features, options, DLL modules, save/INI keys, or newly verified engine addresses/structs, update [README.md](../../../README.md), [MEMORY_MAP.md](../../../docs/MEMORY_MAP.md), [RESEARCH_NOTES.md](../../../docs/RESEARCH_NOTES.md), and [OPTIONS_UI.md](../../../docs/OPTIONS_UI.md) if needed so documentation stays accurate.
