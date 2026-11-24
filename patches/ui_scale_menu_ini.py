import struct
from .common import put_bytes, put_u32, expect_replace, assemble_at, link_block_with_symbols

G_AUTO_SCALE      = 0x5f8730
G_UI_SCALE        = 0x5f8798
G_INI_UI_SCALE    = 0x5f879c
G_LAST_INI_SCALE  = 0x5f87a0
G_INI_AUTO_SCALE  = 0x5f87a4
G_LAST_INI_AUTO   = 0x5f87a5

STR_INI_SCALE_KEY = 0x5e2840
STR_INI_AUTO_KEY  = 0x5e2849
STR_SAV_SCALE_KEY = 0x5e2857
STR_SAV_SCALE_INI = 0x5e2860
STR_SAV_AUTO_KEY  = 0x5e286c
STR_SAV_AUTO_INI  = 0x5e2873

WSTR_UI_SCALE_LBL = 0x5e2894
WSTR_AUTO_LBL     = 0x5e28c4
PRESET_TBL        = 0x5e28e0
PRESET_COUNT      = 9
JTBL_9            = 0x5e2998

BLOCK_D_START     = 0x484c10
BLOCK_D_END       = 0x484e5f  # 591 bytes
BLOCK_E_START     = 0x539770
BLOCK_E_END       = 0x539a19  # 681 bytes

# Fixed entry/hook VAs exported for modular chaining (e.g. patches/camera_angle.py)
FORMAT_UISCALE_WSTR_VA = 0x408120
FORMAT_FLOAT_WSTR_VA   = 0x408125
MAIN_DRAW_ROW5_VA      = 0x4081b8
MAIN_DRAW_AUTO_HOOK_VA = MAIN_DRAW_ROW5_VA  # first instruction calls drawAutoScaleRow
PAUSE_DISP_VA          = 0x408077
PAUSE_DRAW_DIV_VA      = 0x408208
PAUSE_DRAW_TEXT_VA     = 0x484e38  # fits inside unused Block D tail after camAngleAction (0x484e2c..0x484e38)
PAUSE_DRAW_LOC_VA      = 0x4adf0a
PAUSE_RENDER_VA        = 0x4ade56
PAUSE_RENDER_END_VA    = 0x4adf53
PAUSE_CAM_DRAW_HOOK_VA = 0x4ade5b  # second instruction calls pauseDrawBack


def options_row_text_y(
    row: int,
    include_camera_row: bool = True,
    include_time_row: bool = False,
    include_skip_row: bool = False,
) -> int:
    """Text Y of OptionsMenu row `row` (relative to the panel origin), matching apply_options_menu_layout."""
    if include_skip_row:
        return 0x36 + 0x24 * row
    if include_camera_row:
        return 0x36 + 0x28 * row
    return 0x4d + 0x26 * row


