import math
import struct
from .common import assemble_at, expect_replace, link_block_with_symbols, put_bytes, put_u32, va2off
from .ui_scale_menu_ini import (
    FORMAT_FLOAT_WSTR_VA,
    MAIN_DRAW_AUTO_HOOK_VA,
    PAUSE_CAM_DRAW_HOOK_VA,
    PAUSE_DRAW_DIV_VA,
    PAUSE_DRAW_TEXT_VA,
    options_row_text_y,
)

# Dedicated .bss tail globals (0x5fa800..0x5fa820 inside RW .data page after stock .bss ends at 0x5fa7cc)
G_CAM_FOV       = 0x5fa800  # float: -1.0f = pc (default), -2.0f = mobile, [0.0, 90.0] = degrees
G_INI_CAM_FOV   = 0x5fa804  # float: -99.0f = unset in INI, else -2.0/-1.0/[0.0, 90.0]
G_LAST_INI_FOV  = 0x5fa808  # float: last INI camera_fov persisted in ttsav (-99.0f = unset)
G_CAM_DIST      = 0x5fa810  # float: -1.0f = mobile, [0.1, 10.0] = distance multiplier (1.0f = pc)
G_INI_CAM_DIST  = 0x5fa814  # float: 0.0f = unset in INI, else -1.0/[0.1, 10.0]
G_LAST_INI_DIST = 0x5fa818  # float: last INI camera_distance persisted in ttsav (0.0f = unset)
G_CAM_INITED    = 0x5fa81c  # byte: 0 until iniCamHook performs one-time .bss default initialization
G_ACTIVE_MUL    = 0x5fa820  # float: active camera distance multiplier from GameCamera::Update

# .rdata strings & preset table inside 0x5e2904..0x5e2960 (shared fixed constants)
DIST_PRESETS_VA = 0x5e2904  # 8 floats (32B, ends 0x5e2924)
STR_MOBILE_LC   = 0x5e2926  # "mobile\0"       (7B)
STR_PC_LC       = 0x5e292d  # "pc\0"           (3B)
WSTR_PC_VAL     = 0x5e2930  # SSO std::wstring u"pc"     (24B)
WSTR_MOBILE_VAL = 0x5e2948  # SSO std::wstring u"mobile" (24B)

# .rdata label & float constants inside 0x5e29c4..0x5e2a00 (after JTBL_11 at 0x5e2998..0x5e29c4)
WSTR_FOV_BUF    = 0x5e29c4  # u"dolly zoom\0" (22B + 2B pad)
WSTR_FOV_LBL    = 0x5e29dc  # std::wstring u"dolly zoom" (24B, ends 0x5e29f4)
FLT_P1_VA       = 0x5e29f4  # 0.1f
FLT_10_VA       = 0x5e29f8  # 10.0f
FLT_TEPS_VA     = 0x5e29fc  # 0.0005f

# Dedicated .text blocks reclaimed from unused helpers (verified 0 external xrefs)
BLOCK_F_START   = 0x479010
BLOCK_F_END     = 0x4791cb  # 443 bytes (camera distance/stretch helpers, presets, menu actions)
BLOCK_G_START   = 0x52c560
BLOCK_G_END     = 0x52caa0  # 1,344 bytes (camFovVertigoHook, INI/ttsav hooks, Main/Pause draw, tables)
BLOCK_H_START   = 0x4248e0
BLOCK_H_END     = 0x4249b2  # 210 bytes (saveActiveMul, adjustNearFarHook to preserve 24-bit Z precision)
BLOCK_H2_START  = 0x4234d0
BLOCK_H2_END    = 0x4235e0  # 272 bytes (skyboxProjWrap for Renderer::renderSkybox at 0x407805)

# Free tail in existing save block & jump table entries
SLOT_SAVE_WRITE = 0x4080cd  # inside save_write tail (0x4080cd..0x408120, 83B)
JTBL_ENTRY_7    = 0x5e2998 + 7 * 4
JTBL_ENTRY_8    = 0x5e2998 + 8 * 4

# IEEE-754 bit patterns
F_M99 = 0xc2c60000  # -99.0f (unset INI/ttsav sentinel for camera_fov)
F_M2  = 0xc0000000  # -2.0f  (mobile FOV mode)
F_M1  = 0xbf800000  # -1.0f  (pc FOV mode / mobile distance mode)
F_0   = 0x00000000  #  0.0f
F_P1  = 0x3dcccccd  #  0.1f
F_P5  = 0x3f000000  #  0.5f
F_1   = 0x3f800000  #  1.0f
F_10  = 0x41200000  # 10.0f
F_22  = 0x41b00000  # 22.0f
F_35  = 0x420c0000  # 35.0f
F_90  = 0x42b40000  # 90.0f
F_EPS = 0x3a83126f  #  0.001f
F_TEPS = 0x3a03126f #  0.0005f
F_TAN11 = struct.unpack("<I", struct.pack("<f", math.tan(math.radians(11.0))))[0]


def _read_rel_target(buf: bytearray, instr_va: int, expected_opcode: int) -> int:
    off = va2off(instr_va)
    actual_op = buf[off]
    assert actual_op == expected_opcode, (
        f"Expected opcode {hex(expected_opcode)} at {hex(instr_va)}, got {hex(actual_op)}"
    )
    rel = struct.unpack_from("<i", buf, off + 1)[0]
    return instr_va + 5 + rel


