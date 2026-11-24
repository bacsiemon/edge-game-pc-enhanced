"""Partial 60/unlimited-FPS UI smoothing for EDGE PC.

Smooths visible discrete 30 Hz UI elements using ScreenMgr::interpolate alpha
while leaving non-interpolated menus, layout math, and 30 Hz physics intact:
  1. Pause / menu fades:
     - ScreenMgr::[+0x44] / [+0x304] post-process fades (SeparationScale,
       ExtraWhiteScale)
     - Pause camera zoom ([ScreenMgr+0x8c]) and pause target pan ([WorldScreen+0x1c..0x24])
     - Viewport scale zoom ([ScreenMgr+0x34] and derived [+0x2c/+0x30])
     - PauseMenu slide-in/out (+0x4/+0x8) & Page 0/1 horizontal slide (+0x4c)
     - PauseConfirm dialog slide & fade (+0x4/+0x8)
     - EndLevel screen white flash & fade (+0x3c/+0x40)
     - Submenu fade-in / fade-out percentages across all 8 menu screens
  2. WorldScreen HUD flash, rainbow pulse, and Toast notifications:
     - Full-screen flash ([WorldScreen+0x211c]) smooth 3-tick decay + rainbow blend
     - Corner triangle rainbow & alpha wave ([WorldScreen+0x4])
     - Toast message fade-in / fade-out ([WorldScreen+0x2158+0x2b])
  3. WorldScreen Minimap:
     - Open/close opacity transition ([WorldScreen+0x2108]) rebuilding the
       minimap vertex buffer on each distinct interpolated integer opacity
"""

import struct

from patches.common import assemble_at, expect_replace, link_block_with_symbols, put_bytes, va2off

# Unused zero-reference functions in .text (verified via find_free_slots.py --check-refs)
BLOCK_I_START = 0x453FA0
BLOCK_I_END   = 0x4542CD  # 813 B: core timing, float trackers, ScreenMgr savePrev/interpolate

BLOCK_J_START = 0x4FCA10
BLOCK_J_END   = 0x4FCC34  # 548 B: submenu / PauseMenu / PauseConfirm / EndLevel helpers

BLOCK_K_START = 0x44DF40
BLOCK_K_END   = 0x44E154  # 532 B: WorldScreen HUD flash & rainbow helpers

BLOCK_L_START = 0x412F60
BLOCK_L_END   = 0x4130DE  # 382 B: PauseConfirm, EndLevel & PauseMenu X helpers

BLOCK_M_START = 0x43B060
BLOCK_M_END   = 0x43B1B8  # 344 B: Minimap draw/rebuild & PauseMenu page skip guards

BLOCK_N_START = 0x45CC80
BLOCK_N_END   = 0x45CD9D  # 285 B: Toast alpha smooth, Pause Page2 X/Skip helpers

BLOCK_O_START = 0x541B10
BLOCK_O_END   = 0x541C2C  # 284 B: Submenu vertical bar slide Y helper

BLOCK_P_START = 0x5347D0
BLOCK_P_END   = 0x5348EC  # 284 B: MenuScreen 3D model/scroll savePrev & interpolate

BLOCK_Q_START = 0x4F79C0
BLOCK_Q_END   = 0x4F7AD6  # 278 B: ModelNode timer interp, marquee & LevelSelect scroll

BLOCK_R_START = 0x51BDC0
BLOCK_R_END   = 0x51BED5  # 277 B: MenuScreen 3D model / marquee savePrev

# Free tail inside existing zero-initialized .bss page (< 0x5fb000)
BSS_G_UI_ALPHA        = 0x5FA824
BSS_G_JUST_TICKED     = 0x5FA828
BSS_TRK_PP304_4C      = 0x5FA82C  # 16B: {ptr, prev, tgt, last}
BSS_TRK_PP304_68      = 0x5FA83C
BSS_TRK_SM_8C         = 0x5FA84C
BSS_TRK_SM_34         = 0x5FA85C
BSS_TRK_WS_CAMX       = 0x5FA86C
BSS_TRK_WS_CAMY       = 0x5FA87C
BSS_TRK_WS_CAMZ       = 0x5FA88C
BSS_G_SUB_PTR         = 0x5FA89C
BSS_G_SUB_PREV4       = 0x5FA8A0
BSS_G_SUB_PREV8       = 0x5FA8A4
BSS_G_SUB_PREV4C      = 0x5FA8A8
BSS_G_END_PREV3C      = 0x5FA8AC
BSS_G_END_PREV40      = 0x5FA8B0
BSS_G_WS_PTR          = 0x5FA8B4
BSS_G_WS_PREV4        = 0x5FA8B8
BSS_G_WS_PREV2108     = 0x5FA8BC
BSS_G_WS_TGT2108      = 0x5FA8C0
BSS_G_WS_BUILT2108    = 0x5FA8C4
BSS_G_WS_MM_INTERP    = 0x5FA8C8
BSS_G_WS_PREV_TST2B   = 0x5FA8CC
BSS_G_WS_PREV_TST34   = 0x5FA8D0
BSS_G_RB_TMP          = 0x5FA8D4
BSS_G_MS_PREV_C       = 0x5FA8D8
BSS_G_SUB_PREV50      = 0x5FA8DC
BSS_G_MM0             = 0x5FA8E0  # 4 x 32B MenuScreen AnimatedModel trackers (..0x5FA960)

EXT_SYMS = {
    "G_UI_ALPHA":       BSS_G_UI_ALPHA,
    "G_JUST_TICKED":    BSS_G_JUST_TICKED,
    "TRK_PP304_4C":     BSS_TRK_PP304_4C,
    "TRK_PP304_68":     BSS_TRK_PP304_68,
    "TRK_SM_8C":        BSS_TRK_SM_8C,
    "TRK_SM_34":        BSS_TRK_SM_34,
    "TRK_WS_CAMX":      BSS_TRK_WS_CAMX,
    "TRK_WS_CAMY":      BSS_TRK_WS_CAMY,
    "TRK_WS_CAMZ":      BSS_TRK_WS_CAMZ,
    "G_SUB_PTR":        BSS_G_SUB_PTR,
    "G_SUB_PREV4":      BSS_G_SUB_PREV4,
    "G_SUB_PREV8":      BSS_G_SUB_PREV8,
    "G_SUB_PREV4C":     BSS_G_SUB_PREV4C,
    "G_END_PREV3C":     BSS_G_END_PREV3C,
    "G_END_PREV40":     BSS_G_END_PREV40,
    "G_WS_PTR":         BSS_G_WS_PTR,
    "G_WS_PREV4":       BSS_G_WS_PREV4,
    "G_WS_PREV2108":    BSS_G_WS_PREV2108,
    "G_WS_TGT2108":     BSS_G_WS_TGT2108,
    "G_WS_BUILT2108":   BSS_G_WS_BUILT2108,
    "G_WS_MM_INTERP":   BSS_G_WS_MM_INTERP,
    "G_WS_PREV_TST2B":  BSS_G_WS_PREV_TST2B,
    "G_WS_PREV_TST34":  BSS_G_WS_PREV_TST34,
    "G_RB_TMP":         BSS_G_RB_TMP,
    "G_MS_PREV_C":      BSS_G_MS_PREV_C,
    "G_SUB_PREV50":     BSS_G_SUB_PREV50,
    "G_MM0":            BSS_G_MM0,
    "PTR_SCREENMGR":    0x5F9080,
    "PTR_WORLDSCR":     0x5F9064,
    "PTR_MENUSCREEN":   0x5F91B4,
    "PTR_MSTACK":       0x5F87D0,
    "VT_MENUSCREEN":    0x58AF10,
    "VT_PAUSEMENU":     0x58CF44,
    "VT_ENDLEVEL":      0x58CE58,
    "VT_LEVELSELECT":   0x58D51C,
    "PALETTE_RAINBOW":  0x589BB8,
    "HUD_TRI_CONST":    0x586258,
    "TEXT_COL_BYTES":   0x5F88E0,
    "FN_COLOR_LERP":    0x488530,
    "FN_GAME_CAM_UPD":  0x47B8D0,
    "FN_MM_REBUILD":    0x47E620,
    "FN_MM_DRAW":       0x47A960,
    "FN_SET_BLEND":     0x405DE0,
    "FN_SPLINE_SET_T":  0x432390,
    "FN_MSTACK_PUSH":   0x41F750,
    "FN_MSTACK_SET":    0x41F4D0,
    "FN_MSTACK_POP":    0x41F610,
    "FN_FOOTER_Y":      0x496060,
}


def _check_unmodified(buf: bytearray, va: int, instrs: list[str], total_len: int | None = None) -> bytes:
    code = assemble_at(va, instrs)
    if total_len is not None:
        assert len(code) == total_len, f"Expected {total_len}B at {hex(va)}, got {len(code)}B"
    off = va2off(va)
    actual = bytes(buf[off : off + len(code)])
    assert actual == code, f"Unexpected original bytes at {hex(va)}"
    return code


