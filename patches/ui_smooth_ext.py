"""Extended 60/unlimited-FPS UI smoothing for EDGE PC.

Builds on ui_smooth_partial.py (sharing G_UI_ALPHA, G_JUST_TICKED, float
trackers, rainbow math, and ScreenMgr savePrev/interpolate hooks) to smooth the
remaining discrete 30 Hz UI menus, transitions, and timers:
  1. InstructionsMenu & ScoresMenu scrolling + ScoreSelect/Scores animations:
     - InstructionsMenu smooth wheel/arrow scroll ([+0x28]), instant drag snap,
       "s+" highlight rainbow, and Edge Time demo clock ([+0x34])
     - ScoresMenu smooth scroll ([+0x6c]), smooth card opening intro &
       front/back flip ([+0x74]), and smooth collected prism dot rainbow wave
     - ScoresSelectMenu 4 remaining PALETTE_RAINBOW highlights & Menu::drawButton
  2. LevelSelectMenu animations:
     - Smooth scrollbar thumb Y ([+0x58])
     - Smooth level info card fade & horizontal slide-in ([+0x28])
     - Smooth per-tile hover-leave flash decay ([+0x2c[i]])
     - Smooth 10 Hz completion badge rainbow, info-card prism/rank rainbow
  3. DebriefMenu (Level Complete) animations:
     - Continuous sub-tick reveal timer (G_EXT_EFF_A from [+0x44], 1x & 3x)
       driving all row reveals/fades, NEW RECORD zoom & marquee, Rank header
       slide-in, Rank card expand, Rank divider grow, Rank stamp, S/S+ rainbow,
       D..A triangle wave pulse, and action button quadratic alpha fade + rainbow
  4. Screen transition darkening:
     - ConfirmExitGameMenu YES blackout fade ([+0x18])
     - ModeSelect, LevelSelect, and DebriefMenu exit-to-background fades ([+0x8])
  5. ConfirmExitGameMenu (selecting "exit game"):
     - Smooth sidebar collapse & expand box height ([+0x4]/[+0x8])
     - Smooth element fade pct and all 5 rainbow highlight sites
  6. In-level "edge time" HUD counter:
     - Smooth projected screen (X, Y) + vertical HUD rise ([+0x80])
     - Smooth continuous centiseconds (00..99), rainbow + whole-second flash
       fade, and smooth MatrixStack per-second digit bounce (8.0 -> 5.0)
  7. CreditsMenu scroll (Options -> credits & post-completion ending):
     - Smooth sub-tick vertical scroll offset ([+0x4c] + alpha * [+0x50])
       via CreditsMenu vtable[2] wrapper (creditsRenderWrap at 0x58b9d8)
"""

import struct

from patches.common import assemble_at, link_block_with_symbols, put_bytes, va2off
from patches.ui_smooth_partial import _check_unmodified

# Verified zero-reference unused function clusters in .text (find_free_slots.py --check-refs)
BLOCK_S_START = 0x453520
BLOCK_S_END   = 0x4537A0  # 640 B: onSavePrev_Ext & onInterpolate_Ext dispatch

BLOCK_T_START = 0x45DC20
BLOCK_T_END   = 0x45DF90  # 880 B: Scroll wrappers, ScoresMenu flip & prism dots

BLOCK_U_START = 0x510D30
BLOCK_U_END   = 0x510FE0  # 688 B: Exit fades, drawButton rainbow, ConfirmExitGameMenu

BLOCK_V_START = 0x44BAD0
BLOCK_V_END   = 0x44BD20  # 592 B: LevelSelectMenu flash/badges/info-card

BLOCK_W_START = 0x511030
BLOCK_W_END   = 0x5114B0  # 1152 B: DebriefMenu colors, slide/box/bar & Rank pulse/rainbow

BLOCK_X_START = 0x4FAA80
BLOCK_X_END   = 0x4FACE0  # 608 B: DebriefMenu NEW RECORD, Rank stamp, action buttons

BLOCK_Y_START = 0x5423F0
BLOCK_Y_END   = 0x542750  # 864 B: WorldScreen HUD edge timer MatrixStack & coords

BLOCK_Z_START = 0x4FA5D0
BLOCK_Z_END   = 0x4FA8F0  # 800 B: WorldScreen HUD digits/flash & Instructions demo

# Unused page-tail .bss addresses (< 0x5FB000), continuing from 0x5FA960
BSS_G_EXT_SUB_PTR   = 0x5FA960
BSS_G_EXT_PREV_A    = 0x5FA964
BSS_G_EXT_LAST_A    = 0x5FA968
BSS_G_EXT_EFF_A     = 0x5FA96C
BSS_G_EXT_PREV_B    = 0x5FA970
BSS_G_EXT_LAST_B    = 0x5FA974
BSS_G_EXT_EFF_B     = 0x5FA978
BSS_G_EXT_WS_PTR    = 0x5FA97C
BSS_G_WS_PREV_EC    = 0x5FA980  # 2 bytes (slot 0, slot 1) + 2 pad
BSS_G_WS_PREV_ET0   = 0x5FA984
BSS_G_WS_PREV_ET1   = 0x5FA988
BSS_G_WS_PREV_ER0   = 0x5FA98C
BSS_G_WS_PREV_ER1   = 0x5FA990
BSS_G_WS_PREV_X0    = 0x5FA994
BSS_G_WS_PREV_Y0    = 0x5FA998
BSS_G_WS_PREV_X1    = 0x5FA99C
BSS_G_WS_PREV_Y1    = 0x5FA9A0
BSS_G_ET_REM_TMP    = 0x5FA9A4
BSS_G_ET_TICKS_TMP  = 0x5FA9A8
BSS_G_LS_FLASH_PREV = 0x5FA9AC  # 64B (0x5FA9AC..0x5FA9EC)
BSS_G_HUD_MAT_ORIG  = 0x5FA9F0  # 64B (0x5FA9F0..0x5FAA30)
BSS_G_END_CFG_IDX   = 0x5FAA30  # optional debug/scratch (4B)
BSS_G_TIME_FMT      = 0x5FAA34  # byte: 0 = default (0'00"00), 1 = modern (0:00.00); persisted by time_format.py
BSS_G_SKIP_INTRO    = 0x5FAA35  # byte: 0 = off, 1 = on; persisted by skip_intro.py

EXT_VARS = {
    "G_EXT_SUB_PTR":   BSS_G_EXT_SUB_PTR,
    "G_EXT_PREV_A":    BSS_G_EXT_PREV_A,
    "G_EXT_LAST_A":    BSS_G_EXT_LAST_A,
    "G_EXT_EFF_A":     BSS_G_EXT_EFF_A,
    "G_EXT_PREV_B":    BSS_G_EXT_PREV_B,
    "G_EXT_LAST_B":    BSS_G_EXT_LAST_B,
    "G_EXT_EFF_B":     BSS_G_EXT_EFF_B,
    "G_EXT_WS_PTR":    BSS_G_EXT_WS_PTR,
    "G_WS_PREV_EC":    BSS_G_WS_PREV_EC,
    "G_WS_PREV_ET0":   BSS_G_WS_PREV_ET0,
    "G_WS_PREV_ET1":   BSS_G_WS_PREV_ET1,
    "G_WS_PREV_ER0":   BSS_G_WS_PREV_ER0,
    "G_WS_PREV_ER1":   BSS_G_WS_PREV_ER1,
    "G_WS_PREV_X0":    BSS_G_WS_PREV_X0,
    "G_WS_PREV_Y0":    BSS_G_WS_PREV_Y0,
    "G_WS_PREV_X1":    BSS_G_WS_PREV_X1,
    "G_WS_PREV_Y1":    BSS_G_WS_PREV_Y1,
    "G_ET_REM_TMP":    BSS_G_ET_REM_TMP,
    "G_ET_TICKS_TMP":  BSS_G_ET_TICKS_TMP,
    "G_LS_FLASH_PREV": BSS_G_LS_FLASH_PREV,
    "G_HUD_MAT_ORIG":  BSS_G_HUD_MAT_ORIG,
    "G_TIME_FMT":      BSS_G_TIME_FMT,
    "VT_INSTRUCTIONS": 0x58D2FC,
    "VT_SCORES":       0x58D118,
    "VT_DEBRIEF":      0x58CE58,
    "VT_CONFIRMEXIT":  0x58CC18,
    "FN_SCORES_RND":   0x4AF660,
    "FN_MSTACK_SET2":  0x41F520,
    "BG_RGB_ADDR":     0x5F88E0,
}