def apply_options_menu_layout(
    buf: bytearray,
    include_camera_row: bool = True,
    include_time_row: bool = False,
    include_skip_row: bool = False,
) -> None:
    """
    Remove `credits` from OptionsMenu (moving it to MainMenu row 3) and expand to
    12 rows (0..11: ..., skip intro=10, back=11), 11 rows (0..10: ..., time format=9, back=10),
    10 rows (0..9: ..., dolly zoom=8, back=9), or 8 rows (0..7) when include_camera_row is False.
    """
    if include_skip_row and not (include_time_row and include_camera_row):
        raise ValueError("skip intro row requires the time format and camera rows")
    if include_time_row and not include_camera_row:
        raise ValueError("time format row requires the camera rows")
    focus_init = 0x00
    if include_skip_row:
        back_idx, row_cnt, adj_max = 0x0b, 0x0c, 0x0a
        stride, y0 = 0x24, 0x2e
        hit_h, bar_outer_h, bar_inner_h = 0x24, 0x24, 0x20
    elif include_time_row:
        back_idx, row_cnt, adj_max = 0x0a, 0x0b, 0x09
        stride, y0 = 0x28, 0x2e
        hit_h, bar_outer_h, bar_inner_h = 0x24, 0x24, 0x20
    elif include_camera_row:
        back_idx, row_cnt, adj_max = 0x09, 0x0a, 0x08
        stride, y0 = 0x28, 0x2e
        hit_h, bar_outer_h, bar_inner_h = 0x24, 0x24, 0x20
    else:
        back_idx, row_cnt, adj_max = 0x07, 0x08, 0x06
        stride, y0 = 0x26, 0x46
        hit_h, bar_outer_h, bar_inner_h = 0x20, 0x24, 0x20
    ys = [options_row_text_y(i, include_camera_row, include_time_row, include_skip_row) for i in range(5)]

    expect_replace(buf, 0x4b25e8, b"\x83\xe0\x05", bytes([0x83, 0xe0, focus_init]))
    expect_replace(buf, 0x4b2610, b"\x83\xfe\x06", bytes([0x83, 0xfe, back_idx]))
    expect_replace(buf, 0x4b2621, b"\x6b\xc0\x32\x83\xc0\x56", bytes([0x6b, 0xc0, stride, 0x83, 0xc0, y0]))
    expect_replace(buf, 0x4b264e, b"\x83\xc2\x28", bytes([0x83, 0xc2, hit_h]))
    expect_replace(buf, 0x4b2656, b"\x83\xfe\x07", bytes([0x83, 0xfe, row_cnt]))
    expect_replace(buf, 0x4b26ba, b"\x83\xf8\x04", bytes([0x83, 0xf8, adj_max]))
    expect_replace(buf, 0x4b26f6, b"\x83\xf8\x06", bytes([0x83, 0xf8, back_idx]))
    expect_replace(
        buf,
        0x4b26ff,
        b"\xff\x24\x85" + struct.pack("<I", 0x4b2790),
        b"\xff\x24\x85" + struct.pack("<I", JTBL_9),
    )
    expect_replace(buf, 0x4b27e5, b"\xc7\x46\x10\x06\x00\x00\x00", struct.pack("<3sI", b"\xc7\x46\x10", back_idx))
    expect_replace(buf, 0x4b2801, b"\x83\x7e\x10\x07", bytes([0x83, 0x7e, 0x10, row_cnt]))
    expect_replace(buf, 0x4b2822, b"\x83\x7e\x10\x04", bytes([0x83, 0x7e, 0x10, adj_max]))

    expect_replace(buf, 0x4b2929, b"\x6b\xd2\x32", bytes([0x6b, 0xd2, stride]))
    expect_replace(buf, 0x4b297c, b"\x6b\xc0\x32", bytes([0x6b, 0xc0, stride]))
    expect_replace(buf, 0x4b2e2f, b"\x83\xf8\x06", bytes([0x83, 0xf8, back_idx]))
    expect_replace(buf, 0x4b2e3e, b"\x6b\xc0\x32\x83\xc0\x56", bytes([0x6b, 0xc0, stride, 0x83, 0xc0, y0]))
    expect_replace(buf, 0x4b2e76, b"\x6a\x2c", bytes([0x6a, bar_outer_h]))
    expect_replace(buf, 0x4b2ead, b"\x6a\x28", bytes([0x6a, bar_inner_h]))
    expect_replace(buf, 0x4b2ef2, b"\x6a\x28", bytes([0x6a, bar_inner_h]))

    # Compact vertical positions for rows 0..4
    expect_replace(buf, 0x4b2faf, b"\x83\xc1\x61", bytes([0x83, 0xc1, ys[0]]))
    expect_replace(buf, 0x4b3022, b"\x83\xc2\x61", bytes([0x83, 0xc2, ys[0]]))
    expect_replace(buf, 0x4b312d, b"\x81\xc1\x93\x00\x00\x00", struct.pack("<2sI", b"\x81\xc1", ys[1]))
    expect_replace(buf, 0x4b31a6, b"\x81\xc2\x93\x00\x00\x00", struct.pack("<2sI", b"\x81\xc2", ys[1]))
    expect_replace(buf, 0x4b32ad, b"\x81\xc1\xc5\x00\x00\x00", struct.pack("<2sI", b"\x81\xc1", ys[2]))
    expect_replace(buf, 0x4b3300, b"\x81\xc1\xc5\x00\x00\x00", struct.pack("<2sI", b"\x81\xc1", ys[2]))
    expect_replace(buf, 0x4b33f1, b"\x05\xf7\x00\x00\x00", struct.pack("<BI", 0x05, ys[3]))
    expect_replace(buf, 0x4b3441, b"\x81\xc1\xf7\x00\x00\x00", struct.pack("<2sI", b"\x81\xc1", ys[3]))
    expect_replace(buf, 0x4b353d, b"\x05\x29\x01\x00\x00", struct.pack("<BI", 0x05, ys[4]))
    expect_replace(buf, 0x4b358f, b"\x81\xc2\x29\x01\x00\x00", struct.pack("<2sI", b"\x81\xc2", ys[4]))

    expect_replace(buf, 0x4b36c7, b"\x83\xf8\x06", bytes([0x83, 0xf8, back_idx]))
    expect_replace(buf, 0x4b36d6, b"\x83\xf8\x06", bytes([0x83, 0xf8, back_idx]))

    expect_replace(
        buf,
        0x4b35d3,
        b"\x8b\x55\xa4\x8b\x42\x10\x83\xf8\x05\x74\x0f",
        assemble_at(0x4b35d3, [f"call {hex(MAIN_DRAW_ROW5_VA)}", "jmp 0x4b36c1", "nop"], 11),
    )

    # Replace MainMenu row 3 ("more games" 0x32) with "credits" (0x3 -> submenu 0xa) & return CreditsMenu -> MainMenu (0x2)
    expect_replace(buf, 0x4b9827, b"\xb8\x32\x00\x00\x00", b"\xb8\x03\x00\x00\x00")
    expect_replace(buf, 0x4b9f09, b"\xb8\x32\x00\x00\x00", b"\xb8\x03\x00\x00\x00")
    expect_replace(
        buf,
        0x4ba0bf,
        b"\x6a\x01\xe8\x2a\x75\xfc\xff\x68\xc8\xd5\x58\x00",
        assemble_at(0x4ba0bf, ["push 0xc", "call 0x4815f0", "push 0xa", ".byte 0xeb, 0x45", "nop"], 12),
    )

    orig_mm_focus = assemble_at(0x4ba2de, [
        "cmp eax, 0xb",
        "jne 1f",
        "mov DWORD PTR [esi+0x10], 1",
        ".byte 0xeb, 0x22",
        "1: cmp eax, 0x9",
        "jne 2f",
        "mov DWORD PTR [esi+0x10], 2",
        ".byte 0xeb, 0x14",
        "2: .byte 0x33, 0xc9",
        "cmp eax, 0xf",
        "setne cl",
        "dec ecx",
        "and ecx, 4",
        "mov DWORD PTR [esi+0x10], ecx",
        ".byte 0xeb, 0x03",
    ], 45)
    new_mm_focus = assemble_at(0x4ba2de, [
        "push 1",
        "pop ecx",
        "cmp eax, 0xb",
        "je .mmf_set",
        "inc ecx",
        "cmp eax, 0x9",
        "je .mmf_set",
        "inc ecx",
        "cmp eax, 0xa",
        "je .mmf_set",
        "inc ecx",
        "cmp eax, 0xf",
        "je .mmf_set",
        "xor ecx, ecx",
        ".mmf_set:",
        "mov DWORD PTR [esi+0x10], ecx",
        "jmp 0x4ba30e",
    ], 45).ljust(45, b"\x90")
    expect_replace(buf, 0x4ba2de, orig_mm_focus, new_mm_focus)

    expect_replace(buf, 0x4a17e1, b"\x83\xe0\x07", b"\x83\xe0\x00")
    expect_replace(buf, 0x4a18f5, b"\x83\xe2\x07", b"\x83\xe2\x00")
    expect_replace(buf, 0x4a192a, b"\x83\xe0\x07", b"\x83\xe0\x00")
    expect_replace(buf, 0x4a1977, b"\x83\xe0\x07", b"\x83\xe0\x00")
    expect_replace(buf, 0x4a1999, b"\x83\xe2\x07", b"\x83\xe2\x00")
    expect_replace(buf, 0x4a19ae, b"\x6a\x09", b"\x6a\x02")
    expect_replace(buf, 0x4a1a5d, b"\x6a\x09", b"\x6a\x02")


