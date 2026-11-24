#!/usr/bin/env python3
"""Unified patcher for Two Tribes EDGE (PC Steam build)."""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys

from patches.common import DEFAULT_GAME_DIR, EXPECTED_BAK_SHA256, EXPECTED_BAK_SIZE, to_host_path
from patches.audio_volume import apply_audio_volume
from patches.bonus_levels_unlock import apply_bonus_levels_unlock
from patches.camera_angle import apply_camera_angle
from patches.d3d9_color_fix import apply_d3d9_color_fix
from patches.skip_intro import apply_skip_intro
from patches.time_format import apply_time_format
from patches.ui_rt_sharpening import apply_ui_rt_sharpening
from patches.ui_scale_core import apply_ui_scale_core, build_ui_scale_tables
from patches.ui_scale_menu_ini import apply_ui_scale_menu_and_ini
from patches.ui_smooth_ext import apply_ui_smooth_ext
from patches.ui_smooth_partial import apply_ui_smooth_partial


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def is_edge_running() -> bool:
    if os.name == "nt":
        out = subprocess.check_output(
            ["powershell", "-NoProfile", "-Command", "@(Get-Process -Name 'edge' -ErrorAction SilentlyContinue).Count"],
            text=True,
        ).strip()
        return out != "0"
    if shutil.which("powershell.exe"):
        out = subprocess.check_output(
            ["powershell.exe", "-NoProfile", "-Command", "@(Get-Process -Name 'edge' -ErrorAction SilentlyContinue).Count"],
            text=True,
        ).strip()
        return out != "0"
    return False


def build_image(
    bak_path: str,
    enable_color_fix: bool = True,
    enable_bonus_unlock: bool = True,
    enable_ui_scale: bool = True,
    enable_uirt_fix: bool = True,
    enable_camera_angle: bool = True,
    enable_audio_volume: bool = True,
    enable_ui_smooth: bool = True,
    enable_time_format: bool = True,
    enable_skip_intro: bool = True,
) -> tuple[bytearray, dict[str, tuple[int, int]]]:
    if enable_uirt_fix and not enable_ui_scale:
        raise ValueError("--no-ui-scale cannot be combined with uiRT sharpening; pass --no-uirt-fix too.")
    if enable_skip_intro and not enable_time_format:
        raise ValueError("--no-time-format cannot be combined with the skip intro row; pass --no-skip-intro too.")
    if enable_time_format and not (enable_ui_scale and enable_camera_angle):
        raise ValueError("--no-ui-scale / --no-camera-angle cannot be combined with the time format row; pass --no-time-format too.")
    if enable_camera_angle and not enable_ui_scale:
        raise ValueError("--no-ui-scale cannot be combined with camera angle menu/INI; pass --no-camera-angle too.")

    with open(bak_path, "rb") as f:
        buf = bytearray(f.read())

    assert len(buf) == EXPECTED_BAK_SIZE, f"Unexpected backup size {len(buf)} != {EXPECTED_BAK_SIZE}"
    bak_hash = sha256_file(bak_path)
    if bak_hash != EXPECTED_BAK_SHA256:
        print(f"Warning: {bak_path} SHA-256 is {bak_hash}, expected original {EXPECTED_BAK_SHA256}", file=sys.stderr)

    stats: dict[str, tuple[int, int]] = {}
    if enable_color_fix:
        apply_d3d9_color_fix(buf)
    if enable_bonus_unlock:
        apply_bonus_levels_unlock(buf)
    if enable_ui_scale:
        build_ui_scale_tables(buf)
        apply_ui_scale_core(buf)
        if enable_uirt_fix:
            stats.update(apply_ui_rt_sharpening(buf))
        stats.update(
            apply_ui_scale_menu_and_ini(
                buf,
                include_camera_row=enable_camera_angle,
                include_time_row=enable_time_format,
                include_skip_row=enable_skip_intro,
            )
        )
    if enable_camera_angle:
        stats.update(
            apply_camera_angle(
                buf,
                enable_menu_ini=enable_ui_scale,
                include_time_row=enable_time_format,
                include_skip_row=enable_skip_intro,
            )
        )
    if enable_audio_volume:
        stats.update(apply_audio_volume(buf))
    if enable_ui_smooth:
        ui_syms: dict[str, int] = {}
        stats.update(apply_ui_smooth_partial(buf, out_syms=ui_syms))
        stats.update(apply_ui_smooth_ext(buf, ui_syms))
    if enable_time_format:
        stats.update(apply_time_format(buf, include_skip_row=enable_skip_intro))
    if enable_skip_intro:
        stats.update(apply_skip_intro(buf))
    return buf, stats


