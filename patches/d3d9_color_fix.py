from .common import va2off, put_bytes, expect_replace, assemble_at


def apply_d3d9_color_fix(buf: bytearray) -> None:
    """
    Preserve D3DTSS_TEXTURETRANSFORMFLAGS = D3DTTFF_COUNT2 (2) across Direct3D 9 frames and materials.
    Prevents prisms, moving platforms, and goal areas from losing color-cycling transforms when
    frame overlays (e.g. RTSS) alter stage texture-transform flags after Present().
    """
    cave_off = va2off(0x5765b0)
    assert bytes(buf[cave_off : cave_off + 0x50]) == b"\x00" * 0x50, "Padding 0x5765b0..0x576600 not zero"

    # Helper 1 at 0x5765b0: Material::apply SetTransform + SetTextureStageState(stage, TEXTURETRANSFORMFLAGS, COUNT2)
    helper1 = assemble_at(0x5765b0, [
        "push ecx",
        "lea edx, [ebx+0x10]",
        "push edx",
        "push eax",
        "mov edx, DWORD PTR [eax]",
        "call DWORD PTR [edx+0xb0]",
        "mov eax, DWORD PTR [ebp-0x4]",
        "mov edx, DWORD PTR [eax]",
        "push 2",
        "push 0x18",
        "push ebx",
        "push eax",
        "call DWORD PTR [edx+0x10c]",
        "ret",
    ], 32)
    put_bytes(buf, 0x5765b0, helper1, 32)

    # Helper 2 at 0x5765d0: Renderer::beginScene restore TextureTransformFlags=2 on stages 0..7 + original mov ecx,[esi+0xc0]
    helper2 = assemble_at(0x5765d0, [
        "mov eax, DWORD PTR [esi+0xcc]",
        "test eax, eax",
        "je skip",
        "push ebx",
        "push edi",
        "mov edi, eax",
        "xor ebx, ebx",
        "stage_loop:",
        "mov edx, DWORD PTR [edi]",
        "push 2",
        "push 0x18",
        "push ebx",
        "push edi",
        "call DWORD PTR [edx+0x10c]",
        "inc ebx",
        "cmp ebx, 8",
        "jl stage_loop",
        "pop edi",
        "pop ebx",
        "skip:",
        "mov ecx, DWORD PTR [esi+0xc0]",
        "ret",
    ], 48)
    put_bytes(buf, 0x5765d0, helper2, 48)

    # Hook Material::apply (0x437388, 8B) and Renderer::beginScene (0x405f03, 6B)
    expect_replace(
        buf,
        0x437388,
        bytes.fromhex("51 83 c3 10 53 50 ff d2"),
        assemble_at(0x437388, ["call 0x5765b0", "nop", "nop", "nop"], 8),
    )
    expect_replace(
        buf,
        0x405f03,
        bytes.fromhex("8b 8e c0 00 00 00"),
        assemble_at(0x405f03, ["call 0x5765d0", "nop"], 6),
    )
