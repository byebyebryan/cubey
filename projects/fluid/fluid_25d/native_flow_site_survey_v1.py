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
"""Offline, immutable-terrain survey for sustained-flow pilot site candidates.

Priority-flood routing and contributing area are locators only. Raw terrain,
equilibrium fill deltas, and cropped flow are reported as separate evidence;
this tool does not prepare solver inputs or simulate water.
"""

from __future__ import annotations

import argparse
import hashlib
import heapq
import importlib.metadata
import json
import math
import sys
import time
from pathlib import Path
from typing import Any

import numpy as np
import pyflwdir
from PIL import Image, ImageDraw, ImageFont
from rasterio.transform import Affine
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "projects/fluid/fluid_25d"))
import terrain_hydro_read_v1 as hydro  # noqa: E402
import terrain_reach_scan_v1 as reach  # noqa: E402

SPACING_M = 30.0
CELL_AREA_M2 = SPACING_M**2
FILL_COMPONENT_EPS_M = 0.01
SOURCE_OFFSETS_XZ = ((0, 0), (0, -1), (0, 1), (1, 0), (-1, 0))
FLOW_OFFSETS_DZDX = {64: (-1, 0), 1: (0, 1), 4: (1, 0), 16: (0, -1)}
DIAGONAL_CODES = frozenset({2, 8, 32, 128})
SAMPLE_BLOCK_CELLS = 128
MIN_LOCATOR_AREA_KM2 = 0.1
MAX_REACH_EVALUATIONS_PER_MAP = 20
TARGET_REACH_M = 1_200.0
MIN_REACH_M = 600.0
MAX_REACH_M = 1_500.0
SOURCE_CONTEXT_CELLS = 8
MIN_CROP_AXIS_CELLS = 48
MAX_CROP_AXIS_CELLS = 160
PREFERRED_SOURCE_SPREAD_M = 2.0
PREFERRED_GRADE_RANGE = (0.002, 0.02)
PREFERRED_WIDTH_P10_M = 90.0
PREFERRED_FULL_FILL_DEPTH_M = 2.0
SCHEMA = "cubey.fluid25d.native_flow_site_survey.v1"


class SurveyError(RuntimeError):
    pass


