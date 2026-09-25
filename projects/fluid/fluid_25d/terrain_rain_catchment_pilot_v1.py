#!/usr/bin/env -S uv run --python 3.12
# /// script
# requires-python = "==3.12.*"
# dependencies = ["pillow==12.3.0"]
# ///
"""Frozen rain pilot on a mostly contained lowland headwater catchment.

This is a diagnostic study, not a promoted terrain fixture or a one-outlet demo.
"""

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
OUTPUT = ROOT / "outputs/fluid/terrain-rain-catchment-pilot-v1-20260924"
ELEVATION_SHA256 = "fd52d7c3b25f139ac709b8ced673ccc77d749cc33e4c52e66745494a86dcf7a2"
CROP_SHA256 = "7d7ace75441f3e931c3631f72570ecb655012d3a9b1c1f46853dec6bf88b4e1d"
CROP_X, CROP_Z, WIDTH, HEIGHT = 304, 837, 256, 128
OUTLET_WORLD_X, OUTLET_WORLD_Z = 546, 837
OUTLET_LOCAL_X = OUTLET_WORLD_X - CROP_X
OUTLET_NORTH_BIN = OUTLET_LOCAL_X * 16 // WIDTH


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def read_metrics(path: Path, frame: int) -> dict[str, float]:
    selected: dict[str, float] = {}
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            if int(row["frame_index"]) == frame - 1:
                selected[f"{row['category']}.{row['name']}"] = float(row["value"])
    if not selected:
        raise RuntimeError(f"missing exact frame {frame} diagnostics: {path}")
    return selected


def command(app: Path, output: Path, frame: int, view: str) -> list[str]:
    args = [
        str(app), "--headless", "--capture", "png", "--width", "1280", "--height", "720",
        "--fluid25d-solver", "finite-volume", "--fluid25d-fixed-delta-seconds", "2",
        "--fluid25d-substeps", "8", "--fluid25d-scenario", "terrain-case",
        "--terrain-heightfield", str(SOURCE), "--grid-width", str(WIDTH),
        "--grid-height", str(HEIGHT), "--fluid25d-terrain-crop-x", str(CROP_X),
        "--fluid25d-terrain-crop-z", str(CROP_Z),
        "--fluid25d-terrain-water-protocol", "rain-pulse",
        "--fluid25d-rainfall-rate-mm-per-hour", "60",
        "--fluid25d-source-active-duration-seconds", "600", "--frames", str(frame),
        "--profile-diagnostics", "--profile-diagnostic-interval", str(max(1, frame - 1)),
        "--profile-output", str(output / f"{view}-f{frame:04d}-profile"),
        "--output", str(output / f"{view}-f{frame:04d}.png"),
    ]
    if view == "composite":
        args.extend(("--fluid25d-view", "catchment", "--fluid25d-catchment-view", "composite",
                     "--fluid25d-terrain-thin-water-composite"))
    elif view == "depth":
        args.extend(("--fluid25d-view", "diagnostics", "--debug-view", "depth"))
    else:
        raise ValueError(f"unsupported review view: {view}")
    return args


def boundary_summary(metrics: dict[str, float]) -> dict[str, object]:
    prefix = "fluid_25d.boundary."
    bins = {
        side: [metrics[f"{prefix}{side}_bin_{index}_m3"] for index in range(16)]
        for side in ("north", "south", "west", "east")
    }
    side_totals = {side: sum(values) for side, values in bins.items()}
    corner = metrics[f"{prefix}corner_outflow_m3"]
    non_edge = metrics[f"{prefix}non_edge_outflow_m3"]
    total = metrics["fluid_25d.water.cumulative_boundary_outflow_volume_m3"]
    attribution_residual = total - sum(side_totals.values()) - corner - non_edge
    if abs(attribution_residual) > 0.001 or non_edge != 0.0:
        raise RuntimeError("boundary ledger attribution does not close")
    outlet_segment = bins["north"][OUTLET_NORTH_BIN]
    return {
        "side_totals_m3": side_totals,
        "side_bins_m3": bins,
        "corner_m3": corner,
        "non_edge_m3": non_edge,
        "attribution_residual_m3": attribution_residual,
        "intended_outlet_bin": OUTLET_NORTH_BIN,
        "intended_outlet_bin_cell_x": [OUTLET_NORTH_BIN * 16, OUTLET_NORTH_BIN * 16 + 14],
        "intended_outlet_bin_m3": outlet_segment,
        "intended_outlet_bin_fraction_of_all_boundary_outflow": outlet_segment / total,
    }


