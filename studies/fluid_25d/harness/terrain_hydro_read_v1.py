#!/usr/bin/env -S uv run --python 3.12
# /// script
# requires-python = "==3.12.*"
# dependencies = [
#   "numpy==1.26.4",
#   "pillow==12.3.0",
#   "pysheds==0.4",
#   "pyflwdir==0.5.12",
#   "pyproj==3.8.0",
#   "rasterio==1.4.3",
#   "scikit-image==0.26.0",
#   "scipy==1.15.3",
# ]
# ///
"""Read full, immutable Terrain Diffusion elevations as drainage hypotheses.

This is an offline study tool, not a Fluid input or a terrain product. A filled
surface is only a derived routing copy; the source raster is never rewritten.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import math
import time
from pathlib import Path
from typing import Any

import numpy as np
import pyflwdir
from PIL import Image, ImageDraw
from pyproj import CRS
from pysheds.grid import Grid
from pysheds.sview import Raster, ViewFinder
from rasterio.transform import Affine
from scipy import ndimage

import terrain_reach_scan_v1 as reach


ROOT = Path(__file__).resolve().parents[3]
SOURCE_ROOT = ROOT / "cache/terrain/sources/v1"
DEFAULT_OUTPUT = ROOT / "outputs/fluid/terrain-hydro-read-v1-first-map-20260924"
SCHEMA = "cubey.fluid25d.terrain_hydro_read.v1"
SPACING_M = 30.0
CELL_AREA_M2 = SPACING_M * SPACING_M
REVIEW_THRESHOLD_KM2 = (0.25, 1.0, 4.0, 16.0)
FILL_COMPONENT_EPS_M = 0.01
DIRECTION_CODES = (64, 128, 1, 2, 4, 8, 16, 32)
DIRECTION_OFFSETS = ((-1, 0), (-1, 1), (0, 1), (1, 1), (1, 0), (1, -1), (0, -1), (-1, -1))
PRIOR_REACH_BOXES = (
    (384, 837, 256, 128, "broad prior reach"),
    (843, 809, 256, 128, "narrow prior reach"),
)


def make_grid(elevation_m: np.ndarray, spacing_m: float = SPACING_M) -> tuple[Grid, Raster]:
    """Use an arbitrary local metre grid; coordinates are not georeferenced."""
    array = np.asarray(elevation_m, dtype=np.float64)
    if array.ndim != 2 or min(array.shape) < 3 or not np.isfinite(array).all():
        raise ValueError("elevation must be a finite 2D array at least 3x3")
    if not math.isfinite(spacing_m) or spacing_m <= 0:
        raise ValueError("spacing must be finite and positive")
    finder = ViewFinder(
        affine=Affine(spacing_m, 0.0, 0.0, 0.0, -spacing_m, 0.0),
        shape=array.shape,
        nodata=np.nan,
        crs=CRS.from_epsg(3857),
    )
    return Grid(viewfinder=finder), Raster(array, viewfinder=finder)


def terminal_cells(directions: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Return all D8 terminals and those whose last step crosses the map edge."""
    data = np.asarray(directions)
    height, width = data.shape
    terminal = ~np.isin(data, DIRECTION_CODES)
    boundary_exit = np.zeros(data.shape, dtype=bool)
    for code, (dz, dx) in zip(DIRECTION_CODES, DIRECTION_OFFSETS, strict=True):
        if dz == -1:
            boundary_exit[0, :] |= data[0, :] == code
        if dz == 1:
            boundary_exit[height - 1, :] |= data[height - 1, :] == code
        if dx == -1:
            boundary_exit[:, 0] |= data[:, 0] == code
        if dx == 1:
            boundary_exit[:, width - 1] |= data[:, width - 1] == code
    terminal |= boundary_exit
    return terminal, boundary_exit


