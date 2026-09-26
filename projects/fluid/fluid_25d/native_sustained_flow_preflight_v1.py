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
"""Bounded raw-face preflight for two pinned, immutable Terrain Diffusion crops.

This is an offline suitability audit. Priority-flood routes and local fill are
derived equilibrium proxies; they are not dynamic Fluid behavior or field
hydrology. The script does not modify terrain or create solver inputs.
"""

from __future__ import annotations

import argparse
import hashlib
import heapq
import json
import math
import sys
from pathlib import Path
from typing import Any

import numpy as np
import pyflwdir
from rasterio.transform import Affine
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "projects/fluid/fluid_25d"))
import terrain_hydro_read_v1 as hydro  # noqa: E402
import terrain_reach_scan_v1 as reach  # noqa: E402

SPACING_M = 30.0
CELL_AREA_M2 = SPACING_M**2
FILL_EPS_M = 0.01
SECTION_HALF_SPAN_CELLS = 13  # +/-390 m, matching the prior scanner's far-bank sample
FLOW_OFFSETS = {
    64: (-1, 0), 128: (-1, 1), 1: (0, 1), 2: (1, 1),
    4: (1, 0), 8: (1, -1), 16: (0, -1), 32: (-1, -1),
}
DIAGONAL_CODES = frozenset({2, 8, 32, 128})
FOUR_OFFSETS = ((-1, 0), (1, 0), (0, -1), (0, 1))
EIGHT_OFFSETS = FOUR_OFFSETS + ((-1, -1), (-1, 1), (1, -1), (1, 1))

# Fixed sites from the bounded discovery record. Hashes cover the transformed
# native 192x128 crop as little-endian float32, C row-major bytes.
CANDIDATES = (
    {
        "name": "rolling-wet-lowland short trunk",
        "source_sha256": "fd52d7c3b25f139ac709b8ced673ccc77d749cc33e4c52e66745494a86dcf7a2",
        "variant": "rolling-wet-lowland",
        "crop_xzwh": (0, 832, 192, 128),
        "crop_sha256_f32le_c": "676b693a5f14ca758af410b1fa039d50721c91e29ac6d831f4a05ec2b4137ac1",
        "route_search_xzwh": (128, 768, 512, 256),
        "route_start_hint_xz": (115, 890),
        "expected_d8_edge": "west",
    },
    {
        "name": "temperate-mountain-valley moderate floor reference",
        "source_sha256": "a978ecd435d2a161598d78ecf71221cba53b371e98c3525d18ebeab6665aa737",
        "variant": None,
        "crop_xzwh": (352, 877, 192, 128),
        "crop_sha256_f32le_c": "ce8ee9a0d5324177e203abd3fa598ea2f877db56ae61fe275f2ec4151b80d99d",
        "route_search_xzwh": (600, 900, 200, 200),
        "route_start_hint_xz": (450, 941),
        "expected_d8_edge": "west",
    },
)


def _validate_elevation(elevation: np.ndarray, connectivity: int | None = None) -> np.ndarray:
    array = np.asarray(elevation)
    if array.ndim != 2 or min(array.shape, default=0) < 3:
        raise ValueError("elevation must be a 2D array at least 3x3")
    if not np.isfinite(array).all():
        raise ValueError("elevation contains non-finite values")
    if connectivity is not None and connectivity not in (4, 8):
        raise ValueError("connectivity must be 4 or 8")
    return array


def _validate_seeds(elevation: np.ndarray, seeds: tuple[int, int] | list[tuple[int, int]]) -> list[tuple[int, int]]:
    items = [seeds] if isinstance(seeds, tuple) else list(seeds)
    if not items:
        raise ValueError("at least one source cell is required")
    height, width = elevation.shape
    checked = []
    for item in items:
        if len(item) != 2:
            raise ValueError("source cells must be (x, z) pairs")
        x, z = int(item[0]), int(item[1])
        if not (0 <= x < width and 0 <= z < height):
            raise ValueError(f"source cell {(x, z)} is outside elevation bounds")
        checked.append((x, z))
    # Preserve caller order for reported source-cell matrices while removing
    # duplicates from multi-source flood initialization.
    return list(dict.fromkeys(checked))


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def crop_sha256(crop: np.ndarray) -> str:
    """Hash transformed crop values as exact little-endian float32 row-major bytes."""
    array = np.asarray(crop)
    if array.ndim != 2 or min(array.shape, default=0) < 1:
        raise ValueError("crop hash input must be a nonempty 2D array")
    if not np.isfinite(array).all():
        raise ValueError("crop hash input contains non-finite values")
    encoded = np.ascontiguousarray(array, dtype="<f4")
    if not np.isfinite(encoded).all():
        raise ValueError("crop hash input is not representable as finite float32")
    payload = encoded.tobytes(order="C")
    return _sha256_bytes(payload)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_sha256(value: Any) -> str:
    payload = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return _sha256_bytes(payload)


