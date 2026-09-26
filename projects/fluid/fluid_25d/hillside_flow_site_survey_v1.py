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
"""Bounded, immutable-terrain macro-site survey for a hillside flow study.

Full-map and crop D4 routing are analytical locators only. The raw transformed
Terrain Diffusion elevations remain untouched; no water simulation is run.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage
from skimage.measure import find_contours

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "projects/fluid/fluid_25d"))
import native_flow_site_survey_v1 as shared  # noqa: E402
import terrain_reach_scan_v1 as reach  # noqa: E402

SPACING_M = 30.0
DOMAIN_CELLS = 256
MIN_SOURCE_MARGIN_CELLS = 32
MAX_DOMAIN_EVALUATIONS_PER_MAP = 8
MAX_SHORTLIST = 4
ROUTE_LIMIT_M = 3_000.0
ROUTE_LIMIT_STEPS = int(ROUTE_LIMIT_M / SPACING_M)
PROFILE_STATIONS_M = (0, 300, 600, 1_000, 1_500, 2_000, 2_500, 3_000)
LANDSCAPE_VARIANTS = ("temperate-mountain-valley", "alpine-range")
DEFAULT_INVENTORY = ROOT / "outputs/fluid/native-flow-site-survey-v1-20260925/survey.json"
DEFAULT_OUTPUT = ROOT / "outputs/fluid/hillside-flow-v1-20260925/survey"
SCHEMA = "cubey.fluid25d.hillside_flow_site_survey.v1"
SOURCE_OFFSETS_XZ = ((0, 0), (0, -1), (0, 1), (1, 0), (-1, 0))


class SurveyError(RuntimeError):
    """Raised when an input does not match the pinned immutable terrain."""


def _asset_summary(inventory: dict[str, Any], variant: str) -> dict[str, Any]:
    matches = [
        item["asset"]
        for item in inventory.get("map_summaries", [])
        if item.get("asset", {}).get("variant") == variant
        and item["asset"].get("manifest_path", "").startswith(
            f"cache/terrain/sources/v1/landscape-variations/{variant}/"
        )
    ]
    if len(matches) != 1:
        raise SurveyError(f"expected one authoritative map_summaries record for {variant}, found {len(matches)}")
    return matches[0]


def load_pinned_map(variant: str, inventory_path: Path) -> tuple[np.ndarray, dict[str, Any]]:
    """Load one map exactly as the existing Terrain Diffusion reader does."""
    inventory = json.loads(inventory_path.read_text())
    asset = _asset_summary(inventory, variant)
    manifest_path = ROOT / asset["manifest_path"]
    elevation_path = ROOT / asset["elevation_path"]
    if shared.sha256_file(manifest_path) != asset["manifest_sha256"]:
        raise SurveyError(f"manifest hash differs from prior survey inventory for {variant}")
    if shared.sha256_file(elevation_path) != asset["elevation_sha256"]:
        raise SurveyError(f"elevation hash differs from prior survey inventory for {variant}")
    shape = tuple(int(value) for value in asset["shape_zx"])
    if len(shape) != 2 or min(shape) < DOMAIN_CELLS:
        raise SurveyError(f"{variant} payload is smaller than one native domain: {shape}")
    raw_bytes = np.memmap(elevation_path, dtype="<f4", mode="r", shape=shape)
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("height") != asset.get("height_transform"):
        raise SurveyError(f"manifest height transform differs from prior survey inventory for {variant}")
    if float(asset.get("spacing_m", 0.0)) != SPACING_M:
        raise SurveyError(f"{variant} is not the pinned 30 m native terrain")
    transformed = reach.transform_heightfield(raw_bytes, manifest)
    transformed = shared.validate_native_elevation(transformed, float(asset["spacing_m"]))
    identity = {
        "variant": variant,
        "seed": asset.get("seed"),
        "elevation_path": asset["elevation_path"],
        "elevation_sha256": asset["elevation_sha256"],
        "manifest_path": asset["manifest_path"],
        "manifest_sha256": asset["manifest_sha256"],
        "height_transform": asset["height_transform"],
        "shape_zx": list(shape),
        "spacing_m": float(asset["spacing_m"]),
        "aliases": asset.get("aliases", []),
        "transformed_full_map_sha256_f32le_c": shared.crop_sha256(transformed),
    }
    return transformed, identity


def describe_domains(elevation_m: np.ndarray, domain_cells: int = DOMAIN_CELLS) -> list[dict[str, Any]]:
    """Describe a deterministic, disjoint native tiling before the eight-site cap."""
    raw = shared.validate_native_elevation(elevation_m)
    height, width = raw.shape
    result: list[dict[str, Any]] = []
    for z0 in range(0, height - domain_cells + 1, domain_cells):
        for x0 in range(0, width - domain_cells + 1, domain_cells):
            crop = raw[z0 : z0 + domain_cells, x0 : x0 + domain_cells]
            p05, p25, p50, p75, p95 = np.percentile(crop, (5, 25, 50, 75, 95))
            gradients_z, gradients_x = np.gradient(crop, SPACING_M)
            slope = np.hypot(gradients_x, gradients_z)
            result.append(
                {
                    "bbox_full_map_cell_xzwh": [x0, z0, domain_cells, domain_cells],
                    "center_full_map_cell_xz": [x0 + domain_cells // 2, z0 + domain_cells // 2],
                    "height_min_m": float(crop.min()),
                    "height_max_m": float(crop.max()),
                    "domain_relief_m": float(crop.max() - crop.min()),
                    "height_p05_m": float(p05),
                    "height_p25_m": float(p25),
                    "height_p50_m": float(p50),
                    "height_p75_m": float(p75),
                    "height_p95_m": float(p95),
                    "robust_relief_p95_minus_p05_m": float(p95 - p05),
                    "central_relief_p75_minus_p25_m": float(p75 - p25),
                    "slope_p90_fraction": float(np.percentile(slope, 90)),
                    "transformed_crop_sha256_f32le_c": shared.crop_sha256(crop),
                }
            )
    return result


def select_diverse_domains(
    domains: list[dict[str, Any]],
    map_shape_zx: tuple[int, int],
    max_count: int = MAX_DOMAIN_EVALUATIONS_PER_MAP,
) -> list[dict[str, Any]]:
    """Choose at most eight relief-rich tiles with deterministic spatial spread."""
    if max_count < 0:
        raise ValueError("max_count cannot be negative")
    remaining = list(range(len(domains)))
    selected: list[int] = []
    if not remaining or max_count == 0:
        return []
    max_robust = max(float(item["robust_relief_p95_minus_p05_m"]) for item in domains)
    max_range = max(float(item["domain_relief_m"]) for item in domains)
    map_diagonal = math.hypot(map_shape_zx[1], map_shape_zx[0])

    def quality(index: int) -> float:
        item = domains[index]
        robust = float(item["robust_relief_p95_minus_p05_m"])
        domain_range = float(item["domain_relief_m"])
        return 0.7 * (robust / max_robust if max_robust > 0 else 0.0) + 0.3 * (domain_range / max_range if max_range > 0 else 0.0)

    first = min(
        remaining,
        key=lambda index: (
            -quality(index),
            -float(domains[index]["slope_p90_fraction"]),
            domains[index]["bbox_full_map_cell_xzwh"][1],
            domains[index]["bbox_full_map_cell_xzwh"][0],
        ),
    )
    selected.append(first)
    remaining.remove(first)
    while remaining and len(selected) < max_count:
        def diversified_score(index: int) -> tuple[float, float, float, int, int]:
            point = domains[index]["center_full_map_cell_xz"]
            distance = min(
                math.hypot(point[0] - domains[chosen]["center_full_map_cell_xz"][0], point[1] - domains[chosen]["center_full_map_cell_xz"][1])
                for chosen in selected
            )
            distance_fraction = distance / map_diagonal if map_diagonal else 0.0
            score = quality(index) * (0.35 + 0.65 * distance_fraction)
            bbox = domains[index]["bbox_full_map_cell_xzwh"]
            return (
                score,
                quality(index),
                float(domains[index]["slope_p90_fraction"]),
                -bbox[1],
                -bbox[0],
            )

        best = max(remaining, key=diversified_score)
        selected.append(best)
        remaining.remove(best)
    return [dict(domains[index]) for index in selected]


def _station_index(distance_m: int) -> int:
    return int(round(distance_m / SPACING_M))


def _route_profile(
    elevation_m: np.ndarray,
    path_full_map_xz: list[tuple[int, int]],
    domain_bbox: tuple[int, int, int, int],
    full_filled_m: np.ndarray,
    crop_filled_m: np.ndarray,
) -> list[dict[str, Any]]:
    x0, z0, width, height = domain_bbox
    output = []
    for requested_m in PROFILE_STATIONS_M:
        index = _station_index(requested_m)
        if index >= len(path_full_map_xz):
            output.append(
                {
                    "requested_distance_m": requested_m,
                    "available": False,
                    "reason": "D4 locator terminated before this station",
                }
            )
            continue
        x, z = path_full_map_xz[index]
        in_domain = x0 <= x < x0 + width and z0 <= z < z0 + height
        raw_value = float(elevation_m[z, x])
        crop_value = float(crop_filled_m[z - z0, x - x0]) if in_domain else None
        output.append(
            {
                "requested_distance_m": requested_m,
                "actual_path_distance_m": float(index * SPACING_M),
                "path_index": index,
                "available": True,
                "full_map_cell_xz": [x, z],
                "inside_domain": in_domain,
                "raw_elevation_m": raw_value,
                "fall_from_source_m": float(elevation_m[path_full_map_xz[0][1], path_full_map_xz[0][0]] - raw_value),
                "full_map_d4_fill_m": float(full_filled_m[z, x]),
                "crop_d4_fill_m": crop_value,
                "crop_d4_fill_available": in_domain,
            }
        )
    return output


def _path_stats(
    elevation_m: np.ndarray,
    path_full_map_xz: list[tuple[int, int]],
    source_elevation_m: float,
) -> dict[str, Any]:
    if not path_full_map_xz:
        return {
            "path_cells": 0,
            "length_m": 0.0,
            "termination": "empty",
            "early_fall_within_300m_m": None,
            "fall_samples_1_to_3km_m": [],
            "best_fall_1_to_3km_m": None,
            "best_fall_distance_m": None,
            "raw_face_grade_max_fraction": None,
            "raw_face_grade_p95_fraction": None,
        }
    values = np.asarray([elevation_m[z, x] for x, z in path_full_map_xz], dtype=np.float64)
    face_grades = np.abs(np.diff(values)) / SPACING_M
    early_index = _station_index(300)
    profile_indices = [_station_index(distance) for distance in (1_000, 1_500, 2_000, 2_500, 3_000)]
    available = [(index, float(source_elevation_m - values[index])) for index in profile_indices if index < len(values)]
    if available:
        best_index, best_fall = max(available, key=lambda item: (item[1], -item[0]))
    else:
        best_index, best_fall = None, None
    return {
        "path_cells": len(path_full_map_xz),
        "length_m": float(max(0, len(path_full_map_xz) - 1) * SPACING_M),
        "termination": "step-limit" if len(path_full_map_xz) > ROUTE_LIMIT_STEPS else "D4 terminal or native-map boundary",
        "early_fall_within_300m_m": float(source_elevation_m - values[early_index]) if early_index < len(values) else None,
        "fall_samples_1_to_3km_m": [
            {"distance_m": float(index * SPACING_M), "fall_m": fall}
            for index, fall in available
        ],
        "best_fall_1_to_3km_m": best_fall,
        "best_fall_distance_m": float(best_index * SPACING_M) if best_index is not None else None,
        "raw_face_grade_max_fraction": float(face_grades.max(initial=0.0)),
        "raw_face_grade_p95_fraction": float(np.percentile(face_grades, 95)) if face_grades.size else 0.0,
    }


def _source_candidates(
    elevation_m: np.ndarray,
    directions: np.ndarray,
    local_median_m: np.ndarray,
    bbox: tuple[int, int, int, int],
) -> list[tuple[int, int]]:
    x0, z0, width, height = bbox
    inner = elevation_m[z0 + MIN_SOURCE_MARGIN_CELLS : z0 + height - MIN_SOURCE_MARGIN_CELLS,
                        x0 + MIN_SOURCE_MARGIN_CELLS : x0 + width - MIN_SOURCE_MARGIN_CELLS]
    inner_directions = directions[z0 + MIN_SOURCE_MARGIN_CELLS : z0 + height - MIN_SOURCE_MARGIN_CELLS,
                                   x0 + MIN_SOURCE_MARGIN_CELLS : x0 + width - MIN_SOURCE_MARGIN_CELLS]
    if inner.size == 0:
        raise SurveyError("domain cannot contain the required source interior")
    threshold = float(np.percentile(inner, 80))
    y0, x1 = z0 + MIN_SOURCE_MARGIN_CELLS, x0 + MIN_SOURCE_MARGIN_CELLS
    valid = np.isin(inner_directions, tuple(shared.FLOW_OFFSETS_DZDX))
    peaks = inner >= ndimage.maximum_filter(inner, size=15, mode="nearest")
    selected = valid & peaks & (inner >= threshold)
    if not np.any(selected):
        selected = valid & (inner >= threshold)
    if not np.any(selected):
        selected = peaks & (inner >= threshold)
    if not np.any(selected):
        selected = np.ones(inner.shape, dtype=bool)
    rows, cols = np.nonzero(selected)
    points = [(int(x1 + x), int(y0 + z)) for z, x in zip(rows, cols, strict=True)]
    points.sort(
        key=lambda point: (
            -float(local_median_m[point[1], point[0]]),
            -float(elevation_m[point[1], point[0]]),
            point[1],
            point[0],
        )
    )
    return points[:96]


def _select_source(
    elevation_m: np.ndarray,
    full_context: dict[str, Any],
    local_median_m: np.ndarray,
    bbox: tuple[int, int, int, int],
) -> tuple[tuple[int, int], dict[str, Any], dict[str, Any]]:
    x0, z0, width, height = bbox
    interior = elevation_m[z0 + MIN_SOURCE_MARGIN_CELLS : z0 + height - MIN_SOURCE_MARGIN_CELLS,
                           x0 + MIN_SOURCE_MARGIN_CELLS : x0 + width - MIN_SOURCE_MARGIN_CELLS]
    interior_p80 = float(np.percentile(interior, 80))
    interior_p95 = float(np.percentile(interior, 95))
    directions = full_context["directions"]
    viable: list[tuple[tuple[Any, ...], tuple[int, int], dict[str, Any], dict[str, Any]]] = []
    for sx, sz in _source_candidates(elevation_m, directions, local_median_m, bbox):
        if int(directions[sz, sx]) not in shared.FLOW_OFFSETS_DZDX:
            trace = {
                "path_full_map_cell_xz": [[sx, sz]],
                "termination": "direction-terminal",
                "exit_edge": None,
                "exit_crop_cell_xz": None,
                "cardinal_only": True,
                "cycle_free": True,
            }
        else:
            trace = shared.trace_cardinal(directions, (sx, sz), max_steps=ROUTE_LIMIT_STEPS)
        path = [tuple(point) for point in trace["path_full_map_cell_xz"]]
        source_elevation = float(elevation_m[sz, sx])
        stats = _path_stats(elevation_m, path, source_elevation)
        fall = stats["best_fall_1_to_3km_m"]
        early = stats["early_fall_within_300m_m"]
        prominence = float(elevation_m[sz, sx] - local_median_m[sz, sx])
        high_ground_fraction = (source_elevation - interior_p80) / max(1e-6, interior_p95 - interior_p80)
        key = (
            bool(fall is not None and fall >= 100.0),
            -math.inf if fall is None else fall,
            bool(early is not None and early > 0.0),
            -math.inf if early is None else early,
            prominence,
            high_ground_fraction,
            source_elevation,
            -sz,
            -sx,
        )
        viable.append((key, (sx, sz), trace, stats))
    if not viable:
        raise SurveyError(f"no candidate source inside domain {bbox}")
    _, source, trace, stats = max(viable, key=lambda item: item[0])
    return source, trace, stats


def _inside_points(
    points: list[tuple[int, int]], bbox: tuple[int, int, int, int]
) -> list[tuple[int, int]]:
    x0, z0, width, height = bbox
    return [point for point in points if x0 <= point[0] < x0 + width and z0 <= point[1] < z0 + height]


def _fill_summary(delta_m: np.ndarray, points: list[tuple[int, int]]) -> dict[str, Any]:
    values = [float(delta_m[z, x]) for x, z in points]
    return {
        "sampled_cell_count": len(values),
        "positive_fill_cell_count_gt_0_01m": int(sum(value > shared.FILL_COMPONENT_EPS_M for value in values)),
        "max_fill_depth_m": max(values, default=0.0),
        "mean_fill_depth_m": float(np.mean(values)) if values else 0.0,
        "meaning": "equilibrium fill on an analytical copy; not observed water, required storage, or discharge",
    }


def evaluate_domain(
    elevation_m: np.ndarray,
    full_context: dict[str, Any],
    domain: dict[str, Any],
    local_median_m: np.ndarray | None = None,
) -> dict[str, Any]:
    """Measure one 256x256 domain without requiring a route or edge outlet."""
    raw = shared.validate_native_elevation(elevation_m)
    bbox = tuple(int(value) for value in domain["bbox_full_map_cell_xzwh"])
    x0, z0, width, height = bbox
    if width != DOMAIN_CELLS or height != DOMAIN_CELLS:
        raise ValueError("hillside survey evaluates only fixed 256x256 native domains")
    if x0 < 0 or z0 < 0 or x0 + width > raw.shape[1] or z0 + height > raw.shape[0]:
        raise ValueError("domain leaves the native map")
    if local_median_m is None:
        local_median_m = ndimage.median_filter(raw, size=17, mode="nearest")
    crop = np.ascontiguousarray(raw[z0 : z0 + height, x0 : x0 + width], dtype=np.float32)
    crop_filled, crop_directions = shared.priority_flood_d4(crop)
    crop_components = shared.fill_components(crop, crop_filled)
    source, full_trace, full_route_stats = _select_source(raw, full_context, local_median_m, bbox)
    sx, sz = source
    crop_start = (sx - x0, sz - z0)
    crop_trace = shared.trace_cardinal(
        crop_directions,
        crop_start,
        max_steps=ROUTE_LIMIT_STEPS,
        origin_full_map_xz=(x0, z0),
    )
    full_path = [tuple(point) for point in full_trace["path_full_map_cell_xz"]]
    crop_path = [tuple(point) for point in crop_trace["path_full_map_cell_xz"]]
    source_cells = shared.five_cell_source(source, raw.shape)
    source_margin = [sx - x0, x0 + width - 1 - sx, sz - z0, z0 + height - 1 - sz]
    if min(source_margin) < MIN_SOURCE_MARGIN_CELLS:
        raise SurveyError("source selector violated the 32-cell domain interior rule")
    full_points = list(dict.fromkeys(source_cells + full_path))
    crop_route_points = _inside_points(crop_path, bbox)
    full_route_inside = _inside_points(full_path, bbox)
    crop_points = list(dict.fromkeys(source_cells + crop_route_points))
    full_fill_profile = shared.compare_full_and_crop_fill(
        raw,
        full_context["filled_m"],
        crop_filled,
        bbox,
        list(dict.fromkeys(source_cells + full_route_inside + crop_route_points)),
    )
    crop_fill_delta = np.maximum(crop_filled - crop, 0.0).astype(np.float32)
    source_crop = [(x - x0, z - z0) for x, z in source_cells]
    crop_route_local = [(x - x0, z - z0) for x, z in crop_route_points]
    source_crop_components = shared.unique_touched_components(crop_components, source_crop, crop_route_local)
    full_map_components = shared.unique_touched_components(full_context["components"], source_cells, full_path)
    full_delta = full_context["components"]["delta_m"]
    full_profile = _route_profile(raw, full_path, bbox, full_context["filled_m"], crop_filled)
    crop_profile = _route_profile(raw, crop_path, bbox, full_context["filled_m"], crop_filled)
    source_values = [float(raw[z, x]) for x, z in source_cells]
    full_source_route_points = list(dict.fromkeys(source_cells + full_path))
    crop_source_route_points = list(dict.fromkeys(source_crop + crop_route_local))
    full_fill = _fill_summary(full_delta, full_source_route_points)
    crop_fill = _fill_summary(crop_fill_delta, crop_source_route_points)
    full_path_stats = _path_stats(raw, full_path, float(raw[sz, sx]))
    crop_path_stats = _path_stats(raw, crop_path, float(raw[sz, sx]))
    in_crop_mask = [x0 <= x < x0 + width and z0 <= z < z0 + height for x, z in full_path]
    first_outside = next((index for index, inside in enumerate(in_crop_mask) if not inside), None)
    margins_to_map = [sx, raw.shape[1] - 1 - sx, sz, raw.shape[0] - 1 - sz]
    interior_values = raw[z0 + MIN_SOURCE_MARGIN_CELLS : z0 + height - MIN_SOURCE_MARGIN_CELLS,
                          x0 + MIN_SOURCE_MARGIN_CELLS : x0 + width - MIN_SOURCE_MARGIN_CELLS]
    fall_values = full_path_stats["fall_samples_1_to_3km_m"]
    best_fall = full_path_stats["best_fall_1_to_3km_m"]
    early_fall = full_path_stats["early_fall_within_300m_m"]
    preferences = {
        "domain_relief_about_150m_or_more": {"observed_m": float(domain["domain_relief_m"]), "meets_preference": bool(domain["domain_relief_m"] >= 150.0)},
        "robust_relief_about_100m_or_more": {"observed_m": float(domain["robust_relief_p95_minus_p05_m"]), "meets_preference": bool(domain["robust_relief_p95_minus_p05_m"] >= 100.0)},
        "source_is_upland": {
            "elevation_m": float(raw[sz, sx]),
            "interior_p80_m": float(np.percentile(interior_values, 80)),
            "meets_preference": bool(raw[sz, sx] >= np.percentile(interior_values, 80)),
        },
        "downhill_fall_about_100m_within_1_to_3km": {"observed_best_m": best_fall, "meets_preference": bool(best_fall is not None and best_fall >= 100.0)},
        "some_early_fall_within_300m": {"observed_m": early_fall, "meets_preference": bool(early_fall is not None and early_fall > 0.0)},
        "source_footprint_flatness": {"spread_m": float(max(source_values) - min(source_values)), "is_a_gate": False},
        "domain_edge_outlet": {"required": False, "full_map_route_terminated": full_trace["termination"] != "step-limit", "crop_route_exit_edge": crop_trace["exit_edge"]},
    }
    preference_score = (
        0.28 * min(1.0, float(domain["robust_relief_p95_minus_p05_m"]) / 180.0)
        + 0.18 * min(1.0, float(domain["domain_relief_m"]) / 300.0)
        + 0.34 * (0.0 if best_fall is None else min(1.0, max(0.0, best_fall) / 180.0))
        + 0.10 * (0.0 if early_fall is None else min(1.0, max(0.0, early_fall) / 40.0))
        + 0.10 * min(1.0, max(0.0, float(raw[sz, sx] - np.percentile(interior_values, 80))) / 80.0)
    )
    route_bbox = [list(point) for point in full_path]
    identity = f"x{x0:04d}-z{z0:04d}-src{sx:04d}-{sz:04d}"
    return {
        "candidate_id": identity,
        "domain": {**domain, "bbox_full_map_cell_xzwh": list(bbox), "native_size_m": [width * SPACING_M, height * SPACING_M], "transformed_domain_sha256_f32le_c": shared.crop_sha256(crop)},
        "source": {
            "center_full_map_cell_xz": [sx, sz],
            "center_domain_cell_xz": list(crop_start),
            "interior_margin_cells_west_east_north_south": source_margin,
            "native_map_edge_distance_cells_west_east_north_south": margins_to_map,
            "footprint": "center plus four cardinal face-neighbors at native 30 m spacing",
            "cells_full_map_xz": [list(point) for point in source_cells],
            "raw_elevations_m_center_north_south_east_west": source_values,
            "raw_min_m": min(source_values),
            "raw_mean_m": float(np.mean(source_values)),
            "raw_max_m": max(source_values),
            "raw_spread_m": float(max(source_values) - min(source_values)),
            "local_median_prominence_m": float(raw[sz, sx] - local_median_m[sz, sx]),
            "interior_elevation_percentile": float(100.0 * np.mean(interior_values <= raw[sz, sx])),
            "source_full_map_d4_direction_code": int(full_context["directions"][sz, sx]),
            "source_area_locator_km2": float(full_context["accumulation_km2"][sz, sx]),
        },
        "preference_score_0_to_1": float(preference_score),
        "preferences": preferences,
        "full_map_route": {
            "method": "full-map D4 priority-flood direction; analytical locator only",
            "cardinal_only": True,
            "termination": full_trace["termination"],
            "native_map_exit_edge": full_trace["exit_edge"],
            "path_cells": len(full_path),
            "path_length_m": float(max(0, len(full_path) - 1) * SPACING_M),
            "path_full_map_cell_xz": route_bbox,
            "raw_profile": full_profile,
            "fall_and_facegrade_summary": full_path_stats,
        },
        "crop_route": {
            "method": "independent 256x256 crop D4 priority-flood direction; analytical locator only",
            "cardinal_only": True,
            "termination": crop_trace["termination"],
            "domain_exit_edge": crop_trace["exit_edge"],
            "exit_cell_domain_xz": crop_trace["exit_crop_cell_xz"],
            "path_length_m": float(max(0, len(crop_path) - 1) * SPACING_M),
            "path_full_map_cell_xz": [list(point) for point in crop_path],
            "raw_profile": crop_profile,
            "fall_and_facegrade_summary": crop_path_stats,
        },
        "route_context": {
            "full_map_path_cells_inside_domain": int(sum(in_crop_mask)),
            "full_map_path_first_leaves_domain_at_distance_m": None if first_outside is None else float(first_outside * SPACING_M),
            "full_map_path_reaches_domain_edge": first_outside is not None,
            "full_map_context_retained_beyond_crop": True,
            "route_path_does_not_imply_observed_or_simulated_water": True,
        },
        "depressions": {
            "full_map_source_and_full_route": full_map_components,
            "crop_source_and_crop_route": source_crop_components,
            "full_map_sampled_fill_depths": full_fill,
            "crop_sampled_fill_depths": crop_fill,
            "full_map_vs_crop_fill_on_source_and_both_routes_inside_domain": full_fill_profile,
            "crop_fill_lowering_warning": {
                "lowering_gt_0_01m_cell_count": full_fill_profile["samples_lowered_by_crop_more_than_0_01m"],
                "maximum_crop_lowering_m": full_fill_profile["maximum_crop_lowering_m"],
                "warning_only": True,
                "interpretation": "crop perimeter can expose an artificial lower path; full-map evidence remains separately reported",
            },
            "crop_edge_context": {
                "all_perimeter_cells_are_artificial_crop_boundaries": True,
                "crop_route_exit_edge": crop_trace["exit_edge"],
                "crop_route_exit_cell_domain_xz": crop_trace["exit_crop_cell_xz"],
                "crop_route_exit_is_not_a_natural_outlet_claim": True,
                "no_exit_required": True,
            },
        },
        "deterministic_evidence_sha256": shared.sha256_json(
            {
                "candidate_id": identity,
                "domain_sha256": shared.crop_sha256(crop),
                "source": [sx, sz],
                "source_elevations_m": source_values,
                "full_map_route": route_bbox,
                "crop_route": [list(point) for point in crop_path],
                "preferences": preferences,
            }
        ),
    }


def select_shortlist(candidates: list[dict[str, Any]], count: int = MAX_SHORTLIST) -> list[dict[str, Any]]:
    """Keep four visually varied sites while representing both pinned maps."""
    if count <= 0:
        return []
    ordered = sorted(
        candidates,
        key=lambda item: (
            -float(item["preference_score_0_to_1"]),
            -float(item["domain"]["robust_relief_p95_minus_p05_m"]),
            item["domain"]["bbox_full_map_cell_xzwh"][1],
            item["domain"]["bbox_full_map_cell_xzwh"][0],
        ),
    )
    selected: list[dict[str, Any]] = []
    for variant in LANDSCAPE_VARIANTS:
        best = next((item for item in ordered if item.get("asset_variant") == variant), None)
        if best is not None and best not in selected:
            selected.append(best)
    per_variant: dict[str, int] = {}
    for item in selected:
        variant = item["asset_variant"]
        per_variant[variant] = per_variant.get(variant, 0) + 1
    for item in ordered:
        if item in selected or per_variant.get(item["asset_variant"], 0) >= 2:
            continue
        point = item["domain"]["center_full_map_cell_xz"]
        if any(
            chosen["asset_variant"] == item["asset_variant"]
            and math.hypot(
                point[0] - chosen["domain"]["center_full_map_cell_xz"][0],
                point[1] - chosen["domain"]["center_full_map_cell_xz"][1],
            ) < 384.0
            for chosen in selected
        ):
            continue
        selected.append(item)
        per_variant[item["asset_variant"]] = per_variant.get(item["asset_variant"], 0) + 1
        if len(selected) >= count:
            break
    return selected[:count]


def _hillshade(elevation: np.ndarray) -> np.ndarray:
    values = np.asarray(elevation, dtype=np.float32)
    dz, dx = np.gradient(values, SPACING_M)
    slope = np.hypot(dx, dz)
    light = (0.70 - 0.50 * dx + 0.34 * dz) / np.sqrt(1.0 + slope * slope)
    return np.clip((light - 0.16) / 0.84 * 220.0 + 22.0, 0, 255).astype(np.uint8)


def _draw_contours(draw: ImageDraw.ImageDraw, elevation: np.ndarray, box: tuple[int, int, int, int], levels: np.ndarray, color: tuple[int, int, int], scale: tuple[float, float] = (1.0, 1.0), offset: tuple[int, int] = (0, 0), width: int = 1) -> None:
    x0, z0, _, _ = box
    ox, oz = offset
    scale_x, scale_z = scale
    for level in levels:
        for contour in find_contours(elevation, float(level)):
            points = [
                (ox + int(round((x0 + col) * scale_x)), oz + int(round((z0 + row) * scale_z)))
                for row, col in contour
            ]
            if len(points) > 1:
                draw.line(points, fill=color, width=width)


def _draw_domain_map(candidate: dict[str, Any], raw_map: np.ndarray, size: tuple[int, int]) -> Image.Image:
    x0, z0, width, height = candidate["domain"]["bbox_full_map_cell_xzwh"]
    crop = raw_map[z0 : z0 + height, x0 : x0 + width]
    image = Image.fromarray(_hillshade(crop), mode="L").convert("RGB").resize(size, Image.Resampling.BILINEAR)
    draw = ImageDraw.Draw(image)
    scale_x, scale_z = size[0] / width, size[1] / height
    levels = np.linspace(float(crop.min()), float(crop.max()), 13)[1:-1]
    _draw_contours(draw, crop, (0, 0, width, height), levels, (220, 213, 171), scale=(scale_x, scale_z), width=1)
    full_route = candidate["full_map_route"]["path_full_map_cell_xz"]
    pts = [(int(round((x - x0) * scale_x)), int(round((z - z0) * scale_z))) for x, z in full_route if x0 <= x < x0 + width and z0 <= z < z0 + height]
    if len(pts) > 1:
        draw.line(pts, fill=(52, 235, 240), width=3)
    crop_route = candidate["crop_route"]["path_full_map_cell_xz"]
    crop_pts = [(int(round((x - x0) * scale_x)), int(round((z - z0) * scale_z))) for x, z in crop_route if x0 <= x < x0 + width and z0 <= z < z0 + height]
    if len(crop_pts) > 1:
        draw.line(crop_pts, fill=(255, 205, 70), width=2)
    sx, sz = candidate["source"]["center_full_map_cell_xz"]
    center = (int(round((sx - x0) * scale_x)), int(round((sz - z0) * scale_z)))
    for cx, cz in candidate["source"]["cells_full_map_xz"]:
        px, pz = int(round((cx - x0) * scale_x)), int(round((cz - z0) * scale_z))
        draw.rectangle((px - 3, pz - 3, px + 3, pz + 3), outline=(255, 65, 160), width=2)
    draw.ellipse((center[0] - 7, center[1] - 7, center[0] + 7, center[1] + 7), fill=(255, 65, 160), outline=(255, 255, 255), width=2)
    return image


def _draw_full_map_context(candidate: dict[str, Any], raw_map: np.ndarray, tested_domains: list[dict[str, Any]], size: tuple[int, int]) -> Image.Image:
    image = Image.fromarray(_hillshade(raw_map), mode="L").convert("RGB").resize(size, Image.Resampling.BILINEAR)
    draw = ImageDraw.Draw(image)
    scale_x, scale_z = size[0] / raw_map.shape[1], size[1] / raw_map.shape[0]
    for domain in tested_domains:
        x0, z0, width, height = domain["bbox_full_map_cell_xzwh"]
        draw.rectangle((round(x0 * scale_x), round(z0 * scale_z), round((x0 + width) * scale_x), round((z0 + height) * scale_z)), outline=(255, 90, 165), width=1)
    x0, z0, width, height = candidate["domain"]["bbox_full_map_cell_xzwh"]
    draw.rectangle((round(x0 * scale_x), round(z0 * scale_z), round((x0 + width) * scale_x), round((z0 + height) * scale_z)), outline=(255, 224, 68), width=3)
    points = [(int(round(x * scale_x)), int(round(z * scale_z))) for x, z in candidate["full_map_route"]["path_full_map_cell_xz"]]
    if len(points) > 1:
        draw.line(points, fill=(44, 240, 242), width=2)
    sx, sz = candidate["source"]["center_full_map_cell_xz"]
    px, pz = int(round(sx * scale_x)), int(round(sz * scale_z))
    draw.ellipse((px - 5, pz - 5, px + 5, pz + 5), fill=(255, 65, 160), outline=(255, 255, 255), width=1)
    return image


def _draw_profile(candidate: dict[str, Any], size: tuple[int, int]) -> Image.Image:
    image = Image.new("RGB", size, (27, 33, 41))
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()
    margin = (52, 26, 18, 42)
    x0, y0 = margin[0], margin[1]
    plot_w, plot_h = size[0] - margin[0] - margin[2], size[1] - margin[1] - margin[3]
    draw.rectangle((x0, y0, x0 + plot_w, y0 + plot_h), outline=(126, 139, 154))
    profiles = [candidate["full_map_route"]["raw_profile"], candidate["crop_route"]["raw_profile"]]
    available = [point for profile in profiles for point in profile if point.get("available")]
    values = [point["raw_elevation_m"] for point in available]
    values += [point["full_map_d4_fill_m"] for profile in profiles for point in profile if point.get("available")]
    values += [point["crop_d4_fill_m"] for profile in profiles for point in profile if point.get("available") and point.get("crop_d4_fill_m") is not None]
    low, high = (min(values), max(values)) if values else (0.0, 1.0)
    if high - low < 1e-5:
        high = low + 1.0

    def project(point: dict[str, Any]) -> tuple[int, int]:
        distance = float(point.get("actual_path_distance_m", point["requested_distance_m"]))
        px = x0 + int(round(min(ROUTE_LIMIT_M, distance) / ROUTE_LIMIT_M * plot_w))
        py = y0 + plot_h - int(round((float(point["raw_elevation_m"]) - low) / (high - low) * plot_h))
        return px, py

    colors = ((58, 229, 235), (255, 202, 70))
    for profile, color in zip(profiles, colors, strict=True):
        points = [project(point) for point in profile if point.get("available")]
        if len(points) > 1:
            draw.line(points, fill=color, width=3)
    for profile, color in zip(profiles, colors, strict=True):
        points = []
        for point in profile:
            if not point.get("available"):
                continue
            fill_value = point.get("crop_d4_fill_m")
            if fill_value is None:
                continue
            adjusted = dict(point, raw_elevation_m=float(fill_value))
            points.append(project(adjusted))
        if len(points) > 1:
            draw.line(points, fill=tuple(int(c * 0.65) for c in color), width=1)
    for index, distance in enumerate(PROFILE_STATIONS_M):
        px = x0 + int(round(distance / ROUTE_LIMIT_M * plot_w))
        draw.text((px - 8, y0 + plot_h + 6), f"{distance // 1000}k" if distance >= 1_000 else str(distance), fill=(190, 200, 211), font=font)
    draw.text((x0, 6), "Raw elevations and separate full/crop D4 fill profiles", fill=(241, 244, 247), font=font)
    draw.text((4, y0), f"{high:.0f}m", fill=(190, 200, 211), font=font)
    draw.text((4, y0 + plot_h - 10), f"{low:.0f}m", fill=(190, 200, 211), font=font)
    draw.text((x0, size[1] - 14), "cyan: full-map route | amber: crop route | dim: crop fill", fill=(190, 200, 211), font=font)
    return image


def write_candidate_visual(
    candidate: dict[str, Any], raw_map: np.ndarray, tested_domains: list[dict[str, Any]], output_path: Path
) -> None:
    domain_panel = _draw_domain_map(candidate, raw_map, (860, 720))
    overview = _draw_full_map_context(candidate, raw_map, tested_domains, (460, 460))
    profile = _draw_profile(candidate, (460, 250))
    canvas = Image.new("RGB", (1380, 1010), (20, 25, 31))
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default()
    variant = candidate["asset_variant"]
    x0, z0, width, height = candidate["domain"]["bbox_full_map_cell_xzwh"]
    source = candidate["source"]["center_full_map_cell_xz"]
    draw.text((18, 12), f"{candidate['rank']}. {variant}  |  source {source}  |  256 x 256 at 30 m", fill=(247, 248, 249), font=font)
    draw.text((18, 30), f"domain relief {candidate['domain']['domain_relief_m']:.1f} m  |  robust P95-P05 {candidate['domain']['robust_relief_p95_minus_p05_m']:.1f} m  |  native bbox [{x0}, {z0}, {width}, {height}]", fill=(215, 222, 229), font=font)
    draw.text((18, 48), "Analytical D4 locators and priority-flood fill only; unchanged raw terrain; no water simulation", fill=(247, 198, 132), font=font)
    canvas.paste(domain_panel, (18, 76))
    draw.text((900, 76), "Full-map context; pink = evaluated domains, yellow = this site", fill=(228, 232, 236), font=font)
    canvas.paste(overview, (900, 98))
    canvas.paste(profile, (900, 574))
    draw = ImageDraw.Draw(canvas)
    full_stats = candidate["full_map_route"]["fall_and_facegrade_summary"]
    best_fall = full_stats["best_fall_1_to_3km_m"]
    early = full_stats["early_fall_within_300m_m"]
    draw.text((18, 815), f"Source five-cell elevations (center,N,S,E,W): {', '.join(f'{v:.1f}' for v in candidate['source']['raw_elevations_m_center_north_south_east_west'])} m; spread {candidate['source']['raw_spread_m']:.1f} m", fill=(220, 226, 231), font=font)
    draw.text((18, 833), f"Full-map route: {full_stats['length_m']:.0f} m observed; best raw fall at 1-3 km {best_fall if best_fall is not None else 'unavailable'} m; raw fall by 300 m {early if early is not None else 'unavailable'} m", fill=(220, 226, 231), font=font)
    draw.text((18, 851), f"Full/crop max sampled fill {candidate['depressions']['full_map_sampled_fill_depths']['max_fill_depth_m']:.2f} / {candidate['depressions']['crop_sampled_fill_depths']['max_fill_depth_m']:.2f} m; crop lowering warning max {candidate['depressions']['crop_fill_lowering_warning']['maximum_crop_lowering_m']:.2f} m", fill=(220, 226, 231), font=font)
    draw.text((18, 869), f"Source map identity: {candidate['asset']['elevation_sha256'][:16]}…  |  domain SHA-256: {candidate['domain']['transformed_domain_sha256_f32le_c'][:16]}…", fill=(187, 198, 209), font=font)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(output_path)


def run_survey(inventory_path: Path = DEFAULT_INVENTORY, output_dir: Path = DEFAULT_OUTPUT) -> dict[str, Any]:
    started = time.perf_counter()
    if not inventory_path.is_absolute():
        inventory_path = ROOT / inventory_path
    if not output_dir.is_absolute():
        output_dir = ROOT / output_dir
    inventory = json.loads(inventory_path.read_text())
    evaluated: list[dict[str, Any]] = []
    map_summaries: list[dict[str, Any]] = []
    domain_sets: dict[str, list[dict[str, Any]]] = {}
    raw_by_variant: dict[str, np.ndarray] = {}
    for variant in LANDSCAPE_VARIANTS:
        raw, identity = load_pinned_map(variant, inventory_path)
        raw_by_variant[variant] = raw
        context_started = time.perf_counter()
        full_context = shared.full_map_context(raw)
        context_elapsed = time.perf_counter() - context_started
        local_median = ndimage.median_filter(raw, size=17, mode="nearest")
        all_domains = describe_domains(raw)
        selected_domains = select_diverse_domains(all_domains, raw.shape)
        if len(selected_domains) > MAX_DOMAIN_EVALUATIONS_PER_MAP:
            raise SurveyError("diversity selector exceeded the per-map evaluation bound")
        domain_sets[variant] = selected_domains
        map_candidates = []
        for domain in selected_domains:
            candidate = evaluate_domain(raw, full_context, domain, local_median)
            candidate["asset_variant"] = variant
            candidate["asset"] = identity
            candidate["candidate_id"] = f"{variant}-{candidate['candidate_id']}"
            map_candidates.append(candidate)
            evaluated.append(candidate)
        map_summaries.append(
            {
                "asset": identity,
                "full_map_d4_context": {
                    "method": "shared full-map cardinal priority flood; area and fill are locators/context only",
                    "direction_codes": sorted(int(code) for code in np.unique(full_context["directions"])),
                    "positive_fill_component_count_shared_face_4_connected": full_context["components"]["component_count"],
                    "positive_fill_cell_count": full_context["components"]["positive_fill_cell_count"],
                    "maximum_equilibrium_fill_depth_m": float(full_context["components"]["delta_m"].max(initial=0.0)),
                    "maximum_contributing_area_locator_km2": float(full_context["accumulation_km2"].max()),
                    "runtime_s": float(context_elapsed),
                },
                "domain_sampling": {
                    "method": "all fixed disjoint 256x256 native tiles described by raw relief, then relief-weighted farthest-point selection",
                    "available_domain_count": len(all_domains),
                    "domain_evaluations": len(map_candidates),
                    "maximum_evaluations": MAX_DOMAIN_EVALUATIONS_PER_MAP,
                    "evaluated_domains": [
                        {key: value for key, value in candidate["domain"].items() if key != "transformed_domain_sha256_f32le_c"}
                        for candidate in map_candidates
                    ],
                },
                "evaluated_candidate_ids": [item["candidate_id"] for item in map_candidates],
            }
        )
    shortlist = select_shortlist(evaluated)
    for rank, candidate in enumerate(shortlist, start=1):
        candidate["rank"] = rank
        candidate["visualization"] = f"site-{rank:02d}-{candidate['candidate_id']}.png"
        candidate["asset"] = next(item["asset"] for item in map_summaries if item["asset"]["variant"] == candidate["asset_variant"])
    for candidate in shortlist:
        output_path = output_dir / candidate["visualization"]
        write_candidate_visual(candidate, raw_by_variant[candidate["asset_variant"]], domain_sets[candidate["asset_variant"]], output_path)
        candidate["visualization"] = str(output_path.relative_to(ROOT))
    for candidate in evaluated:
        candidate["asset"] = next(item["asset"] for item in map_summaries if item["asset"]["variant"] == candidate["asset_variant"])
    result = {
        "schema": SCHEMA,
        "status": "exploratory-hillside-shortlist",
        "purpose": "rank broader native Terrain Diffusion domains for human review before any hillside flow physics study",
        "input_inventory": {
            "path": str(inventory_path.relative_to(ROOT)),
            "sha256": shared.sha256_file(inventory_path),
            "authoritative_fields": "each input map's source identity is taken from its existing map_summaries.asset record and checked against the immutable manifest and elevation bytes",
        },
        "protocol": {
            "map_variants": list(LANDSCAPE_VARIANTS),
            "native_spacing_m": SPACING_M,
            "domain_shape_cells": [DOMAIN_CELLS, DOMAIN_CELLS],
            "domain_size_m": [DOMAIN_CELLS * SPACING_M, DOMAIN_CELLS * SPACING_M],
            "max_domain_evaluations_per_map": MAX_DOMAIN_EVALUATIONS_PER_MAP,
            "shortlist_count": len(shortlist),
            "source_footprint": "five cells: center and north/south/east/west face-neighbors; source cells are measured individually and need not be flat",
            "route_method": "shared full-map D4 priority-flood direction locator; crop has a separately reported D4 locator",
            "domain_selection": "raw domain range, P95-P05 robust relief, and slope texture followed by deterministic spatial spread",
            "not_simulated": True,
        },
        "preferences_not_gates": {
            "domain_relief_m": 150.0,
            "robust_relief_p95_minus_p05_m": 100.0,
            "source_interior_margin_cells": MIN_SOURCE_MARGIN_CELLS,
            "raw_downhill_fall_m_within_distance_m": {"fall_m": 100.0, "distance_range_m": [1_000.0, 3_000.0]},
            "early_raw_fall_within_m": 300.0,
            "source_five_cell_spread_m": "reported descriptively; no <=2m flatness condition",
            "route_exit_or_edge_outlet": "not required; full-map and crop terminal/edge context are warnings only",
            "depression_fill": "reported separately for full map and crop; no maximum-fill gate",
        },
        "map_summaries": map_summaries,
        "evaluated_domains": evaluated,
        "shortlist": shortlist,
        "runtime_s": float(time.perf_counter() - started),
    }
    result["deterministic_evidence_sha256"] = shared.deterministic_result_fingerprint(result)
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "survey.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, default=DEFAULT_INVENTORY)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    result = run_survey(args.inventory, args.output_dir)
    print(json.dumps({
        "status": result["status"],
        "deterministic_evidence_sha256": result["deterministic_evidence_sha256"],
        "evaluated_domains_by_map": {item["asset"]["variant"]: item["domain_sampling"]["domain_evaluations"] for item in result["map_summaries"]},
        "shortlist": [
            {
                "rank": item["rank"],
                "candidate_id": item["candidate_id"],
                "variant": item["asset_variant"],
                "bbox": item["domain"]["bbox_full_map_cell_xzwh"],
                "source": item["source"]["center_full_map_cell_xz"],
                "domain_relief_m": item["domain"]["domain_relief_m"],
                "robust_relief_m": item["domain"]["robust_relief_p95_minus_p05_m"],
                "fall_1_3km_m": item["full_map_route"]["fall_and_facegrade_summary"]["best_fall_1_to_3km_m"],
                "early_fall_300m_m": item["full_map_route"]["fall_and_facegrade_summary"]["early_fall_within_300m_m"],
                "visualization": item["visualization"],
            }
            for item in result["shortlist"]
        ],
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
