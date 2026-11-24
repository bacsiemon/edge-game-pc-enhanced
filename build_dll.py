#!/usr/bin/env python3
"""Build, offline-verify, deploy, and restore the C++ XINPUT1_3.dll EDGE PC mod."""

import argparse
import hashlib
import os
import pathlib
import shutil
import struct
import subprocess
import sys

from patches.common import DEFAULT_GAME_DIR, EXPECTED_BAK_SHA256, EXPECTED_BAK_SIZE, to_host_path

REPO_ROOT = pathlib.Path(__file__).resolve().parent
DLL_DIR = REPO_ROOT / "dll"
BUILD_DIR = DLL_DIR / "build"
DLL_OUT = BUILD_DIR / "XINPUT1_3.dll"
VERIFIER_OUT = BUILD_DIR / "verify_dll.exe"
RANGES_TXT = BUILD_DIR / "registered_ranges.txt"
RELEASE_DLL = REPO_ROOT / "releases" / "XINPUT1_3.dll"

VCVARS_CANDIDATES = [
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
    r"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat",
    r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat",
    r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
]

DLL_SOURCES = [
    "xinput_proxy.cpp",
    "mod_core_fixes.cpp",
    "mod_ui_scale_rt.cpp",
    "mod_menu_camera_options.cpp",
    "mod_finish_effects.cpp",
    "mod_color_theme.cpp",
    "mod_ui_smooth_part1.cpp",
    "mod_ui_smooth_part2.cpp",
]

# Diff regions in edge_patched_staged.exe that are intentionally superseded or unused in the DLL architecture:
# - 0x4061b8..0x4061c5: NDS branch shortcut only needed to reclaim Block B (0x4061c8..0x406404)
# - 0x4b2713..0x4b2727, 0x4ac9c3..0x4ac9d8: audio_volume redirected jump call sites; DLL hooks 0x494610/0x494650 directly
# - 0x4b2908..0x4b299d: Options slide Y (`0x4b2907` starts 1B earlier in DLL over dead mov/lea; both land at 0x4b299d)
SUPERSEDED_STATIC_RANGES = [
    (0x4061b8, 0x4061c5),
    (0x4b2713, 0x4b2727),
    (0x4ac9c3, 0x4ac9d8),
]