def path_distances(path: list[tuple[int, int]], spacing_m: float = SPACING_M) -> np.ndarray:
    if not path:
        return np.zeros(0, dtype=np.float64)
    distances = np.zeros(len(path), dtype=np.float64)
    for i, ((x0, z0), (x1, z1)) in enumerate(zip(path, path[1:])):
        distances[i + 1] = distances[i] + spacing_m * math.hypot(x1 - x0, z1 - z0)
    return distances


def minimax_to_edges(
    elevation: np.ndarray,
    source_cells: tuple[int, int] | list[tuple[int, int]],
    connectivity: int = 4,
) -> dict[str, Any]:
    """Find the minimum raw-elevation saddle from source cells to each open edge."""
    raw = _validate_elevation(elevation, connectivity)
    seeds = _validate_seeds(raw, source_cells)
    height, width = raw.shape
    flat = np.asarray(raw, dtype=np.float64).ravel()
    best = np.full(flat.size, np.inf, dtype=np.float64)
    previous = np.full(flat.size, -1, dtype=np.int64)
    seed_indices = {z * width + x for x, z in seeds}
    queue: list[tuple[float, int]] = []
    for index in seed_indices:
        best[index] = flat[index]
        heapq.heappush(queue, (float(best[index]), index))

    offsets = FOUR_OFFSETS if connectivity == 4 else EIGHT_OFFSETS
    while queue:
        level, index = heapq.heappop(queue)
        if level != best[index]:
            continue
        z, x = divmod(index, width)
        for dz, dx in offsets:
            nx, nz = x + dx, z + dz
            if not (0 <= nx < width and 0 <= nz < height):
                continue
            neighbor = nz * width + nx
            candidate = max(level, float(flat[neighbor]))
            if candidate < best[neighbor]:
                best[neighbor] = candidate
                previous[neighbor] = index
                heapq.heappush(queue, (candidate, neighbor))

    edges = {
        "west": np.arange(0, height * width, width, dtype=np.int64),
        "east": np.arange(width - 1, height * width, width, dtype=np.int64),
        "north": np.arange(width, dtype=np.int64),
        "south": np.arange((height - 1) * width, height * width, dtype=np.int64),
    }
    source_min = min(float(flat[i]) for i in seed_indices)
    edge_results: dict[str, dict[str, Any]] = {}
    for side, indices in edges.items():
        exit_index = int(indices[np.argmin(best[indices])])
        z, x = divmod(exit_index, width)
        saddle = float(best[exit_index])
        edge_results[side] = {
            "saddle_m": saddle,
            "head_above_lowest_source_cell_m": saddle - source_min,
            "exit_crop_local_xz": [x, z],
        }
    first_edge = min(edge_results, key=lambda side: (edge_results[side]["saddle_m"], side))
    west_index = int(edges["west"][np.argmin(best[edges["west"]])])
    route: list[list[int]] = []
    node = west_index
    for _ in range(flat.size):
        z, x = divmod(node, width)
        route.append([x, z])
        if node in seed_indices:
            break
        node = int(previous[node])
        if node < 0:
            route.clear()
            break
    route.reverse()
    return {
        "connectivity": connectivity,
        "source_min_m": source_min,
        "edge_saddles": edge_results,
        "lowest_open_edge": first_edge,
        "lowest_open_saddle_m": edge_results[first_edge]["saddle_m"],
        "lowest_open_exit_crop_local_xz": edge_results[first_edge]["exit_crop_local_xz"],
        "west_saddle_m": edge_results["west"]["saddle_m"],
        "west_head_m": edge_results["west"]["head_above_lowest_source_cell_m"],
        "west_route_crop_local_xz": route,
    }


def sublevel_component(
    elevation: np.ndarray,
    source_cells: tuple[int, int] | list[tuple[int, int]],
    stage_m: float,
    connectivity: int = 4,
) -> np.ndarray:
    raw = _validate_elevation(elevation, connectivity)
    seeds = _validate_seeds(raw, source_cells)
    if not math.isfinite(stage_m):
        raise ValueError("stage must be finite")
    allowed = raw <= stage_m
    component = np.zeros(raw.shape, dtype=bool)
    stack = []
    for x, z in seeds:
        if allowed[z, x] and not component[z, x]:
            component[z, x] = True
            stack.append((x, z))
    offsets = FOUR_OFFSETS if connectivity == 4 else EIGHT_OFFSETS
    while stack:
        x, z = stack.pop()
        for dz, dx in offsets:
            nx, nz = x + dx, z + dz
            if 0 <= nx < raw.shape[1] and 0 <= nz < raw.shape[0] and allowed[nz, nx] and not component[nz, nx]:
                component[nz, nx] = True
                stack.append((nx, nz))
    return component