def apply_pause_options_menu(
    buf: bytearray,
    syms_d: dict[str, int],
    include_camera_row: bool = True,
    include_time_row: bool = False,
    include_skip_row: bool = False,
) -> dict[str, tuple[int, int]]:
    """
    Extend PauseMenu Page 1 (`0x58cf44`, `[esi+0x44] == 1`) with ui scale (5),
    auto scale (6), optional camera rows (7-8), optional time format row (9),
    optional skip intro row (10), and shifted back (7, 9, 10, or 11).
    """
    if include_skip_row and not (include_time_row and include_camera_row):
        raise ValueError("skip intro row requires the time format and camera rows")
    if include_time_row and not include_camera_row:
        raise ValueError("time format row requires the camera rows")
    if include_skip_row:
        adj_max, back_idx, row_cnt = 10, 11, 12
        back_div_y, back_y, bot_div_y = 0x1e0, 0x1f7, 0x201
    elif include_time_row:
        adj_max, back_idx, row_cnt = 9, 10, 11
        back_div_y, back_y, bot_div_y = 0x1bf, 0x1d6, 0x1e0
    elif include_camera_row:
        adj_max, back_idx, row_cnt = 8, 9, 10
        back_div_y, back_y, bot_div_y = 0x19e, 0x1b5, 0x1bf
    else:
        adj_max, back_idx, row_cnt = 6, 7, 8
        back_div_y, back_y, bot_div_y = 0x15c, 0x173, 0x17d
    bg_h = 0x20 + row_cnt * 0x21

    # Bounds, mouse hit-test, and panel height in PauseMenu (`0x4acbb0..0x4ad528`)
    expect_replace(buf, 0x4acbba, assemble_at(0x4acbba, ["cmp eax, 4"]), assemble_at(0x4acbba, [f"cmp eax, {adj_max}"]))
    expect_replace(buf, 0x4acc6f, assemble_at(0x4acc6f, ["mov DWORD PTR [esi+0x50], 5"]), assemble_at(0x4acc6f, [f"mov DWORD PTR [esi+0x50], {back_idx}"]))
    expect_replace(buf, 0x4accba, assemble_at(0x4accba, ["cmp DWORD PTR [esi+0x50], 6"]), assemble_at(0x4accba, [f"cmp DWORD PTR [esi+0x50], {row_cnt}"]))
    expect_replace(buf, 0x4acfe1, assemble_at(0x4acfe1, ["cmp eax, 6"]), assemble_at(0x4acfe1, [f"cmp eax, {row_cnt}"]))
    expect_replace(buf, 0x4ad523, assemble_at(0x4ad523, ["push 0xe6"]), assemble_at(0x4ad523, [f"push {hex(bg_h)}"]))

    # Pause Page 1 action dispatcher (`0x4ac989..0x4ac99c`) -> PAUSE_DISP_VA (`save_load` tail)
    # Pause action prologue pushes ebx then esi; custom actions end at 0x484d75 (`pop esi; ret`),
    # so pop+restore ebx, keep saved esi pushed for 0x484d75, and dispatch rows 5..8 via JTBL_9.
    disp_lines = [
        "mov eax, DWORD PTR [esi+0x50]",
        f"cmp eax, {back_idx}",
        "ja 0x4acb11",
        "je 0x4ac99c",
        "cmp eax, 5",
        "jb pp1_orig",
        "pop ecx",
        "pop ebx",
        "push ecx",
        f"jmp DWORD PTR [eax*4 + {hex(JTBL_9)}]",
        "pp1_orig:",
        "jmp DWORD PTR [eax*4 + 0x4acb14]",
    ]
    disp_cap = 0x4080c0 - PAUSE_DISP_VA
    disp_code = assemble_at(PAUSE_DISP_VA, disp_lines, disp_cap)
    expect_replace(buf, PAUSE_DISP_VA, b"\xcc" * disp_cap, disp_code.ljust(disp_cap, b"\xcc"))

    orig_dsp = assemble_at(0x4ac989, [
        "mov eax, DWORD PTR [esi+0x50]",
        "cmp eax, 5",
        "ja 0x4acb11",
        "jmp DWORD PTR [eax*4 + 0x4acb14]",
    ])
    expect_replace(buf, 0x4ac989, orig_dsp, assemble_at(0x4ac989, [f"jmp {hex(PAUSE_DISP_VA)}"], 5).ljust(len(orig_dsp), b"\xcc"))

    # pauseDrawText(eax = wstr*, edx = x_off, ecx = y_off) in Block D free tail (`0x484e38..0x484e5f`)
    ptext_cap = BLOCK_D_END - PAUSE_DRAW_TEXT_VA
    ptext_code = assemble_at(PAUSE_DRAW_TEXT_VA, [
        "add edx, DWORD PTR [ebp-0x48]",
        "add ecx, DWORD PTR [ebp-0x44]",
        "add ecx, esi",
        "push 0x44",
        "push ebx",
        "push ebx",
        "push 0x2000",
        "push 0x2000",
        "push ecx",
        "push edx",
        "push eax",
        "call 0x4a6c80",
        "add esp, 0x20",
        "ret",
    ], ptext_cap)
    expect_replace(buf, PAUSE_DRAW_TEXT_VA, b"\xcc" * ptext_cap, ptext_code.ljust(ptext_cap, b"\xcc"))

    # Replace back-row block at 0x4ade56..0x4adf53 (253B) inside PauseMenu::render (preserves ebx=0, esi, ebp)
    pause_cap = PAUSE_RENDER_END_VA - PAUSE_RENDER_VA
    pause_render, pause_syms = link_block_with_symbols(PAUSE_RENDER_VA, [
        ".globl pauseDrawScaleAndAuto, pauseDrawBack, pauseDrawLoc",
        "call pauseDrawScaleAndAuto",
        "call pauseDrawBack",  # fixed VA 0x4ade5b (PAUSE_CAM_DRAW_HOOK_VA)
        "mov ecx, DWORD PTR [ebp-0x50]",
        "jmp 0x4adf59",

        "pauseDrawBack:",
        f"mov edx, {hex(back_div_y)}",
        f"call {hex(PAUSE_DRAW_DIV_VA)}",
        "xor eax, eax",
        "mov edx, 0x158",
        f"mov ecx, {hex(back_y)}",
        "call pauseDrawLoc",
        f"mov edx, {hex(bot_div_y)}",
        f"jmp {hex(PAUSE_DRAW_DIV_VA)}",

        "pauseDrawScaleAndAuto:",
        "mov DWORD PTR ds:0x5f4268, 0xff3b3b3b",
        "mov edx, 0x11a",
        f"call {hex(PAUSE_DRAW_DIV_VA)}",
        f"mov eax, {hex(WSTR_UI_SCALE_LBL)}",
        "mov edx, 0x158",
        "mov ecx, 0x131",
        f"call {hex(PAUSE_DRAW_TEXT_VA)}",
        "sub esp, 0x18",
        "mov ecx, esp",
        f"call {hex(FORMAT_UISCALE_WSTR_VA)}",
        "mov edx, 0x23d",
        "mov ecx, 0x131",
        f"call {hex(PAUSE_DRAW_TEXT_VA)}",
        "add esp, 0x18",

        "mov edx, 0x13b",
        f"call {hex(PAUSE_DRAW_DIV_VA)}",
        f"mov eax, {hex(WSTR_AUTO_LBL)}",
        "mov edx, 0x158",
        "mov ecx, 0x152",
        f"call {hex(PAUSE_DRAW_TEXT_VA)}",
        "mov eax, 0xc",
        f"cmp DWORD PTR ds:{hex(G_AUTO_SCALE)}, ebx",
        "je pda_loc",
        "mov al, 0xb",
        "pda_loc:",
        "mov edx, 0x23d",
        "mov ecx, 0x152",
        "jmp pauseDrawLoc",

        "pauseDrawLoc:",
        "push ecx",
        "push edx",
        "sub esp, 0x18",
        "mov edx, esp",
        "push eax",
        "push edx",
        "call 0x49fd70",
        "add esp, 8",
        "mov edx, DWORD PTR [esp+0x18]",
        "mov ecx, DWORD PTR [esp+0x1c]",
        f"call {hex(PAUSE_DRAW_TEXT_VA)}",
        "cmp DWORD PTR [esp+20], 8",
        "jb pdl_pop",
        "push DWORD PTR [esp]",
        "call 0x4ba62a",
        "add esp, 4",
        "pdl_pop:",
        "add esp, 0x20",
        "ret",
    ], pause_cap)
    assert pause_syms["pauseDrawLoc"] == PAUSE_DRAW_LOC_VA

    head_expect = assemble_at(PAUSE_RENDER_VA, ["mov ecx, DWORD PTR [ebp-0x44]", "mov eax, DWORD PTR [ebp-0x48]"])
    tail_expect = assemble_at(PAUSE_RENDER_END_VA, ["mov ecx, DWORD PTR [ebp-0x50]", "add esp, 0x14"])
    expect_replace(buf, PAUSE_RENDER_VA, head_expect, head_expect)
    expect_replace(buf, PAUSE_RENDER_END_VA, tail_expect, tail_expect)
    put_bytes(buf, PAUSE_RENDER_VA, pause_render.ljust(pause_cap, b"\xcc"), pause_cap)

    return {
        "pause_disp": (len(disp_code), disp_cap),
        "pause_text": (len(ptext_code), ptext_cap),
        "pause_render": (len(pause_render), pause_cap),
    }


