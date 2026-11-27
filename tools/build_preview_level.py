#!/usr/bin/env python3
"""Verify the EDGE .bin level binary schema and generate levels/graphics_preview.bin."""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.common import DEFAULT_GAME_DIR, to_host_path


class BinReader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pos = 0

    def remaining(self) -> int:
        return len(self.data) - self.pos

    def u8(self) -> int:
        v = self.data[self.pos]
        self.pos += 1
        return v

    def u16(self) -> int:
        v = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2
        return v

    def i16(self) -> int:
        v = struct.unpack_from("<h", self.data, self.pos)[0]
        self.pos += 2
        return v

    def u32(self) -> int:
        v = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def bytes(self, n: int) -> bytes:
        b = self.data[self.pos : self.pos + n]
        if len(b) != n:
            raise EOFError(f"Expected {n} bytes at {self.pos}, got {len(b)}")
        self.pos += n
        return b


def parse_level_bin(data: bytes) -> dict:
    r = BinReader(data)
    level_id = r.u32()
    name_len = r.u32()
    name = r.bytes(name_len).decode("utf-8", errors="replace")
    ranks = [r.u16() for _ in range(5)]
    total_prisms = r.i16()
    height_tiles = r.u8()
    size_x = r.u16()
    size_z = r.u16()
    unk_header = (r.u16(), r.u16())

    mm_w = r.u16()
    mm_h = r.u16()
    mm_unk_u8 = r.u8()
    mm_unk_a = r.u16()
    mm_unk_b = r.u16()
    mm_bytes = (mm_w * mm_h + 7) // 8
    mm_data = r.bytes(mm_bytes)

    layer_bytes = (size_x * size_z + 7) // 8
    layers = [r.bytes(layer_bytes) for _ in range(max(0, height_tiles))]

    spawn = (r.i16(), r.i16(), r.i16())
    cam_zoom = r.i16()
    if cam_zoom < 0:
        cam_zoom_extra = r.i16()
        cam_flag = r.u8()
    else:
        cam_zoom_extra = None
        cam_flag = None

    finish = (r.i16(), r.i16(), r.i16())

    num_moving = r.i16()
    moving_plats = []
    for _ in range(num_moving):
        auto_start = r.u8()
        active_flag = r.u8()
        speed_or_unk = r.u16()
        loop_or_unk = r.u8()
        n_wp = r.u8()
        wps = [(r.u16(), r.u16(), r.u16(), r.u16(), r.u16()) for _ in range(n_wp)]
        moving_plats.append((auto_start, active_flag, speed_or_unk, loop_or_unk, wps))

    num_bumpers = r.i16()
    bumpers = []
    for _ in range(num_bumpers):
        b_type = r.u8()
        bx, by, bz = r.i16(), r.i16(), r.i16()
        sides = [(r.i16(), r.i16()) for _ in range(4)]
        bumpers.append((b_type, bx, by, bz, sides))

    num_falling = r.i16()
    falling = [(r.i16(), r.i16(), r.i16(), r.i16()) for _ in range(num_falling)]

    num_checkpoints = r.i16()
    checkpoints = [
        (r.i16(), r.i16(), r.i16(), r.i16(), r.u8(), r.u8())
        for _ in range(num_checkpoints)
    ]

    num_resizers = r.i16()
    resizers = []
    for _ in range(num_resizers):
        rx, ry, rz = r.i16(), r.i16(), r.i16()
        trig = r.u16()
        f1, f2 = r.u8(), r.u8()
        extra = None
        if trig == 0xFFFF:
            extra = (r.u8(), r.i16(), r.i16(), r.i16(), r.u8(), r.u8())
        resizers.append((rx, ry, rz, trig, f1, f2, extra))

    num_prisms = r.i16()
    prisms = [(r.i16(), r.i16(), r.i16(), r.u8()) for _ in range(num_prisms)]

    skipped_extra = r.i16()
    num_triggers = r.i16()
    triggers = [(r.u8(), r.i16(), r.i16()) for _ in range(num_triggers)]

    num_switches = r.i16()
    switches = []
    for _ in range(num_switches):
        s0, s1, s2 = r.u8(), r.u8(), r.u8()
        s3 = r.u16()
        s4, s5, mode = r.u8(), r.u8(), r.u8()
        if mode == 0:
            pos_or_idx = (r.i16(), r.i16(), r.i16())
        elif mode == 1:
            pos_or_idx = (r.i16(),)
        else:
            pos_or_idx = ()
        cnt = r.i16()
        targets = [r.u16() for _ in range(cnt)]
        switches.append((s0, s1, s2, s3, s4, s5, mode, pos_or_idx, targets))

    num_holo = r.i16()
    holos = []
    for _ in range(num_holo):
        hx, hy, hz = r.i16(), r.i16(), r.i16()
        h0 = r.u16()
        h_extra = None
        if h0 == 0xFFFE:
            h_extra = (r.u8(), r.u8(), r.u16())
        h1, h2, h3, h4 = r.i16(), r.i16(), r.i16(), r.i16()
        entries = [(r.i16(), r.u8(), r.u8()) for _ in range(h1)]
        holos.append((hx, hy, hz, h0, h_extra, h1, h2, h3, h4, entries))

    num_shrinkers = r.i16()
    shrinkers = [(r.i16(), r.i16(), r.i16(), r.u8(), r.u8()) for _ in range(num_shrinkers)]

    tail_unused = r.i16()
    theme = r.u8()
    unk_tail_u8 = r.u8()
    music_id = r.u8()

    return {
        "level_id": level_id,
        "name": name,
        "height_tiles": height_tiles,
        "size_x": size_x,
        "size_z": size_z,
        "spawn": spawn,
        "finish": finish,
        "num_moving": num_moving,
        "num_bumpers": num_bumpers,
        "num_falling": num_falling,
        "num_prisms": num_prisms,
        "num_switches": num_switches,
        "num_holo": num_holo,
        "num_shrinkers": num_shrinkers,
        "theme": theme,
        "music_id": music_id,
        "remaining": r.remaining(),
        "total_bytes": len(data),
    }


