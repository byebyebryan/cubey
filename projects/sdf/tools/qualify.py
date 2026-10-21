#!/usr/bin/env python3
"""Capture the fixed SDF review matrix and bounded, render-only GPU profiles."""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import time


def distribution(values):
    ordered = sorted(values)
    if not ordered:
        raise RuntimeError("missing completed GPU timestamp samples")
    return {"samples": len(ordered), "median_ms": statistics.median(ordered),
            "p95_ms": ordered[math.ceil(len(ordered) * .95) - 1], "max_ms": max(ordered)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/dev/projects/sdf/sdf"))
    parser.add_argument("--output", type=Path, default=Path("outputs/sdf/qualification"))
    parser.add_argument("--video", action="store_true")
    parser.add_argument("--windowed", action="store_true", help="exercise input/resize in an isolated Xvfb")
    parser.add_argument("--skip-captures", action="store_true")
    parser.add_argument("--skip-profiles", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    binary = args.binary.resolve()
    output = args.output.resolve()
    for folder in ("captures", "logs", "profiles", "configs"):
        (output / folder).mkdir(parents=True, exist_ok=True)
    receipt_path = output / "manifest.json"
    receipt = json.loads(receipt_path.read_text()) if receipt_path.exists() else {"runs": {}}
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    shader_hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in sorted((binary.parent / "shaders").glob("*.spv"))}
    if receipt.get("binary_sha256", binary_hash) != binary_hash or \
            receipt.get("shader_sha256", shader_hashes) != shader_hashes:
        raise RuntimeError("runtime changed: use a fresh --output directory to preserve earlier receipts")
    receipt.update({"captured_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
                    "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
                    "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()),
                    "binary_sha256": binary_hash, "shader_sha256": shader_hashes,
                    "source_sha256": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in sorted((root / "projects/sdf").rglob("*")) if p.is_file()}})
    runtime = {"binary_sha256": binary_hash, "shader_sha256": shader_hashes}
    try:
        inventory = subprocess.check_output(["nvidia-smi", "--query-gpu=name,driver_version",
            "--format=csv,noheader"], text=True).splitlines()[0].rsplit(",", 1)
        runtime.update(device=inventory[0].strip(), driver=inventory[1].strip())
    except (OSError, subprocess.CalledProcessError):
        runtime.update(device="See native device receipt", driver="not queried")
    (output / "runtime-artifacts.json").write_text(json.dumps(runtime, indent=2) + "\n")

    def save():
        receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")

    def run(name, options, *, failure=False, capture=None):
        command = [str(binary), "--headless", "--require-validation", *map(str, options)]
        config = output / "configs" / f"{name}.json"
        subprocess.run([*command, "--write-config-template", str(config)], cwd=root, check=True,
                       stdout=subprocess.DEVNULL)
        if failure and capture:
            capture.unlink(missing_ok=True)
        start = time.monotonic()
        result = subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log = output / "logs" / f"{name}.log"
        log.write_text(result.stdout)
        if "vulkan validation error" in result.stdout.lower() or "Validation Error" in result.stdout:
            raise RuntimeError(f"{name}: Vulkan validation failed; see {log}")
        if failure:
            if result.returncode == 0 or "SDF field capacity exceeded" not in result.stdout or capture.exists():
                raise RuntimeError(f"{name}: expected a bounded capacity failure, see {log}")
        elif result.returncode != 0:
            raise RuntimeError(f"{name}: exit {result.returncode}, see {log}")
        row = {"command": command, "exit_code": result.returncode, "expected_failure": failure,
               "wall_seconds": time.monotonic() - start, "log": str(log.relative_to(output)),
               "config": str(config.relative_to(output))}
        if capture and not failure:
            row["capture"] = str(capture.relative_to(output))
            row["capture_sha256"] = hashlib.sha256(capture.read_bytes()).hexdigest()
        receipt["runs"][name] = row
        save()
        print(f"{name}: {'expected failure' if failure else 'passed'} ({row['wall_seconds']:.2f}s)", flush=True)
        return row

    unit = binary.with_name("cubey_project_sdf_recipe_tests")
    if not unit.exists():
        raise RuntimeError("build the cubey_project_sdf_recipe_tests target before qualification")
    with (output / "logs/cpu-tests.log").open("w") as log:
        log.write(f"Target: {unit}\n"); log.flush()
        subprocess.run([str(unit)], cwd=root, check=True, stdout=log, stderr=subprocess.STDOUT)
        log.write("Exit code: 0. CPU geometry, recipe and timeline checks passed.\n")

    if not args.skip_captures:
        for scene in ("box", "rotated", "cut"):
            capture = output / "captures" / f"{scene}-normals.png"
            run(f"{scene}-normals", ["--scene", scene, "--debug", 1, "--no-ground", "--validate-field",
                "--width", 640, "--height", 480, "--output", capture], capture=capture)
        for name, yaw in (("depth-front", 0), ("depth-back", math.pi)):
            capture = output / "captures" / f"{name}.png"
            run(name, ["--scene", "box", "--occluder", "--no-ground", "--yaw", yaw, "--pitch", 0,
                "--width", 640, "--height", 480, "--output", capture], capture=capture)
        capture = output / "captures" / "batch-matrix.png"
        run("batch-matrix", ["--scene", "box", "--validate-batches", "--width", 320, "--height", 240,
            "--output", capture], capture=capture)
        for name, count, axis in (("capacity-64", 2048, 64), ("capacity-92", 6400, 92)):
            capture = output / "captures" / f"{name}.png"
            run(name, ["--count", count, "--atlas-axis", axis, "--width", 320, "--height", 240,
                "--output", capture], failure=True, capture=capture)
        preset = root / "projects/sdf/presets/stick-ball.json"
        for name, extra in (("hero", ["--validate-field", "--export-recipe", output / "recipe.json"]),
                            ("baseline", ["--shadows", 0]), ("hard", ["--shadows", 1]),
                            ("ao", ["--ao"]), ("half", ["--motion", "construction", "--time", 4]),
                            ("steps", ["--debug", 3]), ("bricks", ["--debug", 4]),
                            ("seed-43", ["--seed", 43, "--count", 512, "--atlas-axis", 64])):
            capture = output / "captures" / f"{name}.png"
            run(name, ["--config", preset, "--width", 1280, "--height", 720, *extra,
                "--output", capture], capture=capture)
    if not args.skip_profiles:
        for name, shadows, ao in (("primary", 0, False), ("hard", 1, False),
                                   ("soft", 2, False), ("soft-ao", 2, True)):
            prefix = output / "profiles" / name
            row = run(f"profile-{name}", ["--headless-render-only", "--frames", 180,
                "--profile-warmup-frames", 30, "--profile-output", prefix,
                "--width", 1920, "--height", 1080, "--count", 2048, "--atlas-axis", 92,
                "--supersample", 1, "--shadows", shadows, *( ["--ao"] if ao else [])])
            frames = {}
            stages = {}
            with Path(f"{prefix}.passes.csv").open() as stream:
                for item in csv.DictReader(stream):
                    if item["kind"] == "gpu":
                        value = float(item["duration_ms"])
                        stages.setdefault(item["label"], []).append(value)
                        index = int(item["frame_index"])
                        frames[index] = frames.get(index, 0.0) + value
            row["gpu_stages"] = {key: distribution(values) for key, values in stages.items()}
            row["gpu_frame"] = distribution(frames.values())
            row["summary"] = str(Path(f"{prefix}.summary.txt").relative_to(output))
            save()
    if args.video:
        capture = output / "presentation.mp4"
        run("presentation", ["--config", root / "projects/sdf/presets/stick-ball.json",
            "--motion", "presentation", "--capture", "video", "--fps", 30, "--frames", 720,
            "--width", 1280, "--height", 720, "--output", capture], capture=capture)
    if args.windowed:
        subprocess.run(["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", "env",
            "CUBEY_SDF_PRIVATE_XVFB=1", "python3", str(Path(__file__).with_name("check_window.py")),
            "--binary", str(binary), "--output", str(output)], cwd=root, check=True)
    print(f"Receipts: {receipt_path}", flush=True)


if __name__ == "__main__":
    main()