def basin_proxy(raw: np.ndarray, filled: np.ndarray, spacing_m: float) -> tuple[dict[str, Any], np.ndarray]:
    """Connected positive-fill patches: storage diagnostics, not lake labels."""
    delta = np.maximum(np.asarray(filled, dtype=np.float64) - raw, 0.0)
    area = spacing_m * spacing_m
    mask = delta > FILL_COMPONENT_EPS_M
    labels, count = ndimage.label(mask, structure=np.ones((3, 3), dtype=np.uint8))
    sizes = np.bincount(labels.ravel(), minlength=count + 1)
    volumes = np.bincount(labels.ravel(), weights=delta.ravel() * area, minlength=count + 1)
    top_ids = sorted(range(1, count + 1), key=lambda idx: (-volumes[idx], idx))[:12]
    top = []
    for idx in top_ids:
        rows, cols = np.where(labels == idx)
        patch = labels[rows.min() : rows.max() + 1, cols.min() : cols.max() + 1] == idx
        patch_filled = np.asarray(filled)[rows.min() : rows.max() + 1, cols.min() : cols.max() + 1]
        top.append(
            {
                "label": idx,
                "cells": int(sizes[idx]),
                "area_km2": float(sizes[idx] * area / 1e6),
                "fill_volume_m3": float(volumes[idx]),
                "maximum_fill_depth_m": float(delta[labels == idx].max()),
                "derived_spill_elevation_range_m": [float(patch_filled[patch].min()), float(patch_filled[patch].max())],
                "bbox_xz": [int(cols.min()), int(rows.min()), int(cols.max()), int(rows.max())],
                "touches_map_edge": bool(rows.min() == 0 or cols.min() == 0 or rows.max() == raw.shape[0] - 1 or cols.max() == raw.shape[1] - 1),
            }
        )
    summary = {
        "meaning": "positive fill on analytical copy; not observed water or verified lakes",
        "threshold_m": FILL_COMPONENT_EPS_M,
        "filled_cell_fraction": float(np.count_nonzero(mask) / raw.size),
        "fill_volume_m3": float(delta.sum() * area),
        "fill_mean_m": float(delta.mean()),
        "fill_max_m": float(delta.max()),
        "component_count": int(count),
        "top_components": top,
        "limitations": "8-connected fill patches can merge nested basins; spill-elevation range and real-sink classification require separate review",
    }
    return summary, delta


