#!/usr/bin/env python3
"""
Inspect remaining free bytes in currently allocated patch regions, scan .text
for unused 0xcc padding runs, or verify whether a candidate VA range has
external references in .text / .rdata / .data.
"""

import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from patches.common import DEFAULT_GAME_DIR, to_host_path, va2off

KNOWN_TEXT_SLOTS = [
    ("d3d9_trailer", 0x5765b0, 0x576600, 0x00),
    ("virt_height",  0x407eb0, 0x407f00, 0xcc),
    ("scissor_hlp",  0x407f20, 0x407fa0, 0xcc),
    ("viewport_hlp", 0x407fa0, 0x408010, 0xcc),
    ("ui_draw_size", 0x408010, 0x408060, 0xcc),
    ("save_load",    0x408060, 0x408077, 0xcc),
    ("pause_disp",   0x408077, 0x4080c0, 0xcc),
    ("save_write",   0x4080c0, 0x408120, 0xcc),
    ("fmt_wstr",     0x408120, 0x4081b8, 0xcc),
    ("draw_row5",    0x4081b8, 0x408208, 0xcc),
    ("pause_div",    0x408208, 0x408238, 0xcc),
    ("virt_width",   0x408238, 0x408260, 0xcc),
    ("cam_viewport", 0x408260, 0x4082b0, 0xcc),
    ("mouse_scale",  0x4082b0, 0x408320, 0xcc),
    ("apply_scale",  0x408320, 0x408380, 0xcc),
    ("Block A",      0x41f220, 0x41f3ca, 0xcc),
    ("Block B",      0x4061c8, 0x406404, 0xcc),
    ("Block C",      0x45eae0, 0x45edc2, 0xcc),
    ("Block D",      0x484c10, 0x484e5f, 0xcc),
    ("Block E",      0x539770, 0x539a19, 0xcc),
    ("Block F",      0x479010, 0x4791cb, 0xcc),
    ("Block G",      0x52c560, 0x52caa0, 0xcc),
    ("Block H",      0x4248e0, 0x4249b2, 0xcc),
    ("Block H2",     0x4234d0, 0x4235e0, 0xcc),
    ("pause_render", 0x4ade56, 0x4adf53, 0xcc),
    ("audio_main0",  0x4b2fdf, 0x4b307c, 0xcc),
    ("audio_main1",  0x4b3168, 0x4b31fc, 0xcc),
    ("audio_pause0", 0x4ad756, 0x4ad80d, 0xcc),
    ("audio_pause1", 0x4ad8e7, 0x4ad99e, 0xcc),
    ("audio_tog",    0x494610, 0x4946c0, 0xcc),
    ("Block I",      0x453fa0, 0x4542cd, 0xcc),
    ("Block J",      0x4fca10, 0x4fcc34, 0xcc),
    ("Block K",      0x44df40, 0x44e154, 0xcc),
    ("Block L",      0x412f60, 0x4130de, 0xcc),
    ("Block M",      0x43b060, 0x43b1b8, 0xcc),
    ("Block N",      0x45cc80, 0x45cd9d, 0xcc),
    ("Block O",      0x541b10, 0x541c2c, 0xcc),
    ("Block P",      0x5347d0, 0x5348ec, 0xcc),
    ("Block Q",      0x4f79c0, 0x4f7ad6, 0xcc),
    ("Block R",      0x51bdc0, 0x51bed5, 0xcc),
    ("Block S",      0x453520, 0x4537a0, 0xcc),
    ("Block T",      0x45dc20, 0x45df90, 0xcc),
    ("Block U",      0x510d30, 0x510fe0, 0xcc),
    ("Block V",      0x44bad0, 0x44bc00, 0xcc),
    ("time_fmt_data",0x44bc00, 0x44bd20, 0xcc),
    ("Block W",      0x511030, 0x511330, 0xcc),
    ("time_fmt_code",0x511330, 0x5114b0, 0xcc),
    ("Block X",      0x4faa80, 0x4fabd0, 0xcc),
    ("skip_intro_data", 0x4fabd0, 0x4face0, 0xcc),
    ("Block Y",      0x5423f0, 0x542610, 0xcc),
    ("skip_intro_code", 0x542610, 0x542750, 0xcc),
    ("Block Z",      0x4fa5d0, 0x4fa8f0, 0xcc),
]

KNOWN_RDATA_SLOTS = [
    ("rdata_free_mid",  0x5e2904, 0x5e2960, 0x00),
    ("rdata_free_tail", 0x5e29c0, 0x5e2a00, 0x00),
]


def parse_hex(val: str) -> int:
    return int(val, 16) if val.lower().startswith("0x") else int(val, 10)


def measure_tail_free(data: bytes, start_va: int, end_va: int, pad_byte: int) -> tuple[int, int]:
    off0, off1 = va2off(start_va), va2off(end_va - 1) + 1
    chunk = data[off0:off1]
    free = 0
    for b in reversed(chunk):
        if b == pad_byte:
            free += 1
        else:
            break
    return len(chunk) - free, len(chunk)


def show_status(exe_path: str) -> None:
    with open(exe_path, "rb") as f:
        data = f.read()
    print(f"Inspecting: {exe_path}")
    print("\n--- Allocated .text Slots ---")
    for name, s, e, pad in KNOWN_TEXT_SLOTS:
        used, cap = measure_tail_free(data, s, e, pad)
        print(f"  {name:14s} {hex(s)}..{hex(e)}  used={used:4d}/{cap:<4d}  free_tail={cap - used:4d}B")
    print("\n--- Remaining .rdata Slack ---")
    for name, s, e, pad in KNOWN_RDATA_SLOTS:
        used, cap = measure_tail_free(data, s, e, pad)
        print(f"  {name:16s} {hex(s)}..{hex(e)}  used={used:4d}/{cap:<4d}  free={cap - used:4d}B")