def sha256_file(path: os.PathLike | str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def is_edge_running() -> bool:
    ps = "powershell" if os.name == "nt" else "powershell.exe"
    out = subprocess.check_output(
        [ps, "-NoProfile", "-Command", "@(Get-Process -Name 'edge' -ErrorAction SilentlyContinue).Count"],
        text=True,
    ).strip()
    return out != "0"


def find_vcvars() -> str:
    env_vcvars = os.environ.get("VCVARSALL", "").strip()
    if env_vcvars and os.path.exists(env_vcvars):
        return env_vcvars
    for cand in VCVARS_CANDIDATES:
        if os.path.exists(cand):
            return cand
    raise RuntimeError("Could not locate Visual Studio 2022 vcvarsall.bat (set VCVARSALL if installed elsewhere)")


def compile_dll_and_verifier() -> None:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    vcvars = find_vcvars()
    srcs = " ".join(f'"{DLL_DIR / s}"' for s in DLL_SOURCES)
    def_file = f'"{DLL_DIR / "xinput1_3.def"}"'

    bat_path = BUILD_DIR / "_build.bat"
    bat_content = (
        "@echo off\r\n"
        f'call "{vcvars}" x86 >nul || exit /b 1\r\n'
        f'cl.exe /nologo /O2 /arch:SSE2 /EHsc /MT /std:c++17 /W3 '
        f'/Fo"{BUILD_DIR}\\\\" /Fe"{DLL_OUT}" /LD {srcs} {def_file} user32.lib || exit /b 1\r\n'
        f'cl.exe /nologo /O2 /EHsc /MT /std:c++17 /W3 '
        f'/Fo"{BUILD_DIR}\\\\" /Fe"{VERIFIER_OUT}" "{DLL_DIR / "verify_dll.cpp"}" || exit /b 1\r\n'
    )
    bat_path.write_text(bat_content, encoding="utf-8")
    print("Building XINPUT1_3.dll & verify_dll.exe (x86)...")
    res = subprocess.run([str(bat_path)], cwd=str(BUILD_DIR), capture_output=True, text=True)
    if res.returncode != 0:
        print(res.stdout)
        print(res.stderr, file=sys.stderr)
        raise RuntimeError(f"Build failed with code {res.returncode}")
    elif res.stdout.strip():
        print(res.stdout.strip())


def check_exports(dll_path: pathlib.Path) -> list[tuple[int, str | None]]:
    b = dll_path.read_bytes()
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    machine = struct.unpack_from("<H", b, pe + 4)[0]
    assert machine == 0x14C, f"Expected x86 PE machine 0x14c, got {hex(machine)}"
    exp_rva, _ = struct.unpack_from("<II", b, pe + 0x78)
    num_sec = struct.unpack_from("<H", b, pe + 6)[0]
    opt_sz = struct.unpack_from("<H", b, pe + 20)[0]
    sec_off = pe + 24 + opt_sz
    secs = []
    for i in range(num_sec):
        vsz, rva, rawsz, rawoff = struct.unpack_from("<IIII", b, sec_off + i * 40 + 8)
        secs.append((rva, max(vsz, rawsz), rawoff))

    def r2o(r):
        for srva, sz, roff in secs:
            if srva <= r < srva + sz:
                return roff + (r - srva)
        return None

    eoff = r2o(exp_rva)
    base_ord, num_funcs, num_names, addr_funcs, addr_names, addr_ords = struct.unpack_from("<IIIIII", b, eoff + 16)
    names_map = {}
    for i in range(num_names):
        nrva = struct.unpack_from("<I", b, r2o(addr_names) + i * 4)[0]
        ord_idx = struct.unpack_from("<H", b, r2o(addr_ords) + i * 2)[0]
        noff = r2o(nrva)
        names_map[ord_idx] = b[noff : b.index(b"\x00", noff)].decode("ascii")
    exports = []
    for i in range(num_funcs):
        frva = struct.unpack_from("<I", b, r2o(addr_funcs) + i * 4)[0]
        if frva:
            exports.append((base_ord + i, names_map.get(i)))
    ords = {o for o, _ in exports}
    required_ords = {2, 3, 4, 5, 6, 7, 8, 100, 101, 102, 103}
    missing = required_ords - ords
    assert not missing, f"Missing required XINPUT1_3 export ordinals: {sorted(missing)}"
    return exports


def verify_against_bak_and_staged(bak_path: pathlib.Path, staged_path: pathlib.Path) -> None:
    exports = check_exports(DLL_OUT)
    print(f"Verified PE32 exports ({len(exports)} ordinals present, including 2..8 & 100..103).")

    res = subprocess.run([str(VERIFIER_OUT), str(DLL_OUT), str(bak_path), str(RANGES_TXT)], capture_output=True, text=True)
    print(res.stdout.strip())
    if res.returncode != 0:
        print(res.stderr, file=sys.stderr)
        raise RuntimeError("Offline expected-bytes verification against edge.exe.bak FAILED")

    if not staged_path.exists():
        print("Staged static EXE not present; skipping static-diff cross-check.")
        return

    reg_ranges = []
    for line in RANGES_TXT.read_text().splitlines():
        s_str, e_str = line.split()
        reg_ranges.append((int(s_str, 16), int(e_str, 16)))

    caves = [
        (0x4061c8, 0x406404), (0x407e87, 0x408382), (0x412f60, 0x4130de), (0x41f220, 0x41f3ca),
        (0x4234d0, 0x4235e0), (0x4248e0, 0x4249b2), (0x43b060, 0x43b1b8), (0x44bad0, 0x44bd20),
        (0x44df40, 0x44e154), (0x453520, 0x4537a0), (0x453fa0, 0x4542cd), (0x45cc80, 0x45cd9d),
        (0x45dc20, 0x45df90), (0x45eae0, 0x45edc2), (0x479010, 0x4791cb), (0x484c10, 0x484e5f),
        (0x4f79c0, 0x4f7ad6), (0x4fa5d0, 0x4fa8f0), (0x4faa80, 0x4face0), (0x4fca10, 0x4fcc34),
        (0x510d30, 0x510fe0), (0x511030, 0x5114b0), (0x51bdc0, 0x51bed5), (0x52c560, 0x52caa0),
        (0x5347d0, 0x5348ec), (0x539770, 0x539a19), (0x541b10, 0x541c2c), (0x5423f0, 0x542750),
        (0x5765b0, 0x576600), (0x5e2840, 0x5e2a00), (0x5f8730, 0x5f8744), (0x5f8798, 0x5f87a8),
    ]

    def off2va(off):
        if 0x400 <= off < 0x175a00: return 0x401000 + (off - 0x400)
        if 0x175a00 <= off < 0x1e1400: return 0x577000 + (off - 0x175a00)
        if 0x1e1400 <= off < 0x1f6c00: return 0x5e3000 + (off - 0x1e1400)
        return None

    def in_cave(va):
        return any(s <= va < e for s, e in caves)

    bak = bak_path.read_bytes()
    stg = staged_path.read_bytes()
    uncovered = []
    i = 0
    n = len(bak)
    while i < n:
        if bak[i] != stg[i]:
            va = off2va(i)
            if va and not in_cave(va):
                covered = any(rs <= va < re for rs, re in reg_ranges) or any(
                    ss <= va < se for ss, se in SUPERSEDED_STATIC_RANGES
                )
                if not covered:
                    uncovered.append(va)
        i += 1
    if uncovered:
        print(f"ERROR: {len(uncovered)} static diff bytes not covered by DLL patches, e.g. {[hex(x) for x in uncovered[:15]]}")
        raise RuntimeError("Coverage check failed")
    print("Cross-check vs edge_patched_staged.exe: 100% of static non-cave patch regions covered.")


def main() -> int:
    default_game = pathlib.Path(to_host_path(DEFAULT_GAME_DIR))
    p = argparse.ArgumentParser(description="Build, verify, and deploy XINPUT1_3.dll mod for EDGE.")
    mode = p.add_mutually_exclusive_group()
    mode.add_argument("--stage", action="store_true", help="Build DLL and verify offline (default)")
    mode.add_argument("--apply", action="store_true", help="Build, restore clean edge.exe from backup, and install XINPUT1_3.dll")
    mode.add_argument("--restore", action="store_true", help="Remove XINPUT1_3.dll and restore clean edge.exe")
    mode.add_argument("--verify", action="store_true", help="Verify deployed edge.exe + XINPUT1_3.dll hashes")
    p.add_argument("--game-dir", default=str(default_game), help="Game directory containing edge.exe")
    args = p.parse_args()

    game_dir = pathlib.Path(args.game_dir)
    bak_path = game_dir / "edge.exe.bak"
    exe_path = game_dir / "edge.exe"
    installed_dll = game_dir / "XINPUT1_3.dll"
    staged_exe = REPO_ROOT / "edge_patched_staged.exe"

    if args.verify:
        for lbl, fp in [("backup", bak_path), ("edge.exe", exe_path), ("dll_built", DLL_OUT), ("dll_rel", RELEASE_DLL), ("dll_game", installed_dll)]:
            if fp.exists():
                print(f"{lbl:10s}: size={fp.stat().st_size} sha256={sha256_file(fp)} ({fp})")
            else:
                print(f"{lbl:10s}: MISSING ({fp})")
        return 0

    if args.restore:
        if is_edge_running():
            print("Error: edge.exe is currently running.", file=sys.stderr)
            return 1
        assert bak_path.exists(), f"Missing {bak_path}"
        shutil.copy2(bak_path, exe_path)
        if installed_dll.exists():
            installed_dll.unlink()
            print(f"Removed {installed_dll}")
        print(f"Restored {exe_path} (sha256={sha256_file(exe_path)})")
        return 0

    assert bak_path.exists(), f"Missing {bak_path}"
    assert sha256_file(bak_path) == EXPECTED_BAK_SHA256, "edge.exe.bak SHA-256 mismatch"
    assert bak_path.stat().st_size == EXPECTED_BAK_SIZE

    compile_dll_and_verifier()
    verify_against_bak_and_staged(bak_path, staged_exe)
    RELEASE_DLL.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(DLL_OUT, RELEASE_DLL)
    print(f"Updated release {RELEASE_DLL}")

    if args.apply:
        if is_edge_running():
            print("Error: edge.exe is currently running. Close EDGE before --apply.", file=sys.stderr)
            return 1
        if sha256_file(exe_path) != EXPECTED_BAK_SHA256:
            shutil.copy2(bak_path, exe_path)
            print(f"Restored clean vanilla {exe_path} (sha256={sha256_file(exe_path)})")
        shutil.copy2(DLL_OUT, installed_dll)
        print(f"Installed {installed_dll} ({installed_dll.stat().st_size} bytes, sha256={sha256_file(installed_dll)})")
    else:
        print(f"Staged {DLL_OUT} ({DLL_OUT.stat().st_size} bytes, sha256={sha256_file(DLL_OUT)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
