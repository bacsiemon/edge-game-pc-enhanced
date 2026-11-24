"""Customizable `time format` Options row: def (0'00"00) vs new (0:00.00).

Adds OptionsMenu & PauseMenu Page 1 row 9 (`time format`: `def` / `new`, persisted in
`local_savedata.ttsav` as `timeFmt`) driving G_TIME_FMT (0x5FAA34, 0 = def, 1 = new):
  - Level times (DebriefMenu): `%.2d' %.2d" %.2d` -> `%d:%.2d.%.2d`
    (sprintf sites 0x4aa139 / 0x4aa96f)
  - Score cards (ScoresMenu / LevelSelectMenu): `'` (0x1000) -> `:` + `.` (0x2000)
    (scaleY sites 0x4b022c / 0x4b034b / 0x4b743f / 0x4b75aa; sep2 sites 0x4b031e / 0x4b757c)
  - DebriefMenu Edge Time: `-%d,%.2d sec` / `%d,%.2d` -> `-%d.%.2d` / `%d.%.2d` (no `sec` suffix, site 0x4aa5e6)
  - Edge Time HUD / InstructionsMenu demo separator glyph (`'` -> `.`) is handled in ui_smooth_ext.py
Requires ui_scale_menu_ini (11-row OptionsMenu / 11-row PauseMenu Page 1 layout) and camera_angle (rows 7-8).
"""

import struct

from .common import assemble_at, expect_replace, link_block_with_symbols, put_bytes, put_u32, va2off
from .ui_scale_menu_ini import (
    JTBL_9,
    MAIN_DRAW_AUTO_HOOK_VA,
    PAUSE_CAM_DRAW_HOOK_VA,
    PAUSE_DRAW_DIV_VA,
    PAUSE_DRAW_TEXT_VA,
    options_row_text_y,
)
from .ui_smooth_ext import BLOCK_V_END, BLOCK_V_START, BLOCK_W_END, BLOCK_W_START, BSS_G_TIME_FMT

# Sprintf call sites pushing the level-time / edge-time format strings (all `push imm32` unless noted)
LEVEL_FMT_SITE_A      = 0x4AA139   # push 0x58cc58 (`%.2d' %.2d" %.2d`)
LEVEL_FMT_SITE_B      = 0x4AA96F   # push 0x58cc88 (identical text)
EDGE_FMT_SITE         = 0x4AA5E6   # jle +7; push 0x58cc70; jmp +5; push 0x58cc80 (14 B)
SCORE_SEP_SCALEY_SITES = (0x4B022C, 0x4B034B, 0x4B743F, 0x4B75AA)  # push 0x1000
SCORE_SEP2_WSTR_SITES  = ((0x4B031E, 0x58D0D8), (0x4B757C, 0x58D4F8))  # push u":"
MAIN_JTBL_SITE        = 0x4B26FF   # jmp DWORD PTR [eax*4 + JTBL]
PAUSE_JTBL_SITE       = 0x408091   # jmp DWORD PTR [eax*4 + JTBL] inside PAUSE_DISP_VA
LOAD_TTSAV_SITE       = 0x408060   # call loadX
SAVE_TTSAV_SITE       = 0x4080C8   # jmp saveX
ROW_TIME              = 9


def _rel_target(buf: bytearray, va: int, opcode: int) -> int:
    off = va2off(va)
    assert buf[off] == opcode, f"Expected opcode {hex(opcode)} at {hex(va)}, got {hex(buf[off])}"
    return va + 5 + struct.unpack_from("<i", buf, off + 1)[0]


def _free_tail(buf: bytearray, start: int, end: int) -> tuple[int, int]:
    """Return (aligned_start, size) of the trailing 0xcc run of an already-built block."""
    i = end
    while i > start and buf[va2off(i - 1)] == 0xCC:
        i -= 1
    i = (i + 15) & ~15
    assert i < end, f"No free tail in block {hex(start)}..{hex(end)}"
    return i, end - i


def _wstr_units(text: str) -> str:
    return ",".join(str(ord(c)) for c in text)


