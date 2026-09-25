#!/usr/bin/env -S uv run --python 3.12
# /// script
# requires-python = "==3.12.*"
# dependencies = ["pillow==12.3.0"]
# ///
"""Frozen rain Composite A/B: change thin-water opacity, not the solver state."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / "build/dev/projects/fluid/fluid_25d/fluid_25d"
SOURCE = ROOT / "cache/terrain/sources/v1/presets/rolling-lowland-1/heightfield.json"
OUTPUT = ROOT / "outputs/fluid/terrain-rain-thin-water-ab-v1-20260924"
ELEVATION_SHA256 = "fd52d7c3b25f139ac709b8ced673ccc77d749cc33e4c52e66745494a86dcf7a2"
CROP_SHA256 = "aa6777e657234f2f787a22c5b68071ddf6aa3a1a09414b1afb92d8521dd6ce63"
FRAMES = (300, 600)
VARIANTS = ("baseline", "thin-water")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def metrics(path: Path, frame: int) -> dict[str, float]:
    selected: dict[str, float] = {}
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            if int(row["frame_index"]) == frame - 1:
                selected[f"{row['category']}.{row['name']}"] = float(row["value"])
    if not selected:
        raise RuntimeError(f"missing exact frame {frame} profile metrics: {path}")
    return selected


def command(app: Path, output: Path, variant: str, frame: int) -> list[str]:
    args = [
        str(app), "--headless", "--capture", "png", "--width", "1280", "--height", "720",
        "--fluid25d-solver", "finite-volume", "--fluid25d-fixed-delta-seconds", "2",
        "--fluid25d-substeps", "8", "--fluid25d-scenario", "terrain-case",
        "--terrain-heightfield", str(SOURCE), "--grid-width", "256", "--grid-height", "128",
        "--fluid25d-terrain-crop-x", "1216", "--fluid25d-terrain-crop-z", "1664",
        "--fluid25d-terrain-water-protocol", "rain-pulse",
        "--fluid25d-rainfall-rate-mm-per-hour", "60",
        "--fluid25d-source-active-duration-seconds", "600", "--frames", str(frame),
        "--fluid25d-view", "catchment", "--fluid25d-catchment-view", "composite",
        "--profile-diagnostics", "--profile-diagnostic-interval", str(frame - 1),
        "--profile-output", str(output / f"f{frame:04d}-profile"),
        "--output", str(output / f"f{frame:04d}.png"),
    ]
    if variant == "thin-water":
        args.append("--fluid25d-terrain-thin-water-composite")
    return args


def make_review(output: Path) -> Path:
    width, height = 1280, 720
    review = Image.new("RGB", (width * 2, (height + 52) * 2), "#202830")
    draw = ImageDraw.Draw(review)
    for row, frame in enumerate(FRAMES):
        for col, variant in enumerate(VARIANTS):
            x, y = col * width, row * (height + 52)
            with Image.open(output / variant / f"f{frame:04d}.png") as source:
                picture = source.convert("RGB")
                if picture.size != (width, height):
                    raise RuntimeError(f"unexpected capture size: {picture.size}")
                review.paste(picture, (x, y + 52))
            title = f"{variant} | t={frame * 2}s | 60 mm/h rain for first 600s"
            draw.text((x + 16, y + 16), title, fill="white")
    path = output / "review-contact-sheet.png"
    review.save(path)
    return path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=APP)
    parser.add_argument("--output-dir", type=Path, default=OUTPUT)
    args = parser.parse_args()
    app = args.app.resolve()
    output = args.output_dir.resolve()
    if output.exists():
        raise SystemExit(f"refusing to overwrite existing evidence: {output}")
    if not app.is_file():
        raise SystemExit(f"application not found: {app}")
    manifest = json.loads(SOURCE.read_text())
    elevation = SOURCE.parent / manifest["files"]["elevation"]["path"]
    if manifest["files"]["elevation"]["sha256"] != ELEVATION_SHA256 or sha256(elevation) != ELEVATION_SHA256:
        raise SystemExit("pinned elevation payload differs from the review source")
    if manifest["grid"]["sample_spacing_m"] != 30.0 or manifest["grid"]["width"] != 2048 or manifest["grid"]["height"] != 2048:
        raise SystemExit("pinned native grid differs from the review source")
    output.mkdir(parents=True)
    records = []
    expected_identity = f"elevation-sha256={ELEVATION_SHA256}:crop-sha256={CROP_SHA256}:crop=1216,1664,256x128:spacing-m=30.000000"
    for frame in FRAMES:
        frame_records = {}
        for variant in VARIANTS:
            case_dir = output / variant
            case_dir.mkdir(exist_ok=True)
            argv = command(app, case_dir, variant, frame)
            completed = subprocess.run(argv, cwd=ROOT, text=True, capture_output=True, check=False)
            (case_dir / f"f{frame:04d}.log").write_text(completed.stdout + completed.stderr)
            if completed.returncode != 0 or expected_identity not in completed.stdout:
                raise RuntimeError(f"capture failed or terrain identity changed: {variant} f{frame}")
            profile = case_dir / f"f{frame:04d}-profile.metrics.csv"
            capture = case_dir / f"f{frame:04d}.png"
            frame_records[variant] = {
                "command": argv,
                "profile_sha256": sha256(profile),
                "capture_sha256": sha256(capture),
                "metrics": metrics(profile, frame),
            }
        baseline = frame_records["baseline"]
        thin = frame_records["thin-water"]
        if baseline["profile_sha256"] != thin["profile_sha256"]:
            raise RuntimeError(f"render variant changed sampled solver metrics at f{frame}")
        values = baseline["metrics"]
        source_m3 = values["fluid_25d.water.cumulative_source_volume_m3"]
        residual_m3 = values["fluid_25d.water.conservation_residual_m3"]
        if values["fluid_25d.solver.finite_volume_status_flags"] != 0.0 or source_m3 <= 0.0 or abs(residual_m3) / source_m3 > 0.0001:
            raise RuntimeError(f"solver status/source/conservation gate failed at f{frame}")
        records.append({"frame": frame, "seconds": frame * 2, "variants": frame_records})
    review = make_review(output)
    report = {
        "schema": "cubey.fluid25d.terrain_rain_thin_water_ab.v1",
        "status": "render-only A/B; no site or rain product promotion",
        "source_manifest": str(SOURCE.relative_to(ROOT)),
        "source_manifest_sha256": sha256(SOURCE),
        "source_elevation_sha256": ELEVATION_SHA256,
        "transformed_crop_sha256": CROP_SHA256,
        "app_sha256": sha256(app),
        "water_shader_source_sha256": sha256(ROOT / "projects/fluid/sim/fluid_25d/shaders/fluid_25d_water.frag"),
        "water_shader_asset_sha256": sha256(app.parent / "shaders/fluid_25d_water.frag.spv"),
        "runner_sha256": sha256(Path(__file__)),
        "grid": {"width": 256, "height": 128, "spacing_m": 30.0, "crop_x": 1216, "crop_z": 1664},
        "forcing": {"kind": "uniform rainfall excess", "rate_mm_per_hour": 60, "duration_seconds": 600, "initial_depth_m": 0.0},
        "primary_check": "entire exact-frame profile metrics file SHA-256 is identical between baseline and thin-water",
        "records": records,
        "review_capture": str(review.relative_to(ROOT)),
        "review_capture_sha256": sha256(review),
    }
    (output / "result.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"output": str(output), "same_solver_metrics": True, "frames": list(FRAMES), "review": str(review)}, indent=2))


if __name__ == "__main__":
    main()
