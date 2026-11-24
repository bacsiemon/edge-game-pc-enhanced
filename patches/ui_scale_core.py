import struct
from .common import put_bytes, put_u32, put_rel32, assemble_at


def build_ui_scale_tables(buf: bytearray) -> None:
    """Populate .data/.bss defaults and .rdata keys, lowercase UTF-16LE labels, and UI click presets."""
    put_u32(buf, 0x5f8730, 1)  # gAutoScale = 1 (on)
    put_bytes(buf, 0x5f8798, b"\x00" * 12 + b"\xff\xff")  # floats=0, iniAuto/lastIniAuto=-1

    put_bytes(buf, 0x5e2840, b"ui_scale\x00", 9)
    put_bytes(buf, 0x5e2849, b"ui_auto_scale\x00", 14)
    put_bytes(buf, 0x5e2857, b"uiScaleF\x00", 9)
    put_bytes(buf, 0x5e2860, b"uiScaleFIni\x00", 12)
    put_bytes(buf, 0x5e286c, b"uiAuto\x00", 7)
    put_bytes(buf, 0x5e2873, b"uiAutoIni\x00", 10)

    ui_lbl = "ui scale\x00".encode("utf-16le")
    put_bytes(buf, 0x5e2880, ui_lbl, 18)
    put_bytes(buf, 0x5e2894, struct.pack("<I", 0x5e2880) + bytes(12) + struct.pack("<II", 8, 8), 24)

    auto_lbl = "auto scale\x00".encode("utf-16le")
    put_bytes(buf, 0x5e28ac, auto_lbl, 22)
    put_bytes(buf, 0x5e28c4, struct.pack("<I", 0x5e28ac) + bytes(12) + struct.pack("<II", 10, 10), 24)

    presets = [0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 3.0, 4.0]
    for i, val in enumerate(presets):
        put_bytes(buf, 0x5e28e0 + i * 4, struct.pack("<f", val), 4)


