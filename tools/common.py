import hashlib
import os
import pathlib
import re

EXPECTED_EXE_SIZE = 2714624
EXPECTED_EXE_SHA256 = "b73a0df8e07d47f3d3e4b57642514033c00449c0070876513323c257c3cabe9b"


def to_host_path(win_path: str) -> str:
    """Convert a Windows path to a WSL /mnt/<drive> path when running inside POSIX/WSL."""
    if os.name == "nt":
        return win_path
    m = re.match(r"^([A-Za-z]):[\\/](.*)$", win_path)
    if m:
        drive, rest = m.group(1).lower(), m.group(2).replace("\\", "/")
        return f"/mnt/{drive}/{rest}"
    return win_path.replace("\\", "/")


def to_wsl_path(path: str) -> str:
    """Convert a Windows or POSIX path into a WSL-accessible path."""
    abs_p = os.path.abspath(path)
    m = re.match(r"^([A-Za-z]):[\\/](.*)$", abs_p)
    if m:
        drive, rest = m.group(1).lower(), m.group(2).replace("\\", "/")
        return f"/mnt/{drive}/{rest}"
    return abs_p.replace("\\", "/")


def sha256_file(path: os.PathLike | str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def is_clean_edge_exe(path: os.PathLike | str) -> bool:
    p = pathlib.Path(path)
    return p.is_file() and p.stat().st_size == EXPECTED_EXE_SIZE and sha256_file(p) == EXPECTED_EXE_SHA256


def find_clean_edge_exe(game_dir: os.PathLike | str) -> pathlib.Path | None:
    """Locate an unmodified EDGE executable in `game_dir` (`edge.exe` first, optional `edge.exe.bak` fallback)."""
    gd = pathlib.Path(game_dir)
    for name in ("edge.exe", "edge.exe.bak"):
        cand = gd / name
        if is_clean_edge_exe(cand):
            return cand
    return None


def resolve_default_game_dir() -> str:
    """Resolve the EDGE installation folder from EDGE_GAME_DIR or common Steam library paths."""
    env_dir = os.environ.get("EDGE_GAME_DIR", "").strip()
    if env_dir:
        return env_dir
    candidates = [
        r"C:\Program Files (x86)\Steam\steamapps\common\EDGE",
        r"D:\Games\Launchers\Steam\steamapps\common\EDGE",
        r"D:\SteamLibrary\steamapps\common\EDGE",
        r"E:\SteamLibrary\steamapps\common\EDGE",
    ]
    for candidate in candidates:
        host_dir = to_host_path(candidate)
        if os.path.isfile(os.path.join(host_dir, "edge.exe")) or os.path.isfile(
            os.path.join(host_dir, "edge.exe.bak")
        ):
            return candidate
    return candidates[0]


DEFAULT_GAME_DIR = resolve_default_game_dir()
