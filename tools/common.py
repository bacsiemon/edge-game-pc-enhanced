import os
import re

EXPECTED_BAK_SIZE = 2714624
EXPECTED_BAK_SHA256 = "b73a0df8e07d47f3d3e4b57642514033c00449c0070876513323c257c3cabe9b"


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
        if os.path.isfile(os.path.join(host_dir, "edge.exe.bak")) or os.path.isfile(
            os.path.join(host_dir, "edge.exe")
        ):
            return candidate
    return candidates[0]


DEFAULT_GAME_DIR = resolve_default_game_dir()