def apply_ui_smooth_partial(buf: bytearray, out_syms: dict[str, int] | None = None) -> dict[str, tuple[int, int]]:
    """Install partial 60/unlimited-FPS interpolation for pause/menu fades, HUD flash, and minimap."""
    # Clear unused block regions cleanly first
    for s, e in [
        (BLOCK_I_START, BLOCK_I_END),
        (BLOCK_J_START, BLOCK_J_END),
        (BLOCK_K_START, BLOCK_K_END),
        (BLOCK_L_START, BLOCK_L_END),
        (BLOCK_M_START, BLOCK_M_END),
        (BLOCK_N_START, BLOCK_N_END),
        (BLOCK_O_START, BLOCK_O_END),
        (BLOCK_P_START, BLOCK_P_END),
        (BLOCK_Q_START, BLOCK_Q_END),
        (BLOCK_R_START, BLOCK_R_END),
    ]:
        put_bytes(buf, s, b"\xcc" * (e - s))

    # ------------------------------------------------------------------
    # 0a. Block R (0x51BDC0..0x51BED5, 277B): MenuScreen savePrev state
    # ------------------------------------------------------------------
    asm_r = [
        ".globl onSavePrev_MS",
        "onSavePrev_MS:",
        "push ebx",
        "push esi",
        "mov eax, DWORD PTR [PTR_MENUSCREEN]",
        "test eax, eax",
        "je .spm_clear_all",
        "mov edx, DWORD PTR [eax+0xc]",
        "mov DWORD PTR [G_MS_PREV_C], edx",
        "mov edx, DWORD PTR [G_SUB_PTR]",
        "test edx, edx",
        "je .spm_models",
        "cmp DWORD PTR [edx], VT_LEVELSELECT",
        "jne .spm_models",
        "mov edx, DWORD PTR [edx+0x50]",
        "mov DWORD PTR [G_SUB_PREV50], edx",
        ".spm_models:",
        "lea ecx, [eax+0x120]",
        "mov ebx, G_MM0",
        "mov esi, 4",
        ".spm_loop:",
        "mov eax, DWORD PTR [ecx]",
        "mov DWORD PTR [ebx], eax",
        "mov DWORD PTR [ebx+12], 0",
        "test eax, eax",
        "je .spm_next",
        "mov edx, DWORD PTR [eax+0x20]",
        "mov DWORD PTR [ebx+4], edx",
        "mov DWORD PTR [ebx+8], edx",
        "mov edx, DWORD PTR [eax]",
        "test edx, edx",
        "je .spm_next",
        "mov edx, DWORD PTR [edx+0xdc]",
        "test edx, edx",
        "je .spm_next",
        "mov DWORD PTR [ebx+12], edx",
        "mov eax, DWORD PTR [edx+0x10]",
        "mov DWORD PTR [ebx+16], eax",
        "mov DWORD PTR [ebx+20], eax",
        ".spm_next:",
        "add ecx, 8",
        "add ebx, 32",
        "dec esi",
        "jne .spm_loop",
        "pop esi",
        "pop ebx",
        "ret",
        ".spm_clear_all:",
        "mov ebx, G_MM0",
        "mov esi, 4",
        ".spm_clr_loop:",
        "mov DWORD PTR [ebx], 0",
        "mov DWORD PTR [ebx+12], 0",
        "add ebx, 32",
        "dec esi",
        "jne .spm_clr_loop",
        "pop esi",
        "pop ebx",
        "ret",
    ]
    code_r, syms_r = link_block_with_symbols(BLOCK_R_START, asm_r, BLOCK_R_END - BLOCK_R_START, EXT_SYMS)
    put_bytes(buf, BLOCK_R_START, code_r, budget=BLOCK_R_END - BLOCK_R_START)

    # ------------------------------------------------------------------
    # 0b. Block P (0x5347D0..0x5348EC, 284B): MenuScreen 3D model interpolate
    # Linked before Block I so Block I calls onSavePrev_MS & onInterpolate_MS directly.
    # ------------------------------------------------------------------
    asm_p = [
        ".globl onInterpolate_MS",
        "f_p_ten:  .float 10.0",
        "f_p_mten: .float -10.0",

        # onInterpolate_MS(esi = ScreenMgr*): preserves ebx, esi, edi, ebp
        "onInterpolate_MS:",
        "push ebx",
        "push edi",
        "mov eax, DWORD PTR [PTR_MENUSCREEN]",
        "test eax, eax",
        "je .ipm_ret",
        "cmp DWORD PTR [esi+0x4c], eax",
        "jne .ipm_ret",
        "cmp DWORD PTR [esi+0x50], 0x10",
        "jae .ipm_ret",
        "lea ebx, [eax+0x120]",
        "mov edi, G_MM0",
        "push 4",
        ".ipm_loop:",
        "mov eax, DWORD PTR [ebx]",
        "test eax, eax",
        "je .ipm_next",
        "cmp DWORD PTR [edi], eax",
        "jne .ipm_next",
        "mov edx, DWORD PTR [edi+12]",
        "test edx, edx",
        "je .ipm_spline",
        "cmp BYTE PTR [G_JUST_TICKED], 0",
        "je .ipm_spline",
        "mov ecx, DWORD PTR [eax]",
        "test ecx, ecx",
        "je .ipm_spline",
        "cmp DWORD PTR [ecx+0xdc], edx",
        "jne .ipm_spline",
        "mov ecx, DWORD PTR [edx+0x10]",
        "mov DWORD PTR [edi+20], ecx",
        ".ipm_spline:",
        "cmp DWORD PTR [eax+0x8], 0",
        "je .ipm_next",
        "mov ecx, DWORD PTR [eax+0x20]",
        "cmp BYTE PTR [G_JUST_TICKED], 0",
        "je .ipm_chk_mix",
        "mov DWORD PTR [edi+8], ecx",
        ".ipm_chk_mix:",
        "movss xmm1, DWORD PTR [edi+4]",
        "movss xmm0, DWORD PTR [edi+8]",
        "ucomiss xmm1, xmm0",
        "je .ipm_next",
        "subss xmm0, xmm1",
        "comiss xmm0, DWORD PTR [f_p_ten]",
        "ja .ipm_snap",
        "comiss xmm0, DWORD PTR [f_p_mten]",
        "jb .ipm_snap",
        "mulss xmm0, DWORD PTR [G_UI_ALPHA]",
        "addss xmm0, xmm1",
        "jmp .ipm_eval",
        ".ipm_snap:",
        "movss xmm0, DWORD PTR [edi+8]",
        ".ipm_eval:",
        "push esi",
        "push eax",
        "mov esi, DWORD PTR [PTR_MSTACK]",
        "push ecx",
        "movss DWORD PTR [esp], xmm0",
        "mov ecx, DWORD PTR [eax+0x8]",
        "call FN_SPLINE_SET_T",
        "mov ecx, esi",
        "call FN_MSTACK_PUSH",
        "mov eax, DWORD PTR [esp]",
        "add eax, 0x54",
        "push eax",
        "mov ecx, esi",
        "call FN_MSTACK_SET",
        "mov eax, DWORD PTR [esp]",
        "mov ecx, DWORD PTR [eax]",
        "test ecx, ecx",
        "je .ipm_pop",
        "mov edx, DWORD PTR [ecx]",
        "push eax",
        "add eax, 0x8",
        "push eax",
        "call DWORD PTR [edx+0xc]",
        ".ipm_pop:",
        "mov ecx, esi",
        "call FN_MSTACK_POP",
        "pop eax",
        "pop esi",
        ".ipm_next:",
        "add ebx, 8",
        "add edi, 32",
        "dec DWORD PTR [esp]",
        "jne .ipm_loop",
        "add esp, 4",
        ".ipm_ret:",
        "pop edi",
        "pop ebx",
        "ret",
    ]
    code_p, syms_p = link_block_with_symbols(BLOCK_P_START, asm_p, BLOCK_P_END - BLOCK_P_START, EXT_SYMS)
    put_bytes(buf, BLOCK_P_START, code_p, budget=BLOCK_P_END - BLOCK_P_START)
    syms_for_i = dict(EXT_SYMS)
    syms_for_i.update(syms_r)
    syms_for_i.update(syms_p)

    # ------------------------------------------------------------------
    # 1. Block I (0x453FA0..0x4542CD, 813B): constants + float trackers + frame hooks
    # ------------------------------------------------------------------
    asm_i = [
        ".globl f_zero, f_one",
        "f_zero:    .float 0.0",
        "f_one:     .float 1.0",

        # Compact 4B descriptor table for ScreenMgr float trackers (.word base_off, val_off):
        #   base_off == 0 -> esi + val_off; >0 -> [esi+base_off] + val_off; -1 terminates
        #   Track [esi+0x304] CubeScreenEffect directly ([esi+0x44] aliases +0x304 in gameplay/menu
        #   and swaps to a smaller 0x3c base object +0x2fc in pause/win without +0x4c/+0x68).
        #   Sequential 16B BSS slots starting at TRK_PP304_4C (idx 0..3).
        "sm_trk_tbl:",
        ".word 0x304, 0x4c",
        ".word 0x304, 0x68",
        ".word 0,     0x8c",
        ".word 0,     0x34",
        ".word 0xffff",

        # resolve_sm_entry: esi = ScreenMgr*, ebx = entry* -> ecx = float* (0 if null)
        "resolve_sm_entry:",
        "movzx eax, WORD PTR [ebx]",
        "test eax, eax",
        "je .rse_direct",
        "mov ecx, DWORD PTR [esi+eax]",
        "test ecx, ecx",
        "je .rse_ret",
        "movzx eax, WORD PTR [ebx+2]",
        "add ecx, eax",
        "ret",
        ".rse_direct:",
        "movzx ecx, WORD PTR [ebx+2]",
        "add ecx, esi",
        ".rse_ret:",
        "ret",

        # save_float_tracker(ecx = float*, edx = trk*): preserves ebx, esi, edi
        "save_float_tracker:",
        "test ecx, ecx",
        "jne .sft_ok",
        "mov DWORD PTR [edx], 0",
        "ret",
        ".sft_ok:",
        "mov eax, DWORD PTR [ecx]",
        "cmp DWORD PTR [edx], ecx",
        "jne .sft_init",
        "cmp eax, DWORD PTR [edx+12]",
        "jne .sft_store",
        "mov eax, DWORD PTR [edx+8]",
        "mov DWORD PTR [ecx], eax",
        ".sft_store:",
        "mov DWORD PTR [edx+4], eax",
        "mov DWORD PTR [edx+8], eax",
        "mov DWORD PTR [edx+12], eax",
        "ret",
        ".sft_init:",
        "mov DWORD PTR [edx], ecx",
        "jmp .sft_store",

        # interp_float_tracker(ecx = float*, edx = trk*) -> al = 1 if prev != tgt else 0
        # Preserves ebx, esi, edi; reads G_JUST_TICKED & G_UI_ALPHA
        "interp_float_tracker:",
        "test ecx, ecx",
        "jne .ift_ok",
        "mov DWORD PTR [edx], 0",
        "xor al, al",
        "ret",
        ".ift_ok:",
        "mov eax, DWORD PTR [ecx]",
        "cmp DWORD PTR [edx], ecx",
        "jne .ift_snap",
        "cmp BYTE PTR [G_JUST_TICKED], 0",
        "je .ift_same_tick",
        "mov DWORD PTR [edx+8], eax",
        "jmp .ift_lerp",
        ".ift_same_tick:",
        "cmp eax, DWORD PTR [edx+12]",
        "je .ift_lerp",
        ".ift_snap:",
        "mov DWORD PTR [edx], ecx",
        "mov DWORD PTR [edx+4], eax",
        "mov DWORD PTR [edx+8], eax",
        "mov DWORD PTR [edx+12], eax",
        "xor al, al",
        "ret",
        ".ift_lerp:",
        "mov eax, DWORD PTR [edx+4]",
        "cmp eax, DWORD PTR [edx+8]",
        "jne .ift_mix",
        "mov DWORD PTR [edx+12], eax",
        "xor al, al",
        "ret",
        ".ift_mix:",
        "movss xmm0, DWORD PTR [edx+4]",
        "movss xmm1, DWORD PTR [edx+8]",
        "subss xmm1, xmm0",
        "mulss xmm1, DWORD PTR [G_UI_ALPHA]",
        "addss xmm0, xmm1",
        "movss DWORD PTR [ecx], xmm0",
        "movss DWORD PTR [edx+12], xmm0",
        "mov al, 1",
        "ret",

        # get_active_sub(esi = ScreenMgr*) -> eax = sub* (or 0), ecx = vtable (or 0)
        ".globl get_active_sub",
        "get_active_sub:",
        "mov eax, DWORD PTR [esi+0x4c]",
        "test eax, eax",
        "je .gas_none",
        "cmp DWORD PTR [eax], VT_MENUSCREEN",
        "jne .gas_none",
        "mov edx, DWORD PTR [esi+0x50]",
        "cmp edx, 0x17",
        "ja .gas_none",
        "mov eax, DWORD PTR [eax+edx*8+0x44]",
        "test eax, eax",
        "je .gas_none",
        "mov ecx, DWORD PTR [eax]",
        "ret",
        ".gas_none:",
        "xor eax, eax",
        "xor ecx, ecx",
        "ret",

        # Hooked from 0x481303..0x481309 in ScreenMgr::savePrev (esi = ScreenMgr*)
        # Must preserve esi, return eax = [esi+0x330]
        ".globl onSavePrev",
        "onSavePrev:",
        "push ebx",
        "push edi",
        "mov ebx, OFFSET sm_trk_tbl",
        "mov edi, TRK_PP304_4C",
        ".sp_sm_loop:",
        "cmp WORD PTR [ebx], 0xffff",
        "je .sp_ws",
        "call resolve_sm_entry",
        "mov edx, edi",
        "call save_float_tracker",
        "add ebx, 4",
        "add edi, 16",
        "jmp .sp_sm_loop",
        ".sp_ws:",
        "mov eax, DWORD PTR [PTR_WORLDSCR]",
        "test eax, eax",
        "je .sp_no_ws",
        "cmp BYTE PTR [eax+0x2120], 0",
        "je .sp_no_ws",
        "push eax",
        "lea ebx, [eax+0x1c]",
        "mov edi, TRK_WS_CAMX",
        ".sp_ws_cam_loop:",
        "mov ecx, ebx",
        "mov edx, edi",
        "call save_float_tracker",
        "add ebx, 4",
        "add edi, 16",
        "cmp edi, G_SUB_PTR",
        "jb .sp_ws_cam_loop",
        ".sp_ws_snap:",
        "pop edi",
        "cmp DWORD PTR [G_WS_PTR], edi",
        "je .sp_ws_fields",
        "mov DWORD PTR [G_WS_PTR], edi",
        "mov DWORD PTR [G_WS_BUILT2108], -1",
        ".sp_ws_fields:",
        "mov eax, DWORD PTR [edi+0x4]",
        "mov DWORD PTR [G_WS_PREV4], eax",
        "mov eax, DWORD PTR [edi+0x2108]",
        "mov DWORD PTR [G_WS_PREV2108], eax",
        "mov DWORD PTR [G_WS_TGT2108], eax",
        "movzx eax, BYTE PTR [edi+0x2158+0x2b]",
        "mov DWORD PTR [G_WS_PREV_TST2B], eax",
        "mov eax, DWORD PTR [edi+0x2158+0x34]",
        "mov DWORD PTR [G_WS_PREV_TST34], eax",
        "jmp .sp_sub",
        ".sp_no_ws:",
        "mov DWORD PTR [G_WS_PTR], 0",
        ".sp_sub:",
        "call get_active_sub",
        "mov DWORD PTR [G_SUB_PTR], eax",
        "test eax, eax",
        "je .sp_done",
        "mov edx, DWORD PTR [eax+0x4]",
        "mov DWORD PTR [G_SUB_PREV4], edx",
        "mov edx, DWORD PTR [eax+0x8]",
        "mov DWORD PTR [G_SUB_PREV8], edx",
        "cmp ecx, VT_PAUSEMENU",
        "jne .sp_chk_end",
        "mov edx, DWORD PTR [eax+0x4c]",
        "mov DWORD PTR [G_SUB_PREV4C], edx",
        "jmp .sp_done",
        ".sp_chk_end:",
        "cmp ecx, VT_ENDLEVEL",
        "jne .sp_done",
        "mov edx, DWORD PTR [eax+0x3c]",
        "mov DWORD PTR [G_END_PREV3C], edx",
        "mov edx, DWORD PTR [eax+0x40]",
        "mov DWORD PTR [G_END_PREV40], edx",
        ".sp_done:",
        ".globl sp_ms_callsite",
        "sp_ms_callsite:",
        "call onSavePrev_MS",
        "mov BYTE PTR [G_JUST_TICKED], 1",
        "pop edi",
        "pop ebx",
        "mov eax, DWORD PTR [esi+0x330]",
        "ret",

        # Jumped to from 0x48140b..0x481410 in ScreenMgr::interpolate
        # On entry: stack has [ebp+8] = alpha, callee-saved regs + ebp; eax = vt[4], ecx = [esi+0x4c]
        ".globl onInterpolate",
        "onInterpolate:",
        "call eax",
        "movss xmm0, DWORD PTR [ebp+0x8]",
        "maxss xmm0, DWORD PTR [f_zero]",
        "minss xmm0, DWORD PTR [f_one]",
        "movss DWORD PTR [G_UI_ALPHA], xmm0",
        "push ebx",
        "sub esp, 4",
        "mov BYTE PTR [esp], 0",  # [esp] bit0 = pause cam changed, bit1 = sm34 changed
        "mov ebx, OFFSET sm_trk_tbl",
        "mov edi, TRK_PP304_4C",
        ".ip_sm_loop:",
        "cmp WORD PTR [ebx], 0xffff",
        "je .ip_sm_done",
        "call resolve_sm_entry",
        "mov edx, edi",
        "call interp_float_tracker",
        "test al, al",
        "je .ip_sm_next",
        "cmp edi, TRK_SM_8C",
        "jne .ip_chk_34",
        "or BYTE PTR [esp], 1",
        "jmp .ip_sm_next",
        ".ip_chk_34:",
        "cmp edi, TRK_SM_34",
        "jne .ip_sm_next",
        "or BYTE PTR [esp], 2",
        ".ip_sm_next:",
        "add ebx, 4",
        "add edi, 16",
        "jmp .ip_sm_loop",
        ".ip_sm_done:",
        "test BYTE PTR [esp], 2",
        "je .ip_ws",
        "movss xmm0, DWORD PTR [esi+0x34]",
        "cvtsi2ss xmm1, DWORD PTR [esi+0x24]",
        "mulss xmm1, xmm0",
        "cvttss2si eax, xmm1",
        "mov DWORD PTR [esi+0x2c], eax",
        "cvtsi2ss xmm1, DWORD PTR [esi+0x28]",
        "mulss xmm1, xmm0",
        "cvttss2si ecx, xmm1",
        "mov DWORD PTR [esi+0x30], ecx",
        ".ip_ws:",
        "mov edi, DWORD PTR [PTR_WORLDSCR]",
        "test edi, edi",
        "je .ip_finish",
        "cmp BYTE PTR [edi+0x2120], 0",
        "je .ip_finish",
        "cmp BYTE PTR [G_JUST_TICKED], 0",
        "jne .ip_ws_sync_mm",
        "cmp DWORD PTR [G_WS_PTR], edi",
        "je .ip_ws_cam",
        ".ip_ws_sync_mm:",
        "mov eax, DWORD PTR [edi+0x2108]",
        "mov DWORD PTR [G_WS_TGT2108], eax",
        ".ip_ws_cam:",
        "mov eax, DWORD PTR [esi+0x50]",
        "cmp eax, 0x12",
        "jb .ip_ws_cam_upd",
        "cmp eax, 0x16",
        "ja .ip_ws_cam_upd",
        "lea ebx, [edi+0x1c]",
        "push edi",
        "mov edi, TRK_WS_CAMX",
        ".ip_ws_cam_loop:",
        "mov ecx, ebx",
        "mov edx, edi",
        "call interp_float_tracker",
        "or BYTE PTR [esp+4], al",
        "add ebx, 4",
        "add edi, 16",
        "cmp edi, G_SUB_PTR",
        "jb .ip_ws_cam_loop",
        "pop edi",
        ".ip_ws_cam_upd:",
        "test BYTE PTR [esp], 1",
        "je .ip_finish",
        "mov ecx, edi",
        "call FN_GAME_CAM_UPD",
        ".ip_finish:",
        ".globl ip_ms_callsite",
        "ip_ms_callsite:",
        "call onInterpolate_MS",
        "mov BYTE PTR [G_JUST_TICKED], 0",
        "add esp, 4",
        "pop ebx",
        "pop edi",
        "pop esi",
        "pop ebp",
        "ret 4",
    ]
    code_i, syms_i = link_block_with_symbols(BLOCK_I_START, asm_i, BLOCK_I_END - BLOCK_I_START, syms_for_i)
    put_bytes(buf, BLOCK_I_START, code_i, budget=BLOCK_I_END - BLOCK_I_START)

    # Combine Block I + Block P exported symbols for Blocks J..Q
    all_syms = dict(syms_for_i)
    all_syms.update(syms_i)

    # ------------------------------------------------------------------
    # 2. Block J (0x4FCA10..0x4FCC34, 548B): Submenu & PauseMenu fade/slide helpers
    # ------------------------------------------------------------------
    asm_j = [
        ".globl smooth_counter_pair, clamp_pct_0_100, div_pct_8, div_pct_4, div_pct_4_m4, f_four, f_five, f_20, f_100, f_255",
        "f_four:  .float 4.0",
        "f_five:  .float 5.0",
        "f_12p5:  .float 12.5",
        "f_20:    .float 20.0",
        "f_25:    .float 25.0",
        "f_100:   .float 100.0",
        "f_m223:  .float -223.0",
        "f_255:   .float 255.0",

        # smooth_counter_pair(eax = cur, edx = prev, cl = isOutBool) -> xmm0 = smooth float
        "smooth_counter_pair:",
        "test cl, cl",
        "je .scp_in",
        "test eax, eax",
        "js .scp_cur",
        "test edx, edx",
        "js .scp_cur",  # just started fade-out (-1 -> N): hold N for first tick
        "jmp .scp_mix",
        ".scp_in:",
        "cmp eax, edx",
        "jl .scp_cur",  # reset backward: snap immediately
        ".scp_mix:",
        "cvtsi2ss xmm0, edx",
        "cmp eax, edx",
        "je .scp_done",
        "sub eax, edx",
        "cvtsi2ss xmm1, eax",
        "mulss xmm1, DWORD PTR [G_UI_ALPHA]",
        "addss xmm0, xmm1",
        ".scp_done:",
        "ret",
        ".scp_cur:",
        "cvtsi2ss xmm0, eax",
        "ret",

        # div_pct_8(xmm0 = val) -> eax = clamp_round(val * 12.5, 0, 100)
        "div_pct_8:",
        "mulss xmm0, DWORD PTR [f_12p5]",
        "jmp clamp_pct_0_100",
        "div_pct_4_m4:",
        "subss xmm0, DWORD PTR [f_four]",
        "div_pct_4:",
        "mulss xmm0, DWORD PTR [f_25]",
        "clamp_pct_0_100:",
        "maxss xmm0, DWORD PTR [f_zero]",
        "minss xmm0, DWORD PTR [f_100]",
        "cvtss2si eax, xmm0",
        "ret",

        # calcSubFadePct(ecx = sub*, [esp+4] = mode) -> eax = pct (0..100)
        # Preserves ebx, esi, edi, ebp.
        ".globl calcSubFadePct",
        "calcSubFadePct:",
        "mov eax, DWORD PTR [ecx+0x4]",
        "cmp eax, 8",
        "jge .csf_exit",
        # Enter branch (cur4 < 8)
        "cmp DWORD PTR [G_SUB_PTR], ecx",
        "jne .csf_in_cur",
        "mov edx, DWORD PTR [G_SUB_PREV4]",
        "jmp .csf_in_smooth",
        ".csf_in_cur:",
        "mov edx, eax",
        ".csf_in_smooth:",
        "push ecx",
        "xor cl, cl",
        "call smooth_counter_pair",
        "pop ecx",
        "mov eax, DWORD PTR [esp+4]",
        "cmp eax, 1",
        "je .csf_in_m1",
        "cmp eax, 2",
        "je .csf_in_m2",
        "call div_pct_8",
        "ret 4",
        ".csf_in_m1:",
        "mov eax, DWORD PTR [PTR_SCREENMGR]",
        "cmp DWORD PTR [eax+0x54], 2",
        "je .csf_use8",
        "call div_pct_4_m4",
        "ret 4",
        ".csf_in_m2:",
        "mov eax, DWORD PTR [PTR_SCREENMGR]",
        "mov edx, DWORD PTR [eax+0x54]",
        "cmp edx, 1",
        "je .csf_use8",
        "cmp edx, 0x12",
        "je .csf_use8",
        "cmp edx, 0x14",
        "jb .csf_use4m4",
        "cmp edx, 0x16",
        "jbe .csf_use8",
        ".csf_use4m4:",
        "call div_pct_4_m4",
        "ret 4",
        ".csf_use8:",
        "call div_pct_8",
        "ret 4",

        ".csf_exit:",
        "mov eax, DWORD PTR [ecx+0x8]",
        "test eax, eax",
        "js .csf_full100",
        "mov edx, DWORD PTR [esp+4]",
        "cmp edx, 3",
        "jne .csf_exit_sm",
        "cmp DWORD PTR [ecx+0xc], 3",
        "jne .csf_full100",
        ".csf_exit_sm:",
        "cmp DWORD PTR [G_SUB_PTR], ecx",
        "jne .csf_out_cur",
        "mov edx, DWORD PTR [G_SUB_PREV8]",
        "jmp .csf_out_calc",
        ".csf_out_cur:",
        "mov edx, eax",
        ".csf_out_calc:",
        "push ecx",
        "mov cl, 1",
        "call smooth_counter_pair",
        "pop ecx",
        "mov edx, DWORD PTR [esp+4]",
        "cmp edx, 4",
        "je .csf_use4",
        "cmp edx, 2",
        "je .csf_use4m4",
        "cmp edx, 1",
        "jne .csf_use8",
        "cmp DWORD PTR [ecx+0xc], 2",
        "je .csf_use8",
        "jmp .csf_use4m4",
        ".csf_use4:",
        "call div_pct_4",
        "ret 4",
        ".csf_full100:",
        "mov eax, 100",
        "ret 4",

        # pauseSmoothSlideY(ecx = PauseMenu*) -> eax = vertical offset (-223..0)
        # Preserves ebx, ecx, esi, edi, ebp.
        ".globl pauseSmoothSlideY",
        "pauseSmoothSlideY:",
        "mov eax, DWORD PTR [ecx+0x8]",
        "test eax, eax",
        "js .ps_chk_in",
        "cmp DWORD PTR [G_SUB_PTR], ecx",
        "jne .ps_out_cur",
        "mov edx, DWORD PTR [G_SUB_PREV8]",
        "jmp .ps_out_mix",
        ".ps_out_cur:",
        "mov edx, eax",
        ".ps_out_mix:",
        "push ecx",
        "mov cl, 1",
        "call smooth_counter_pair",
        "pop ecx",
        "movss xmm1, DWORD PTR [f_four]",
        "subss xmm1, xmm0",
        "mulss xmm1, DWORD PTR [f_25]",
        "jmp .ps_scale",
        ".ps_chk_in:",
        "mov eax, DWORD PTR [ecx+0x4]",
        "cmp eax, 5",
        "jge .ps_zero",
        "cmp DWORD PTR [G_SUB_PTR], ecx",
        "jne .ps_in_cur",
        "mov edx, DWORD PTR [G_SUB_PREV4]",
        "jmp .ps_in_mix",
        ".ps_in_cur:",
        "mov edx, eax",
        ".ps_in_mix:",
        "push ecx",
        "xor cl, cl",
        "call smooth_counter_pair",
        "pop ecx",
        "movss xmm1, DWORD PTR [f_five]",
        "subss xmm1, xmm0",
        "mulss xmm1, DWORD PTR [f_20]",
        ".ps_scale:",
        "maxss xmm1, DWORD PTR [f_zero]",
        "minss xmm1, DWORD PTR [f_100]",
        "mulss xmm1, DWORD PTR [f_m223]",
        "divss xmm1, DWORD PTR [f_100]",
        "cvtss2si eax, xmm1",
        "ret",
        ".ps_zero:",
        "xor eax, eax",
        "ret",

    ]
    code_j, syms_j = link_block_with_symbols(BLOCK_J_START, asm_j, BLOCK_J_END - BLOCK_J_START, all_syms)
    put_bytes(buf, BLOCK_J_START, code_j, budget=BLOCK_J_END - BLOCK_J_START)
    all_syms.update(syms_j)

    # ------------------------------------------------------------------
    # 2b. Block L (0x412F60..0x4130DE, 382B): PauseConfirm, EndLevel, PauseMenu X + constants
    # ------------------------------------------------------------------
    asm_l = [
        ".globl f_three",
        "f_three: .float 3.0",
        "f_51:    .float 51.0",
        "f_m44:   .float -44.0",
        "f_100d7: .float 14.285714",

        # pauseSmoothX(ecx = PauseMenu*) -> xmm2 = -(smooth 0..320) float
        # Preserves ebx, esi, edi, ebp.
        ".globl pauseSmoothX",
        "pauseSmoothX:",
        "mov eax, DWORD PTR [ecx+0x4c]",
        "cvtsi2ss xmm2, eax",
        "cmp DWORD PTR [G_SUB_PTR], ecx",
        "jne .px_neg",
        "mov edx, DWORD PTR [G_SUB_PREV4C]",
        "cmp eax, edx",
        "je .px_neg",
        "cvtsi2ss xmm2, edx",
        "sub eax, edx",
        "cvtsi2ss xmm3, eax",
        "mulss xmm3, DWORD PTR [G_UI_ALPHA]",
        "addss xmm2, xmm3",
        ".px_neg:",
        "xorps xmm3, xmm3",
        "subss xmm3, xmm2",
        "movaps xmm2, xmm3",
        "movss xmm1, DWORD PTR [f_one]",
        "xorps xmm0, xmm0",
        "ret",

        # pauseConfirmSmooth(ecx = PauseConfirm*) -> eax = slide_y (-44..0), edx = fade_pct (0..100)
        # Preserves ebx, esi, edi, ebp.
        ".globl pauseConfirmSmooth",
        "pauseConfirmSmooth:",
        "mov eax, DWORD PTR [ecx+0x4]",
        "cmp eax, 3",
        "jle .pc_slide",
        "xor eax, eax",
        "jmp .pc_fade",
        ".pc_slide:",
        "cmp DWORD PTR [G_SUB_PTR], ecx",
        "jne .pc_in_cur",
        "mov edx, DWORD PTR [G_SUB_PREV4]",
        "jmp .pc_in_mix",
        ".pc_in_cur:",
        "mov edx, eax",
        ".pc_in_mix:",
        "push ecx",
        "xor cl, cl",
        "call smooth_counter_pair",
        "pop ecx",
        "movss xmm1, DWORD PTR [f_three]",
        "subss xmm1, xmm0",
        "divss xmm1, DWORD PTR [f_three]",
        "maxss xmm1, DWORD PTR [f_zero]",
        "minss xmm1, DWORD PTR [f_one]",
        "mulss xmm1, DWORD PTR [f_m44]",
        "cvtss2si eax, xmm1",
        ".pc_fade:",
        "push eax",
        "mov eax, DWORD PTR [ecx+0x8]",
        "test eax, eax",
        "js .pc_f100",
        "cmp DWORD PTR [G_SUB_PTR], ecx",
        "jne .pc_out_cur",
        "mov edx, DWORD PTR [G_SUB_PREV8]",
        "jmp .pc_out_mix",
        ".pc_out_cur:",
        "mov edx, eax",
        ".pc_out_mix:",
        "push ecx",
        "mov cl, 1",
        "call smooth_counter_pair",
        "pop ecx",
        "call div_pct_8",
        "mov edx, eax",
        "pop eax",
        "ret",
        ".pc_f100:",
        "mov edx, 100",
        "pop eax",
        "ret",

        # endFlashAlpha(ecx = EndLevel*) -> al (0..255)
        # endFadePct(ecx = EndLevel*) -> eax (0..100)
        # Preserves ebx, esi, edi, ebp.
        ".globl endFlashAlpha, endFadePct",
        "endFlashAlpha:",
        "mov eax, DWORD PTR [ecx+0x3c]",
        "cmp DWORD PTR [G_SUB_PTR], ecx",
        "jne .ef_cur",
        "mov edx, DWORD PTR [G_END_PREV3C]",
        "jmp .ef_mix",
        ".ef_cur:",
        "mov edx, eax",
        ".ef_mix:",
        "push ecx",
        "xor cl, cl",
        "call smooth_counter_pair",
        "pop ecx",
        "mulss xmm0, DWORD PTR [f_51]",
        "maxss xmm0, DWORD PTR [f_zero]",
        "minss xmm0, DWORD PTR [f_255]",
        "cvtss2si eax, xmm0",
        "ret",

        "endFadePct:",
        "mov eax, DWORD PTR [ecx+0x40]",
        "cmp eax, 7",
        "jle .efd_go",
        "mov eax, 100",
        "ret",
        ".efd_go:",
        "cmp DWORD PTR [G_SUB_PTR], ecx",
        "jne .efd_cur",
        "mov edx, DWORD PTR [G_END_PREV40]",
        "jmp .efd_mix",
        ".efd_cur:",
        "mov edx, eax",
        ".efd_mix:",
        "push ecx",
        "xor cl, cl",
        "call smooth_counter_pair",
        "pop ecx",
        "mulss xmm0, DWORD PTR [f_100d7]",
        "call clamp_pct_0_100",
        "ret",
    ]
    code_l, syms_l = link_block_with_symbols(BLOCK_L_START, asm_l, BLOCK_L_END - BLOCK_L_START, all_syms)
    put_bytes(buf, BLOCK_L_START, code_l, budget=BLOCK_L_END - BLOCK_L_START)
    all_syms.update(syms_l)

    # ------------------------------------------------------------------
    # 3. Block K (0x44DF40..0x44E154, 532B): WorldScreen HUD flash & rainbow
    # ------------------------------------------------------------------
    asm_k = [
        "f_50:  .float 50.0",
        "f_0p5: .float 0.5",
        "f_10:  .float 10.0",
        "f_200: .float 200.0",

        # getSmoothWsFrame(ecx = WorldScreen*) -> xmm0 = continuous frame float
        # Preserves ecx, ebx, edx, esi, edi, ebp.
        ".globl getSmoothWsFrame",
        "getSmoothWsFrame:",
        "mov eax, DWORD PTR [ecx+0x4]",
        "cvtsi2ss xmm0, eax",
        "cmp DWORD PTR [G_WS_PTR], ecx",
        "jne .gswf_ret",
        "cmp eax, DWORD PTR [G_WS_PREV4]",
        "jbe .gswf_ret",
        "mov eax, DWORD PTR [G_WS_PREV4]",
        "cvtsi2ss xmm0, eax",
        "addss xmm0, DWORD PTR [G_UI_ALPHA]",
        ".gswf_ret:",
        "ret",

        # calcSmoothRainbow(xmm0 = frame, xmm1 = ticksPerColor) -> eax = 0x00RRGGBB
        # Preserves ebx, esi, edi, ebp.
        ".globl calcSmoothRainbow",
        "calcSmoothRainbow:",
        "divss xmm0, xmm1",
        "cvttss2si ecx, xmm0",   # total steps >= 0
        "cvtsi2ss xmm1, ecx",
        "subss xmm0, xmm1",       # frac in [0, 1)
        "mulss xmm0, DWORD PTR [f_100]",
        "cvtss2si eax, xmm0",     # pct (0..100)
        "push eax",
        "mov eax, ecx",
        "xor edx, edx",
        "mov ecx, 13",
        "div ecx",                # edx = idx in [0..12]
        "lea eax, [edx+1]",
        "cmp eax, 13",
        "jl .csr_idx_ok",
        "xor eax, eax",
        ".csr_idx_ok:",
        "mov ecx, DWORD PTR [eax*4+PALETTE_RAINBOW]",
        "mov edx, DWORD PTR [edx*4+PALETTE_RAINBOW]",
        "pop eax",
        "push eax",               # pct
        "push ecx",               # c1
        "push edx",               # c0
        "call FN_COLOR_LERP",
        "add esp, 12",
        "ret",

        # getSmoothMenuRainbow(eax = frame) -> eax = 0x00RRGGBB, [G_RB_TMP] = eax
        # Preserves ebx, ecx, edx, esi, edi, ebp.
        ".globl getSmoothMenuRainbow",
        "getSmoothMenuRainbow:",
        "push ecx",
        "push edx",
        "add eax, 130",
        "cvtsi2ss xmm0, eax",
        "addss xmm0, DWORD PTR [G_UI_ALPHA]",
        "movss xmm1, DWORD PTR [f_one]",
        "call calcSmoothRainbow",
        "mov DWORD PTR [G_RB_TMP], eax",
        "pop edx",
        "pop ecx",
        "ret",

        # hudFlashSmooth(edi = WorldScreen*, ebx = Renderer*, [ebp-8..5] out) -> al = 1 drawn, 0 skip
        # Exact 3-tick (100 ms) fade: sweeps float(cur) -> float(cur-1) while cur > 0, skips immediately at cur <= 0.
        # Preserves ebx, edi, ebp.
        ".globl hudFlashSmooth",
        "hudFlashSmooth:",
        "mov eax, DWORD PTR [edi+0x211c]",
        "test eax, eax",
        "jle .hfs_skip",
        "cvtsi2ss xmm0, eax",
        "cmp DWORD PTR [G_WS_PTR], edi",
        "jne .hfs_alpha",
        "subss xmm0, DWORD PTR [G_UI_ALPHA]",
        "maxss xmm0, DWORD PTR [f_zero]",
        "minss xmm0, DWORD PTR [f_three]",
        ".hfs_alpha:",
        "movss xmm1, DWORD PTR [f_one]",
        "addss xmm1, xmm1",                  # 2.0f
        "ucomiss xmm0, xmm1",
        "jbe .hfs_low",
        "subss xmm0, xmm1",
        "mulss xmm0, DWORD PTR [f_100]",
        "addss xmm0, DWORD PTR [f_100]",
        "jmp .hfs_round_a",
        ".hfs_low:",
        "mulss xmm0, DWORD PTR [f_50]",
        ".hfs_round_a:",
        "maxss xmm0, DWORD PTR [f_zero]",
        "minss xmm0, DWORD PTR [f_200]",
        "cvtss2si esi, xmm0",
        "test esi, esi",
        "jle .hfs_skip",
        "jmp .hfs_draw",
        ".hfs_skip:",
        "xor al, al",
        "ret",
        ".hfs_draw:",
        "push esi",                          # save alpha_u8
        "push 1",
        "mov ecx, ebx",
        "call FN_SET_BLEND",
        "mov ecx, edi",
        "call getSmoothWsFrame",
        "movss xmm1, DWORD PTR [f_one]",
        "call calcSmoothRainbow",            # eax = 0x00RRGGBB
        "push 0x4b",
        "push 0xffffff",
        "push eax",
        "call FN_COLOR_LERP",
        "add esp, 12",
        "pop esi",
        "mov ecx, eax",
        "mov edx, eax",
        "sar ecx, 16",
        "sar edx, 8",
        "mov BYTE PTR [ebp-0x8], cl",
        "mov BYTE PTR [ebp-0x7], dl",
        "mov BYTE PTR [ebp-0x6], al",
        "mov eax, esi",
        "mov BYTE PTR [ebp-0x5], al",
        "mov al, 1",
        "ret",

        # hudRainbowSmooth(edi = WorldScreen*) -> ecx = 0x00RRGGBB, eax = alpha (200..250)
        # Preserves ebx, edi, ebp.
        ".globl hudRainbowSmooth",
        "hudRainbowSmooth:",
        "mov ecx, edi",
        "call getSmoothWsFrame",
        "sub esp, 4",
        "movss DWORD PTR [esp], xmm0",       # save f_frame
        "movss xmm1, DWORD PTR [f_10]",
        "call calcSmoothRainbow",
        "movss xmm0, DWORD PTR [esp]",
        "add esp, 4",
        "push eax",                          # save color
        "movaps xmm1, xmm0",
        "divss xmm1, DWORD PTR [f_200]",
        "cvttss2si edx, xmm1",
        "cvtsi2ss xmm1, edx",
        "mulss xmm1, DWORD PTR [f_200]",
        "subss xmm0, xmm1",                  # f_frame mod 200 in [0, 200)
        "ucomiss xmm0, DWORD PTR [f_100]",
        "jb .hrw_tri_ok",
        "movss xmm1, DWORD PTR [f_200]",
        "subss xmm1, xmm0",
        "movaps xmm0, xmm1",
        ".hrw_tri_ok:",
        "mulss xmm0, DWORD PTR [f_0p5]",
        "cvtss2si eax, xmm0",
        "add eax, 200",
        "pop ecx",                           # ecx = 0x00RRGGBB
        "ret",
    ]
    code_k, syms_k = link_block_with_symbols(BLOCK_K_START, asm_k, BLOCK_K_END - BLOCK_K_START, all_syms)
    put_bytes(buf, BLOCK_K_START, code_k, budget=BLOCK_K_END - BLOCK_K_START)
    all_syms.update(syms_k)

    # ------------------------------------------------------------------
    # 3b. Block M (0x43B060..0x43B1B8, 344B): Minimap & PauseMenu skip guards
    # ------------------------------------------------------------------
    asm_m = [
        ".globl minimapSmoothDraw, mmSkipIn, mmSkipOut, mmForceVb, pauseP0Skip, pauseP1Skip",
        "minimapSmoothDraw:",
        "mov eax, DWORD PTR [edi+0x2108]",
        "cmp DWORD PTR [G_WS_PTR], edi",
        "jne .msd_sync",
        "cmp eax, DWORD PTR [G_WS_TGT2108]",
        "je .msd_interp",
        ".msd_sync:",
        "mov DWORD PTR [G_WS_PTR], edi",
        "mov DWORD PTR [G_WS_TGT2108], eax",
        "mov DWORD PTR [G_WS_PREV2108], eax",
        ".msd_interp:",
        "mov edx, DWORD PTR [G_WS_PREV2108]",
        "mov eax, DWORD PTR [G_WS_TGT2108]",
        "cmp eax, edx",
        "je .msd_chk_pos",
        "cvtsi2ss xmm0, edx",
        "sub eax, edx",
        "cvtsi2ss xmm1, eax",
        "mulss xmm1, DWORD PTR [G_UI_ALPHA]",
        "addss xmm0, xmm1",
        "maxss xmm0, DWORD PTR [f_zero]",
        "minss xmm0, DWORD PTR [f_255]",
        "cvtss2si eax, xmm0",
        ".msd_chk_pos:",
        "test eax, eax",
        "jle .msd_ret",
        "mov edx, DWORD PTR [PTR_SCREENMGR]",
        "mov ecx, DWORD PTR [edx+0x34]",
        "cmp eax, DWORD PTR [G_WS_BUILT2108]",
        "jne .msd_rebuild",
        "cmp DWORD PTR [edi+0x1c0], ecx",
        "je .msd_draw",
        ".msd_rebuild:",
        "push eax",
        "mov BYTE PTR [G_WS_MM_INTERP], 1",
        "mov DWORD PTR [edi+0x2108], eax",
        "push 0",
        "mov ecx, edi",
        "call FN_MM_REBUILD",
        "mov BYTE PTR [G_WS_MM_INTERP], 0",
        "pop eax",
        ".msd_draw:",
        "mov DWORD PTR [edi+0x2108], eax",
        "mov ecx, edi",
        "call FN_MM_DRAW",
        "mov edx, DWORD PTR [G_WS_TGT2108]",
        "mov DWORD PTR [edi+0x2108], edx",
        ".msd_ret:",
        "ret",

        "mmSkipIn:",
        "cmp BYTE PTR [G_WS_MM_INTERP], 0",
        "jne .mmsi_skip",
        "mov edx, DWORD PTR [ebx+0x2108]",
        "jmp 0x47e670",
        ".mmsi_skip:",
        "jmp 0x47e6a9",

        "mmSkipOut:",
        "cmp BYTE PTR [G_WS_MM_INTERP], 0",
        "jne .mmso_skip",
        "mov eax, DWORD PTR [ebx+0x2108]",
        "jmp 0x47e69a",
        ".mmso_skip:",
        "jmp 0x47e6a9",

        "mmForceVb:",
        "cmp BYTE PTR [G_WS_MM_INTERP], 0",
        "je .mmfv_chk",
        "mov cl, 1",
        ".mmfv_chk:",
        "test cl, cl",
        "je .mmfv_done",
        "mov eax, DWORD PTR [ebx+0x2108]",
        "mov DWORD PTR [G_WS_BUILT2108], eax",
        ".mmfv_done:",
        "mov edi, DWORD PTR ds:0x5f9040",
        "ret",

        "pauseP0Skip:",
        "cmp DWORD PTR [edi+0x4c], 0x140",
        "jne .p0_draw",
        "cmp DWORD PTR [G_SUB_PTR], edi",
        "jne .p0_skip",
        "cmp DWORD PTR [G_SUB_PREV4C], 0x140",
        "jne .p0_draw",
        ".p0_skip:",
        "jmp 0x4ad44c",
        ".p0_draw:",
        "jmp 0x4ad1ea",

        "pauseP1Skip:",
        "cmp DWORD PTR [edi+0x4c], ebx",
        "jne .p1_draw",
        "cmp DWORD PTR [G_SUB_PTR], edi",
        "jne .p1_skip",
        "cmp DWORD PTR [G_SUB_PREV4C], ebx",
        "jne .p1_draw",
        ".p1_skip:",
        "jmp 0x4adf61",
        ".p1_draw:",
        "jmp 0x4ad468",
    ]
    code_m, syms_m = link_block_with_symbols(BLOCK_M_START, asm_m, BLOCK_M_END - BLOCK_M_START, all_syms)
    put_bytes(buf, BLOCK_M_START, code_m, budget=BLOCK_M_END - BLOCK_M_START)
    all_syms.update(syms_m)

    # ------------------------------------------------------------------
    # 3c. Block N (0x45CC80..0x45CD9D, 285B): Toast alpha smooth
    # ------------------------------------------------------------------
    asm_n = [
        "f_170_255: .float 0.6666667",

        ".globl toastAlphaSmooth",
        "toastAlphaSmooth:",
        "mov eax, DWORD PTR [G_WS_PTR]",
        "xor ecx, ecx",
        "test eax, eax",
        "je .tas_chk_timer",
        "add eax, 0x2158",
        "cmp esi, eax",
        "sete cl",
        ".tas_chk_timer:",
        "cmp DWORD PTR [esi+0x34], 0",
        "jg .tas_val",
        "test cl, cl",
        "je .tas_skip",
        "cmp DWORD PTR [G_WS_PREV_TST34], 0",
        "jle .tas_skip",
        "cmp DWORD PTR [G_WS_PREV_TST2B], 0",
        "jle .tas_skip",
        ".tas_val:",
        "movzx eax, BYTE PTR [esi+0x2b]",
        "cvtsi2ss xmm0, eax",
        "test cl, cl",
        "je .tas_scale",
        "mov edx, DWORD PTR [G_WS_PREV_TST2B]",
        "cmp eax, edx",
        "je .tas_scale",
        "cvtsi2ss xmm0, edx",
        "sub eax, edx",
        "cvtsi2ss xmm1, eax",
        "mulss xmm1, DWORD PTR [G_UI_ALPHA]",
        "addss xmm0, xmm1",
        ".tas_scale:",
        "mulss xmm0, DWORD PTR [f_170_255]",
        "maxss xmm0, DWORD PTR [f_zero]",
        "minss xmm0, DWORD PTR [f_255]",
        "cvtss2si eax, xmm0",
        "ret",
        ".tas_skip:",
        "xor eax, eax",
        "ret",

        ".globl pauseP2Skip, pauseP2X",
        "pauseP2Skip:",
        "cmp DWORD PTR [edi+0x4c], ebx",
        "jne .p2_draw",
        "cmp DWORD PTR [G_SUB_PTR], edi",
        "jne .p2_skip",
        "cmp DWORD PTR [G_SUB_PREV4C], ebx",
        "jne .p2_draw",
        ".p2_skip:",
        "jmp 0x4ae25b",
        ".p2_draw:",
        "jmp 0x4adf7d",

        # pauseP2X(edi = PauseMenu*) -> xmm2 = -smoothX, xmm1 = 1.0f, xmm0 = 0.0f
        # Preserves ebx, ecx, edx, esi, edi, ebp.
        "pauseP2X:",
        "push edx",
        "mov eax, DWORD PTR [edi+0x4c]",
        "mov edx, eax",
        "cmp DWORD PTR [G_SUB_PTR], edi",
        "jne .p2x_cur",
        "mov edx, DWORD PTR [G_SUB_PREV4C]",
        ".p2x_cur:",
        "cvtsi2ss xmm2, edx",
        "cmp eax, edx",
        "je .p2x_neg",
        "sub eax, edx",
        "cvtsi2ss xmm0, eax",
        "mulss xmm0, DWORD PTR [G_UI_ALPHA]",
        "addss xmm2, xmm0",
        ".p2x_neg:",
        "xorps xmm0, xmm0",
        "subss xmm0, xmm2",
        "movaps xmm2, xmm0",
        "xorps xmm0, xmm0",
        "movss xmm1, DWORD PTR [f_one]",
        "pop edx",
        "ret",
    ]
    code_n, syms_n = link_block_with_symbols(BLOCK_N_START, asm_n, BLOCK_N_END - BLOCK_N_START, all_syms)
    put_bytes(buf, BLOCK_N_START, code_n, budget=BLOCK_N_END - BLOCK_N_START)
    all_syms.update(syms_n)

    # ------------------------------------------------------------------
    # 3d. Block O (0x541B10..0x541C2C, 284B): Submenu selection bar Y slide helper
    # ------------------------------------------------------------------
    asm_o = [
        # calcSubSlideY(ecx = sub*, edx = mode, [esp+4] = row_arg if mode==1) -> eax = y offset
        # Modes: 0=MainMenu, 1=ModeSelect, 2=s_4b28b0, 3=s_4b1aa0
        # Preserves ebx, esi, edi, ebp.
        ".globl calcSubSlideY",
        "calcSubSlideY:",
        "push esi",
        "push edi",
        "mov esi, ecx",
        "mov edi, edx",
        "mov eax, DWORD PTR [esi+0x8]",
        "test eax, eax",
        "js .csy_try_in",
        "cmp eax, 4",
        "jge .csy_try_in",
        "test edi, edi",
        "je .csy_out_go",
        "cmp DWORD PTR [esi+0xc], 2",
        "je .csy_try_in",
        ".csy_out_go:",
        "mov cl, 1",                   # isOutBool = 1
        "cmp DWORD PTR [G_SUB_PTR], esi",
        "jne .csy_out_cur",
        "mov edx, DWORD PTR [G_SUB_PREV8]",
        "jmp .csy_mix",
        ".csy_out_cur:",
        "mov edx, eax",
        "jmp .csy_mix",
        ".csy_try_in:",
        "mov eax, DWORD PTR [esi+0x4]",
        "cmp eax, 4",
        "jge .csy_zero",
        "mov ecx, DWORD PTR [PTR_SCREENMGR]",
        "mov ecx, DWORD PTR [ecx+0x54]",
        "test edi, edi",
        "jne .csy_chk_m2",
        "cmp ecx, 1",
        "je .csy_zero",
        "cmp ecx, 0x12",
        "je .csy_zero",
        "cmp ecx, 0x14",
        "jl .csy_in_go",
        "cmp ecx, 0x16",
        "jle .csy_zero",
        "jmp .csy_in_go",
        ".csy_chk_m2:",
        "cmp ecx, 2",
        "je .csy_zero",
        ".csy_in_go:",
        "xor cl, cl",                  # isOutBool = 0
        "cmp DWORD PTR [G_SUB_PTR], esi",
        "jne .csy_in_cur",
        "mov edx, DWORD PTR [G_SUB_PREV4]",
        "jmp .csy_mix",
        ".csy_in_cur:",
        "mov edx, eax",
        ".csy_mix:",
        "push ecx",
        "call smooth_counter_pair",    # xmm0 = smooth counter (0..4)
        "pop ecx",
        "movss xmm1, DWORD PTR [f_four]",
        "subss xmm1, xmm0",
        "maxss xmm1, DWORD PTR [f_zero]",
        "minss xmm1, DWORD PTR [f_four]",
        "divss xmm1, DWORD PTR [f_four]", # xmm1 = pct_rem in [0.0, 1.0]
        "mov edx, DWORD PTR [esi+0x10]",
        "test edi, edi",
        "jne .csy_mode2",
        "imul eax, edx, -50",
        "jmp .csy_sub82",
        ".csy_mode2:",
        "cmp edi, 2",
        "jne .csy_m13",
        "imul eax, edx, -40",
        "test cl, 1",
        "je .csy_sub82",
        "sub eax, 11",
        ".csy_sub82:",
        "sub eax, 82",
        "jmp .csy_scale",
        ".csy_m13:",
        "mov eax, edx",
        "cmp edi, 1",
        "jne .csy_chk4",
        "cmp edx, 5",
        "je .csy_footer",
        "mov eax, DWORD PTR [esp+12]",
        "jmp .csy_row50",
        ".csy_chk4:",
        "cmp edx, 4",
        "je .csy_footer",
        ".csy_row50:",
        "imul eax, eax, -50",
        "jmp .csy_sub82",
        ".csy_footer:",
        "call FN_FOOTER_Y",            # preserves ecx, edx
        "neg eax",
        "add eax, 0x3d",
        ".csy_scale:",
        "cvtsi2ss xmm0, eax",
        "mulss xmm0, xmm1",
        "cvttss2si eax, xmm0",
        "test cl, 1",
        "jne .csy_ret",
        "add eax, 4",
        ".csy_ret:",
        "pop edi",
        "pop esi",
        "ret",
        ".csy_zero:",
        "xor eax, eax",
        "pop edi",
        "pop esi",
        "ret",
    ]
    code_o, syms_o = link_block_with_symbols(BLOCK_O_START, asm_o, BLOCK_O_END - BLOCK_O_START, all_syms)
    put_bytes(buf, BLOCK_O_START, code_o, budget=BLOCK_O_END - BLOCK_O_START)
    all_syms.update(syms_o)

    # ------------------------------------------------------------------
    # 3e. Block Q (0x4F79C0..0x4F7AD6, 278B): ModelNode timer interp, marquee, LevelSelect scroll
    # ------------------------------------------------------------------
    asm_q = [
        "f_q_ten: .float 10.0",
        ".globl interpModelNodeT, msScrollSmooth, lsScrollSmooth",

        # interpModelNodeT(eax = Timer*): writes interpolated time float into [ebp+0x8]
        # Preserves ebx, ecx, edx, esi, edi, ebp.
        "interpModelNodeT:",
        "push ecx",
        "push edx",
        "mov ecx, G_MM0",
        "mov edx, 4",
        ".imnt_find:",
        "cmp DWORD PTR [ecx+12], eax",
        "je .imnt_hit",
        "add ecx, 32",
        "dec edx",
        "jne .imnt_find",
        ".imnt_raw:",
        "mov eax, DWORD PTR [eax+0x10]",
        "mov DWORD PTR [ebp+0x8], eax",
        "pop edx",
        "pop ecx",
        "ret",
        ".imnt_hit:",
        "movss xmm0, DWORD PTR [ecx+16]",  # prev_nt
        "movss xmm1, DWORD PTR [eax+0x10]", # cur_nt
        "ucomiss xmm0, xmm1",
        "je .imnt_raw",
        "jb .imnt_fwd",
        # Wraparound handling: cur_nt < prev_nt (e.g. loop 280 -> 0)
        "movss xmm2, DWORD PTR [eax+0x4]",  # max
        "subss xmm2, DWORD PTR [eax]",      # span = max - min
        "xorps xmm3, xmm3",
        "comiss xmm2, xmm3",
        "jbe .imnt_raw",
        "addss xmm1, xmm2",
        "subss xmm1, xmm0",
        "comiss xmm1, DWORD PTR [f_q_ten]",
        "ja .imnt_raw",
        "mulss xmm1, DWORD PTR [G_UI_ALPHA]",
        "addss xmm0, xmm1",
        "comiss xmm0, DWORD PTR [eax+0x4]",
        "jb .imnt_store",
        "subss xmm0, xmm2",
        "jmp .imnt_store",
        ".imnt_fwd:",
        "subss xmm1, xmm0",
        "comiss xmm1, DWORD PTR [f_q_ten]",
        "ja .imnt_raw",
        "mulss xmm1, DWORD PTR [G_UI_ALPHA]",
        "addss xmm0, xmm1",
        ".imnt_store:",
        "movss DWORD PTR [ebp+0x8], xmm0",
        "pop edx",
        "pop ecx",
        "ret",

        # msScrollSmooth(ecx, edi=MenuScreen*, eax): updates [ebp+0x8]=eax-0xb, esi=ecx-smoothScroll
        "msScrollSmooth:",
        "push ecx",
        "sub eax, 0xb",
        "mov DWORD PTR [ebp+0x8], eax",
        "mov eax, DWORD PTR [edi+0xc]",
        "mov esi, ecx",
        "cmp edi, DWORD PTR [PTR_MENUSCREEN]",
        "jne .mss_raw",
        "mov edx, DWORD PTR [G_MS_PREV_C]",
        "mov ecx, eax",
        "sub ecx, edx",
        "jl .mss_raw",
        "cmp ecx, 20",
        "jg .mss_raw",
        "cvtsi2ss xmm0, edx",
        "cvtsi2ss xmm1, ecx",
        "mulss xmm1, DWORD PTR [G_UI_ALPHA]",
        "addss xmm0, xmm1",
        "cvtss2si eax, xmm0",
        ".mss_raw:",
        "sub esi, eax",
        "pop ecx",
        "ret",

        # lsScrollSmooth(eax=row, edi=LevelSelect*) -> eax = row*45 - smoothScroll
        # Preserves ebx, ecx, esi, edi, ebp.
        "lsScrollSmooth:",
        "push ecx",
        "imul eax, eax, 0x2d",
        "mov ecx, DWORD PTR [edi+0x50]",
        "cmp DWORD PTR [G_SUB_PTR], edi",
        "jne .lss_sub",
        "mov edx, DWORD PTR [G_SUB_PREV50]",
        "cmp ecx, edx",
        "je .lss_sub",
        "sub ecx, edx",
        "cvtsi2ss xmm0, edx",
        "cvtsi2ss xmm1, ecx",
        "mulss xmm1, DWORD PTR [G_UI_ALPHA]",
        "addss xmm0, xmm1",
        "cvtss2si ecx, xmm0",
        ".lss_sub:",
        "sub eax, ecx",
        "pop ecx",
        "ret",
    ]
    code_q, syms_q = link_block_with_symbols(BLOCK_Q_START, asm_q, BLOCK_Q_END - BLOCK_Q_START, all_syms)
    put_bytes(buf, BLOCK_Q_START, code_q, budget=BLOCK_Q_END - BLOCK_Q_START)
    all_syms.update(syms_q)

    # ------------------------------------------------------------------
    # 4. Inline hooks (each guarded with exact expected bytes + int3 padding)
    # ------------------------------------------------------------------

    # 4a0. PauseMenu Page 2 Confirm horizontal X slide & skip guard
    _check_unmodified(buf, 0x4ADF74, ["cmp DWORD PTR [edi+0x4c], ebx", "je 0x4ae25b"], 9)
    put_bytes(
        buf, 0x4ADF74,
        assemble_at(0x4ADF74, [f"jmp {hex(all_syms['pauseP2Skip'])}"]).ljust(9, b"\xcc"),
        budget=9,
    )
    _check_unmodified(buf, 0x4ADF88, ["mov ecx, DWORD PTR [edi+0x4c]", "movss xmm1, DWORD PTR ds:0x577c74"], 11)
    put_bytes(
        buf, 0x4ADF88,
        assemble_at(0x4ADF88, [f"call {hex(all_syms['pauseP2X'])}"]).ljust(20, b"\x90"),
        budget=20,
    )

    # 4a1. ModelNode::render timer (0x425715, 6B), MenuScreen marquee (0x49c52b, 11B), LevelSelect scroll (0x4b80a3, 6B)
    _check_unmodified(buf, 0x425715, ["fld DWORD PTR [eax+0x10]", "fstp DWORD PTR [ebp+0x8]"], 6)
    put_bytes(
        buf, 0x425715,
        assemble_at(0x425715, [f"call {hex(all_syms['interpModelNodeT'])}", "nop"], 6),
        budget=6,
    )
    _check_unmodified(buf, 0x49C52D, ["sub esi, DWORD PTR [edi+0xc]", "sub eax, 0xb", "mov DWORD PTR [ebp+0x8], eax"], 9)
    put_bytes(
        buf, 0x49C52B,
        assemble_at(0x49C52B, [f"call {hex(all_syms['msScrollSmooth'])}"]).ljust(11, b"\x90"),
        budget=11,
    )
    _check_unmodified(buf, 0x4B80A3, ["imul eax, eax, 0x2d", "sub eax, DWORD PTR [edi+0x50]"], 6)
    put_bytes(
        buf, 0x4B80A3,
        assemble_at(0x4B80A3, [f"call {hex(all_syms['lsScrollSmooth'])}", "nop"], 6),
        budget=6,
    )

    # 4a2. Submenu selection bar vertical Y slides (MainMenu, ModeSelect, s_4b28b0, s_4b1aa0)
    _check_unmodified(buf, 0x4B9901, ["mov eax, DWORD PTR [edi+0x8]", "mov DWORD PTR [ebp-0x54], 0"], 10)
    put_bytes(
        buf, 0x4B9901,
        assemble_at(0x4B9901, [
            "mov ecx, edi",
            "mov edx, 0",
            f"call {hex(all_syms['calcSubSlideY'])}",
            "jmp 0x4b99ab",
        ]).ljust(0x4B99AB - 0x4B9901, b"\xcc"),
        budget=0x4B99AB - 0x4B9901,
    )
    _check_unmodified(buf, 0x4B8E93, ["mov eax, DWORD PTR [ebx+0x8]", "test eax, eax"], 5)
    put_bytes(
        buf, 0x4B8E93,
        assemble_at(0x4B8E93, [
            "push edi",
            "mov ecx, ebx",
            "mov edx, 1",
            f"call {hex(all_syms['calcSubSlideY'])}",
            "pop edi",
            "mov ecx, eax",
            "jmp 0x4b8f4a",
        ]).ljust(0x4B8F4A - 0x4B8E93, b"\xcc"),
        budget=0x4B8F4A - 0x4B8E93,
    )
    _check_unmodified(buf, 0x4B2901, ["mov ebx, DWORD PTR [edi+0x8]", "add esp, 0x14"], 6)
    put_bytes(
        buf, 0x4B2907,
        assemble_at(0x4B2907, [
            "mov ecx, edi",
            "mov edx, 2",
            f"call {hex(all_syms['calcSubSlideY'])}",
            "jmp 0x4b299d",
        ]).ljust(0x4B299D - 0x4B2907, b"\xcc"),
        budget=0x4B299D - 0x4B2907,
    )
    _check_unmodified(buf, 0x4B1AC4, ["mov eax, DWORD PTR [ebx+0x8]", "mov DWORD PTR [ebp-0x68], 0"], 10)
    put_bytes(
        buf, 0x4B1AC4,
        assemble_at(0x4B1AC4, [
            "mov ecx, ebx",
            "mov edx, 3",
            f"call {hex(all_syms['calcSubSlideY'])}",
            "mov ecx, eax",
            "jmp 0x4b1b84",
        ]).ljust(0x4B1B84 - 0x4B1AC4, b"\xcc"),
        budget=0x4B1B84 - 0x4B1AC4,
    )

    # 4a3. Smooth rainbow highlights across MenuScreen submenus and PauseMenu
    rb_fn = hex(all_syms["getSmoothMenuRainbow"])
    rb_ptr = hex(BSS_G_RB_TMP)
    menu_rainbow_sites = [
        (0x4B9B06, ["mov ecx, DWORD PTR [ebp-0x68]"], 0x4B9B21 - 0x4B9B06,
         ["mov ebx, eax", "mov eax, DWORD PTR [ebp-0x68]", f"call {rb_fn}"]),
        (0x4B9B8B, ["mov eax, 0x4ec4ec4f", "mul ebx"], 0x4B9BA8 - 0x4B9B8B,
         ["mov eax, ebx", f"call {rb_fn}", "mov ecx, eax", f"mov eax, {rb_ptr}"]),
        (0x4B9CA2, ["mov eax, 0x4ec4ec4f", "mul ebx"], 0x4B9CB8 - 0x4B9CA2,
         ["mov eax, ebx", f"call {rb_fn}", "mov ecx, eax"]),
        (0x4B9002, ["mov ecx, DWORD PTR [ebp-0x68]", "mov eax, 0x4ec4ec4f"], 0x4B9022 - 0x4B9002,
         ["mov eax, DWORD PTR [ebp-0x68]", f"call {rb_fn}", "mov edx, eax", f"mov edi, {rb_ptr}"]),
        (0x4B90E3, ["mov ecx, DWORD PTR [ebp-0x68]"], 0x4B90FE - 0x4B90E3,
         ["mov ebx, eax", "mov eax, DWORD PTR [ebp-0x68]", f"call {rb_fn}", "mov ecx, eax"]),
        (0x4B9171, ["mov ecx, DWORD PTR [ebp-0x68]", "mov eax, 0x4ec4ec4f"], 0x4B918A - 0x4B9171,
         ["mov eax, DWORD PTR [ebp-0x68]", f"call {rb_fn}", "mov edx, eax"]),
        (0x4B9238, ["mov ecx, DWORD PTR [ebp-0x68]", "mov eax, 0x4ec4ec4f"], 0x4B9251 - 0x4B9238,
         ["mov eax, DWORD PTR [ebp-0x68]", f"call {rb_fn}"]),
        (0x4B7BF2, ["mov ecx, DWORD PTR [ebp-0x54]", "mov eax, 0x4ec4ec4f"], 0x4B7C12 - 0x4B7BF2,
         ["mov eax, DWORD PTR [ebp-0x54]", f"call {rb_fn}", f"mov ebx, {rb_ptr}"]),
        (0x4B7D4D, ["mov eax, 0x4ec4ec4f", "mul ecx"], 0x4B7D63 - 0x4B7D4D,
         ["mov eax, ecx", f"call {rb_fn}", "mov edx, eax"]),
        (0x4B7E1B, ["mov ecx, DWORD PTR [ebp-0x54]"], 0x4B7E34 - 0x4B7E1B,
         ["mov eax, DWORD PTR [ebp-0x54]", f"call {rb_fn}"]),
        (0x4B3E47, ["mov eax, 0x4ec4ec4f", "mul edi"], 0x4B3E6B - 0x4B3E47,
         ["add esp, 0x18", "mov eax, edi", f"call {rb_fn}", f"mov edi, {rb_ptr}", "cmp DWORD PTR [ebx+0x4], 0x8"]),
        (0x4AF6E1, ["mov ecx, DWORD PTR [ebp-0xa4]", "mov eax, 0x4ec4ec4f"], 0x4AF704 - 0x4AF6E1,
         ["add esp, 0x18", "mov eax, DWORD PTR [ebp-0xa4]", f"call {rb_fn}", f"mov eax, {rb_ptr}", "cmp DWORD PTR [edi+0x4], 0x8"]),
        (0x4B5813, ["mov eax, 0x4ec4ec4f", "mul edi"], 0x4B5832 - 0x4B5813,
         ["mov eax, edi", f"call {rb_fn}", "mov ecx, eax", f"mov ebx, {rb_ptr}"]),
        (0x4B58D9, ["mul edi", "shr edx, 2"], 0x4B58EC - 0x4B58D9,
         ["mov eax, edi", f"call {rb_fn}", "mov ecx, eax"]),
        (0x4B5957, ["mul edi", "shr edx, 2"], 0x4B596A - 0x4B5957,
         ["mov eax, edi", f"call {rb_fn}", "mov ecx, eax"]),
        (0x4AD2D8, ["mov eax, DWORD PTR [ebp-0x54]"], 0x4AD2EF - 0x4AD2D8,
         ["mov eax, DWORD PTR [ebp-0x54]", "lea eax, [eax+edi+3]", f"call {rb_fn}", "mov edx, eax"]),
        (0x4AE11C, ["mov eax, DWORD PTR [ebp-0x54]"], 0x4AE132 - 0x4AE11C,
         ["mov eax, DWORD PTR [ebp-0x54]", "add eax, 3", f"call {rb_fn}", "mov edx, eax"]),
        (0x4AE14D, ["mov eax, DWORD PTR [ebp-0x54]"], 0x4AE163 - 0x4AE14D,
         ["mov eax, DWORD PTR [ebp-0x54]", "add eax, 3", f"call {rb_fn}", "mov edx, eax"]),
        (0x4AE27C, ["mov eax, DWORD PTR [ebp-0x54]", "inc eax"], 0x4AE2A5 - 0x4AE27C,
         ["mov eax, DWORD PTR [ebp-0x54]", "inc eax", f"call {rb_fn}", "add esp, 0x14",
          "push 0x44", "push ebx", "push ebx", "push 0x3000", "mov BYTE PTR [ebp-0x3d], 0xff",
          "push 0x3000", "mov edx, eax"]),
        (0x4AE2F9, ["mov eax, DWORD PTR [ebp-0x54]"], 0x4AE324 - 0x4AE2F9,
         ["mov eax, DWORD PTR [ebp-0x54]", "add eax, 2", f"call {rb_fn}",
          "push 0x44", "push ebx", "push ebx", "push 0x8000", "mov BYTE PTR [ebp-0x3d], 0xff",
          "push 0x8000", "add edi, 0x24", "mov edx, eax"]),
        (0x4AE39C, ["mov eax, DWORD PTR [ebp-0x54]"], 0x4AE3B4 - 0x4AE39C,
         ["mov eax, DWORD PTR [ebp-0x54]", "sub eax, ebx", f"call {rb_fn}",
          "mov edx, eax", "mov edi, DWORD PTR [ebp-0x60]"]),
    ]
    for va, guard, span, repl in menu_rainbow_sites:
        _check_unmodified(buf, va, guard)
        put_bytes(buf, va, assemble_at(va, repl).ljust(span, b"\x90"), budget=span)

    # 4a. ScreenMgr::savePrev (0x481303..0x481309, 6B) & ScreenMgr::interpolate (0x48140b..0x481413, 8B)
    _check_unmodified(buf, 0x481303, ["mov eax, DWORD PTR [esi+0x330]"], 6)
    put_bytes(
        buf, 0x481303,
        assemble_at(0x481303, [f"call {hex(all_syms['onSavePrev'])}", "nop"], 6),
        budget=6,
    )

    _check_unmodified(buf, 0x48140B, ["call eax", "pop edi", "pop esi", "pop ebp", "ret 4"], 8)
    put_bytes(
        buf, 0x48140B,
        assemble_at(0x48140B, [f"jmp {hex(all_syms['onInterpolate'])}", "nop", "nop", "nop"], 8),
        budget=8,
    )

    # 4b. Submenu fade pct blocks (0x4af68a, 0x4b3df4, 0x4b57b1, 0x4b7b5f, 0x4b29a0, 0x4b8f4d, 0x4b99fc, 0x4b1b87)
    # Uniform reg-preserving blocks ending before `push esi`
    submenu_simple = [
        ("s_4af660", 0x4AF68A, 0x4AF6AB, "edi", 0, "mov eax, DWORD PTR [edi+0x4]"),
        ("s_4b3dd0", 0x4B3DF4, 0x4B3E15, "ebx", 0, "mov eax, DWORD PTR [ebx+0x4]"),
        ("s_4b5790", 0x4B57B1, 0x4B57D2, "ebx", 0, "mov eax, DWORD PTR [ebx+0x4]"),
        ("Options",  0x4B29A0, 0x4B2A13, "edi", 1, "mov eax, DWORD PTR [edi+0x4]"),
        ("s_4b8e40", 0x4B8F4D, 0x4B8FB1, "ebx", 1, "mov eax, DWORD PTR [ebx+0x4]"),
        ("s_4b98e0", 0x4B99FC, 0x4B9A5E, "edi", 2, "mov esi, 0x64"),
    ]
    for _name, start_va, end_va, reg, mode, guard_insn in submenu_simple:
        budget = end_va - start_va
        _check_unmodified(buf, start_va, [guard_insn])
        hook = assemble_at(
            start_va,
            [
                f"push {mode}",
                f"mov ecx, {reg}",
                f"call {hex(all_syms['calcSubFadePct'])}",
                "mov esi, eax",
                f"jmp {hex(end_va)}",
            ],
        )
        assert len(hook) <= budget
        put_bytes(buf, start_va, hook.ljust(budget, b"\xcc"), budget=budget)

    # s_4b7b30 (0x4b7b54..0x4b7b7e): also preserve `mov DWORD PTR [edi+0x68], esi`
    _check_unmodified(buf, 0x4B7B54, ["mov eax, DWORD PTR [edi+0x4]", "mov DWORD PTR [edi+0x68], esi"])
    hook_7b = assemble_at(
        0x4B7B54,
        [
            "mov DWORD PTR [edi+0x68], esi",
            "push 3",
            "mov ecx, edi",
            f"call {hex(all_syms['calcSubFadePct'])}",
            "mov esi, eax",
            "jmp 0x4b7b7e",
        ],
    )
    put_bytes(buf, 0x4B7B54, hook_7b.ljust(0x4B7B7E - 0x4B7B54, b"\xcc"), budget=0x4B7B7E - 0x4B7B54)

    # s_4b1aa0 (0x4b1b87..0x4b1c1b): also manages [ebp-0x64] (0xffffff or 0) & [ebp-0x68] on nextState==0x10
    _check_unmodified(buf, 0x4B1B87, ["mov eax, DWORD PTR [ebx+0x4]", "mov esi, 0x64", "mov DWORD PTR [ebp-0x64], 0xffffff"])
    hook_1b = assemble_at(
        0x4B1B87,
        [
            "mov DWORD PTR [ebp-0x64], 0xffffff",
            "cmp DWORD PTR [ebx+0x4], 8",
            "jl .m1",
            "cmp DWORD PTR [ebx+0x8], 0",
            "jl .m1",
            "cmp DWORD PTR [ebx+0xc], 0x10",
            "jne .m1",
            "mov DWORD PTR [ebp-0x68], 0",
            "mov DWORD PTR [ebp-0x64], 0",
            "push 4",
            "jmp .do_call",
            ".m1:",
            "push 1",
            ".do_call:",
            "mov ecx, ebx",
            f"call {hex(all_syms['calcSubFadePct'])}",
            "mov esi, eax",
            "jmp 0x4b1c1b",
        ],
    )
    put_bytes(buf, 0x4B1B87, hook_1b.ljust(0x4B1C1B - 0x4B1B87, b"\xcc"), budget=0x4B1C1B - 0x4B1B87)

    # 4c. PauseMenu vertical slide (0x4ad147..0x4ad1a0, 89B) -> ebx=0, esi=slide_y
    _check_unmodified(buf, 0x4AD147, ["mov edx, DWORD PTR [edi+0x4]"])
    hook_ps = assemble_at(
        0x4AD147,
        [
            "mov ecx, edi",
            f"call {hex(all_syms['pauseSmoothSlideY'])}",
            "xor ebx, ebx",
            "mov esi, eax",
            "jmp 0x4ad1a0",
        ],
    )
    put_bytes(buf, 0x4AD147, hook_ps.ljust(0x4AD1A0 - 0x4AD147, b"\xcc"), budget=0x4AD1A0 - 0x4AD147)

    # PauseMenu Page 0 skip check (0x4ad1dd..0x4ad1ea, 13B): skip only once prev4c==320 && cur4c==320
    _check_unmodified(buf, 0x4AD1DD, ["cmp DWORD PTR [edi+0x4c], 0x140", "je 0x4ad44c"], 13)
    # 13B exact budget -> jump to a small inline/tail stub or inline cmp directly!
    # Wait: inline needs `cmp [edi+0x4c], 0x140; jne 0x4ad1ea; cmp [G_SUB_PTR], edi; jne 0x4ad44c; cmp [G_SUB_PREV4C], 0x140; je 0x4ad44c` = 7+2+7+6+10+6 > 13B, so place 2 tiny stubs at end of Block J?
    # Wait let's check remaining Block J space first — instead we can patch `0x4ad1dd..0x4ad1ea` -> tail stub if needed, OR verify spare bytes in `pause_render` / Block J!
    # Wait: `0x4ad1fa..0x4ad20e` (20B) replaces `mov ecx,[eax+0x4c]; ... cvtsi2ss xmm2, ecx` with:
    #   mov ecx, eax; call pauseSmoothX; (7B) leaves `xmm1` / `xmm0` / `edx`?
    # Check: `0x4ad1f4` set `xmm0 = 0`, `0x4ad1fd` set `xmm1 = 1.0f`, `0x4ad20a` set `xmm2`.
    # Our `pauseSmoothX` ONLY touches `eax, edx, xmm2, xmm3`, preserving `xmm0, xmm1`!
    _check_unmodified(buf, 0x4AD1FA, ["mov ecx, DWORD PTR [eax+0x4c]"])
    hook_p0x = assemble_at(
        0x4AD1FA,
        [
            "mov ecx, eax",
            f"call {hex(all_syms['pauseSmoothX'])}",
            "jmp 0x4ad20e",
        ],
    )
    put_bytes(buf, 0x4AD1FA, hook_p0x.ljust(0x4AD20E - 0x4AD1FA, b"\xcc"), budget=0x4AD20E - 0x4AD1FA)

    # PauseMenu Page 1 X translate (0x4ad48b..0x4ad4a9, 30B)
    _check_unmodified(buf, 0x4AD48B, ["mov eax, DWORD PTR [edi+0x4c]"])
    hook_p1x = assemble_at(
        0x4AD48B,
        [
            "mov ecx, edi",
            f"call {hex(all_syms['pauseSmoothX'])}",
            "lea ecx, [ebp-0xa4]",
            "push ecx",
            "mov ecx, DWORD PTR [ebp-0x50]",
            "jmp 0x4ad4a9",
        ],
    )
    put_bytes(buf, 0x4AD48B, hook_p1x.ljust(0x4AD4A9 - 0x4AD48B, b"\xcc"), budget=0x4AD4A9 - 0x4AD48B)

    # 4d. PauseConfirm (0x4ae793..0x4ae7e2, 79B) -> ebx = slide_y, edi = fade_pct
    _check_unmodified(buf, 0x4AE793, ["mov eax, DWORD PTR [esi+0x4]", "add esp, 0x14"])
    hook_pc = assemble_at(
        0x4AE793,
        [
            "add esp, 0x14",
            "mov ecx, esi",
            f"call {hex(all_syms['pauseConfirmSmooth'])}",
            "mov ebx, eax",
            "mov edi, edx",
            "jmp 0x4ae7e2",
        ],
    )
    put_bytes(buf, 0x4AE793, hook_pc.ljust(0x4AE7E2 - 0x4AE793, b"\xcc"), budget=0x4AE7E2 - 0x4AE793)

    # 4e. EndLevel flash (0x4a99de..0x4a9a11, 51B) & fade (0x4a9a5f..0x4a9a86, 39B)
    _check_unmodified(buf, 0x4A99DE, ["mov eax, DWORD PTR [esi+0x3c]", "add esp, 0x4"])
    hook_ef = assemble_at(
        0x4A99DE,
        [
            "add esp, 0x4",
            "cmp DWORD PTR [esi+0x3c], 5",
            "jg 0x4a9a5f",
            "mov ecx, esi",
            f"call {hex(all_syms['endFlashAlpha'])}",
            "mov cl, al",
            "jmp 0x4a9a0b",
        ],
    )
    put_bytes(buf, 0x4A99DE, hook_ef.ljust(0x4A9A0B - 0x4A99DE, b"\xcc"), budget=0x4A9A0B - 0x4A99DE)

    _check_unmodified(buf, 0x4A9A5F, ["mov eax, DWORD PTR [esi+0x40]", "push ebx", "push edi"])
    hook_efd = assemble_at(
        0x4A9A5F,
        [
            "push ebx",
            "push edi",
            "mov ecx, esi",
            f"call {hex(all_syms['endFadePct'])}",
            "mov ebx, eax",
            "jmp 0x4a9a86",
        ],
    )
    put_bytes(buf, 0x4A9A5F, hook_efd.ljust(0x4A9A86 - 0x4A9A5F, b"\xcc"), budget=0x4A9A86 - 0x4A9A5F)

    # 4f. WorldScreen full-screen flash (0x47b45c..0x47b4ea, 142B)
    _check_unmodified(buf, 0x47B45C, ["cmp DWORD PTR [edi+0x211c], 0", "jle 0x47b507"])
    hook_hf = assemble_at(
        0x47B45C,
        [
            f"call {hex(all_syms['hudFlashSmooth'])}",
            "test al, al",
            "je 0x47b507",
            "jmp 0x47b4ea",
        ],
    )
    put_bytes(buf, 0x47B45C, hook_hf.ljust(0x47B4EA - 0x47B45C, b"\xcc"), budget=0x47B4EA - 0x47B45C)

    # WorldScreen corner triangle rainbow + alpha pulse (0x47b531..0x47b5af, 126B)
    _check_unmodified(buf, 0x47B531, ["mov eax, DWORD PTR [edi+0x4]"])
    hook_hr = assemble_at(
        0x47B531,
        [
            f"call {hex(all_syms['hudRainbowSmooth'])}",
            "jmp 0x47b5af",
        ],
    )
    put_bytes(buf, 0x47B531, hook_hr.ljust(0x47B5AF - 0x47B531, b"\xcc"), budget=0x47B5AF - 0x47B531)

    # 4g. Toast::render fade (0x495377..0x4953c1, 74B)
    _check_unmodified(buf, 0x495377, ["cmp DWORD PTR [esi+0x34], 0", "jle 0x49540a"])
    hook_tst = assemble_at(
        0x495377,
        [
            f"call {hex(all_syms['toastAlphaSmooth'])}",
            "test al, al",
            "je 0x49540a",
            f"mov ecx, DWORD PTR [{hex(all_syms['TEXT_COL_BYTES'])}]",
            "mov DWORD PTR [ebp-0x4], ecx",
            "mov BYTE PTR [ebp-0x1], al",
            "push 0x18",
            "push 0x0",
            "jmp 0x4953c1",
        ],
    )
    put_bytes(buf, 0x495377, hook_tst.ljust(0x4953C1 - 0x495377, b"\xcc"), budget=0x4953C1 - 0x495377)

    # 4h. Minimap smooth draw (0x47b511..0x47b518, 7B) + rebuild/pause skip guards (in Block M)
    _check_unmodified(buf, 0x47B513, ["call 0x47a960"], 5)
    put_bytes(
        buf, 0x47B511,
        assemble_at(0x47B511, [f"call {hex(all_syms['minimapSmoothDraw'])}", "nop", "nop"], 7),
        budget=7,
    )

    _check_unmodified(buf, 0x47E66A, ["mov edx, DWORD PTR [ebx+0x2108]"], 6)
    put_bytes(buf, 0x47E66A, assemble_at(0x47E66A, [f"jmp {hex(all_syms['mmSkipIn'])}", "nop"], 6), budget=6)

    _check_unmodified(buf, 0x47E694, ["mov eax, DWORD PTR [ebx+0x2108]"], 6)
    put_bytes(buf, 0x47E694, assemble_at(0x47E694, [f"jmp {hex(all_syms['mmSkipOut'])}", "nop"], 6), budget=6)

    _check_unmodified(buf, 0x47E8D0, ["mov edi, DWORD PTR ds:0x5f9040"], 6)
    put_bytes(buf, 0x47E8D0, assemble_at(0x47E8D0, [f"call {hex(all_syms['mmForceVb'])}", "nop"], 6), budget=6)

    _check_unmodified(buf, 0x4AD1DD, ["cmp DWORD PTR [edi+0x4c], 0x140", "je 0x4ad44c"], 13)
    put_bytes(
        buf, 0x4AD1DD,
        assemble_at(0x4AD1DD, [f"jmp {hex(all_syms['pauseP0Skip'])}"]).ljust(13, b"\xcc"),
        budget=13,
    )

    _check_unmodified(buf, 0x4AD45F, ["cmp DWORD PTR [edi+0x4c], ebx", "je 0x4adf61"], 9)
    put_bytes(
        buf, 0x4AD45F,
        assemble_at(0x4AD45F, [f"jmp {hex(all_syms['pauseP1Skip'])}"]).ljust(9, b"\xcc"),
        budget=9,
    )

    if out_syms is not None:
        out_syms.update(all_syms)

    return {
        "Block I": (len(code_i), BLOCK_I_END - BLOCK_I_START),
        "Block J": (len(code_j), BLOCK_J_END - BLOCK_J_START),
        "Block K": (len(code_k), BLOCK_K_END - BLOCK_K_START),
        "Block L": (len(code_l), BLOCK_L_END - BLOCK_L_START),
        "Block M": (len(code_m), BLOCK_M_END - BLOCK_M_START),
        "Block N": (len(code_n), BLOCK_N_END - BLOCK_N_START),
        "Block O": (len(code_o), BLOCK_O_END - BLOCK_O_START),
        "Block P": (len(code_p), BLOCK_P_END - BLOCK_P_START),
        "Block Q": (len(code_q), BLOCK_Q_END - BLOCK_Q_START),
        "Block R": (len(code_r), BLOCK_R_END - BLOCK_R_START),
    }