def scan_padding(bak_path: str, min_bytes: int, limit: int) -> None:
    with open(bak_path, "rb") as f:
        data = f.read()
    text = data[0x400 : 0x400 + (0x576600 - 0x401000)]
    pat = re.compile(rb"(?:\xcc{" + str(min_bytes).encode() + rb",}|\x00{" + str(min_bytes).encode() + rb",})")
    hits = []
    for m in pat.finditer(text):
        va0 = 0x401000 + m.start()
        va1 = 0x401000 + m.end()
        byte_kind = "0x00" if text[m.start()] == 0 else "0xcc"
        hits.append((va1 - va0, byte_kind, va0, va1))
    hits.sort(reverse=True)
    print(f"Found {len(hits)} contiguous padding runs >= {min_bytes}B in .text (showing top {min(limit, len(hits))}):")
    for sz, kind, va0, va1 in hits[:limit]:
        print(f"  {sz:4d}B [{kind}]  {hex(va0)}..{hex(va1)}")


def _parse_pe_sections(data: bytes) -> dict[str, tuple[int, int, int]]:
    """Return {section_name: (va_start, raw_off, scan_size)} from PE headers."""
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_off:pe_off + 4] != b"PE\x00\x00":
        raise ValueError("Invalid PE signature")
    nsec = struct.unpack_from("<H", data, pe_off + 6)[0]
    opt_sz = struct.unpack_from("<H", data, pe_off + 20)[0]
    img_base = struct.unpack_from("<I", data, pe_off + 24 + 28)[0]
    sec_off = pe_off + 24 + opt_sz
    out: dict[str, tuple[int, int, int]] = {}
    for i in range(nsec):
        nm, vsize, rva, rsz, roff = struct.unpack_from("<8s4I", data, sec_off + 40 * i)
        name = nm.rstrip(b"\x00").decode("ascii", "replace")
        span = min(vsize, rsz) if vsize and rsz else (vsize or rsz)
        span = max(0, min(span, len(data) - roff)) if roff < len(data) else 0
        out[name] = (img_base + rva, roff, span)
    return out


def check_refs(bak_path: str, start_va: int, end_va: int) -> None:
    with open(bak_path, "rb") as f:
        data = f.read()
    secs = _parse_pe_sections(data)
    text_va, text_s, text_sz = secs[".text"]
    text_e = text_s + text_sz

    ext_pointers = []
    for sec_name, step in ((".text", 1), (".rdata", 4), (".data", 4)):
        if sec_name not in secs:
            continue
        sva, roff, span = secs[sec_name]
        sec_tag = sec_name.lstrip(".")
        for rel_off in range(0, span - 3, step):
            src_va = sva + rel_off
            if sec_name == ".text" and start_va <= src_va < end_va:
                continue
            val = struct.unpack_from("<I", data, roff + rel_off)[0]
            if start_va <= val < end_va:
                ext_pointers.append((hex(roff + rel_off), f"{sec_tag}:{hex(src_va)}", hex(val)))

    ext_rel = []
    for pos in range(text_s, text_e - 4):
        instr_va = pos - text_s + text_va
        if start_va <= instr_va < end_va:
            continue
        op = data[pos]
        if op in (0xe8, 0xe9) or (pos > text_s and data[pos - 1] == 0x0f and 0x80 <= op <= 0x8f):
            rel = struct.unpack_from("<i", data, pos + 1)[0]
            tgt = instr_va + 5 + rel
            if start_va <= tgt < end_va:
                ext_rel.append((hex(instr_va), hex(tgt)))

    print(f"External references into {hex(start_va)}..{hex(end_va)}:")
    print(f"  Absolute 32-bit pointers ({len(ext_pointers)}): {ext_pointers[:20]}")
    print(f"  Relative calls/jumps     ({len(ext_rel)}): {ext_rel[:20]}")


def main() -> int:
    default_bak = to_host_path(os.path.join(DEFAULT_GAME_DIR, "edge.exe.bak"))
    default_live = to_host_path(os.path.join(DEFAULT_GAME_DIR, "edge.exe"))

    p = argparse.ArgumentParser(description="Check remaining patch space and reference safety in EDGE.")
    p.add_argument("--exe", default=default_live, help="Patched executable to check used/free slot budgets")
    p.add_argument("--bak", default=default_bak, help="Unmodified edge.exe.bak for padding/reference scans")
    p.add_argument("--scan-padding", type=int, metavar="MIN_BYTES", help="Scan .text in bak for 0xcc padding >= N bytes")
    p.add_argument("--limit", type=int, default=20, help="Max results for --scan-padding")
    p.add_argument("--check-refs", nargs=2, metavar=("START_VA", "END_VA"), help="Check external pointers/branches into VA range")
    args = p.parse_args()

    if args.scan_padding is not None:
        scan_padding(args.bak, args.scan_padding, args.limit)
        return 0
    if args.check_refs is not None:
        check_refs(args.bak, parse_hex(args.check_refs[0]), parse_hex(args.check_refs[1]))
        return 0

    target = args.exe if os.path.exists(args.exe) else args.bak
    show_status(target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
