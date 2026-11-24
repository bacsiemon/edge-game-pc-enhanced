#!/usr/bin/env python3
"""
Cached disassembly viewer for EDGE .text functions.
Automatically collapses int3 padding runs and resolves function bounds.
"""

import argparse
import os
import re
import shutil
import sqlite3
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools.common import (
    DEFAULT_GAME_DIR,
    EXPECTED_EXE_SIZE,
    find_clean_edge_exe,
    is_clean_edge_exe,
    to_host_path,
    to_wsl_path,
)

TEXT_VA_START = 0x401000
TEXT_VA_END = 0x576600
BAK_INDEX_STOP = 0x5765A0
FUNC_SEARCH_BACK = 0x800
FUNC_SEARCH_FWD = 0x2000
_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_BAK_CACHE_PATH = os.path.join(_REPO_ROOT, ".cache", "bak_disasm.sqlite")

_INSN_LINE_RE = re.compile(r"^\s*([0-9a-fA-F]+):\s*(.*)$")


def filter_disasm_lines(raw_lines: list[str], max_lines: int) -> list[str]:
    out: list[str] = []
    int3_run = 0
    for ln in raw_lines:
        if not ln.strip():
            continue
        m = _INSN_LINE_RE.match(ln)
        if not m:
            int3_run = 0
            out.append(ln)
            continue
        asm_part = m.group(2).strip()
        if asm_part.lower() == "int3":
            int3_run += 1
            if int3_run <= 2:
                out.append(ln)
            elif int3_run == 3:
                out.append("  ... (int3 padding)")
            continue
        int3_run = 0
        out.append(ln)

    if max_lines > 0 and len(out) > max_lines:
        out = out[:max_lines] + [f"... truncated ({len(out) - max_lines} more lines)"]
    return out


def parse_hex(val: str) -> int:
    return int(val, 16) if val.lower().startswith("0x") else int(val, 10)


def _run_objdump(exe_path: str, start: int, stop: int) -> str:
    use_wsl = (os.name == "nt") and not shutil.which("objdump")
    target = to_wsl_path(exe_path) if use_wsl else exe_path
    cmd = [
        "objdump",
        "-d",
        "--no-show-raw-insn",
        "-M",
        "intel",
        f"--start-address={hex(start)}",
        f"--stop-address={hex(stop)}",
        target,
    ]
    if use_wsl:
        cmd = ["wsl", *cmd]
    return subprocess.check_output(cmd, text=True, errors="replace")


def _ensure_bak_index(bak_path: str) -> sqlite3.Connection:
    os.makedirs(os.path.dirname(_BAK_CACHE_PATH), exist_ok=True)
    conn = sqlite3.connect(_BAK_CACHE_PATH)
    conn.execute("CREATE TABLE IF NOT EXISTS insns (va INTEGER PRIMARY KEY, asm TEXT NOT NULL)")
    conn.execute("CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v TEXT NOT NULL)")
    row = conn.execute("SELECT v FROM meta WHERE k = 'size'").fetchone()
    if row is not None and row[0] == str(EXPECTED_EXE_SIZE):
        has_rows = conn.execute("SELECT 1 FROM insns LIMIT 1").fetchone()
        if has_rows is not None:
            return conn

    raw = _run_objdump(bak_path, TEXT_VA_START, BAK_INDEX_STOP)
    rows: list[tuple[int, str]] = []
    for ln in raw.splitlines():
        m = _INSN_LINE_RE.match(ln)
        if m:
            rows.append((int(m.group(1), 16), m.group(2).strip()))
    with conn:
        conn.execute("DELETE FROM insns")
        conn.executemany("INSERT INTO insns (va, asm) VALUES (?, ?)", rows)
        conn.execute("INSERT OR REPLACE INTO meta (k, v) VALUES ('size', ?)", (str(EXPECTED_EXE_SIZE),))
    return conn


def _resolve_func_bounds(conn: sqlite3.Connection, addr: int) -> tuple[int, int]:
    lo = max(TEXT_VA_START, addr - FUNC_SEARCH_BACK)
    prev_rows = conn.execute(
        "SELECT va, asm FROM insns WHERE va >= ? AND va <= ? ORDER BY va DESC",
        (lo, addr),
    ).fetchall()
    func_start = max(TEXT_VA_START, (addr - 0x100) & ~0xF)
    last_real_va: int | None = None
    for va, asm in prev_rows:
        if asm.strip().lower() == "int3":
            if last_real_va is not None:
                func_start = last_real_va
                break
        else:
            last_real_va = va
    if prev_rows and prev_rows[-1][1].strip().lower() != "int3" and func_start == prev_rows[-1][0]:
        # No int3 hit within lo..addr unless prev_rows stopped inside padding; keep default if uncertain
        pass

    hi = min(BAK_INDEX_STOP, func_start + FUNC_SEARCH_FWD)
    next_rows = conn.execute(
        "SELECT va, asm FROM insns WHERE va > ? AND va <= ? ORDER BY va ASC",
        (addr, hi),
    ).fetchall()
    func_stop = hi
    for va, asm in next_rows:
        if asm.strip().lower() == "int3":
            func_stop = va
            break
    if func_stop <= func_start:
        func_stop = min(BAK_INDEX_STOP, func_start + FUNC_SEARCH_FWD)
    return func_start, func_stop