def apply_ui_smooth_ext(buf: bytearray, base_syms: dict[str, int]) -> dict[str, tuple[int, int]]:
    """Install extended 60/unlimited-FPS UI smoothing patches."""
    for s, e in [
        (BLOCK_S_START, BLOCK_S_END),
        (BLOCK_T_START, BLOCK_T_END),
        (BLOCK_U_START, BLOCK_U_END),
        (BLOCK_V_START, BLOCK_V_END),
        (BLOCK_W_START, BLOCK_W_END),
        (BLOCK_X_START, BLOCK_X_END),
        (BLOCK_Y_START, BLOCK_Y_END),
        (BLOCK_Z_START, BLOCK_Z_END),
    ]:
        put_bytes(buf, s, b"\xcc" * (e - s), budget=e - s)

    syms = dict(base_syms)
    syms.update(EXT_VARS)

    # ------------------------------------------------------------------
    # 1. Block S (0x543A10..0x543B20, 272B): Snapshot/interp & scroll wrappers
    # ------------------------------------------------------------------
    asm_s = [
        ".globl onSavePrev_Ext, onInterpolate_Ext",

        # snap_sub_state(eax = sub*, ecx = vtable): snapshots submenu ext fields -> PREV_*
        # Preserves ebx, esi, edi, ebp.
        "snap_sub_state:",
        "mov DWORD PTR [G_EXT_SUB_PTR], eax",
        "cmp ecx, VT_INSTRUCTIONS",
        "je .ss_pair28_34",
        "cmp ecx, VT_SCORES",
        "je .ss_pair6c_74",
        "cmp ecx, VT_LEVELSELECT",
        "je .ss_ls",
        "cmp ecx, VT_DEBRIEF",
        "jne .ss_chk_ceg",
        "mov edx, DWORD PTR [eax+0x44]",
        "jmp .ss_store_a",
        ".ss_chk_ceg:",
        "cmp ecx, VT_CONFIRMEXIT",
        "jne .ss_ret",
        "mov edx, DWORD PTR [eax+0x18]",
        ".ss_store_a:",
        "mov DWORD PTR [G_EXT_PREV_A], edx",
        ".ss_ret:",
        "ret",
        ".ss_pair28_34:",
        "mov edx, DWORD PTR [eax+0x28]",
        "mov DWORD PTR [G_EXT_PREV_A], edx",
        "mov edx, DWORD PTR [eax+0x34]",
        "mov DWORD PTR [G_EXT_PREV_B], edx",
        "ret",
        ".ss_pair6c_74:",
        "mov edx, DWORD PTR [eax+0x6c]",
        "mov DWORD PTR [G_EXT_PREV_A], edx",
        "mov edx, DWORD PTR [eax+0x74]",
        "mov DWORD PTR [G_EXT_PREV_B], edx",
        "ret",
        ".ss_ls:",
        "mov edx, DWORD PTR [eax+0x58]",
        "mov DWORD PTR [G_EXT_PREV_A], edx",
        "mov edx, DWORD PTR [eax+0x28]",
        "mov DWORD PTR [G_EXT_PREV_B], edx",
        "mov edx, DWORD PTR [eax+0x2c]",
        "test edx, edx",
        "je .ss_ret",
        "mov ecx, DWORD PTR [eax+0x18]",
        "cmp ecx, 64",
        "jbe .ss_ls_cnt",
        "mov ecx, 64",
        ".ss_ls_cnt:",
        "test ecx, ecx",
        "je .ss_ret",
        "push esi",
        "xor esi, esi",
        ".ss_ls_lp:",
        "mov eax, DWORD PTR [edx+esi*4]",
        "mov BYTE PTR [G_LS_FLASH_PREV+esi], al",
        "inc esi",
        "cmp esi, ecx",
        "jb .ss_ls_lp",
        "pop esi",
        "ret",

        # Hooked from Block I sp_ms_callsite (esi = ScreenMgr*)
        "onSavePrev_Ext:",
        "call onSavePrev_MS",
        "mov eax, DWORD PTR [G_SUB_PTR]",
        "test eax, eax",
        "je .spe_ws",
        "mov ecx, DWORD PTR [eax]",
        "call snap_sub_state",
        ".spe_ws:",
        "mov eax, DWORD PTR [G_WS_PTR]",
        "test eax, eax",
        "je .spe_ret",
        "mov DWORD PTR [G_EXT_WS_PTR], eax",
        "mov dx, WORD PTR [eax+0x6c]",
        "mov WORD PTR [G_WS_PREV_EC], dx",
        "push esi",
        "push edi",
        "lea esi, [eax+0x78]",
        "mov edi, G_WS_PREV_ET0",
        "mov ecx, 4",
        "rep movsd",
        "lea esi, [eax+0x2148]",
        "mov cl, 4",
        "rep movsd",
        "pop edi",
        "pop esi",
        ".spe_ret:",
        "ret",

        # ext_upd_pair(eax = cur, edx = off 0/12, cl = snap_dec, ch = snap_jump2)
        "ext_upd_pair:",
        "push ebx",
        "mov ebx, DWORD PTR [G_EXT_PREV_A+edx]",
        "cmp BYTE PTR [G_JUST_TICKED], 0",
        "je .eup_nt",
        "mov DWORD PTR [G_EXT_LAST_A+edx], eax",
        "test cl, cl",
        "je .eup_j2",
        "cmp eax, ebx",
        "jl .eup_snap",
        ".eup_j2:",
        "test ch, ch",
        "je .eup_mix",
        "push eax",
        "sub eax, ebx",
        "cmp eax, 2",
        "jg .eup_sp",
        "cmp eax, -2",
        "jl .eup_sp",
        "pop eax",
        "jmp .eup_mix",
        ".eup_sp:",
        "pop eax",
        ".eup_snap:",
        "mov ebx, eax",
        "mov DWORD PTR [G_EXT_PREV_A+edx], eax",
        "jmp .eup_mix",
        ".eup_nt:",
        "cmp eax, DWORD PTR [G_EXT_LAST_A+edx]",
        "je .eup_mix",
        "mov DWORD PTR [G_EXT_LAST_A+edx], eax",
        "jmp .eup_snap",
        ".eup_mix:",
        "cvtsi2ss xmm0, ebx",
        "sub eax, ebx",
        "cvtsi2ss xmm1, eax",
        "mulss xmm1, DWORD PTR [G_UI_ALPHA]",
        "addss xmm0, xmm1",
        "movss DWORD PTR [G_EXT_EFF_A+edx], xmm0",
        "pop ebx",
        "ret",

        # Hooked from Block I ip_ms_callsite (esi = ScreenMgr*)
        "onInterpolate_Ext:",
        "call onInterpolate_MS",
        "call get_active_sub",
        "test eax, eax",
        "je .ipe_ret",
        "cmp DWORD PTR [G_EXT_SUB_PTR], eax",
        "je .ipe_have",
        "push eax",
        "push ecx",
        "call snap_sub_state",
        "pop ecx",
        "pop eax",
        ".ipe_have:",
        "push ebx",
        "mov ebx, eax",
        "cmp ecx, VT_INSTRUCTIONS",
        "je .ipe_instr",
        "cmp ecx, VT_SCORES",
        "je .ipe_scores",
        "cmp ecx, VT_LEVELSELECT",
        "je .ipe_ls",
        "cmp ecx, VT_DEBRIEF",
        "je .ipe_deb",
        "cmp ecx, VT_CONFIRMEXIT",
        "jne .ipe_pop",
        "mov eax, DWORD PTR [ebx+0x18]",
        "xor edx, edx",
        "xor ecx, ecx",
        "call ext_upd_pair",
        "jmp .ipe_pop",
        ".ipe_deb:",
        "mov eax, DWORD PTR [ebx+0x44]",
        "xor edx, edx",
        "xor ecx, ecx",
        "call ext_upd_pair",
        "jmp .ipe_pop",
        ".ipe_instr:",
        "mov eax, DWORD PTR [ebx+0x28]",
        "xor edx, edx",
        "xor ecx, ecx",
        "call ext_upd_pair",
        "mov eax, DWORD PTR [ebx+0x34]",
        "mov edx, 12",
        "mov cl, 1",
        "xor ch, ch",
        "call ext_upd_pair",
        "jmp .ipe_pop",
        ".ipe_scores:",
        "mov eax, DWORD PTR [ebx+0x6c]",
        "xor edx, edx",
        "xor ecx, ecx",
        "call ext_upd_pair",
        "mov eax, DWORD PTR [ebx+0x74]",
        "mov edx, 12",
        "xor cl, cl",
        "mov ch, 1",
        "call ext_upd_pair",
        "jmp .ipe_pop",
        ".ipe_ls:",
        "mov eax, DWORD PTR [ebx+0x58]",
        "xor edx, edx",
        "xor ecx, ecx",
        "call ext_upd_pair",
        "mov eax, DWORD PTR [ebx+0x28]",
        "mov edx, 12",
        "mov ecx, 0x0001",  # cl=1 (snap dec), ch=0
        "call ext_upd_pair",
        ".ipe_pop:",
        "pop ebx",
        ".ipe_ret:",
        "ret",
    ]
    code_s, syms_s = link_block_with_symbols(BLOCK_S_START, asm_s, BLOCK_S_END - BLOCK_S_START, syms)
    put_bytes(buf, BLOCK_S_START, code_s, budget=BLOCK_S_END - BLOCK_S_START)
    syms.update(syms_s)

    # ------------------------------------------------------------------
    # 2. Block T (0x4F38B0..0x4F39B0, 256B): Scroll wrappers, ScoresMenu flip/dots
    # ------------------------------------------------------------------
    asm_t = [
        ".globl scoresRenderWrap, instrSubScrollY, lsThumbEdx, lsThumbEcx",
        ".globl scoresCardFlip, scoresDotRainbow, f_8f",
        "f_200f:   .float 200.0",
        "f_512f:   .float 512.0",
        "f_2048f:  .float 2048.0",
        "f_4096f:  .float 4096.0",
        "f_m1f:    .float -1.0",
        "f_8f:     .float 8.0",
        "f_91f:    .float 91.0",
        "f_99f:    .float 99.0",
        "f_108f:   .float 108.0",
        "f_191f:   .float 191.0",
        "f_199f:   .float 199.0",

        # VT_SCORES[2] wrapper: swaps [this+0x6c] to rounded G_EXT_EFF_A across render
        "scoresRenderWrap:",
        "push esi",
        "push edi",
        "mov esi, ecx",
        "mov edi, DWORD PTR [esi+0x6c]",
        "cmp DWORD PTR [G_EXT_SUB_PTR], esi",
        "jne .srw_go",
        "cvtss2si eax, DWORD PTR [G_EXT_EFF_A]",
        "mov DWORD PTR [esi+0x6c], eax",
        ".srw_go:",
        "push DWORD PTR [esp+12]",
        "call FN_SCORES_RND",
        "mov DWORD PTR [esi+0x6c], edi",
        "pop edi",
        "pop esi",
        "ret 4",

        # InstructionsMenu 0x4b4246..0x4b424c: sub edi, smoothScrollY (preserve eax, ecx, ebx, esi, ebp)
        "instrSubScrollY:",
        "mov edx, DWORD PTR [ebp-0x44]",
        "cmp DWORD PTR [G_EXT_SUB_PTR], edx",
        "jne .iss_raw",
        "cvtss2si edx, DWORD PTR [G_EXT_EFF_A]",
        "sub edi, edx",
        "ret",
        ".iss_raw:",
        "sub edi, DWORD PTR [edx+0x28]",
        "ret",

        # LevelSelect 0x4b8453..0x4b8459: edx = thumbY, lea ecx, [ebp-0x40]
        "lsThumbEdx:",
        "cmp DWORD PTR [G_EXT_SUB_PTR], edi",
        "jne .lte_raw",
        "cvtss2si edx, DWORD PTR [G_EXT_EFF_A]",
        "lea ecx, [ebp-0x40]",
        "ret",
        ".lte_raw:",
        "mov edx, DWORD PTR [edi+0x58]",
        "lea ecx, [ebp-0x40]",
        "ret",

        # LevelSelect 0x4b84a1..0x4b84a7: ecx = thumbY, sar edx, 5
        "lsThumbEcx:",
        "cmp DWORD PTR [G_EXT_SUB_PTR], edi",
        "jne .lce_raw",
        "cvtss2si ecx, DWORD PTR [G_EXT_EFF_A]",
        "sar edx, 5",
        "ret",
        ".lce_raw:",
        "mov ecx, DWORD PTR [edi+0x58]",
        "sar edx, 5",
        "ret",

        # ScoresMenu 0x4b0a6b..0x4b0aa9: [ebp-0x88] = RGBA(frame - dot_idx - card_idx), edx = [ebp-0x9c]
        "scoresDotRainbow:",
        "mov eax, DWORD PTR [ebp-0xa4]",
        "sub eax, ecx",
        "sub eax, DWORD PTR [ebp-0x50]",
        "call getSmoothMenuRainbow",
        "bswap eax",
        "shr eax, 8",
        "or eax, 0xff000000",
        "mov DWORD PTR [ebp-0xb4], eax",
        "mov DWORD PTR [ebp-0x88], eax",
        "mov edx, DWORD PTR [ebp-0x9c]",
        "ret",

        # ScoresMenu card flip 0x4afdb0..0x4afe40
        # Reads [ebp-0x5c]=this, [ebp-0x50]=cardIdx; sets [ebp-0x91], ebx, [ebp-0x70]
        "scoresCardFlip:",
        "mov edx, DWORD PTR [ebp-0x5c]",
        "mov esi, DWORD PTR [edx+0x74]",
        "mov ecx, DWORD PTR [ebp-0x50]",
        "cmp DWORD PTR [G_EXT_SUB_PTR], edx",
        "jne .scf_raw",
        "cvtsi2ss xmm1, ecx",
        "movss xmm0, DWORD PTR [G_EXT_EFF_B]",
        "subss xmm0, xmm1",         # xmm0 = diff_f = eff_B - cardIdx
        "xorps xmm2, xmm2",
        "comiss xmm0, xmm2",
        "jae .scf_pos",
        # Opening intro wave (diff_f < 0.0, front face):
        #   -5.0 <= diff_f < -1.0: (3.0 - diff_f) * 512 (4096 -> 2048; diff < -5 clamps to 4096)
        #   -1.0 <= diff_f <  0.0: diff_f * 2048 + 4096 (2048 -> 4096 continuously across final tick)
        "mov BYTE PTR [ebp-0x91], 0",
        "ucomiss xmm0, DWORD PTR [f_m1f]",
        "jae .scf_neg_open",
        "movss xmm1, DWORD PTR [f_three]",
        "subss xmm1, xmm0",
        "mulss xmm1, DWORD PTR [f_512f]",
        "movaps xmm0, xmm1",
        "jmp .scf_apply",
        ".scf_neg_open:",
        "mulss xmm0, DWORD PTR [f_2048f]",
        "addss xmm0, DWORD PTR [f_4096f]",
        "jmp .scf_apply",
        ".scf_pos:",
        "movaps xmm1, xmm0",        # preserve diff_f >= 0
        "movaps xmm2, xmm0",
        "divss xmm2, DWORD PTR [f_200f]",
        "cvttss2si eax, xmm2",
        "cvtsi2ss xmm2, eax",
        "mulss xmm2, DWORD PTR [f_200f]",
        "subss xmm0, xmm2",
        "maxss xmm0, DWORD PTR [f_zero]",  # p in [0, 200)
        "ucomiss xmm0, DWORD PTR [f_100]",
        "setae al",
        "mov BYTE PTR [ebp-0x91], al",
        "test al, al",
        "jne .scf_back",
        "ucomiss xmm1, DWORD PTR [f_8f]",
        "jbe .scf_fclose",
        "ucomiss xmm0, DWORD PTR [f_8f]",
        "jae .scf_fclose",
        "mulss xmm0, DWORD PTR [f_512f]",
        "jmp .scf_apply",
        ".scf_fclose:",
        "ucomiss xmm0, DWORD PTR [f_91f]",
        "jb .scf_full",
        "movss xmm1, DWORD PTR [f_99f]",
        "subss xmm1, xmm0",
        "maxss xmm1, DWORD PTR [f_zero]",
        "mulss xmm1, DWORD PTR [f_512f]",
        "movaps xmm0, xmm1",
        "jmp .scf_apply",
        ".scf_back:",
        "ucomiss xmm0, DWORD PTR [f_108f]",
        "jae .scf_bclose",
        "subss xmm0, DWORD PTR [f_100]",
        "mulss xmm0, DWORD PTR [f_512f]",
        "jmp .scf_apply",
        ".scf_bclose:",
        "ucomiss xmm0, DWORD PTR [f_191f]",
        "jb .scf_full",
        "movss xmm1, DWORD PTR [f_199f]",
        "subss xmm1, xmm0",
        "maxss xmm1, DWORD PTR [f_zero]",
        "mulss xmm1, DWORD PTR [f_512f]",
        "movaps xmm0, xmm1",
        "jmp .scf_apply",
        ".scf_full:",
        "movss xmm0, DWORD PTR [f_4096f]",
        ".scf_apply:",
        "minss xmm0, DWORD PTR [f_4096f]",
        "cvtss2si eax, xmm0",
        "mov DWORD PTR [ebp-0x70], eax",
        "mov ebx, eax",
        "cmp ebx, 0x100",
        "jge .scf_txt",
        "mov ebx, 0x100",
        ".scf_txt:",
        "cmp eax, 0x3e8",
        "jge .scf_done",
        "mov DWORD PTR [ebp-0x70], 0",
        ".scf_done:",
        "ret",
        ".scf_raw:",
        "sub esi, ecx",
        "mov eax, 0x51eb851f",
        "mul esi",
        "shr edx, 6",
        "imul edx, edx, 200",
        "mov eax, esi",
        "sub eax, edx",
        "cmp eax, 100",
        "setae cl",
        "mov ebx, 0x1000",
        "mov BYTE PTR [ebp-0x91], cl",
        "test cl, cl",
        "jne .scr_b",
        "cmp esi, 8",
        "jbe .scr_fc",
        "cmp eax, 8",
        "jae .scr_fc",
        "shl eax, 12",
        "shr eax, 3",
        "mov ebx, eax",
        "jmp .scr_a",
        ".scr_fc:",
        "cmp eax, 92",
        "jb .scr_a",
        "mov ebx, 99",
        "jmp .scr_s",
        ".scr_b:",
        "cmp eax, 108",
        "jae .scr_bc",
        "lea ebx, [eax-100]",
        "jmp .scr_sh",
        ".scr_bc:",
        "cmp eax, 192",
        "jb .scr_a",
        "mov ebx, 199",
        ".scr_s:",
        "sub ebx, eax",
        ".scr_sh:",
        "shl ebx, 12",
        "shr ebx, 3",
        ".scr_a:",
        "mov eax, ebx",
        "mov DWORD PTR [ebp-0x70], eax",
        "cmp ebx, 0x100",
        "jge .scr_t",
        "mov ebx, 0x100",
        ".scr_t:",
        "cmp eax, 0x3e8",
        "jge .scf_done",
        "mov DWORD PTR [ebp-0x70], 0",
        "ret",

        # CreditsMenu (vtable 0x58b9d0) [2] render wrapper: scroll float [this+0x4c] advances by
        # speed [this+0x50] once per 30 Hz tick; render temporarily uses pos + alpha * speed.
        ".globl creditsRenderWrap",
        "creditsRenderWrap:",
        "push esi",
        "mov esi, ecx",
        "push DWORD PTR [esi+0x4c]",
        "cmp BYTE PTR [esi+0x48], 0",
        "je .crw_go",
        "cmp BYTE PTR [esi+0x49], 0",
        "jne .crw_go",
        "cmp DWORD PTR [esi+0x4], 5",
        "jl .crw_go",
        "movss xmm1, DWORD PTR [esi+0x50]",
        "movaps xmm0, xmm1",
        "mulss xmm0, DWORD PTR [G_UI_ALPHA]",
        "addss xmm0, DWORD PTR [esi+0x4c]",
        "xorps xmm2, xmm2",
        "comiss xmm2, xmm1",
        "jbe .crw_st",
        "maxss xmm0, DWORD PTR [0x58b9c8]",
        ".crw_st:",
        "movss DWORD PTR [esi+0x4c], xmm0",
        ".crw_go:",
        "push DWORD PTR [esp+12]",
        "mov ecx, esi",
        "call 0x4a1a70",
        "pop DWORD PTR [esi+0x4c]",
        "pop esi",
        "ret 4",
    ]
    code_t, syms_t = link_block_with_symbols(BLOCK_T_START, asm_t, BLOCK_T_END - BLOCK_T_START, syms)
    put_bytes(buf, BLOCK_T_START, code_t, budget=BLOCK_T_END - BLOCK_T_START)
    syms.update(syms_t)

    # ------------------------------------------------------------------
    # 3. Block U (0x45CDA0..0x45CEA0, 256B): Shared exit overlay, drawButton, CEG
    # ------------------------------------------------------------------
    asm_u = [
        ".globl calcSubExitOverlayAlpha, drawBtnRainbow, instrSRainbow",
        ".globl debriefFillExitRgba, cegBoxHeight, cegFadePct, cegBlackoutAlpha",
        "f_0p25:   .float 0.25",
        "f_31p875: .float 31.875",

        # calcSubExitOverlayAlpha(ecx = sub*) -> ecx = alpha (0..255), preserves ebx, esi, edi, ebp
        "calcSubExitOverlayAlpha:",
        "mov eax, DWORD PTR [ecx+0x8]",
        "cmp DWORD PTR [G_SUB_PTR], ecx",
        "jne .csea_cur",
        "mov edx, DWORD PTR [G_SUB_PREV8]",
        "jmp .csea_mix",
        ".csea_cur:",
        "mov edx, eax",
        ".csea_mix:",
        "mov cl, 1",
        "call smooth_counter_pair",
        "mulss xmm0, DWORD PTR [f_31p875]",
        "movss xmm1, DWORD PTR [f_255]",
        "subss xmm1, xmm0",
        "maxss xmm1, DWORD PTR [f_zero]",
        "minss xmm1, DWORD PTR [f_255]",
        "cvtss2si ecx, xmm1",
        "ret",

        # Debrief 0x4abe4f..0x4abea9: fill [ebp-0xe0..dd] & eax = PTR_SCREENMGR, preserve ebx, esi, edi
        "debriefFillExitRgba:",
        "mov ecx, DWORD PTR [ebp-0xec]",
        "call calcSubExitOverlayAlpha",
        "mov al, BYTE PTR [BG_RGB_ADDR]",
        "mov BYTE PTR [ebp-0xe0], al",
        "mov al, BYTE PTR [BG_RGB_ADDR+1]",
        "mov BYTE PTR [ebp-0xdf], al",
        "mov al, BYTE PTR [BG_RGB_ADDR+2]",
        "mov BYTE PTR [ebp-0xde], al",
        "mov BYTE PTR [ebp-0xdd], cl",
        "mov eax, DWORD PTR [PTR_SCREENMGR]",
        "ret",

        # 0x48873f..0x488746: ecx = getSmoothMenuRainbow([ebp+0x18]), preserve xmm0, xmm1, ebx, esi, edi, ebp
        "drawBtnRainbow:",
        "sub esp, 8",
        "movss DWORD PTR [esp], xmm0",
        "movss DWORD PTR [esp+4], xmm1",
        "mov eax, DWORD PTR [ebp+0x18]",
        "call getSmoothMenuRainbow",
        "mov ecx, eax",
        "movss xmm0, DWORD PTR [esp]",
        "movss xmm1, DWORD PTR [esp+4]",
        "add esp, 8",
        "ret",

        # InstructionsMenu "s" of "s+" (0x4b4e4c..0x4b4e53): edx = getSmoothMenuRainbow([ebp-0x70] + 1)
        "instrSRainbow:",
        "mov eax, DWORD PTR [ebp-0x70]",
        "inc eax",
        "call getSmoothMenuRainbow",
        "mov edx, eax",
        "ret",

        # ConfirmExitGameMenu box height (0x4a8df1..0x4a8e98, ebx = this) -> eax = boxH
        "cegBoxHeight:",
        "mov eax, DWORD PTR [ebx+0x8]",
        "test eax, eax",
        "js .cbh_in",
        "cmp DWORD PTR [G_SUB_PTR], ebx",
        "jne .cbh_o_cur",
        "mov edx, DWORD PTR [G_SUB_PREV8]",
        "jmp .cbh_o_mix",
        ".cbh_o_cur:",
        "mov edx, eax",
        ".cbh_o_mix:",
        "mov cl, 1",
        "call smooth_counter_pair",
        "ucomiss xmm0, DWORD PTR [f_four]",
        "jb .cbh_calc",
        "mov eax, 0x7e",
        "ret",
        ".cbh_in:",
        "mov eax, DWORD PTR [ebx+0x4]",
        "cmp DWORD PTR [G_SUB_PTR], ebx",
        "jne .cbh_i_cur",
        "mov edx, DWORD PTR [G_SUB_PREV4]",
        "jmp .cbh_i_mix",
        ".cbh_i_cur:",
        "mov edx, eax",
        ".cbh_i_mix:",
        "cmp eax, 4",
        "jl .cbh_i_go",
        "cmp edx, 4",
        "jl .cbh_i_go",
        "mov eax, 0x7e",
        "ret",
        ".cbh_i_go:",
        "xor cl, cl",
        "call smooth_counter_pair",
        ".cbh_calc:",
        "mulss xmm0, DWORD PTR [f_0p25]",
        "movss xmm1, DWORD PTR [f_one]",
        "subss xmm1, xmm0",
        "maxss xmm1, DWORD PTR [f_zero]",
        "sub esp, 4",
        "movss DWORD PTR [esp], xmm1",
        "call FN_FOOTER_Y",
        "sub eax, 0x7e",
        "cvtsi2ss xmm0, eax",
        "mulss xmm0, DWORD PTR [esp]",
        "add esp, 4",
        "cvtss2si eax, xmm0",
        "add eax, 0x7e",
        "ret",

        # ConfirmExitGameMenu fade pct (0x4a8ee3..0x4a8f3f, ebx = this) -> esi = pct (0..100)
        "cegFadePct:",
        "mov ecx, ebx",
        "cmp DWORD PTR [ecx+0x4], 8",
        "jge .cfp_chk_out",
        "mov eax, DWORD PTR [PTR_SCREENMGR]",
        "cmp DWORD PTR [eax+0x54], 2",
        "je .cfp_use_sub",
        "cmp DWORD PTR [ecx+0x4], 4",
        "jg .cfp_use_sub",
        "mov esi, 100",
        "ret",
        ".cfp_chk_out:",
        "mov eax, DWORD PTR [ecx+0x8]",
        "test eax, eax",
        "js .cfp_use_sub",
        "cmp DWORD PTR [G_SUB_PTR], ecx",
        "jne .cfp_o_cur",
        "mov edx, DWORD PTR [G_SUB_PREV8]",
        "jmp .cfp_o_mix",
        ".cfp_o_cur:",
        "mov edx, eax",
        ".cfp_o_mix:",
        "mov cl, 1",
        "call smooth_counter_pair",
        "call div_pct_4_m4",
        "mov esi, eax",
        "ret",
        ".cfp_use_sub:",
        "push 1",
        "call calcSubFadePct",
        "mov esi, eax",
        "ret",

        # ConfirmExitGameMenu YES blackout (0x4a94f3..0x4a9511, eax = this) -> al = alpha (0..255)
        "cegBlackoutAlpha:",
        "cmp DWORD PTR [G_EXT_SUB_PTR], eax",
        "jne .cba_raw",
        "movss xmm0, DWORD PTR [G_EXT_EFF_A]",
        "jmp .cba_scale",
        ".cba_raw:",
        "cvtsi2ss xmm0, DWORD PTR [eax+0x18]",
        ".cba_scale:",
        "mulss xmm0, DWORD PTR [f_31p875]",
        "maxss xmm0, DWORD PTR [f_zero]",
        "minss xmm0, DWORD PTR [f_255]",
        "cvtss2si eax, xmm0",
        "ret",
    ]
    code_u, syms_u = link_block_with_symbols(BLOCK_U_START, asm_u, BLOCK_U_END - BLOCK_U_START, syms)
    put_bytes(buf, BLOCK_U_START, code_u, budget=BLOCK_U_END - BLOCK_U_START)
    syms.update(syms_u)

    # ------------------------------------------------------------------
    # 4. Block V (0x44B1A0..0x44B2A0, 256B): LevelSelectMenu helpers
    # ------------------------------------------------------------------
    asm_v = [
        ".globl lsTileFlashPct, lsTileBadgeRainbow, lsInfoCardPct7, lsInfoCardSlideX",
        ".globl f_15f, f_100d7, f_273f, f_50f",
        "f_15f:   .float 15.0",
        "f_100d7: .float 14.285714",  # 100.0 / 7.0
        "f_273f:  .float 273.0",
        "f_50f:   .float 50.0",
        "f_130f:  .float 130.0",

        # LevelSelect 0x4b81a3..0x4b81c5 (eax = curTimer, ebx = tileIdx, edi = this)
        # Returns eax = 1 if draw needed (ecx = pct), 0 if skip. Preserves ebx, esi, edi, ebp.
        "lsTileFlashPct:",
        "cmp DWORD PTR [edi+0x8], 0",
        "jge .ltf_skip",
        "cmp DWORD PTR [G_EXT_SUB_PTR], edi",
        "jne .ltf_cur",
        "cmp ebx, 64",
        "jae .ltf_cur",
        "movzx edx, BYTE PTR [G_LS_FLASH_PREV+ebx]",
        "cmp eax, edx",
        "jg .ltf_cur",
        "cvtsi2ss xmm0, edx",
        "sub eax, edx",
        "cvtsi2ss xmm1, eax",
        "mulss xmm1, DWORD PTR [G_UI_ALPHA]",
        "addss xmm0, xmm1",
        "jmp .ltf_pct",
        ".ltf_cur:",
        "cvtsi2ss xmm0, eax",
        ".ltf_pct:",
        "ucomiss xmm0, DWORD PTR [f_zero]",
        "jbe .ltf_skip",
        "mulss xmm0, DWORD PTR [f_100]",
        "divss xmm0, DWORD PTR [f_15f]",
        "call clamp_pct_0_100",
        "mov ecx, eax",
        "test ecx, ecx",
        "jle .ltf_skip",
        "mov eax, 1",
        "ret",
        ".ltf_skip:",
        "xor eax, eax",
        "ret",

        # LevelSelect 0x4b825f..0x4b8282 (ebx = tileIdx, [ebp-0x54] = frame) -> edx = rgb
        "lsTileBadgeRainbow:",
        "cvtsi2ss xmm0, DWORD PTR [ebp-0x54]",
        "addss xmm0, DWORD PTR [G_UI_ALPHA]",
        "divss xmm0, DWORD PTR [f_three]",
        "mov eax, ebx",
        "shr eax, 1",
        "cvtsi2ss xmm1, eax",
        "subss xmm0, xmm1",
        "addss xmm0, DWORD PTR [f_130f]",
        "movss xmm1, DWORD PTR [f_one]",
        "call calcSmoothRainbow",
        "mov edx, eax",
        "ret",

        # Helper returning xmm0 = max(0, eff28) (edi = LevelSelect*)
        "get_ls_eff28:",
        "cmp DWORD PTR [G_EXT_SUB_PTR], edi",
        "jne .gle_raw",
        "movss xmm0, DWORD PTR [G_EXT_EFF_B]",
        "jmp .gle_cl",
        ".gle_raw:",
        "cvtsi2ss xmm0, DWORD PTR [edi+0x28]",
        ".gle_cl:",
        "maxss xmm0, DWORD PTR [f_zero]",
        "ret",

        # LevelSelect 0x4b8527..0x4b8566 (edi = this, esi = pct) -> ebx = pct7
        "lsInfoCardPct7:",
        "cmp DWORD PTR [edi+0x8], 0",
        "jl .lic_in",
        "mov ebx, esi",
        "ret",
        ".lic_in:",
        "call get_ls_eff28",
        "mulss xmm0, DWORD PTR [f_100d7]",
        "call clamp_pct_0_100",
        "mov ebx, eax",
        "ret",

        # LevelSelect 0x4b865e..0x4b868c -> returns eax = [ebp-0x58] = slideX, preserves ebx, esi, edi
        "lsInfoCardSlideX:",
        "call get_ls_eff28",
        "divss xmm0, DWORD PTR [f_five]",
        "maxss xmm0, DWORD PTR [f_zero]",
        "minss xmm0, DWORD PTR [f_one]",
        "mulss xmm0, DWORD PTR [f_273f]",
        "subss xmm0, DWORD PTR [f_50f]",
        "cvtss2si eax, xmm0",
        "mov DWORD PTR [ebp-0x58], eax",
        "ret",
    ]
    code_v, syms_v = link_block_with_symbols(BLOCK_V_START, asm_v, BLOCK_V_END - BLOCK_V_START, syms)
    put_bytes(buf, BLOCK_V_START, code_v, budget=BLOCK_V_END - BLOCK_V_START)
    syms.update(syms_v)

    # ------------------------------------------------------------------
    # 5. Block W (0x40D1F0..0x40D2F0, 256B): DebriefMenu shared helpers
    # ------------------------------------------------------------------
    asm_w = [
        ".globl get_deb_eff44, debColorStage, debTitleRb",
        ".globl debRankSlideX, debBoxHeight, debBarWidth, debRankOverlay",
        ".globl f_7f, f_two, f_43f",
        "f_two:   .float 2.0",
        "f_43f:   .float 43.0",
        "rank_p:  .byte 50, 20, 8, 2",
        "f_145f:  .float 145.0",
        "f_30f:   .float 30.0",
        "f_25f:   .float 25.0",
        "f_119f:  .float 119.0",
        "f_34f:   .float 34.0",
        "f_35f:   .float 35.0",
        "f_7f:    .float 7.0",

        # Returns xmm0 = eff44 (`[G_EXT_EFF_A]` if active else float([this+0x44])), edx = this
        "get_deb_eff44:",
        "mov edx, DWORD PTR [ebp-0xec]",
        "cmp DWORD PTR [G_EXT_SUB_PTR], edx",
        "jne .gde_raw",
        "movss xmm0, DWORD PTR [G_EXT_EFF_A]",
        "ret",
        ".gde_raw:",
        "cvtsi2ss xmm0, DWORD PTR [edx+0x44]",
        "ret",

        # Unified Debrief color helper (preserving ebx, esi, edi, ebp):
        # eax = startTick, edx = targetCol (0 => rainbow(eff44 + rbOff), else 0x3b3b3b/0xa1a1a1)
        # cl = whiteEnter (0 => 0x3b3b3b on enter, 1 => 0xffffff on enter), ch = rbOff (int)
        # Returns eax = 0x00RRGGBB, AND stores RGBA bytes at [ebp-0xdc] & [ebp-0xe0]!
        "debColorStage:",
        "push ecx",
        "push eax",
        "push edx",
        "call get_deb_eff44",   # xmm0 = eff44, edx = this
        "mov eax, DWORD PTR [edx+0x8]",
        "mov ecx, DWORD PTR [esp]",     # targetCol
        "test eax, eax",
        "js .dcs_enter",
        # Exit branch ([this+0x8] >= 0): lerp(0xffffff, targetOrRainbow, ebx)
        "test ecx, ecx",
        "jne .dcs_exit_lerp",
        "movzx eax, BYTE PTR [esp+9]",  # rbOff
        "cvtsi2ss xmm1, eax",
        "addss xmm0, xmm1",
        "movss xmm1, DWORD PTR [f_one]",
        "call calcSmoothRainbow",
        "mov ecx, eax",
        "jmp .dcs_exit_lerp",
        ".dcs_enter:",
        "test ecx, ecx",
        "jne .dcs_ent_fade",
        # Rainbow target: compute rainbow color in [esp]
        "movzx eax, BYTE PTR [esp+9]",
        "cvtsi2ss xmm1, eax",
        "addss xmm0, xmm1",
        "movss xmm1, DWORD PTR [f_one]",
        "call calcSmoothRainbow",
        "mov DWORD PTR [esp], eax",     # targetCol = rainbow
        "call get_deb_eff44",           # reload eff44
        ".dcs_ent_fade:",
        "cvtsi2ss xmm1, DWORD PTR [esp+4]", # startTick
        "subss xmm0, xmm1",
        "mulss xmm0, DWORD PTR [f_20]",
        "ucomiss xmm0, DWORD PTR [f_100]",
        "jae .dcs_full",
        "maxss xmm0, DWORD PTR [f_zero]",
        "cvtss2si eax, xmm0",
        "mov edx, 0xffffff",
        "cmp BYTE PTR [esp+8], 0",
        "jne .dcs_from_ok",
        "mov edx, 0x3b3b3b",
        ".dcs_from_ok:",
        "mov ecx, DWORD PTR [esp]",
        "push eax",
        "push ecx",
        "push edx",
        "call FN_COLOR_LERP",
        "add esp, 12",
        "mov ecx, eax",
        "jmp .dcs_exit_lerp",
        ".dcs_full:",
        "mov ecx, DWORD PTR [esp]",
        ".dcs_exit_lerp:",
        "push ebx",
        "push ecx",
        "push 0xffffff",
        "call FN_COLOR_LERP",
        "add esp, 24", # pop 3 args (12) + 3 saved (12)
        "mov ecx, eax",
        "mov edx, eax",
        "sar ecx, 16",
        "sar edx, 8",
        "mov BYTE PTR [ebp-0xdc], cl",
        "mov BYTE PTR [ebp-0xdb], dl",
        "mov BYTE PTR [ebp-0xda], al",
        "mov BYTE PTR [ebp-0xd9], 0xff",
        "mov ecx, DWORD PTR [ebp-0xdc]",
        "ret",

        # Stage 1 Title rainbow: 0x4a9b24..0x4a9b42 -> eax = colorLerp(0xffffff, rb(eff44), ebx)
        "debTitleRb:",
        "call get_deb_eff44",
        "movss xmm1, DWORD PTR [f_one]",
        "call calcSmoothRainbow",
        "push ebx",
        "push eax",
        "push 0xffffff",
        "call FN_COLOR_LERP",
        "add esp, 12",
        "ret",

        # Stage 9 Rank header slide-in: 0x4aabeb..0x4aac09 -> esi = -145..0
        "debRankSlideX:",
        "call get_deb_eff44",
        "maxss xmm0, DWORD PTR [f_25f]",
        "minss xmm0, DWORD PTR [f_30f]",
        "subss xmm0, DWORD PTR [f_30f]",
        "divss xmm0, DWORD PTR [f_five]",
        "mulss xmm0, DWORD PTR [f_145f]",
        "cvtss2si esi, xmm0",
        "ret",

        # Stage 10 Rank box height: 0x4ab014..0x4ab04a -> esi = 10..119
        "debBoxHeight:",
        "call get_deb_eff44",
        "subss xmm0, DWORD PTR [f_34f]",
        "maxss xmm0, DWORD PTR [f_zero]",
        "minss xmm0, DWORD PTR [f_five]",
        "divss xmm0, DWORD PTR [f_five]",
        "mulss xmm0, DWORD PTR [f_119f]",
        "cvtss2si esi, xmm0",
        "cmp esi, 10",
        "jge .dbh_ok",
        "mov esi, 10",
        ".dbh_ok:",
        "ret",

        # Stage 11 Rank bar grow: 0x4ab12c..0x4ab16a -> eax = w << 12 (resumes at 0x4ab165 sar eax, 12)
        "debBarWidth:",
        "call get_deb_eff44",
        "subss xmm0, DWORD PTR [f_35f]",
        "maxss xmm0, DWORD PTR [f_zero]",
        "minss xmm0, DWORD PTR [f_7f]",
        "divss xmm0, DWORD PTR [f_7f]",
        "movzx esi, BYTE PTR ds:0x5f91e8",
        "shl esi, 15",
        "add esi, 10",
        "cvtsi2ss xmm1, esi",
        "mulss xmm0, xmm1",
        "cvtss2si eax, xmm0",
        "ret",

        "debRankOverlay:",
        "call get_deb_eff44",
        "mov ecx, DWORD PTR [ebp-0xf8]",
        "mov ecx, DWORD PTR [ecx+0x114]",
        "mov eax, DWORD PTR [edx+0x8]",
        "cmp ecx, 4",
        "jae .dro_ss",
        "test eax, eax",
        "js .dro_da",
        ".dro_blk:",
        "mov DWORD PTR [ebp-0xe0], 0xff000000",
        "ret",
        ".dro_da:",
        "cmp DWORD PTR [edx+0x44], 43",
        "jl .dro_blk",
        "subss xmm0, DWORD PTR [f_43f]",
        "maxss xmm0, DWORD PTR [f_zero]",
        "movzx eax, BYTE PTR [rank_p+ecx]",
        "cvtsi2ss xmm1, eax",
        "movaps xmm2, xmm0",
        "divss xmm2, xmm1",
        "cvttss2si ecx, xmm2",
        "cvtsi2ss xmm2, ecx",
        "mulss xmm2, xmm1",
        "subss xmm0, xmm2",           # phase in [0, P)
        "divss xmm0, xmm1",           # norm in [0, 1)
        "mulss xmm0, DWORD PTR [f_two]",
        "ucomiss xmm0, DWORD PTR [f_one]",
        "jbe .dro_pct",
        "movss xmm1, DWORD PTR [f_two]",
        "subss xmm1, xmm0",
        "movaps xmm0, xmm1",
        ".dro_pct:",
        "mulss xmm0, DWORD PTR [f_100]",
        "call clamp_pct_0_100",
        "push eax",
        "push 0xffffff",
        "push 0",
        "call FN_COLOR_LERP",
        "add esp, 12",
        "jmp .dro_store",
        ".dro_ss:",
        "mov edx, DWORD PTR [ebp-0xf4]",
        "test eax, eax",
        "jge .dro_ss_mix",
        "inc edx",
        "mov ebx, edi",
        ".dro_ss_mix:",
        "cvtsi2ss xmm1, edx",
        "addss xmm0, xmm1",
        "movss xmm1, DWORD PTR [f_one]",
        "call calcSmoothRainbow",
        "push ebx",
        "push eax",
        "push 0",
        "call FN_COLOR_LERP",
        "add esp, 12",
        ".dro_store:",
        "mov ecx, eax",
        "mov edx, eax",
        "sar ecx, 16",
        "sar edx, 8",
        "mov BYTE PTR [ebp-0xe0], cl",
        "mov BYTE PTR [ebp-0xdf], dl",
        "mov BYTE PTR [ebp-0xde], al",
        "mov BYTE PTR [ebp-0xdd], 0xff",
        "ret",
    ]
    code_w, syms_w = link_block_with_symbols(BLOCK_W_START, asm_w, BLOCK_W_END - BLOCK_W_START, syms)
    put_bytes(buf, BLOCK_W_START, code_w, budget=BLOCK_W_END - BLOCK_W_START)
    syms.update(syms_w)

    # ------------------------------------------------------------------
    # 6. Block X (0x4FAA80..0x4FACE0, 608B): Debrief NEW RECORD, Rank, Buttons
    # ------------------------------------------------------------------
    asm_x = [
        ".globl debNrScale, debNrMarquee, debRankStamp, debBtnQuadAlpha",
        "f_19f:    .float 19.0",
        "f_14f:    .float 14.0",
        "f_40960f: .float 40960.0",
        "f_76f:    .float 76.0",
        "f_38f:    .float 38.0",
        "f_42f:    .float 42.0",
        "f_28672f: .float 28672.0",
        "f_41f:    .float 41.0",
        "f_10000f: .float 10000.0",

        "debNrScale:",
        "call get_deb_eff44",
        "maxss xmm0, DWORD PTR [f_14f]",
        "minss xmm0, DWORD PTR [f_19f]",
        "movss xmm1, DWORD PTR [f_19f]",
        "subss xmm1, xmm0",
        "divss xmm1, DWORD PTR [f_five]",
        "mulss xmm1, DWORD PTR [f_40960f]",
        "cvtss2si esi, xmm1",
        "add esi, 0x1000",
        "mov eax, DWORD PTR ds:0x589ddc",
        "ret",

        "debNrMarquee:",
        "call get_deb_eff44",
        "mulss xmm0, DWORD PTR [f_four]",
        "subss xmm0, DWORD PTR [f_76f]",
        "maxss xmm0, DWORD PTR [f_zero]",
        "cvtss2si eax, xmm0",
        "cdq",
        "idiv DWORD PTR [ebp-0xdc]",
        "add edi, edx",
        "ret",

        "debRankStamp:",
        "call get_deb_eff44",
        "minss xmm0, DWORD PTR [f_43f]",
        "maxss xmm0, DWORD PTR [f_42f]",
        "subss xmm0, DWORD PTR [f_38f]",  # t in [4.0, 5.0]
        "cvtss2si esi, xmm0",
        "divss xmm0, DWORD PTR [f_five]",
        "movaps xmm1, xmm0",
        "mulss xmm0, DWORD PTR [f_28672f]",
        "cvtss2si ecx, xmm0",
        "movzx edi, BYTE PTR ds:0x5f91e9",
        "xor edx, edx",
        "mov eax, 0x1000",
        "div edi",
        "cmp ecx, eax",
        "jge .drs_sy",
        "mov ecx, eax",
        ".drs_sy:",
        "mov DWORD PTR [ebp-0xdc], ecx",
        "mulss xmm1, DWORD PTR [f_100]",
        "call clamp_pct_0_100",
        "mov edi, eax",
        "ret",

        "debBtnQuadAlpha:",
        "call get_deb_eff44",
        "cmp DWORD PTR [edx+0x8], 0",
        "jl .dbq_in",
        "cvtsi2ss xmm0, ebx",
        "jmp .dbq_sq",
        ".dbq_in:",
        "subss xmm0, DWORD PTR [f_41f]",
        "mulss xmm0, DWORD PTR [f_20]",
        "maxss xmm0, DWORD PTR [f_zero]",
        "minss xmm0, DWORD PTR [f_100]",
        ".dbq_sq:",
        "mulss xmm0, xmm0",
        "mulss xmm0, DWORD PTR [f_255]",
        "divss xmm0, DWORD PTR [f_10000f]",
        "cvtss2si ebx, xmm0",
        "xor edi, edi",
        "xor esi, esi",
        "ret",
    ]
    code_x, syms_x = link_block_with_symbols(BLOCK_X_START, asm_x, BLOCK_X_END - BLOCK_X_START, syms)
    put_bytes(buf, BLOCK_X_START, code_x, budget=BLOCK_X_END - BLOCK_X_START)
    syms.update(syms_x)

    # ------------------------------------------------------------------
    # 8. Block Y (0x4FB340..0x4FB430, 240B): WorldScreen HUD edge timer setup
    # ------------------------------------------------------------------
    asm_y = [
        ".globl hudTimerSetup",
        "f_22f:  .float 22.0",
        "f_eps:  .float 0.0001",

        # 0x47ac9c..0x47ad55 (esi = WorldScreen*, [ebp+0x8] = slot, [ebp-0x8] = X_int, ebx = Y_int, [ebp-0x4] = aspect)
        # Computes G_ET_TICKS_TMP, bounce_f -> pushes scaled MatrixStack, saves G_HUD_MAT_ORIG, [ebp-0xc]=5
        "hudTimerSetup:",
        "mov ecx, DWORD PTR [ebp+0x8]",
        "mov eax, DWORD PTR [esi+ecx*4+0x78]",
        "mov DWORD PTR [ebp-0xc], 5",
        "cmp DWORD PTR [G_EXT_WS_PTR], esi",
        "jne .hts_raw_t",
        "cmp BYTE PTR [G_WS_PREV_EC+ecx], 0",
        "je .hts_raw_t",
        "mov edx, DWORD PTR [G_WS_PREV_ET0+ecx*4]",
        "cmp eax, edx",
        "jl .hts_raw_t",
        "push eax",
        "sub eax, edx",
        "cmp eax, 2",
        "jg .hts_pop_t",
        "cvtsi2ss xmm0, edx",
        "cvtsi2ss xmm1, eax",
        "mulss xmm1, DWORD PTR [G_UI_ALPHA]",
        "addss xmm0, xmm1",
        "add esp, 4",
        "jmp .hts_st_t",
        ".hts_pop_t:",
        "pop eax",
        ".hts_raw_t:",
        "cvtsi2ss xmm0, eax",
        ".hts_st_t:",
        "movss DWORD PTR [G_ET_TICKS_TMP], xmm0",
        # Bounce scale bounce_f in xmm4: 5.0 if ticks <= 10 or rem >= 3.0
        "movss xmm4, DWORD PTR [f_five]",
        "cmp DWORD PTR [esi+ecx*4+0x78], 10",
        "jle .hts_pos",
        "addss xmm0, DWORD PTR [f_eps]",
        "movaps xmm1, xmm0",
        "divss xmm1, DWORD PTR [f_22f]",
        "cvttss2si eax, xmm1",
        "cvtsi2ss xmm1, eax",
        "mulss xmm1, DWORD PTR [f_22f]",
        "subss xmm0, xmm1",
        "maxss xmm0, DWORD PTR [f_zero]",
        "ucomiss xmm0, DWORD PTR [f_one]",
        "jae .hts_b12",
        "movss xmm4, DWORD PTR [f_8f]",
        "jmp .hts_pos",
        ".hts_b12:",
        "ucomiss xmm0, DWORD PTR [f_two]",
        "jae .hts_b23",
        "movss xmm4, DWORD PTR [f_8f]",
        "subss xmm0, DWORD PTR [f_one]",
        "subss xmm4, xmm0",
        "jmp .hts_pos",
        ".hts_b23:",
        "ucomiss xmm0, DWORD PTR [f_three]",
        "jae .hts_pos",
        "movss xmm4, DWORD PTR [f_7f]",
        "subss xmm0, DWORD PTR [f_two]",
        "mulss xmm0, DWORD PTR [f_two]",
        "subss xmm4, xmm0",
        ".hts_pos:",
        # Interpolate X_f -> xmm2, Y_f -> xmm3
        "cvtsi2ss xmm2, DWORD PTR [ebp-0x8]",
        "cvtsi2ss xmm3, ebx",
        "cmp DWORD PTR [G_EXT_WS_PTR], esi",
        "jne .hts_neg_y",
        "movss xmm5, DWORD PTR [f_one]",
        "subss xmm5, DWORD PTR [G_UI_ALPHA]",
        "cmp BYTE PTR [G_WS_PREV_EC+ecx], 0",
        "je .hts_rise0",
        "mov eax, DWORD PTR [esi+ecx*8+0x2148]",
        "sub eax, DWORD PTR [G_WS_PREV_X0+ecx*8]",
        "cvtsi2ss xmm0, eax",
        "mulss xmm0, xmm5",
        "subss xmm2, xmm0",
        "mov eax, DWORD PTR [esi+ecx*8+0x214c]",
        "sub eax, DWORD PTR [G_WS_PREV_Y0+ecx*8]",
        "cvtsi2ss xmm0, eax",
        "mulss xmm0, xmm5",
        "subss xmm3, xmm0",
        "jmp .hts_neg_y",
        ".hts_rise0:",
        "mov eax, DWORD PTR [esi+ecx*4+0x80]",
        "cvtsi2ss xmm0, eax",
        "mulss xmm0, xmm5",
        "addss xmm3, xmm0",
        ".hts_neg_y:",
        "xorps xmm0, xmm0",
        "subss xmm0, xmm3",
        "movaps xmm3, xmm0",            # xmm3 = -Y_f
        "divss xmm4, DWORD PTR [f_five]", # s = bounce_f / 5.0
        # Zero 16-float matrix [ebp-0x5c..0x20]
        "lea edi, [ebp-0x5c]",
        "xor eax, eax",
        "mov ecx, 16",
        "rep stosd",
        "movss xmm1, DWORD PTR [f_one]",
        "movss DWORD PTR [ebp-0x5c], xmm1",
        "movss DWORD PTR [ebp-0x48], xmm1",
        "movss DWORD PTR [ebp-0x34], xmm1",
        "movss DWORD PTR [ebp-0x2c], xmm2",
        "movss DWORD PTR [ebp-0x28], xmm3",
        "movss DWORD PTR [ebp-0x20], xmm1",
        # Copy unscaled matrix into G_HUD_MAT_ORIG
        "push esi",
        "lea esi, [ebp-0x5c]",
        "mov edi, G_HUD_MAT_ORIG",
        "mov ecx, 16",
        "rep movsd",
        "pop esi",
        # Apply bounce scale s, X pivot + 2 * aspect * (s - 1), Y baseline + 20 * aspect * (s - 1)
        "movaps xmm0, xmm4",
        "subss xmm0, xmm1",
        "mulss xmm0, DWORD PTR [ebp-0x4]",
        "movaps xmm5, xmm0",
        "mulss xmm5, DWORD PTR [f_two]",
        "addss xmm2, xmm5",
        "mulss xmm0, DWORD PTR [f_20]",
        "addss xmm3, xmm0",
        "movss DWORD PTR [ebp-0x5c], xmm4",
        "movss DWORD PTR [ebp-0x48], xmm4",
        "movss DWORD PTR [ebp-0x2c], xmm2",
        "movss DWORD PTR [ebp-0x28], xmm3",
        "mov edi, DWORD PTR [PTR_MSTACK]",
        "mov DWORD PTR [ebp-0x18], edi",
        "mov ecx, edi",
        "call FN_MSTACK_PUSH",
        "lea edx, [ebp-0x5c]",
        "push edx",
        "mov ecx, edi",
        "call FN_MSTACK_SET2",
        "mov ecx, edi",
        "call 0x41f660",
        "ret",
    ]
    code_y, syms_y = link_block_with_symbols(BLOCK_Y_START, asm_y, BLOCK_Y_END - BLOCK_Y_START, syms)
    put_bytes(buf, BLOCK_Y_START, code_y, budget=BLOCK_Y_END - BLOCK_Y_START)
    syms.update(syms_y)

    # ------------------------------------------------------------------
    # 9. Block Z (0x44E370..0x44E460, 240B): HUD edge time digits & Instructions demo
    # ------------------------------------------------------------------
    asm_z = [
        ".globl hudTimerDigitsAndColor, hudRestoreMat, edgeDemoSetup, setSepGlyph, demoSepPush",

        # 0x47ad62..0x47adde (esi = WorldScreen*, [ebp+0x8] = slot) -> [ebp-0x8]=sec, ebx=cs, edi=color
        "hudTimerDigitsAndColor:",
        "movss xmm0, DWORD PTR [G_ET_TICKS_TMP]",
        "addss xmm0, DWORD PTR [f_eps]",
        "movaps xmm1, xmm0",
        "divss xmm1, DWORD PTR [f_22f]",
        "cvttss2si eax, xmm1",
        "mov DWORD PTR [ebp-0x8], eax", # sec
        "cvtsi2ss xmm1, eax",
        "mulss xmm1, DWORD PTR [f_22f]",
        "subss xmm0, xmm1",
        "maxss xmm0, DWORD PTR [f_zero]",
        "movss DWORD PTR [G_ET_REM_TMP], xmm0",
        "mulss xmm0, DWORD PTR [f_100]",
        "divss xmm0, DWORD PTR [f_22f]",
        "cvttss2si ebx, xmm0",
        "cmp ebx, 99",
        "jle .htd_col",
        "mov ebx, 99",
        ".htd_col:",
        "mov ecx, esi",
        "call getSmoothWsFrame",
        "movss xmm1, DWORD PTR [f_one]",
        "call calcSmoothRainbow",
        "mov edi, eax",
        "mov eax, DWORD PTR [ebp+0x8]",
        "cmp eax, 1",
        "jne .htd_flash",
        "cmp BYTE PTR [esi+0x48], 0",
        "je .htd_flash",
        "mov ecx, DWORD PTR [esi+0x4]",
        "and cl, al",
        "movzx edi, cl",
        "neg edi",
        "sbb edi, edi",
        "and edi, 0xffeeeef5",
        "add edi, 0xaaaaaa",
        ".htd_flash:",
        "mov edx, DWORD PTR [ebp+0x8]",
        "cmp DWORD PTR [esi+edx*4+0x78], 0x55da",
        "je .htd_done",
        "movss xmm0, DWORD PTR [G_ET_REM_TMP]",
        "ucomiss xmm0, DWORD PTR [f_one]",
        "jae .htd_done",
        "mulss xmm0, DWORD PTR [f_100]",
        "call clamp_pct_0_100",
        "push eax",
        "push edi",
        "push 0xffffff",
        "call FN_COLOR_LERP",
        "add esp, 12",
        "mov edi, eax",
        ".htd_done:",
        "ret",

        # 0x47af27..0x47af5a: restore unscaled HUD MatrixStack (pop+push+SET2+apply), draw '.' at 4*aspect
        "hudRestoreMat:",
        "mov ecx, DWORD PTR [PTR_MSTACK]",
        "call 0x41f610",
        "mov ecx, DWORD PTR [PTR_MSTACK]",
        "call FN_MSTACK_PUSH",
        "push G_HUD_MAT_ORIG",
        "mov ecx, DWORD PTR [PTR_MSTACK]",
        "call FN_MSTACK_SET2",
        "mov ecx, DWORD PTR [PTR_MSTACK]",
        "call 0x41f660",
        "call setSepGlyph",
        "movss xmm0, DWORD PTR [ebp-0x4]",
        "cvttss2si eax, xmm0",
        "mov DWORD PTR [ebp+0x8], eax",
        "cmp BYTE PTR [G_TIME_FMT], 0",
        "jne .hrm_modern",
        # default: stock prime (') args: x = int(aspect * 0x579c48), y = 0, scale = int(aspect)
        "cvtps2pd xmm0, xmm0",
        "mulsd xmm0, QWORD PTR ds:0x579c48",
        "cvttsd2si edx, xmm0",
        "push 0xaa",
        "push 0xff",
        "push edi",
        "push eax",
        "push 0",
        "push edx",
        "jmp .hrm_draw",
        ".hrm_modern:",
        "mulss xmm0, DWORD PTR [f_four]",
        "cvttss2si edx, xmm0",
        "push 0xaa",
        "push 0xff",
        "push edi",
        "push edx",
        "push eax",
        "push 0",
        ".hrm_draw:",
        "push 0xa",
        "mov ecx, esi",
        "call 0x479f00",
        "movss xmm0, DWORD PTR [ebp-0x4]",
        "ret",

        # Rewrites glyph 10 rects (0x5F3B88, 3 rects x 4 B): stock prime vs modern baseline dot
        "setSepGlyph:",
        "cmp BYTE PTR [G_TIME_FMT], 0",
        "jne .ssg_modern",
        "mov DWORD PTR ds:0x5f3b88, 0x06010000",
        "mov DWORD PTR ds:0x5f3b8c, 0x05010001",
        "mov DWORD PTR ds:0x5f3b90, 0x04010002",
        "ret",
        ".ssg_modern:",
        "mov DWORD PTR ds:0x5f3b88, 0x01010400",
        "mov DWORD PTR ds:0x5f3b8c, 0",
        "mov DWORD PTR ds:0x5f3b90, 0",
        "ret",

        # Replaces `push 1; lea ecx, [esi+0xa]` at 0x47a43e: pushes scale (default 1 / modern 4), ecx = esi + y
        # (y: default 0xa / modern 0xb). Preserves eax, edx, flags are clobbered (unused).
        "demoSepPush:",
        "push eax",
        "lea ecx, [esi+0xa]",
        "mov eax, 1",
        "cmp BYTE PTR [G_TIME_FMT], 0",
        "je .dsp_go",
        "lea ecx, [esi+0xb]",
        "mov eax, 4",
        ".dsp_go:",
        "xchg DWORD PTR [esp+4], eax",
        "xchg DWORD PTR [esp], eax",
        "ret",

        # InstructionsMenu edge time demo 0x47a2d8..0x47a341 ([ebp+0x10]=tick, [ebp+0x14]=frame)
        # Sets edi=sec, [ebp-0x8]=cs, esi=int(rem), [ebp+0x14]=smoothColor, ebx=ecx (Game*)
        "edgeDemoSetup:",
        "call setSepGlyph",
        "mov ebx, ecx",
        "mov edx, DWORD PTR [ebp+0x10]",
        "mov eax, DWORD PTR [G_EXT_SUB_PTR]",
        "test eax, eax",
        "je .eds_raw",
        "cmp DWORD PTR [eax], VT_INSTRUCTIONS",
        "jne .eds_raw",
        "movss xmm0, DWORD PTR [G_EXT_EFF_B]",
        "jmp .eds_div",
        ".eds_raw:",
        "cvtsi2ss xmm0, edx",
        ".eds_div:",
        "addss xmm0, DWORD PTR [f_eps]",
        "movaps xmm1, xmm0",
        "divss xmm1, DWORD PTR [f_22f]",
        "cvttss2si edi, xmm1",         # edi = sec
        "cvtsi2ss xmm1, edi",
        "mulss xmm1, DWORD PTR [f_22f]",
        "subss xmm0, xmm1",
        "maxss xmm0, DWORD PTR [f_zero]", # rem in [0, 22)
        "cvttss2si esi, xmm0",         # esi = int(rem)
        "sub esp, 4",
        "movss DWORD PTR [esp], xmm0",
        "mulss xmm0, DWORD PTR [f_100]",
        "divss xmm0, DWORD PTR [f_22f]",
        "cvttss2si eax, xmm0",
        "cmp eax, 99",
        "jle .eds_cs",
        "mov eax, 99",
        ".eds_cs:",
        "mov DWORD PTR [ebp-0x8], eax",
        "mov eax, DWORD PTR [ebp+0x14]",
        "call getSmoothMenuRainbow",
        "movss xmm0, DWORD PTR [esp]",
        "add esp, 4",
        "cmp DWORD PTR [ebp+0x10], 0x55da",
        "je .eds_st",
        "ucomiss xmm0, DWORD PTR [f_one]",
        "jae .eds_st",
        "push eax",                     # save rainbow
        "mulss xmm0, DWORD PTR [f_100]",
        "call clamp_pct_0_100",
        "pop edx",                      # rainbow
        "push eax",
        "push edx",
        "push 0xffffff",
        "call FN_COLOR_LERP",
        "add esp, 12",
        ".eds_st:",
        "mov DWORD PTR [ebp+0x14], eax",
        "ret",
    ]
    code_z, syms_z = link_block_with_symbols(BLOCK_Z_START, asm_z, BLOCK_Z_END - BLOCK_Z_START, syms)
    put_bytes(buf, BLOCK_Z_START, code_z, budget=BLOCK_Z_END - BLOCK_Z_START)
    syms.update(syms_z)

    # ------------------------------------------------------------------
    # 10. Inline hooks (guarded via _check_unmodified + exact budgets)
    # ------------------------------------------------------------------

    # 10a. Chain onSavePrev_Ext & onInterpolate_Ext from Block I call sites
    _check_unmodified(buf, syms["sp_ms_callsite"], [f"call {hex(syms['onSavePrev_MS'])}"], 5)
    put_bytes(
        buf, syms["sp_ms_callsite"],
        assemble_at(syms["sp_ms_callsite"], [f"call {hex(syms['onSavePrev_Ext'])}"], 5),
        budget=5,
    )
    _check_unmodified(buf, syms["ip_ms_callsite"], [f"call {hex(syms['onInterpolate_MS'])}"], 5)
    put_bytes(
        buf, syms["ip_ms_callsite"],
        assemble_at(syms["ip_ms_callsite"], [f"call {hex(syms['onInterpolate_Ext'])}"], 5),
        budget=5,
    )

    # 10b. ScoresMenu vtable[2] scrollY wrapper
    expect_vtable = struct.pack("<I", 0x4AF660)
    expect_wrap = struct.pack("<I", syms["scoresRenderWrap"])
    voff = va2off(0x58D120)
    assert buf[voff : voff + 4] == expect_vtable, "Unexpected ScoresMenu vtable[2]"
    buf[voff : voff + 4] = expect_wrap

    # 10b'. CreditsMenu vtable[2] (0x58b9d8) smooth scroll wrapper (Options -> credits & all-levels ending)
    cvoff = va2off(0x58B9D8)
    assert buf[cvoff : cvoff + 4] == struct.pack("<I", 0x4A1A70), "Unexpected CreditsMenu vtable[2]"
    buf[cvoff : cvoff + 4] = struct.pack("<I", syms["creditsRenderWrap"])

    # ScoresMenu card flip (0x4afdb0..0x4afe40, 144B) & prism dot rainbow (0x4b0a6b..0x4b0aa9, 62B)
    _check_unmodified(buf, 0x4AFDB0, ["mov edx, DWORD PTR [ebp-0x5c]", "mov esi, DWORD PTR [edx+0x74]"], 6)
    put_bytes(
        buf, 0x4AFDB0,
        assemble_at(0x4AFDB0, [f"call {hex(syms['scoresCardFlip'])}", "jmp 0x4afe40"]).ljust(0x4AFE40 - 0x4AFDB0, b"\xcc"),
        budget=0x4AFE40 - 0x4AFDB0,
    )
    _check_unmodified(buf, 0x4B0A6B, ["mov eax, DWORD PTR [ebp-0xa4]"])
    put_bytes(
        buf, 0x4B0A6B,
        assemble_at(0x4B0A6B, [f"call {hex(syms['scoresDotRainbow'])}", "jmp 0x4b0ab5"]).ljust(0x4B0AB5 - 0x4B0A6B, b"\xcc"),
        budget=0x4B0AB5 - 0x4B0A6B,
    )

    # 10c. InstructionsMenu scrollY, "s+" rainbow, Menu::drawButton rainbow
    _check_unmodified(buf, 0x4B4246, ["mov edx, DWORD PTR [ebp-0x44]", "sub edi, DWORD PTR [edx+0x28]"], 6)
    put_bytes(
        buf, 0x4B4246,
        assemble_at(0x4B4246, [f"call {hex(syms['instrSubScrollY'])}", "nop"], 6),
        budget=6,
    )
    _check_unmodified(buf, 0x4B4E4C, ["mov edx, DWORD PTR [edx*4+0x589bb8]"], 7)
    put_bytes(
        buf, 0x4B4E4C,
        assemble_at(0x4B4E4C, [f"call {hex(syms['instrSRainbow'])}", "nop", "nop"], 7),
        budget=7,
    )
    _check_unmodified(buf, 0x48873F, ["mov ecx, DWORD PTR [ecx*4+0x589bb8]"], 7)
    put_bytes(
        buf, 0x48873F,
        assemble_at(0x48873F, [f"call {hex(syms['drawBtnRainbow'])}", "nop", "nop"], 7),
        budget=7,
    )

    # 10d. ScoresSelectMenu 4 remaining PALETTE_RAINBOW highlights
    _check_unmodified(buf, 0x4B1C71, ["mov eax, 0x4ec4ec4f", "mul edi"], 7)
    put_bytes(
        buf, 0x4B1C71,
        assemble_at(0x4B1C71, [
            "mov eax, edi",
            f"call {hex(syms['getSmoothMenuRainbow'])}",
            f"mov edi, {hex(base_syms['G_RB_TMP'])}",
            "jmp 0x4b1c8e",
        ]).ljust(0x4B1C8E - 0x4B1C71, b"\xcc"),
        budget=0x4B1C8E - 0x4B1C71,
    )
    _check_unmodified(buf, 0x4B1D5A, ["mov eax, 0x4ec4ec4f", "mul edi"], 7)
    put_bytes(
        buf, 0x4B1D5A,
        assemble_at(0x4B1D5A, [
            "mov eax, edi",
            f"call {hex(syms['getSmoothMenuRainbow'])}",
            "mov edx, eax",
            "jmp 0x4b1d70",
        ]).ljust(0x4B1D70 - 0x4B1D5A, b"\xcc"),
        budget=0x4B1D70 - 0x4B1D5A,
    )
    for va_start, va_end in [(0x4B1DFD, 0x4B1E13), (0x4B1F6A, 0x4B1F80)]:
        _check_unmodified(buf, va_start, ["mov eax, 0x4ec4ec4f", "mul edi"], 7)
        put_bytes(
            buf, va_start,
            assemble_at(va_start, [
                "mov eax, edi",
                f"call {hex(syms['getSmoothMenuRainbow'])}",
                f"jmp {hex(va_end)}",
            ]).ljust(va_end - va_start, b"\xcc"),
            budget=va_end - va_start,
        )

    # 10e. LevelSelectMenu scrollbar thumb, tile flash decay, badge 10Hz rainbow, info card, exit darken
    _check_unmodified(buf, 0x4B8453, ["mov edx, DWORD PTR [edi+0x58]", "lea ecx, [ebp-0x40]"], 6)
    put_bytes(
        buf, 0x4B8453,
        assemble_at(0x4B8453, [f"call {hex(syms['lsThumbEdx'])}", "nop"], 6),
        budget=6,
    )
    _check_unmodified(buf, 0x4B84A1, ["mov ecx, DWORD PTR [edi+0x58]", "sar edx, 0x5"], 6)
    put_bytes(
        buf, 0x4B84A1,
        assemble_at(0x4B84A1, [f"call {hex(syms['lsThumbEcx'])}", "nop"], 6),
        budget=6,
    )
    _check_unmodified(buf, 0x4B81A3, ["test eax, eax"])
    _check_unmodified(buf, 0x4B81A7, ["cmp DWORD PTR [edi+0x8], 0x0"])
    put_bytes(
        buf, 0x4B81A3,
        assemble_at(0x4B81A3, [
            f"call {hex(syms['lsTileFlashPct'])}",
            "test eax, eax",
            "je 0x4b8205",
            "jmp 0x4b81c5",
        ]).ljust(0x4B81C5 - 0x4B81A3, b"\xcc"),
        budget=0x4B81C5 - 0x4B81A3,
    )
    _check_unmodified(buf, 0x4B8258, ["cmp BYTE PTR [ecx+ebx*2+0x1], 0x0"])
    _check_unmodified(buf, 0x4B8266, ["mov eax, 0xaaaaaaab"])
    put_bytes(
        buf, 0x4B825F,
        assemble_at(0x4B825F, [
            f"call {hex(syms['lsTileBadgeRainbow'])}",
            "push esi",
            "jmp 0x4b828a",
        ]).ljust(0x4B828A - 0x4B825F, b"\xcc"),
        budget=0x4B828A - 0x4B825F,
    )
    _check_unmodified(buf, 0x4B8527, ["mov ecx, DWORD PTR [edi+0x28]", "imul ecx, ecx, 0x64"], 6)
    put_bytes(
        buf, 0x4B8527,
        assemble_at(0x4B8527, [
            f"call {hex(syms['lsInfoCardPct7'])}",
            "jmp 0x4b8566",
        ]).ljust(0x4B8566 - 0x4B8527, b"\xcc"),
        budget=0x4B8566 - 0x4B8527,
    )
    _check_unmodified(buf, 0x4B865E, ["mov ecx, DWORD PTR [ebp-0x50]", "imul ecx, ecx, 0x111"], 9)
    put_bytes(
        buf, 0x4B865E,
        assemble_at(0x4B865E, [
            f"call {hex(syms['lsInfoCardSlideX'])}",
            "add esp, 0x20",
            "push 0xffffff",
            "push ebx",
            "mov edx, DWORD PTR [ebp-0x48]",
            "add edx, 0x2d",
            "jmp 0x4b868c",
        ]).ljust(0x4B868C - 0x4B865E, b"\xcc"),
        budget=0x4B868C - 0x4B865E,
    )
    _check_unmodified(buf, 0x4B86FC, ["mov eax, DWORD PTR [ebp-0x54]"])
    put_bytes(
        buf, 0x4B86FC,
        assemble_at(0x4B86FC, [
            "mov eax, DWORD PTR [ebp-0x54]",
            "sub eax, ecx",
            f"call {hex(syms['getSmoothMenuRainbow'])}",
            "mov edx, eax",
            "jmp 0x4b8711",
        ]).ljust(0x4B8711 - 0x4B86FC, b"\xcc"),
        budget=0x4B8711 - 0x4B86FC,
    )
    _check_unmodified(buf, 0x4B6C3E, ["mov eax, 0x4ec4ec4f", "mul ebx"], 7)
    put_bytes(
        buf, 0x4B6C3E,
        assemble_at(0x4B6C3E, [
            "mov eax, ebx",
            f"call {hex(syms['getSmoothMenuRainbow'])}",
            "jmp 0x4b6c54",
        ]).ljust(0x4B6C54 - 0x4B6C3E, b"\xcc"),
        budget=0x4B6C54 - 0x4B6C3E,
    )

    # Fullscreen exit fades (ModeSelect 0x4b9689..0x4b96a5, LevelSelect 0x4b896b..0x4b8987)
    for va_start, va_end in [(0x4B9689, 0x4B96A5), (0x4B896B, 0x4B8987)]:
        _check_unmodified(buf, va_start, ["imul ecx, ecx, 0xff", "mov eax, 0x51eb851f"], 11)
        put_bytes(
            buf, va_start,
            assemble_at(va_start, [
                "mov ecx, edi",
                f"call {hex(syms['calcSubExitOverlayAlpha'])}",
                f"jmp {hex(va_end)}",
            ]).ljust(va_end - va_start, b"\xcc"),
            budget=va_end - va_start,
        )

    # 10f. ConfirmExitGameMenu collapse/expand, fade pct, 5 rainbow highlights, YES blackout
    _check_unmodified(buf, 0x4A8DF1, ["mov eax, DWORD PTR [ebx+0x8]", "mov DWORD PTR [ebp-0x54], 0x7e"], 10)
    put_bytes(
        buf, 0x4A8DF1,
        assemble_at(0x4A8DF1, [
            f"call {hex(syms['cegBoxHeight'])}",
            "mov DWORD PTR [ebp-0x54], eax",
            "jmp 0x4a8e9b",
        ]).ljust(0x4A8E98 - 0x4A8DF1, b"\xcc"),
        budget=0x4A8E98 - 0x4A8DF1,
    )
    _check_unmodified(buf, 0x4A8EE3, ["mov esi, 0x64", "cmp eax, 0x8"], 8)
    put_bytes(
        buf, 0x4A8EE3,
        assemble_at(0x4A8EE3, [
            f"call {hex(syms['cegFadePct'])}",
            "jmp 0x4a8f3f",
        ]).ljust(0x4A8F3F - 0x4A8EE3, b"\xcc"),
        budget=0x4A8F3F - 0x4A8EE3,
    )
    _check_unmodified(buf, 0x4A8F84, ["mov eax, 0x4ec4ec4f", "mul edi"], 7)
    put_bytes(
        buf, 0x4A8F84,
        assemble_at(0x4A8F84, [
            "mov eax, edi",
            f"call {hex(syms['getSmoothMenuRainbow'])}",
            "mov ecx, eax",
            f"mov ebx, {hex(base_syms['G_RB_TMP'])}",
            "jmp 0x4a8fa3",
        ]).ljust(0x4A8FA3 - 0x4A8F84, b"\xcc"),
        budget=0x4A8FA3 - 0x4A8F84,
    )
    for va_start, va_end in [(0x4A9045, 0x4A905D), (0x4A90F8, 0x4A9110)]:
        _check_unmodified(buf, va_start, ["mov eax, 0x4ec4ec4f", "mul edi"], 7)
        put_bytes(
            buf, va_start,
            assemble_at(va_start, [
                "mov eax, edi",
                f"call {hex(syms['getSmoothMenuRainbow'])}",
                "mov ecx, eax",
                f"jmp {hex(va_end)}",
            ]).ljust(va_end - va_start, b"\xcc"),
            budget=va_end - va_start,
        )
    for va_start, va_end in [(0x4A926B, 0x4A9283), (0x4A9390, 0x4A93A8)]:
        _check_unmodified(buf, va_start, ["mov eax, 0x4ec4ec4f", "mul edi"], 7)
        put_bytes(
            buf, va_start,
            assemble_at(va_start, [
                "mov eax, edi",
                f"call {hex(syms['getSmoothMenuRainbow'])}",
                f"jmp {hex(va_end)}",
            ]).ljust(va_end - va_start, b"\xcc"),
            budget=va_end - va_start,
        )
    _check_unmodified(buf, 0x4A94F3, ["xorps xmm0, xmm0", "cvtsi2ss xmm0, eax"], 7)
    put_bytes(
        buf, 0x4A94F3,
        assemble_at(0x4A94F3, [
            "mov eax, DWORD PTR [ebp-0x48]",
            f"call {hex(syms['cegBlackoutAlpha'])}",
            "jmp 0x4a9511",
        ]).ljust(0x4A9511 - 0x4A94F3, b"\xcc"),
        budget=0x4A9511 - 0x4A94F3,
    )

    # 10g. DebriefMenu (Level Complete): Stages 1..15
    # Stage 1 Title rainbow (0x4a9b24..0x4a9b45, cleans stack +0x14 left from prior call)
    _check_unmodified(buf, 0x4A9B24, ["mov eax, DWORD PTR [esi+0x44]", "cdq"], 4)
    put_bytes(
        buf, 0x4A9B24,
        assemble_at(0x4A9B24, [
            f"call {hex(syms['debTitleRb'])}",
            "add esp, 0x14",
            "jmp 0x4a9b45",
        ]).ljust(0x4A9B45 - 0x4A9B24, b"\xcc"),
        budget=0x4A9B45 - 0x4A9B24,
    )

    # Unified Debrief color stages:
    # (start_va, end_va, first_instrs, check_gate, start_tick, target_rgb, white_enter, rb_off, extra_post)
    deb_color_hooks = [
        # Stage 2 Subtitle (0x4a9bdd..0x4a9c9f)
        (0x4A9BDD, 0x4A9C9F, ["cmp DWORD PTR [esi+0x8], 0x0"], 1, 0, 0, 1, []),
        # Stage 3 TIME header (0x4a9cf5..0x4a9db0)
        (0x4A9CF5, 0x4A9DB0, ["cmp DWORD PTR [esi+0x8], 0x0", "mov ecx, 0xd"], 2, 0, 0, 2, []),
        # Stage 4 Row 3 label (0x4a9e80..0x4a9f24, after 0x4a9e7d add esp, 0x20)
        (0x4A9E80, 0x4A9F24, ["cmp eax, 0x3", "jl 0x4a9ff7"], 3, 0x3B3B3B, 1, 0, ["jl 0x4a9ff7"]),
        # Stage 5 Row 3 value (0x4a9ff7..0x4aa0bc)
        (0x4A9FF7, 0x4AA0BC, ["mov esi, DWORD PTR [esi+0x44]", "cmp esi, 0x4"], 4, 0xA1A1A1, 1, 0, ["cmp DWORD PTR [esi+0x44], 4", "jl 0x4aa227"]),
        # Stage 6a Row 4 EDGE TIME label (0x4aa227..0x4aa2d6)
        (0x4AA227, 0x4AA2D6, ["mov eax, DWORD PTR [ebp-0xec]", "mov ecx, DWORD PTR [eax+0x44]"], 5, 0xA1A1A1, 1, 0, ["mov ecx, DWORD PTR [eax+0x44]", "cmp ecx, 5", "jl 0x4aa6db"]),
        # Stage 6b bestTime > 0 (0x4aa401..0x4aa50d -> resume 0x4aa5ab, inc edi & [ebp-0xf4] = edi)
        (0x4AA401, 0x4AA50D, ["cmp DWORD PTR [eax+0x8], 0x0", "mov eax, DWORD PTR [eax+0x44]"], 5, 0, 1, None, ["inc edi", "mov DWORD PTR [ebp-0xf4], edi"]),
        # Stage 6b bestTime <= 0 (0x4aa50d..0x4aa5ab)
        (0x4AA50D, 0x4AA5AB, ["cmp DWORD PTR [eax+0x8], 0x0"], 5, 0xA1A1A1, 1, 0, []),
        # Stage 7a Row 5 highlight bar (0x4aa6db..0x4aa7eb, [ebp-0xf0] already copied from [ebp-0xdc]!)
        (0x4AA6DB, 0x4AA7EB, ["mov eax, DWORD PTR [ebp-0xec]", "mov eax, DWORD PTR [eax+0x44]"], 6, 0, 1, None, ["cmp DWORD PTR [eax+0x44], 6", "jl 0x4aaa64", "mov ecx, DWORD PTR [ebp-0xdc]", "mov DWORD PTR [ebp-0xf0], ecx"]),
        # Stage 7b Row 5 delta text (0x4aa87f..0x4aa943, writes [ebp-0xf0])
        (0x4AA87F, 0x4AA943, ["cmp DWORD PTR [ecx+0x8], 0x0"], 6, 0x3B3B3B, 1, 0, ["mov ecx, DWORD PTR [ebp-0xdc]", "mov DWORD PTR [ebp-0xf0], ecx"]),
        # Stage 8a NEW RECORD color (0x4aadbf..0x4aaebe, check gate >= 14)
        (0x4AADBF, 0x4AAEBE, ["mov ecx, DWORD PTR [ebp-0xec]", "mov eax, DWORD PTR [ecx+0x44]"], 14, 0, 1, None, ["cmp DWORD PTR [ecx+0x44], 14", "jl 0x4ab00e"]),
        # Stage 13a Row 6 Score (0x4ab722..0x4ab7c5, after xor edi, edi & add esp, 0x40)
        (0x4AB722, 0x4AB7C5, ["cmp eax, 0x27", "jl 0x4ab93a"], 39, 0x3B3B3B, 1, 0, ["jl 0x4ab93a"]),
        # Stage 13b Row 7 Prisms (0x4ab93f..0x4ab9ed)
        (0x4AB93F, 0x4AB9ED, ["mov edx, DWORD PTR [ebp-0xec]", "mov eax, DWORD PTR [edx+0x44]"], 40, 0x3B3B3B, 1, 0, ["cmp DWORD PTR [edx+0x44], 40", "jl 0x4abc65"]),
    ]
    for s_va, e_va, instrs, st, tgt, we, rboff, gate_prefix in deb_color_hooks:
        _check_unmodified(buf, s_va, instrs)
        lines = []
        # Gate / check runs BEFORE calling debColorStage! Split gate vs post properly:
        if s_va in (0x4A9E80, 0x4AB722):
            lines.extend([f"cmp eax, {st}", gate_prefix[0]])
        elif s_va == 0x4A9FF7:
            lines.extend(["cmp DWORD PTR [esi+0x44], 4", "jl 0x4aa227"])
        elif s_va == 0x4AA227:
            lines.extend(["mov eax, DWORD PTR [ebp-0xec]", "cmp DWORD PTR [eax+0x44], 5", "jl 0x4aa6db"])
        elif s_va == 0x4AA6DB:
            lines.extend(["mov eax, DWORD PTR [ebp-0xec]", "cmp DWORD PTR [eax+0x44], 6", "jl 0x4aaa64"])
        elif s_va == 0x4AADBF:
            lines.extend(["mov ecx, DWORD PTR [ebp-0xec]", "cmp DWORD PTR [ecx+0x44], 14", "jl 0x4ab00e"])
        elif s_va == 0x4AB93F:
            lines.extend(["mov edx, DWORD PTR [ebp-0xec]", "cmp DWORD PTR [edx+0x44], 40", "jl 0x4abc65"])

        cx_val = (0 if rboff is None else (rboff << 8)) | we
        lines.append(f"mov eax, {st}")
        lines.append(f"mov edx, {hex(tgt)}")
        if rboff is None:
            lines.append("mov ecx, edi")
            lines.append("shl ecx, 8")
            if we:
                lines.append("or cl, 1")
        else:
            lines.append(f"mov ecx, {hex(cx_val)}")
        lines.append(f"call {hex(syms['debColorStage'])}")

        if s_va == 0x4AA401:
            lines.extend(["inc edi", "mov DWORD PTR [ebp-0xf4], edi", "jmp 0x4aa5ab"])
        else:
            if s_va in (0x4AA6DB, 0x4AA87F):
                lines.extend(["mov ecx, DWORD PTR [ebp-0xdc]", "mov DWORD PTR [ebp-0xf0], ecx"])
            elif s_va in (0x4A9FF7, 0x4AADBF, 0x4AB722, 0x4AB93F):
                lines.append("mov DWORD PTR [ebp-0xe0], ecx")
            lines.append(f"jmp {hex(e_va)}")
        put_bytes(
            buf, s_va,
            assemble_at(s_va, lines).ljust(e_va - s_va, b"\xcc"),
            budget=e_va - s_va,
        )

    # Debrief Stages 8b, 8c, 9, 10, 11, 12a, 12b, 14a, 14b, 15
    _check_unmodified(buf, 0x4AAECE, ["cmp eax, 0x13"])
    put_bytes(
        buf, 0x4AAECE,
        assemble_at(0x4AAECE, [f"call {hex(syms['debNrScale'])}", "jmp 0x4aaeff"]).ljust(0x4AAEFF - 0x4AAECE, b"\xcc"),
        budget=0x4AAEFF - 0x4AAECE,
    )
    _check_unmodified(buf, 0x4AAF17, ["cmp DWORD PTR [edx+0x44], 0x13"])
    _check_unmodified(buf, 0x4AAF1B, ["mov DWORD PTR [ebp-0xdc], ecx"])
    put_bytes(
        buf, 0x4AAF17,
        assemble_at(0x4AAF17, [
            "mov DWORD PTR [ebp-0xdc], ecx",
            "lea edi, [eax+0xa]",
            "cmp DWORD PTR [edx+0x44], 19",
            "jl 0x4aaf37",
            f"call {hex(syms['debNrMarquee'])}",
            "jmp 0x4aaf37",
        ]).ljust(0x4AAF37 - 0x4AAF17, b"\xcc"),
        budget=0x4AAF37 - 0x4AAF17,
    )
    _check_unmodified(buf, 0x4AABE2, ["cmp eax, 0x19", "jl 0x4aadad"], 9)
    put_bytes(
        buf, 0x4AABE2,
        assemble_at(0x4AABE2, [
            "cmp eax, 25",
            "jl 0x4aadad",
            f"call {hex(syms['debRankSlideX'])}",
            "push ebx",
            "push 0x3b3b3b",
            "push 0xffffff",
            "jmp 0x4aac1b",
        ]).ljust(0x4AAC1B - 0x4AABE2, b"\xcc"),
        budget=0x4AAC1B - 0x4AABE2,
    )
    _check_unmodified(buf, 0x4AB014, ["mov eax, DWORD PTR [eax+0x44]", "cmp eax, 0x22"], 6)
    put_bytes(
        buf, 0x4AB014,
        assemble_at(0x4AB014, [
            "cmp DWORD PTR [eax+0x44], 34",
            "jl 0x4ab5a1",
            f"call {hex(syms['debBoxHeight'])}",
            "jmp 0x4ab04a",
        ]).ljust(0x4AB04A - 0x4AB014, b"\xcc"),
        budget=0x4AB04A - 0x4AB014,
    )
    _check_unmodified(buf, 0x4AB12C, ["movzx esi, BYTE PTR ds:0x5f91e8"], 7)
    put_bytes(
        buf, 0x4AB12C,
        assemble_at(0x4AB12C, [
            f"call {hex(syms['debBarWidth'])}",
            "jmp 0x4ab152",
        ]).ljust(0x4AB152 - 0x4AB12C, b"\xcc"),
        budget=0x4AB152 - 0x4AB12C,
    )
    _check_unmodified(buf, 0x4AB197, ["cmp ecx, 0x2b"])
    put_bytes(
        buf, 0x4AB197,
        assemble_at(0x4AB197, [
            f"call {hex(syms['debRankStamp'])}",
            "jmp 0x4ab201",
        ]).ljust(0x4AB201 - 0x4AB197, b"\xcc"),
        budget=0x4AB201 - 0x4AB197,
    )
    _check_unmodified(buf, 0x4AB33D, ["mov eax, DWORD PTR [ebp-0xec]", "cmp DWORD PTR [eax+0x8], 0x0"], 10)
    put_bytes(
        buf, 0x4AB33D,
        assemble_at(0x4AB33D, [
            f"call {hex(syms['debRankOverlay'])}",
            "jmp 0x4ab480",
        ]).ljust(0x4AB480 - 0x4AB33D, b"\xcc"),
        budget=0x4AB480 - 0x4AB33D,
    )
    _check_unmodified(buf, 0x4ABC79, ["cmp DWORD PTR [ecx+0x8], edi"])
    put_bytes(
        buf, 0x4ABC79,
        assemble_at(0x4ABC79, [
            f"call {hex(syms['debBtnQuadAlpha'])}",
            "jmp 0x4abcd1",
        ]).ljust(0x4ABCD1 - 0x4ABC79, b"\xcc"),
        budget=0x4ABCD1 - 0x4ABC79,
    )
    _check_unmodified(buf, 0x4ABCEF, ["mov edx, DWORD PTR [ebp-0x104]"], 6)
    put_bytes(
        buf, 0x4ABCEF,
        assemble_at(0x4ABCEF, [
            "mov edx, DWORD PTR [ebp-0x104]",
            "lea eax, [esi+edx+3]",
            f"call {hex(syms['getSmoothMenuRainbow'])}",
            "mov edx, eax",
            "jmp 0x4abd10",
        ]).ljust(0x4ABD0B - 0x4ABCEF, b"\xcc"),
        budget=0x4ABD0B - 0x4ABCEF,
    )
    _check_unmodified(buf, 0x4ABE4F, ["movzx edx, BYTE PTR ds:0x5f88e0"], 7)
    put_bytes(
        buf, 0x4ABE4F,
        assemble_at(0x4ABE4F, [
            f"call {hex(syms['debriefFillExitRgba'])}",
            "jmp 0x4abea9",
        ]).ljust(0x4ABEA9 - 0x4ABE4F, b"\xcc"),
        budget=0x4ABEA9 - 0x4ABE4F,
    )

    # 10h. WorldScreen "edge time" HUD (0x47ac20) & Instructions demo (0x47a2d0)
    _check_unmodified(buf, 0x47AC9C, ["mov eax, DWORD PTR [esi+edi*4+0x78]", "cmp eax, 0xa"], 7)
    put_bytes(
        buf, 0x47AC9C,
        assemble_at(0x47AC9C, [
            f"call {hex(syms['hudTimerSetup'])}",
            "jmp 0x47ad55",
        ]).ljust(0x47AD55 - 0x47AC9C, b"\xcc"),
        budget=0x47AD55 - 0x47AC9C,
    )
    _check_unmodified(buf, 0x47AD62, ["mov eax, DWORD PTR [esi+ecx*4+0x78]", "cdq"], 5)
    put_bytes(
        buf, 0x47AD62,
        assemble_at(0x47AD62, [
            f"call {hex(syms['hudTimerDigitsAndColor'])}",
            "jmp 0x47adde",
        ]).ljust(0x47ADDE - 0x47AD62, b"\xcc"),
        budget=0x47ADDE - 0x47AD62,
    )
    _check_unmodified(buf, 0x47AF27, ["movss xmm0, DWORD PTR [ebp-0x4]", "push 0xaa"], 10)
    put_bytes(
        buf, 0x47AF27,
        assemble_at(0x47AF27, [
            f"call {hex(syms['hudRestoreMat'])}",
            "jmp 0x47af5a",
        ]).ljust(0x47AF5A - 0x47AF27, b"\xcc"),
        budget=0x47AF5A - 0x47AF27,
    )
    _check_unmodified(buf, 0x47A2D8, ["mov esi, DWORD PTR [ebp+0x10]", "mov eax, 0x2e8ba2e9"], 8)
    put_bytes(
        buf, 0x47A2D8,
        assemble_at(0x47A2D8, [
            "push edi",
            f"call {hex(syms['edgeDemoSetup'])}",
            "jmp 0x47a341",
        ]).ljust(0x47A341 - 0x47A2D8, b"\xcc"),
        budget=0x47A341 - 0x47A2D8,
    )
    _check_unmodified(buf, 0x47A43E, ["push 0x1", "lea ecx, [esi+0xa]"], 5)
    put_bytes(
        buf, 0x47A43E,
        assemble_at(0x47A43E, [f"call {hex(syms['demoSepPush'])}"], 5),
        budget=5,
    )

    # Glyph 10 (Edge Time separator) keeps its stock prime rects (count 3 @ 0x58640A, rects @ 0x5F3B88);
    # setSepGlyph rewrites the 3 rects at runtime for the modern "." (G_TIME_FMT == 1).
    off_58640a = va2off(0x58640A)
    if buf[off_58640a:off_58640a + 1] != b"\x03":
        raise RuntimeError(f"Expected original byte 0x03 at 0x58640A, got {buf[off_58640a:off_58640a+1].hex()}")

    orig_glyph10 = bytes([0, 0, 1, 6, 1, 0, 1, 5, 2, 0, 1, 4, 255, 255, 255, 255, 255, 255, 255, 255])
    off_5f3b88 = va2off(0x5F3B88)
    if bytes(buf[off_5f3b88:off_5f3b88 + 20]) != orig_glyph10:
        raise RuntimeError(f"Unexpected original glyph 10 rects at 0x5F3B88: {list(buf[off_5f3b88:off_5f3b88+20])}")

    return {
        "Block S":  (len(code_s),  BLOCK_S_END  - BLOCK_S_START),
        "Block T":  (len(code_t),  BLOCK_T_END  - BLOCK_T_START),
        "Block U":  (len(code_u),  BLOCK_U_END  - BLOCK_U_START),
        "Block V":  (len(code_v),  BLOCK_V_END  - BLOCK_V_START),
        "Block W":  (len(code_w),  BLOCK_W_END  - BLOCK_W_START),
        "Block X":  (len(code_x),  BLOCK_X_END  - BLOCK_X_START),
        "Block Y":  (len(code_y),  BLOCK_Y_END  - BLOCK_Y_START),
        "Block Z":  (len(code_z),  BLOCK_Z_END  - BLOCK_Z_START),
    }