def apply_time_format(buf: bytearray, include_skip_row: bool = False) -> dict[str, tuple[int, int]]:
    # 1. Resolve chain targets from the already-installed menu/ttsav hooks
    old_draw = _rel_target(buf, MAIN_DRAW_AUTO_HOOK_VA, 0xE8)
    old_pause_draw = _rel_target(buf, PAUSE_CAM_DRAW_HOOK_VA, 0xE8)
    old_load = _rel_target(buf, LOAD_TTSAV_SITE, 0xE8)
    old_save = _rel_target(buf, SAVE_TTSAV_SITE, 0xE9)

    jt_off = va2off(JTBL_9)
    jt = list(struct.unpack_from("<10I", buf, jt_off))  # [0..8] actions, [9] back

    data_va, data_cap = _free_tail(buf, BLOCK_V_START, BLOCK_V_END)
    code_va, code_cap = _free_tail(buf, BLOCK_W_START, BLOCK_W_END)

    # 2. Data slot: format strings, wstrings, 11-entry OptionsMenu jump table
    data_lines = [
        ".globl fmtLvlModern, fmtEdgeModNeg, fmtEdgeModPos, strSavTimeKey, wstrTimeLbl, wstrDefault, wstrModern, wstrDot, mainJtbl",
        'fmtLvlModern: .asciz "%d:%.2d.%.2d"',
        'fmtEdgeModNeg: .asciz "-%d.%.2d"',
        'fmtEdgeModPos: .asciz "%d.%.2d"',
        'strSavTimeKey: .asciz "timeFmt"',
        ".balign 4",
        "wstrTimeBuf:",
        f".short {_wstr_units('time format')},0",
        "wstrTimeLbl:",
        ".long wstrTimeBuf, 0, 0, 0, 11, 11",
        "wstrDefault:",
        f".short {_wstr_units('def')},0,0,0,0,0",
        ".long 3, 7",
        "wstrModern:",
        f".short {_wstr_units('new')},0,0,0,0,0",
        ".long 3, 7",
        "wstrDot:",
        ".short 46, 0",
        "mainJtbl:",
        ".long " + ", ".join(hex(v) for v in jt[:9]) + ", timeFmtActionAddr, " + hex(jt[9]),
    ]
    # timeFmtActionAddr is only known after the code slot is linked; link code first with data symbols as externs.
    # Two-pass: assemble data once with a dummy to learn symbol addresses, then re-assemble with the real action VA.
    dummy, data_syms = link_block_with_symbols(
        data_va, ["timeFmtActionAddr = 0"] + data_lines, data_cap
    )
    code_lines = [
        ".globl timeFmtPush, fmtSelEdge, sepScaleYPush, sep2WstrPush, timeFmtAction, fmtTimeWstr, drawRow9Main, pauseDrawTimeFmt, loadTimeTtsav, saveTimeTtsav",

        # push-imm32 replacement for the level-time format pointer: pushes fmt, preserves all registers
        "timeFmtPush:",
        "push eax",
        "mov eax, 0x58cc58",
        "cmp BYTE PTR ds:G_TIME_FMT, 0",
        "je .tfp_go",
        "mov eax, OFFSET fmtLvlModern",
        ".tfp_go:",
        "xchg DWORD PTR [esp+4], eax",
        "xchg DWORD PTR [esp], eax",
        "ret",

        # DebriefMenu Edge Time: ecx = format for value in [ebp-0xfc] (<= 0 -> `-` variant); preserves eax
        "fmtSelEdge:",
        "mov ecx, 0x58cc70",
        "mov edx, 0x58cc80",
        "cmp BYTE PTR ds:G_TIME_FMT, 0",
        "je .fse_go",
        "mov ecx, OFFSET fmtEdgeModNeg",
        "mov edx, OFFSET fmtEdgeModPos",
        ".fse_go:",
        "cmp DWORD PTR [ebp-0xfc], 0",
        "jle .fse_out",
        "mov ecx, edx",
        ".fse_out:",
        "ret",

        # Score cards separator height scale (0x1000 top tick in def vs 0x2000 full height in new); preserves all regs
        "sepScaleYPush:",
        "push eax",
        "mov eax, 0x1000",
        "cmp BYTE PTR ds:G_TIME_FMT, 0",
        "je .ssy_go",
        "mov eax, 0x2000",
        ".ssy_go:",
        "xchg DWORD PTR [esp+4], eax",
        "xchg DWORD PTR [esp], eax",
        "ret",

        # Score cards second separator string (u":" in def vs u"." in new); preserves all regs
        "sep2WstrPush:",
        "push eax",
        "mov eax, 0x58d0d8",
        "cmp BYTE PTR ds:G_TIME_FMT, 0",
        "je .s2w_go",
        "mov eax, OFFSET wstrDot",
        ".s2w_go:",
        "xchg DWORD PTR [esp+4], eax",
        "xchg DWORD PTR [esp], eax",
        "ret",

        # Options row 9 action (left/right both toggle)
        "timeFmtAction:",
        "xor BYTE PTR ds:G_TIME_FMT, 1",
        "jmp 0x484d75",

        # fmtTimeWstr(ecx = temp wstr*) -> eax = static wstring*
        "fmtTimeWstr:",
        "mov eax, OFFSET wstrDefault",
        "cmp BYTE PTR ds:G_TIME_FMT, 0",
        "je .ftw_ret",
        "mov eax, OFFSET wstrModern",
        ".ftw_ret:",
        "ret",

        # drawMainRow(eax = rowIdx, esi = label*, ebx = y, edx = fmtFn*); preserves edi (= 0), ebp
        "drawMainRow:",
        "push edx",
        "call 0x53994b",
        "test al, al",
        "je .dmr_ret",
        "mov eax, esi",
        "mov ecx, ebx",
        "mov edx, 0x1e",
        "call 0x539986",
        "sub esp, 0x18",
        "mov ecx, esp",
        "call DWORD PTR [esp+0x18]",
        "mov ecx, ebx",
        "mov edx, 0xe6",
        "call 0x539986",
        "add esp, 0x18",
        ".dmr_ret:",
        "pop edx",
        "ret",

        "drawRow9Main:",
        "call OLD_DRAW",
        "push ebx",
        "push esi",
        f"mov eax, {ROW_TIME}",
        "mov esi, OFFSET wstrTimeLbl",
        f"mov ebx, {hex(options_row_text_y(ROW_TIME, True, True, include_skip_row))}",
        "mov edx, OFFSET fmtTimeWstr",
        "call drawMainRow",
        "pop esi",
        "pop ebx",
        "ret",

        # Pause Menu Page 1 row 9 (divider at 0x19e, text at 0x1b5)
        "pauseDrawTimeFmt:",
        "mov edx, 0x19e",
        f"call {hex(PAUSE_DRAW_DIV_VA)}",
        "mov eax, OFFSET wstrTimeLbl",
        "mov edx, 0x158",
        "mov ecx, 0x1b5",
        f"call {hex(PAUSE_DRAW_TEXT_VA)}",
        "call fmtTimeWstr",
        "mov edx, 0x23d",
        "mov ecx, 0x1b5",
        f"call {hex(PAUSE_DRAW_TEXT_VA)}",
        "jmp OLD_PAUSE_DRAW",

        # Chained after the existing ttsav loader: timeFmt in {0, 1}
        "loadTimeTtsav:",
        "call OLD_LOAD",
        "mov BYTE PTR ds:G_TIME_FMT, 0",
        "mov eax, OFFSET strSavTimeKey",
        "call 0x539770",
        "test al, al",
        "je .ltt_ret",
        "sub esp, 4",
        "fistp DWORD PTR [esp]",
        "pop eax",
        "cmp eax, 1",
        "ja .ltt_ret",
        "mov BYTE PTR ds:G_TIME_FMT, al",
        ".ltt_ret:",
        "ret",

        # Chained after the existing ttsav saver (tail jump to saveOneDouble)
        "saveTimeTtsav:",
        "call OLD_SAVE",
        "movzx eax, BYTE PTR ds:G_TIME_FMT",
        "push eax",
        "fild DWORD PTR [esp]",
        "add esp, 4",
        "mov eax, OFFSET strSavTimeKey",
        "jmp 0x5398dc",
    ]
    code_ext = {
        **data_syms,
        "G_TIME_FMT": BSS_G_TIME_FMT,
        "OLD_DRAW": old_draw,
        "OLD_PAUSE_DRAW": old_pause_draw,
        "OLD_LOAD": old_load,
        "OLD_SAVE": old_save,
    }
    code, code_syms = link_block_with_symbols(code_va, code_lines, code_cap, ext_syms=code_ext)

    # Re-link data with the real action address
    data, data_syms2 = link_block_with_symbols(
        data_va,
        [f"timeFmtActionAddr = {hex(code_syms['timeFmtAction'])}"] + data_lines,
        data_cap,
    )
    assert data_syms2 == data_syms, "data symbol layout changed between passes"

    put_bytes(buf, data_va, data, data_cap)
    put_bytes(buf, code_va, code, code_cap)

    # 3. Hook installs
    for site, old_imm in ((LEVEL_FMT_SITE_A, 0x58CC58), (LEVEL_FMT_SITE_B, 0x58CC88)):
        expect_replace(
            buf, site,
            b"\x68" + struct.pack("<I", old_imm),
            assemble_at(site, [f"call {hex(code_syms['timeFmtPush'])}"], 5),
        )

    old_edge = bytes([0x7E, 0x07, 0x68]) + struct.pack("<I", 0x58CC70) + bytes([0xEB, 0x05, 0x68]) + struct.pack("<I", 0x58CC80)
    new_edge = assemble_at(EDGE_FMT_SITE, [f"call {hex(code_syms['fmtSelEdge'])}", "push ecx"]).ljust(len(old_edge), b"\x90")
    expect_replace(buf, EDGE_FMT_SITE, old_edge, new_edge)

    for site in SCORE_SEP_SCALEY_SITES:
        expect_replace(
            buf, site,
            b"\x68\x00\x10\x00\x00",
            assemble_at(site, [f"call {hex(code_syms['sepScaleYPush'])}"], 5),
        )

    for site, old_imm in SCORE_SEP2_WSTR_SITES:
        expect_replace(
            buf, site,
            b"\x68" + struct.pack("<I", old_imm),
            assemble_at(site, [f"call {hex(code_syms['sep2WstrPush'])}"], 5),
        )

    expect_replace(
        buf, MAIN_JTBL_SITE,
        b"\xff\x24\x85" + struct.pack("<I", JTBL_9),
        b"\xff\x24\x85" + struct.pack("<I", data_syms["mainJtbl"]),
    )
    expect_replace(
        buf, PAUSE_JTBL_SITE,
        b"\xff\x24\x85" + struct.pack("<I", JTBL_9),
        b"\xff\x24\x85" + struct.pack("<I", data_syms["mainJtbl"]),
    )

    expect_replace(
        buf, MAIN_DRAW_AUTO_HOOK_VA,
        assemble_at(MAIN_DRAW_AUTO_HOOK_VA, [f"call {hex(old_draw)}"], 5),
        assemble_at(MAIN_DRAW_AUTO_HOOK_VA, [f"call {hex(code_syms['drawRow9Main'])}"], 5),
    )
    expect_replace(
        buf, PAUSE_CAM_DRAW_HOOK_VA,
        assemble_at(PAUSE_CAM_DRAW_HOOK_VA, [f"call {hex(old_pause_draw)}"], 5),
        assemble_at(PAUSE_CAM_DRAW_HOOK_VA, [f"call {hex(code_syms['pauseDrawTimeFmt'])}"], 5),
    )
    expect_replace(
        buf, LOAD_TTSAV_SITE,
        assemble_at(LOAD_TTSAV_SITE, [f"call {hex(old_load)}"], 5),
        assemble_at(LOAD_TTSAV_SITE, [f"call {hex(code_syms['loadTimeTtsav'])}"], 5),
    )
    expect_replace(
        buf, SAVE_TTSAV_SITE,
        assemble_at(SAVE_TTSAV_SITE, [f"jmp {hex(old_save)}"], 5),
        assemble_at(SAVE_TTSAV_SITE, [f"jmp {hex(code_syms['saveTimeTtsav'])}"], 5),
    )

    return {
        "time_fmt_code": (len(code), code_cap),
        "time_fmt_data": (len(data), data_cap),
    }