def boundary_summary(component: np.ndarray) -> dict[str, Any]:
    if component.ndim != 2:
        raise ValueError("component must be a 2D array")
    return {
        "west": bool(component[:, 0].any()),
        "east": bool(component[:, -1].any()),
        "north": bool(component[0, :].any()),
        "south": bool(component[-1, :].any()),
        "cell_count": int(np.count_nonzero(component)),
    }


def priority_flood(elevation: np.ndarray, connectivity: int = 4) -> tuple[np.ndarray, np.ndarray]:
    raw = _validate_elevation(elevation, connectivity)
    filled, directions = pyflwdir.dem.fill_depressions(
        np.asarray(raw, dtype=np.float32).copy(), outlets="edge", connectivity=connectivity
    )
    return np.asarray(filled, dtype=np.float32), np.asarray(directions)


def local_depression_audit(
    elevation: np.ndarray,
    source_cells: tuple[int, int] | list[tuple[int, int]],
    route_crop_local_xz: list[tuple[int, int]],
    connectivity: int = 4,
    filled_surface: np.ndarray | None = None,
    directions: np.ndarray | None = None,
) -> dict[str, Any]:
    """Report equilibrium fill components intersecting source cells or a route.

    Storage is an equilibrium proxy to the derived spill surface, not required
    transient mass, a lower bound on discharge, or evidence of a permanent lake.
    """
    raw = _validate_elevation(elevation, connectivity)
    seeds = _validate_seeds(raw, source_cells)
    route = _validate_seeds(raw, route_crop_local_xz) if route_crop_local_xz else []
    if filled_surface is None or directions is None:
        filled_surface, directions = priority_flood(raw, connectivity)
    filled = _validate_elevation(filled_surface, connectivity)
    if filled.shape != raw.shape or np.asarray(directions).shape != raw.shape:
        raise ValueError("filled surface and direction map must match elevation shape")

    delta = np.maximum(np.asarray(filled, dtype=np.float64) - np.asarray(raw, dtype=np.float64), 0.0)
    mask = delta > FILL_EPS_M
    structure = ndimage.generate_binary_structure(2, 1 if connectivity == 4 else 2)
    labels, _count = ndimage.label(mask, structure=structure)
    sizes = np.bincount(labels.ravel())
    volumes = np.bincount(labels.ravel(), weights=delta.ravel() * CELL_AREA_M2, minlength=sizes.size)
    source_labels = sorted({int(labels[z, x]) for x, z in seeds if labels[z, x] > 0})
    route_labels = sorted({int(labels[z, x]) for x, z in route if labels[z, x] > 0})

    def describe(label_ids: list[int]) -> list[dict[str, Any]]:
        output = []
        for label in label_ids:
            ys, xs = np.where(labels == label)
            selected = labels == label
            output.append({
                "cells": int(sizes[label]),
                "area_km2": float(sizes[label] * CELL_AREA_M2 / 1e6),
                "equilibrium_fill_proxy_m3": float(volumes[label]),
                "max_fill_depth_m": float(delta[selected].max()),
                "spill_level_min_m": float(np.min(filled[selected])),
                "spill_level_max_m": float(np.max(filled[selected])),
                "bbox_crop_local_xz": [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())],
            })
        return output

    source_depths = [float(delta[z, x]) for x, z in seeds]
    route_depths = [float(delta[z, x]) for x, z in route]
    diagonal_count = int(sum(np.count_nonzero(np.asarray(directions) == code) for code in DIAGONAL_CODES))
    return {
        "connectivity": connectivity,
        "meaning": "derived equilibrium local fill proxy; not required transient mass or a lower bound",
        "source_cell_fill_depths_m_z_major": [source_depths[i : i + 3] for i in range(0, len(source_depths), 3)] if len(seeds) == 9 else source_depths,
        "source_cells_with_positive_fill": int(np.count_nonzero(np.asarray(source_depths) > FILL_EPS_M)),
        "source_component_count": len(source_labels),
        "source_component_fill_proxy_m3": float(sum(volumes[label] for label in source_labels)),
        "source_components": describe(source_labels),
        "route_cells_with_positive_fill": int(np.count_nonzero(np.asarray(route_depths) > FILL_EPS_M)),
        "route_component_count": len(route_labels),
        "route_component_fill_proxy_sum_m3": float(sum(volumes[label] for label in route_labels)),
        "route_component_fill_proxy_max_m3": float(max((volumes[label] for label in route_labels), default=0.0)),
        "route_components": describe(route_labels),
        "direction_codes": sorted(int(value) for value in np.unique(directions)),
        "diagonal_direction_cell_count": diagonal_count,
        "cardinal_direction_only": diagonal_count == 0,
    }