def apply_ui_scale_menu_and_ini(
    buf: bytearray,
    include_camera_row: bool = True,
    include_time_row: bool = False,
    include_skip_row: bool = False,
) -> dict[str, tuple[int, int]]:
    """
    Install ui_scale/ui_auto_scale INI parsing, local_savedata.ttsav persistence,
    720p-baseline vs manual virtual height calculation, and OptionsMenu rows 5 & 6.
    """
    auto_y = options_row_text_y(6, include_camera_row, include_time_row, include_skip_row)
    scale_y = options_row_text_y(5, include_camera_row, include_time_row, include_skip_row)
    min_virt_h = 0x200 if include_camera_row else 0x1c2
    put_bytes(buf, BLOCK_D_START, b"\xcc" * (BLOCK_D_END - BLOCK_D_START))
    block_d, syms_d = link_block_with_symbols(BLOCK_D_START, [
        ".globl clampUiScale, getEffectiveUiScale, readIniSettings, uiScaleAction, autoScaleAction",

        "clampUiScale:",
        "ucomiss xmm0, xmm0",
        "jp cus_bad",
        "push 0x3dcccccd", # 0.1f
        "ucomiss xmm0, DWORD PTR [esp]",
        "jae cus_hi",
        "movss xmm0, DWORD PTR [esp]",
        "cus_hi:",
        "mov DWORD PTR [esp], 0x41200000", # 10.0f
        "ucomiss xmm0, DWORD PTR [esp]",
        "jbe cus_ok",
        "movss xmm0, DWORD PTR [esp]",
        "cus_ok:",
        "add esp, 4",
        "mov al, 1",
        "ret",
        "cus_bad:",
        "xor al, al",
        "ret",

        "getEffectiveUiScale:",
        f"mov eax, DWORD PTR ds:{hex(G_UI_SCALE)}",
        "test eax, eax",
        "jne ges_chk",
        f"mov eax, DWORD PTR ds:{hex(G_INI_UI_SCALE)}",
        "test eax, eax",
        "jne ges_chk",
        "mov eax, 0x3f800000", # 1.0f
        "ges_chk:",
        "push eax",
        "movss xmm0, DWORD PTR [esp]",
        "call clampUiScale",
        "test al, al",
        "jne ges_ok",
        "mov DWORD PTR [esp], 0x3f800000",
        "jmp ges_fld",
        "ges_ok:",
        "movss DWORD PTR [esp], xmm0",
        "ges_fld:",
        "fld DWORD PTR [esp]",
        "add esp, 4",
        "ret",

        "queryIniKey:",
        "push eax",
        "lea ecx, [ebp-0x100]",
        "call 0x4458c0",
        "lea ecx, [ebx+0x1c]",
        "call 0x4014e0",
        "lea eax, [ebx+0x1c]",
        "push eax",
        "lea eax, [ebx+0x38]",
        "push eax",
        "push ebx",
        "lea ecx, [ebp-0x110]",
        "call 0x4f5d70",
        "ret",

        "freeIniRetAndKey:",
        "mov ecx, ebx",
        "call 0x51e620",
        "lea ecx, [ebx+0x1c]",
        "jmp 0x51e620",

        "readIniSettings:",
        "call 0x4458c0",
        "push ebx",
        "sub esp, 0x5c",
        "mov ebx, esp",
        f"mov DWORD PTR ds:{hex(G_INI_UI_SCALE)}, 0",
        f"mov BYTE PTR ds:{hex(G_INI_AUTO_SCALE)}, 0xff",
        "lea ecx, [ebx+0x38]",
        "push 0x585a2c", # "graphics"
        "call 0x4014e0",
        f"mov eax, {hex(STR_INI_SCALE_KEY)}",
        "call queryIniKey",
        "cmp DWORD PTR [ebx+0x10], 0",
        "je ris_free1",
        "lea eax, [ebp-0x100]",
        "push eax",
        "push ebx",
        "call 0x4f4fd0",
        "add esp, 8",
        "fstp DWORD PTR [ebx+0x54]",
        "cmp BYTE PTR [ebp-0xf8], 0",
        "jne ris_free1",
        "movss xmm0, DWORD PTR [ebx+0x54]",
        "call clampUiScale",
        "test al, al",
        "je ris_free1",
        f"movss DWORD PTR ds:{hex(G_INI_UI_SCALE)}, xmm0",
        "ris_free1:",
        "call freeIniRetAndKey",
        f"mov eax, {hex(STR_INI_AUTO_KEY)}",
        "call queryIniKey",
        "cmp DWORD PTR [ebx+0x10], 0",
        "je ris_free2",
        "lea eax, [ebp-0x100]",
        "push eax",
        "push ebx",
        "call 0x4f4980",
        "add esp, 8",
        "cmp BYTE PTR [ebp-0xf8], 0",
        "jne ris_free2",
        "movzx eax, al",
        f"mov BYTE PTR ds:{hex(G_INI_AUTO_SCALE)}, al",
        f"mov DWORD PTR ds:{hex(G_AUTO_SCALE)}, eax",
        "ris_free2:",
        "call freeIniRetAndKey",
        "lea ecx, [ebx+0x38]",
        "call 0x51e620",
        "add esp, 0x5c",
        "pop ebx",
        "lea ecx, [ebp-0x100]",
        "jmp 0x4458c0",

        "applyOptionChange:",
        "mov ecx, DWORD PTR ds:0x5f9080",
        "push 0xc",
        "call 0x4815f0",
        "mov ecx, DWORD PTR ds:0x5f9080",
        "add ecx, 0x298",
        "call 0x4927d0",
        "mov ecx, DWORD PTR ds:0x5f9080",
        "call 0x408320",
        "pop esi",
        "ret",

        "uiScaleAction:",
        "call getEffectiveUiScale",
        "sub esp, 4",
        "fstp DWORD PTR [esp]",
        "movss xmm0, DWORD PTR [esp]",
        "mov edx, DWORD PTR ds:0x5f9080",
        "cmp BYTE PTR [edx+0xb3], 0",
        "jne usa_left",
        "mov DWORD PTR [esp], 0x3a83126f", # 0.001f
        "addss xmm0, DWORD PTR [esp]",
        "xor ecx, ecx",
        "usa_fwd:",
        f"movss xmm1, DWORD PTR [ecx*4 + {hex(PRESET_TBL)}]",
        "ucomiss xmm1, xmm0",
        "ja usa_pick",
        "inc ecx",
        f"cmp ecx, {PRESET_COUNT}",
        "jb usa_fwd",
        "xor ecx, ecx",
        "jmp usa_pick",
        "usa_left:",
        "mov DWORD PTR [esp], 0x3a83126f",
        "subss xmm0, DWORD PTR [esp]",
        f"mov ecx, {PRESET_COUNT - 1}",
        "usa_back:",
        f"movss xmm1, DWORD PTR [ecx*4 + {hex(PRESET_TBL)}]",
        "ucomiss xmm0, xmm1",
        "ja usa_pick",
        "dec ecx",
        "jns usa_back",
        f"mov ecx, {PRESET_COUNT - 1}",
        "usa_pick:",
        f"mov eax, DWORD PTR [ecx*4 + {hex(PRESET_TBL)}]",
        f"mov DWORD PTR ds:{hex(G_UI_SCALE)}, eax",
        "add esp, 4",
        "jmp applyOptionChange",

        "autoScaleAction:",
        f"xor DWORD PTR ds:{hex(G_AUTO_SCALE)}, 1",
        "jmp applyOptionChange",
    ], BLOCK_D_END - BLOCK_D_START)
    put_bytes(buf, BLOCK_D_START, block_d, BLOCK_D_END - BLOCK_D_START)

    put_bytes(buf, BLOCK_E_START, b"\xcc" * (BLOCK_E_END - BLOCK_E_START))
    block_e, syms_e = link_block_with_symbols(BLOCK_E_START, [
        ".globl loadScaleTtsav, saveScaleTtsav, setRowColor, drawTextAt, drawAutoScaleRow",

        "readTtsavNum:",
        "push ebx",
        "mov ebx, eax",
        "push ebx",
        "mov ecx, esi",
        "call 0x51c660",
        "test al, al",
        "je rtn_no",
        "push 3",
        "push ebx",
        "mov ecx, esi",
        "call 0x51c540",
        "mov ecx, eax",
        "call 0x51b620",
        "test al, al",
        "je rtn_no",
        "push ebx",
        "mov ecx, esi",
        "call 0x51c540",
        "mov ecx, eax",
        "call 0x51b3f0",
        "mov al, 1",
        "pop ebx",
        "ret",
        "rtn_no:",
        "xor al, al",
        "pop ebx",
        "ret",

        "loadScaleTtsav:",
        "sub esp, 4",
        f"cmp DWORD PTR ds:{hex(G_UI_SCALE)}, 0",
        "jne lst_s1",
        f"mov DWORD PTR ds:{hex(G_UI_SCALE)}, 0x3f800000",
        "lst_s1:",
        f"mov eax, {hex(STR_SAV_SCALE_KEY)}",
        "call readTtsavNum",
        "test al, al",
        "je lst_s_ini",
        "fstp DWORD PTR [esp]",
        "movss xmm0, DWORD PTR [esp]",
        f"call {hex(syms_d['clampUiScale'])}",
        "test al, al",
        "je lst_s_ini",
        f"movss DWORD PTR ds:{hex(G_UI_SCALE)}, xmm0",
        "lst_s_ini:",
        f"mov DWORD PTR ds:{hex(G_LAST_INI_SCALE)}, 0",
        f"mov eax, {hex(STR_SAV_SCALE_INI)}",
        "call readTtsavNum",
        "test al, al",
        "je lst_chk_s",
        f"fstp DWORD PTR ds:{hex(G_LAST_INI_SCALE)}",
        "lst_chk_s:",
        f"mov eax, DWORD PTR ds:{hex(G_INI_UI_SCALE)}",
        "test eax, eax",
        "jne lst_has_s",
        f"mov DWORD PTR ds:{hex(G_LAST_INI_SCALE)}, 0",
        "jmp lst_auto",
        "lst_has_s:",
        f"movss xmm0, DWORD PTR ds:{hex(G_INI_UI_SCALE)}",
        "movss xmm1, xmm0",
        f"subss xmm1, DWORD PTR ds:{hex(G_LAST_INI_SCALE)}",
        "movss DWORD PTR [esp], xmm1",
        "and DWORD PTR [esp], 0x7fffffff",
        "movss xmm1, DWORD PTR [esp]",
        "mov DWORD PTR [esp], 0x3a03126f", # 0.0005f
        "ucomiss xmm1, DWORD PTR [esp]",
        "jbe lst_auto",
        f"movss DWORD PTR ds:{hex(G_UI_SCALE)}, xmm0",
        f"movss DWORD PTR ds:{hex(G_LAST_INI_SCALE)}, xmm0",
        "lst_auto:",
        f"mov DWORD PTR ds:{hex(G_AUTO_SCALE)}, 1",
        f"mov eax, {hex(STR_SAV_AUTO_KEY)}",
        "call readTtsavNum",
        "test al, al",
        "je lst_a_ini",
        "fistp DWORD PTR [esp]",
        "cmp DWORD PTR [esp], 0",
        "setne al",
        "movzx eax, al",
        f"mov DWORD PTR ds:{hex(G_AUTO_SCALE)}, eax",
        "lst_a_ini:",
        f"mov BYTE PTR ds:{hex(G_LAST_INI_AUTO)}, 0xff",
        f"mov eax, {hex(STR_SAV_AUTO_INI)}",
        "call readTtsavNum",
        "test al, al",
        "je lst_chk_a",
        "fistp DWORD PTR [esp]",
        "mov eax, DWORD PTR [esp]",
        "test eax, eax",
        "jl lst_chk_a",
        "cmp eax, 1",
        "jg lst_chk_a",
        f"mov BYTE PTR ds:{hex(G_LAST_INI_AUTO)}, al",
        "lst_chk_a:",
        f"movsx eax, BYTE PTR ds:{hex(G_INI_AUTO_SCALE)}",
        "test eax, eax",
        "jge lst_has_a",
        f"mov BYTE PTR ds:{hex(G_LAST_INI_AUTO)}, 0xff",
        "add esp, 4",
        "ret",
        "lst_has_a:",
        f"cmp al, BYTE PTR ds:{hex(G_LAST_INI_AUTO)}",
        "je lst_done",
        f"mov DWORD PTR ds:{hex(G_AUTO_SCALE)}, eax",
        f"mov BYTE PTR ds:{hex(G_LAST_INI_AUTO)}, al",
        "lst_done:",
        "add esp, 4",
        "ret",

        "saveOneDouble:",
        "push ebx",
        "mov ebx, eax",
        "lea ecx, [ebp-0x50]",
        "sub esp, 8",
        "fstp QWORD PTR [esp]",
        "call 0x51b290",
        "push eax",
        "push ebx",
        "mov ecx, edi",
        "call 0x51db80",
        "mov ecx, eax",
        "call 0x51d1b0",
        "lea ecx, [ebp-0x50]",
        "call 0x51c800",
        "pop ebx",
        "ret",

        "saveScaleTtsav:",
        f"call {hex(syms_d['getEffectiveUiScale'])}",
        f"mov eax, {hex(STR_SAV_SCALE_KEY)}",
        "call saveOneDouble",
        f"fld DWORD PTR ds:{hex(G_LAST_INI_SCALE)}",
        f"mov eax, {hex(STR_SAV_SCALE_INI)}",
        "call saveOneDouble",
        f"fild DWORD PTR ds:{hex(G_AUTO_SCALE)}",
        f"mov eax, {hex(STR_SAV_AUTO_KEY)}",
        "call saveOneDouble",
        f"movsx eax, BYTE PTR ds:{hex(G_LAST_INI_AUTO)}",
        "push eax",
        "fild DWORD PTR [esp]",
        "add esp, 4",
        f"mov eax, {hex(STR_SAV_AUTO_INI)}",
        "jmp saveOneDouble",

        "setRowColor:",
        "mov edx, DWORD PTR [ebp-0x5c]",
        "cmp DWORD PTR [edx+0x10], eax",
        "je src_sel",
        "cmp BYTE PTR [ebp-0x41], 0",
        "jne src_hov",
        "xor al, al",
        "ret",
        "src_hov:",
        "mov eax, DWORD PTR [ebp-0x64]",
        "jmp src_col",
        "src_sel:",
        "mov eax, DWORD PTR [ebp-0x50]",
        "src_col:",
        "mov ecx, eax",
        "mov edx, eax",
        "sar ecx, 0x10",
        "sar edx, 0x8",
        "mov BYTE PTR [ebp-0x40], cl",
        "mov BYTE PTR [ebp-0x3f], dl",
        "mov BYTE PTR [ebp-0x3e], al",
        "mov BYTE PTR [ebp-0x3d], 0xff",
        "mov eax, DWORD PTR [ebp-0x40]",
        "mov DWORD PTR ds:0x5f4268, eax",
        "mov al, 1",
        "ret",

        "drawTextAt:",
        "add ecx, DWORD PTR [ebp-0x48]",
        "add edx, DWORD PTR [ebp-0x4c]",
        "push 0x14",
        "push edi",
        "push 1",
        "push 0x2000",
        "push 0x2000",
        "push ecx",
        "push edx",
        "push eax",
        "call 0x4a6c80",
        "add esp, 0x20",
        "ret",

        "drawAutoScaleRow:",
        "mov eax, 6",
        "call setRowColor",
        "test al, al",
        "je das_done",
        f"mov eax, {hex(WSTR_AUTO_LBL)}",
        f"mov ecx, {hex(auto_y)}",
        "mov edx, 0x1e",
        "call drawTextAt",
        "sub esp, 0x18",
        "mov edx, 0xc",
        f"cmp DWORD PTR ds:{hex(G_AUTO_SCALE)}, 0",
        "je das_loc",
        "mov edx, 0xb",
        "das_loc:",
        "mov eax, esp",
        "push edx",
        "push eax",
        "call 0x49fd70",
        "add esp, 8",
        f"mov ecx, {hex(auto_y)}",
        "mov edx, 0xe6",
        "call drawTextAt",
        "cmp DWORD PTR [esp+20], 8",
        "jb das_pop",
        "push DWORD PTR [esp]",
        "call 0x4ba62a",
        "add esp, 4",
        "das_pop:",
        "add esp, 0x18",
        "das_done:",
        "ret",
    ], BLOCK_E_END - BLOCK_E_START)
    put_bytes(buf, BLOCK_E_START, block_e, BLOCK_E_END - BLOCK_E_START)

    virt_code = assemble_at(0x407eb0, [
        "mov eax, DWORD PTR [ecx+0xbc]",
        "test eax, eax",
        "jle h_done",
        "push eax",
        f"call {hex(syms_d['getEffectiveUiScale'])}",
        "push eax",
        "fstp DWORD PTR [esp]",
        f"cmp DWORD PTR ds:{hex(G_AUTO_SCALE)}, 0",
        "je h_man",
        "mov DWORD PTR [esp+4], 0x44340000", # 720.0f baseline
        "fld DWORD PTR [esp+4]",
        "jmp h_div",
        "h_man:",
        "fild DWORD PTR [esp+4]",
        "h_div:",
        "fdiv DWORD PTR [esp]",
        "fistp DWORD PTR [esp]",
        "pop eax",
        "pop edx",
        f"cmp eax, {min_virt_h}", # min virtH keeps back row clear of footer
        "jge h_done",
        f"mov eax, {min_virt_h}",
        "h_done:",
        "ret",
    ], 80)
    put_bytes(buf, 0x407eb0, virt_code, 80)

    if include_camera_row:
        jtbl = [
            0x4b2706, 0x4b2718, 0x4b272a, 0x4b273c, 0x4b274e,
            syms_d["uiScaleAction"], syms_d["autoScaleAction"],
            0x484e2c,  # entry 7: overwritten by camera_angle with camDistAction
            0x484e2c,  # entry 8: overwritten by camera_angle with camFovAction
            0x4b2778,
        ]
    else:
        jtbl = [
            0x4b2706, 0x4b2718, 0x4b272a, 0x4b273c, 0x4b274e,
            syms_d["uiScaleAction"], syms_d["autoScaleAction"],
            0x4b2778,
        ]
    for i, target in enumerate(jtbl):
        put_u32(buf, JTBL_9 + i * 4, target)

    put_bytes(buf, 0x476fea, assemble_at(0x476fea, [f"call {hex(syms_d['readIniSettings'])}"]), 5)

    put_bytes(buf, 0x408060, assemble_at(0x408060, [
        f"call {hex(syms_e['loadScaleTtsav'])}",
        "lea ecx, [edi-0x298]",
        "call 0x408320",
        "mov ecx, esi",
        "jmp 0x51c660",
    ]), 96)

    put_bytes(buf, 0x4080c0, assemble_at(0x4080c0, [
        "lea ecx, [ebp-0x50]",
        "call 0x51c800",
        f"jmp {hex(syms_e['saveScaleTtsav'])}",
    ]), 96)

    # 0x408120..0x408238 (280B): shared formatUiScaleWstr + fmtFloatWstr (0x408125) + drawRow5Main + pauseDrawDiv
    ui_scale_row_cap = 0x408238 - FORMAT_UISCALE_WSTR_VA
    put_bytes(buf, FORMAT_UISCALE_WSTR_VA, b"\xcc" * ui_scale_row_cap)

    fmt_wstr = assemble_at(FORMAT_UISCALE_WSTR_VA, [
        f"call {hex(syms_d['getEffectiveUiScale'])}",
        "fmtFloatWstr:",  # fixed VA 0x408125 (FORMAT_FLOAT_WSTR_VA): formats st(0) in [0.0, 90.0] into wstring at ecx
        "xor eax, eax",
        "mov DWORD PTR [ecx], eax",
        "mov DWORD PTR [ecx+4], eax",
        "mov DWORD PTR [ecx+8], eax",
        "mov DWORD PTR [ecx+12], eax",
        "mov DWORD PTR [ecx+20], 7",
        "push 0x42c80000",  # 100.0f
        "fmul DWORD PTR [esp]",
        "fistp DWORD PTR [esp]",
        "pop eax",
        "test eax, eax",
        "jge fsp_hi",
        "xor eax, eax",
        "fsp_hi:",
        "cmp eax, 9000",
        "jle fsp_div",
        "mov eax, 9000",
        "fsp_div:",
        "cdq",
        "push ecx",
        "mov ecx, 100",
        "idiv ecx",
        "pop ecx",
        "push esi",
        "xor esi, esi",
        "aam",
        "test ah, ah",
        "je fsp_one",
        "add ah, 0x30",
        "mov BYTE PTR [ecx], ah",
        "inc esi",
        "fsp_one:",
        "add al, 0x30",
        "mov BYTE PTR [ecx+esi*2], al",
        "inc esi",
        "test edx, edx",
        "je fsp_done",
        "mov BYTE PTR [ecx+esi*2], 0x2e",
        "inc esi",
        "mov eax, edx",
        "aam",
        "add ah, 0x30",
        "mov BYTE PTR [ecx+esi*2], ah",
        "inc esi",
        "test al, al",
        "je fsp_done",
        "add al, 0x30",
        "mov BYTE PTR [ecx+esi*2], al",
        "inc esi",
        "fsp_done:",
        "mov DWORD PTR [ecx+16], esi",
        "pop esi",
        "mov eax, ecx",
        "ret",
    ], MAIN_DRAW_ROW5_VA - FORMAT_UISCALE_WSTR_VA)
    put_bytes(buf, FORMAT_UISCALE_WSTR_VA, fmt_wstr, MAIN_DRAW_ROW5_VA - FORMAT_UISCALE_WSTR_VA)

    draw_row5 = assemble_at(MAIN_DRAW_ROW5_VA, [
        f"call {hex(syms_e['drawAutoScaleRow'])}",  # fixed VA MAIN_DRAW_AUTO_HOOK_VA (0x4081b8)
        "push esi",
        "mov eax, 5",
        f"call {hex(syms_e['setRowColor'])}",
        "test al, al",
        "je dus_done",
        f"mov eax, {hex(WSTR_UI_SCALE_LBL)}",
        f"mov ecx, {hex(scale_y)}",
        "mov edx, 0x1e",
        f"call {hex(syms_e['drawTextAt'])}",
        "sub esp, 0x18",
        "mov ecx, esp",
        f"call {hex(FORMAT_UISCALE_WSTR_VA)}",
        f"mov ecx, {hex(scale_y)}",
        "mov edx, 0xe6",
        f"call {hex(syms_e['drawTextAt'])}",
        "add esp, 0x18",
        "dus_done:",
        "pop esi",
        "ret",
    ], PAUSE_DRAW_DIV_VA - MAIN_DRAW_ROW5_VA)
    put_bytes(buf, MAIN_DRAW_ROW5_VA, draw_row5, PAUSE_DRAW_DIV_VA - MAIN_DRAW_ROW5_VA)

    pdiv_code = assemble_at(PAUSE_DRAW_DIV_VA, [
        "mov ecx, DWORD PTR [ebp-0x44]",
        "mov eax, DWORD PTR [ebp-0x48]",
        "add ecx, esi",
        "add ecx, edx",
        "add eax, 0x140",
        "push 0x5f93a4",
        "push 1",
        "push 0x140",
        "push ecx",
        "push eax",
        "call 0x488670",
        "add esp, 0x14",
        "ret",
    ], 0x408238 - PAUSE_DRAW_DIV_VA)
    put_bytes(buf, PAUSE_DRAW_DIV_VA, pdiv_code, 0x408238 - PAUSE_DRAW_DIV_VA)

    apply_options_menu_layout(
        buf,
        include_camera_row=include_camera_row,
        include_time_row=include_time_row,
        include_skip_row=include_skip_row,
    )
    stats = {
        "virt_h": (len(virt_code), 80),
        "block_d": (len(block_d), BLOCK_D_END - BLOCK_D_START),
        "block_e": (len(block_e), BLOCK_E_END - BLOCK_E_START),
        "fmt_wstr": (len(fmt_wstr), MAIN_DRAW_ROW5_VA - FORMAT_UISCALE_WSTR_VA),
        "draw_row5": (len(draw_row5), PAUSE_DRAW_DIV_VA - MAIN_DRAW_ROW5_VA),
        "pause_div": (len(pdiv_code), 0x408238 - PAUSE_DRAW_DIV_VA),
    }
    stats.update(
        apply_pause_options_menu(
            buf,
            syms_d,
            include_camera_row=include_camera_row,
            include_time_row=include_time_row,
            include_skip_row=include_skip_row,
        )
    )
    return stats