def read_hydrology(elevation_m: np.ndarray, spacing_m: float = SPACING_M, boxes: tuple[tuple[int, int, int, int, str], ...] = ()) -> dict[str, Any]:
    grid, raw = make_grid(elevation_m, spacing_m)
    pf_filled, pf_d8 = pyflwdir.dem.fill_depressions(np.asarray(raw).copy(), outlets="edge", connectivity=8)
    pf_flow = pyflwdir.from_array(pf_d8, ftype="d8", transform=Affine(spacing_m, 0, 0, 0, -spacing_m, 0))
    pf_acc = np.asarray(pf_flow.upstream_area(unit="cell"), dtype=np.float64)
    pf_terminals = np.zeros(raw.shape, dtype=bool)
    pf_terminals.ravel()[pf_flow.idxs_pit] = True
    pitless = grid.fill_pits(raw)
    filled = grid.fill_depressions(pitless)
    basins, fill_delta = basin_proxy(np.asarray(raw), np.asarray(pf_filled), spacing_m)
    routed = grid.resolve_flats(filled)
    d8 = grid.flowdir(routed, routing="d8", dirmap=DIRECTION_CODES)
    mfd = grid.flowdir(routed, routing="mfd", dirmap=DIRECTION_CODES)
    pysheds_acc_d8 = np.asarray(grid.accumulation(d8, routing="d8", dirmap=DIRECTION_CODES), dtype=np.float64)
    acc_d8 = pf_acc
    acc_mfd = np.asarray(grid.accumulation(mfd, routing="mfd", dirmap=DIRECTION_CODES), dtype=np.float64)
    pysheds_terminals, _ = terminal_cells(np.asarray(d8))
    terminals = pf_terminals
    edge = np.zeros(raw.shape, dtype=bool)
    edge[[0, -1], :] = True
    edge[:, [0, -1]] = True
    total = float(acc_d8[terminals].sum())
    expected = float(raw.size)
    interior_terminals = terminals & ~edge
    boundary_terminals = terminals & edge
    interior_codes, interior_code_counts = np.unique(np.asarray(pf_d8)[interior_terminals], return_counts=True)
    prior_boxes = []
    for x, z, box_width, box_height, label in boxes:
        if x + box_width > raw.shape[1] or z + box_height > raw.shape[0]:
            continue
        section = np.s_[z : z + box_height, x : x + box_width]
        prior_boxes.append(
            {
                "label": label,
                "bbox_xzwh": [x, z, box_width, box_height],
                "d8_max_contributing_area_km2": float(acc_d8[section].max() * spacing_m * spacing_m / 1e6),
                "mfd_max_contributing_area_km2": float(acc_mfd[section].max() * spacing_m * spacing_m / 1e6),
                "fill_fraction": float(np.count_nonzero(fill_delta[section] > FILL_COMPONENT_EPS_M) / (box_width * box_height)),
            }
        )
    drainage = {
        "meaning": "unit-runoff contributing area, not water depth, discharge, or permanent streams",
        "primary_d8_method": "pyflwdir priority flood with edge outlets and explicit through-flat D8 directions",
        "mfd_valid_for_ranking": False,
        "mfd_caveat": "PySheds flat resolution has interior terminals on this map; MFD is visual sensitivity only until an independent connected MFD route is validated",
        "pysheds_d8_diagnostic": {
            "interior_terminal_count": int(np.count_nonzero(pysheds_terminals & ~edge)),
            "interior_terminal_area_fraction": float(pysheds_acc_d8[pysheds_terminals & ~edge].sum() / expected),
            "routed_remaining_pit_count": int(np.count_nonzero(np.asarray(grid.detect_pits(routed)))),
            "routed_remaining_flat_count": int(np.count_nonzero(np.asarray(grid.detect_flats(routed)))),
        },
        "d8_terminal_count": int(np.count_nonzero(terminals)),
        "d8_interior_terminal_count": int(np.count_nonzero(terminals & ~edge)),
        "d8_interior_terminal_area_fraction": float(acc_d8[interior_terminals].sum() / expected),
        "d8_largest_interior_terminal_area_km2": float(acc_d8[interior_terminals].max() * spacing_m * spacing_m / 1e6) if np.any(interior_terminals) else 0.0,
        "d8_interior_terminal_codes": {str(int(code)): int(count) for code, count in zip(interior_codes, interior_code_counts, strict=True)},
        "d8_boundary_terminal_count": int(np.count_nonzero(boundary_terminals)),
        "d8_boundary_terminal_area_fraction": float(acc_d8[boundary_terminals].sum() / expected),
        "d8_terminal_area_balance_relative": float((total - expected) / expected),
        "d8_max_contributing_area_km2": float(acc_d8.max() * spacing_m * spacing_m / 1e6),
        "mfd_max_contributing_area_km2": float(acc_mfd.max() * spacing_m * spacing_m / 1e6),
        "thresholds_km2": list(REVIEW_THRESHOLD_KM2),
        "prior_morphology_boxes": prior_boxes,
        "threshold_cell_counts": [
            {
                "threshold_km2": threshold,
                "d8_cells": int(np.count_nonzero(acc_d8 * spacing_m * spacing_m >= threshold * 1e6)),
                "mfd_cells": int(np.count_nonzero(acc_mfd * spacing_m * spacing_m >= threshold * 1e6)),
            }
            for threshold in REVIEW_THRESHOLD_KM2
        ],
    }
    return {
        "drainage": drainage,
        "basins": basins,
        "d8_area_km2": acc_d8 * spacing_m * spacing_m / 1e6,
        "mfd_area_km2": acc_mfd * spacing_m * spacing_m / 1e6,
        "fill_delta_m": fill_delta,
        "d8_directions": np.asarray(pf_d8),
    }


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def load_asset(asset: Any, inventory: dict[str, Any], preferred_variant: str | None = None) -> tuple[np.ndarray, dict[str, Any]]:
    record = next((alias for alias in asset.aliases if alias.variant == preferred_variant), asset.canonical)
    manifest = json.loads(record.manifest_path.read_text())
    raw = np.memmap(record.elevation_path, dtype="<f4", mode="r", shape=(record.height, record.width))
    elevation = reach.transform_heightfield(raw, manifest)
    return elevation, {
        "inventory": inventory,
        "variant": record.variant,
        "seed": record.seed,
        "manifest_path": str(record.manifest_path.relative_to(ROOT)),
        "manifest_sha256": record.manifest_sha256,
        "elevation_path": str(record.elevation_path.relative_to(ROOT)),
        "elevation_sha256": record.elevation_sha256,
        "height_transform": manifest["height"],
        "shape_zx": [record.height, record.width],
        "spacing_m": record.spacing_m,
        "aliases": [str(alias.manifest_path.relative_to(ROOT)) for alias in asset.aliases],
    }


