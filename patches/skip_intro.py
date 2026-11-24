"""Customizable `skip intro` Options row: off / on.

Adds OptionsMenu & PauseMenu Page 1 row 10 (`skip intro`: `off` / `on`, persisted in
`local_savedata.ttsav` as `skipIntro`) driving G_SKIP_INTRO (0x5FAA35, 0 = off, 1 = on):
  - Starts MenuScreen intro timeline at frame 253.0 (right as the splash wipe finishes)
  - Enables MainMenu rendering, update, and mouse input immediately on frame 0
  - Suppresses intro click-skip abort and late chime in MenuScreen::update
  - Evaluates camera splines / offset and EDGE logo parent scale nodes at frame 320.0
Requires ui_scale_menu_ini (12-row OptionsMenu / 12-row PauseMenu Page 1 layout),
camera_angle (rows 7-8), and time_format (row 9).
"""

import struct

from .common import assemble_at, expect_replace, link_block_with_symbols, put_bytes, va2off
from .time_format import (
    LOAD_TTSAV_SITE,
    MAIN_JTBL_SITE,
    PAUSE_JTBL_SITE,
    SAVE_TTSAV_SITE,
    _free_tail,
    _rel_target,
    _wstr_units,
)
from .ui_scale_menu_ini import (
    MAIN_DRAW_AUTO_HOOK_VA,
    PAUSE_CAM_DRAW_HOOK_VA,
    PAUSE_DRAW_DIV_VA,
    PAUSE_DRAW_LOC_VA,
    PAUSE_DRAW_TEXT_VA,
    options_row_text_y,
)
from .ui_smooth_ext import (
    BLOCK_X_END,
    BLOCK_X_START,
    BLOCK_Y_END,
    BLOCK_Y_START,
    BSS_G_SKIP_INTRO,
)

ROW_SKIP_INTRO        = 10
MS_CTOR_HOOK_VA       = 0x49F262  # call 0x421ff0 (~RefPtr) after menu_background / shadow setup
MS_MOUSE_DOWN_GATE_VA = 0x49C26B  # 0x49c26b..0x49c290 (37 B)
MS_MOUSE_MOVE_GATE_VA = 0x49C30F  # 0x49c30f..0x49c334 (37 B)
MS_RENDER_GATE_VA     = 0x49C884  # 0x49c884..0x49c8a9 (37 B)
MS_UPDATE_GATE_VA     = 0x49C97D  # 0x49c97d..0x49c9a2 (37 B)
MS_UPDATE_SKIP_VA     = 0x49CA01  # 0x49ca01..0x49ca30 (47 B)
MS_CAM_T1_VA          = 0x49CB21  # movss xmm0, DWORD PTR [esi+0x164] (8 B)
MS_CAM_T2_VA          = 0x49CB34  # movss xmm0, DWORD PTR [esi+0x164] (8 B)
MS_CAM_OFF_T_VA       = 0x49CCF7  # movss xmm1, DWORD PTR [esi+0x164] (8 B)
SPLINE_EVAL_HOOK_VA   = 0x4323CF  # call 0x4392e0 in Spline::applyMatrix (5 B)


def _rel8(opcode: int, instr_va: int, target_va: int) -> str:
    """Emit an explicit 2-byte short jump (GNU as defaults external imm32 targets to rel32)."""
    disp = target_va - (instr_va + 2)
    assert -128 <= disp <= 127, f"Short branch out of range at {hex(instr_va)} -> {hex(target_va)}: {disp}"
    return f".byte {hex(opcode)}, {disp & 0xff}"


