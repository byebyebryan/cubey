#!/usr/bin/env -S uv run --python 3.12
# /// script
# requires-python = "==3.12.*"
# dependencies = [
#   "numpy==1.26.4", "pillow==12.3.0", "pysheds==0.4",
#   "pyflwdir==0.5.12", "pyproj==3.8.0", "rasterio==1.4.3",
#   "scikit-image==0.26.0", "scipy==1.15.3",
# ]
# ///
"""Read depressions in the existing immutable rain crop, not a new site survey.

The filled surface is an analysis-only copy and never enters Fluid. Analytical
storage is not simulated pooling. No water footprint is inferred from images.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np
import pyflwdir
from PIL import Image, ImageDraw
from scipy import ndimage

import terrain_hydro_read_v1 as hydro
import terrain_reach_scan_v1 as reach

ROOT = Path(__file__).resolve().parents[3]
SPEC = Path(__file__).parent / "fixtures/hillside-rain-study-v1/temperate-mountain-rain.json"
SCHEMA = "cubey.fluid25d.hillside_rain_basins.v2"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_crop() -> tuple[np.ndarray, dict]:
    spec = json.loads(SPEC.read_text())
    manifest_path = ROOT / spec["manifest"]
    if sha(manifest_path) != spec["manifest_sha256"]:
        raise ValueError("pinned rain manifest changed")
    manifest = json.loads(manifest_path.read_text())
    payload = manifest["files"]["elevation"]
    path = manifest_path.parent / payload["path"]
    if sha(path) != spec["elevation_sha256"]:
        raise ValueError("pinned rain elevation changed")
    if (payload["dtype"] != "float32-le" or payload["layout"] != "row-major-zx"
            or manifest["grid"]["sample_spacing_m"] != 30):
        raise ValueError("unsupported native raster layout")
    raw = np.memmap(path, dtype="<f4", mode="r", shape=tuple(payload["shape"]))
    x, z, w, h = spec["crop_xzwh"]
    crop = reach.transform_heightfield(raw[z:z + h, x:x + w], manifest)
    digest = hashlib.sha256(np.asarray(crop, dtype="<f4").tobytes()).hexdigest()
    if crop.shape != (512, 512) or digest != spec["transformed_crop_sha256"]:
        raise ValueError("pinned transformed crop changed")
    return crop, {key: spec[key] for key in (
        "manifest", "manifest_sha256", "elevation_sha256",
        "transformed_crop_sha256", "crop_xzwh", "cell_size_m")}


def read_crop(elevation: np.ndarray, spacing_m: float = 30.0) -> tuple[dict, np.ndarray]:
    raw = np.asarray(elevation, dtype=np.float32)
    if raw.ndim != 2 or min(raw.shape) < 3 or not np.isfinite(raw).all():
        raise ValueError("terrain must be a finite 2D array at least 3x3")
    if not math.isfinite(spacing_m) or spacing_m <= 0:
        raise ValueError("spacing must be finite and positive")
    # Never mutate the input, and never return a replacement simulation bed.
    filled, _ = pyflwdir.dem.fill_depressions(raw.copy(), outlets="edge", connectivity=8)
    basins, delta = hydro.basin_proxy(raw, filled, spacing_m)
    labels, _ = ndimage.label(delta > hydro.FILL_COMPONENT_EPS_M,
                              structure=np.ones((3, 3), dtype=np.uint8))
    step = np.concatenate((np.abs(np.diff(raw.astype(np.float64), axis=0)).ravel(),
                           np.abs(np.diff(raw.astype(np.float64), axis=1)).ravel()))
    candidates = []
    for component in basins["top_components"]:
        mask = labels == component["label"]
        floor = float(raw[mask].min())
        floor_index = int(np.flatnonzero(mask.ravel() & (raw.ravel() == floor))[0])
        z, x = divmod(floor_index, raw.shape[1])
        stages = []
        spill = float(np.asarray(filled)[mask].min())
        for depth_m in (.1, 1.0, 5.0):
            level = floor + depth_m
            if level >= spill:
                continue
            stage_labels, _ = ndimage.label(mask & (raw.astype(np.float64) <= level),
                                            structure=np.ones((3, 3), dtype=np.uint8))
            stage_mask = stage_labels == stage_labels[z, x]
            stages.append({"floor_relative_stage_depth_m": depth_m,
                           "floor_connected_cells": int(stage_mask.sum()),
                           "floor_connected_area_m2": float(stage_mask.sum() * spacing_m**2),
                           "storage_to_stage_m3": float((level - raw[stage_mask].astype(np.float64)).sum()
                                                        * spacing_m**2)})
        candidates.append({**component, "global_floor_cell_xz": [x, z],
                           "raw_floor_m": floor,
                           "analytical_partial_stages": stages,
                           "own_rain_12mm_per_hour_2h_m3": float(mask.sum() * spacing_m**2 * .024),
                           "own_rain_48mm_per_hour_2h_m3": float(mask.sum() * spacing_m**2 * .096),
                           "observation_mask_sha256": hashlib.sha256(
                               mask.astype(np.uint8).tobytes()).hexdigest()})
    return {
        "schema": SCHEMA,
        "meaning": "Analytical depression hypotheses on current crop only; not observed water.",
        "spacing_m": spacing_m, "shape_zx": list(raw.shape),
        "whole_crop_rain_2h_m3": {"12mm_per_hour": float(raw.size * spacing_m**2 * .024),
                                   "48mm_per_hour": float(raw.size * spacing_m**2 * .096)},
        "elevation_range_m": [float(raw.min()), float(raw.max())],
        "adjacent_absolute_bed_step_m": {
            "median": float(np.median(step)), "p90": float(np.quantile(step, .9)),
            "p99": float(np.quantile(step, .99)), "maximum": float(step.max()),
        },
        "bed_step_above_uniform_rain_depth_fraction": {
            "12mm_per_hour_2h_0.024m": float(np.mean(step >= .024)),
            "48mm_per_hour_2h_0.096m": float(np.mean(step >= .096)),
        },
        "basins": basins, "ranked_observation_hypotheses": candidates,
        "limits": [
            "Positive-fill patches can merge nested basins; ranking is storage geometry, not water delivery.",
            "All crop edges are analytical outlets; outside-crop catchments are unknown.",
            "No infiltration, erosion, permanence or calibrated hydrology is implied.",
            "Observation masks are fixed before any subsequent simulation comparison.",
        ],
    }, labels


def render_map(crop: np.ndarray, labels: np.ndarray, result: dict, path: Path) -> None:
    """Scientific annotated map; amber means analysis-only depression, not water."""
    relief = (crop - crop.min()) / max(float(np.ptp(crop)), 1)
    gray = np.rint(45 + relief * 170).astype(np.uint8)
    rgb = np.repeat(gray[..., None], 3, axis=2)
    rgb[labels > 0] = (200, 130, 35)
    terrain_image = Image.fromarray(rgb).resize((1024, 1024), Image.Resampling.NEAREST)
    canvas = Image.new("RGB", (1024, 1120), (18, 21, 25))
    canvas.paste(terrain_image, (0, 96))
    draw = ImageDraw.Draw(canvas)
    draw.text((16, 10), "CURRENT MOUNTAIN CROP - analytical depression reading (NOT simulated water)", fill="white")
    draw.text((16, 30), "Gray: elevation | Amber: positive fill on derived copy | x right, z down | native cells 30m", fill="white")
    draw.text((16, 50), "A/B/C: largest storage hypotheses | Red: inherited collection ROI and V1 peak-depth cell", fill="white")
    draw.text((16, 70), "Source terrain unchanged; filled copy never enters simulation. No new sites surveyed.", fill="white")
    for name, item in zip(("A", "B", "C"), result["ranked_observation_hypotheses"]):
        xmin, zmin, xmax, zmax = item["bbox_xz"]
        draw.rectangle((xmin * 2, zmin * 2 + 96, (xmax + 1) * 2, (zmax + 1) * 2 + 96),
                       outline=(255, 225, 110), width=2)
        draw.text((xmin * 2, zmin * 2 + 80), name, fill=(255, 225, 110))
    draw.rectangle((48 * 2, 241 * 2 + 96, 69 * 2, 262 * 2 + 96), outline=(255, 80, 80), width=2)
    draw.text((48 * 2, 262 * 2 + 100), "V1 collection ROI", fill=(255, 100, 100))
    draw.ellipse((176 * 2 - 5, 60 * 2 + 91, 176 * 2 + 5, 60 * 2 + 101), outline=(255, 80, 80), width=2)
    draw.text((176 * 2 + 8, 60 * 2 + 91), "V1 peak cell", fill=(255, 100, 100))
    canvas.save(path)


def observe_water(bed: np.ndarray, depth: np.ndarray, velocity: np.ndarray,
                  mask: np.ndarray, direct_rain_depth_m: float, spacing_m: float = 30.0,
                  material_depth_m: float = .01, active_speed_m_per_s: float = .02) -> dict:
    """Read explicit fields only; coherent pond classification remains separate.

    Net storage beyond rain is not gross inflow. Speed-volume partition includes
    thin films, while the connected footprint uses a fixed depth threshold.
    """
    bed, depth, velocity = (np.asarray(value, dtype=np.float64)
                            for value in (bed, depth, velocity))
    mask = np.asarray(mask)
    if (bed.ndim != 2 or depth.shape != bed.shape or mask.shape != bed.shape
            or mask.dtype != np.bool_ or velocity.shape != (*bed.shape, 2)
            or not np.isfinite(bed).all() or not np.isfinite(depth).all()
            or not np.isfinite(velocity).all() or np.any(depth < 0) or not mask.any()):
        raise ValueError("invalid explicit water fields or observation mask")
    if (not math.isfinite(spacing_m) or spacing_m <= 0
            or not math.isfinite(direct_rain_depth_m) or direct_rain_depth_m < 0
            or not math.isfinite(material_depth_m) or material_depth_m <= 0
            or not math.isfinite(active_speed_m_per_s) or active_speed_m_per_s <= 0):
        raise ValueError("invalid observation thresholds")
    speed = np.linalg.norm(velocity, axis=2)
    area = spacing_m * spacing_m
    if not np.isfinite(speed).all() or not math.isfinite(area):
        raise ValueError("observation speed or cell area overflowed")
    moving = mask & (speed >= active_speed_m_per_s)
    wet = mask & (depth >= material_depth_m)
    connected, count = ndimage.label(wet, structure=np.ones((3, 3), dtype=np.uint8))
    sizes = np.bincount(connected.ravel(), minlength=count + 1)
    largest = int(np.argmax(sizes[1:]) + 1) if count else 0
    footprint = connected == largest if largest else np.zeros(mask.shape, dtype=bool)
    surface = (bed + depth)[footprint]
    volume = float(depth[mask].sum() * area)
    direct = float(mask.sum() * area * direct_rain_depth_m)
    moving_volume = float(depth[moving].sum() * area)
    return {
        "water_volume_m3": volume, "direct_rain_volume_m3": direct,
        "net_storage_beyond_own_rain_m3": volume - direct,
        "moving_volume_m3": moving_volume, "slow_volume_m3": volume - moving_volume,
        "material_connected_components": count,
        "largest_connected_footprint_cells": int(footprint.sum()),
        "largest_connected_footprint_area_m2": float(footprint.sum() * area),
        "largest_connected_surface_range_m": [float(surface.min()), float(surface.max())]
            if surface.size else None,
        "limits": "Depth connectivity and slow water alone do not establish a level pond, incoming branches or spill.",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    crop, identity = load_crop()
    result, labels = read_crop(crop)
    result["input_identity"] = identity
    result["runner_sha256"] = sha(Path(__file__))
    # Masks only; no filled elevation is serialized as a usable simulation input.
    np.save(args.output / "analytical-depression-labels.npy", labels)
    result["mask_payload_sha256"] = sha(args.output / "analytical-depression-labels.npy")
    render_map(crop, labels, result, args.output / "analytical-depressions.png")
    result["map_sha256"] = sha(args.output / "analytical-depressions.png")
    (args.output / "summary.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"output": str(args.output), "basin_count": result["basins"]["component_count"],
                      "bed_step_median_m": result["adjacent_absolute_bed_step_m"]["median"]}))


if __name__ == "__main__":
    main()