def load_first_map() -> tuple[np.ndarray, dict[str, Any]]:
    assets, inventory = reach.load_pinned_assets(SOURCE_ROOT)
    matches = [
        asset for asset in assets
        if any(alias.variant == "rolling-wet-lowland" and alias.seed == 12345 for alias in asset.aliases)
    ]
    if len(matches) != 1:
        raise RuntimeError(f"expected one pinned lowland payload, found {len(matches)}")
    return load_asset(matches[0], inventory, preferred_variant="rolling-wet-lowland")


def _hillshade(elevation: np.ndarray) -> np.ndarray:
    dz, dx = np.gradient(np.asarray(elevation, dtype=np.float32), SPACING_M)
    slope = np.sqrt(dx * dx + dz * dz)
    light = (0.65 - 0.48 * dx + 0.38 * dz) / np.sqrt(1.0 + slope * slope)
    normalized = np.clip((light - 0.18) / 0.8, 0.0, 1.0)
    return (55.0 + 180.0 * normalized).astype(np.uint8)


def _panel(base: np.ndarray, field: np.ndarray, kind: str, boxes: tuple[tuple[int, int, int, int, str], ...], output_width: int = 900) -> Image.Image:
    height, width = base.shape
    scale = output_width / width
    out_height = int(round(height * scale))
    background = np.stack((base, base, base), axis=-1)
    if kind == "area":
        value = np.clip(np.log10(np.maximum(field, 0.001) / 0.25) / math.log10(16 / 0.25), 0, 1)
        strength = np.where(field >= REVIEW_THRESHOLD_KM2[0], 0.34 + 0.55 * value, 0.0)[..., None]
        ink = np.stack((25 + 45 * value, 125 + 70 * value, 215 + 10 * value), axis=-1)
    elif kind == "fill":
        value = np.clip(np.log1p(field) / math.log1p(10.0), 0, 1)
        strength = np.where(field >= FILL_COMPONENT_EPS_M, 0.25 + 0.7 * value, 0.0)[..., None]
        ink = np.stack((235 + 15 * value, 190 - 125 * value, 55 - 30 * value), axis=-1)
    else:
        raise ValueError(kind)
    rgb = np.clip(background * (1.0 - strength) + ink * strength, 0, 255).astype(np.uint8)
    if kind == "area":
        # Display-only three-pixel support keeps major trunks visible after
        # downsampling; it never enters candidate metrics or Fluid inputs.
        major = ndimage.maximum_filter(field, size=3) >= REVIEW_THRESHOLD_KM2[-1]
        rgb[major] = (15, 71, 196)
    picture = Image.fromarray(rgb, mode="RGB").resize((output_width, out_height), Image.Resampling.BOX)
    draw = ImageDraw.Draw(picture)
    for x, z, box_width, box_height, label in boxes:
        draw.rectangle((round(x * scale), round(z * scale), round((x + box_width) * scale), round((z + box_height) * scale)), outline="#ff67bd", width=2)
        draw.text((round(x * scale) + 3, round(z * scale) + 3), label, fill="white", stroke_width=2, stroke_fill="black")
    return picture


def write_capture(output: Path, elevation: np.ndarray, result: dict[str, Any], boxes: tuple[tuple[int, int, int, int, str], ...] = ()) -> None:
    shade = _hillshade(elevation)
    entries = (
        ("Connected D8 contributing area", result["d8_area_km2"], "area"),
        ("MFD sensitivity - unresolved sinks", result["mfd_area_km2"], "area"),
        ("Derived fill depth (basin proxy)", result["fill_delta_m"], "fill"),
    )
    panels = [_panel(shade, np.asarray(field), kind, boxes) for _, field, kind in entries]
    width, height = panels[0].size
    sheet = Image.new("RGB", (width * len(panels), height + 72), "#202830")
    sheet_draw = ImageDraw.Draw(sheet)
    for index, ((title, _, kind), panel) in enumerate(zip(entries, panels, strict=True)):
        sheet.paste(panel, (index * width, 52))
        sheet_draw.text((index * width + 12, 10), title, fill="white")
        legend = "cyan: >=0.25 km2, blue: >=16 km2 (thickened for display)" if kind == "area" else "yellow/red: derived fill; no lake claim"
        sheet_draw.text((index * width + 12, 30), legend, fill="#d5e4eb")
    footer = "Pink rectangles: prior morphology-only reaches; map edges are not natural outlets." if boxes else "Map edges are artificial outlets; colored drainage is contributing area, not known water."
    sheet_draw.text((12, height + 56), footer, fill="white")
    sheet.save(output / "contact-sheet.png")
    for (title, _, _), panel in zip(entries, panels, strict=True):
        panel.save(output / (title.lower().replace(" ", "-").replace("(", "").replace(")", "") + ".png"))