def trace_direction_map(
    directions: np.ndarray,
    start_crop_local_xz: tuple[int, int],
    crop_origin_world_xz: tuple[int, int] = (0, 0),
    require_cardinal: bool = True,
) -> dict[str, Any]:
    direction_map = np.asarray(directions)
    if direction_map.ndim != 2 or not np.isfinite(direction_map).all():
        raise ValueError("direction map must be a finite 2D array")
    if not np.equal(direction_map, np.floor(direction_map)).all():
        raise ValueError("direction map codes must be integers")
    unknown_codes = {int(code) for code in np.unique(direction_map)} - set(FLOW_OFFSETS) - {0}
    if unknown_codes:
        raise ValueError(f"direction map contains unsupported codes: {sorted(unknown_codes)}")
    if require_cardinal and any(int(code) in DIAGONAL_CODES for code in np.unique(direction_map)):
        raise ValueError("direction map contains a diagonal direction")
    height, width = direction_map.shape
    x, z = (int(start_crop_local_xz[0]), int(start_crop_local_xz[1]))
    if not (0 <= x < width and 0 <= z < height):
        raise ValueError("start cell is outside the direction map")
    ox, oz = crop_origin_world_xz
    path = [(x, z)]
    visited = {(x, z)}
    termination = "direction-terminal"
    for _ in range(direction_map.size):
        code = int(direction_map[z, x])
        if code not in FLOW_OFFSETS:
            if x in (0, width - 1) or z in (0, height - 1):
                termination = "crop-edge-terminal"
            break
        dz, dx = FLOW_OFFSETS[code]
        nx, nz = x + dx, z + dz
        if not (0 <= nx < width and 0 <= nz < height):
            termination = "outward-edge-face"
            break
        if (nx, nz) in visited:
            raise RuntimeError("direction map contains a cycle from the requested start")
        visited.add((nx, nz))
        path.append((nx, nz))
        x, z = nx, nz
    else:
        raise RuntimeError("direction trace exceeded the map cell count")
    world_path = [(px + ox, pz + oz) for px, pz in path]
    return {
        "path_crop_local_xz": [list(point) for point in path],
        "path_world_xz": [list(point) for point in world_path],
        "length_m": float(path_distances(path)[-1]),
        "termination": termination,
        "contains_cycle": False,
        "cardinal_only": all(abs(x1 - x0) + abs(z1 - z0) == 1 for (x0, z0), (x1, z1) in zip(path, path[1:])),
    }


def raw_cardinal_descent(
    elevation: np.ndarray,
    start_crop_local_xz: tuple[int, int],
    crop_origin_world_xz: tuple[int, int] = (0, 0),
) -> dict[str, Any]:
    """Follow strictly lower four-neighbor raw cells; stop at first pit or flat."""
    raw = _validate_elevation(elevation)
    x, z = int(start_crop_local_xz[0]), int(start_crop_local_xz[1])
    if not (0 <= x < raw.shape[1] and 0 <= z < raw.shape[0]):
        raise ValueError("start cell is outside elevation bounds")
    ox, oz = crop_origin_world_xz
    path = [(x, z)]
    visited = {(x, z)}
    for _ in range(raw.size):
        here = float(raw[z, x])
        lower = []
        for dz, dx in FOUR_OFFSETS:
            nx, nz = x + dx, z + dz
            if 0 <= nx < raw.shape[1] and 0 <= nz < raw.shape[0] and (nx, nz) not in visited:
                level = float(raw[nz, nx])
                if level < here:
                    lower.append((level, nx, nz))
        if not lower:
            break
        _level, x, z = min(lower)
        if (x, z) in visited:
            raise RuntimeError("raw cardinal descent contains a cycle")
        visited.add((x, z))
        path.append((x, z))
    world_path = [(px + ox, pz + oz) for px, pz in path]
    crop_edge = x in (0, raw.shape[1] - 1) or z in (0, raw.shape[0] - 1)
    return {
        "method": "steepest strictly lower raw cardinal neighbor; no fill or flat resolution",
        "path_crop_local_xz": [list(point) for point in path],
        "path_world_xz": [list(point) for point in world_path],
        "length_m": float(path_distances(path)[-1]),
        "terminal_crop_local_xz": list(path[-1]),
        "terminal_world_xz": list(world_path[-1]),
        "terminal_m": float(raw[path[-1][1], path[-1][0]]),
        "termination": "crop-edge" if crop_edge else "raw-four-neighbor-pit-or-flat",
        "ended_at_raw_four_neighbor_pit_or_flat": not crop_edge,
    }


