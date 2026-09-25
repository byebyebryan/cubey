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
"""Bounded early-stage pool morphology screen on the pinned Terrain Diffusion corpus.

This is an elevation-only diagnostic. Filled elevations are a derived routing
copy; stage footprints and rain budgets below are not observed water or a Fluid
solver result.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
import time
from pathlib import Path
from typing import Any

import numpy as np
import pyflwdir
from scipy import ndimage

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import terrain_hydro_read_v1 as hydro  # noqa: E402
import terrain_reach_scan_v1 as reach  # noqa: E402


ROOT = Path(__file__).resolve().parents[3]
SOURCE_ROOT = ROOT / "cache/terrain/sources/v1"
CORPUS_SUMMARY = ROOT / "outputs/fluid/terrain-hydro-read-v1-corpus-visible-20260924/summary.json"
DEFAULT_OUTPUT = ROOT / "outputs/fluid/rain-pool-screen-v1-20260924"
SCHEMA = "cubey.fluid25d.terrain_rain_pool_screen.v1"
EXPECTED_UNIQUE = reach.EXPECTED_UNIQUE_PAYLOADS
CELL_AREA_M2 = reach.NATIVE_SPACING_M**2
RAIN_DEPTH_M = 0.01  # unchanged 60 mm/h for 600 s
STAGE_DEPTHS_M = (0.05, 0.10)
MIN_COMPONENT_CELLS = 25
VISUALLY_PROMISING_CELLS = 100
DONOR_DILATION_CELLS = 3
CROP_WIDTH = 256
CROP_HEIGHT = 128
CROP_MARGIN_CELLS = DONOR_DILATION_CELLS + 1
EIGHT_CONNECTED = np.ones((3, 3), dtype=np.uint8)


class PoolScreenError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def stable_inventory_sha256(items: list[dict[str, Any]]) -> str:
    """Hash canonical source identity rows independent of enumeration order."""
    normalized = []
    for item in items:
        digest = str(item["elevation_sha256"])
        if len(digest) != 64 or any(char not in "0123456789abcdef" for char in digest):
            raise ValueError("elevation_sha256 must be a lowercase SHA-256 hex digest")
        normalized.append(
            {
                "elevation_sha256": digest,
                "manifest_sha256": str(item["manifest_sha256"]),
                "variant": str(item["variant"]),
                "seed": int(item["seed"]),
                "shape_zx": [int(value) for value in item["shape_zx"]],
                "spacing_m": float(item["spacing_m"]),
            }
        )
    normalized.sort(key=lambda row: (row["elevation_sha256"], row["manifest_sha256"]))
    payload = json.dumps(normalized, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def load_corpus_cases(
    summary_path: Path = CORPUS_SUMMARY,
) -> tuple[dict[str, dict[str, Any]], str | None, str]:
    """Load prior map labels only as an optional, non-authoritative cross-check."""
    if not summary_path.is_file():
        return {}, None, "not_present_optional"
    summary_hash = sha256_file(summary_path)
    try:
        summary = json.loads(summary_path.read_text())
    except (OSError, json.JSONDecodeError):
        return {}, summary_hash, "unreadable_optional"
    if not isinstance(summary, dict):
        return {}, summary_hash, "invalid_root_optional"
    cases = summary.get("cases")
    if summary.get("case_count") != 12 or not isinstance(cases, list) or len(cases) != 12:
        return {}, summary_hash, "invalid_case_count_optional"
    by_hash: dict[str, dict[str, Any]] = {}
    for case in cases:
        if not isinstance(case, dict):
            return {}, summary_hash, "invalid_case_optional"
        digest = case.get("elevation_sha256")
        if not isinstance(digest, str):
            return {}, summary_hash, "invalid_hash_optional"
        if digest in by_hash:
            return {}, summary_hash, "duplicate_hash_optional"
        by_hash[digest] = case
    if set(by_hash) != EXPECTED_UNIQUE:
        return {}, summary_hash, "hash_inventory_mismatch_optional"
    # Result files are useful secondary evidence, but their absence must not
    # prevent a cache-only reproduction. Report mismatches without overriding
    # the pinned cache inventory as the identity authority.
    result_checks = []
    for digest, case in by_hash.items():
        relative = case.get("result")
        result_path = ROOT / relative if isinstance(relative, str) else None
        if result_path is None or not result_path.is_file():
            result_checks.append("missing")
            continue
        try:
            result = json.loads(result_path.read_text())
        except (OSError, json.JSONDecodeError):
            result_checks.append("unreadable")
            continue
        if not isinstance(result, dict):
            result_checks.append("invalid")
            continue
        result_source = result.get("source")
        if not isinstance(result_source, dict):
            result_checks.append("invalid")
            continue
        result_checks.append(
            "match" if result_source.get("elevation_sha256") == digest else "mismatch"
        )
    if "mismatch" in result_checks:
        status = "summary_hashes_match_result_mismatch_optional"
    elif any(check != "match" for check in result_checks):
        status = "summary_hashes_match_result_files_partial_optional"
    else:
        status = "summary_and_result_hashes_match"
    return by_hash, summary_hash, status


def load_pinned_cache_assets() -> tuple[list[Any], dict[str, Any]]:
    """Use immutable cache manifests/payload hashes as the authoritative pin."""
    records = reach.PINNED.discover_manifests(SOURCE_ROOT)
    assets = reach.PINNED.deduplicate_assets(records)
    reach.validate_pinned_inventory(assets)
    expected_hash_list = "\n".join(sorted(EXPECTED_UNIQUE)) + "\n"
    return assets, {
        "source_root": str(SOURCE_ROOT.relative_to(ROOT)),
        "manifest_count": len(records),
        "unique_payload_count": len(assets),
        "deduplicated_alias_count": len(records) - len(assets),
        "expected_unique_payload_set_sha256": hashlib.sha256(expected_hash_list.encode("ascii")).hexdigest(),
        "identity_authority": "pinned 12 elevation payload hashes verified against current cache manifests",
    }


def crop_for_bbox(
    bbox_xz: tuple[int, int, int, int],
    map_width: int,
    map_height: int,
    crop_width: int = CROP_WIDTH,
    crop_height: int = CROP_HEIGHT,
    margin: int = CROP_MARGIN_CELLS,
) -> dict[str, Any]:
    """Find a deterministic crop containing an inclusive bbox and cell margin."""
    xmin, zmin, xmax, zmax = bbox_xz
    if map_width < crop_width or map_height < crop_height:
        return {"fits": False, "reason": "crop_larger_than_source"}
    if xmax < xmin or zmax < zmin:
        raise ValueError("invalid inclusive bbox")
    if xmax - xmin + 1 + 2 * margin > crop_width or zmax - zmin + 1 + 2 * margin > crop_height:
        return {"fits": False, "reason": "footprint_plus_margin_exceeds_crop"}

    x_low = max(0, xmax + margin - crop_width + 1)
    x_high = min(xmin - margin, map_width - crop_width)
    z_low = max(0, zmax + margin - crop_height + 1)
    z_high = min(zmin - margin, map_height - crop_height)
    if x_low > x_high or z_low > z_high:
        return {"fits": False, "reason": "source_edge_prevents_requested_margin"}

    x_centered = int(round((xmin + xmax + 1 - crop_width) / 2.0))
    z_centered = int(round((zmin + zmax + 1 - crop_height) / 2.0))
    x = min(max(x_centered, x_low), x_high)
    z = min(max(z_centered, z_low), z_high)
    actual_margin = min(xmin - x, zmin - z, x + crop_width - 1 - xmax, z + crop_height - 1 - zmax)
    return {
        "fits": actual_margin >= margin,
        "reason": None if actual_margin >= margin else "margin_check_failed",
        "x": int(x),
        "z": int(z),
        "width": int(crop_width),
        "height": int(crop_height),
        "minimum_footprint_to_crop_edge_cells": int(actual_margin),
        "required_margin_cells": int(margin),
    }


def dilation_area_cells(
    stage_label: np.ndarray,
    label_id: int,
    bbox: tuple[slice, slice],
    iterations: int = DONOR_DILATION_CELLS,
) -> int:
    """Count a Chebyshev-radius donor apron, clipped to map edge."""
    z_slice, x_slice = bbox
    if iterations < 0:
        raise ValueError("dilation iterations must be non-negative")
    z0 = max(0, int(z_slice.start) - iterations)
    z1 = min(stage_label.shape[0], int(z_slice.stop) + iterations)
    x0 = max(0, int(x_slice.start) - iterations)
    x1 = min(stage_label.shape[1], int(x_slice.stop) + iterations)
    patch = stage_label[z0:z1, x0:x1] == label_id
    dilated = ndimage.binary_dilation(
        patch,
        structure=EIGHT_CONNECTED,
        iterations=iterations,
        border_value=0,
    )
    return int(np.count_nonzero(dilated))


def scan_stage_arrays(
    elevation_m: np.ndarray,
    filled_m: np.ndarray,
    *,
    spacing_m: float = reach.NATIVE_SPACING_M,
    stage_depths_m: tuple[float, ...] = STAGE_DEPTHS_M,
) -> dict[str, Any]:
    """Measure global-floor-connected low stages within >1 cm fill components.

    A stage component is retained only if it touches a global raw-minimum cell
    of its derived-fill component. Secondary local minima are counted only by
    the explicit limitation in the output; they are not independent seeds.
    """
    raw = np.asarray(elevation_m, dtype=np.float32)
    filled = np.asarray(filled_m, dtype=np.float32)
    if raw.ndim != 2 or raw.shape != filled.shape or min(raw.shape) < 3:
        raise ValueError("raw and filled surfaces must be same-shape 2D arrays at least 3x3")
    if not np.isfinite(raw).all() or not np.isfinite(filled).all():
        raise ValueError("raw and filled surfaces must be finite")
    if not math.isfinite(spacing_m) or spacing_m <= 0:
        raise ValueError("spacing_m must be finite and positive")
    if not stage_depths_m or any(not math.isfinite(value) or value <= 0 for value in stage_depths_m):
        raise ValueError("stage depths must be finite and positive")

    delta = np.maximum(filled - raw, 0.0)
    fill_mask = delta > hydro.FILL_COMPONENT_EPS_M
    labels, component_count = ndimage.label(fill_mask, structure=EIGHT_CONNECTED)
    component_sizes = np.bincount(labels.ravel(), minlength=component_count + 1)
    if component_count == 0:
        return {
            "component_count": 0,
            "fill_cells_ge_25": 0,
            "stage_counts": {
                f"{depth:.2f}m": {
                    "connected_footprints_touching_global_floor": 0,
                    "footprints_ge_25": 0,
                    "footprints_ge_100": 0,
                    "optimistic_budget_pass_ge_25": 0,
                }
                for depth in stage_depths_m
            },
            "candidates": [],
        }

    component_floor = np.full(component_count + 1, np.inf, dtype=np.float32)
    np.minimum.at(component_floor, labels.ravel(), raw.ravel())
    floor_cells = fill_mask & (raw == component_floor[labels])
    spill_floor_min = np.full(component_count + 1, np.inf, dtype=np.float32)
    spill_floor_max = np.full(component_count + 1, -np.inf, dtype=np.float32)
    np.minimum.at(spill_floor_min, labels[floor_cells], filled[floor_cells])
    np.maximum.at(spill_floor_max, labels[floor_cells], filled[floor_cells])
    fill_surface_min = np.full(component_count + 1, np.inf, dtype=np.float32)
    fill_surface_max = np.full(component_count + 1, -np.inf, dtype=np.float32)
    np.minimum.at(fill_surface_min, labels.ravel(), filled.ravel())
    np.maximum.at(fill_surface_max, labels.ravel(), filled.ravel())
    component_slices = ndimage.find_objects(labels)
    touches_edge = np.zeros(component_count + 1, dtype=bool)
    height, width = raw.shape
    for component_id, bbox in enumerate(component_slices, start=1):
        if bbox is None:
            continue
        z_slice, x_slice = bbox
        touches_edge[component_id] = (
            z_slice.start == 0 or z_slice.stop == height or x_slice.start == 0 or x_slice.stop == width
        )

    stage_counts: dict[str, dict[str, int]] = {}
    candidates: list[dict[str, Any]] = []
    cell_area = spacing_m * spacing_m

    for stage_depth in stage_depths_m:
        stage_level = component_floor + np.float32(stage_depth)
        below_spill = stage_level < spill_floor_min
        eligible = fill_mask & below_spill[labels] & (raw <= stage_level[labels])
        stage_labels, stage_count = ndimage.label(eligible, structure=EIGHT_CONNECTED)
        stage_sizes = np.bincount(stage_labels.ravel(), minlength=stage_count + 1)

        volume_per_cell = np.maximum(stage_level[labels] - raw, 0.0) * cell_area
        stage_volumes = np.bincount(
            stage_labels.ravel(), weights=volume_per_cell.ravel(), minlength=stage_count + 1
        )
        floor_stage = stage_labels[floor_cells]
        floor_component = labels[floor_cells]
        floor_counts = np.bincount(floor_stage, minlength=stage_count + 1)
        floor_component_sums = np.bincount(
            floor_stage, weights=floor_component, minlength=stage_count + 1
        )
        floor_flat_indices = np.flatnonzero(floor_cells.ravel())
        floor_seed_by_stage = np.full(stage_count + 1, raw.size, dtype=np.int64)
        np.minimum.at(floor_seed_by_stage, stage_labels.ravel()[floor_flat_indices], floor_flat_indices)
        stage_to_component = np.zeros(stage_count + 1, dtype=np.int32)
        seeded = floor_counts > 0
        stage_to_component[seeded] = np.rint(
            floor_component_sums[seeded] / floor_counts[seeded]
        ).astype(np.int32)

        candidate_ids = np.flatnonzero(
            seeded & (stage_sizes >= MIN_COMPONENT_CELLS) & (np.arange(stage_count + 1) > 0)
        )
        stage_slices = ndimage.find_objects(stage_labels)
        key = f"{stage_depth:.2f}m"
        stage_counts[key] = {
            "connected_footprints_touching_global_floor": int(np.count_nonzero(seeded[1:])),
            "footprints_ge_25": int(candidate_ids.size),
            "footprints_ge_100": int(np.count_nonzero(stage_sizes[candidate_ids] >= VISUALLY_PROMISING_CELLS)),
            "optimistic_budget_pass_ge_25": 0,
        }

        for stage_id_value in candidate_ids:
            stage_id = int(stage_id_value)
            component_id = int(stage_to_component[stage_id])
            bbox = stage_slices[stage_id - 1]
            if bbox is None:
                continue
            z_slice, x_slice = bbox
            xmin, zmin = int(x_slice.start), int(z_slice.start)
            xmax, zmax = int(x_slice.stop - 1), int(z_slice.stop - 1)
            footprint_cells = int(stage_sizes[stage_id])
            apron_cells = dilation_area_cells(stage_labels, stage_id, bbox)
            stage_storage_m3 = float(stage_volumes[stage_id])
            direct_rain_budget_m3 = float(footprint_cells * cell_area * RAIN_DEPTH_M)
            rain_budget_m3 = float(apron_cells * cell_area * RAIN_DEPTH_M)
            budget_ratio = rain_budget_m3 / stage_storage_m3 if stage_storage_m3 > 0 else math.inf
            floor_flat_index = int(floor_seed_by_stage[stage_id])
            floor_z, floor_x = divmod(floor_flat_index, width)
            component_bbox = component_slices[component_id - 1]
            if component_bbox is None:
                raise PoolScreenError("stage seed refers to a missing derived-fill component")
            component_z, component_x = component_bbox
            crop = crop_for_bbox(
                (xmin, zmin, xmax, zmax), width, height,
                margin=CROP_MARGIN_CELLS,
            )
            fill_floor_depth = float(spill_floor_min[component_id] - component_floor[component_id])
            footprint_touches_edge = (
                xmin == 0 or zmin == 0 or xmax == width - 1 or zmax == height - 1
            )
            candidate = {
                "stage_depth_m": float(stage_depth),
                "component_label": component_id,
                "component_cells": int(component_sizes[component_id]),
                "component_touches_source_edge": bool(touches_edge[component_id]),
                "component_bbox_xzwh": [
                    int(component_x.start), int(component_z.start),
                    int(component_x.stop - component_x.start), int(component_z.stop - component_z.start),
                ],
                "component_global_raw_floor_m": float(component_floor[component_id]),
                "global_floor_cell_xz": [int(floor_x), int(floor_z)],
                "derived_spill_proxy_at_floor_m": float(spill_floor_min[component_id]),
                "derived_spill_proxy_floor_cell_range_m": [
                    float(spill_floor_min[component_id]), float(spill_floor_max[component_id])
                ],
                "derived_fill_surface_range_m": [
                    float(fill_surface_min[component_id]), float(fill_surface_max[component_id])
                ],
                "floor_to_spill_proxy_depth_m": fill_floor_depth,
                "stage_elevation_m": float(component_floor[component_id] + stage_depth),
                "spill_margin_above_stage_m": float(
                    spill_floor_min[component_id] - (component_floor[component_id] + stage_depth)
                ),
                "stage_footprint_bbox_xzwh": [xmin, zmin, xmax - xmin + 1, zmax - zmin + 1],
                "stage_footprint_cells": footprint_cells,
                "stage_footprint_area_m2": float(footprint_cells * cell_area),
                "stage_storage_proxy_m3": stage_storage_m3,
                "direct_footprint_rain_budget_m3": direct_rain_budget_m3,
                "direct_budget_to_stage_storage_ratio": float(
                    direct_rain_budget_m3 / stage_storage_m3 if stage_storage_m3 > 0 else math.inf
                ),
                "three_cell_dilated_source_cells": int(apron_cells),
                "optimistic_rain_budget_m3": rain_budget_m3,
                "optimistic_budget_to_stage_storage_ratio": float(budget_ratio),
                "optimistic_budget_pass": bool(budget_ratio >= 1.0),
                "visually_promising_ge_100_cells": bool(footprint_cells >= VISUALLY_PROMISING_CELLS),
                "stage_footprint_touches_source_edge": bool(footprint_touches_edge),
                "crop": crop,
                "optimistic_size_budget_crop_gate_pass": bool(
                    footprint_cells >= MIN_COMPONENT_CELLS
                    and budget_ratio >= 1.0
                    and crop.get("fits")
                    and not touches_edge[component_id]
                    and not footprint_touches_edge
                ),
            }
            candidates.append(candidate)
            if candidate["optimistic_budget_pass"]:
                stage_counts[key]["optimistic_budget_pass_ge_25"] += 1

        del stage_labels, stage_sizes, stage_volumes, stage_slices, volume_per_cell, eligible

    fill_cells_ge_25 = int(np.count_nonzero(component_sizes[1:] >= MIN_COMPONENT_CELLS))
    return {
        "component_count": int(component_count),
        "fill_cells_ge_25": fill_cells_ge_25,
        "stage_counts": stage_counts,
        "candidates": candidates,
    }


def candidate_sort_key(candidate: dict[str, Any]) -> tuple[Any, ...]:
    return (
        -int(candidate["optimistic_size_budget_crop_gate_pass"]),
        -int(candidate["visually_promising_ge_100_cells"]),
        -int(candidate["stage_footprint_cells"]),
        -float(candidate["optimistic_budget_to_stage_storage_ratio"]),
        float(candidate["stage_depth_m"]),
        str(candidate["elevation_sha256"]),
        int(candidate["component_label"]),
    )


def screen_asset(
    asset: Any,
    inventory: dict[str, Any],
    case: dict[str, Any] | None,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    started = time.monotonic()
    elevation, source = hydro.load_asset(asset, inventory)
    filled, _directions = pyflwdir.dem.fill_depressions(
        np.asarray(elevation, dtype=np.float32).copy(), outlets="edge", connectivity=8
    )
    # Reuse the reader's exact >1 cm, 8-connected derived-fill definition and
    # cross-check our label count against its top-component summary.
    basin_summary, _fill_delta = hydro.basin_proxy(elevation, filled, source["spacing_m"])
    scanned = scan_stage_arrays(elevation, filled, spacing_m=source["spacing_m"])
    if scanned["component_count"] != basin_summary["component_count"]:
        raise PoolScreenError("derived-fill component count differs from terrain_hydro_read_v1")

    case_name = (
        str(case["case"])
        if case is not None and isinstance(case.get("case"), str)
        else f"{source['variant'] or 'terrain'}-seed{source['seed']}-{source['elevation_sha256'][:8]}"
    )
    identity = {
        "case": case_name,
        "elevation_sha256": source["elevation_sha256"],
        "manifest_path": source["manifest_path"],
        "manifest_sha256": source["manifest_sha256"],
        "aliases": source["aliases"],
        "variant": source["variant"],
        "seed": source["seed"],
        "shape_zx": source["shape_zx"],
        "spacing_m": source["spacing_m"],
        "derived_fill_component_count": scanned["component_count"],
        "fill_components_ge_25_cells": scanned["fill_cells_ge_25"],
        "stage_counts": scanned["stage_counts"],
        "elapsed_seconds": float(time.monotonic() - started),
    }
    candidates = []
    for candidate in scanned["candidates"]:
        candidates.append({**identity_for_candidate(identity), **candidate})
    # Crop relief is useful for the shortlist but expensive if repeated for
    # every small stage pocket. Every corpus-wide top-24 candidate is among
    # the top 24 within its own map under the identical deterministic rank.
    for candidate in sorted(candidates, key=candidate_sort_key)[:24]:
        crop = candidate["crop"]
        if not crop.get("fits"):
            crop["raw_relief_p05_p95_m"] = None
            crop["raw_min_max_m"] = None
            continue
        cx, cz = int(crop["x"]), int(crop["z"])
        crop_raw = elevation[cz : cz + CROP_HEIGHT, cx : cx + CROP_WIDTH]
        p05, p95 = np.percentile(crop_raw, (5.0, 95.0))
        crop["raw_relief_p05_p95_m"] = float(p95 - p05)
        crop["raw_min_max_m"] = [float(crop_raw.min()), float(crop_raw.max())]
        crop["stage_footprint_min_margin_to_crop_edge_cells"] = int(
            crop["minimum_footprint_to_crop_edge_cells"]
        )
    return identity, candidates


def identity_for_candidate(identity: dict[str, Any]) -> dict[str, Any]:
    return {
        "case": identity["case"],
        "elevation_sha256": identity["elevation_sha256"],
        "variant": identity["variant"],
        "seed": identity["seed"],
        "spacing_m": identity["spacing_m"],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    output_dir = args.output_dir.resolve()
    if output_dir.exists():
        raise SystemExit(f"refusing to overwrite existing output: {output_dir}")

    cases_by_hash, corpus_summary_sha, corpus_crosscheck_status = load_corpus_cases()
    assets, inventory = load_pinned_cache_assets()
    if len(assets) != 12 or {asset.elevation_sha256 for asset in assets} != EXPECTED_UNIQUE:
        raise PoolScreenError("cached source inventory differs from the pinned 12-payload set")

    asset_rows = []
    for asset in assets:
        record = asset.canonical
        asset_rows.append(
            {
                "elevation_sha256": asset.elevation_sha256,
                "manifest_sha256": record.manifest_sha256,
                "variant": record.variant,
                "seed": record.seed,
                "shape_zx": [record.height, record.width],
                "spacing_m": record.spacing_m,
            }
        )
    inventory_hash = stable_inventory_sha256(asset_rows)

    per_map = []
    all_candidates: list[dict[str, Any]] = []
    for map_index, asset in enumerate(assets, start=1):
        case = cases_by_hash.get(asset.elevation_sha256)
        case_label = case["case"] if case is not None else f"{asset.canonical.variant}-seed{asset.canonical.seed}"
        print(f"[{map_index}/12] {case_label} {asset.elevation_sha256[:12]}", flush=True)
        identity, candidates = screen_asset(asset, inventory, case)
        per_map.append(identity)
        all_candidates.extend(candidates)
        print(
            f"  components={identity['derived_fill_component_count']} "
            f"candidate_sites={len(candidates)} elapsed={identity['elapsed_seconds']:.1f}s",
            flush=True,
        )

    all_candidates.sort(key=candidate_sort_key)
    plausible = [candidate for candidate in all_candidates if candidate["optimistic_size_budget_crop_gate_pass"]]
    visual_promising = [candidate for candidate in plausible if candidate["visually_promising_ge_100_cells"]]
    screen = {
        "schema": SCHEMA,
        "status": "completed bounded morphology screen; not a Fluid pilot or product fixture selection",
        "source_corpus": {
            "pinned_unique_payload_count": len(assets),
            "expected_elevation_sha256_set": sorted(EXPECTED_UNIQUE),
            "canonical_inventory_sha256": inventory_hash,
            "terrain_site_scan_inventory": inventory,
            "hydro_read_corpus_summary": str(CORPUS_SUMMARY.relative_to(ROOT)),
            "hydro_read_corpus_summary_sha256": corpus_summary_sha,
            "hydro_read_corpus_crosscheck_status": corpus_crosscheck_status,
        },
        "method": {
            "derived_surface": "pyflwdir DEM priority-fill copy with 8-connectivity and map-edge outlets; raw elevation unchanged",
            "fill_components": f"8-connected cells where max(filled - raw, 0) > {hydro.FILL_COMPONENT_EPS_M:.2f} m",
            "stage_rule": "for each derived-fill component, seed only its global raw-minimum cells; stage is floor + 0.05 m or +0.10 m; retain only if strictly below minimum filled elevation at the floor; measure the 8-connected sublevel component containing a global-floor cell",
            "stage_storage": "sum(stage elevation - raw elevation) over connected stage footprint times native cell area; elevation-only volume proxy",
            "rain_budget": "optimistic upper bound: unchanged 0.010 m rain over footprint plus its clipped Chebyshev-radius-3 cell dilation (90 m); assumes 100% local runoff capture, no infiltration, and no loss",
            "visual_size_gates": {"minimum_cells": MIN_COMPONENT_CELLS, "visually_promising_cells": VISUALLY_PROMISING_CELLS},
            "crop": {
                "grid": [CROP_WIDTH, CROP_HEIGHT],
                "spacing_m": reach.NATIVE_SPACING_M,
                "margin_cells": CROP_MARGIN_CELLS,
                "margin_meaning": "contains 3-cell donor apron plus at least one boundary cell",
                "relief_metric": "raw crop p95 - p05",
            },
            "secondary_minimum_limitation": "Only the global raw-minimum plateau of each >1 cm derived-fill component is seeded. Secondary local minima do not receive their own floor-relative stages; nested/local subbasins can therefore be missed. This is not mathematical exhaustion.",
            "interpretation_caveats": [
                "filled terrain is an analytical copy and is not water or a lake label",
                "the 3-cell dilation is not a routed contributing area; its rain budget deliberately overstates likely local capture",
                "source-edge components and stage footprints are flagged and excluded from the crop/optimistic gate",
                "crop perimeter behavior, flow speed, losses, and visible water are not simulated here",
            ],
            "script_sha256": sha256_file(Path(__file__)),
        },
        "per_map": per_map,
        "counts": {
            "stage_footprints_ge_25_cells": sum(item["stage_counts"]["0.05m"]["footprints_ge_25"] + item["stage_counts"]["0.10m"]["footprints_ge_25"] for item in per_map),
            "stage_footprints_ge_100_cells": sum(item["stage_counts"]["0.05m"]["footprints_ge_100"] + item["stage_counts"]["0.10m"]["footprints_ge_100"] for item in per_map),
            "optimistic_size_budget_crop_gate_pass": len(plausible),
            "visually_promising_ge_100_gate_pass": len(visual_promising),
        },
        "top_3_optimistic_visual_candidates": plausible[:3],
        "top_3_ge_100_visual_candidates": visual_promising[:3],
        "top_ranked_candidates": all_candidates[:24],
    }

    output_dir.mkdir(parents=True, exist_ok=False)
    output_path = output_dir / "screen.json"
    output_path.write_text(json.dumps(screen, indent=2, sort_keys=True) + "\n")
    print(
        json.dumps(
            {
                "output": str(output_path),
                "inventory_sha256": inventory_hash,
                "maps": len(per_map),
                "candidate_sites_ge_25": len(all_candidates),
                "optimistic_crop_budget_passes": len(plausible),
                "visually_promising_passes": len(visual_promising),
            },
            sort_keys=True,
        ),
        flush=True,
    )


if __name__ == "__main__":
    main()