def make_contact_sheet(output: Path) -> Path:
    panels = [
        ("composite", 1, "t=2s  |  rainfall starts"),
        ("composite", 300, "t=600s  |  60 mm/h rain ends"),
        ("composite", 600, "t=1200s  |  600s after rain"),
        ("depth", 300, "depth map  |  t=600s"),
        ("depth", 600, "depth map  |  t=1200s"),
    ]
    panel_w, panel_h, title_h = 1280, 720, 44
    sheet = Image.new("RGB", (panel_w * 2, (panel_h + title_h) * 3), "#202830")
    draw = ImageDraw.Draw(sheet)
    for position, (view, frame, label) in enumerate(panels):
        x = (position % 2) * panel_w
        y = (position // 2) * (panel_h + title_h)
        with Image.open(output / f"{view}-f{frame:04d}.png") as source:
            picture = source.convert("RGB")
            if picture.size != (panel_w, panel_h):
                raise RuntimeError(f"unexpected capture size: {picture.size}")
            sheet.paste(picture, (x, y + title_h))
        draw.text((x + 14, y + 13), label, fill="white")
    draw.text((panel_w + 14, 2 * (panel_h + title_h) + 13),
              "Read with caution: all four crop edges are open; no outlet promotion", fill="white")
    path = output / "review-contact-sheet.png"
    sheet.save(path)
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
        raise SystemExit("pinned native terrain grid differs from the review source")
    output.mkdir(parents=True)
    expected_identity = f"elevation-sha256={ELEVATION_SHA256}:crop-sha256={CROP_SHA256}:crop={CROP_X},{CROP_Z},{WIDTH}x{HEIGHT}:spacing-m=30.000000"
    captures: list[dict[str, object]] = []
    diagnoses: dict[str, dict[str, object]] = {}
    for view, frames in (("composite", (1, 300, 600)), ("depth", (300, 600))):
        for frame in frames:
            argv = command(app, output, frame, view)
            completed = subprocess.run(argv, cwd=ROOT, text=True, capture_output=True, check=False)
            (output / f"{view}-f{frame:04d}.log").write_text(completed.stdout + completed.stderr)
            if completed.returncode != 0 or expected_identity not in completed.stdout:
                raise RuntimeError(f"capture failed or terrain identity changed: {view} f{frame}")
            profile = output / f"{view}-f{frame:04d}-profile.metrics.csv"
            values = read_metrics(profile, frame)
            source_m3 = values["fluid_25d.water.cumulative_source_volume_m3"]
            if values["fluid_25d.solver.finite_volume_status_flags"] != 0.0 or source_m3 <= 0.0:
                raise RuntimeError(f"solver status or source gate failed: {view} f{frame}")
            if abs(values["fluid_25d.water.conservation_residual_m3"]) / source_m3 > 0.0001:
                raise RuntimeError(f"conservation gate failed: {view} f{frame}")
            key = f"f{frame:04d}"
            if view == "composite":
                diagnoses[key] = {
                    "seconds": frame * 2,
                    "metrics": values,
                    "boundary": boundary_summary(values),
                }
            else:
                composite = next(c for c in captures if c["view"] == "composite" and c["frame"] == frame)
                if sha256(profile) != composite["profile_sha256"]:
                    raise RuntimeError(f"debug view changed exact solver diagnostics: f{frame}")
            captures.append({
                "view": view, "frame": frame, "command": argv,
                "capture_sha256": sha256(output / f"{view}-f{frame:04d}.png"),
                "profile_sha256": sha256(profile),
            })
    review = make_contact_sheet(output)
    decision = "single-outlet presentation gate fails; retain as distributed-runoff evidence only"
    report = {
        "schema": "cubey.fluid25d.terrain_rain_catchment_pilot.v1",
        "decision": decision,
        "source_manifest": str(SOURCE.relative_to(ROOT)),
        "source_manifest_sha256": sha256(SOURCE),
        "source_elevation_sha256": ELEVATION_SHA256,
        "transformed_crop_sha256": CROP_SHA256,
        "app_sha256": sha256(app),
        "runner_sha256": sha256(Path(__file__)),
        "site": {
            "crop": [CROP_X, CROP_Z, WIDTH, HEIGHT], "cell_spacing_m": 30.0,
            "intended_outlet_global_cell": [OUTLET_WORLD_X, OUTLET_WORLD_Z],
            "intended_outlet_local_cell": [OUTLET_LOCAL_X, 0],
            "full_d8_upstream_area_km2": 16.5222,
            "upstream_mask_inside_crop_km2": 15.2127,
            "upstream_mask_outside_crop_km2": 1.3095,
            "upstream_mask_contained_fraction": 15.2127 / 16.5222,
        },
        "forcing": {"kind": "uniform rainfall excess", "rate_mm_per_hour": 60,
                    "duration_seconds": 600, "drain_seconds": 600, "initial_depth_m": 0.0,
                    "losses": "none", "boundary": "all outward perimeter faces open"},
        "captures": captures,
        "diagnoses": diagnoses,
        "review_capture": str(review.relative_to(ROOT)),
        "review_capture_sha256": sha256(review),
    }
    (output / "result.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    last = diagnoses["f0600"]
    print(json.dumps({"output": str(output), "decision": decision,
                      "outlet_bin_fraction": last["boundary"]["intended_outlet_bin_fraction_of_all_boundary_outflow"],
                      "review": str(review)}, indent=2))


if __name__ == "__main__":
    main()