def _cross_section(
    elevation: np.ndarray,
    path_crop_local_xz: list[tuple[int, int]],
    index: int,
    depth_m: float,
    crop_origin_world_xz: tuple[int, int],
) -> dict[str, Any]:
    x, z = path_crop_local_xz[index]
    before = path_crop_local_xz[max(0, index - 2)]
    after = path_crop_local_xz[min(len(path_crop_local_xz) - 1, index + 2)]
    dx, dz = after[0] - before[0], after[1] - before[1]
    cross_axis = "z" if abs(dx) >= abs(dz) else "x"
    offsets = np.arange(-SECTION_HALF_SPAN_CELLS, SECTION_HALF_SPAN_CELLS + 1, dtype=np.int32)
    if cross_axis == "z":
        xs, zs = np.full(offsets.shape, x, dtype=np.int32), z + offsets
    else:
        xs, zs = x + offsets, np.full(offsets.shape, z, dtype=np.int32)
    inside = (xs >= 0) & (xs < elevation.shape[1]) & (zs >= 0) & (zs < elevation.shape[0])
    values = np.full(offsets.shape, np.inf, dtype=np.float64)
    values[inside] = elevation[zs[inside], xs[inside]]
    center = SECTION_HALF_SPAN_CELLS
    floor_index = int(np.argmin(values))
    floor_m = float(values[floor_index])
    stage_m = floor_m + depth_m
    wet = values <= stage_m
    left = right = floor_index
    while left > 0 and wet[left - 1]:
        left -= 1
    while right + 1 < wet.size and wet[right + 1]:
        right += 1
    area_m2 = float(np.maximum(stage_m - values[left : right + 1], 0.0).sum() * SPACING_M)
    ox, oz = crop_origin_world_xz
    floor_offset = (floor_index - center) * SPACING_M
    return {
        "center_crop_local_xz": [x, z],
        "center_world_xz": [x + ox, z + oz],
        "cross_axis": cross_axis,
        "cross_section_floor_m": floor_m,
        "route_cell_above_sampled_floor_m": float(elevation[z, x]) - floor_m,
        "sampled_floor_offset_from_route_cell_m": float(floor_offset),
        "sample_half_span_m": float(SECTION_HALF_SPAN_CELLS * SPACING_M),
        "width_under_local_stage_m": float((right - left + 1) * SPACING_M),
        "stage_above_sampled_floor_m": depth_m,
        "section_area_proxy_m2": area_m2,
    }


def route_profile(
    elevation: np.ndarray,
    path_crop_local_xz: list[tuple[int, int]],
    crop_origin_world_xz: tuple[int, int],
    route_kind: str,
) -> dict[str, Any]:
    raw = _validate_elevation(elevation)
    if not path_crop_local_xz:
        raise ValueError("route path is empty")
    for x, z in path_crop_local_xz:
        if not (0 <= x < raw.shape[1] and 0 <= z < raw.shape[0]):
            raise ValueError("route cell is outside elevation bounds")
    distances = path_distances(path_crop_local_xz)
    elevations = np.asarray([raw[z, x] for x, z in path_crop_local_xz], dtype=np.float64)
    delta = np.diff(elevations)
    step_m = np.diff(distances)
    rise = float(max(0.0, delta.max())) if delta.size else 0.0
    grade = float(np.max(np.abs(delta / step_m))) if step_m.size else 0.0
    origin_x, origin_z = crop_origin_world_xz
    stations = sorted(set([0, len(path_crop_local_xz) - 1] + [int(np.argmin(np.abs(distances - d))) for d in np.arange(0, distances[-1] + 1, 300.0)]))
    result: dict[str, Any] = {
        "route_kind": route_kind,
        "route_cells_crop_local_xz": [list(p) for p in path_crop_local_xz],
        "start_world_xz": [path_crop_local_xz[0][0] + origin_x, path_crop_local_xz[0][1] + origin_z],
        "end_world_xz": [path_crop_local_xz[-1][0] + origin_x, path_crop_local_xz[-1][1] + origin_z],
        "length_m": float(distances[-1]),
        "start_bed_m": float(elevations[0]),
        "end_bed_m": float(elevations[-1]),
        "net_fall_m": float(elevations[0] - elevations[-1]),
        "maximum_single_step_adverse_rise_m": rise,
        "maximum_absolute_one_step_grade": grade,
        "one_step_grade_p95": float(np.percentile(np.abs(delta / step_m), 95)) if step_m.size else 0.0,
        "cross_section_meaning": "sampled minima within +/-390 m; geometric width proxy, not proven connected water width",
        "stage_proxies": {},
    }
    for depth_m in (0.25, 0.5, 1.0, 3.0):
        sections = [_cross_section(raw, path_crop_local_xz, i, depth_m, crop_origin_world_xz) for i in range(len(path_crop_local_xz))]
        widths = np.asarray([section["width_under_local_stage_m"] for section in sections], dtype=np.float64)
        volume_m3 = 0.0
        for i in range(len(sections) - 1):
            ds = SPACING_M * math.hypot(path_crop_local_xz[i + 1][0] - path_crop_local_xz[i][0], path_crop_local_xz[i + 1][1] - path_crop_local_xz[i][1])
            volume_m3 += sections[i]["section_area_proxy_m2"] * ds
        result["stage_proxies"][f"local_floor_plus_{depth_m:.2f}m"] = {
            "meaning": "equilibrium geometry proxy; not required transient source mass",
            "min_sampled_width_m": float(widths.min()),
            "median_sampled_width_m": float(np.median(widths)),
            "p10_sampled_width_m": float(np.percentile(widths, 10)),
            "fraction_samples_under_90m": float(np.mean(widths < 90.0)),
            "integrated_section_volume_proxy_m3": float(volume_m3),
            "samples_every_about_300m": [sections[index] for index in stations],
        }
    return result