def apply_ui_scale_core(buf: bytearray) -> None:
    """
    Separate physical resolution (3D render targets) from virtual UI coordinates:
    - Keeps 3D render targets & 3D particles at physical resolution (0x407e90/0x407ea0).
    - Redirects virtual 2D width/height, scissor, viewport, camera, and mouse projection to virtual resolution.
    - Installs applyUiScale (0x408320) and hooks UserSettings::load/save call sites.
    """
    # Force mode-0 renderer path cleanly to 0x408382
    put_rel32(buf, 0x407e81, 0x408382, 0xe9)
    put_bytes(buf, 0x407e86, b"\x90")
    put_bytes(buf, 0x407e87, b"\xcc" * (0x408382 - 0x407e87))

    # 0x407e90: getPhysWidth, 0x407ea0: getPhysHeight
    put_bytes(buf, 0x407e90, assemble_at(0x407e90, ["mov eax, DWORD PTR [ecx+0xb8]", "ret"]), 16)
    put_bytes(buf, 0x407ea0, assemble_at(0x407ea0, ["mov eax, DWORD PTR [ecx+0xbc]", "ret"]), 16)

    for va in [0x401aab, 0x404787, 0x40c620, 0x40c830]:
        put_rel32(buf, va, 0x407e90, 0xe8)
    for va in [0x401abd, 0x404799, 0x40c62e, 0x40c828, 0x4d0b9d]:
        put_rel32(buf, va, 0x407ea0, 0xe8)

    # Clear 0x407eb0..0x407f00 (computeVirtHeight populated by ui_scale_menu_ini)
    put_bytes(buf, 0x407eb0, b"\xcc" * 80)

    # 0x408238: computeVirtWidth (preserves ebx, ecx, esi, edi, ebp)
    put_bytes(buf, 0x408238, assemble_at(0x408238, [
        "mov eax, DWORD PTR [ecx+0xbc]",
        "test eax, eax",
        "jle w_phys",
        "push ecx",
        "call 0x405b60",
        "pop ecx",
        "mul DWORD PTR [ecx+0xb8]",
        "div DWORD PTR [ecx+0xbc]",
        "ret",
        "w_phys:",
        "mov eax, DWORD PTR [ecx+0xb8]",
        "ret",
    ]), 40)

    put_rel32(buf, 0x405b30, 0x408238, 0xe9)
    put_rel32(buf, 0x405b60, 0x407eb0, 0xe9)

    # Virtual camera/viewport setup sites
    put_bytes(buf, 0x40703a, assemble_at(0x40703a, [
        "xor ebx, ebx",
        "mov ecx, esi",
        "call 0x405b30",
        "mov DWORD PTR [ebp-0x10], eax",
        "fild DWORD PTR [ebp-0x10]",
        "fstp DWORD PTR [ebp-0x14]",
        "mov ecx, esi",
        "call 0x405b60",
        "mov DWORD PTR [ebp-0x10], eax",
        "jmp 0x407099",
    ]), 95)

    put_bytes(buf, 0x406404, assemble_at(0x406404, [
        "mov ecx, esi",
        "call 0x405b60",
        "mov DWORD PTR [ebp-0xc], eax",
        "mov ecx, esi",
        "call 0x405b30",
        "mov DWORD PTR [ebp-0x8], eax",
        "jmp 0x40644d",
    ]), 73)

    put_bytes(buf, 0x40839d, assemble_at(0x40839d, [
        "mov ecx, esi",
        "call 0x405b60",
        "mov DWORD PTR [ebp+0x8], eax",
        "mov ecx, esi",
        "call 0x405b30",
        "mov DWORD PTR [ebp-0x14], eax",
        "jmp 0x4083f6",
    ]), 89)

    # Scissor rect scaling helper (overwritten in part by ui_rt_sharpening to use getUiDrawSize)
    put_bytes(buf, 0x4060e6, assemble_at(0x4060e6, [
        "push esi",
        "push edi",
        "mov esi, ecx",
        "jmp 0x407f20",
    ]), 37)
    put_bytes(buf, 0x407f20, assemble_at(0x407f20, [
        "push ebx",
        "mov ecx, esi",
        "call 0x405b30",
        "test eax, eax",
        "jg s_wok",
        "mov eax, 1",
        "s_wok:",
        "mov edi, eax",
        "mov ecx, esi",
        "call 0x405b60",
        "test eax, eax",
        "jg s_hok",
        "mov eax, 1",
        "s_hok:",
        "mov ebx, eax",
        "mov ecx, DWORD PTR [ebp+0x8]",
        "mov eax, DWORD PTR [ecx]",
        "imul DWORD PTR [esi+0xb8]",
        "cdq",
        "idiv edi",
        "mov DWORD PTR [ebp-0x14], eax",
        "mov eax, DWORD PTR [ecx]",
        "add eax, DWORD PTR [ecx+0x8]",
        "imul DWORD PTR [esi+0xb8]",
        "cdq",
        "idiv edi",
        "mov DWORD PTR [ebp-0xc], eax",
        "mov eax, DWORD PTR [ecx+0x4]",
        "imul DWORD PTR [esi+0xbc]",
        "cdq",
        "idiv ebx",
        "mov DWORD PTR [ebp-0x10], eax",
        "mov eax, DWORD PTR [ecx+0x4]",
        "add eax, DWORD PTR [ecx+0xc]",
        "imul DWORD PTR [esi+0xbc]",
        "cdq",
        "idiv ebx",
        "mov DWORD PTR [ebp-0x8], eax",
        "pop ebx",
        "mov eax, DWORD PTR [esi+0xcc]",
        "lea edx, [ebp-0x14]",
        "jmp 0x40610b",
    ]), 128)

    # Viewport scaling helper
    put_bytes(buf, 0x405bcd, assemble_at(0x405bcd, [
        "mov ecx, DWORD PTR [ebp+0x10]",
        "jmp 0x407fa0",
    ]), 20)
    put_bytes(buf, 0x407fa0, assemble_at(0x407fa0, [
        "push ebx",
        "push eax",
        "push ecx",
        "push edx",
        "mov ecx, edi",
        "call 0x405b30",
        "test eax, eax",
        "jg v_wok",
        "mov eax, 1",
        "v_wok:",
        "mov ebx, eax",
        "mov eax, esi",
        "imul DWORD PTR [edi+0xb8]",
        "cdq",
        "idiv ebx",
        "mov esi, eax",
        "mov eax, DWORD PTR [esp+0x4]",
        "imul DWORD PTR [edi+0xb8]",
        "cdq",
        "idiv ebx",
        "mov DWORD PTR [esp+0x4], eax",
        "mov ecx, edi",
        "call 0x405b60",
        "test eax, eax",
        "jg v_hok",
        "mov eax, 1",
        "v_hok:",
        "mov ebx, eax",
        "mov eax, DWORD PTR [esp]",
        "imul DWORD PTR [edi+0xbc]",
        "cdq",
        "idiv ebx",
        "mov DWORD PTR [esp], eax",
        "mov eax, DWORD PTR [esp+0x8]",
        "imul DWORD PTR [edi+0xbc]",
        "cdq",
        "idiv ebx",
        "pop edx",
        "pop ecx",
        "add esp, 0x4",
        "pop ebx",
        "jmp 0x405be1",
    ]), 112)

    # Viewport::applyToCamera helper
    put_bytes(buf, 0x4089db, assemble_at(0x4089db, ["jmp 0x408260"]), 52)
    put_bytes(buf, 0x408260, assemble_at(0x408260, [
        "mov eax, DWORD PTR ds:0x5f8770",
        "test eax, eax",
        "je cam_phys",
        "mov ecx, eax",
        "call 0x405b30",
        "mov DWORD PTR [ebp-0x4], eax",
        "fild DWORD PTR [ebp-0x4]",
        "fstp DWORD PTR [ebp-0x4]",
        "mov ecx, DWORD PTR ds:0x5f8770",
        "call 0x405b60",
        "mov DWORD PTR [ebp+0x8], eax",
        "fild DWORD PTR [ebp+0x8]",
        "fstp DWORD PTR [ebp+0x8]",
        "mov ecx, DWORD PTR [esi+0x18]",
        "jmp 0x408a0f",
        "cam_phys:",
        "fild DWORD PTR [esi+0x8]",
        "fstp DWORD PTR [ebp-0x4]",
        "fild DWORD PTR [esi+0xc]",
        "fstp DWORD PTR [ebp+0x8]",
        "jmp 0x408a0f",
    ]), 80)

    # MouseController scaling helper
    put_bytes(buf, 0x44c7e6, assemble_at(0x44c7e6, ["jmp 0x4082b0"]), 42)
    put_bytes(buf, 0x4082b0, assemble_at(0x4082b0, [
        "cmp DWORD PTR [esi+0xb8], 0",
        "jle m_fallback",
        "cmp DWORD PTR [esi+0xbc], 0",
        "jle m_fallback",
        "push DWORD PTR [eax]",
        "push DWORD PTR [eax+0x4]",
        "mov ecx, esi",
        "call 0x405b30",
        "mov DWORD PTR [ebp-0x8], eax",
        "fild DWORD PTR [ebp-0x8]",
        "fmul DWORD PTR [esp+0x4]",
        "fidiv DWORD PTR [esi+0xb8]",
        "fstp DWORD PTR [ebp-0x8]",
        "mov ecx, esi",
        "call 0x405b60",
        "mov DWORD PTR [ebp-0x4], eax",
        "fild DWORD PTR [ebp-0x4]",
        "fmul DWORD PTR [esp]",
        "fidiv DWORD PTR [esi+0xbc]",
        "fstp DWORD PTR [ebp-0x4]",
        "add esp, 0x8",
        "jmp 0x44c810",
        "m_fallback:",
        "fld DWORD PTR [eax]",
        "fstp DWORD PTR [ebp-0x8]",
        "fld DWORD PTR [eax+0x4]",
        "fstp DWORD PTR [ebp-0x4]",
        "jmp 0x44c810",
    ]), 112)

    # 0x408320: shared applyUiScale(ecx = ScreenMgr*) (preserves ebx, esi, edi)
    put_bytes(buf, 0x408320, assemble_at(0x408320, [
        "push esi",
        "mov esi, ecx",
        "mov ecx, DWORD PTR ds:0x5f8770",
        "test ecx, ecx",
        "je app_done",
        "call 0x407010",
        "mov ecx, DWORD PTR ds:0x5f8770",
        "call 0x406190",
        "test esi, esi",
        "je app_hud",
        "mov ecx, DWORD PTR ds:0x5f8770",
        "call 0x405b30",
        "mov DWORD PTR [esi+0x24], eax",
        "mov DWORD PTR [esi+0x2c], eax",
        "mov ecx, DWORD PTR ds:0x5f8770",
        "call 0x405b60",
        "mov DWORD PTR [esi+0x28], eax",
        "mov DWORD PTR [esi+0x30], eax",
        "app_hud:",
        "mov ecx, DWORD PTR ds:0x5f9064",
        "test ecx, ecx",
        "je app_done",
        "mov edx, DWORD PTR [ecx]",
        "call DWORD PTR [edx+0x24]",
        "app_done:",
        "pop esi",
        "ret",
    ]), 96)

    # Hook UserSettings::load & save call sites (bodies populated by ui_scale_menu_ini)
    put_bytes(buf, 0x492558, assemble_at(0x492558, ["call 0x408060", "nop", "nop"]), 7)
    put_bytes(buf, 0x408060, b"\xcc" * 96)
    put_bytes(buf, 0x4926e7, assemble_at(0x4926e7, ["call 0x4080c0", "nop", "nop", "nop"]), 8)
    put_bytes(buf, 0x4080c0, b"\xcc" * 96)
