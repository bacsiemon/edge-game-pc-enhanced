from .common import expect_replace, assemble_at


def apply_bonus_levels_unlock(buf: bytearray) -> None:
    """
    Ensure bonus levels are always treated as unlocked inside 0x4944d0 after the level-index bounds check.
    Replaces reading the asynchronous Steam community group flag ([ecx+0x2d4]) at 0x49450e with `mov al, 1`.
    """
    expect_replace(
        buf,
        0x49450e,
        bytes.fromhex("8a 81 d4 02 00 00"),
        assemble_at(0x49450e, ["mov al, 1", "nop", "nop", "nop", "nop"], 6),
    )
