#!/usr/bin/env python3
"""Capture a bounded native negative-sticks audition with reproducible receipts."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import subprocess
import time


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/dev/projects/sdf/sdf"))
    parser.add_argument("--output", type=Path, default=Path("outputs/sdf/negative-audition"))
    parser.add_argument("--atlas-axis", type=int, choices=(64, 80, 92), default=64)
    parser.add_argument("--sun-elevation", type=float, default=35)
    parser.add_argument("--sun-azimuth", type=float, default=160)
    parser.add_argument("--details", action="store_true", help="also capture stages/opposite views of two candidates")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    binary = args.binary.resolve()
    output = args.output.resolve()
    for folder in ("captures", "logs", "configs", "recipes"):
        (output / folder).mkdir(parents=True, exist_ok=True)
    manifest = output / "manifest.json"
    if manifest.exists():
        raise RuntimeError("Use a fresh output directory to preserve the previous audition")
    receipt = {
        "captured_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()),
        "binary_sha256": sha256(binary),
        "shader_sha256": {p.name: sha256(p) for p in sorted((binary.parent / "shaders").glob("*.spv"))},
        "runs": {},
    }
    jobs = [(f"seed-{seed}-cuts-{count}", seed, count, 0.7)
            for seed in (7, 42, 91) for count in (24, 48, 72)]
    if args.details:
        for seed, final_count in ((42, 72), (91, 48)):
            jobs.extend((f"detail-{seed}-cuts-{count}", seed, count, 0.7) for count in (0, 4, 16))
            jobs.append((f"opposite-{seed}-cuts-{final_count}", seed, final_count, 0.7 + 3.141592654))
    for name, seed, count, yaw in jobs:
        capture = output / "captures" / f"{name}.png"
        config = output / "configs" / f"{name}.json"
        recipe = output / "recipes" / f"{name}.json"
        command = [str(binary), "--headless", "--require-validation", "--scene", "negative-sticks",
                   "--seed", str(seed), "--count", str(count), "--atlas-axis", str(args.atlas_axis),
                   "--width", "1280", "--height", "720", "--supersample", "1",
                   "--yaw", str(yaw), "--pitch", "0.4", "--distance", "1.6", "--target-y", "0",
                   "--shadows", "1", "--no-ground", "--no-ao",
                   "--sun-elevation", str(args.sun_elevation),
                   "--sun-azimuth", str(args.sun_azimuth), "--exposure", "0.7", "--roughness", "0.36",
                   "--export-recipe", str(recipe), "--output", str(capture)]
        subprocess.run([*command, "--write-config-template", str(config)], cwd=root, check=True,
                       stdout=subprocess.DEVNULL)
        start = time.monotonic()
        result = subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        log = output / "logs" / f"{name}.log"
        log.write_text(result.stdout)
        overflow = result.returncode != 0 and "SDF field capacity exceeded" in result.stdout
        if "validation error" in result.stdout.lower() or (result.returncode and not overflow):
            raise RuntimeError(f"{name} failed; see {log}")
        receipt["runs"][name] = {
            "seed": seed, "cutters": count, "yaw": yaw,
            "command": command, "exit_code": result.returncode, "capacity_failure": overflow,
            "wall_seconds": time.monotonic() - start,
            "config": str(config.relative_to(output)), "recipe": str(recipe.relative_to(output)),
            "log": str(log.relative_to(output)),
            "capture": str(capture.relative_to(output)) if not overflow else None,
            "capture_sha256": sha256(capture) if not overflow else None,
            "field_stats": [line for line in result.stdout.splitlines() if line.startswith("sdf:")],
        }
        manifest.write_text(json.dumps(receipt, indent=2) + "\n")
        print(f"{name}: {'capacity failure' if overflow else 'captured'}", flush=True)
    print(f"Receipts: {manifest}")


if __name__ == "__main__":
    main()