def apply_camera_angle(
    buf: bytearray,
    enable_menu_ini: bool = True,
    include_time_row: bool = False,
    include_skip_row: bool = False,
) -> dict[str, tuple[int, int]]:
    """
    Install two independent camera settings in Main Options (rows 7-8), Pause Menu Page 1,
    `settings.ini` (`[graphics] camera_distance` & `camera_fov`), and `local_savedata.ttsav`:
      - `camera distance`: `mobile` (-1.0) or relative multiplier around PC default (1.0), cycling
        `[mobile, 0.5, 0.65, 0.75, 1, 1.25, 1.5, 2]` in-game and allowing any `[0.1, 10.0]` float in INI.
      - `dolly zoom` (INI `camera_fov` / save `camFov`): `pc` (-1.0), `mobile` (-2.0), or `[0.0, 90.0]` degrees (`[pc, mobile, 0, 5, 10, 15, 20, 25, 30]`
        in-game), where `mobile` matches Mobile `M_ortho` (`1.0 deg` horizontal + `1.65/1.55` Y stretch),
        `0` gives pure isometric without Y stretch, and `1..90` applies dolly-zoomed perspective.
    """
    # Note: G_CAM_* globals sit in zero-initialized .bss tail (0x5fa800..0x5fa820, unbacked in file),
    # so iniCamHook initializes defaults once on first startup when G_CAM_INITED == 0.

    # 1. Populate shared .rdata preset table, strings & SSO std::wstrings
    put_bytes(buf, 0x5e2904, b"\x00" * 34, 34)
    put_bytes(
        buf,
        DIST_PRESETS_VA,
        struct.pack("<8I", F_M1, 0x3f000000, 0x3f266666, 0x3f400000, F_1, 0x3fa00000, 0x3fc00000, 0x40000000),
        32,
    )
    put_bytes(buf, STR_MOBILE_LC, b"mobile\x00", 7)
    put_bytes(buf, STR_PC_LC,     b"pc\x00", 3)

    sso_pc = "pc\x00".encode("utf-16le").ljust(16, b"\x00") + struct.pack("<II", 2, 7)
    sso_mob = "mobile\x00".encode("utf-16le").ljust(16, b"\x00") + struct.pack("<II", 6, 7)
    put_bytes(buf, WSTR_PC_VAL, sso_pc, 24)
    put_bytes(buf, WSTR_MOBILE_VAL, sso_mob, 24)

    fov_lbl_utf16 = "dolly zoom\x00".encode("utf-16le").ljust(24, b"\x00")
    put_bytes(buf, WSTR_FOV_BUF, fov_lbl_utf16, 24)
    put_bytes(buf, WSTR_FOV_LBL, struct.pack("<I", WSTR_FOV_BUF) + bytes(12) + struct.pack("<II", 10, 10), 24)
    put_u32(buf, FLT_P1_VA, F_P1)
    put_u32(buf, FLT_10_VA, F_10)
    put_u32(buf, FLT_TEPS_VA, F_TEPS)

    # Resolve chain targets placed by ui_scale_menu_ini (if enabled)
    ext_syms: dict[str, int] = {}
    if enable_menu_ini:
        orig_load_ttsav = _read_rel_target(buf, 0x408060, 0xe8)
        orig_save_ttsav = _read_rel_target(buf, 0x4080c8, 0xe9)
        orig_draw_auto = _read_rel_target(buf, MAIN_DRAW_AUTO_HOOK_VA, 0xe8)
        orig_pause_back = _read_rel_target(buf, PAUSE_CAM_DRAW_HOOK_VA, 0xe8)
        orig_ini_free = _read_rel_target(buf, 0x484d59, 0xe8)
        ext_syms["origLoadTtsav"] = orig_load_ttsav
        ext_syms["origDrawAuto"] = orig_draw_auto
        ext_syms["origPauseBack"] = orig_pause_back
        ext_syms["origIniFree"] = orig_ini_free

    # 2. Assemble Block F (helpers, constant table, Y-stretch hook, and preset cyclers)
    block_f_lines = [
        ".globl checkInLevel, pushDeff, perspScaleY, camDistAction, camFovAction, c_tbl",

        # Preserves eax, ebx, esi, edi, ebp, FPU/XMM; sets ZF=0 (edx=1) and ecx=&c_tbl iff in-level
        "checkInLevel:",
        "xor edx, edx",
        "mov ecx, DWORD PTR ds:0x5f9064",   # Game*
        "jecxz cil_ret",
        "cmp BYTE PTR [ecx+0x2120], dl",     # level loaded & active
        "je cil_ret",
        "mov ecx, DWORD PTR ds:0x5f9080",   # GameEngine*
        "jecxz cil_ret",
        "cmp DWORD PTR [ecx+0x50], 0x11",   # state >= 0x11
        "jb cil_ret",
        "inc edx",
        "cil_ret:",
        "mov ecx, OFFSET c_tbl",
        "test edx, edx",
        "ret",

        # pushDeff: pushes effective distance ratio d_eff onto FPU st(0); preserves xmm0, eax, ecx, edx, ebx, esi, edi
        "pushDeff:",
        f"fld DWORD PTR ds:{hex(G_CAM_DIST)}",
        "ftst",
        "fnstsw ax",
        "sahf",
        "ja pde_ret",                       # > 0: numeric multiplier directly
        "fstp st(0)",
        "movss xmm1, DWORD PTR [ebp-0x10]", # aspect
        "movaps xmm2, xmm1",
        "mulss xmm1, DWORD PTR [ecx+20]",   # 0.11558293f
        "addss xmm1, DWORD PTR [ecx+24]",   # 0.01733744f
        "maxss xmm1, DWORD PTR [ecx+28]",   # 0.19071183f
        "minss xmm1, DWORD PTR [ecx+32]",   # 0.26699657f (base_num)
        "maxss xmm2, DWORD PTR [ecx+52]",   # max(aspect, 1.0f)
        "mulss xmm2, DWORD PTR [ecx+40]",   # * tan(11 deg)
        "divss xmm1, xmm2",
        "push eax",
        "movss DWORD PTR [esp], xmm1",
        "fld DWORD PTR [esp]",
        "pop eax",
        "pde_ret:",
        "ret",

        # Tail hook at 0x4357f7 (pOut in eax): Mobile projection Y stretch (1.65 / 1.55) when camera_fov == mobile
        "perspScaleY:",
        f"cmp DWORD PTR ds:{hex(G_CAM_FOV)}, {hex(F_M2)}",
        "jne psy_done",
        "call checkInLevel",
        "je psy_done",
        "fld DWORD PTR [eax+0x14]",
        "fmul DWORD PTR [ecx]",
        "fstp DWORD PTR [eax+0x14]",
        "psy_done:",
        "pop ebp",
        "ret 0x10",

        # Menu row 7/8 actions: cycle camera distance / fov preset list
        "camDistAction:",
        f"mov edx, {hex(G_CAM_DIST)}",
        f"mov eax, {hex(DIST_PRESETS_VA)}",
        "push 8",
        "pop ecx",
        "jmp cfl_action",

        "camFovAction:",
        f"mov edx, {hex(G_CAM_FOV)}",
        "mov eax, OFFSET fovPresets",
        "push 9",
        "pop ecx",
        "cfl_action:",
        "push edx",
        "movss xmm0, DWORD PTR [edx]",
        "call cycleFloatList",
        "pop edx",
        "movss DWORD PTR [edx], xmm0",
        "jmp 0x484d75",

        # cycleFloatList(eax = float* tbl, ecx = count, xmm0 = current) -> xmm0 = next/prev preset
        "cycleFloatList:",
        "push ebx",
        "mov ebx, DWORD PTR ds:0x5f9080",
        "mov bl, BYTE PTR [ebx+0xb3]",
        "pcmpeqd xmm2, xmm2",
        "psrld xmm2, 1",                     # 0x7fffffff abs mask
        "movss xmm3, DWORD PTR ds:c_tbl+48", # 0.001f epsilon
        "xor edx, edx",
        "cfl_find:",
        "movss xmm1, DWORD PTR [eax+edx*4]",
        "subss xmm1, xmm0",
        "andps xmm1, xmm2",
        "ucomiss xmm1, xmm3",
        "jb cfl_matched",
        "inc edx",
        "cmp edx, ecx",
        "jb cfl_find",
        # Unmatched custom INI value (>= 0): step relative to sorted non-negative entries
        "test bl, bl",
        "jne cfl_unm_l",
        "xor edx, edx",
        "addss xmm0, xmm3",
        "cfl_unm_r:",
        "cmp DWORD PTR [eax+edx*4], 0",
        "jl cfl_unm_rn",
        "ucomiss xmm0, DWORD PTR [eax+edx*4]",
        "jb cfl_pick",
        "cfl_unm_rn:",
        "inc edx",
        "cmp edx, ecx",
        "jb cfl_unm_r",
        "xor edx, edx",
        "jmp cfl_pick",
        "cfl_unm_l:",
        "lea edx, [ecx-1]",
        "subss xmm0, xmm3",
        "cfl_unm_ll:",
        "cmp DWORD PTR [eax+edx*4], 0",
        "jl cfl_pick0",
        "ucomiss xmm0, DWORD PTR [eax+edx*4]",
        "ja cfl_pick",
        "dec edx",
        "jns cfl_unm_ll",
        "cfl_pick0:",
        "xor edx, edx",
        "jmp cfl_pick",
        "cfl_matched:",
        "test bl, bl",
        "jne cfl_m_l",
        "inc edx",
        "cmp edx, ecx",
        "jb cfl_pick",
        "xor edx, edx",
        "jmp cfl_pick",
        "cfl_m_l:",
        "dec edx",
        "jns cfl_pick",
        "lea edx, [ecx-1]",
        "cfl_pick:",
        "movss xmm0, DWORD PTR [eax+edx*4]",
        "pop ebx",
        "ret",

        "fovPresets:",
        f".long {hex(F_M1)}, {hex(F_M2)}, {hex(F_0)}, 0x40a00000, {hex(F_10)}, 0x41700000, 0x41a00000, 0x41c80000, 0x41f00000",

        "c_tbl:",
        f".long 0x3f884211, {hex(F_22)}, {hex(F_35)}, 0x3c0efa35",  # +0..+12:  1.65/1.55, 22, 35, pi/360
        ".long 0x42e52ee1, 0x3decb6be, 0x3c8e0731, 0x3e434abb",     # +16..+28: 360/pi, asp_m, asp_b, num_lo
        f".long 0x3e88b3a3, 0x3e348e95, {hex(F_TAN11)}, {hex(F_P5)}", # +32..+44: num_hi, tan10, tan11, 0.5f
        f".long {hex(F_EPS)}, {hex(F_1)}, 0x3f4ccccd",              # +48..+56: 0.001f, 1.0f, 0.80f
        ".long 0x3ec49808",                                         # +60: 22.0f * [0x57a0f0] (skybox ref fovy rad)
    ]

    put_bytes(buf, BLOCK_F_START, b"\xcc" * (BLOCK_F_END - BLOCK_F_START))
    block_f, syms_f = link_block_with_symbols(
        BLOCK_F_START, block_f_lines, BLOCK_F_END - BLOCK_F_START
    )
    put_bytes(buf, BLOCK_F_START, block_f, BLOCK_F_END - BLOCK_F_START)

    # Install Y-stretch hook at 0x4357f7
    expect_replace(
        buf,
        0x4357f7,
        b"\x5d\xc2\x10\x00\xcc",
        assemble_at(0x4357f7, [f"jmp {hex(syms_f['perspScaleY'])}"], 5),
    )

    allocs: dict[str, tuple[int, int]] = {
        "block_f": (len(block_f), BLOCK_F_END - BLOCK_F_START),
    }

    # 3. Assemble Block G (camFovVertigoHook, INI/ttsav load & save, Main/Pause row 7 & 8 draw)
    g_ext = {
        "checkInLevel": syms_f["checkInLevel"],
        "pushDeff": syms_f["pushDeff"],
        "c_tbl": syms_f["c_tbl"],
        **ext_syms,
    }
    block_g_lines = [
        ".globl camFovVertigoHook",

        # mapFov(xmm0 -> xmm0): al=is_568, ecx=&c_tbl, xmm3=1.0f
        "mapFov:",
        "ucomiss xmm0, DWORD PTR [ecx+4]",  # 22.0f?
        "jne mf_clamp",
        "test al, al",
        "je mf_not568",
        "movss xmm0, DWORD PTR [ecx+4]",    # 22.0f (level568 button)
        f"cmp DWORD PTR ds:{hex(G_CAM_FOV)}, {hex(F_M2)}",
        "jne mf_ret",
        "movss xmm0, DWORD PTR [ecx+8]",    # 35.0f (level568 button in mobile mode)
        "mf_ret:",
        "ret",
        "mf_not568:",
        "movaps xmm0, xmm3",                # 1.0f (mobile ortho horizontal)
        f"cmp DWORD PTR ds:{hex(G_CAM_FOV)}, {hex(F_M2)}",
        "je mf_ret",
        f"movss xmm0, DWORD PTR ds:{hex(G_CAM_FOV)}",
        "mf_clamp:",
        "maxss xmm0, DWORD PTR [ecx+44]",   # >= 0.5f
        "minss xmm0, DWORD PTR ds:0x586b14", # <= 90.0f
        "ret",

        # Hook at 0x47ba3b in GameCamera::Update (preserves ebx, esi, edi; xmm3 = 1.0f)
        "camFovVertigoHook:",
        "call checkInLevel",
        "je cfv_orig",
        f"cmp DWORD PTR ds:{hex(G_CAM_FOV)}, {hex(F_M1)}",  # pc (-1.0f)?
        "jne cfv_custom",
        f"cmp DWORD PTR ds:{hex(G_CAM_DIST)}, {hex(F_1)}",  # 1.0f?
        "jne cfv_pc_dist",
        "cfv_orig:",
        "mov edx, DWORD PTR [ebp-0xc]",
        "cmp BYTE PTR [edx+0x10], 0",
        "jmp 0x47ba42",

        "cfv_pc_dist:",
        "mov edx, DWORD PTR [ebp-0xc]",
        "cmp BYTE PTR [edx+0x10], 0",
        "je cfv_pc_novert",
        "push ecx",
        "mov ecx, DWORD PTR ds:0x5f9080",
        "movss xmm0, DWORD PTR [ebp-0x4]",
        "movss DWORD PTR [esp], xmm0",
        "call 0x481c50",                    # GetVertigo(fovy) -> st0 (pops 4B)
        "call checkInLevel",                # restores ecx = &c_tbl
        "jmp cfv_pc_mul",
        "cfv_pc_novert:",
        "fld1",
        "cfv_pc_mul:",
        "call pushDeff",                    # st0 = d_eff, st1 = vertigo
        "fmulp st(1), st(0)",
        "fstp DWORD PTR [ebp-0xc]",
        "jmp 0x47ba5f",

        "cfv_custom:",
        "xor eax, eax",
        "mov edx, DWORD PTR ds:0x5f9040",   # g_Level (real Level* singleton)
        "test edx, edx",
        "je cfv_sub",
        "cmp DWORD PTR [edx+0x78], 568",    # level568 ("curious land")
        "sete al",
        "cfv_sub:",
        "mov edx, DWORD PTR [ebp-0xc]",
        "movss xmm0, DWORD PTR [edx+4]",    # target_fov
        "call mapFov",
        "movaps xmm1, xmm0",
        "movss xmm0, DWORD PTR [edx]",      # start_fov
        "ucomiss xmm0, DWORD PTR [edx+4]",
        "je cfv_got_fov",
        "call mapFov",
        "movss xmm2, DWORD PTR [ebp-0x8]",
        "subss xmm2, DWORD PTR [edx]",
        "movss xmm4, DWORD PTR [edx+4]",
        "subss xmm4, DWORD PTR [edx]",
        "divss xmm2, xmm4",
        "subss xmm1, xmm0",
        "mulss xmm1, xmm2",
        "addss xmm0, xmm1",
        "call mf_clamp",
        "jmp cfv_store_fov",
        "cfv_got_fov:",
        "movaps xmm0, xmm1",
        "cfv_store_fov:",
        "movss DWORD PTR [ebp-0x4], xmm0",
        "fld DWORD PTR [ebp-0x4]",
        "fmul DWORD PTR [ecx+12]",          # * pi / 360
        "fptan",
        "fstp st(0)",                       # st0 = t_eff

        "call pushDeff",                    # st0 = d_eff, st1 = t_eff
        "fxch st(1)",                       # st0 = t_eff, st1 = d_eff
        "movss xmm2, DWORD PTR [ebp-0x10]", # aspect
        "mov al, BYTE PTR [edx+0x10]",      # vertigo flag

        f"cmp DWORD PTR ds:{hex(G_CAM_FOV)}, {hex(F_M2)}",
        "jne cfv_num_v",
        "ucomiss xmm2, xmm3",
        "jbe cfv_mob_tv",
        "fdiv DWORD PTR [ebp-0x10]",        # st0 = t_eff / aspect
        "fld st(0)",
        "fld1",
        "fpatan",
        "fmul DWORD PTR [ecx+16]",
        "fstp DWORD PTR [ebp-0x4]",
        "cfv_mob_tv:",
        "test al, al",
        "jne cfv_dolly",
        "ucomiss xmm0, xmm3",
        "jbe cfv_dolly",
        "fstp st(0)",
        "fld DWORD PTR [ecx+36]",          # tan(10 deg)
        "ucomiss xmm2, xmm3",
        "jbe cfv_dolly",
        "fdiv DWORD PTR [ebp-0x10]",
        "jmp cfv_dolly",

        "cfv_num_v:",
        "test al, al",
        "jne cfv_dolly",
        "ucomiss xmm0, xmm3",
        "jbe cfv_dolly",
        "fstp st(0)",
        "fstp DWORD PTR [ebp-0xc]",         # non-vertigo perspective: mul = d_eff
        "jmp 0x47ba5f",

        "cfv_dolly:",
        "fdivr DWORD PTR [ecx+40]",         # tan(11 deg) / t_denom
        "fmulp st(1), st(0)",
        "fstp DWORD PTR [ebp-0xc]",
        "jmp 0x47ba5f",
    ]

    if enable_menu_ini:
        block_g_lines += [
            ".globl iniCamHook, loadCamTtsav, drawRow7And8Main, pauseDrawDistAndFov",
            ".globl strSavDistKey, strSavDistIni, strSavFovKey, strSavFovIni",

            # matchIc(edi = target literal) checks std::string at ebx; sets al=1 iff equal
            "matchIc:",
            "mov eax, ebx",
            "cmp DWORD PTR [ebx+0x14], 0x10",
            "jb mi_go",
            "mov eax, DWORD PTR [ebx]",
            "mi_go:",
            "xor ecx, ecx",
            "mi_lp:",
            "mov dl, BYTE PTR [eax+ecx]",
            "or dl, 0x20",
            "cmp dl, BYTE PTR [edi+ecx]",
            "jne mi_no",
            "cmp BYTE PTR [edi+ecx], 0",
            "je mi_yes",
            "inc ecx",
            "jmp mi_lp",
            "mi_yes:",
            "mov al, 1",
            "ret",
            "mi_no:",
            "xor al, al",
            "ret",

            # parseFltClamped(xmm1=min, xmm2=max) -> sets al=1 and xmm0 in [min,max] if valid float
            "parseFltClamped:",
            "lea eax, [ebp-0x100]",
            "push eax",
            "push ebx",
            "call 0x4f4fd0",
            "add esp, 8",
            "fstp DWORD PTR [ebx+0x54]",
            "cmp BYTE PTR [ebp-0xf8], 0",
            "jne pfc_bad",
            "movss xmm0, DWORD PTR [ebx+0x54]",
            "ucomiss xmm0, xmm0",
            "jp pfc_bad",
            "maxss xmm0, xmm1",
            "minss xmm0, xmm2",
            "mov al, 1",
            "ret",
            "pfc_bad:",
            "xor al, al",
            "ret",

            # parseIniOne(esi = desc*): queries INI key for desc, stores gVal & gIni, tail-calls origIniFree
            "parseIniOne:",
            "mov ecx, DWORD PTR [esi]",
            "mov eax, DWORD PTR [esi+8]",
            "mov DWORD PTR [ecx+4], eax",
            f"cmp BYTE PTR ds:{hex(G_CAM_INITED)}, 0",
            "jne pio_query",
            "mov DWORD PTR [ecx+8], eax",
            "mov eax, DWORD PTR [esi+4]",
            "mov DWORD PTR [ecx], eax",
            "pio_query:",
            "mov eax, DWORD PTR [esi+28]",
            "call 0x484c7b",
            "cmp DWORD PTR [ebx+0x10], 0",
            "je pio_done",
            "mov edi, DWORD PTR [esi+32]",
            "call matchIc",
            "test al, al",
            "je pio_kw2",
            "mov eax, DWORD PTR [esi+12]",
            "jmp pio_set",
            "pio_kw2:",
            "mov edi, DWORD PTR [esi+36]",
            "call matchIc",
            "test al, al",
            "je pio_flt",
            "mov eax, DWORD PTR [esi+16]",
            "jmp pio_set",
            "pio_flt:",
            "movss xmm1, DWORD PTR [esi+20]",
            "movss xmm2, DWORD PTR [esi+24]",
            "call parseFltClamped",
            "test al, al",
            "je pio_done",
            "push eax",
            "movss DWORD PTR [esp], xmm0",
            "pop eax",
            "pio_set:",
            "mov ecx, DWORD PTR [esi]",
            "mov DWORD PTR [ecx], eax",
            "mov DWORD PTR [ecx+4], eax",
            "pio_done:",
            "jmp origIniFree",

            # Chained from Block D 0x484d59 (ebx = INI pair struct, ebp = readIniSettings frame)
            "iniCamHook:",
            "call origIniFree",
            "push edi",
            "push esi",
            "mov esi, OFFSET distDesc",
            "call parseIniOne",
            "mov esi, OFFSET fovDesc",
            "call parseIniOne",
            f"mov BYTE PTR ds:{hex(G_CAM_INITED)}, 1",
            "pop esi",
            "pop edi",
            "ret",

            # validCamFloat(eax = desc*, xmm0 = val) -> al=1 iff valid (sentinel1, sentinel2, or in [min, max])
            "validCamFloat:",
            "ucomiss xmm0, xmm0",
            "jp vcf_no",
            "ucomiss xmm0, DWORD PTR [eax+12]",
            "je vcf_yes",
            "ucomiss xmm0, DWORD PTR [eax+16]",
            "je vcf_yes",
            "ucomiss xmm0, DWORD PTR [eax+20]",
            "jb vcf_no",
            "ucomiss xmm0, DWORD PTR [eax+24]",
            "ja vcf_no",
            "vcf_yes:",
            "mov al, 1",
            "ret",
            "vcf_no:",
            "xor al, al",
            "ret",

            # tryReadCam(eax = key*, edx = dst*, edi = desc*): loads & validates ttsav float into [edx]
            "tryReadCam:",
            "push edx",
            "call 0x539770",
            "pop edx",
            "test al, al",
            "je trc_ret",
            "sub esp, 4",
            "fstp DWORD PTR [esp]",
            "movss xmm0, DWORD PTR [esp]",
            "add esp, 4",
            "mov eax, edi",
            "call validCamFloat",
            "test al, al",
            "je trc_ret",
            "movss DWORD PTR [edx], xmm0",
            "trc_ret:",
            "ret",

            # loadOneCam(edi = desc*): loads saved key + saved ini key and reconciles INI override
            "loadOneCam:",
            "push ebx",
            "mov ebx, DWORD PTR [edi]",
            "mov eax, DWORD PTR [edi+4]",
            "mov DWORD PTR [ebx], eax",
            "mov eax, DWORD PTR [edi+40]",
            "mov edx, ebx",
            "call tryReadCam",
            "mov eax, DWORD PTR [edi+8]",
            "mov DWORD PTR [ebx+8], eax",
            "mov eax, DWORD PTR [edi+44]",
            "lea edx, [ebx+8]",
            "call tryReadCam",
            "movss xmm0, DWORD PTR [ebx+4]",
            "ucomiss xmm0, DWORD PTR [edi+8]",
            "je loc_unset",
            "movaps xmm1, xmm0",
            "subss xmm1, DWORD PTR [ebx+8]",
            "pcmpeqd xmm2, xmm2",
            "psrld xmm2, 1",
            "andps xmm1, xmm2",
            f"ucomiss xmm1, DWORD PTR ds:{hex(FLT_TEPS_VA)}",
            "jbe loc_done",
            "movss DWORD PTR [ebx+8], xmm0",
            "movss DWORD PTR [ebx], xmm0",
            "jmp loc_done",
            "loc_unset:",
            "mov eax, DWORD PTR [edi+8]",
            "mov DWORD PTR [ebx+8], eax",
            "loc_done:",
            "pop ebx",
            "ret",

            "loadCamTtsav:",
            "call origLoadTtsav",
            "push edi",
            "mov edi, OFFSET distDesc",
            "call loadOneCam",
            "mov edi, OFFSET fovDesc",
            "call loadOneCam",
            "pop edi",
            "ret",

            # fmtDistWstr / fmtFovWstr(ecx = temp_wstr*) -> eax = wstring*
            "fmtDistWstr:",
            f"cmp DWORD PTR ds:{hex(G_CAM_DIST)}, 0",
            "jge fdw_num",
            f"mov eax, {hex(WSTR_MOBILE_VAL)}",
            "ret",
            "fdw_num:",
            f"fld DWORD PTR ds:{hex(G_CAM_DIST)}",
            f"jmp {hex(FORMAT_FLOAT_WSTR_VA)}",

            "fmtFovWstr:",
            f"mov eax, DWORD PTR ds:{hex(G_CAM_FOV)}",
            f"cmp eax, {hex(F_M1)}",
            "jne ffw_chk_m",
            f"mov eax, {hex(WSTR_PC_VAL)}",
            "ret",
            "ffw_chk_m:",
            f"cmp eax, {hex(F_M2)}",
            "jne ffw_num",
            f"mov eax, {hex(WSTR_MOBILE_VAL)}",
            "ret",
            "ffw_num:",
            f"fld DWORD PTR ds:{hex(G_CAM_FOV)}",
            f"jmp {hex(FORMAT_FLOAT_WSTR_VA)}",

            # drawMainCamRow(eax = rowIdx, esi = label*, ebx = y, edx = fmtFn*); preserves edi=0, ebp
            "drawMainCamRow:",
            "push edx",
            "call 0x53994b",
            "test al, al",
            "je dmcr_ret",
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
            "dmcr_ret:",
            "pop edx",
            "ret",

            # Main Menu Options rows 7 (cam distance, y=0x118) & 8 (dolly zoom, y=0x138)
            "drawRow7And8Main:",
            "call origDrawAuto",
            "push ebx",
            "push esi",
            "mov eax, 7",
            "mov esi, OFFSET wstrDistLbl",
            f"mov ebx, {hex(options_row_text_y(7, True, include_time_row, include_skip_row))}",
            "mov edx, OFFSET fmtDistWstr",
            "call drawMainCamRow",
            "mov eax, 8",
            f"mov esi, {hex(WSTR_FOV_LBL)}",
            f"mov ebx, {hex(options_row_text_y(8, True, include_time_row, include_skip_row))}",
            "mov edx, OFFSET fmtFovWstr",
            "call drawMainCamRow",
            "pop esi",
            "pop ebx",
            "ret",

            # pauseDrawOneCam(eax = label*, ecx = y, edx = divY, edi = fmtFn*); preserves ebx=0, esi
            "pauseDrawOneCam:",
            "push eax",
            "push ecx",
            f"call {hex(PAUSE_DRAW_DIV_VA)}",
            "pop ecx",
            "pop eax",
            "push ecx",
            "mov edx, 0x158",
            f"call {hex(PAUSE_DRAW_TEXT_VA)}",
            "sub esp, 0x18",
            "mov ecx, esp",
            "call edi",
            "mov ecx, DWORD PTR [esp+0x18]",
            "mov edx, 0x23d",
            f"call {hex(PAUSE_DRAW_TEXT_VA)}",
            "add esp, 0x1c",
            "ret",

            # Pause Menu Page 1 rows 7 (y=0x173) & 8 (y=0x194)
            "pauseDrawDistAndFov:",
            "push edi",
            "mov eax, OFFSET wstrDistLbl",
            "mov ecx, 0x173",
            "mov edx, 0x15c",
            "mov edi, OFFSET fmtDistWstr",
            "call pauseDrawOneCam",
            f"mov eax, {hex(WSTR_FOV_LBL)}",
            "mov ecx, 0x194",
            "mov edx, 0x17d",
            "mov edi, OFFSET fmtFovWstr",
            "call pauseDrawOneCam",
            "pop edi",
            "jmp origPauseBack",

            # Unified descriptors (48B each: gVal, defFlt, unsetFlt, s1, s2, min, max, iniKey, kw1, kw2, savKey, savIniKey)
            "distDesc:",
            f".long {hex(G_CAM_DIST)}, {hex(F_1)}, {hex(F_0)}, {hex(F_M1)}, {hex(F_1)}, {hex(F_P1)}, {hex(F_10)}, strIniDistKey, {hex(STR_MOBILE_LC)}, {hex(STR_PC_LC)}, strSavDistKey, strSavDistIni",
            "fovDesc:",
            f".long {hex(G_CAM_FOV)}, {hex(F_M1)}, {hex(F_M99)}, {hex(F_M1)}, {hex(F_M2)}, {hex(F_0)}, {hex(F_90)}, strIniFovKey, {hex(STR_PC_LC)}, {hex(STR_MOBILE_LC)}, strSavFovKey, strSavFovIni",
            "wstrDistBuf:",
            ".short 99,97,109,32,100,105,115,116,97,110,99,101,0",
            "wstrDistLbl:",
            ".long wstrDistBuf, 0, 0, 0, 12, 12",
            'strIniDistKey: .asciz "camera_distance"',
            'strIniFovKey:  .asciz "camera_fov"',
            'strSavDistKey: .asciz "camDist"',
            'strSavDistIni: .asciz "camDistIni"',
            'strSavFovKey:  .asciz "camFov"',
            'strSavFovIni:  .asciz "camFovIni"',
        ]

    put_bytes(buf, BLOCK_G_START, b"\xcc" * (BLOCK_G_END - BLOCK_G_START))
    block_g, syms_g = link_block_with_symbols(
        BLOCK_G_START, block_g_lines, BLOCK_G_END - BLOCK_G_START, ext_syms=g_ext
    )
    put_bytes(buf, BLOCK_G_START, block_g, BLOCK_G_END - BLOCK_G_START)

    # Install GameCamera::Update FOV/distance hook at 0x47ba3b
    expect_replace(
        buf,
        0x47ba3b,
        b"\x8b\x55\xf4\x80\x7a\x10\x00",
        assemble_at(0x47ba3b, [f"jmp {hex(syms_g['camFovVertigoHook'])}", "nop", "nop"], 7),
    )

    allocs["block_g"] = (len(block_g), BLOCK_G_END - BLOCK_G_START)

    # 4. Assemble Block H (depth-precision-preserving near/far shift for telephoto FOVs)
    block_h_lines = [
        ".globl saveActiveMul, adjustNearFarHook",

        # Hook at 0x47ba5f (replaces mov eax,[0x5f9080]; fld [eax+0x34] = 8B): record final [ebp-0xc] mul
        "saveActiveMul:",
        "mov eax, DWORD PTR [ebp-0xc]",
        f"mov DWORD PTR ds:{hex(G_ACTIVE_MUL)}, eax",
        "mov eax, DWORD PTR ds:0x5f9080",
        "fld DWORD PTR [eax+0x34]",
        "jmp 0x47ba67",

        # Hook at 0x47bbc3 (replaces call 0x423840; esi = Camera*, [esp+4] = near, [esp+8] = far)
        # Keep near near (0.80 * d_eye - d_base) and shift far cleanly so D24S8 resolves <= 0.0013 units
        # (well below ShadowVolume's 0.030 world-unit inset, preventing shadow Z-fighting).
        "adjustNearFarHook:",
        f"movss xmm3, DWORD PTR ds:{hex(G_ACTIVE_MUL)}",
        "ucomiss xmm3, DWORD PTR ds:0x577c74",
        "jbe anf_orig",
        "call checkInLevel",
        "je anf_orig",
        "fldz",
        "push 8",
        "pop edx",
        "anf_lp:",
        "fld DWORD PTR [esi+edx+0x108]",
        "fsub DWORD PTR [esi+edx+0x208]",
        "fmul st(0), st(0)",
        "faddp st(1), st(0)",
        "sub edx, 4",
        "jns anf_lp",
        "fsqrt",
        "sub esp, 4",
        "fstp DWORD PTR [esp]",
        "movss xmm0, DWORD PTR [esp]",
        "movaps xmm1, xmm0",
        "divss xmm1, xmm3",
        "movaps xmm2, xmm0",
        "mulss xmm2, DWORD PTR [ecx+56]",
        "subss xmm2, xmm1",
        "maxss xmm2, DWORD PTR [esp+8]",
        "movss DWORD PTR [esp+8], xmm2",
        "movss xmm2, DWORD PTR [esp+12]",
        "divss xmm2, xmm3",
        "addss xmm2, xmm0",
        "movss DWORD PTR [esp+12], xmm2",
        "add esp, 4",
        "anf_orig:",
        "mov ecx, esi",
        "jmp 0x423840",
    ]
    put_bytes(buf, BLOCK_H_START, b"\xcc" * (BLOCK_H_END - BLOCK_H_START))
    block_h, syms_h = link_block_with_symbols(
        BLOCK_H_START,
        block_h_lines,
        BLOCK_H_END - BLOCK_H_START,
        ext_syms={"checkInLevel": syms_f["checkInLevel"]},
    )
    put_bytes(buf, BLOCK_H_START, block_h, BLOCK_H_END - BLOCK_H_START)

    expect_replace(
        buf,
        0x47ba5f,
        b"\xa1\x80\x90\x5f\x00\xd9\x40\x34",
        assemble_at(0x47ba5f, [f"jmp {hex(syms_h['saveActiveMul'])}", "nop", "nop", "nop"], 8),
    )
    expect_replace(
        buf,
        0x47bbc3,
        assemble_at(0x47bbc3, ["call 0x423840"], 5),
        assemble_at(0x47bbc3, [f"call {hex(syms_h['adjustNearFarHook'])}"], 5),
    )

    allocs["block_h"] = (len(block_h), BLOCK_H_END - BLOCK_H_START)

    # 4b. Assemble Block H2 (skybox projection & frustum override so near-plane/FOV shifts never clip skybox_1..4)
    block_h2_lines = [
        ".globl skyboxProjWrap",

        # Wrapper at 0x407805 (replaces call 0x4075f0 inside Renderer::begin3D; ecx = Renderer*)
        "skyboxProjWrap:",
        "cmp DWORD PTR [ecx+0x58], 0",
        "je 0x4075f0",
        "push esi",
        "mov esi, ecx",
        "call checkInLevel",
        "je sb_orig",
        "mov edx, DWORD PTR [esi+0x48]",
        "test edx, edx",
        "je sb_orig",
        "cmp DWORD PTR [edx+0x2b0], 1",
        "jne sb_orig",
        "movss xmm0, DWORD PTR [edx+0x1c0]",
        "ucomiss xmm0, DWORD PTR [ecx+60]",         # fovy < 22 deg?
        "jb sb_fix",
        "cmp DWORD PTR [edx+0x1c4], 0x43fa0000",    # near > 500.0f?
        "ja sb_fix",
        "cmp DWORD PTR [edx+0x1c8], 0x44fa0000",    # far < 2000.0f?
        "jae sb_orig",
        "sb_fix:",
        "maxss xmm0, DWORD PTR [ecx+60]",           # fovy_sky = max(cam->fovy, 22 deg)
        "sub esp, 0x50",                            # [esp..+0x3f] = M_sky (64B), [+0x40..+0x4f] = saved Frustum (16B)
        "movups xmm1, XMMWORD PTR [edx+0x30c]",
        "movups XMMWORD PTR [esp+0x40], xmm1",
        "mov ecx, esp",
        "push 0x45fa0000",                          # far = 8000.0f
        "push 0x42c80000",                          # near = 100.0f
        "push DWORD PTR [edx+0x1dc]",               # aspect
        "push eax",
        "movss DWORD PTR [esp], xmm0",              # fovy_sky
        "push ecx",                                 # pOut = &M_sky
        "call 0x4bb2d0",                            # D3DXMatrixPerspectiveFovRH (pops 20B)
        "mov ecx, esp",
        "push 0",
        "call 0x435780",                            # SetTransform(D3DTS_PROJECTION=3, &M_sky) (pops 4B)
        "mov edx, DWORD PTR [esi+0x48]",
        "mov DWORD PTR [edx+0x30c], 0x3f800000",    # Frustum.tan = 1.0f
        "mov DWORD PTR [edx+0x310], 0x45fa0000",    # Frustum.far = 8000.0f
        "mov DWORD PTR [edx+0x314], 0x42c80000",    # Frustum.near = 100.0f
        "mov ecx, esi",
        "call 0x4075f0",
        "mov edx, DWORD PTR [esi+0x48]",
        "movups xmm1, XMMWORD PTR [esp+0x40]",
        "movups XMMWORD PTR [edx+0x30c], xmm1",
        "add esp, 0x50",
        "mov ecx, DWORD PTR [edx+0x304]",
        "push 0",
        "call 0x435780",                            # Restore D3DTS_PROJECTION to cam->projMat
        "pop esi",
        "ret",
        "sb_orig:",
        "mov ecx, esi",
        "pop esi",
        "jmp 0x4075f0",
    ]
    put_bytes(buf, BLOCK_H2_START, b"\xcc" * (BLOCK_H2_END - BLOCK_H2_START))
    block_h2, syms_h2 = link_block_with_symbols(
        BLOCK_H2_START,
        block_h2_lines,
        BLOCK_H2_END - BLOCK_H2_START,
        ext_syms={"checkInLevel": syms_f["checkInLevel"]},
    )
    put_bytes(buf, BLOCK_H2_START, block_h2, BLOCK_H2_END - BLOCK_H2_START)

    expect_replace(
        buf,
        0x407805,
        assemble_at(0x407805, ["call 0x4075f0"], 5),
        assemble_at(0x407805, [f"call {hex(syms_h2['skyboxProjWrap'])}"], 5),
    )

    allocs["block_h2"] = (len(block_h2), BLOCK_H2_END - BLOCK_H2_START)

    if enable_menu_ini:
        put_u32(buf, JTBL_ENTRY_7, syms_f["camDistAction"])
        put_u32(buf, JTBL_ENTRY_8, syms_f["camFovAction"])

        save_write_cam = assemble_at(SLOT_SAVE_WRITE, [
            f"call {hex(orig_save_ttsav)}",
            f"fld DWORD PTR ds:{hex(G_CAM_DIST)}",
            f"mov eax, {hex(syms_g['strSavDistKey'])}",
            "call 0x5398dc",
            f"fld DWORD PTR ds:{hex(G_LAST_INI_DIST)}",
            f"mov eax, {hex(syms_g['strSavDistIni'])}",
            "call 0x5398dc",
            f"fld DWORD PTR ds:{hex(G_CAM_FOV)}",
            f"mov eax, {hex(syms_g['strSavFovKey'])}",
            "call 0x5398dc",
            f"fld DWORD PTR ds:{hex(G_LAST_INI_FOV)}",
            f"mov eax, {hex(syms_g['strSavFovIni'])}",
            "jmp 0x5398dc",
        ], 83)
        expect_replace(buf, SLOT_SAVE_WRITE, b"\xcc" * len(save_write_cam), save_write_cam)

        expect_replace(
            buf,
            0x484d59,
            assemble_at(0x484d59, [f"call {hex(orig_ini_free)}"], 5),
            assemble_at(0x484d59, [f"call {hex(syms_g['iniCamHook'])}"], 5),
        )
        expect_replace(
            buf,
            0x408060,
            assemble_at(0x408060, [f"call {hex(orig_load_ttsav)}"], 5),
            assemble_at(0x408060, [f"call {hex(syms_g['loadCamTtsav'])}"], 5),
        )
        expect_replace(
            buf,
            0x4080c8,
            assemble_at(0x4080c8, [f"jmp {hex(orig_save_ttsav)}"], 5),
            assemble_at(0x4080c8, [f"jmp {hex(SLOT_SAVE_WRITE)}"], 5),
        )
        expect_replace(
            buf,
            MAIN_DRAW_AUTO_HOOK_VA,
            assemble_at(MAIN_DRAW_AUTO_HOOK_VA, [f"call {hex(orig_draw_auto)}"], 5),
            assemble_at(MAIN_DRAW_AUTO_HOOK_VA, [f"call {hex(syms_g['drawRow7And8Main'])}"], 5),
        )
        expect_replace(
            buf,
            PAUSE_CAM_DRAW_HOOK_VA,
            assemble_at(PAUSE_CAM_DRAW_HOOK_VA, [f"call {hex(orig_pause_back)}"], 5),
            assemble_at(PAUSE_CAM_DRAW_HOOK_VA, [f"call {hex(syms_g['pauseDrawDistAndFov'])}"], 5),
        )

        allocs["save_write_cam"] = (len(save_write_cam), 83)

    return allocs
