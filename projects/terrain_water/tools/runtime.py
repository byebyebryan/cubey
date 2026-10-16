"""Maintained Terrain Water host utilities; no study, NumPy or CUDA import."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
OUTPUT_ROOT = ROOT / "outputs/fluid"
LIVE_PYTHON = OUTPUT_ROOT / "native-runoff-reuse-v1-20261002-trm3Ve/synxflow-setup/env/.venv/bin/python"
# Retain the qualified private binary's original location/RPATH; callers may
# select the same hash through --extension. Source helpers live with this project.
EXTENSION = ROOT / "projects/fluid/fluid_25d/external_synxflow/private-builds/service-cuda117-gcc114-zero-dt-20261004/build/synxflow/apps/cudaFloodSolversPybind/flood.cpython-311-x86_64-linux-gnu.so"
EXTENSION_SHA = "251cea1bd0658acdfc5b5129038d93970bf1769d7cac00bbf1edff99a886315f"
CASE = OUTPUT_ROOT / "native-rain-recession-v1-20261003-1ZMqTJ/cases/mountain-rain-on-14400s"
CASE_INPUT_SHA = "de6da45d40600460e90abc2b153cec859a0ac45cd5b2b4a7b5b7595f6d3ec05b"

def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n").encode()


def write_json_exclusive(path: Path, value: object) -> None:
    """Write a new manifest and refuse to replace any existing evidence."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(json_bytes(value))


def input_tree(directory: Path) -> str:
    if not directory.is_dir() or directory.is_symlink():
        raise ValueError(f"missing/non-directory/symlink native input: {directory}")
    rows = {}
    for p in sorted(directory.rglob("*")):
        if p.is_symlink():
            raise ValueError("native input may not contain symlinks")
        if p.is_file():
            rows[str(p.relative_to(directory))] = sha256_file(p)
    return hashlib.sha256(json.dumps(rows, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def fresh_output(path: Path) -> Path:
    absolute = path.absolute()
    root = OUTPUT_ROOT.resolve()
    if root not in absolute.parents or absolute == root:
        raise ValueError("output must be a fresh leaf under outputs/fluid")
    if any(p.is_symlink() for p in (absolute, *absolute.parents)):
        raise ValueError("launcher output may not traverse symlinks")
    resolved = absolute.resolve()
    if root not in resolved.parents or resolved.exists() or not resolved.parent.is_dir():
        raise ValueError("output must be a fresh leaf below an existing parent")
    return resolved


def active_owned_group(group_id: int) -> list[int]:
    """Linux: exclude zombies, require both the owned process group and session."""
    active = []
    for p in Path("/proc").iterdir():
        if not p.name.isdecimal():
            continue
        try:
            stat = (p / "stat").read_text().rsplit(")", 1)[1].split()
            if stat[0] != "Z" and int(stat[2]) == group_id and int(stat[3]) == group_id:
                active.append(int(p.name))
        except (OSError, ValueError, IndexError):
            continue
    return active


def stop_owned(process: subprocess.Popen, grace_s: float = 15) -> dict:
    """Signal only the new session/group created for this child, then reap it.

    rtk proxy can keep a wrapper parent; group targeting includes that wrapper's
    worker/viewer and never relies on machine-wide executable-name matching.
    """
    active = active_owned_group(process.pid)
    sent = bool(active)
    if active:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    escalated = False
    deadline = time.monotonic() + grace_s
    while active_owned_group(process.pid) and time.monotonic() < deadline:
        process.poll()
        time.sleep(.05)
    if active_owned_group(process.pid):
        os.killpg(process.pid, signal.SIGKILL)
        escalated = True
    process.wait(timeout=5)
    deadline = time.monotonic() + 5
    while active_owned_group(process.pid) and time.monotonic() < deadline:
        time.sleep(.05)
    if active_owned_group(process.pid):
        raise RuntimeError("owned process group did not stop")
    return {"pid": process.pid, "exit_code": process.returncode, "signal": "SIGTERM" if sent else None,
            "escalated": escalated, "active_group_remaining": []}
