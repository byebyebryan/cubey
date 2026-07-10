#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "numba>=0.61",
#   "numpy>=2.0",
#   "pillow>=11.0",
# ]
# ///

"""Run the research-only analytical terrain implementation as an external oracle."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess
import sys
import time

import numpy as np
from PIL import Image, ImageDraw


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--generator",
        type=Path,
        default=Path("build/dev/projects/terrain/terrain_generate"),
    )
    parser.add_argument(
        "--analytical-ref",
        type=Path,
        default=Path.home() / "code/ref/analytical-terrains",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("outputs/terrain/landscape-evolution-v1/oracle"),
    )
    parser.add_argument("--seed", type=int, default=9012)
    parser.add_argument("--grid-size", type=int, default=513)
    parser.add_argument("--cell-size", type=float, default=100.0)
    return parser.parse_args()


def read_field(directory: Path, manifest: dict, name: str) -> np.ndarray:
    grid = manifest["interior_grid"]
    entry = manifest["fields"][name]
    values = np.fromfile(directory / entry["raw_file"], dtype="<f4")
    expected = grid["width"] * grid["height"]
    if values.size != expected:
        raise RuntimeError(f"{name} has {values.size} samples, expected {expected}")
    return values.reshape((grid["height"], grid["width"])).astype(np.float64)


def distribution(values: np.ndarray) -> dict[str, float]:
    finite = values[np.isfinite(values)]
    if finite.size != values.size:
        raise RuntimeError("oracle output contains non-finite samples")
    return {
        "min": float(finite.min()),
        "p01": float(np.quantile(finite, 0.01)),
        "p05": float(np.quantile(finite, 0.05)),
        "p50": float(np.quantile(finite, 0.50)),
        "p95": float(np.quantile(finite, 0.95)),
        "p99": float(np.quantile(finite, 0.99)),
        "max": float(finite.max()),
        "mean": float(finite.mean()),
    }


def orientation_metrics(height: np.ndarray, cell_size: float) -> dict[str, object]:
    dz, dx = np.gradient(height, cell_size)
    magnitude = np.hypot(dx, dz)
    angle = np.mod(np.arctan2(dz, dx), math.pi)
    active = magnitude >= 0.01
    bins, _ = np.histogram(
        angle[active],
        bins=16,
        range=(0.0, math.pi),
        weights=np.minimum(magnitude[active], 2.0),
    )
    total = float(bins.sum())
    normalized = bins / total if total > 0.0 else bins
    return {
        "gradient_orientation_bins": [float(value) for value in normalized],
        "gradient_orientation_anisotropy": float(normalized.max() * 16.0),
    }


def graph_metrics(height: np.ndarray, receivers: np.ndarray) -> dict[str, float]:
    height_flat = height.reshape(-1)
    receiver_flat = receivers.reshape(-1).astype(np.int64)
    rows, columns = height.shape
    unresolved_sinks = 0
    discontinuities = 0
    severe_discontinuities = 0
    discontinuity_excess: list[float] = []
    interior_count = max((rows - 2) * (columns - 2), 1)
    for y in range(1, rows - 1):
        for x in range(1, columns - 1):
            index = y * columns + x
            receiver = int(receiver_flat[index])
            if receiver == index:
                unresolved_sinks += 1
                continue
            receiver_drop = max(height_flat[index] - height_flat[receiver], 0.0)
            has_discontinuity = False
            max_excess = 0.0
            for neighbor in (index - 1, index + 1, index - columns, index + columns):
                if neighbor == receiver or int(receiver_flat[neighbor]) == index:
                    continue
                excess = height_flat[index] - height_flat[neighbor] - receiver_drop
                max_excess = max(max_excess, float(excess))
                if excess > 1.0e-5:
                    has_discontinuity = True
            discontinuities += int(has_discontinuity)
            severe_discontinuities += int(max_excess > 100.0)
            if max_excess > 0.0:
                discontinuity_excess.append(max_excess)
    excess_p95 = (
        float(np.quantile(discontinuity_excess, 0.95)) if discontinuity_excess else 0.0
    )
    return {
        "unresolved_sink_coverage": unresolved_sinks / interior_count,
        "basin_discontinuity_coverage": discontinuities / interior_count,
        "severe_basin_discontinuity_coverage_gt_100m": severe_discontinuities
        / interior_count,
        "basin_discontinuity_excess_p95_m": excess_p95,
    }


def unit_image(values: np.ndarray, low: float, high: float) -> Image.Image:
    unit = np.clip((values - low) / max(high - low, 1e-12), 0.0, 1.0)
    pixels = np.flipud(np.rint(unit * 255.0).astype(np.uint8))
    return Image.fromarray(pixels, mode="L").convert("RGB")


def hillshade(height: np.ndarray, cell_size: float) -> Image.Image:
    dz, dx = np.gradient(height, cell_size)
    normal_x = -dx
    normal_y = np.ones_like(height)
    normal_z = -dz
    length = np.sqrt(normal_x * normal_x + normal_y * normal_y + normal_z * normal_z)
    light = np.asarray([0.58, 0.46, 0.67], dtype=np.float64)
    light /= np.linalg.norm(light)
    diffuse = (normal_x * light[0] + normal_y * light[1] + normal_z * light[2]) / length
    shade = np.clip(0.22 + 0.78 * diffuse, 0.0, 1.0)
    return unit_image(shade, 0.0, 1.0)


def labeled_sheet(images: list[tuple[str, Image.Image]], output: Path) -> None:
    width = max(image.width for _, image in images)
    label_height = 28
    sheet = Image.new(
        "RGB", (width * len(images), images[0][1].height + label_height), "white"
    )
    draw = ImageDraw.Draw(sheet)
    for index, (label, image) in enumerate(images):
        x = index * width
        sheet.paste(image, (x, 0))
        draw.text((x + 8, image.height + 7), label, fill="black")
    sheet.save(output)


def main() -> int:
    args = parse_args()
    if args.grid_size < 17 or args.grid_size % 2 == 0:
        raise RuntimeError("oracle input grid size must be odd and at least 17")
    if not args.generator.is_file():
        raise RuntimeError(f"terrain generator not found: {args.generator}")
    if not (args.analytical_ref / "analytical/analytical_multigrid.py").is_file():
        raise RuntimeError(f"analytical reference not found: {args.analytical_ref}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    input_dir = args.output_dir / "input"
    input_dir.mkdir(parents=True, exist_ok=True)
    guard_samples = 64
    source_grid_size = args.grid_size + (guard_samples * 2)
    subprocess.run(
        [
            str(args.generator),
            "--grid-size",
            str(source_grid_size),
            "--terrain-cell-size",
            str(args.cell_size),
            "--terrain-seed",
            str(args.seed),
            "--terrain-recipe",
            "upland-broad-noise-control-v1",
            "--terrain-export-raw",
            "--terrain-output-dir",
            str(input_dir),
        ],
        check=True,
    )

    manifest = json.loads((input_dir / "manifest.json").read_text())
    initial = read_field(input_dir, manifest, "source_height_m")
    uplift_potential = read_field(input_dir, manifest, "uplift_potential")

    edge_crop = [0, 0]
    required_multiple = 8
    cropped_height = initial.shape[0] - (initial.shape[0] % required_multiple)
    cropped_width = initial.shape[1] - (initial.shape[1] % required_multiple)
    if cropped_height != initial.shape[0] or cropped_width != initial.shape[1]:
        edge_crop = [
            initial.shape[1] - cropped_width,
            initial.shape[0] - cropped_height,
        ]
        initial = initial[:cropped_height, :cropped_width]
        uplift_potential = uplift_potential[:cropped_height, :cropped_width]

    shape = initial.shape
    data = {
        "initial_height": initial,
        "uplift": uplift_potential * 1.0e-3,
        "k_spl": np.full(shape, 2.0e-5, dtype=np.float64),
        "k_hillslope": np.full(shape, 0.1, dtype=np.float64),
        "k_thermal": np.full(shape, 1.0e-3, dtype=np.float64),
        "m_spl": np.full(shape, 0.4, dtype=np.float64),
        "m_hillslope": np.full(shape, -0.6, dtype=np.float64),
        "n": np.ones(shape, dtype=np.float64),
        "dx": args.cell_size,
        "t": 1.6e6,
        "multigrid_levels": 4,
        "solution_iters": 6,
        "solution_level": 0,
        "smoothing_iters": 50,
        "out_slope": 0.0,
        "boundary": "all_sides",
        "advection_mode": "spl",
    }

    sys.path.insert(0, str(args.analytical_ref))
    from analytical import analytical_multigrid  # pylint: disable=import-outside-toplevel

    start = time.perf_counter()
    analytical_multigrid.run(data)
    elapsed = time.perf_counter() - start
    full_height = np.asarray(data["height"], dtype=np.float64)
    full_drainage = np.asarray(data["drain"], dtype=np.float64)
    receivers = np.asarray(data["receivers"], dtype=np.int64).reshape(shape)
    review_height = args.grid_size - edge_crop[1]
    review_width = args.grid_size - edge_crop[0]
    review_slice = (
        slice(guard_samples, guard_samples + review_height),
        slice(guard_samples, guard_samples + review_width),
    )
    height = full_height[review_slice]
    drainage = full_drainage[review_slice]
    dz, dx = np.gradient(height, args.cell_size)
    slope = np.hypot(dx, dz)

    height.astype("<f4").tofile(args.output_dir / "height.f32")
    drainage.astype("<f4").tofile(args.output_dir / "drainage_area_m2.f32")
    height_image = unit_image(height, 0.0, 4000.0)
    slope_image = unit_image(slope, 0.0, 2.5)
    drainage_image = unit_image(np.log1p(drainage), 0.0, math.log1p(100_000_000.0))
    shade_image = hillshade(height, args.cell_size)
    height_image.save(args.output_dir / "height.png")
    slope_image.save(args.output_dir / "slope.png")
    drainage_image.save(args.output_dir / "drainage.png")
    shade_image.save(args.output_dir / "hillshade.png")
    labeled_sheet(
        [
            ("height 0..4000 m", height_image),
            ("hillshade", shade_image),
            ("slope 0..2.5", slope_image),
            ("drainage log", drainage_image),
        ],
        args.output_dir / "oracle-contact-sheet.png",
    )

    commit = subprocess.run(
        ["git", "-C", str(args.analytical_ref), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    summary = {
        "schema": "cubey.terrain.analytical-oracle.v1",
        "reference": {
            "path": str(args.analytical_ref.resolve()),
            "commit": commit,
            "license_scope": "research-and-evaluation-only",
            "paper_doi": "10.1111/cgf.15033",
        },
        "input": {
            "manifest": str((input_dir / "manifest.json").resolve()),
            "content_hash": manifest["content_hash"],
            "seed": args.seed,
            "requested_review_grid": [args.grid_size, args.grid_size],
            "source_grid": [source_grid_size, source_grid_size],
            "solver_grid": [int(shape[1]), int(shape[0])],
            "published_review_grid": [int(height.shape[1]), int(height.shape[0])],
            "cell_size_m": args.cell_size,
            "guard_samples": guard_samples,
            "positive_edge_crop_samples": edge_crop,
        },
        "parameters": {
            "age_years": data["t"],
            "multigrid_levels": data["multigrid_levels"],
            "iterations_per_level": data["solution_iters"],
            "altitude_correction_iterations": data["smoothing_iters"],
            "stream_power_exponent": 0.4,
            "stream_power_coefficient": 2.0e-5,
            "max_uplift_m_per_year": 1.0e-3,
            "hillslope_coefficient": 0.1,
            "thermal_coefficient": 1.0e-3,
        },
        "elapsed_seconds": elapsed,
        "fields": {
            "height_m": distribution(height),
            "slope": distribution(slope),
            "drainage_area_m2": distribution(drainage),
        },
        "review_metrics": orientation_metrics(height, args.cell_size)
        | graph_metrics(full_height, receivers),
    }
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(
        f"analytical oracle: solver={shape[1]}x{shape[0]} "
        f"review={height.shape[1]}x{height.shape[0]} "
        f"in {elapsed:.3f}s -> {args.output_dir}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