def verify_real_levels(levels_dir: pathlib.Path) -> None:
    sample_names = [
        "ending_normal.bin",
        "compost_805.bin",
        "gears_802.bin",
        "babylonian_817.bin",
        "chunk_816.bin",
        "cubism_808.bin",
    ]
    checked = 0
    for fname in sample_names:
        p = levels_dir / fname
        if not p.is_file():
            continue
        info = parse_level_bin(p.read_bytes())
        print(
            f"[verify] {fname}: {info['total_bytes']} B, "
            f"{info['size_x']}x{info['height_tiles']}x{info['size_z']}, "
            f"theme={info['theme']}, music={info['music_id']}, remaining={info['remaining']}"
        )
        if info["remaining"] != 0:
            raise AssertionError(f"Parser mismatch on {fname}: remaining={info['remaining']}")
        checked += 1
    if checked < 3:
        raise RuntimeError(f"Expected at least 3 sample .bin files in {levels_dir}, found {checked}")


def build_preview_level_bytes(levels_dir: pathlib.Path) -> bytes:
    """Build graphics_preview.bin using 1st contact (level309.bin) terrain as the base."""
    src_path = levels_dir / "level309.bin"
    if not src_path.is_file():
        raise FileNotFoundError(f"Base level not found: {src_path}")

    r = BinReader(src_path.read_bytes())
    _src_id = r.u32()
    src_name_len = r.u32()
    _src_name = r.bytes(src_name_len)
    _src_ranks = [r.u16() for _ in range(5)]
    _src_total_prisms = r.i16()
    height_tiles = r.u8()
    size_x = r.u16()
    size_z = r.u16()
    unk_header = (r.u16(), r.u16())

    mm_w = r.u16()
    mm_h = r.u16()
    mm_unk_u8 = r.u8()
    mm_unk_a = r.u16()
    mm_unk_b = r.u16()
    mm_bytes = (mm_w * mm_h + 7) // 8
    mm_data = r.bytes(mm_bytes)

    layer_bytes = (size_x * size_z + 7) // 8
    layers = [r.bytes(layer_bytes) for _ in range(max(0, height_tiles))]

    # 1 showcase bumper at (9, 7, 1) + all 8 original level309 directional-instruction bumpers
    keep_bumpers = [
        (1, 9, 7, 1, [(-1, -1), (10, 36), (28, 36), (-1, -1)]),
        (1, 11, 7, 0, [(-1, -1), (-1, -1), (12, 30), (-1, -1)]),
        (1, 11, 6, 0, [(-1, -1), (-1, -1), (8, 30), (-1, -1)]),
        (1, 11, 5, 0, [(-1, -1), (-1, -1), (4, 30), (-1, -1)]),
        (1, 11, 4, 0, [(-1, -1), (-1, -1), (0, 30), (-1, -1)]),
        (1, 6, 17, 0, [(-1, -1), (-1, -1), (4, 30), (-1, -1)]),
        (1, 6, 16, 0, [(-1, -1), (-1, -1), (8, 30), (-1, -1)]),
        (1, 6, 15, 0, [(-1, -1), (-1, -1), (12, 30), (-1, -1)]),
        (1, 6, 18, 0, [(-1, -1), (-1, -1), (0, 30), (-1, -1)]),
    ]

    buf = bytearray()
    w8 = lambda v: buf.append(v & 0xFF)
    w16 = lambda v: buf.extend(struct.pack("<h", v))
    wu16 = lambda v: buf.extend(struct.pack("<H", v))
    wu32 = lambda v: buf.extend(struct.pack("<I", v))

    # Header (preserve exact level309 dimensions, minimap, and bit-packed static terrain layers)
    wu32(9999)  # levelId
    name_bytes = b"graphics_preview"
    wu32(len(name_bytes))
    buf.extend(name_bytes)
    for rt in (20, 30, 45, 60, 90):
        wu16(rt)
    w16(2)  # totalPrisms
    w8(height_tiles)
    wu16(size_x)
    wu16(size_z)
    wu16(unk_header[0])
    wu16(unk_header[1])

    wu16(mm_w)
    wu16(mm_h)
    w8(mm_unk_u8)
    wu16(mm_unk_a)
    wu16(mm_unk_b)
    buf.extend(mm_data)

    for layer in layers:
        buf.extend(layer)

    # Player cube spawn (X, Z, Y): exact screenshot ledge nub (7, 10, 1) on y=1 floor
    w16(7)
    w16(10)
    w16(1)

    # Camera zoom preset (overridden at runtime by ApplyPreviewCamera_Impl)
    w16(0)

    # Finish platform (X, Z, Y): original 1st contact finish pad (11, 12, 1)
    w16(11)
    w16(12)
    w16(1)

    # Moving platforms: 2 synchronized active full-height platforms forming a 1x2 block on the y=2 terrace (z=3..4, y=3),
    # ping-ponging NW (x=7) <-> SE (x=9) at 1 tile/sec starting immediately.
    w16(2)  # count
    for mp_z in (3, 4):
        w8(2)         # active_mode = 2 (active immediately)
        w8(1)         # loop_target_plus1 = 1 (loop to wp 0)
        wu16(0xFFFF)  # cloner/unused
        w8(1)         # is_full_height = 1 (platform.rmdl)
        w8(2)         # 2 waypoints
        wu16(7); wu16(mp_z); wu16(3); wu16(60); wu16(0)
        wu16(9); wu16(mp_z); wu16(3); wu16(60); wu16(0)

    # Bumpers (type, X, Z, Y, 4x (delay, period))
    w16(len(keep_bumpers))
    for b_type, bx, bz, by, sides in keep_bumpers:
        w8(b_type)
        w16(bx); w16(bz); w16(by)
        for d_ticks, p_ticks in sides:
            w16(d_ticks); w16(p_ticks)

    # Falling platforms (X, Z, Y, delay): 1 row (x=8, z=8..10, y=1) in the open central chasm
    w16(3)
    w16(8); w16(8);  w16(1); w16(10)
    w16(8); w16(9);  w16(1); w16(10)
    w16(8); w16(10); w16(1); w16(10)

    # Checkpoints: 0
    w16(0)

    # Resizers: 0
    w16(0)

    # Prisms (X, Z, Y, flag): 2 original in-frame prisms from level309
    w16(2)
    w16(6);  w16(6); w16(2); w8(1)
    w16(11); w16(5); w16(1); w8(1)

    # Skipped extra + Triggers: 0
    w16(0)
    w16(0)

    # Switches (X, Z, Y):
    # - 1 normal switch ("switch", s0=1) SW of shrinkers at (6, 13, 1)
    # - 1 translucent ghost switch ("switch_ghost", s0=2) at (8, 13, 1) replacing the previous hint tile spot
    w16(2)
    w8(1); w8(0); w8(0)
    wu16(0xFFFF)
    w8(0); w8(0); w8(0)  # mode=0 (grid pos)
    w16(6); w16(13); w16(1)
    w16(0)  # 0 targets

    w8(2); w8(0); w8(2)
    wu16(0xFFFF)
    w8(0); w8(0); w8(0)  # mode=0 (grid pos)
    w16(8); w16(13); w16(1)
    w16(0)  # 0 targets

    # Holo switches (X, Z, Y): 1 prominent holoswitch (? hint tile) at (6, 7, 1)
    w16(1)
    w16(6); w16(7); w16(1)
    wu16(0xFFFF)
    w16(0); w16(0); w16(0); w16(0)

    # Shrinkers (X, Z, Y, flag0, type): 1 to_mini (0) + 1 to_big (1) with flag0=1 (visible) in foreground
    w16(2)
    w16(6); w16(12); w16(1); w8(1); w8(0)
    w16(7); w16(12); w16(1); w8(1); w8(1)

    # Tail
    w16(0)  # ignored short
    w8(0)   # theme (overridden at runtime to 0..3)
    w8(0)   # unused byte
    w8(0)   # musicId

    return bytes(buf)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--verify-only", action="store_true", help="Only verify real .bin levels without writing output")
    args = ap.parse_args()

    game_dir = pathlib.Path(to_host_path(DEFAULT_GAME_DIR))
    levels_dir = game_dir / "levels"
    verify_real_levels(levels_dir)

    data = build_preview_level_bytes(levels_dir)
    parsed = parse_level_bin(data)
    if parsed["remaining"] != 0:
        raise AssertionError(f"Generated preview level failed round-trip parse: remaining={parsed['remaining']}")
    print(
        f"[ok] Built graphics_preview.bin ({len(data)} B, "
        f"{parsed['size_x']}x{parsed['height_tiles']}x{parsed['size_z']}, "
        f"moving={parsed['num_moving']}, bumpers={parsed['num_bumpers']}, "
        f"falling={parsed['num_falling']}, prisms={parsed['num_prisms']}, "
        f"switches={parsed['num_switches']}, holo={parsed['num_holo']}, "
        f"shrinkers={parsed['num_shrinkers']})"
    )
    if args.verify_only:
        return 0

    out_paths = [
        levels_dir / "graphics_preview.bin",
        REPO_ROOT / "releases" / "levels" / "graphics_preview.bin",
    ]
    for out in out_paths:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
        print(f"[wrote] {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
