#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Explicit environment/qualified-extension import check; no solver run/install.

Imports may initialize library caches; use caller-owned MPLCONFIGDIR/XDG_CACHE_HOME.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib
import importlib.metadata
import importlib.util
import json
from pathlib import Path
import platform
import re
import sys

HERE = Path(__file__).resolve().parent
PROFILE = HERE / "runtime_profile.json"
LOCK = HERE / "requirements-linux-cp311.lock"


def locked_packages(text: str) -> dict[str, str]:
    result = {}
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        match = re.fullmatch(r"([A-Za-z0-9_.-]+)==([^\s]+) --hash=sha256:([a-f0-9]{64})", line)
        if not match:
            raise ValueError("lock requires exact versions and one wheel SHA-256 per package")
        name, version, _ = match.groups()
        key = re.sub(r"[-_.]+", "-", name).lower()
        if key in result:
            raise ValueError("duplicate package in lock")
        result[key] = version
    if not result:
        raise ValueError("empty dependency lock")
    return result


def check_environment(packages: dict[str, str]) -> None:
    if (platform.python_implementation() != "CPython" or sys.version_info[:2] != (3, 11)
            or platform.system() != "Linux" or platform.machine() != "x86_64"):
        raise ValueError("qualified environment requires Linux x86_64 / CPython 3.11")
    libc, version = platform.libc_ver()
    if libc != "glibc" or tuple(map(int, version.split(".")[:2])) < (2, 28):
        raise ValueError("locked wheels require glibc >= 2.28")
    for name, expected in packages.items():
        if importlib.metadata.version(name) != expected:
            raise ValueError(f"installed version differs from lock: {name} (expected {expected})")


def check(extension: Path) -> dict:
    profile_raw, lock_raw = PROFILE.read_bytes(), LOCK.read_bytes()
    profile = json.loads(profile_raw)
    if profile.get("schema") != "cubey.fluid25d.synxflow-runtime-profile.v1":
        raise ValueError("unexpected runtime profile schema")
    # Do not resolve an input symlink before rejecting it. No hash override flag.
    if extension.is_symlink() or not extension.is_file():
        raise ValueError("extension must be an existing regular non-symlink file")
    actual_sha = hashlib.sha256(extension.read_bytes()).hexdigest()
    if actual_sha != profile["qualified_service"]["extension_sha256"]:
        raise ValueError("unqualified extension hash; build success is not runtime admission")
    packages = locked_packages(lock_raw.decode())
    check_environment(packages)
    importlib.import_module("synxflow.IO.InputModel")
    spec = importlib.util.spec_from_file_location("flood", extension.resolve())
    if spec is None or spec.loader is None:
        raise ValueError("cannot load qualified extension")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not callable(getattr(module, "run_service", None)):
        raise ValueError("extension lacks Cubey service hooks; stock wheel is not sufficient")
    return {
        "schema": "cubey.fluid25d.synxflow-runtime-check.v1", "passed": True,
        "python": sys.executable, "python_version": platform.python_version(),
        "package_count": len(packages), "extension": str(extension.absolute()),
        "extension_sha256": actual_sha,
        "profile_sha256": hashlib.sha256(profile_raw).hexdigest(),
        "lock_sha256": hashlib.sha256(lock_raw).hexdigest(),
        "io_import": True, "service_import": True, "solver_executed": False,
        "scope": "Import/identity only; not CUDA execution, numerical parity or lifecycle qualification."
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--extension", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = check(args.extension)
    except (OSError, ValueError, KeyError, ImportError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