def _trace_full_map_d8(elevation: np.ndarray, search_xzwh: tuple[int, int, int, int]) -> tuple[list[tuple[int, int]], np.ndarray]:
    filled, directions = priority_flood(elevation, connectivity=8)
    del filled
    flow = pyflwdir.from_array(directions, ftype="d8", transform=Affine(SPACING_M, 0, 0, 0, -SPACING_M, 0))
    accumulation = np.asarray(flow.upstream_area(unit="cell"), dtype=np.float64) * CELL_AREA_M2 / 1e6
    x0, z0, width, height = search_xzwh
    window = accumulation[z0 : z0 + height, x0 : x0 + width]
    local_z, local_x = np.argwhere(window == window.max())[0]
    x, z = x0 + int(local_x), z0 + int(local_z)
    path = [(x, z)]
    visited = {(x, z)}
    for _ in range(elevation.size):
        code = int(directions[z, x])
        if code not in FLOW_OFFSETS:
            break
        dz, dx = FLOW_OFFSETS[code]
        nx, nz = x + dx, z + dz
        if not (0 <= nx < elevation.shape[1] and 0 <= nz < elevation.shape[0]):
            break
        if (nx, nz) in visited:
            raise RuntimeError("full-map D8 locator contains a cycle")
        path.append((nx, nz))
        visited.add((nx, nz))
        x, z = nx, nz
    return path, accumulation