class RejectCandidate(ValueError):
    def __init__(self, reason: str, **details: Any) -> None:
        super().__init__(reason)
        self.reason = reason
        self.details = details


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_json(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def crop_sha256(crop: np.ndarray) -> str:
    values = np.asarray(crop)
    if values.ndim != 2 or values.size == 0 or not np.isfinite(values).all():
        raise ValueError("crop must be a nonempty finite 2D array")
    payload = np.ascontiguousarray(values, dtype="<f4").tobytes(order="C")
    return hashlib.sha256(payload).hexdigest()


def validate_native_elevation(elevation_m: np.ndarray, spacing_m: float = SPACING_M) -> np.ndarray:
    values = np.asarray(elevation_m)
    if values.ndim != 2 or min(values.shape, default=0) < 3 or not np.isfinite(values).all():
        raise ValueError("elevation must be a finite 2D native raster at least 3x3")
    if not math.isfinite(spacing_m) or spacing_m != SPACING_M:
        raise ValueError("survey requires the pinned 30 m native spacing")
    return np.asarray(values, dtype=np.float32)


def priority_flood_d4(elevation_m: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Return one disposable full-map D4 fill and direction map."""
    raw = validate_native_elevation(elevation_m)
    filled, directions = pyflwdir.dem.fill_depressions(raw.copy(), outlets="edge", connectivity=4)
    filled = np.asarray(filled, dtype=np.float32)
    directions = np.asarray(directions)
    if filled.shape != raw.shape or directions.shape != raw.shape or not np.isfinite(filled).all():
        raise SurveyError("priority-flood output is malformed")
    codes = {int(code) for code in np.unique(directions)}
    if codes & DIAGONAL_CODES or codes - set(FLOW_OFFSETS_DZDX) - {0}:
        raise SurveyError(f"D4 priority flood returned non-cardinal direction codes: {sorted(codes)}")
    return filled, directions


def fill_components(elevation_m: np.ndarray, filled_m: np.ndarray) -> dict[str, Any]:
    """Label full-map positive-fill components once with shared-face connectivity."""
    raw = validate_native_elevation(elevation_m)
    filled = validate_native_elevation(filled_m)
    if raw.shape != filled.shape:
        raise ValueError("raw and filled terrain shapes differ")
    delta = np.maximum(filled - raw, 0.0).astype(np.float32)
    mask = delta > FILL_COMPONENT_EPS_M
    labels, count = ndimage.label(mask, structure=ndimage.generate_binary_structure(2, 1))
    flat_labels = labels.ravel()
    sizes = np.bincount(flat_labels, minlength=count + 1).astype(np.int64, copy=False)
    volumes = np.bincount(
        flat_labels,
        weights=delta.ravel().astype(np.float64) * CELL_AREA_M2,
        minlength=count + 1,
    )
    max_depth = np.zeros(count + 1, dtype=np.float32)
    min_spill = np.full(count + 1, np.inf, dtype=np.float32)
    max_spill = np.full(count + 1, -np.inf, dtype=np.float32)
    np.maximum.at(max_depth, flat_labels, delta.ravel())
    np.minimum.at(min_spill, flat_labels, filled.ravel())
    np.maximum.at(max_spill, flat_labels, filled.ravel())
    # Unfilled cells (label zero) are deliberately not depression components.
    max_depth[0] = 0.0
    return {
        "delta_m": delta,
        "labels": labels,
        "component_count": int(count),
        "sizes": sizes,
        "volumes_m3": volumes,
        "max_depth_m": max_depth,
        "min_spill_m": min_spill,
        "max_spill_m": max_spill,
        "positive_fill_cell_count": int(np.count_nonzero(mask)),
    }


def full_map_context(elevation_m: np.ndarray) -> dict[str, Any]:
    """Perform the single full-map D4 fill, area accumulation, and component pass."""
    raw = validate_native_elevation(elevation_m)
    filled, directions = priority_flood_d4(raw)
    flow = pyflwdir.from_array(
        directions,
        ftype="d8",
        transform=Affine(SPACING_M, 0.0, 0.0, 0.0, -SPACING_M, 0.0),
    )
    accumulation_cells = np.asarray(flow.upstream_area(unit="cell"), dtype=np.float64)
    accumulation_km2 = accumulation_cells * CELL_AREA_M2 / 1e6
    components = fill_components(raw, filled)
    return {
        "filled_m": filled,
        "directions": directions,
        "accumulation_km2": accumulation_km2,
        "components": components,
    }


def trace_cardinal(
    directions: np.ndarray,
    start_xz: tuple[int, int],
    max_steps: int,
    origin_full_map_xz: tuple[int, int] = (0, 0),
) -> dict[str, Any]:
    flow = np.asarray(directions)
    if flow.ndim != 2 or not np.isfinite(flow).all():
        raise ValueError("direction map must be a finite 2D array")
    codes = {int(code) for code in np.unique(flow)}
    if codes & DIAGONAL_CODES or codes - set(FLOW_OFFSETS_DZDX) - {0}:
        raise ValueError(f"direction map contains non-cardinal or unsupported codes: {sorted(codes)}")
    height, width = flow.shape
    x, z = map(int, start_xz)
    if not (0 <= x < width and 0 <= z < height):
        raise ValueError("route start lies outside direction map")
    ox, oz = origin_full_map_xz
    path = [(x, z)]
    visited = {(x, z)}
    termination = "step-limit"
    exit_side: str | None = None
    exit_local: list[int] | None = None
    for _ in range(max_steps):
        code = int(flow[z, x])
        if code not in FLOW_OFFSETS_DZDX:
            if x in (0, width - 1) or z in (0, height - 1):
                termination = "boundary-terminal"
                exit_side = "west" if x == 0 else "east" if x == width - 1 else "north" if z == 0 else "south"
                exit_local = [x, z]
            else:
                termination = "direction-terminal"
            break
        dz, dx = FLOW_OFFSETS_DZDX[code]
        nx, nz = x + dx, z + dz
        if not (0 <= nx < width and 0 <= nz < height):
            termination = "outward-edge-face"
            exit_side = "west" if nx < 0 else "east" if nx >= width else "north" if nz < 0 else "south"
            exit_local = [x, z]
            break
        if (nx, nz) in visited:
            raise RuntimeError(f"cardinal route cycle at crop cell {(nx, nz)}")
        path.append((nx, nz))
        visited.add((nx, nz))
        x, z = nx, nz
    distances = np.arange(len(path), dtype=np.float64) * SPACING_M
    return {
        "path_crop_cell_xz": [list(point) for point in path],
        "path_full_map_cell_xz": [[x + ox, z + oz] for x, z in path],
        "length_m": float(distances[-1]) if distances.size else 0.0,
        "termination": termination,
        "exit_edge": exit_side,
        "exit_crop_cell_xz": exit_local,
        "cardinal_only": True,
        "cycle_free": True,
    }


def path_length(path: list[tuple[int, int]]) -> float:
    return float(max(0, len(path) - 1) * SPACING_M)


def target_segment(path: list[tuple[int, int]], target_m: float = TARGET_REACH_M) -> list[tuple[int, int]]:
    needed = int(round(target_m / SPACING_M)) + 1
    return path[:needed]


def sample_network_centers(
    accumulation_km2: np.ndarray,
    directions: np.ndarray,
    max_evaluations: int = MAX_REACH_EVALUATIONS_PER_MAP,
) -> tuple[list[dict[str, Any]], int]:
    """Sample one flow locator near each fixed spatial-block center, then spread."""
    area = np.asarray(accumulation_km2, dtype=np.float64)
    flow = np.asarray(directions)
    if area.ndim != 2 or area.shape != flow.shape:
        raise ValueError("area and direction grids must have the same 2D shape")
    height, width = area.shape
    valid = (area >= MIN_LOCATOR_AREA_KM2) & np.isin(flow, tuple(FLOW_OFFSETS_DZDX))
    # Leave room for the five-cell source and the requested crop context.
    valid[:SOURCE_CONTEXT_CELLS, :] = False
    valid[-SOURCE_CONTEXT_CELLS:, :] = False
    valid[:, :SOURCE_CONTEXT_CELLS] = False
    valid[:, -SOURCE_CONTEXT_CELLS:] = False
    pool: list[dict[str, Any]] = []
    for z0 in range(0, height, SAMPLE_BLOCK_CELLS):
        for x0 in range(0, width, SAMPLE_BLOCK_CELLS):
            z1 = min(height, z0 + SAMPLE_BLOCK_CELLS)
            x1 = min(width, x0 + SAMPLE_BLOCK_CELLS)
            local_z, local_x = np.nonzero(valid[z0:z1, x0:x1])
            if local_x.size == 0:
                continue
            xs = local_x + x0
            zs = local_z + z0
            center_x = (x0 + x1 - 1) / 2.0
            center_z = (z0 + z1 - 1) / 2.0
            distance2 = (xs - center_x) ** 2 + (zs - center_z) ** 2
            order = np.lexsort((xs, zs, -area[zs, xs], distance2))
            selected = int(order[0])
            pool.append(
                {
                    "full_map_cell_xz": [int(xs[selected]), int(zs[selected])],
                    "locator_contributing_area_km2": float(area[zs[selected], xs[selected]]),
                    "sampling_block_xz": [x0, z0],
                }
            )
    if not pool:
        return [], 0

    # Farthest-point sampling yields spatially and longitudinally separated
    # observations while keeping every tie break deterministic.
    height_mid, width_mid = (height - 1) / 2.0, (width - 1) / 2.0
    first = min(
        range(len(pool)),
        key=lambda i: (
            (pool[i]["full_map_cell_xz"][0] - width_mid) ** 2
            + (pool[i]["full_map_cell_xz"][1] - height_mid) ** 2,
            -pool[i]["locator_contributing_area_km2"],
            pool[i]["full_map_cell_xz"][1],
            pool[i]["full_map_cell_xz"][0],
        ),
    )
    chosen = [first]
    while len(chosen) < min(max_evaluations, len(pool)):
        remaining = [index for index in range(len(pool)) if index not in set(chosen)]
        next_index = max(
            remaining,
            key=lambda i: (
                min(
                    (pool[i]["full_map_cell_xz"][0] - pool[j]["full_map_cell_xz"][0]) ** 2
                    + (pool[i]["full_map_cell_xz"][1] - pool[j]["full_map_cell_xz"][1]) ** 2
                    for j in chosen
                ),
                pool[i]["locator_contributing_area_km2"],
                -pool[i]["full_map_cell_xz"][1],
                -pool[i]["full_map_cell_xz"][0],
            ),
        )
        chosen.append(next_index)
    selected_pool = [pool[index] for index in chosen]
    selected_pool.sort(key=lambda item: (item["full_map_cell_xz"][1], item["full_map_cell_xz"][0]))
    return selected_pool, len(pool)


def _fit_axis_bounds(low: int, high_inclusive: int, limit: int, minimum_size: int, pad: int) -> tuple[int, int]:
    start = low - pad
    end = high_inclusive + pad + 1
    size = end - start
    if size < minimum_size:
        extra = minimum_size - size
        start -= extra // 2
        end += extra - extra // 2
    if start < 0:
        end = min(limit, end - start)
        start = 0
    if end > limit:
        start = max(0, start - (end - limit))
        end = limit
    if end - start < minimum_size:
        raise RejectCandidate("raster edge leaves insufficient crop context", axis_limit=limit)
    if end - start > MAX_CROP_AXIS_CELLS:
        raise RejectCandidate("route requires a crop wider than the 160-cell bound", axis_size=end - start)
    return start, end


def choose_crop_bbox(
    path_full_map_xz: list[tuple[int, int]],
    shape_zx: tuple[int, int],
    source_full_map_xz: tuple[int, int],
) -> tuple[int, int, int, int]:
    if not path_full_map_xz:
        raise RejectCandidate("empty full-map locator route")
    height, width = shape_zx
    xs = [point[0] for point in path_full_map_xz]
    zs = [point[1] for point in path_full_map_xz]
    x0, x1 = _fit_axis_bounds(min(xs), max(xs), width, MIN_CROP_AXIS_CELLS, SOURCE_CONTEXT_CELLS)
    z0, z1 = _fit_axis_bounds(min(zs), max(zs), height, MIN_CROP_AXIS_CELLS, SOURCE_CONTEXT_CELLS)
    sx, sz = source_full_map_xz
    margins = [sx - x0, x1 - 1 - sx, sz - z0, z1 - 1 - sz]
    if min(margins) < SOURCE_CONTEXT_CELLS:
        raise RejectCandidate("source lacks eight-cell crop context", source_crop_margins_cells=margins)
    return x0, z0, x1 - x0, z1 - z0


def five_cell_source(center_full_map_xz: tuple[int, int], shape_zx: tuple[int, int]) -> list[tuple[int, int]]:
    x, z = center_full_map_xz
    height, width = shape_zx
    cells = [(x + dx, z + dz) for dx, dz in SOURCE_OFFSETS_XZ]
    if any(not (0 <= sx < width and 0 <= sz < height) for sx, sz in cells):
        raise RejectCandidate("fixed five-cell source footprint leaves the native map")
    return cells


def route_tangent(path: list[tuple[int, int]], index: int) -> dict[str, Any]:
    before = path[max(0, index - 2)]
    after = path[min(len(path) - 1, index + 2)]
    dx, dz = after[0] - before[0], after[1] - before[1]
    if dx == 0 and dz == 0 and len(path) > 1:
        dx = path[min(index + 1, len(path) - 1)][0] - path[max(0, index - 1)][0]
        dz = path[min(index + 1, len(path) - 1)][1] - path[max(0, index - 1)][1]
    length = math.hypot(dx, dz)
    return {
        "cell_vector_dx_dz": [0.0, 0.0] if length == 0 else [dx / length, dz / length],
        "nearest_cardinal_direction_dx_dz": [int(np.sign(dx)), 0] if abs(dx) >= abs(dz) else [0, int(np.sign(dz))],
    }


def cross_section_width(
    elevation_m: np.ndarray,
    path_full_map_xz: list[tuple[int, int]],
    index: int,
    stage_above_route_bed_m: float,
) -> dict[str, Any]:
    raw = np.asarray(elevation_m, dtype=np.float32)
    x, z = path_full_map_xz[index]
    tangent = route_tangent(path_full_map_xz, index)
    dx, dz = tangent["nearest_cardinal_direction_dx_dz"]
    cross_axis = "z" if dx else "x"
    offsets = np.arange(-10, 11, dtype=np.int32)
    if cross_axis == "z":
        xs, zs = np.full(offsets.size, x), z + offsets
    else:
        xs, zs = x + offsets, np.full(offsets.size, z)
    inside = (xs >= 0) & (xs < raw.shape[1]) & (zs >= 0) & (zs < raw.shape[0])
    center_index = 10
    bed_m = float(raw[z, x])
    stage_m = bed_m + float(stage_above_route_bed_m)
    sublevel = np.zeros(offsets.size, dtype=bool)
    sublevel[inside] = raw[zs[inside], xs[inside]] <= stage_m
    left = right = center_index
    while left > 0 and sublevel[left - 1]:
        left -= 1
    while right + 1 < sublevel.size and sublevel[right + 1]:
        right += 1
    endpoint_a = [int(xs[left]), int(zs[left])]
    endpoint_b = [int(xs[right]), int(zs[right])]
    left_bank_stop = left > 0 and bool(inside[left - 1]) and not bool(sublevel[left - 1])
    right_bank_stop = right + 1 < sublevel.size and bool(inside[right + 1]) and not bool(sublevel[right + 1])
    terrain_censored = (left > 0 and not bool(inside[left - 1])) or (
        right + 1 < sublevel.size and not bool(inside[right + 1])
    )
    return {
        "method": "contiguous raw sublevel interval containing this actual route cell",
        "cross_axis": cross_axis,
        "route_cell_full_map_xz": [x, z],
        "route_bed_m": bed_m,
        "stage_above_route_bed_m": float(stage_above_route_bed_m),
        "stage_m": stage_m,
        "sample_half_span_m": 300.0,
        "width_m": float((right - left + 1) * SPACING_M),
        "endpoints_full_map_cell_xz": [endpoint_a, endpoint_b],
        "offset_cells_from_route": [int(offsets[left]), int(offsets[right])],
        "nearby_unconnected_sublevels_ignored": True,
        "left_bank_stop_observed": bool(left_bank_stop),
        "right_bank_stop_observed": bool(right_bank_stop),
        "censored_by_sample_window": bool(left == 0 or right == sublevel.size - 1),
        "censored_by_terrain_boundary": bool(terrain_censored),
    }


def component_report(component_data: dict[str, Any], labels_touched: list[int]) -> list[dict[str, Any]]:
    sizes = component_data["sizes"]
    volumes = component_data["volumes_m3"]
    max_depth = component_data["max_depth_m"]
    min_spill = component_data["min_spill_m"]
    max_spill = component_data["max_spill_m"]
    result = []
    for label in sorted(set(int(value) for value in labels_touched if value > 0)):
        result.append(
            {
                "component_label_full_map_analysis_only": label,
                "cells": int(sizes[label]),
                "area_km2": float(sizes[label] * CELL_AREA_M2 / 1e6),
                "equilibrium_fill_proxy_m3": float(volumes[label]),
                "max_fill_depth_m": float(max_depth[label]),
                "spill_level_range_m": [float(min_spill[label]), float(max_spill[label])],
            }
        )
    return result


def unique_touched_components(
    component_data: dict[str, Any],
    source_full_map_xz: list[tuple[int, int]],
    route_full_map_xz: list[tuple[int, int]],
) -> dict[str, Any]:
    labels = component_data["labels"]

    def at(points: list[tuple[int, int]]) -> list[int]:
        return sorted({int(labels[z, x]) for x, z in points if int(labels[z, x]) > 0})

    source_ids = at(source_full_map_xz)
    route_ids = at(route_full_map_xz)
    all_ids = sorted(set(source_ids) | set(route_ids))
    descriptions = component_report(component_data, all_ids)
    volume_by_id = {item["component_label_full_map_analysis_only"]: item["equilibrium_fill_proxy_m3"] for item in descriptions}
    return {
        "meaning": "distinct full-map equilibrium fill components; proxy storage is not required transient mass or a rate",
        "source_component_count": len(source_ids),
        "route_component_count": len(route_ids),
        "source_route_union_component_count": len(all_ids),
        "source_components": component_report(component_data, source_ids),
        "route_components": component_report(component_data, route_ids),
        "source_route_union_equilibrium_fill_proxy_m3_deduplicated": float(sum(volume_by_id.values())),
        "source_route_union_components": descriptions,
    }


def compare_full_and_crop_fill(
    raw_m: np.ndarray,
    full_filled_m: np.ndarray,
    crop_filled_m: np.ndarray,
    crop_xzwh: tuple[int, int, int, int],
    comparison_full_map_xz: list[tuple[int, int]],
) -> dict[str, Any]:
    x0, z0, width, height = crop_xzwh
    if crop_filled_m.shape != (height, width):
        raise ValueError("cropped fill does not match the crop bbox")
    evidence = []
    lower_count = 0
    max_lowering = 0.0
    raw = np.asarray(raw_m, dtype=np.float32)
    for x, z in comparison_full_map_xz:
        full_level = float(full_filled_m[z, x])
        crop_level = float(crop_filled_m[z - z0, x - x0])
        lowering = max(0.0, full_level - crop_level)
        if lowering > FILL_COMPONENT_EPS_M:
            lower_count += 1
        max_lowering = max(max_lowering, lowering)
        evidence.append(
            {
                "full_map_cell_xz": [x, z],
                "raw_m": float(raw[z, x]),
                "full_map_d4_fill_m": full_level,
                "crop_d4_fill_m": crop_level,
                "crop_minus_full_fill_m": crop_level - full_level,
            }
        )
    return {
        "sample_count": len(evidence),
        "samples_lowered_by_crop_more_than_0_01m": lower_count,
        "maximum_crop_lowering_m": float(max_lowering),
        "crop_gained_lower_outlet_on_profile": lower_count > 0,
        "profiles": evidence,
    }


def deterministic_result_fingerprint(value: Any) -> str:
    """Hash evidence while excluding runtimes and generated output locations."""
    volatile = {"runtime_s", "contact_sheet", "output", "output_dir", "generated_at"}

    def stable(item: Any) -> Any:
        if isinstance(item, dict):
            return {key: stable(value) for key, value in sorted(item.items()) if key not in volatile}
        if isinstance(item, list):
            return [stable(value) for value in item]
        return item

    return sha256_json(stable(value))


def minimax_edge_saddles(
    raw_m: np.ndarray,
    source_crop_xz: list[tuple[int, int]],
    origin_full_map_xz: tuple[int, int],
) -> dict[str, Any]:
    """Small-crop, D4 minimax alternatives; never run over a full payload."""
    raw = validate_native_elevation(raw_m)
    height, width = raw.shape
    best = np.full(raw.size, np.inf, dtype=np.float32)
    queue: list[tuple[float, int]] = []
    for x, z in source_crop_xz:
        index = z * width + x
        value = float(raw[z, x])
        if value < best[index]:
            best[index] = value
            heapq.heappush(queue, (value, index))
    while queue:
        level, index = heapq.heappop(queue)
        if level != float(best[index]):
            continue
        z, x = divmod(index, width)
        for dz, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            nx, nz = x + dx, z + dz
            if 0 <= nx < width and 0 <= nz < height:
                neighbor = nz * width + nx
                candidate = max(level, float(raw[nz, nx]))
                if candidate < float(best[neighbor]):
                    best[neighbor] = candidate
                    heapq.heappush(queue, (candidate, neighbor))
    edges = {
        "west": np.arange(0, height * width, width, dtype=np.int64),
        "east": np.arange(width - 1, height * width, width, dtype=np.int64),
        "north": np.arange(width, dtype=np.int64),
        "south": np.arange((height - 1) * width, height * width, dtype=np.int64),
    }
    ox, oz = origin_full_map_xz
    result = {}
    for side, indices in edges.items():
        selected = int(indices[np.argmin(best[indices])])
        z, x = divmod(selected, width)
        result[side] = {
            "source_to_edge_minimax_saddle_m": float(best[selected]),
            "edge_exit_crop_cell_xz": [x, z],
            "edge_exit_full_map_cell_xz": [x + ox, z + oz],
        }
    return result


def depression_preference(max_fill_depth_m: float) -> dict[str, Any]:
    return {
        "max_full_map_fill_depth_on_source_and_reach_m": float(max_fill_depth_m),
        "limited_burden_preference": "at most 2 m maximum equilibrium fill depth on sampled source/reach cells",
        "meets_preference": bool(max_fill_depth_m <= PREFERRED_FULL_FILL_DEPTH_M),
        "warning_only": True,
        "meaning": "equilibrium fill diagnostic, not current water, required transient mass, or discharge",
    }


def score_preferences(
    source_spread_m: float,
    net_grade: float,
    width_p10_1m_m: float,
    max_fill_depth_m: float,
) -> dict[str, Any]:
    grade_low, grade_high = PREFERRED_GRADE_RANGE
    if net_grade <= 0:
        grade_score = 0.0
    elif grade_low <= net_grade <= grade_high:
        grade_score = 1.0
    else:
        center = math.sqrt(grade_low * grade_high)
        grade_score = float(math.exp(-abs(math.log(max(net_grade, 1e-9) / center))))
    spread_score = max(0.0, 1.0 - source_spread_m / 5.0)
    width_score = min(1.0, width_p10_1m_m / PREFERRED_WIDTH_P10_M)
    depression_score = 1.0 / (1.0 + max_fill_depth_m / PREFERRED_FULL_FILL_DEPTH_M)
    weighted = {
        "moderate_net_grade": 0.30 * grade_score,
        "coherent_five_cell_source": 0.25 * spread_score,
        "raw_connected_width_at_1m_stage": 0.25 * width_score,
        "limited_fill_depth_preference": 0.20 * depression_score,
    }
    preferred = (
        source_spread_m <= PREFERRED_SOURCE_SPREAD_M
        and grade_low <= net_grade <= grade_high
        and width_p10_1m_m >= PREFERRED_WIDTH_P10_M
        and max_fill_depth_m <= PREFERRED_FULL_FILL_DEPTH_M
    )
    return {
        "score_0_to_1": float(sum(weighted.values())),
        "preferred": bool(preferred),
        "preference_checks": {
            "source_spread_at_most_2m": bool(source_spread_m <= PREFERRED_SOURCE_SPREAD_M),
            "net_grade_between_0_2_and_2_percent": bool(grade_low <= net_grade <= grade_high),
            "raw_connected_width_p10_at_1m_at_least_90m": bool(width_p10_1m_m >= PREFERRED_WIDTH_P10_M),
            "full_fill_depth_at_most_2m_warning_only": bool(max_fill_depth_m <= PREFERRED_FULL_FILL_DEPTH_M),
        },
        "weighted_terms": {name: float(value) for name, value in weighted.items()},
        "formula": "0.30*grade + 0.25*source coherence + 0.25*raw width + 0.20*fill-depth preference; each term is in [0,1]",
    }


def evaluate_candidate(
    raw_m: np.ndarray,
    full_context: dict[str, Any],
    candidate: dict[str, Any],
) -> dict[str, Any]:
    raw = validate_native_elevation(raw_m)
    full_directions = full_context["directions"]
    full_filled = full_context["filled_m"]
    accumulation = full_context["accumulation_km2"]
    components = full_context["components"]
    sx, sz = candidate["full_map_cell_xz"]
    full_trace = trace_cardinal(full_directions, (sx, sz), max_steps=51)
    full_path = [tuple(point) for point in full_trace["path_full_map_cell_xz"]]
    if len(full_path) < 41:
        raise RejectCandidate("full-map D4 locator route is shorter than 1200 m", locator_length_m=full_trace["length_m"])
    locator_segment = target_segment(full_path)
    bbox = choose_crop_bbox(locator_segment, raw.shape, (sx, sz))
    x0, z0, crop_width, crop_height = bbox
    crop = np.ascontiguousarray(raw[z0 : z0 + crop_height, x0 : x0 + crop_width], dtype=np.float32)
    if crop.shape != (crop_height, crop_width):
        raise RejectCandidate("crop leaves the native payload bounds")
    crop_filled, crop_directions = priority_flood_d4(crop)
    crop_start = (sx - x0, sz - z0)
    crop_trace = trace_cardinal(crop_directions, crop_start, max_steps=crop.size, origin_full_map_xz=(x0, z0))
    crop_path = [tuple(point) for point in crop_trace["path_full_map_cell_xz"]]
    if crop_trace["length_m"] < MIN_REACH_M:
        raise RejectCandidate("cropped D4 route is shorter than 600 m", crop_route_length_m=crop_trace["length_m"])
    if crop_trace["length_m"] < TARGET_REACH_M + 150.0:
        raise RejectCandidate(
            "cropped route leaves less than 150 m downstream buffer after the 1200 m reach",
            crop_route_length_m=crop_trace["length_m"],
        )
    crop_reach = target_segment(crop_path)
    if len(crop_reach) < 21:
        raise RejectCandidate("cropped route cannot supply the minimum 600 m review reach")
    start_bed = float(raw[crop_reach[0][1], crop_reach[0][0]])
    end_bed = float(raw[crop_reach[-1][1], crop_reach[-1][0]])
    net_fall = start_bed - end_bed
    reach_length = path_length(crop_reach)
    if net_fall <= 0.0:
        raise RejectCandidate("cropped cardinal route has no positive raw net fall", net_fall_m=net_fall)
    if reach_length < MIN_REACH_M or reach_length > MAX_REACH_M:
        raise RejectCandidate("selected raw reach is outside the 600-1500 m range", reach_length_m=reach_length)

    source_cells = five_cell_source((sx, sz), raw.shape)
    patch_values = np.asarray([raw[z, x] for x, z in source_cells], dtype=np.float64)
    spread = float(patch_values.max() - patch_values.min())
    compare_points = list(dict.fromkeys(source_cells + crop_path))
    fill_comparison = compare_full_and_crop_fill(raw, full_filled, crop_filled, bbox, compare_points)
    if fill_comparison["crop_gained_lower_outlet_on_profile"]:
        raise RejectCandidate(
            "crop D4 fill is lower than full-map D4 fill on source-to-edge route profile",
            max_crop_lowering_m=fill_comparison["maximum_crop_lowering_m"],
            lowered_profile_cells=fill_comparison["samples_lowered_by_crop_more_than_0_01m"],
        )

    source_reach_components = unique_touched_components(components, source_cells, crop_reach)
    source_to_edge_components = unique_touched_components(components, source_cells, crop_path)
    full_delta = components["delta_m"]
    reach_fill_points = list(dict.fromkeys(source_cells + crop_reach))
    fill_depths = [float(full_delta[z, x]) for x, z in reach_fill_points]
    max_fill_depth = max(fill_depths, default=0.0)
    distances = np.arange(len(crop_reach), dtype=np.float64) * SPACING_M
    stations = [0, 10, 20, 30, 40]
    station_records = []
    widths: dict[str, list[float]] = {"0.5": [], "1.0": [], "2.0": []}
    for index in stations:
        x, z = crop_reach[index]
        route_cell = raw[z, x]
        record = {
            "distance_from_source_m": float(distances[index]),
            "route_cell_full_map_xz": [x, z],
            "raw_bed_m": float(route_cell),
            "full_map_d4_fill_m": float(full_filled[z, x]),
            "crop_d4_fill_m": float(crop_filled[z - z0, x - x0]),
            "full_map_fill_depth_m": float(full_delta[z, x]),
            "crop_fill_depth_m": float(max(0.0, crop_filled[z - z0, x - x0] - route_cell)),
            "tangent": route_tangent(crop_reach, index),
            "cross_sections": {},
        }
        for stage in (0.5, 1.0, 2.0):
            section = cross_section_width(raw, crop_reach, index, stage)
            record["cross_sections"][f"route_bed_plus_{stage:.1f}m"] = section
            widths[f"{stage:.1f}"].append(section["width_m"])
        station_records.append(record)

    net_grade = net_fall / reach_length
    width_p10_1m = float(np.percentile(widths["1.0"], 10))
    preference = score_preferences(spread, net_grade, width_p10_1m, max_fill_depth)
    area_samples = [float(accumulation[z, x]) for x, z in crop_reach]
    face_grades = np.abs(np.diff([float(raw[z, x]) for x, z in crop_reach])) / SPACING_M
    minimax = minimax_edge_saddles(crop, [(x - x0, z - z0) for x, z in source_cells], (x0, z0))
    actual_edge = crop_trace["exit_edge"]
    actual_saddle = minimax.get(actual_edge, {}).get("source_to_edge_minimax_saddle_m") if actual_edge else None
    lower_edges = []
    if actual_saddle is not None:
        lower_edges = [
            {"edge": side, **values}
            for side, values in minimax.items()
            if values["source_to_edge_minimax_saddle_m"] < actual_saddle - FILL_COMPONENT_EPS_M
        ]

    source_crop_cells = [[x - x0, z - z0] for x, z in source_cells]
    source_data = {
        "footprint": "fixed five-cell cardinal cross, center plus four face-neighbors, 30 m radius",
        "center_full_map_cell_xz": [sx, sz],
        "center_crop_cell_xz": list(crop_start),
        "cells_full_map_xz": [list(point) for point in source_cells],
        "cells_crop_xz": source_crop_cells,
        "raw_elevations_m_center_north_south_east_west": [float(value) for value in patch_values],
        "raw_min_m": float(patch_values.min()),
        "raw_mean_m": float(patch_values.mean()),
        "raw_max_m": float(patch_values.max()),
        "raw_spread_m": spread,
        "source_cell_full_map_fill_depths_m_center_north_south_east_west": [float(full_delta[z, x]) for x, z in source_cells],
        "crop_cell_fill_depths_m_center_north_south_east_west": [
            float(max(0.0, crop_filled[z - z0, x - x0] - raw[z, x])) for x, z in source_cells
        ],
    }
    crop_edges = {
        "all_four_perimeters_open": True,
        "actual_crop_d4_route_exit_edge": actual_edge,
        "actual_route_exit_crop_cell_xz": crop_trace["exit_crop_cell_xz"],
        "actual_route_exit_full_map_cell_xz": crop_path[-1] if crop_path else None,
        "exit_window_crop_cell_xz": _exit_window(crop_trace, crop.shape),
        "actual_route_exit_minimax_saddle_m": actual_saddle,
        "source_to_each_open_edge_minimax_saddle": minimax,
        "other_lower_source_connected_edge_alternatives": lower_edges,
        "interpretation": "measured crop routing and source-connected saddle alternatives; no edge is forced",
    }
    full_locator_record = {
        "method": "full-map D4 priority-flood direction; locator only",
        "path_first_1200m_full_map_cell_xz": [list(point) for point in locator_segment],
        "raw_start_m": float(raw[locator_segment[0][1], locator_segment[0][0]]),
        "raw_end_m": float(raw[locator_segment[-1][1], locator_segment[-1][0]]),
        "raw_net_fall_m": float(raw[locator_segment[0][1], locator_segment[0][0]] - raw[locator_segment[-1][1], locator_segment[-1][0]]),
        "full_locator_length_m": path_length(locator_segment),
    }
    target_route_record = {
        "method": "cropped D4 priority-flood cardinal route; raw route profile is independently measured",
        "path_first_1200m_full_map_cell_xz": [list(point) for point in crop_reach],
        "path_first_1200m_crop_cell_xz": [[x - x0, z - z0] for x, z in crop_reach],
        "raw_start_m": start_bed,
        "raw_end_m": end_bed,
        "raw_net_fall_m": net_fall,
        "length_m": reach_length,
        "net_grade_fraction": net_grade,
        "max_single_step_adverse_rise_m": float(max(0.0, np.diff([raw[z, x] for x, z in crop_reach]).max(initial=0.0))),
        "raw_absolute_face_step_grade_max_fraction": float(face_grades.max(initial=0.0)),
        "raw_absolute_face_step_grade_p95_fraction": float(np.percentile(face_grades, 95)) if face_grades.size else 0.0,
        "raw_net_fall_gate_passed": True,
        "cardinal_and_cycle_free": True,
        "gauge_positions": {
            "upstream": _gauge(crop_reach, 0, x0, z0),
            "midstream": _gauge(crop_reach, 20, x0, z0),
            "downstream": _gauge(crop_reach, 40, x0, z0),
        },
        "profiles_at_0_300_600_900_1200m": station_records,
        "full_map_vs_crop_fill_profile": fill_comparison,
        "raw_cross_section_width_summary_m": {
            f"route_bed_plus_{stage}m": {
                "samples": values,
                "minimum": float(min(values)),
                "p10": float(np.percentile(values, 10)),
                "median": float(np.median(values)),
                "sample_window_censored_count": int(
                    sum(
                        bool(record["cross_sections"][f"route_bed_plus_{float(stage):.1f}m"]["censored_by_sample_window"])
                        for record in station_records
                    )
                ),
                "terrain_boundary_censored_count": int(
                    sum(
                        bool(record["cross_sections"][f"route_bed_plus_{float(stage):.1f}m"]["censored_by_terrain_boundary"])
                        for record in station_records
                    )
                ),
                "both_bank_stops_observed_count": int(
                    sum(
                        bool(record["cross_sections"][f"route_bed_plus_{float(stage):.1f}m"]["left_bank_stop_observed"])
                        and bool(record["cross_sections"][f"route_bed_plus_{float(stage):.1f}m"]["right_bank_stop_observed"])
                        for record in station_records
                    )
                ),
            }
            for stage, values in widths.items()
        },
        "width_limit": "each interval is contiguous in raw elevation and contains this exact routed cell; unconnected neighboring lows are excluded; a window-censored width is a lower bound; not a simulated wet width",
    }
    return {
        "candidate_id": f"{candidate['elevation_sha256'][:10]}-{sx:04d}-{sz:04d}",
        "status": "preferred" if preference["preferred"] else "borderline",
        "score": preference["score_0_to_1"],
        "scoring": preference,
        "depression_preference": depression_preference(max_fill_depth),
        "locator_contributing_area_km2": candidate["locator_contributing_area_km2"],
        "candidate_sampling": candidate,
        "crop": {
            "bbox_full_map_cell_xzwh": list(bbox),
            "shape_zx": [crop_height, crop_width],
            "size_m": [crop_width * SPACING_M, crop_height * SPACING_M],
            "transformed_crop_sha256_f32le_c": crop_sha256(crop),
            "transformed_crop_encoding": "exact transformed little-endian float32 C row-major crop bytes",
            "height_min_m": float(crop.min()),
            "height_max_m": float(crop.max()),
            "source_margin_cells_west_east_north_south": [sx - x0, x0 + crop_width - 1 - sx, sz - z0, z0 + crop_height - 1 - sz],
            "route_exit_distance_after_1200m_m": float(crop_trace["length_m"] - TARGET_REACH_M),
            "actual_cardinal_route_to_crop_edge_length_m": crop_trace["length_m"],
            "actual_route_trace_to_edge": crop_trace,
        },
        "source": source_data,
        "full_map_locator": full_locator_record,
        "route": target_route_record,
        "full_map_depressions": {
            "source_to_crop_edge_route": source_to_edge_components,
            "source_and_reviewed_1200m_reach": source_reach_components,
        },
        "crop_edges": crop_edges,
        "area_locator_context": {
            "meaning": "unit-runoff D4 contributing area in km2; locator context, not discharge Q",
            "source_area_km2": float(accumulation[sz, sx]),
            "reach_min_km2": float(min(area_samples)),
            "reach_median_km2": float(np.median(area_samples)),
            "reach_max_km2": float(max(area_samples)),
        },
    }


def _gauge(path: list[tuple[int, int]], index: int, x0: int, z0: int) -> dict[str, Any]:
    x, z = path[index]
    return {
        "distance_from_source_m": float(index * SPACING_M),
        "full_map_cell_xz": [x, z],
        "crop_cell_xz": [x - x0, z - z0],
        "tangent": route_tangent(path, index),
    }


def _exit_window(trace: dict[str, Any], shape_zx: tuple[int, int]) -> dict[str, Any] | None:
    side = trace["exit_edge"]
    point = trace["exit_crop_cell_xz"]
    if side is None or point is None:
        return None
    x, z = point
    height, width = shape_zx
    if side in ("west", "east"):
        return {"edge": side, "crop_cell_xz_bounds": [[x, max(0, z - 2)], [x, min(height - 1, z + 2)]]}
    return {"edge": side, "crop_cell_xz_bounds": [[max(0, x - 2), z], [min(width - 1, x + 2), z]]}


def sample_rejection(candidate: dict[str, Any], reason: str, details: dict[str, Any]) -> dict[str, Any]:
    return {
        "full_map_cell_xz": candidate["full_map_cell_xz"],
        "locator_contributing_area_km2": candidate["locator_contributing_area_km2"],
        "reason": reason,
        "details": details,
    }


def select_global_candidates(candidates: list[dict[str, Any]], count: int = 5) -> list[dict[str, Any]]:
    ordered = sorted(
        candidates,
        key=lambda item: (
            not item["scoring"]["preferred"],
            -item["score"],
            item["asset"]["elevation_sha256"],
            item["candidate_id"],
        ),
    )
    selected: list[dict[str, Any]] = []
    used_payloads: set[str] = set()
    for item in ordered:
        payload = item["asset"]["elevation_sha256"]
        if payload in used_payloads:
            continue
        selected.append(item)
        used_payloads.add(payload)
        if len(selected) >= count:
            return selected
    # Only reuse a payload when fewer than five payloads yielded hard-gate sites.
    selected_ids = {item["candidate_id"] for item in selected}
    for item in ordered:
        if item["candidate_id"] not in selected_ids:
            selected.append(item)
            selected_ids.add(item["candidate_id"])
        if len(selected) >= count:
            break
    return selected


def _hillshade(elevation: np.ndarray) -> np.ndarray:
    dz, dx = np.gradient(np.asarray(elevation, dtype=np.float32), SPACING_M)
    slope = np.sqrt(dx * dx + dz * dz)
    light = (0.70 - 0.55 * dx + 0.35 * dz) / np.sqrt(1.0 + slope * slope)
    shade = np.clip((light - 0.18) / 0.8, 0.0, 1.0)
    return (45 + shade * 180).astype(np.uint8)


def write_contact_sheet(candidate: dict[str, Any], output_path: Path, raw_map: np.ndarray) -> None:
    crop_data = candidate["crop"]
    x0, z0, width, height = crop_data["bbox_full_map_cell_xzwh"]
    crop = raw_map[z0 : z0 + height, x0 : x0 + width]
    scale = min(720 / width, 520 / height)
    panel_w, panel_h = max(1, int(round(width * scale))), max(1, int(round(height * scale)))
    shade = Image.fromarray(_hillshade(crop), mode="L").convert("RGB").resize((panel_w, panel_h), Image.Resampling.NEAREST)
    canvas = Image.new("RGB", (1200, 650), (22, 26, 32))
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default()
    draw.text((20, 14), f"{candidate['rank']}. {candidate['asset']['variant']} - {candidate['status']}  score {candidate['score']:.3f}", fill=(245, 245, 245), font=font)
    draw.text((20, 34), f"source {candidate['source']['center_full_map_cell_xz']}  crop {crop_data['bbox_full_map_cell_xzwh']}  raw net fall {candidate['route']['raw_net_fall_m']:.2f} m", fill=(210, 216, 222), font=font)
    origin_x, origin_y = 20, 70
    canvas.paste(shade, (origin_x, origin_y))
    draw = ImageDraw.Draw(canvas)
    route = candidate["route"]["path_first_1200m_crop_cell_xz"]
    points = [(origin_x + int(round(x * scale)), origin_y + int(round(z * scale))) for x, z in route]
    if len(points) > 1:
        draw.line(points, fill=(60, 230, 225), width=3)
    source_x, source_z = candidate["source"]["center_crop_cell_xz"]
    cx, cz = origin_x + int(round(source_x * scale)), origin_y + int(round(source_z * scale))
    draw.ellipse((cx - 7, cz - 7, cx + 7, cz + 7), fill=(244, 92, 173), outline=(255, 255, 255), width=2)
    for label, key, color in (("U", "upstream", (255, 223, 80)), ("M", "midstream", (255, 166, 72)), ("D", "downstream", (255, 95, 95))):
        gx, gz = candidate["route"]["gauge_positions"][key]["crop_cell_xz"]
        px, py = origin_x + int(round(gx * scale)), origin_y + int(round(gz * scale))
        draw.ellipse((px - 5, py - 5, px + 5, py + 5), fill=color, outline=(20, 20, 20))
        draw.text((px + 6, py - 8), label, fill=color, font=font)
    # Raw bed and disposable fill profiles, plotted in a separate labeled panel.
    chart_x, chart_y, chart_w, chart_h = 800, 105, 370, 390
    draw.rectangle((chart_x, chart_y, chart_x + chart_w, chart_y + chart_h), outline=(130, 140, 150))
    records = candidate["route"]["profiles_at_0_300_600_900_1200m"]
    vals = [record["raw_bed_m"] for record in records] + [record["full_map_d4_fill_m"] for record in records] + [record["crop_d4_fill_m"] for record in records]
    vmin, vmax = min(vals), max(vals)
    if vmax - vmin < 1e-5:
        vmax = vmin + 1.0

    def project(i: int, value: float) -> tuple[int, int]:
        px = chart_x + int(round(i / max(1, len(records) - 1) * (chart_w - 20))) + 10
        py = chart_y + chart_h - 10 - int(round((value - vmin) / (vmax - vmin) * (chart_h - 20)))
        return px, py

    for key, color in (("raw_bed_m", (60, 230, 225)), ("full_map_d4_fill_m", (255, 223, 80)), ("crop_d4_fill_m", (244, 92, 173))):
        points = [project(i, record[key]) for i, record in enumerate(records)]
        if len(points) > 1:
            draw.line(points, fill=color, width=2)
    draw.text((chart_x, chart_y - 24), "Raw bed / D4 fill profiles (m)", fill=(240, 240, 240), font=font)
    for index, (label, color) in enumerate((("raw bed", (60, 230, 225)), ("full-map fill", (255, 223, 80)), ("crop fill", (244, 92, 173)))):
        draw.text((chart_x + index * 122, chart_y + chart_h + 10), label, fill=color, font=font)
    summary = f"Source spread {candidate['source']['raw_spread_m']:.2f} m | 1 m stage p10 width {candidate['route']['raw_cross_section_width_summary_m']['route_bed_plus_1.0m']['p10']:.0f} m | D4 fill is analytical only"
    draw.text((20, 610), summary, fill=(220, 225, 230), font=font)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(output_path)


def _candidate_display(candidate: dict[str, Any]) -> dict[str, Any]:
    # Keep the core survey concise while retaining the recipe and profile evidence.
    return candidate


def analyze_asset(asset: Any, inventory: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]], np.ndarray, dict[str, Any]]:
    started = time.perf_counter()
    elevation, identity = hydro.load_asset(asset, inventory)
    if identity["spacing_m"] != SPACING_M or identity["elevation_sha256"] != asset.elevation_sha256:
        raise SurveyError("loaded Terrain Diffusion asset differs from native spacing or pinned identity")
    raw = validate_native_elevation(elevation, identity["spacing_m"])
    full_started = time.perf_counter()
    context = full_map_context(raw)
    full_elapsed = time.perf_counter() - full_started
    candidates, pool_count = sample_network_centers(context["accumulation_km2"], context["directions"])
    evaluated: list[dict[str, Any]] = []
    rejected: list[dict[str, Any]] = []
    rejection_counts: dict[str, int] = {}
    for candidate in candidates:
        candidate["elevation_sha256"] = identity["elevation_sha256"]
        try:
            evidence = evaluate_candidate(raw, context, candidate)
            evidence["asset"] = {
                "variant": identity["variant"],
                "seed": identity["seed"],
                "elevation_sha256": identity["elevation_sha256"],
                "manifest_path": identity["manifest_path"],
                "manifest_sha256": identity["manifest_sha256"],
                "height_transform": identity["height_transform"],
                "aliases": identity["aliases"],
            }
            evaluated.append(evidence)
        except RejectCandidate as exc:
            rejection_counts[exc.reason] = rejection_counts.get(exc.reason, 0) + 1
            rejected.append(sample_rejection(candidate, exc.reason, exc.details))
    asset_record = {
        "variant": identity["variant"],
        "seed": identity["seed"],
        "elevation_sha256": identity["elevation_sha256"],
        "manifest_path": identity["manifest_path"],
        "manifest_sha256": identity["manifest_sha256"],
        "elevation_path": identity["elevation_path"],
        "height_transform": identity["height_transform"],
        "shape_zx": identity["shape_zx"],
        "spacing_m": identity["spacing_m"],
        "aliases": identity["aliases"],
    }
    summary = {
        "asset": asset_record,
        "runtime_s": {
            "full_map_d4_fill_area_and_component_pass": float(full_elapsed),
            "whole_map_survey": float(time.perf_counter() - started),
        },
        "full_map_d4": {
            "meaning": "one cardinal priority-flood pass and one upstream-area accumulation per payload; area is locator context only",
            "direction_codes": sorted(int(code) for code in np.unique(context["directions"])),
            "cardinal_only": True,
            "positive_fill_component_count_4_connected": context["components"]["component_count"],
            "positive_fill_cell_count": context["components"]["positive_fill_cell_count"],
            "max_equilibrium_fill_depth_m": float(context["components"]["delta_m"].max(initial=0.0)),
            "max_contributing_area_km2": float(context["accumulation_km2"].max()),
        },
        "candidate_sampling": {
            "fixed_spatial_block_cells": SAMPLE_BLOCK_CELLS,
            "minimum_locator_area_km2": MIN_LOCATOR_AREA_KM2,
            "candidate_pool_spatial_blocks": pool_count,
            "reaches_evaluated": len(candidates),
            "maximum_reaches_evaluated": MAX_REACH_EVALUATIONS_PER_MAP,
            "sampling_method": "one network locator nearest each 128x128 block center, then deterministic farthest-point coverage; this deliberately samples longitudinal positions",
        },
        "accepted_reach_count": len(evaluated),
        "rejection_counts": rejection_counts,
        "rejected_reaches": rejected,
        "top_accepted_preview": [
            {
                "candidate_id": item["candidate_id"],
                "status": item["status"],
                "score": item["score"],
                "center_full_map_cell_xz": item["source"]["center_full_map_cell_xz"],
                "net_fall_m": item["route"]["raw_net_fall_m"],
                "source_spread_m": item["source"]["raw_spread_m"],
            }
            for item in sorted(evaluated, key=lambda item: (-item["score"], item["candidate_id"]))[:3]
        ],
    }
    return summary, evaluated, raw, identity


def package_versions() -> dict[str, str]:
    versions = {}
    for name in ("numpy", "pyflwdir", "rasterio", "scipy", "Pillow"):
        try:
            versions[name] = importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError:
            versions[name] = "unknown"
    return versions


def build_survey(output_dir: Path) -> dict[str, Any]:
    assets, inventory = reach.load_pinned_assets(ROOT / "cache/terrain/sources/v1")
    map_summaries: list[dict[str, Any]] = []
    all_candidates: list[dict[str, Any]] = []
    for asset in assets:
        summary, candidates, raw, _identity = analyze_asset(asset, inventory)
        map_summaries.append(summary)
        all_candidates.extend(candidates)
        print(
            json.dumps(
                {
                    "payload": asset.elevation_sha256[:12],
                    "variant": summary["asset"]["variant"],
                    "runtime_s": round(summary["runtime_s"]["whole_map_survey"], 2),
                    "reaches": summary["candidate_sampling"]["reaches_evaluated"],
                    "accepted": summary["accepted_reach_count"],
                    "preferred": sum(bool(item["scoring"]["preferred"]) for item in candidates),
                },
                sort_keys=True,
            ),
            flush=True,
        )
        del raw
    selected = select_global_candidates(all_candidates)
    for rank, candidate in enumerate(selected, start=1):
        candidate["rank"] = rank
    helper_paths = {
        "native_flow_site_survey_v1.py": Path(__file__),
        "terrain_reach_scan_v1.py": Path(reach.__file__),
        "terrain_hydro_read_v1.py": Path(hydro.__file__),
    }
    survey = {
        "schema": SCHEMA,
        "status": "deterministic offline site survey; no water simulation and no Fluid recipe changes",
        "criteria": {
            "hard_gates": [
                "valid finite raw Terrain Diffusion payload, native 30 m spacing, and exact pinned inventory identity",
                "cardinal and cycle-free cropped D4 route with positive raw net fall over a 1200 m target reach",
                "fixed five-cell cardinal source footprint and at least eight cells of crop context",
                "at least 150 m downstream crop-route buffer after the 1200 m target reach",
                "cropped D4 fill may not be more than 0.01 m lower than full-map D4 fill on source/reach profile cells",
            ],
            "preferences": {
                "source_spread_m": f"<= {PREFERRED_SOURCE_SPREAD_M:g}",
                "net_raw_grade_fraction": list(PREFERRED_GRADE_RANGE),
                "raw_contiguous_cross_section_p10_at_1m_stage_m": f">= {PREFERRED_WIDTH_P10_M:g}",
                "maximum_sampled_equilibrium_fill_depth_m": f"<= {PREFERRED_FULL_FILL_DEPTH_M:g}; warning and score preference only",
            },
            "ranking_formula": "0.30 grade + 0.25 source coherence + 0.25 connected width + 0.20 fill-depth preference; equilibrium volumes and area are not gates or Q estimates",
            "candidate_diversity": "at most 20 deterministic reach evaluations per payload; global top 5 prefer distinct payloads",
            "sampling_area_threshold_meaning": "unit-runoff contributing area in km2 is a locator threshold, not discharge or Q",
        },
        "pinned_inventory": inventory,
        "tools": {
            "runner_path": str(Path(__file__).resolve().relative_to(ROOT)),
            "runner_sha256": sha256_file(Path(__file__).resolve()),
            "helper_sha256": {name: sha256_file(path) for name, path in helper_paths.items()},
            "package_versions": package_versions(),
            "crop_hash": "SHA-256 over exact transformed little-endian float32 C row-major bytes",
        },
        "native_spacing_m": SPACING_M,
        "payload_count": len(map_summaries),
        "map_summaries": map_summaries,
        "global_candidate_count_hard_gates_passed": len(all_candidates),
        "hard_gate_passed_candidates": [_candidate_display(item) for item in all_candidates],
        "ranked_candidates": [_candidate_display(item) for item in selected],
        "interpretation_limits": [
            "D4 contributing area is unit-runoff geometry, not a discharge, permanent stream, or water source rate.",
            "Priority-flood fills and component volumes are equilibrium analysis proxies, not observed water or required transient mass.",
            "Raw contiguous widths contain the routed cell at bed plus stage; they are geometric sensitivities, not actual wetted widths.",
            "A candidate and contact sheet do not certify suitability; the primary reviews the evidence before any Fluid recipe is frozen.",
            "Terrain is immutable; no seeds, carved channels, crop-specific outlet forcing, or solver runs are included.",
        ],
    }
    survey["deterministic_result_sha256"] = deterministic_result_fingerprint(
        {
            "criteria": survey["criteria"],
            "pinned_inventory": survey["pinned_inventory"],
            "tools": survey["tools"],
            "map_summaries": survey["map_summaries"],
            "hard_gate_passed_candidates": survey["hard_gate_passed_candidates"],
            "ranked_candidates": [_candidate_display(item) for item in selected],
        }
    )
    survey["_selected_payload_hashes"] = [item["asset"]["elevation_sha256"] for item in selected]
    return survey


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "outputs/fluid/native-flow-site-survey-v1-20260925",
    )
    args = parser.parse_args()
    output_dir = args.output_dir.resolve()
    survey_path = output_dir / "survey.json"
    if output_dir.exists() and any(output_dir.iterdir()):
        raise SystemExit(f"refusing to overwrite existing evidence: {output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)
    survey = build_survey(output_dir)
    for candidate in survey["ranked_candidates"]:
        source_sha = candidate["asset"]["elevation_sha256"]
        image_name = f"candidate-{candidate['rank']:02d}-{candidate['candidate_id']}.png"
        candidate["contact_sheet"] = str((output_dir / image_name).resolve().relative_to(ROOT))
    assets, inventory = reach.load_pinned_assets(ROOT / "cache/terrain/sources/v1")
    assets_by_hash = {asset.elevation_sha256: asset for asset in assets}
    for candidate in survey["ranked_candidates"]:
        asset = assets_by_hash[candidate["asset"]["elevation_sha256"]]
        raw_map, _identity = hydro.load_asset(asset, inventory)
        sheet = output_dir / Path(candidate["contact_sheet"]).name
        write_contact_sheet(candidate, sheet, raw_map)
        del raw_map
    survey.pop("_selected_payload_hashes", None)
    survey_path.write_text(json.dumps(survey, indent=2, sort_keys=True) + "\n")
    print(
        json.dumps(
            {
                "status": survey["status"],
                "output": str(survey_path),
                "payload_count": survey["payload_count"],
                "candidates": [
                    {
                        "rank": item["rank"],
                        "variant": item["asset"]["variant"],
                        "status": item["status"],
                        "score": item["score"],
                        "center_full_map_cell_xz": item["source"]["center_full_map_cell_xz"],
                        "net_fall_m": item["route"]["raw_net_fall_m"],
                        "source_spread_m": item["source"]["raw_spread_m"],
                    }
                    for item in survey["ranked_candidates"]
                ],
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
