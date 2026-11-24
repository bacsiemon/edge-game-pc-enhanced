"""Customizable 0-100 (step 10) music and SFX volume sliders/selectors replacing on/off."""

import struct

from .common import assemble_at, expect_replace, link_block_with_symbols, put_bytes, va2off

MAIN_ROW0_VAL_START  = 0x4b2fdf  # OptionsMenu::render row 0 (sounds) value block
MAIN_ROW0_VAL_END    = 0x4b307c  # 157 B
MAIN_ROW1_VAL_START  = 0x4b3168  # OptionsMenu::render row 1 (music) value block
MAIN_ROW1_VAL_END    = 0x4b31fc  # 148 B
PAUSE_ROW0_VAL_START = 0x4ad756  # PauseMenu::render row 0 (sounds) value block
PAUSE_ROW0_VAL_END   = 0x4ad80d  # 183 B
PAUSE_ROW1_VAL_START = 0x4ad8e7  # PauseMenu::render row 1 (music) value block
PAUSE_ROW1_VAL_END   = 0x4ad99e  # 183 B
AUDIO_TOGGLE_START   = 0x494610  # toggleSfx + toggleMusic functions
AUDIO_TOGGLE_END     = 0x4946c0  # 176 B


def apply_audio_volume(buf: bytearray) -> dict[str, tuple[int, int]]:
    """
    Replace boolean SFX/music toggles in Main Options and Pause Page 1 with 0..100
    (step 10) numeric volume controls, fix XACT SFX category lookup ("Default" vs
    missing "Effects"), scale OggStream music volume by musicVolume/100, and apply
    both saved volumes at startup.
    """
    # 0. Guard expected original bytes at all hook and replacement sites
    main_row0_y = buf[va2off(0x4b2fb1)]
    main_row1_y = struct.unpack_from("<I", buf, va2off(0x4b312f))[0]

    expect_replace(
        buf,
        0x585474,
        b"Effects\x00",
        b"Default\x00",
    )

    orig_main0_head = assemble_at(MAIN_ROW0_VAL_START, [
        "mov edx, DWORD PTR ds:0x5f9080",
        "cmp DWORD PTR [edx+0x2b0], edi",
    ])
    orig_main0_tail = assemble_at(0x4b3072, ["call 0x4ba62a", "add esp, 4"])
    assert buf[va2off(MAIN_ROW0_VAL_START) : va2off(MAIN_ROW0_VAL_START) + len(orig_main0_head)] == orig_main0_head
    assert buf[va2off(0x4b3072) : va2off(0x4b307a)] == orig_main0_tail

    orig_main1_head = assemble_at(MAIN_ROW1_VAL_START, [
        "mov edx, DWORD PTR ds:0x5f9080",
        "cmp DWORD PTR [edx+0x2ac], edi",
    ])
    orig_main1_tail = assemble_at(0x4b31f4, ["call 0x4ba62a", "add esp, 4"])
    assert buf[va2off(MAIN_ROW1_VAL_START) : va2off(MAIN_ROW1_VAL_START) + len(orig_main1_head)] == orig_main1_head
    assert buf[va2off(0x4b31f4) : va2off(MAIN_ROW1_VAL_END)] == orig_main1_tail

    orig_pause0_head = assemble_at(PAUSE_ROW0_VAL_START, ["mov ecx, DWORD PTR ds:0x5f9080"])
    orig_pause0_cmp  = assemble_at(0x4ad75e, ["cmp DWORD PTR [ecx+0x2b0], ebx"])
    orig_pause0_tail = assemble_at(0x4ad805, ["call 0x4ba62a", "add esp, 4"])
    assert buf[va2off(PAUSE_ROW0_VAL_START) : va2off(PAUSE_ROW0_VAL_START) + len(orig_pause0_head)] == orig_pause0_head
    assert buf[va2off(0x4ad75e) : va2off(0x4ad75e) + len(orig_pause0_cmp)] == orig_pause0_cmp
    assert buf[va2off(0x4ad805) : va2off(PAUSE_ROW0_VAL_END)] == orig_pause0_tail

    orig_pause1_head = assemble_at(PAUSE_ROW1_VAL_START, ["mov edx, DWORD PTR ds:0x5f9080"])
    orig_pause1_cmp  = assemble_at(0x4ad8ef, ["cmp DWORD PTR [edx+0x2ac], ebx"])
    orig_pause1_tail = assemble_at(0x4ad996, ["call 0x4ba62a", "add esp, 4"])
    assert buf[va2off(PAUSE_ROW1_VAL_START) : va2off(PAUSE_ROW1_VAL_START) + len(orig_pause1_head)] == orig_pause1_head
    assert buf[va2off(0x4ad8ef) : va2off(0x4ad8ef) + len(orig_pause1_cmp)] == orig_pause1_cmp
    assert buf[va2off(0x4ad996) : va2off(PAUSE_ROW1_VAL_END)] == orig_pause1_tail

    orig_tog_sfx = assemble_at(0x494610, [
        "push esi",
        "mov esi, DWORD PTR ds:0x5f9080",
        "mov eax, DWORD PTR [esi+0x2b0]",
    ])
    orig_tog_mus = assemble_at(0x494650, [
        "push esi",
        "mov esi, DWORD PTR ds:0x5f9080",
        "mov eax, DWORD PTR [esi+0x2ac]",
    ])
    orig_tog_end = assemble_at(0x4946b4, ["lea ecx, [esi+0x298]", "pop esi", "jmp 0x4927d0"])
    assert buf[va2off(0x494610) : va2off(0x494610) + len(orig_tog_sfx)] == orig_tog_sfx
    assert buf[va2off(0x494650) : va2off(0x494650) + len(orig_tog_mus)] == orig_tog_mus
    assert buf[va2off(0x4946b4) : va2off(AUDIO_TOGGLE_END)] == orig_tog_end

    # 1. PauseMenu Row 0 value slot (0x4ad756..0x4ad80d, 183B):
    #    inline stub + clampVol + fmtInt0to100Wstr + drawPauseVolWstr
    pause0_cap = PAUSE_ROW0_VAL_END - PAUSE_ROW0_VAL_START
    put_bytes(buf, PAUSE_ROW0_VAL_START, b"\xcc" * pause0_cap, pause0_cap)
    pause0_code, syms_p0 = link_block_with_symbols(PAUSE_ROW0_VAL_START, [
        ".globl clampVol, fmtInt0to100Wstr, drawPauseVolWstr",
        "mov eax, DWORD PTR ds:0x5f9080",
        "mov eax, DWORD PTR [eax+0x2b0]",
        "mov ecx, 0x8c",
        "call drawPauseVolWstr",
        f"jmp {hex(PAUSE_ROW0_VAL_END)}",

        # clampVol(eax -> eax in [0, 100]); preserves ecx, edx, ebx, esi, edi, ebp
        "clampVol:",
        "test eax, eax",
        "jge cv_le100",
        "xor eax, eax",
        "ret",
        "cv_le100:",
        "cmp eax, 100",
        "jle cv_ok",
        "push 100",
        "pop eax",
        "cv_ok:",
        "ret",

        # fmtInt0to100Wstr(eax = val, ecx = wstring*) -> eax = ecx; preserves ebx, esi, edi, ebp
        "fmtInt0to100Wstr:",
        "call clampVol",
        "xor edx, edx",
        "mov DWORD PTR [ecx], edx",
        "mov DWORD PTR [ecx+4], edx",
        "mov DWORD PTR [ecx+8], edx",
        "mov DWORD PTR [ecx+12], edx",
        "mov DWORD PTR [ecx+20], 7",
        "cmp eax, 100",
        "jne fi_lt100",
        "mov DWORD PTR [ecx], 0x00300031",
        "mov WORD PTR [ecx+4], 0x0030",
        "mov DWORD PTR [ecx+16], 3",
        "mov eax, ecx",
        "ret",
        "fi_lt100:",
        "aam",
        "test ah, ah",
        "je fi_ones",
        "add ah, 0x30",
        "mov BYTE PTR [ecx], ah",
        "inc edx",
        "fi_ones:",
        "add al, 0x30",
        "mov BYTE PTR [ecx+edx*2], al",
        "inc edx",
        "mov DWORD PTR [ecx+16], edx",
        "mov eax, ecx",
        "ret",

        # drawPauseVolWstr(eax = val, ecx = yOff); preserves ebx (=0), esi, edi, ebp
        "drawPauseVolWstr:",
        "push ebx",
        "sub esp, 0x18",
        "mov ebx, ecx",
        "mov ecx, esp",
        "call fmtInt0to100Wstr",
        "mov edx, DWORD PTR [ebp-0x44]",
        "mov ecx, DWORD PTR [ebp-0x48]",
        "add edx, esi",
        "add edx, ebx",
        "add ecx, 0x23d",
        "xor ebx, ebx",
        "push 0x44",
        "push ebx",
        "push ebx",
        "push 0x2000",
        "push 0x2000",
        "push edx",
        "push ecx",
        "push eax",
        "call 0x4a6c80",
        "add esp, 0x38",
        "pop ebx",
        "ret",
    ], pause0_cap)
    put_bytes(buf, PAUSE_ROW0_VAL_START, pause0_code.ljust(pause0_cap, b"\xcc"), pause0_cap)

    # 2. OptionsMenu Row 0 value slot (0x4b2fdf..0x4b307c, 157B):
    #    inline stub + drawMainVolWstr + stepVol10
    main0_cap = MAIN_ROW0_VAL_END - MAIN_ROW0_VAL_START
    put_bytes(buf, MAIN_ROW0_VAL_START, b"\xcc" * main0_cap, main0_cap)
    main0_code, syms_m0 = link_block_with_symbols(MAIN_ROW0_VAL_START, [
        ".globl drawMainVolWstr, stepVol10",
        "mov eax, DWORD PTR ds:0x5f9080",
        "mov eax, DWORD PTR [eax+0x2b0]",
        f"mov edx, {hex(main_row0_y)}",
        "call drawMainVolWstr",
        "jmp 0x4b307f",

        # drawMainVolWstr(eax = val, edx = yOff); preserves ebx, esi, edi (=0), ebp
        "drawMainVolWstr:",
        "push edx",
        "sub esp, 0x18",
        "mov ecx, esp",
        "call fmtInt0to100Wstr",
        "mov ecx, DWORD PTR [ebp-0x48]",
        "mov edx, DWORD PTR [ebp-0x4c]",
        "add ecx, DWORD PTR [esp+0x18]",
        "add edx, 0xe6",
        "push 0x14",
        "push edi",
        "push 1",
        "push 0x2000",
        "push 0x2000",
        "push ecx",
        "push edx",
        "push eax",
        "call 0x4a6c80",
        "add esp, 0x3c",
        "ret",

        # stepVol10(eax = cur) -> eax = next in [0, 100] step 10; preserves ebx, esi, edi, ebp
        "stepVol10:",
        "call clampVol",
        "mov edx, DWORD PTR ds:0x5f9080",
        "cmp BYTE PTR [edx+0xb3], 0",
        "jne sv_left",
        "cdq",
        "push 10",
        "pop ecx",
        "idiv ecx",
        "imul eax, eax, 10",
        "add eax, 10",
        "cmp eax, 100",
        "jle sv_done",
        "xor eax, eax",
        "ret",
        "sv_left:",
        "test eax, eax",
        "jg sv_dec",
        "push 100",
        "pop eax",
        "ret",
        "sv_dec:",
        "dec eax",
        "cdq",
        "push 10",
        "pop ecx",
        "idiv ecx",
        "imul eax, eax, 10",
        "sv_done:",
        "ret",
    ], main0_cap, ext_syms={
        "clampVol": syms_p0["clampVol"],
        "fmtInt0to100Wstr": syms_p0["fmtInt0to100Wstr"],
    })
    put_bytes(buf, MAIN_ROW0_VAL_START, main0_code.ljust(main0_cap, b"\xcc"), main0_cap)

    # 3. OptionsMenu Row 1 value slot (0x4b3168..0x4b31fc, 148B):
    #    inline stub + applySfxVolume + f64_100
    main1_cap = MAIN_ROW1_VAL_END - MAIN_ROW1_VAL_START
    put_bytes(buf, MAIN_ROW1_VAL_START, b"\xcc" * main1_cap, main1_cap)
    main1_code, syms_m1 = link_block_with_symbols(MAIN_ROW1_VAL_START, [
        ".globl applySfxVolume, f64_100",
        "mov eax, DWORD PTR ds:0x5f9080",
        "mov eax, DWORD PTR [eax+0x2ac]",
        f"mov edx, {hex(main_row1_y)}",
        "call drawMainVolWstr",
        f"jmp {hex(MAIN_ROW1_VAL_END)}",

        # applySfxVolume(eax = sfxVol [0..100]); preserves ebx, esi, edi, ebp
        "applySfxVolume:",
        "call clampVol",
        "mov edx, DWORD PTR ds:0x5f903c",
        "test edx, edx",
        "je asv_ret",
        "cvtsi2sd xmm0, eax",
        "divsd xmm0, QWORD PTR [f64_100]",
        "push ecx",
        "cvtpd2ps xmm0, xmm0",
        "mov ecx, edx",
        "movss DWORD PTR [esp], xmm0",
        "push 0",
        "call 0x474480",
        "asv_ret:",
        "ret",

        ".balign 8",
        "f64_100:",
        ".double 100.0",
    ], main1_cap, ext_syms={
        "clampVol": syms_p0["clampVol"],
        "drawMainVolWstr": syms_m0["drawMainVolWstr"],
    })
    put_bytes(buf, MAIN_ROW1_VAL_START, main1_code.ljust(main1_cap, b"\xcc"), main1_cap)

    # 4. PauseMenu Row 1 value slot (0x4ad8e7..0x4ad99e, 183B):
    #    inline stub + scaleMusicVol + applyMusicVolume
    pause1_cap = PAUSE_ROW1_VAL_END - PAUSE_ROW1_VAL_START
    put_bytes(buf, PAUSE_ROW1_VAL_START, b"\xcc" * pause1_cap, pause1_cap)
    pause1_code, syms_p1 = link_block_with_symbols(PAUSE_ROW1_VAL_START, [
        ".globl scaleMusicVol, applyMusicVolume",
        "mov eax, DWORD PTR ds:0x5f9080",
        "mov eax, DWORD PTR [eax+0x2ac]",
        "mov ecx, 0xad",
        "call drawPauseVolWstr",
        f"jmp {hex(PAUSE_ROW1_VAL_END)}",

        # scaleMusicVol called from AudioPlayer::setVolume(1, vol) at 0x4744a3
        # Output: xmm0 = vol * 0.75 * musicVol / 100, [esi+0x10] = xmm0, ecx = [esi+0x8], edx = 0
        "scaleMusicVol:",
        "mov eax, DWORD PTR ds:0x5f9080",
        "test eax, eax",
        "je smv_def100",
        "mov eax, DWORD PTR [eax+0x2ac]",
        "call clampVol",
        "jmp smv_calc",
        "smv_def100:",
        "push 100",
        "pop eax",
        "smv_calc:",
        "cvtsi2sd xmm1, eax",
        "mulsd xmm1, QWORD PTR ds:0x585490",
        "divsd xmm1, QWORD PTR [f64_100]",
        "movss xmm0, DWORD PTR [ebp+0xc]",
        "cvtps2pd xmm0, xmm0",
        "mulsd xmm0, xmm1",
        "cvtpd2ps xmm0, xmm0",
        "movss DWORD PTR [esi+0x10], xmm0",
        "mov ecx, DWORD PTR [esi+0x8]",
        "xor edx, edx",
        "ret",

        # applyMusicVolume(eax = new_vol, ebx = old_vol, esi = ScreenMgr*); preserves ebx, esi, edi, ebp
        "applyMusicVolume:",
        "mov ecx, esi",
        "test eax, eax",
        "jle amv_zero",
        "push 0xc",
        "call 0x4815f0",
        "test ebx, ebx",
        "jle amv_start",
        "mov edx, DWORD PTR ds:0x5f903c",
        "test edx, edx",
        "je amv_ret",
        "push DWORD PTR [esi+0x94]",
        "push 1",
        "mov ecx, edx",
        "call 0x474480",
        "amv_ret:",
        "ret",
        "amv_start:",
        "mov ecx, esi",
        "push 0",
        "call 0x481780",
        "ret",
        "amv_zero:",
        "push 0xb",
        "call 0x4815f0",
        "xorps xmm0, xmm0",
        "push ecx",
        "mov ecx, esi",
        "movss DWORD PTR [esp], xmm0",
        "push 0",
        "call 0x481560",
        "ret",
    ], pause1_cap, ext_syms={
        "clampVol": syms_p0["clampVol"],
        "drawPauseVolWstr": syms_p0["drawPauseVolWstr"],
        "f64_100": syms_m1["f64_100"],
    })
    put_bytes(buf, PAUSE_ROW1_VAL_START, pause1_code.ljust(pause1_cap, b"\xcc"), pause1_cap)

    # 5. Action slot (0x494610..0x4946c0): sfxVolAction + musicVolAction + applyVolOnLoad
    tog_cap = AUDIO_TOGGLE_END - AUDIO_TOGGLE_START
    put_bytes(buf, AUDIO_TOGGLE_START, b"\xcc" * tog_cap, tog_cap)
    tog_code, syms_tog = link_block_with_symbols(AUDIO_TOGGLE_START, [
        ".globl sfxVolAction, musicVolAction, applyVolOnLoad",
        "sfxVolAction:",
        "push esi",
        "mov esi, DWORD PTR ds:0x5f9080",
        "mov eax, DWORD PTR [esi+0x2b0]",
        "call stepVol10",
        "mov DWORD PTR [esi+0x2b0], eax",
        "call applySfxVolume",
        "cmp DWORD PTR [esi+0x2b0], 0",
        "jle sva_save",
        "mov ecx, esi",
        "push 0xc",
        "call 0x4815f0",
        "sva_save:",
        "lea ecx, [esi+0x298]",
        "pop esi",
        "jmp 0x4927d0",

        "musicVolAction:",
        "push esi",
        "push ebx",
        "mov esi, DWORD PTR ds:0x5f9080",
        "mov ebx, DWORD PTR [esi+0x2ac]",
        "mov eax, ebx",
        "call stepVol10",
        "mov DWORD PTR [esi+0x2ac], eax",
        "call applyMusicVolume",
        "lea ecx, [esi+0x298]",
        "pop ebx",
        "pop esi",
        "jmp 0x4927d0",

        # applyVolOnLoad replaces call 0x492ed0 at 0x482b7f inside ScreenMgr::ScreenMgr (esi = ScreenMgr*)
        "applyVolOnLoad:",
        "call 0x492ed0",
        "mov eax, DWORD PTR [esi+0x2ac]",
        "call clampVol",
        "mov DWORD PTR [esi+0x2ac], eax",
        "mov eax, DWORD PTR [esi+0x2b0]",
        "call clampVol",
        "mov DWORD PTR [esi+0x2b0], eax",
        "call applySfxVolume",
        "mov edx, DWORD PTR ds:0x5f903c",
        "test edx, edx",
        "je avol_ret",
        "push 0",
        "push 1",
        "mov ecx, edx",
        "call 0x474480",
        "avol_ret:",
        "ret",
    ], tog_cap, ext_syms={
        "clampVol": syms_p0["clampVol"],
        "stepVol10": syms_m0["stepVol10"],
        "applySfxVolume": syms_m1["applySfxVolume"],
        "applyMusicVolume": syms_p1["applyMusicVolume"],
    })
    put_bytes(buf, AUDIO_TOGGLE_START, tog_code.ljust(tog_cap, b"\xcc"), tog_cap)

    # 6. Install call-site hooks
    # Redirect OptionsMenu and PauseMenu rows 0 & 1 to sfxVolAction / musicVolAction
    for jmp_va, orig_target, new_target in [
        (0x4b2713, 0x494610, syms_tog["sfxVolAction"]),
        (0x4ac9c3, 0x494610, syms_tog["sfxVolAction"]),
        (0x4b2725, 0x494650, syms_tog["musicVolAction"]),
        (0x4ac9d6, 0x494650, syms_tog["musicVolAction"]),
    ]:
        expect_replace(
            buf,
            jmp_va,
            assemble_at(jmp_va, [f"jmp {hex(orig_target)}"], 5),
            assemble_at(jmp_va, [f"jmp {hex(new_target)}"], 5),
        )

    # Hook AudioPlayer::setVolume(1, vol) at 0x4744a3..0x4744bf (28 B)
    orig_set_vol = assemble_at(0x4744a3, [
        "movss xmm0, DWORD PTR [ebp+0xc]",
        "mov ecx, DWORD PTR [esi+0x8]",
        "cvtps2pd xmm0, xmm0",
        "mulsd xmm0, QWORD PTR ds:0x585490",
        "cvtpd2ps xmm0, xmm0",
        "movss DWORD PTR [esi+0x10], xmm0",
    ])
    expect_replace(
        buf,
        0x4744a3,
        orig_set_vol,
        assemble_at(0x4744a3, [f"call {hex(syms_p1['scaleMusicVol'])}", "jmp 0x4744bf"], 10).ljust(len(orig_set_vol), b"\xcc"),
    )

    # Hook ScreenMgr startup loadUserSettings call at 0x482b7f (5 B)
    expect_replace(
        buf,
        0x482b7f,
        assemble_at(0x482b7f, ["call 0x492ed0"], 5),
        assemble_at(0x482b7f, [f"call {hex(syms_tog['applyVolOnLoad'])}"], 5),
    )

    return {
        "audio_main0": (len(main0_code), main0_cap),
        "audio_main1": (len(main1_code), main1_cap),
        "audio_pause0": (len(pause0_code), pause0_cap),
        "audio_pause1": (len(pause1_code), pause1_cap),
        "audio_tog": (len(tog_code), tog_cap),
    }