def apply_skip_intro(buf: bytearray) -> dict[str, tuple[int, int]]:
    # 1. Resolve chain targets from the already-installed menu/ttsav hooks
    old_draw = _rel_target(buf, MAIN_DRAW_AUTO_HOOK_VA, 0xE8)
    old_pause_draw = _rel_target(buf, PAUSE_CAM_DRAW_HOOK_VA, 0xE8)
    old_load = _rel_target(buf, LOAD_TTSAV_SITE, 0xE8)
    old_save = _rel_target(buf, SAVE_TTSAV_SITE, 0xE9)

    old_jtbl_va = struct.unpack_from("<I", buf, va2off(MAIN_JTBL_SITE) + 3)[0]
    jt = list(struct.unpack_from("<11I", buf, va2off(old_jtbl_va)))  # [0..9] actions, [10] back

    data_va, data_cap = _free_tail(buf, BLOCK_X_START, BLOCK_X_END)
    code_va, code_cap = _free_tail(buf, BLOCK_Y_START, BLOCK_Y_END)

    # 2. Data + runtime intro slot (Block X tail): save key, label wstring, 12-entry jump table, runtime hooks
    data_lines = [
        ".globl strSavSkipKey, wstrSkipLbl, mainJtbl",
        ".globl initSkipIntro, getCamIntroT, getCamIntroT1, evalSplineCurve",
        'strSavSkipKey: .asciz "skipIntro"',
        ".balign 4",
        "wstrSkipBuf:",
        f".short {_wstr_units('skip intro')},0",
        ".balign 4",
        "wstrSkipLbl:",
        ".long wstrSkipBuf, 0, 0, 0, 10, 10",
        "mainJtbl:",
        ".long " + ", ".join(hex(v) for v in jt[:10]) + ", skipIntroActionAddr, " + hex(jt[10]),

        # MenuScreen ctor hook: start timeline at frame 253.0 (0x437d0000) and tail-call ~RefPtr (ecx intact)
        "initSkipIntro:",
        "cmp BYTE PTR ds:G_SKIP_INTRO, 0",
        "je .isi_out",
        "mov eax, 0x437d0000",
        "mov DWORD PTR [esi+0x164], eax",
        "mov BYTE PTR [esi+0x11c], 1",
        "mov edx, DWORD PTR [esi+0x120]",
        "test edx, edx",
        "je .isi_shd",
        "mov DWORD PTR [edx+0x20], eax",
        ".isi_shd:",
        "mov edx, DWORD PTR [esi+0x128]",
        "test edx, edx",
        "je .isi_out",
        "mov DWORD PTR [edx+0x20], eax",
        ".isi_out:",
        "jmp 0x421ff0",

        # Camera intro time getters (return final frame 320.0 from [esi+0x158] when skip intro is on)
        "getCamIntroT:",
        "movss xmm0, DWORD PTR [esi+0x164]",
        "cmp BYTE PTR ds:G_SKIP_INTRO, 0",
        "je .gcit_ret",
        "movss xmm0, DWORD PTR [esi+0x158]",
        ".gcit_ret:",
        "ret",

        "getCamIntroT1:",
        "movss xmm1, DWORD PTR [esi+0x164]",
        "cmp BYTE PTR ds:G_SKIP_INTRO, 0",
        "je .gcit1_ret",
        "movss xmm1, DWORD PTR [esi+0x158]",
        ".gcit1_ret:",
        "ret",

        # Spline::applyMatrix hook: lock 8B0F35C6 / 7DE27093 EDGE logo parent scale nodes (sx.default == 1.5f) at frame 320.0
        "evalSplineCurve:",
        "cmp BYTE PTR ds:G_SKIP_INTRO, 0",
        "je .esc_go",
        "test ecx, ecx",
        "je .esc_go",
        "mov eax, DWORD PTR [ecx+0x30]",
        "test eax, eax",
        "je .esc_go",
        "cmp DWORD PTR [eax], 0x3fc00000",
        "jne .esc_go",
        "mov DWORD PTR [esp+4], 0x43a00000",
        ".esc_go:",
        "jmp 0x4392e0",
    ]
    data_ext = {"G_SKIP_INTRO": BSS_G_SKIP_INTRO}
    dummy, data_syms = link_block_with_symbols(
        data_va, ["skipIntroActionAddr = 0"] + data_lines, data_cap, ext_syms=data_ext
    )

    row_y = options_row_text_y(ROW_SKIP_INTRO, True, True, True)
    code_lines = [
        ".globl skipIntroAction, drawRow10Main, pauseDrawSkipIntro, loadSkipTtsav, saveSkipTtsav",

        # Options row 10 action (left/right/enter all toggle)
        "skipIntroAction:",
        "xor BYTE PTR ds:G_SKIP_INTRO, 1",
        "jmp 0x484d75",

        # Main OptionsMenu row 10 drawer
        "drawRow10Main:",
        "call OLD_DRAW",
        f"mov eax, {ROW_SKIP_INTRO}",
        "call 0x53994b",
        "test al, al",
        "je .drm_ret",
        "mov eax, OFFSET wstrSkipLbl",
        f"mov ecx, {hex(row_y)}",
        "mov edx, 0x1e",
        "call 0x539986",
        "sub esp, 0x18",
        "mov edx, 0xc",
        "cmp BYTE PTR ds:G_SKIP_INTRO, 0",
        "je .drm_loc",
        "mov dl, 0xb",
        ".drm_loc:",
        "mov eax, esp",
        "push edx",
        "push eax",
        "call 0x49fd70",
        "add esp, 8",
        f"mov ecx, {hex(row_y)}",
        "mov edx, 0xe6",
        "call 0x539986",
        "cmp DWORD PTR [esp+20], 8",
        "jb .drm_pop",
        "push DWORD PTR [esp]",
        "call 0x4ba62a",
        "add esp, 4",
        ".drm_pop:",
        "add esp, 0x18",
        ".drm_ret:",
        "ret",

        # Pause Menu Page 1 row 10 (divider at 0x1bf, text at 0x1d6, preserves ebx=0, esi, ebp)
        "pauseDrawSkipIntro:",
        "mov edx, 0x1bf",
        f"call {hex(PAUSE_DRAW_DIV_VA)}",
        "mov eax, OFFSET wstrSkipLbl",
        "mov edx, 0x158",
        "mov ecx, 0x1d6",
        f"call {hex(PAUSE_DRAW_TEXT_VA)}",
        "mov eax, 0xc",
        "cmp BYTE PTR ds:G_SKIP_INTRO, bl",
        "je .pdsi_loc",
        "mov al, 0xb",
        ".pdsi_loc:",
        "mov edx, 0x23d",
        "mov ecx, 0x1d6",
        f"call {hex(PAUSE_DRAW_LOC_VA)}",
        "jmp OLD_PAUSE_DRAW",

        # Chained after ttsav loader: skipIntro in {0, 1}
        "loadSkipTtsav:",
        "call OLD_LOAD",
        "mov BYTE PTR ds:G_SKIP_INTRO, 0",
        "mov eax, OFFSET strSavSkipKey",
        "call 0x539770",
        "test al, al",
        "je .lst_ret",
        "sub esp, 4",
        "fistp DWORD PTR [esp]",
        "pop eax",
        "test eax, eax",
        "setne al",
        "mov BYTE PTR ds:G_SKIP_INTRO, al",
        ".lst_ret:",
        "ret",

        # Chained after ttsav saver (tail jump to saveOneDouble)
        "saveSkipTtsav:",
        "call OLD_SAVE",
        "movzx eax, BYTE PTR ds:G_SKIP_INTRO",
        "push eax",
        "fild DWORD PTR [esp]",
        "add esp, 4",
        "mov eax, OFFSET strSavSkipKey",
        "jmp 0x5398dc",
    ]
    code_ext = {
        **data_syms,
        "G_SKIP_INTRO": BSS_G_SKIP_INTRO,
        "OLD_DRAW": old_draw,
        "OLD_PAUSE_DRAW": old_pause_draw,
        "OLD_LOAD": old_load,
        "OLD_SAVE": old_save,
    }
    code, code_syms = link_block_with_symbols(code_va, code_lines, code_cap, ext_syms=code_ext)

    # Re-link data with the real action address
    data, data_syms2 = link_block_with_symbols(
        data_va,
        [f"skipIntroActionAddr = {hex(code_syms['skipIntroAction'])}"] + data_lines,
        data_cap,
        ext_syms=data_ext,
    )
    assert data_syms2 == data_syms, "data symbol layout changed between passes"

    put_bytes(buf, data_va, data, data_cap)
    put_bytes(buf, code_va, code, code_cap)

    # 3. Hook installs
    expect_replace(
        buf,
        MS_CTOR_HOOK_VA,
        assemble_at(MS_CTOR_HOOK_VA, ["call 0x421ff0"], 5),
        assemble_at(MS_CTOR_HOOK_VA, [f"call {hex(data_syms['initSkipIntro'])}"], 5),
    )

    for site, reg, run_va, skip_va in (
        (MS_MOUSE_DOWN_GATE_VA, "ecx", 0x49C290, 0x49C2A9),
        (MS_MOUSE_MOVE_GATE_VA, "ecx", 0x49C334, 0x49C353),
        (MS_RENDER_GATE_VA,     "esi", 0x49C8A9, 0x49C8C0),
        (MS_UPDATE_GATE_VA,     "esi", 0x49C9A2, 0x49C9BA),
    ):
        orig_gate = assemble_at(site, [
            f"cmp BYTE PTR [{reg}+0x15c], 0",
            _rel8(0x75, site + 7, skip_va),
            f"movss xmm0, DWORD PTR [{reg}+0x164]",
            f"movss xmm1, DWORD PTR [{reg}+0x158]",
            "cvtps2pd xmm0, xmm0",
            "cvtps2pd xmm1, xmm1",
            "comisd xmm0, xmm1",
            _rel8(0x76, site + 35, skip_va),
        ], 37)
        new_gate = assemble_at(site, [
            f"cmp BYTE PTR ds:{hex(BSS_G_SKIP_INTRO)}, 0",
            _rel8(0x75, site + 7, run_va),
            f"cmp BYTE PTR [{reg}+0x15c], 0",
            _rel8(0x75, site + 16, skip_va),
            f"movss xmm0, DWORD PTR [{reg}+0x164]",
            f"ucomiss xmm0, DWORD PTR [{reg}+0x158]",
            _rel8(0x76, site + 33, skip_va),
        ], 35).ljust(37, b"\x90")
        expect_replace(buf, site, orig_gate, new_gate)

    orig_skip = assemble_at(MS_UPDATE_SKIP_VA, [
        "cmp BYTE PTR [esi+0x15c], 0",
        _rel8(0x75, MS_UPDATE_SKIP_VA + 7, 0x49CA26),
        "movss xmm0, DWORD PTR [esi+0x164]",
        "movss xmm1, DWORD PTR [esi+0x158]",
        "cvtps2pd xmm0, xmm0",
        "cvtps2pd xmm1, xmm1",
        "comisd xmm0, xmm1",
        _rel8(0x77, MS_UPDATE_SKIP_VA + 35, 0x49CA2C),
        "mov BYTE PTR [ebp-0x65], 0",
        _rel8(0xEB, MS_UPDATE_SKIP_VA + 41, 0x49CA30),
        "mov BYTE PTR [ebp-0x65], 1",
    ], 47)
    new_skip = assemble_at(MS_UPDATE_SKIP_VA, [
        "mov BYTE PTR [ebp-0x65], 1",
        f"cmp BYTE PTR ds:{hex(BSS_G_SKIP_INTRO)}, 0",
        _rel8(0x75, MS_UPDATE_SKIP_VA + 11, 0x49CA86),
        "cmp BYTE PTR [esi+0x15c], 0",
        "jne .not_done",
        "movss xmm0, DWORD PTR [esi+0x164]",
        "ucomiss xmm0, DWORD PTR [esi+0x158]",
        _rel8(0x77, MS_UPDATE_SKIP_VA + 37, 0x49CA30),
        ".not_done:",
        "mov BYTE PTR [ebp-0x65], 0",
    ], 43).ljust(47, b"\x90")
    expect_replace(buf, MS_UPDATE_SKIP_VA, orig_skip, new_skip)

    expect_replace(
        buf,
        MS_CAM_T1_VA,
        b"\xf3\x0f\x10\x86\x64\x01\x00\x00",
        assemble_at(MS_CAM_T1_VA, [f"call {hex(data_syms['getCamIntroT'])}"], 5).ljust(8, b"\x90"),
    )
    expect_replace(
        buf,
        MS_CAM_T2_VA,
        b"\xf3\x0f\x10\x86\x64\x01\x00\x00",
        assemble_at(MS_CAM_T2_VA, [f"call {hex(data_syms['getCamIntroT'])}"], 5).ljust(8, b"\x90"),
    )
    expect_replace(
        buf,
        MS_CAM_OFF_T_VA,
        b"\xf3\x0f\x10\x8e\x64\x01\x00\x00",
        assemble_at(MS_CAM_OFF_T_VA, [f"call {hex(data_syms['getCamIntroT1'])}"], 5).ljust(8, b"\x90"),
    )
    expect_replace(
        buf,
        SPLINE_EVAL_HOOK_VA,
        assemble_at(SPLINE_EVAL_HOOK_VA, ["call 0x4392e0"], 5),
        assemble_at(SPLINE_EVAL_HOOK_VA, [f"call {hex(data_syms['evalSplineCurve'])}"], 5),
    )

    expect_replace(
        buf, MAIN_JTBL_SITE,
        b"\xff\x24\x85" + struct.pack("<I", old_jtbl_va),
        b"\xff\x24\x85" + struct.pack("<I", data_syms["mainJtbl"]),
    )
    expect_replace(
        buf, PAUSE_JTBL_SITE,
        b"\xff\x24\x85" + struct.pack("<I", old_jtbl_va),
        b"\xff\x24\x85" + struct.pack("<I", data_syms["mainJtbl"]),
    )

    expect_replace(
        buf, MAIN_DRAW_AUTO_HOOK_VA,
        assemble_at(MAIN_DRAW_AUTO_HOOK_VA, [f"call {hex(old_draw)}"], 5),
        assemble_at(MAIN_DRAW_AUTO_HOOK_VA, [f"call {hex(code_syms['drawRow10Main'])}"], 5),
    )
    expect_replace(
        buf, PAUSE_CAM_DRAW_HOOK_VA,
        assemble_at(PAUSE_CAM_DRAW_HOOK_VA, [f"call {hex(old_pause_draw)}"], 5),
        assemble_at(PAUSE_CAM_DRAW_HOOK_VA, [f"call {hex(code_syms['pauseDrawSkipIntro'])}"], 5),
    )
    expect_replace(
        buf, LOAD_TTSAV_SITE,
        assemble_at(LOAD_TTSAV_SITE, [f"call {hex(old_load)}"], 5),
        assemble_at(LOAD_TTSAV_SITE, [f"call {hex(code_syms['loadSkipTtsav'])}"], 5),
    )
    expect_replace(
        buf, SAVE_TTSAV_SITE,
        assemble_at(SAVE_TTSAV_SITE, [f"jmp {hex(old_save)}"], 5),
        assemble_at(SAVE_TTSAV_SITE, [f"jmp {hex(code_syms['saveSkipTtsav'])}"], 5),
    )

    return {
        "skip_intro_data": (len(data), data_cap),
        "skip_intro_code": (len(code), code_cap),
    }