def _analyze_candidate(asset: Any, inventory: dict[str, Any], spec: dict[str, Any]) -> dict[str, Any]:
    elevation, identity = hydro.load_asset(asset, inventory, preferred_variant=spec["variant"])
    if identity["elevation_sha256"] != spec["source_sha256"] or not np.isfinite(elevation).all():
        raise ValueError(f"{spec['name']}: source identity or finite-height gate failed")
    path, accumulation = _trace_full_map_d8(elevation, spec["route_search_xzwh"])
    hint_x, hint_z = spec["route_start_hint_xz"]
    route_index = int(np.argmin([(x - hint_x) ** 2 + (z - hint_z) ** 2 for x, z in path]))
    snap_distance_m = SPACING_M * math.hypot(path[route_index][0] - hint_x, path[route_index][1] - hint_z)
    cumulative = path_distances(path)
    end_index = min(int(np.searchsorted(cumulative, cumulative[route_index] + 1_200.0)), len(path) - 1)
    if end_index <= route_index or cumulative[end_index] - cumulative[route_index] < 600.0:
        raise ValueError(f"{spec['name']}: D8 locator did not yield a 600-1500 m segment")

    x0, z0, width, height = spec["crop_xzwh"]
    crop = np.ascontiguousarray(elevation[z0 : z0 + height, x0 : x0 + width], dtype=np.float32)
    if crop.shape != (height, width):
        raise ValueError(f"{spec['name']}: transformed crop has the wrong dimensions")
    actual_crop_hash = crop_sha256(crop)
    if actual_crop_hash != spec["crop_sha256_f32le_c"]:
        raise ValueError(f"{spec['name']}: transformed crop hash differs from its pin")
    d8_segment_world = path[route_index : end_index + 1]
    d8_segment_local = [(x - x0, z - z0) for x, z in d8_segment_world]
    if any(not (0 <= x < width and 0 <= z < height) for x, z in d8_segment_local):
        raise ValueError(f"{spec['name']}: target segment is outside the pinned crop")

    source_local = d8_segment_local[0]
    source_patch = [(source_local[0] + dx, source_local[1] + dz) for dz in (-1, 0, 1) for dx in (-1, 0, 1)]
    source_center_world = [source_local[0] + x0, source_local[1] + z0]
    patch_values = np.asarray([crop[z, x] for x, z in source_patch], dtype=np.float64)
    source_min_cell = source_patch[int(np.argmin(patch_values))]
    source_raw_descent = raw_cardinal_descent(crop, source_min_cell, (x0, z0))

    spill4 = minimax_to_edges(crop, source_patch, 4)
    spill8 = minimax_to_edges(crop, source_patch, 8)
    stage_tests = {}
    for depth_m in (0.25, 0.5, 1.0, 2.0):
        stage_m = float(patch_values.min() + depth_m)
        stage_tests[f"source_low_cell_plus_{depth_m:.2f}m"] = {
            "stage_m": stage_m,
            "four_neighbor": boundary_summary(sublevel_component(crop, source_patch, stage_m, 4)),
            "eight_neighbor": boundary_summary(sublevel_component(crop, source_patch, stage_m, 8)),
        }

    filled4, directions4 = priority_flood(crop, connectivity=4)
    d4_route = trace_direction_map(directions4, source_local, (x0, z0), require_cardinal=True)
    d4_full_local = [tuple(point) for point in d4_route["path_crop_local_xz"]]
    d4_full_dist = path_distances(d4_full_local)
    d4_end = min(int(np.searchsorted(d4_full_dist, 1_200.0)), len(d4_full_local) - 1)
    d4_segment_local = d4_full_local[: d4_end + 1]
    d4_fill = local_depression_audit(crop, source_patch, d4_segment_local, 4, filled4, directions4)

    filled8, directions8 = priority_flood(crop, connectivity=8)
    d8_fill = local_depression_audit(crop, source_patch, d8_segment_local, 8, filled8, directions8)
    d4_profile = route_profile(crop, d4_segment_local, (x0, z0), "priority-flood four-neighbor route")
    d8_profile = route_profile(crop, d8_segment_local, (x0, z0), "full-map D8 locator segment")

    first_edge = spill4["lowest_open_edge"]
    first_stage = spill4["lowest_open_saddle_m"]
    first_component = sublevel_component(crop, source_patch, first_stage, 4)
    fill_delta = np.maximum(first_stage - crop.astype(np.float64), 0.0)
    flat_stage_context = {
        "meaning": "source-connected flat-stage component across the crop; can include downstream valleys and is NOT required transient mass",
        "stage_m": float(first_stage),
        "lowest_open_edge": first_edge,
        "component_cells": int(first_component.sum()),
        "component_area_km2": float(first_component.sum() * CELL_AREA_M2 / 1e6),
        "flat_stage_volume_context_m3": float(fill_delta[first_component].sum() * CELL_AREA_M2),
    }

    return {
        "candidate": spec["name"],
        "source_identity": identity,
        "crop": {
            "world_xzwh": list(spec["crop_xzwh"]),
            "size_m": [width * SPACING_M, height * SPACING_M],
            "transformed_crop_sha256": actual_crop_hash,
            "expected_transformed_crop_sha256": spec["crop_sha256_f32le_c"],
            "transformed_crop_encoding": "little-endian float32, C row-major bytes after the pinned float32 height transform",
            "height_min_m": float(crop.min()),
            "height_max_m": float(crop.max()),
            "all_cells_finite": bool(np.isfinite(crop).all()),
        },
        "source_patch": {
            "center_world_xz": source_center_world,
            "world_xzwh": [source_local[0] + x0 - 1, source_local[1] + z0 - 1, 3, 3],
            "crop_local_xz_cells": [list(point) for point in source_patch],
            "raw_min_m": float(patch_values.min()),
            "raw_mean_m": float(patch_values.mean()),
            "raw_max_m": float(patch_values.max()),
            "raw_spread_m": float(patch_values.max() - patch_values.min()),
            "lowest_cell_raw_d4_descent": source_raw_descent,
            "local_equilibrium_fill_4n": d4_fill,
            "local_equilibrium_fill_8n_sensitivity": d8_fill,
        },
        "crop_and_exits": {
            "all_four_perimeter_edges_open": True,
            "expected_d8_locator_edge": spec["expected_d8_edge"],
            "four_neighbor_minimax_to_each_open_edge": spill4,
            "eight_neighbor_minimax_sensitivity": spill8,
            "source_low_cell_stage_connectivity": stage_tests,
            "priority_flood_d4_route_to_open_edge": d4_route,
            "flat_stage_context_not_budget": flat_stage_context,
        },
        "target_reach": {
            "d8_locator": d8_profile,
            "raw_crop_d4_route": d4_profile,
            "target_length_requested_m": 1_200.0,
        },
        "interpretation_limits": [
            "D8 is a route locator; crop D4 priority-flood routing is a derived route, not raw downhill proof.",
            "Minimax saddle levels use raw elevations and all crop edges open; they do not force an outlet.",
            "Local equilibrium fill components are not required transient mass, a lower bound, or a discharge estimate.",
            "Cross-section minima are searched only within +/-390 m and include their offset from the route cell; widths are not proven connected water widths.",
            "No terrain edits, initial water, numerical simulation, source rate, or sink were applied.",
        ],
    }