def _align_start_live(raw_lines: list[str], req_start: int) -> tuple[list[str], int | None]:
    parsed_insns: list[tuple[int, str]] = []
    headers: list[str] = []
    for ln in raw_lines:
        m = _INSN_LINE_RE.match(ln)
        if m:
            parsed_insns.append((int(m.group(1), 16), ln))
        elif ln.strip() and not ln.strip().startswith("."):
            headers.append(ln)
    if not parsed_insns:
        return raw_lines, None

    actual_start: int | None = None
    for va, _ln in parsed_insns:
        if va <= req_start:
            actual_start = va
        else:
            break
    if actual_start is None:
        actual_start = parsed_insns[0][0]
    filtered = [*headers, *(ln for va, ln in parsed_insns if va >= actual_start)]
    return filtered, actual_start


def main() -> int:
    game_dir = to_host_path(DEFAULT_GAME_DIR)
    clean_default = find_clean_edge_exe(game_dir)
    default_exe = str(clean_default) if clean_default else os.path.join(game_dir, "edge.exe")
    p = argparse.ArgumentParser(description="Cached disassembly viewer for EDGE .text functions.")
    p.add_argument("start", nargs="?", type=parse_hex, help="Start virtual address in .text (e.g. 0x407eb0)")
    p.add_argument("stop", nargs="?", type=parse_hex, help="Stop virtual address in .text (e.g. 0x407f00)")
    p.add_argument("--func", type=parse_hex, help="Resolve enclosing function bounds in unmodified edge.exe around VA")
    p.add_argument("--exe", default=default_exe, help="Path to unmodified edge.exe (or staged executable)")
    p.add_argument("--max-lines", type=int, default=0, help="Maximum instruction lines to print (0 = unlimited)")
    args = p.parse_args()

    max_lines = max(args.max_lines, 0)
    bak_conn: sqlite3.Connection | None = None
    is_clean = is_clean_edge_exe(args.exe)

    if args.func is not None:
        clean_path = args.exe if is_clean else default_exe
        if not is_clean_edge_exe(clean_path):
            print(f"Error: unmodified edge.exe needed for --func index: {clean_path}", file=sys.stderr)
            return 1
        bak_conn = _ensure_bak_index(clean_path)
        start_va, stop_va = _resolve_func_bounds(bak_conn, args.func)
    else:
        if args.start is None or args.stop is None:
            p.error("Provide both start and stop VAs, or pass --func <va>.")
        start_va, stop_va = args.start, args.stop

    if start_va < TEXT_VA_START or stop_va > TEXT_VA_END or stop_va <= start_va:
        print(f"Error: range must stay inside .text ({hex(TEXT_VA_START)}..{hex(TEXT_VA_END)})", file=sys.stderr)
        return 1

    align_note: str | None = None
    if is_clean and stop_va <= BAK_INDEX_STOP:
        if bak_conn is None:
            bak_conn = _ensure_bak_index(args.exe)
        actual_start = start_va
        exact = bak_conn.execute("SELECT 1 FROM insns WHERE va = ?", (start_va,)).fetchone()
        if exact is None:
            snapped = bak_conn.execute(
                "SELECT MAX(va) FROM insns WHERE va <= ? AND va >= ?",
                (start_va, max(TEXT_VA_START, start_va - 15)),
            ).fetchone()
            if snapped is not None and snapped[0] is not None:
                actual_start = snapped[0]
                if actual_start != start_va:
                    align_note = f"  [aligned start {hex(start_va)} -> {hex(actual_start)}]"
        rows = bak_conn.execute(
            "SELECT va, asm FROM insns WHERE va >= ? AND va < ? ORDER BY va",
            (actual_start, stop_va),
        ).fetchall()
        raw_lines = [f"  {va:x}:\t{asm}" for va, asm in rows]
        lines = filter_disasm_lines(raw_lines, max_lines)
        hdr = f"{args.exe} (cached .text index): 0x{actual_start:x}..0x{stop_va:x}"
        out_lines = [hdr, *([align_note] if align_note else []), *lines]
    else:
        fetch_start = max(TEXT_VA_START, (start_va - 15) & ~0xF)
        raw = _run_objdump(args.exe, fetch_start, stop_va)
        aligned_raw, actual_start = _align_start_live(raw.splitlines(), start_va)
        if actual_start is not None and actual_start != start_va:
            align_note = f"  [aligned start {hex(start_va)} -> {hex(actual_start)}]"
        lines = filter_disasm_lines(aligned_raw, max_lines)
        out_lines = ([align_note, *lines] if align_note else lines)

    print("\n".join(out_lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
