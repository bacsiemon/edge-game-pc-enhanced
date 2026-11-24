import hashlib
import json
import os
import re
import shlex
import shutil
import sqlite3
import struct
import subprocess
import tempfile

EXPECTED_BAK_SIZE = 2714624
EXPECTED_BAK_SHA256 = "b73a0df8e07d47f3d3e4b57642514033c00449c0070876513323c257c3cabe9b"
_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_ASM_CACHE_PATH = os.path.join(_REPO_ROOT, ".cache", "asm_cache.sqlite")
_ASM_CACHE_CONN: sqlite3.Connection | None = None


def _get_asm_cache_conn() -> sqlite3.Connection:
    global _ASM_CACHE_CONN
    if _ASM_CACHE_CONN is None:
        os.makedirs(os.path.dirname(_ASM_CACHE_PATH), exist_ok=True)
        conn = sqlite3.connect(_ASM_CACHE_PATH)
        conn.execute(
            "CREATE TABLE IF NOT EXISTS asm_cache "
            "(key TEXT PRIMARY KEY, code BLOB NOT NULL, syms_json TEXT NOT NULL)"
        )
        _ASM_CACHE_CONN = conn
    return _ASM_CACHE_CONN


def to_host_path(win_path: str) -> str:
    """Convert a Windows path to a WSL /mnt/<drive> path when running inside POSIX/WSL."""
    if os.name == "nt":
        return win_path
    m = re.match(r"^([A-Za-z]):[\\/](.*)$", win_path)
    if m:
        drive, rest = m.group(1).lower(), m.group(2).replace("\\", "/")
        return f"/mnt/{drive}/{rest}"
    return win_path.replace("\\", "/")


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


def to_wsl_path(path: str) -> str:
    """Convert a Windows or POSIX path into a WSL-accessible path."""
    abs_p = os.path.abspath(path)
    m = re.match(r"^([A-Za-z]):[\\/](.*)$", abs_p)
    if m:
        drive, rest = m.group(1).lower(), m.group(2).replace("\\", "/")
        return f"/mnt/{drive}/{rest}"
    return abs_p.replace("\\", "/")


def _has_local_binutils() -> bool:
    return bool(shutil.which("as") and shutil.which("ld") and shutil.which("nm") and shutil.which("objcopy"))


def _run_tool(cmd):
    if _has_local_binutils():
        return subprocess.check_output(cmd, text=True)
    if os.name == "nt" and shutil.which("wsl"):
        return subprocess.check_output(["wsl", *cmd], text=True)
    raise RuntimeError(
        "Missing GNU 32-bit binutils (as, ld, nm, objcopy) in PATH and WSL is unavailable."
    )


def va2off(va: int) -> int:
    """Map an x86 PE virtual address in edge.exe (1.x Steam build) to its file offset."""
    if 0x401000 <= va < 0x576600:
        return va - 0x401000 + 0x400
    if 0x577000 <= va < 0x5e2a00:
        return va - 0x577000 + 0x175a00
    if 0x5e3000 <= va < 0x5f8800:
        return va - 0x5e3000 + 0x1e1400
    raise ValueError(f"Unexpected VA {hex(va)}")


def put_bytes(buf: bytearray, va: int, data: bytes, budget: int | None = None) -> None:
    if budget is not None:
        assert len(data) <= budget, f"Overflow at {hex(va)}: {len(data)} > {budget}"
    off = va2off(va)
    buf[off : off + len(data)] = data


def put_u32(buf: bytearray, va: int, val: int) -> None:
    put_bytes(buf, va, struct.pack("<I", val))


def put_rel32(buf: bytearray, instr_va: int, target_va: int, opcode: int) -> None:
    rel = target_va - (instr_va + 5)
    put_bytes(buf, instr_va, bytes([opcode]) + struct.pack("<i", rel))


def expect_replace(buf: bytearray, va: int, expected: bytes, new_bytes: bytes) -> None:
    off = va2off(va)
    actual = bytes(buf[off : off + len(expected)])
    assert actual == expected, f"Mismatch at {hex(va)}: expected {expected.hex()}, got {actual.hex()}"
    assert len(expected) == len(new_bytes), f"Length mismatch at {hex(va)}"
    buf[off : off + len(new_bytes)] = new_bytes