def write_case(output: Path, elevation: np.ndarray, source: dict[str, Any], status: str, boxes: tuple[tuple[int, int, int, int, str], ...] = ()) -> dict[str, Any]:
    start = time.monotonic()
    result = read_hydrology(elevation, boxes=boxes)
    output.mkdir(parents=True)
    write_capture(output, elevation, result, boxes=boxes)
    document = {
        "schema": SCHEMA,
        "status": status,
        "source": source,
        "method": {
            "primary_library": "pyflwdir",
            "routing": "pyflwdir priority flood and through-flat D8 direction; Pysheds MFD is an unvalidated sensitivity view",
            "basin": "positive derived fill patches from raw elevation, not water or lake labels",
            "local_coordinates_only": True,
            "review_thresholds_km2": list(REVIEW_THRESHOLD_KM2),
            "tool_versions": {name: importlib.metadata.version(name) for name in ("pyflwdir", "pysheds", "numpy", "scipy", "scikit-image", "rasterio", "pyproj", "pillow")},
            "script_sha256": _sha256(Path(__file__)),
        },
        "drainage": result["drainage"],
        "basins": result["basins"],
        "elapsed_seconds": time.monotonic() - start,
        "capture_sha256": _sha256(output / "contact-sheet.png"),
    }
    (output / "result.json").write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")
    return document


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--all-pinned", action="store_true", help="read the full 12-payload frozen corpus after the first-map gate")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    if output.exists():
        raise SystemExit(f"refusing to overwrite existing output: {output}")
    if not args.all_pinned:
        elevation, source = load_first_map()
        document = write_case(output, elevation, source, "one-map gate; no corpus conclusion", PRIOR_REACH_BOXES)
        print(json.dumps({"output": str(output), "elapsed_seconds": document["elapsed_seconds"], "drainage": document["drainage"], "basin_component_count": document["basins"]["component_count"]}, indent=2))
        return

    assets, inventory = reach.load_pinned_assets(SOURCE_ROOT)
    output.mkdir(parents=True)
    rows = []
    for index, asset in enumerate(assets, start=1):
        lowland = any(alias.variant == "rolling-wet-lowland" and alias.seed == 12345 for alias in asset.aliases)
        elevation, source = load_asset(asset, inventory, preferred_variant="rolling-wet-lowland" if lowland else None)
        name = "".join(character if character.isalnum() or character == "-" else "-" for character in source["variant"])
        case_dir = output / f"{name}-seed{source['seed']}-{source['elevation_sha256'][:8]}"
        document = write_case(case_dir, elevation, source, "corpus discovery only; no fixture promotion", PRIOR_REACH_BOXES if lowland else ())
        row = {
            "case": case_dir.name,
            "elevation_sha256": source["elevation_sha256"],
            "variant": source["variant"],
            "seed": source["seed"],
            "d8_max_contributing_area_km2": document["drainage"]["d8_max_contributing_area_km2"],
            "d8_interior_terminal_count": document["drainage"]["d8_interior_terminal_count"],
            "d8_terminal_area_balance_relative": document["drainage"]["d8_terminal_area_balance_relative"],
            "fill_fraction": document["basins"]["filled_cell_fraction"],
            "fill_max_m": document["basins"]["fill_max_m"],
            "fill_volume_m3": document["basins"]["fill_volume_m3"],
            "largest_basin_proxy": document["basins"]["top_components"][0] if document["basins"]["top_components"] else None,
            "prior_morphology_boxes": document["drainage"]["prior_morphology_boxes"],
            "capture": str((case_dir / "contact-sheet.png").relative_to(ROOT)),
            "result": str((case_dir / "result.json").relative_to(ROOT)),
        }
        rows.append(row)
        print(f"[{index}/{len(assets)}] {row['case']}: max area {row['d8_max_contributing_area_km2']:.1f} km2; fill {row['fill_fraction']:.1%}", flush=True)
    summary = {
        "schema": "cubey.fluid25d.terrain_hydro_corpus.v1",
        "status": "full cached source corpus read as topographic hypotheses, not water truth or fixture selection",
        "inventory": inventory,
        "case_count": len(rows),
        "cases": rows,
        "script_sha256": _sha256(Path(__file__)),
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"output": str(output), "case_count": len(rows)}, indent=2))


if __name__ == "__main__":
    main()
