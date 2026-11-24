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
   - Follow [PATCHING_GUIDE.md](../../PATCHING_GUIDE.md) for ABI, x87 FPU, `std::wstring`, INI, save-key, and layout pitfalls.
2. **Use cached inspection tools (save time & tokens)**:
   - Prefer `py tools/safe_disasm.py --func <va>` (queries `.cache/bak_disasm.sqlite` with no `wsl` spawn and auto-resolves enclosing `int3` function bounds).
   - For sub-ranges, `py tools/safe_disasm.py <start> <stop> [--max-lines N]` auto-snaps unaligned `start` VAs backward to the enclosing instruction.
   - To find all `.text` callers or struct-offset accesses in unmodified `edge.exe` without dumping functions, query `.cache/bak_disasm.sqlite` (`SELECT va, asm FROM insns WHERE ... LIMIT 20`) instead of invoking `objdump`.
   - Locate exact helpers in `dll/*.cpp` via `Select-String` / `rg -n` and read `<= 50`-line slices only as needed.
3. **Implement patches modularly with guards**:
   - Edit or add C++ hook modules under `dll/*.cpp` and wire hook registration into [build_dll.py](../../../build_dll.py) / [xinput_proxy.cpp](../../../dll/xinput_proxy.cpp) (`RegRelCall`/`RegRelJmp` with expected original byte verification).
4. **Stage and verify before applying**:
   - Never modify `edge.exe` on disk and never overwrite locked binaries while `edge.exe` is running.
   - Build and verify offline via `py build_dll.py --stage` (`< 5`s via MSVC `cl.exe` x86), which checks exports and runs `verify_dll.exe` against unmodified `edge.exe`.
   - When deploying via `py build_dll.py --apply` / `--restore`, if `edge.exe` is running, automatically close it cleanly first (falling back to `Stop-Process` if needed), deploy and verify hashes (`--verify`), and automatically reopen the game via `steam://rungameid/38740` afterward.
5. **Update the markdown if needed**:
   - Whenever adding or modifying features, options, DLL modules, save/INI keys, or newly verified engine addresses/structs, update [README.md](../../../README.md) for user-facing changes and [MEMORY_MAP.md](../../../docs/MEMORY_MAP.md), [RESEARCH_NOTES.md](../../../docs/RESEARCH_NOTES.md), and [OPTIONS_UI.md](../../../docs/OPTIONS_UI.md) for technical/internal changes so documentation stays accurate.