def assemble_at(va: int, lines: list[str], budget: int | None = None) -> bytes:
    """Assemble Intel-syntax 32-bit x86 instructions anchored at virtual address `va`."""
    src = ".intel_syntax noprefix\n.text\n" + "\n".join(lines) + "\n"
    cache_key = hashlib.sha256(f"asm:{hex(va)}\n{src}".encode("utf-8")).hexdigest()
    conn = _get_asm_cache_conn()
    row = conn.execute("SELECT code FROM asm_cache WHERE key = ?", (cache_key,)).fetchone()
    if row is not None:
        code = bytes(row[0])
    else:
        use_wsl = (not _has_local_binutils()) and os.name == "nt"
        with tempfile.TemporaryDirectory() as td:
            s_path = os.path.join(td, "s.s")
            o_path = os.path.join(td, "s.o")
            b_path = os.path.join(td, "s.bin")
            with open(s_path, "w", newline="\n") as f:
                f.write(src)
            if use_wsl:
                s_arg = shlex.quote(to_wsl_path(s_path))
                o_arg = shlex.quote(to_wsl_path(o_path))
                b_arg = shlex.quote(to_wsl_path(b_path))
                sh_cmd = (
                    f"as --32 {s_arg} -o {o_arg} && "
                    f"ld -m elf_i386 -e 0 -Ttext {hex(va)} --oformat=binary {o_arg} -o {b_arg}"
                )
                subprocess.check_call(["wsl", "sh", "-c", sh_cmd])
            else:
                _run_tool(["as", "--32", s_path, "-o", o_path])
                _run_tool(["ld", "-m", "elf_i386", "-e", "0", "-Ttext", hex(va), "--oformat=binary", o_path, "-o", b_path])
            with open(b_path, "rb") as f:
                code = f.read()
        conn.execute(
            "INSERT OR REPLACE INTO asm_cache (key, code, syms_json) VALUES (?, ?, ?)",
            (cache_key, code, "{}"),
        )
        conn.commit()
    if budget is not None:
        assert len(code) <= budget, f"Budget exceeded at {hex(va)}: {len(code)} > {budget}"
    return code


def link_block_with_symbols(
    base_va: int,
    asm_lines: list[str],
    budget: int,
    ext_syms: dict[str, int] | None = None,
) -> tuple[bytes, dict[str, int]]:
    """Assemble and link a multi-function block at `base_va`, returning binary bytes and exported symbols."""
    defs = [f".equ {k}, {hex(v)}" for k, v in ext_syms.items()] if ext_syms else []
    src = ".intel_syntax noprefix\n.text\n" + "\n".join(defs + asm_lines) + "\n"
    cache_key = hashlib.sha256(f"link:{hex(base_va)}\n{src}".encode("utf-8")).hexdigest()
    conn = _get_asm_cache_conn()
    row = conn.execute("SELECT code, syms_json FROM asm_cache WHERE key = ?", (cache_key,)).fetchone()
    if row is not None:
        code = bytes(row[0])
        syms: dict[str, int] = json.loads(row[1])
    else:
        use_wsl = (not _has_local_binutils()) and os.name == "nt"
        with tempfile.TemporaryDirectory() as td:
            s_path = os.path.join(td, "b.s")
            o_path = os.path.join(td, "b.o")
            e_path = os.path.join(td, "b.elf")
            b_path = os.path.join(td, "b.bin")
            with open(s_path, "w", newline="\n") as f:
                f.write(src)
            if use_wsl:
                s_arg = shlex.quote(to_wsl_path(s_path))
                o_arg = shlex.quote(to_wsl_path(o_path))
                e_arg = shlex.quote(to_wsl_path(e_path))
                b_arg = shlex.quote(to_wsl_path(b_path))
                sh_cmd = (
                    f"as --32 {s_arg} -o {o_arg} && "
                    f"ld -m elf_i386 -e 0 -Ttext {hex(base_va)} {o_arg} -o {e_arg} && "
                    f"objcopy -j .text -O binary {e_arg} {b_arg} && "
                    f"nm -n {e_arg}"
                )
                nm_out = subprocess.check_output(["wsl", "sh", "-c", sh_cmd], text=True)
            else:
                _run_tool(["as", "--32", s_path, "-o", o_path])
                _run_tool(["ld", "-m", "elf_i386", "-e", "0", "-Ttext", hex(base_va), o_arg, "-o", e_arg])
                nm_out = _run_tool(["nm", "-n", e_arg])
                _run_tool(["objcopy", "-j", ".text", "-O", "binary", e_arg, b_path])
            syms = {}
            for ln in nm_out.splitlines():
                m = re.match(r"^([0-9a-fA-F]+)\s+[Tt]\s+(\S+)$", ln.strip())
                if m:
                    syms[m.group(2)] = int(m.group(1), 16)
            with open(b_path, "rb") as f:
                code = f.read()
        conn.execute(
            "INSERT OR REPLACE INTO asm_cache (key, code, syms_json) VALUES (?, ?, ?)",
            (cache_key, code, json.dumps(syms)),
        )
        conn.commit()
    assert len(code) <= budget, f"Block at {hex(base_va)} exceeded budget: {len(code)} > {budget}"
    return code, syms