def synthetic_controls() -> dict[str, Any]:
    diagonal = np.full((7, 7), 9.0, dtype=np.float32)
    for index in range(1, 7):
        diagonal[index, index] = 0.0
    d4 = minimax_to_edges(diagonal, (1, 1), 4)
    d8 = minimax_to_edges(diagonal, (1, 1), 8)
    if (d4["lowest_open_saddle_m"], d8["lowest_open_saddle_m"]) != (9.0, 0.0):
        raise AssertionError("diagonal-only D4/D8 control failed")

    bowl = np.full((5, 5), 3.0, dtype=np.float32)
    bowl[1:4, 1:4] = np.asarray([[2, 1, 2], [1, 0, 1], [2, 1, 2]], dtype=np.float32)
    bowl_audit = local_depression_audit(bowl, (2, 2), [(2, 2)], 4)
    bowl_multi = local_depression_audit(bowl, [(2, 2), (2, 3), (3, 2)], [(2, 2)], 4)
    if bowl_audit["source_component_fill_proxy_m3"] != 13_500.0:
        raise AssertionError("known bowl local fill storage changed")
    if bowl_multi["source_component_fill_proxy_m3"] != bowl_audit["source_component_fill_proxy_m3"]:
        raise AssertionError("multi-source fill component was counted more than once")

    open_slope = np.tile(np.asarray([5, 4, 3, 2, 1], dtype=np.float32), (5, 1))
    slope_audit = local_depression_audit(open_slope, (2, 2), [(2, 2)], 4)
    if slope_audit["source_component_fill_proxy_m3"] != 0.0:
        raise AssertionError("open-slope local storage control changed")

    directions = np.zeros((3, 4), dtype=np.int16)
    directions[1, 1] = 1
    directions[1, 2] = 1
    trace = trace_direction_map(directions, (1, 1), (352, 877))
    if trace["path_crop_local_xz"] != [[1, 1], [2, 1], [3, 1]] or trace["path_world_xz"] != [[353, 878], [354, 878], [355, 878]]:
        raise AssertionError("cardinal route or crop-origin control failed")
    if not trace["cardinal_only"] or trace["contains_cycle"]:
        raise AssertionError("cardinal route control produced a diagonal or cycle")
    return {
        "status": "pass",
        "diagonal_only_d4_vs_d8_saddles_m": [d4["lowest_open_saddle_m"], d8["lowest_open_saddle_m"]],
        "known_bowl_local_equilibrium_fill_proxy_m3": bowl_audit["source_component_fill_proxy_m3"],
        "multi_source_same_component_fill_proxy_m3": bowl_multi["source_component_fill_proxy_m3"],
        "open_slope_local_equilibrium_fill_proxy_m3": slope_audit["source_component_fill_proxy_m3"],
        "direction_trace_cardinal_cycle_free_and_origin_checked": True,
    }


def build_audit() -> dict[str, Any]:
    assets, inventory = reach.load_pinned_assets(ROOT / "cache/terrain/sources/v1")
    by_hash = {asset.elevation_sha256: asset for asset in assets}
    results = [_analyze_candidate(by_hash[spec["source_sha256"]], inventory, spec) for spec in CANDIDATES]
    return {
        "schema": "cubey.fluid25d.native_sustained_flow_preflight.v1",
        "status": "bounded offline preflight; no water simulation",
        "runner": {
            "path": str(Path(__file__).resolve().relative_to(ROOT)),
            "sha256": sha256_file(Path(__file__).resolve()),
            "replay_command": "rtk proxy uv run --python 3.12 projects/fluid/fluid_25d/native_sustained_flow_preflight_v1.py --output-dir <fresh-output-dir>",
        },
        "pinned_inventory": inventory,
        "native_spacing_m": SPACING_M,
        "fixed_horizon_s": 7200,
        "source_rate": "No Q was frozen and no simulation was run; primary chooses common Q and 2Q before any pilot.",
        "synthetic_controls": synthetic_controls(),
        "deterministic_result_sha256": canonical_sha256(results),
        "candidates": results,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "outputs/fluid/native-sustained-flow-preflight-v1-20260925",
    )
    args = parser.parse_args()
    output_dir = args.output_dir.resolve()
    output_file = output_dir / "audit.json"
    if output_file.exists():
        raise SystemExit(f"refusing to overwrite existing evidence: {output_file}")
    output_dir.mkdir(parents=True, exist_ok=True)
    audit = build_audit()
    output_file.write_text(json.dumps(audit, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"status": audit["status"], "output": str(output_file), "runner_sha256": audit["runner"]["sha256"], "candidates": [{"name": item["candidate"], "crop_sha256": item["crop"]["transformed_crop_sha256"], "source_fill_m3": item["source_patch"]["local_equilibrium_fill_4n"]["source_component_fill_proxy_m3"], "d4_route_length_m": item["crop_and_exits"]["priority_flood_d4_route_to_open_edge"]["length_m"]} for item in audit["candidates"]]}, indent=2))


if __name__ == "__main__":
    main()
