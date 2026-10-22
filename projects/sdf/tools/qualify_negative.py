#!/usr/bin/env python3
"""Qualify the negative-sticks scene without replacing the additive baseline."""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
from pathlib import Path
import re
import subprocess
import time

from negative_sticks import sha256
from qualify import distribution


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/dev/projects/sdf/sdf"))
    parser.add_argument("--output", type=Path, default=Path("outputs/sdf/negative-qualification"))
    parser.add_argument("--video", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    binary = args.binary.resolve()
    output = args.output.resolve()
    if (output / "manifest.json").exists():
        raise RuntimeError("Use a fresh output directory to preserve previous qualification")
    for folder in ("captures", "logs", "configs", "profiles"):
        (output / folder).mkdir(parents=True, exist_ok=True)
    receipt = {
        "captured_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()),
        "binary_sha256": sha256(binary),
        "shader_sha256": {p.name: sha256(p) for p in sorted((binary.parent / "shaders").glob("*.spv"))},
        "source_sha256": {str(p.relative_to(root)): sha256(p)
                          for p in sorted((root / "projects/sdf").rglob("*"))
                          if p.is_file() and p.suffix in (".cpp", ".h", ".glsl", ".comp", ".vert", ".frag")},
        "device": subprocess.check_output(["nvidia-smi", "--query-gpu=name,driver_version",
                                            "--format=csv,noheader"], text=True).strip(),
        "runs": {}, "checks": {},
    }

    def save():
        (output / "manifest.json").write_text(json.dumps(receipt, indent=2) + "\n")

    def run(name, options, *, preset=True, capture=True, expected_error=None):
        image = output / "captures" / f"{name}.png"
        if expected_error:
            image.unlink(missing_ok=True)
        command = [str(binary), "--headless", "--require-validation"]
        if preset:
            command += ["--config", str(root / "projects/sdf/presets/negative-sticks.json")]
        command += ["--width", "1280", "--height", "720", *map(str, options)]
        if capture:
            command += ["--output", str(image)]
        config = output / "configs" / f"{name}.json"
        subprocess.run([*command, "--write-config-template", str(config)], cwd=root, check=True,
                       stdout=subprocess.DEVNULL)
        start = time.monotonic()
        result = subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        log = output / "logs" / f"{name}.log"
        log.write_text(result.stdout)
        if "validation error" in result.stdout.lower():
            raise RuntimeError(f"{name}: Vulkan validation failed; see {log}")
        if expected_error:
            if result.returncode == 0 or expected_error not in result.stdout or image.exists():
                raise RuntimeError(f"{name}: expected bounded rejection; see {log}")
        elif result.returncode or (capture and not image.exists()):
            raise RuntimeError(f"{name}: native capture failed; see {log}")
        row = {
            "command": command, "config": str(config.relative_to(output)),
            "log": str(log.relative_to(output)), "exit_code": result.returncode,
            "expected_error": expected_error, "wall_seconds": time.monotonic() - start,
            "field_stats": [s for s in result.stdout.splitlines() if s.startswith("sdf:")],
        }
        if capture and not expected_error:
            row.update(capture=str(image.relative_to(output)), capture_sha256=sha256(image))
        receipt["runs"][name] = row
        save()
        print(f"{name}: {'expected rejection' if expected_error else 'passed'}", flush=True)
        return row

    unit = binary.with_name("cubey_project_sdf_recipe_tests")
    with (output / "logs/cpu-tests.log").open("w") as log:
        subprocess.run([str(unit)], cwd=root, check=True, stdout=log, stderr=subprocess.STDOUT)
    receipt["checks"]["cpu_recipe_timeline"] = "passed"
    run("subtraction-matrix", ["--scene", "cut", "--validate-subtractions", "--validate-field",
                               "--width", 320, "--height", 240], preset=False)
    run("additive-batch-regression", ["--scene", "box", "--validate-batches", "--width", 320,
                                      "--height", 240], preset=False)
    for scene in ("box", "rotated", "cut"):
        run(f"fixture-{scene}", ["--scene", scene, "--debug", 1, "--validate-field", "--width", 640,
                                 "--height", 480], preset=False)
    run("additive-sticks", ["--config", root / "projects/sdf/presets/stick-ball.json",
                            "--motion", "static", "--validate-field"], preset=False)
    run("hero", ["--motion", "static", "--validate-field", "--export-recipe", output / "recipe.json"])
    for name, options in (
        ("base", ["--time", 0, "--validate-field"]),
        ("four", ["--time", 8, "--validate-field"]),
        ("sixteen", ["--motion", "static", "--count", 16, "--validate-field"]),
        ("middle", ["--time", 12, "--validate-field"]),
        ("opposite", ["--time", 24]),
        ("normals", ["--motion", "static", "--debug", 1]),
        ("steps", ["--motion", "static", "--debug", 3]),
        ("cue", ["--time", 2.3, "--fps", 30]),
        ("cue-off", ["--time", 2.3, "--no-cutter-cue"]),
        ("cue-60fps", ["--time", 2.3, "--fps", 60]),
        ("replay-middle", ["--recipe", output / "recipe.json", "--scene", "sticks", "--time", 12,
                           "--validate-field"]),
        ("zero-cuts", ["--count", 0, "--time", 24, "--validate-field"]),
        ("one-cut", ["--count", 1, "--time", 24, "--validate-field"]),
    ):
        run(name, options)
    for left, right in (("middle", "replay-middle"), ("cue-off", "base"), ("cue", "cue-60fps")):
        if receipt["runs"][left]["capture_sha256"] != receipt["runs"][right]["capture_sha256"]:
            raise RuntimeError(f"Native pixels differ: {left} / {right}")
    if receipt["runs"]["cue"]["capture_sha256"] == receipt["runs"]["cue-off"]["capture_sha256"]:
        raise RuntimeError("Cutter guide did not change the preview image")
    receipt["checks"]["native_replay_cue_and_logical_time"] = "passed"
    run("capacity-64", ["--motion", "static", "--atlas-axis", 64, "--supersample", 1],
        expected_error="SDF field capacity exceeded")
    run("incompatible-carve", ["--scene", "sticks", "--count", 4],
        expected_error="Carve motion requires one initial fill")

    for name, shadows, motion, frames in (("primary", 0, "static", 180),
                                          ("hard", 1, "static", 180),
                                          ("carving", 1, "carve", 720)):
        prefix = output / "profiles" / name
        row = run(f"profile-{name}", ["--motion", motion, "--headless-render-only", "--frames", frames,
                                      "--fps", 30, "--supersample", 1, "--no-cutter-cue",
                                      "--width", 1920, "--height", 1080, "--shadows", shadows,
                                      "--profile-warmup-frames", 30, "--profile-output", prefix], capture=False)
        gpu_frames, stages, prepare = {}, {}, []
        with Path(f"{prefix}.passes.csv").open() as stream:
            for item in csv.DictReader(stream):
                value = float(item["duration_ms"])
                if item["kind"] == "gpu":
                    stages.setdefault(item["label"], []).append(value)
                    index = int(item["frame_index"])
                    gpu_frames[index] = gpu_frames.get(index, 0) + value
                elif item["label"] == "sdf.prepare_geometry":
                    prepare.append(value)
        row["gpu_frame"] = distribution(list(gpu_frames.values()))
        row["gpu_stages"] = {k: distribution(v) for k, v in stages.items()}
        if prepare:
            row["cpu_prepare_geometry"] = distribution(prepare)
        builds = [float(v) for line in row["field_stats"] for v in re.findall(r"\bbuild_ms=([0-9.]+)", line)]
        row["field_build_ms"] = distribution(builds)
        save()

    if args.video:
        video = output / "presentation.mp4"
        run("presentation", ["--capture", "video", "--fps", 30, "--frames", 720,
                              "--output", video], capture=False)
        receipt["video_sha256"] = sha256(video)
        receipt["video_probe"] = json.loads(subprocess.check_output([
            "ffprobe", "-v", "error", "-show_entries", "stream=width,height,nb_frames,r_frame_rate",
            "-show_entries", "format=duration", "-of", "json", str(video)], text=True))
        for seconds in (0, 2.3, 3, 8, 12, 16, 20, 23.9):
            subprocess.run(["ffmpeg", "-v", "error", "-ss", str(seconds), "-i", str(video),
                            "-frames:v", "1", "-y", str(output / "captures" / f"video-{seconds}.png")], check=True)
        save()
    print(f"Qualification receipts: {output / 'manifest.json'}")


if __name__ == "__main__":
    main()