def main() -> int:
    default_target = to_host_path(os.path.join(DEFAULT_GAME_DIR, "edge.exe"))
    default_bak = to_host_path(os.path.join(DEFAULT_GAME_DIR, "edge.exe.bak"))
    default_staged = os.path.join(os.path.dirname(os.path.abspath(__file__)), "edge_patched_staged.exe")

    p = argparse.ArgumentParser(description="Build, stage, apply, or restore EDGE PC fixes.")
    mode = p.add_mutually_exclusive_group()
    mode.add_argument("--stage", action="store_true", help="Build into staged file (default if neither --apply/--restore)")
    mode.add_argument("--apply", action="store_true", help="Build and install directly onto target edge.exe (fails if running)")
    mode.add_argument("--restore", action="store_true", help="Restore target edge.exe from edge.exe.bak")
    mode.add_argument("--verify", action="store_true", help="Verify staged/target file hashes against backup")

    p.add_argument("--bak", default=default_bak, help="Path to clean edge.exe.bak")
    p.add_argument("--target", default=default_target, help="Path to installed edge.exe")
    p.add_argument("--out", default=default_staged, help="Output path when staging")

    p.add_argument("--no-color-fix", action="store_true", help="Skip D3D9 texture-transform color fix")
    p.add_argument("--no-bonus-unlock", action="store_true", help="Skip bonus-level unlock fix")
    p.add_argument("--no-ui-scale", action="store_true", help="Skip UI scale + Options menu + INI patches")
    p.add_argument("--no-uirt-fix", action="store_true", help="Skip offscreen uiRT fractional font fix")
    p.add_argument("--no-camera-angle", action="store_true", help="Skip PC/mobile camera angle toggle & framing")
    p.add_argument("--no-audio-volume", action="store_true", help="Skip customizable 0-100 music & SFX volume")
    p.add_argument("--no-ui-smooth", action="store_true", help="Skip partial 60fps+ fade, flash, and minimap smoothing")
    p.add_argument("--no-time-format", action="store_true", help="Skip the customizable time format Options row (default vs modern)")
    p.add_argument("--no-skip-intro", action="store_true", help="Skip the skip intro Options row")
    args = p.parse_args()

    if args.verify:
        for label, path in [("backup", args.bak), ("target", args.target), ("staged", args.out)]:
            if os.path.exists(path):
                print(f"{label:7s}: size={os.path.getsize(path)} sha256={sha256_file(path)} ({path})")
            else:
                print(f"{label:7s}: MISSING ({path})")
        return 0

    if args.restore:
        if is_edge_running():
            print("Error: edge.exe is currently running. Close EDGE before restoring.", file=sys.stderr)
            return 1
        assert os.path.exists(args.bak), f"Backup not found: {args.bak}"
        shutil.copy2(args.bak, args.target)
        print(f"Restored {args.target} from {args.bak} (sha256={sha256_file(args.target)})")
        return 0

    if not os.path.exists(args.bak) and os.path.exists(args.target):
        if sha256_file(args.target) == EXPECTED_BAK_SHA256:
            shutil.copy2(args.target, args.bak)
            print(f"Created clean backup: {args.bak}")
        else:
            print(f"Error: {args.bak} not found and current {args.target} is already modified.", file=sys.stderr)
            return 1

    buf, stats = build_image(
        args.bak,
        enable_color_fix=not args.no_color_fix,
        enable_bonus_unlock=not args.no_bonus_unlock,
        enable_ui_scale=not args.no_ui_scale,
        enable_uirt_fix=not args.no_uirt_fix,
        enable_camera_angle=not args.no_camera_angle,
        enable_audio_volume=not args.no_audio_volume,
        enable_ui_smooth=not args.no_ui_smooth,
        enable_time_format=not args.no_time_format,
        enable_skip_intro=not args.no_skip_intro,
    )

    out_path = args.target if args.apply else args.out
    if args.apply and is_edge_running():
        print("Error: edge.exe is currently running. Close EDGE before applying (use --stage to build safely).", file=sys.stderr)
        return 1

    with open(out_path, "wb") as f:
        f.write(buf)

    print("Allocations:", ", ".join(f"{k}={u}/{t}" for k, (u, t) in stats.items()) or "none")
    print(f"Wrote {out_path} ({len(buf)} bytes, sha256={sha256_file(out_path)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
