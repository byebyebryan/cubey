#!/usr/bin/env python3
"""Search the frozen Terrain Diffusion cache for lower-relief incised reaches.

The scan is an elevation-only morphology heuristic. It never edits terrain,
generates seeds, or emits Fluid solver inputs.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import math
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import dijkstra
from skimage.morphology import skeletonize


ROOT = Path(__file__).resolve().parents[3]
PINNED_SCANNER_PATH = Path(__file__).resolve().with_name("terrain_site_scan_v1.py")
PINNED_SCAN_PATH = ROOT / "outputs/fluid/terrain-site-scan-v1-20260923/scan.json"
PINNED_SCAN_SHA256 = "ca29947bf1ec1d73c0f1af93a81fb25e46cd2ece7d7731c5a9f87470c5837f50"
PINNED_SCANNER_SHA256 = "6594f59b8c49c1a447f75c140816af94a8f984b86796ef728df0d0a0ff4dfdc1"
SOURCE_ROOT_RELATIVE = Path("cache/terrain/sources/v1")
OUTPUT_RELATIVE = Path("outputs/fluid/terrain-reach-scan-v1-20260923")
SCHEMA = "cubey.fluid25d.terrain_reach_scan.v1"

CONTEXT_WIDTH = 512
CONTEXT_HEIGHT = 256
NESTED_WIDTH = 256
NESTED_HEIGHT = 128
NATIVE_SPACING_M = 30.0
SCAN_STRIDE_X = 128
SCAN_STRIDE_Z = 64

# Valley proxy at native resolution: sub-30 m texture is lightly smoothed,
# then a 930 m grayscale closing estimates the nearby ridge envelope.
SMOOTH_SIGMA_CELLS = 1.0
VALLEY_CLOSING_SIZE_CELLS = 31
VALLEY_DEPTH_THRESHOLD_M = 12.0
MIN_COMPONENT_CELLS = 8
MAX_COMPONENTS_TO_TRACE = 6

# Eligibility separates modest broad relief from local, repeated incision.
MIN_CONTEXT_RELIEF_M = 80.0
MAX_PRIMARY_CONTEXT_RELIEF_M = 500.0
MAX_CONTRAST_CONTEXT_RELIEF_M = 700.0
MIN_NESTED_RELIEF_M = 50.0
MAX_NESTED_RELIEF_M = 350.0
MAX_NESTED_TO_CONTEXT_RELIEF_RATIO = 0.65
MIN_CONTEXT_TRUNK_LENGTH_M = 5_000.0
MIN_NESTED_TRUNK_LENGTH_M = 3_000.0
CROSS_SECTION_SPACING_M = 300.0
MIN_CROSS_SECTIONS = 8
FLOOR_TOLERANCE_M = 3.0
MIN_FLOOR_WIDTH_M = 90.0  # three adjacent 30 m samples
BANK_SAMPLE_NEAR_M = 180.0
BANK_SAMPLE_FAR_M = 390.0
MIN_BANK_RELIEF_M = 8.0
MIN_PAIRED_BANK_RATIO = 0.70
MIN_MULTI_CELL_FLOOR_RATIO = 0.60
MIN_DOWNHILL_STEP_FRACTION = 0.55
MIN_END_TO_END_SLOPE = 0.0002
MAX_PRIMARY_END_TO_END_SLOPE = 0.01
MAX_CONTRAST_END_TO_END_SLOPE = 0.04
LONGITUDINAL_SMOOTHING_LENGTH_M = 300.0
LONGITUDINAL_SMOOTHING_WINDOW_SAMPLES = 11
MAX_PRIMARY_PATH_ADVERSE_RISE_M = 3.0
MAX_CONTRAST_PATH_ADVERSE_RISE_M = 12.0
MIN_ENDPOINT_SEPARATION_FRACTION_OF_DIAGONAL = 0.25
MIN_PATH_SPAN_FRACTION_OF_LONGER_CROP_AXIS = 0.25
MIN_ROUTE_DIRECTNESS_RATIO = 0.50
MIN_PREVIOUS_CROP_EDGE_MARGIN_CELLS = 2
MAX_CONTACT_SHEET_CANDIDATES = 6
MAX_SHORTLIST_CANDIDATES_PER_PAYLOAD = 2
MAX_SHORTLIST_REACH_OVERLAP_RATIO = 0.20

EXPECTED_UNIQUE_PAYLOADS = frozenset(
    {
        "00fc8836855c52caba7d9b114b00d8ba426d0b9f716825f7e19c42f2e273c28d",
        "27b49f12f29ae24629a8ec03d12b53c6986404c0354069529be75a5ea02c45df",
        "2a919b516d8ae4fb8c193cdd8db1a8ba055ba702e5cbd1ff50ad2b7fc6ab3c48",
        "4c6cda32de46801ca52b5edc37a925a947438de59537e55ec7b08c8883f68b51",
        "5fbf519f9af538d0f1b684e01a4aa1c830ac1896ced2f082ddecb09a3bc9a63a",
        "7e0d5cfc47abdfc8d1338217efa7c70a082fe46bc742028c5e90e871b73f441c",
        "88cc6c1fdacbd9759f90e6781ddf4ff570e4516fb0652438dc28f7ad23086d8b",
        "a978ecd435d2a161598d78ecf71221cba53b371e98c3525d18ebeab6665aa737",
        "c35a8d9757fbd856a44bc48dceb49cd939f16b9819727ea07ef97c5d37a7f7d5",
        "cd05ae46686a6e0b62b018b9b9f8682a0fd37de84005f2ed42a5ab4bc4350cdc",
        "f072dd7bc0991d0de1b4ef2c6839373271e0b40e65a23f80aec9fdfd08476cd0",
        "fd52d7c3b25f139ac709b8ced673ccc77d749cc33e4c52e66745494a86dcf7a2",
    }
)


class ReachScanError(RuntimeError):
    pass


def _load_pinned_scanner() -> Any:
    spec = importlib.util.spec_from_file_location("terrain_site_scan_v1_pinned", PINNED_SCANNER_PATH)
    if spec is None or spec.loader is None:
        raise ReachScanError(f"cannot load pinned source scanner: {PINNED_SCANNER_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


PINNED = _load_pinned_scanner()


@dataclass
class WindowResult:
    asset: Any
    x: int
    z: int
    context: dict[str, Any]
    nested: dict[str, Any] | None
    selected_reach: str
    status: str
    score: float
    failures: list[str]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_pinned_inventory(assets: list[Any]) -> None:
    actual = {asset.elevation_sha256 for asset in assets}
    if len(assets) != 12 or actual != EXPECTED_UNIQUE_PAYLOADS:
        raise ReachScanError(
            "cached inventory differs from the frozen 12-payload Terrain Diffusion scan; "
            f"unique={len(assets)}, missing={sorted(EXPECTED_UNIQUE_PAYLOADS - actual)}, "
            f"unexpected={sorted(actual - EXPECTED_UNIQUE_PAYLOADS)}"
        )


def load_pinned_assets(source_root: Path, pinned_scan_path: Path = PINNED_SCAN_PATH) -> tuple[list[Any], dict[str, Any]]:
    if not pinned_scan_path.is_file() or sha256_file(pinned_scan_path) != PINNED_SCAN_SHA256:
        raise ReachScanError("the prior terrain-site scan identity is missing or differs from its frozen pin")
    if not PINNED_SCANNER_PATH.is_file() or sha256_file(PINNED_SCANNER_PATH) != PINNED_SCANNER_SHA256:
        raise ReachScanError("the prior terrain-site scanner source differs from its frozen pin")
    prior_scan = json.loads(pinned_scan_path.read_text())
    if prior_scan.get("schema") != PINNED.SCHEMA or prior_scan.get("contract", {}).get(
        "unique_elevation_payloads"
    ) != 12:
        raise ReachScanError("the frozen terrain-site inventory has an unexpected schema or payload count")
    expected_from_prior = {item["elevation_sha256"] for item in prior_scan.get("inventory", [])}
    if expected_from_prior != EXPECTED_UNIQUE_PAYLOADS:
        raise ReachScanError("the frozen terrain-site inventory payload hashes no longer match the pinned set")
    records = PINNED.discover_manifests(source_root)
    assets = PINNED.deduplicate_assets(records)
    validate_pinned_inventory(assets)
    return assets, {
        "manifest_count": len(records),
        "unique_payload_count": len(assets),
        "deduplicated_alias_count": len(records) - len(assets),
        "pinned_scan_path": str(pinned_scan_path.resolve().relative_to(ROOT)),
        "pinned_scan_sha256": PINNED_SCAN_SHA256,
        "pinned_scanner_sha256": PINNED_SCANNER_SHA256,
    }


def native_origins(width: int, height: int) -> list[tuple[int, int]]:
    if width < CONTEXT_WIDTH or height < CONTEXT_HEIGHT:
        return []
    return [
        (x, z)
        for z in range(0, height - CONTEXT_HEIGHT + 1, SCAN_STRIDE_Z)
        for x in range(0, width - CONTEXT_WIDTH + 1, SCAN_STRIDE_X)
    ]


def transform_heightfield(raw: np.ndarray, manifest: dict[str, Any]) -> np.ndarray:
    """Match TerrainRasterHeightSource's float32 affine height transform."""
    offset = np.float32(manifest["height"]["offset_m"])
    scale = np.float32(manifest["height"]["scale"])
    with np.errstate(over="ignore", invalid="ignore"):
        result = (np.asarray(raw, dtype=np.float32) + offset) * scale
    if not np.isfinite(result).all():
        raise ReachScanError("transformed terrain payload contains non-finite elevations")
    return result


def compute_valley_depth(elevation_m: np.ndarray) -> np.ndarray:
    elevation = np.asarray(elevation_m, dtype=np.float32)
    if elevation.ndim != 2 or not np.isfinite(elevation).all():
        raise ValueError("terrain elevation must be a finite 2D array")
    smooth = ndimage.gaussian_filter(
        elevation, sigma=SMOOTH_SIGMA_CELLS, mode="nearest"
    )
    ridge_envelope = ndimage.grey_closing(
        smooth,
        size=(VALLEY_CLOSING_SIZE_CELLS, VALLEY_CLOSING_SIZE_CELLS),
        mode="nearest",
    )
    return np.maximum(ridge_envelope - smooth, 0.0).astype(np.float32)


def _longest_path_in_skeleton(skeleton: np.ndarray) -> tuple[np.ndarray, float, int]:
    coords = np.argwhere(skeleton)
    if coords.shape[0] < 2:
        return np.empty((0, 2), dtype=np.int32), 0.0, 0
    index = np.full(skeleton.shape, -1, dtype=np.int32)
    index[coords[:, 0], coords[:, 1]] = np.arange(coords.shape[0], dtype=np.int32)
    row_parts: list[np.ndarray] = []
    col_parts: list[np.ndarray] = []
    weight_parts: list[np.ndarray] = []
    for dz, dx in ((0, 1), (1, -1), (1, 0), (1, 1)):
        z = coords[:, 0]
        x = coords[:, 1]
        target_z = z + dz
        target_x = x + dx
        inside = (
            (target_z >= 0)
            & (target_z < skeleton.shape[0])
            & (target_x >= 0)
            & (target_x < skeleton.shape[1])
        )
        source_nodes = np.flatnonzero(inside)
        destination_nodes = index[target_z[inside], target_x[inside]]
        connected = destination_nodes >= 0
        source_nodes = source_nodes[connected]
        destination_nodes = destination_nodes[connected]
        if source_nodes.size == 0:
            continue
        weight = np.full(
            source_nodes.size,
            NATIVE_SPACING_M * (math.sqrt(2.0) if dz and dx else 1.0),
            dtype=np.float64,
        )
        row_parts.extend((source_nodes, destination_nodes))
        col_parts.extend((destination_nodes, source_nodes))
        weight_parts.extend((weight, weight))
    if not row_parts:
        return np.empty((0, 2), dtype=np.int32), 0.0, 0
    graph = csr_matrix(
        (np.concatenate(weight_parts), (np.concatenate(row_parts), np.concatenate(col_parts))),
        shape=(coords.shape[0], coords.shape[0]),
    )
    degree = np.diff(graph.indptr)
    endpoints = np.flatnonzero(degree == 1)
    if endpoints.size < 2:
        endpoints = np.flatnonzero(degree > 0)
    if endpoints.size < 2:
        return np.empty((0, 2), dtype=np.int32), 0.0, 0

    first_distances = dijkstra(graph, directed=False, indices=int(endpoints[0]))
    far_a = int(endpoints[np.argmax(first_distances[endpoints])])
    distances, predecessors = dijkstra(
        graph, directed=False, indices=far_a, return_predecessors=True
    )
    far_b = int(endpoints[np.argmax(distances[endpoints])])
    if not math.isfinite(float(distances[far_b])):
        return np.empty((0, 2), dtype=np.int32), 0.0, 0
    path_nodes = [far_b]
    node = far_b
    while node != far_a:
        predecessor = int(predecessors[node])
        if predecessor < 0 or predecessor == node:
            return np.empty((0, 2), dtype=np.int32), 0.0, 0
        path_nodes.append(predecessor)
        node = predecessor
        if len(path_nodes) > coords.shape[0]:
            return np.empty((0, 2), dtype=np.int32), 0.0, 0
    path = coords[np.asarray(path_nodes[::-1], dtype=np.int32)]
    neighbor_count = ndimage.convolve(
        skeleton.astype(np.uint8), PINNED.NEIGHBOR_KERNEL, mode="constant", cval=0
    )
    junction_pixels = skeleton & (neighbor_count >= 3)
    _junction_labels, junction_count = ndimage.label(
        junction_pixels, structure=PINNED.EIGHT_CONNECTED
    )
    return path.astype(np.int32), float(distances[far_b]), int(junction_count)


def longest_trunk_path(valley_depth_m: np.ndarray) -> tuple[np.ndarray, float, int, int, float]:
    depth = np.asarray(valley_depth_m, dtype=np.float32)
    mask = depth >= VALLEY_DEPTH_THRESHOLD_M
    labels, label_count = ndimage.label(mask, structure=PINNED.EIGHT_CONNECTED)
    if label_count == 0:
        return np.empty((0, 2), dtype=np.int32), 0.0, 0, 0, 0.0
    sizes = np.bincount(labels.ravel())
    sizes[0] = 0
    components = np.argsort(sizes)[::-1]
    components = [int(label) for label in components if sizes[label] >= MIN_COMPONENT_CELLS]
    best_path = np.empty((0, 2), dtype=np.int32)
    best_length = 0.0
    best_junctions = 0
    best_component = 0
    tested_components = 0
    for label in components[:MAX_COMPONENTS_TO_TRACE]:
        component_mask = labels == label
        skeleton = skeletonize(component_mask)
        path, length_m, junction_count = _longest_path_in_skeleton(skeleton)
        tested_components += 1
        if length_m > best_length:
            best_path = path
            best_length = length_m
            best_junctions = junction_count
            best_component = int(sizes[label])
    mask_fraction = float(np.count_nonzero(mask) / mask.size) if mask.size else 0.0
    return best_path, best_length, best_junctions, tested_components, mask_fraction


def _path_distances(path: np.ndarray) -> np.ndarray:
    if len(path) < 2:
        return np.zeros((len(path),), dtype=np.float64)
    delta = np.diff(path.astype(np.float64), axis=0)
    step_lengths = NATIVE_SPACING_M * np.sqrt(np.sum(delta * delta, axis=1))
    return np.concatenate(([0.0], np.cumsum(step_lengths)))


def _longitudinal_path_profile(
    elevation_m: np.ndarray, path: np.ndarray, include_profile: bool = False
) -> dict[str, Any]:
    elevation = np.asarray(elevation_m, dtype=np.float32)
    path = np.asarray(path, dtype=np.int32)
    if len(path) < 2:
        return {
            "path_endpoint_a_elevation_m": 0.0,
            "path_endpoint_b_elevation_m": 0.0,
            "path_end_to_end_fall_m": 0.0,
            "path_end_to_end_slope": 0.0,
            "path_max_downstream_adverse_rise_m": 0.0,
            "path_terminal_rise_above_minimum_m": 0.0,
            "path_downhill_step_fraction": 0.0,
            "path_orientation_high_to_low_is_forward": True,
        }
    node_distances = _path_distances(path)
    path_length = float(node_distances[-1])
    node_elevations = elevation[path[:, 0], path[:, 1]].astype(np.float64)
    sample_distances = np.arange(0.0, path_length, NATIVE_SPACING_M, dtype=np.float64)
    if sample_distances.size == 0 or sample_distances[-1] < path_length:
        sample_distances = np.append(sample_distances, path_length)
    sample_z = np.interp(sample_distances, node_distances, path[:, 0].astype(np.float64))
    sample_x = np.interp(sample_distances, node_distances, path[:, 1].astype(np.float64))
    sampled_elevation = ndimage.map_coordinates(
        elevation,
        np.vstack((sample_z, sample_x)),
        order=1,
        mode="nearest",
        prefilter=False,
    ).astype(np.float64)
    window = min(LONGITUDINAL_SMOOTHING_WINDOW_SAMPLES, sampled_elevation.size)
    if window > 1 and window % 2 == 0:
        window -= 1
    smoothed_elevation = (
        ndimage.uniform_filter1d(sampled_elevation, size=window, mode="nearest")
        if window > 1
        else sampled_elevation.copy()
    )
    high_to_low_forward = bool(smoothed_elevation[0] >= smoothed_elevation[-1])
    if not high_to_low_forward:
        sample_distances = path_length - sample_distances[::-1]
        sample_x = sample_x[::-1]
        sample_z = sample_z[::-1]
        sampled_elevation = sampled_elevation[::-1]
        smoothed_elevation = smoothed_elevation[::-1]
    smoothed_differences = np.diff(smoothed_elevation)
    running_minimum = np.minimum.accumulate(smoothed_elevation)
    adverse_rise = np.maximum(smoothed_elevation - running_minimum, 0.0)
    fall = float(smoothed_elevation[0] - smoothed_elevation[-1])
    result = {
        "path_endpoint_a_elevation_m": float(node_elevations[0]),
        "path_endpoint_b_elevation_m": float(node_elevations[-1]),
        "path_end_to_end_fall_m": fall,
        "path_end_to_end_slope": fall / path_length if path_length > 0.0 else 0.0,
        "path_max_downstream_adverse_rise_m": float(adverse_rise.max()),
        "path_terminal_rise_above_minimum_m": float(smoothed_elevation[-1] - smoothed_elevation.min()),
        "path_downhill_step_fraction": float(np.mean(smoothed_differences <= 0.0))
        if smoothed_differences.size
        else 0.0,
        "path_orientation_high_to_low_is_forward": high_to_low_forward,
    }
    if include_profile:
        result["_longitudinal_profile"] = [
            {
                "distance_from_high_endpoint_m": float(distance),
                "x_native_local": float(x),
                "z_native_local": float(z),
                "elevation_m": float(raw),
                "elevation_smoothed_m": float(smoothed),
            }
            for distance, x, z, raw, smoothed in zip(
                sample_distances,
                sample_x,
                sample_z,
                sampled_elevation,
                smoothed_elevation,
                strict=True,
            )
        ]
    return result


def _longest_true_run(mask: np.ndarray) -> tuple[int, int] | None:
    padded = np.concatenate(([False], mask, [False])).astype(np.int8)
    edges = np.diff(padded)
    starts = np.flatnonzero(edges == 1)
    ends = np.flatnonzero(edges == -1)
    if starts.size == 0:
        return None
    lengths = ends - starts
    best = int(np.argmax(lengths))
    return int(starts[best]), int(ends[best])


def measure_cross_sections(elevation_m: np.ndarray, path: np.ndarray) -> dict[str, Any]:
    elevation = np.asarray(elevation_m, dtype=np.float32)
    if len(path) < 2:
        return {
            "stations": [],
            "tested_count": 0,
            "paired_bank_count": 0,
            "paired_bank_ratio": 0.0,
            "multi_cell_floor_count": 0,
            "multi_cell_floor_ratio": 0.0,
            "median_left_bank_relief_m": 0.0,
            "median_right_bank_relief_m": 0.0,
            "median_floor_width_m": 0.0,
            "minimum_floor_width_m": 0.0,
            "station_end_to_end_fall_m": 0.0,
            "station_end_to_end_slope": 0.0,
            "station_downhill_step_fraction": 0.0,
            "endpoint_separation_m": 0.0,
            "endpoint_separation_fraction_of_diagonal": 0.0,
            "path_span_x_m": 0.0,
            "path_span_z_m": 0.0,
            "path_span_fraction_of_longer_crop_axis": 0.0,
            "route_directness_ratio": 0.0,
        }
    path_distance = _path_distances(path)
    path_length = float(path_distance[-1])
    endpoint_delta = (path[-1] - path[0]).astype(np.float64)
    endpoint_separation = float(np.linalg.norm(endpoint_delta) * NATIVE_SPACING_M)
    crop_diagonal = math.hypot(
        (elevation.shape[1] - 1) * NATIVE_SPACING_M,
        (elevation.shape[0] - 1) * NATIVE_SPACING_M,
    )
    span_x = float((path[:, 1].max() - path[:, 1].min()) * NATIVE_SPACING_M)
    span_z = float((path[:, 0].max() - path[:, 0].min()) * NATIVE_SPACING_M)
    crop_longer_axis = max(
        (elevation.shape[1] - 1) * NATIVE_SPACING_M,
        (elevation.shape[0] - 1) * NATIVE_SPACING_M,
    )
    # Give each measurement room on both ends. A short pit or spur therefore
    # cannot inflate the repeated-bank count with one local cross-section.
    station_distances = np.arange(
        CROSS_SECTION_SPACING_M,
        max(CROSS_SECTION_SPACING_M, path_length - CROSS_SECTION_SPACING_M) + 0.01,
        CROSS_SECTION_SPACING_M,
    )
    station_indices = np.searchsorted(path_distance, station_distances).clip(1, len(path) - 2)
    stations: list[dict[str, Any]] = []
    offsets = np.arange(-15, 16, dtype=np.int32)
    bank_half_width = int(BANK_SAMPLE_FAR_M / NATIVE_SPACING_M)
    bank_near = int(BANK_SAMPLE_NEAR_M / NATIVE_SPACING_M)
    floor_radius = 5
    for station_distance, index in zip(station_distances, station_indices, strict=True):
        before = path[max(0, int(index) - 3)].astype(np.float64)
        after = path[min(len(path) - 1, int(index) + 3)].astype(np.float64)
        tangent = after - before
        norm = float(np.linalg.norm(tangent))
        if norm < 1.0e-6:
            continue
        # Paths are stored in row/column (z/x) order, so rotate the tangent
        # (-90 degrees) in that same coordinate basis.
        normal = np.array([tangent[1], -tangent[0]], dtype=np.float64) / norm
        z_center, x_center = path[int(index)]
        sample_z = np.rint(z_center + offsets * normal[0]).astype(np.int32)
        sample_x = np.rint(x_center + offsets * normal[1]).astype(np.int32)
        if (
            sample_z.min() < 0
            or sample_x.min() < 0
            or sample_z.max() >= elevation.shape[0]
            or sample_x.max() >= elevation.shape[1]
        ):
            continue
        profile = elevation[sample_z, sample_x].astype(np.float64)
        center_band = np.flatnonzero(np.abs(offsets) <= floor_radius)
        local_floor_i = int(center_band[np.argmin(profile[center_band])])
        local_floor_m = float(profile[local_floor_i])
        low_cells = profile <= local_floor_m + FLOOR_TOLERANCE_M
        run = _longest_true_run(low_cells)
        floor_start, floor_end = run if run is not None and run[0] <= local_floor_i < run[1] else (
            local_floor_i,
            local_floor_i + 1,
        )
        floor_width_m = float((floor_end - floor_start) * NATIVE_SPACING_M)
        floor_m = float(np.median(profile[floor_start:floor_end]))
        negative_bank = np.flatnonzero((offsets <= -bank_near) & (offsets >= -bank_half_width))
        positive_bank = np.flatnonzero((offsets >= bank_near) & (offsets <= bank_half_width))
        left_relief = (
            float(np.percentile(profile[negative_bank], 75.0) - floor_m)
            if negative_bank.size
            else 0.0
        )
        right_relief = (
            float(np.percentile(profile[positive_bank], 75.0) - floor_m)
            if positive_bank.size
            else 0.0
        )
        paired = left_relief >= MIN_BANK_RELIEF_M and right_relief >= MIN_BANK_RELIEF_M
        floor_pass = floor_width_m >= MIN_FLOOR_WIDTH_M
        stations.append(
            {
                "path_distance_m": float(station_distance),
                "x_native_local": int(x_center),
                "z_native_local": int(z_center),
                "normal_x": float(normal[1]),
                "normal_z": float(normal[0]),
                "floor_elevation_m": floor_m,
                "floor_width_m": floor_width_m,
                "left_bank_relief_m": left_relief,
                "right_bank_relief_m": right_relief,
                "paired_banks": paired,
                "multi_cell_floor": floor_pass,
            }
        )
    tested = len(stations)
    paired_count = sum(bool(row["paired_banks"]) for row in stations)
    floor_count = sum(bool(row["multi_cell_floor"]) for row in stations)
    floor_widths = [float(row["floor_width_m"]) for row in stations]
    left_values = [float(row["left_bank_relief_m"]) for row in stations]
    right_values = [float(row["right_bank_relief_m"]) for row in stations]
    floor_values = [float(row["floor_elevation_m"]) for row in stations]
    if floor_values and floor_values[0] < floor_values[-1]:
        floor_values = list(reversed(floor_values))
    fall = floor_values[0] - floor_values[-1] if len(floor_values) > 1 else 0.0
    slope = fall / path_length if path_length > 0.0 else 0.0
    downhill = (
        float(np.mean(np.diff(floor_values) <= 0.0)) if len(floor_values) > 1 else 0.0
    )
    return {
        "stations": stations,
        "tested_count": tested,
        "paired_bank_count": paired_count,
        "paired_bank_ratio": paired_count / tested if tested else 0.0,
        "multi_cell_floor_count": floor_count,
        "multi_cell_floor_ratio": floor_count / tested if tested else 0.0,
        "median_left_bank_relief_m": float(np.median(left_values)) if left_values else 0.0,
        "median_right_bank_relief_m": float(np.median(right_values)) if right_values else 0.0,
        "median_floor_width_m": float(np.median(floor_widths)) if floor_widths else 0.0,
        "minimum_floor_width_m": float(min(floor_widths)) if floor_widths else 0.0,
        "station_end_to_end_fall_m": float(fall),
        "station_end_to_end_slope": float(slope),
        "station_downhill_step_fraction": downhill,
        "endpoint_separation_m": endpoint_separation,
        "endpoint_separation_fraction_of_diagonal": (
            endpoint_separation / crop_diagonal if crop_diagonal > 0.0 else 0.0
        ),
        "path_span_x_m": span_x,
        "path_span_z_m": span_z,
        "path_span_fraction_of_longer_crop_axis": (
            max(span_x, span_z) / crop_longer_axis if crop_longer_axis > 0.0 else 0.0
        ),
        "route_directness_ratio": endpoint_separation / path_length if path_length > 0.0 else 0.0,
    }


def regional_relief(elevation_m: np.ndarray) -> dict[str, float]:
    values = np.asarray(elevation_m, dtype=np.float32)
    p05, p50, p95 = np.percentile(values, (5.0, 50.0, 95.0))
    return {
        "p05_m": float(p05),
        "p50_m": float(p50),
        "p95_m": float(p95),
        "p05_p95_relief_m": float(p95 - p05),
        "minimum_m": float(values.min()),
        "maximum_m": float(values.max()),
    }


def analyze_elevation_window(
    elevation_m: np.ndarray,
    valley_depth_m: np.ndarray | None = None,
    minimum_path_length_m: float = MIN_CONTEXT_TRUNK_LENGTH_M,
    relief_metrics: dict[str, float] | None = None,
) -> dict[str, Any]:
    elevation = np.asarray(elevation_m, dtype=np.float32)
    if elevation.ndim != 2 or not np.isfinite(elevation).all():
        raise ValueError("candidate elevation window must be a finite 2D array")
    depth = compute_valley_depth(elevation) if valley_depth_m is None else np.asarray(
        valley_depth_m, dtype=np.float32
    )
    if depth.shape != elevation.shape:
        raise ValueError("valley depth and elevation window dimensions differ")
    relief = regional_relief(elevation) if relief_metrics is None else relief_metrics
    path, path_length, junction_count, component_count, mask_fraction = longest_trunk_path(depth)
    cross = measure_cross_sections(elevation, path)
    result: dict[str, Any] = {
        **relief,
        "longest_trunk_path_m": path_length,
        "valley_mask_fraction": mask_fraction,
        "junction_cluster_count_diagnostic_only": junction_count,
        "components_traced": component_count,
        **{key: value for key, value in cross.items() if key != "stations"},
        **_longitudinal_path_profile(elevation, path),
        "_path": path,
        "_cross_sections": cross["stations"],
    }
    result["failure_reasons"] = reach_failure_reasons(
        result, minimum_path_length_m=minimum_path_length_m
    )
    result["eligible"] = not result["failure_reasons"]
    result["score"] = reach_score(result)
    return result


def reach_failure_reasons(
    metrics: dict[str, Any],
    minimum_path_length_m: float,
    minimum_relief_m: float = MIN_CONTEXT_RELIEF_M,
) -> list[str]:
    failures: list[str] = []
    relief = float(metrics["p05_p95_relief_m"])
    if relief < minimum_relief_m:
        failures.append("regional_relief_below_flatness_floor")
    if relief > MAX_CONTRAST_CONTEXT_RELIEF_M:
        failures.append("regional_relief_above_700m_contrast_ceiling")
    if float(metrics["longest_trunk_path_m"]) < minimum_path_length_m:
        failures.append("connected_trunk_path_too_short")
    if float(metrics["endpoint_separation_fraction_of_diagonal"]) < MIN_ENDPOINT_SEPARATION_FRACTION_OF_DIAGONAL:
        failures.append("trunk_endpoints_too_close_for_crop_dimensions")
    if float(metrics["path_span_fraction_of_longer_crop_axis"]) < MIN_PATH_SPAN_FRACTION_OF_LONGER_CROP_AXIS:
        failures.append("trunk_spatial_span_too_small_for_crop_dimensions")
    if float(metrics["route_directness_ratio"]) < MIN_ROUTE_DIRECTNESS_RATIO:
        failures.append("trunk_route_directness_below_source_outlet_minimum")
    if int(metrics["tested_count"]) < MIN_CROSS_SECTIONS:
        failures.append("too_few_repeated_cross_sections")
    if float(metrics["median_left_bank_relief_m"]) < MIN_BANK_RELIEF_M:
        failures.append("left_bank_incision_below_minimum")
    if float(metrics["median_right_bank_relief_m"]) < MIN_BANK_RELIEF_M:
        failures.append("right_bank_incision_below_minimum")
    if float(metrics["paired_bank_ratio"]) < MIN_PAIRED_BANK_RATIO:
        failures.append("bilateral_banks_not_repeated_at_required_ratio")
    if float(metrics["multi_cell_floor_ratio"]) < MIN_MULTI_CELL_FLOOR_RATIO:
        failures.append("multi_cell_floor_not_repeated_at_required_ratio")
    if float(metrics["path_end_to_end_fall_m"]) <= 0.0:
        failures.append("no_end_to_end_downhill_fall")
    slope = float(metrics["path_end_to_end_slope"])
    if slope < MIN_END_TO_END_SLOPE:
        failures.append("end_to_end_fall_too_small_for_path_length")
    if slope > MAX_CONTRAST_END_TO_END_SLOPE:
        failures.append("end_to_end_fall_too_steep_for_reach")
    elif slope > MAX_PRIMARY_END_TO_END_SLOPE:
        failures.append("path_grade_above_primary_gentle_reach_band_contrast_only")
    adverse_rise = float(metrics["path_max_downstream_adverse_rise_m"])
    if adverse_rise > MAX_CONTRAST_PATH_ADVERSE_RISE_M:
        failures.append("path_adverse_rise_exceeds_12m_contrast_ceiling")
    elif adverse_rise > MAX_PRIMARY_PATH_ADVERSE_RISE_M:
        failures.append("path_adverse_rise_above_gentle_through_flow_band_contrast_only")
    if float(metrics["path_downhill_step_fraction"]) < MIN_DOWNHILL_STEP_FRACTION:
        failures.append("downhill_direction_not_consistent_enough")
    return failures


def reach_score(metrics: dict[str, Any]) -> float:
    """Rank moderate-relief, incised, long reaches without junction bonuses."""
    if float(metrics.get("longest_trunk_path_m", 0.0)) <= 0.0:
        return 0.0
    relief = float(metrics["p05_p95_relief_m"])
    # A moderate target rewards useful regional form, not flatness or cliffs.
    relief_factor = max(0.0, 1.0 - abs(relief - 260.0) / 360.0)
    length_factor = min(float(metrics["longest_trunk_path_m"]) / 7_500.0, 1.0)
    incision_factor = min(
        min(
            float(metrics["median_left_bank_relief_m"]),
            float(metrics["median_right_bank_relief_m"]),
        )
        / 35.0,
        1.0,
    )
    score = (
        relief_factor
        * length_factor
        * incision_factor
        * min(float(metrics["paired_bank_ratio"]), 1.0)
        * min(float(metrics["multi_cell_floor_ratio"]), 1.0)
    )
    return float(score)


def _best_contiguous_subpath(path: np.ndarray, x0: int, z0: int) -> np.ndarray:
    inside = (
        (path[:, 1] >= x0)
        & (path[:, 1] < x0 + NESTED_WIDTH)
        & (path[:, 0] >= z0)
        & (path[:, 0] < z0 + NESTED_HEIGHT)
    )
    run = _longest_true_run(inside)
    if run is None:
        return np.empty((0, 2), dtype=np.int32)
    return path[run[0] : run[1]].copy()


def find_best_nested_reach(
    context_elevation: np.ndarray,
    context_depth: np.ndarray,
    context_metrics: dict[str, Any],
) -> dict[str, Any] | None:
    path = context_metrics["_path"]
    if len(path) < 2:
        return None
    centers = np.linspace(0, len(path) - 1, num=9, dtype=np.int32)
    origins: set[tuple[int, int]] = set()
    for index in centers:
        z_center, x_center = path[int(index)]
        x0 = int(np.clip(int(x_center) - NESTED_WIDTH // 2, 0, CONTEXT_WIDTH - NESTED_WIDTH))
        z0 = int(np.clip(int(z_center) - NESTED_HEIGHT // 2, 0, CONTEXT_HEIGHT - NESTED_HEIGHT))
        origins.add((x0, z0))
    best: dict[str, Any] | None = None
    for x0, z0 in sorted(origins):
        path_part = _best_contiguous_subpath(path, x0, z0)
        if len(path_part) < 2:
            continue
        elevation = context_elevation[z0 : z0 + NESTED_HEIGHT, x0 : x0 + NESTED_WIDTH]
        depth = context_depth[z0 : z0 + NESTED_HEIGHT, x0 : x0 + NESTED_WIDTH]
        path_local = path_part - np.array([z0, x0], dtype=np.int32)
        relief = regional_relief(elevation)
        path_length = float(_path_distances(path_local)[-1])
        cross = measure_cross_sections(elevation, path_local)
        nested: dict[str, Any] = {
            **relief,
            "x_in_context": x0,
            "z_in_context": z0,
            "width": NESTED_WIDTH,
            "height": NESTED_HEIGHT,
            "sample_spacing_m": NATIVE_SPACING_M,
            "longest_trunk_path_m": path_length,
            "valley_mask_fraction": float(
                np.count_nonzero(depth >= VALLEY_DEPTH_THRESHOLD_M) / depth.size
            ),
            "junction_cluster_count_diagnostic_only": context_metrics[
                "junction_cluster_count_diagnostic_only"
            ],
            "components_traced": context_metrics["components_traced"],
            **{key: value for key, value in cross.items() if key != "stations"},
            **_longitudinal_path_profile(elevation, path_local),
            "_path": path_local,
            "_cross_sections": cross["stations"],
        }
        nested["failure_reasons"] = reach_failure_reasons(
            nested,
            minimum_path_length_m=MIN_NESTED_TRUNK_LENGTH_M,
            minimum_relief_m=MIN_NESTED_RELIEF_M,
        )
        if relief["p05_p95_relief_m"] > MAX_NESTED_RELIEF_M:
            nested["failure_reasons"].append("nested_reach_relief_above_350m")
        if relief["p05_p95_relief_m"] < MIN_NESTED_RELIEF_M:
            nested["failure_reasons"].append("nested_reach_too_flat")
        nested["eligible"] = not nested["failure_reasons"]
        nested["score"] = reach_score(nested)
        if best is None or _nested_preference(nested) > _nested_preference(best):
            best = nested
    if best is not None:
        best["x_in_context"] = int(best["x_in_context"])
        best["z_in_context"] = int(best["z_in_context"])
    return best


def _nested_preference(metrics: dict[str, Any]) -> tuple[bool, float]:
    return bool(metrics["eligible"]), float(metrics["score"])


def _near_miss_results(ranked: list[WindowResult], eligible: list[WindowResult]) -> list[WindowResult]:
    eligible_ids = {id(item) for item in eligible}
    return [item for item in ranked if id(item) not in eligible_ids]


def _selected_reach_box(item: WindowResult) -> tuple[int, int, int, int]:
    if item.selected_reach == "nested" and item.nested is not None:
        return (
            item.x + int(item.nested["x_in_context"]),
            item.z + int(item.nested["z_in_context"]),
            NESTED_WIDTH,
            NESTED_HEIGHT,
        )
    return item.x, item.z, CONTEXT_WIDTH, CONTEXT_HEIGHT


def _reach_box_overlap_ratio(
    first: tuple[int, int, int, int], second: tuple[int, int, int, int]
) -> float:
    first_x, first_z, first_width, first_height = first
    second_x, second_z, second_width, second_height = second
    intersection_width = max(
        0, min(first_x + first_width, second_x + second_width) - max(first_x, second_x)
    )
    intersection_height = max(
        0, min(first_z + first_height, second_z + second_height) - max(first_z, second_z)
    )
    intersection = intersection_width * intersection_height
    smaller_area = min(first_width * first_height, second_width * second_height)
    return intersection / smaller_area if smaller_area else 0.0


def _diverse_shortlist(
    ranked: list[WindowResult], max_candidates: int = MAX_CONTACT_SHEET_CANDIDATES
) -> list[WindowResult]:
    """Prefer one reach per payload, then allow non-overlapping second sites."""
    selected: list[WindowResult] = []
    counts: dict[str, int] = {}
    boxes: dict[str, list[tuple[int, int, int, int]]] = {}

    def append_if_distinct(item: WindowResult, allow_second_per_payload: bool) -> bool:
        digest = item.asset.elevation_sha256
        payload_count = counts.get(digest, 0)
        cap = MAX_SHORTLIST_CANDIDATES_PER_PAYLOAD if allow_second_per_payload else 1
        if payload_count >= cap:
            return False
        box = _selected_reach_box(item)
        if any(
            _reach_box_overlap_ratio(box, existing) > MAX_SHORTLIST_REACH_OVERLAP_RATIO
            for existing in boxes.get(digest, [])
        ):
            return False
        selected.append(item)
        counts[digest] = payload_count + 1
        boxes.setdefault(digest, []).append(box)
        return True

    # First pass maximizes source diversity before considering a second reach
    # from any already represented payload.
    for item in ranked:
        append_if_distinct(item, allow_second_per_payload=False)
        if len(selected) >= max_candidates:
            return selected
    for item in ranked:
        append_if_distinct(item, allow_second_per_payload=True)
        if len(selected) >= max_candidates:
            break
    return selected


def classify_window(context: dict[str, Any], nested: dict[str, Any] | None) -> tuple[str, str, float, list[str]]:
    relief = float(context["p05_p95_relief_m"])
    context_failures = [
        reason for reason in context["failure_reasons"] if reason != "regional_relief_above_700m_contrast_ceiling"
    ]
    if relief <= MAX_PRIMARY_CONTEXT_RELIEF_M and not context_failures:
        return "eligible_context", "context", float(context["score"]), []
    if MAX_PRIMARY_CONTEXT_RELIEF_M < relief <= MAX_CONTRAST_CONTEXT_RELIEF_M:
        if nested is not None:
            nested_failures = list(nested["failure_reasons"])
            if float(nested["p05_p95_relief_m"]) > MAX_NESTED_RELIEF_M:
                nested_failures.append("500_700m_context_requires_substantially_gentler_nested_reach")
            elif float(nested["p05_p95_relief_m"]) > relief * MAX_NESTED_TO_CONTEXT_RELIEF_RATIO:
                nested_failures.append("nested_reach_not_at_least_35_percent_gentler_than_context")
            if not nested_failures:
                return "eligible_nested_reach", "nested", float(nested["score"]), []
            return "contrast_only", "none", float(nested["score"]), sorted(set(nested_failures))
        return "contrast_only", "none", 0.0, ["500_700m_context_requires_gentler_nested_reach"]
    if relief > MAX_CONTRAST_CONTEXT_RELIEF_M:
        return "ineligible", "none", 0.0, ["regional_relief_above_700m_contrast_ceiling"]
    failures = list(context_failures)
    if nested is not None and nested["eligible"]:
        if float(nested["p05_p95_relief_m"]) <= MAX_NESTED_RELIEF_M:
            return "eligible_nested_reach", "nested", float(nested["score"]), []
    if nested is not None:
        failures.extend(nested["failure_reasons"])
    explicit_contrast_failures = {
        "path_grade_above_primary_gentle_reach_band_contrast_only",
        "path_adverse_rise_above_gentle_through_flow_band_contrast_only",
    }
    if failures and all(reason in explicit_contrast_failures for reason in failures):
        return "contrast_only", "none", float((nested or context)["score"]), sorted(set(failures))
    return "ineligible", "none", float(context["score"]), sorted(set(failures))


def candidate_origins(width: int, height: int) -> list[tuple[int, int]]:
    return PINNED.candidate_origins(
        width,
        height,
        crop_width=CONTEXT_WIDTH,
        crop_height=CONTEXT_HEIGHT,
        stride_x=SCAN_STRIDE_X,
        stride_z=SCAN_STRIDE_Z,
    )


def _candidate_rank_key(item: WindowResult) -> tuple[Any, ...]:
    return (
        item.status not in ("eligible_context", "eligible_nested_reach"),
        -item.score,
        -float((item.nested or item.context).get("paired_bank_ratio", 0.0)),
        -float((item.nested or item.context).get("longest_trunk_path_m", 0.0)),
        item.asset.elevation_sha256,
        item.z,
        item.x,
    )


def scan_assets(assets: list[Any], repo_root: Path) -> tuple[list[WindowResult], list[dict[str, Any]]]:
    results: list[WindowResult] = []
    per_asset: list[dict[str, Any]] = []
    total_excluded_previous = 0
    for asset_index, asset in enumerate(assets, start=1):
        canonical = asset.canonical
        print(
            f"[terrain-reach-scan] asset {asset_index}/{len(assets)} "
            f"{asset.elevation_sha256[:12]} {canonical.variant or 'default'}: loading raster",
            file=sys.stderr,
            flush=True,
        )
        manifest = json.loads(canonical.manifest_path.read_text())
        raw = np.memmap(
            canonical.elevation_path,
            dtype="<f4",
            mode="r",
            shape=(canonical.height, canonical.width),
        )
        elevation = transform_heightfield(raw, manifest)
        del raw
        valley_depth = compute_valley_depth(elevation)
        origins = candidate_origins(canonical.width, canonical.height)
        source_results: list[WindowResult] = []
        excluded_previous = 0
        for window_index, (x, z) in enumerate(origins, start=1):
            if window_index == 1 or window_index % 64 == 0:
                print(
                    f"[terrain-reach-scan] asset {asset_index}/{len(assets)} "
                    f"window {window_index}/{len(origins)}",
                    file=sys.stderr,
                    flush=True,
                )
            if PINNED.overlaps_previous_v2(asset.elevation_sha256, x, z):
                excluded_previous += 1
                total_excluded_previous += 1
                continue
            crop = elevation[z : z + CONTEXT_HEIGHT, x : x + CONTEXT_WIDTH]
            crop_depth = valley_depth[z : z + CONTEXT_HEIGHT, x : x + CONTEXT_WIDTH]
            crop_relief = regional_relief(crop)
            if crop_relief["p05_p95_relief_m"] > MAX_CONTRAST_CONTEXT_RELIEF_M:
                empty_cross = measure_cross_sections(crop, np.empty((0, 2), dtype=np.int32))
                context = {
                    **crop_relief,
                    "longest_trunk_path_m": 0.0,
                    "valley_mask_fraction": float(
                        np.count_nonzero(crop_depth >= VALLEY_DEPTH_THRESHOLD_M) / crop_depth.size
                    ),
                    "junction_cluster_count_diagnostic_only": 0,
                    "components_traced": 0,
                    **{key: value for key, value in empty_cross.items() if key != "stations"},
                    **_longitudinal_path_profile(crop, np.empty((0, 2), dtype=np.int32)),
                    "_path": np.empty((0, 2), dtype=np.int32),
                    "_cross_sections": [],
                    "failure_reasons": ["regional_relief_above_700m_contrast_ceiling"],
                    "eligible": False,
                    "score": 0.0,
                }
            else:
                context = analyze_elevation_window(crop, crop_depth, relief_metrics=crop_relief)
            nested = None
            if context["longest_trunk_path_m"] >= MIN_NESTED_TRUNK_LENGTH_M:
                nested = find_best_nested_reach(crop, crop_depth, context)
            status, selected_reach, score, failures = classify_window(context, nested)
            result = WindowResult(
                asset=asset,
                x=x,
                z=z,
                context=context,
                nested=nested,
                selected_reach=selected_reach,
                status=status,
                score=score,
                failures=failures,
            )
            source_results.append(result)
        results.extend(source_results)
        per_asset.append(
            {
                "elevation_sha256": asset.elevation_sha256,
                "canonical_manifest": PINNED._relative_path(canonical.manifest_path, repo_root),
                "alias_count": len(asset.aliases),
                "context_windows_total": len(origins),
                "context_windows_scanned": len(source_results),
                "previous_v2_overlaps_excluded": excluded_previous,
                "eligible_context_count": sum(item.status == "eligible_context" for item in source_results),
                "eligible_nested_reach_count": sum(item.status == "eligible_nested_reach" for item in source_results),
                "contrast_only_count": sum(item.status == "contrast_only" for item in source_results),
                "ineligible_count": sum(item.status == "ineligible" for item in source_results),
            }
        )
        print(
            f"[terrain-reach-scan] asset {asset_index}/{len(assets)} complete: "
            f"scanned={len(source_results)} excluded_prior={excluded_previous} "
            f"eligible_context={per_asset[-1]['eligible_context_count']} "
            f"eligible_nested={per_asset[-1]['eligible_nested_reach_count']}",
            file=sys.stderr,
            flush=True,
        )
    return results, per_asset


def _metrics_without_private(metrics: dict[str, Any] | None) -> dict[str, Any] | None:
    if metrics is None:
        return None
    return {
        key: value
        for key, value in metrics.items()
        if not key.startswith("_")
    }


def _as_candidate_record(item: WindowResult, rank: int | None = None) -> dict[str, Any]:
    canonical = item.asset.canonical
    return {
        "rank": rank,
        "status": item.status,
        "score": item.score,
        "selected_reach": item.selected_reach,
        "elevation_sha256": item.asset.elevation_sha256,
        "manifest_path": PINNED._relative_path(canonical.manifest_path, ROOT),
        "manifest_sha256": canonical.manifest_sha256,
        "alias_manifest_paths": [
            PINNED._relative_path(alias.manifest_path, ROOT) for alias in item.asset.aliases
        ],
        "variant": canonical.variant,
        "seed": canonical.seed,
        "context_crop": {
            "x": item.x,
            "z": item.z,
            "width": CONTEXT_WIDTH,
            "height": CONTEXT_HEIGHT,
            "sample_spacing_m": NATIVE_SPACING_M,
        },
        "context_metrics": _metrics_without_private(item.context),
        "nested_reach": None
        if item.nested is None
        else {
            "x": item.x + int(item.nested["x_in_context"]),
            "z": item.z + int(item.nested["z_in_context"]),
            "width": NESTED_WIDTH,
            "height": NESTED_HEIGHT,
            "sample_spacing_m": NATIVE_SPACING_M,
            "metrics": _metrics_without_private(item.nested),
        },
        "failure_reasons": item.failures,
        "interpretation": "topographic morphology proxy only; not mapped flow or hydrology truth",
    }


def _selected_reach_trace(item: WindowResult, source_elevation_m: np.ndarray) -> dict[str, Any] | None:
    trace_reach_kind = item.selected_reach
    diagnostic_only = item.selected_reach == "none"
    if item.selected_reach == "nested" and item.nested is not None:
        metrics = item.nested
    elif item.selected_reach == "context":
        metrics = item.context
    elif diagnostic_only and item.nested is not None:
        metrics = item.nested
        trace_reach_kind = "nested_diagnostic"
    elif diagnostic_only:
        metrics = item.context
        trace_reach_kind = "context_diagnostic"
    else:
        return None
    path = np.asarray(metrics.get("_path", np.empty((0, 2))), dtype=np.int32)
    if len(path) < 2:
        return None
    if trace_reach_kind.startswith("nested"):
        crop_x = item.x + int(metrics["x_in_context"])
        crop_z = item.z + int(metrics["z_in_context"])
        _width, _height = NESTED_WIDTH, NESTED_HEIGHT
    else:
        crop_x, crop_z, _width, _height = item.x, item.z, CONTEXT_WIDTH, CONTEXT_HEIGHT
    source_crop = source_elevation_m[crop_z : crop_z + _height, crop_x : crop_x + _width]
    longitudinal = _longitudinal_path_profile(source_crop, path, include_profile=True)
    longitudinal_rows = longitudinal.pop("_longitudinal_profile", [])
    for row in longitudinal_rows:
        row["x_native"] = crop_x + row.pop("x_native_local")
        row["z_native"] = crop_z + row.pop("z_native_local")

    def endpoint_record(point: np.ndarray) -> dict[str, Any]:
        local_z, local_x = map(int, point)
        native_x = crop_x + local_x
        native_z = crop_z + local_z
        return {
            "x_native": native_x,
            "z_native": native_z,
            "elevation_m": float(source_elevation_m[native_z, native_x]),
        }

    station_rows = metrics.get("_cross_sections", [])
    station_table: list[dict[str, Any]] = []
    for station in station_rows:
        station_table.append(
            {
                "path_distance_m": float(station["path_distance_m"]),
                "x_native": crop_x + int(station["x_native_local"]),
                "z_native": crop_z + int(station["z_native_local"]),
                "normal_x": float(station["normal_x"]),
                "normal_z": float(station["normal_z"]),
                "bed_elevation_m": float(station["floor_elevation_m"]),
                "floor_width_m": float(station["floor_width_m"]),
                "left_bank_relief_m": float(station["left_bank_relief_m"]),
                "right_bank_relief_m": float(station["right_bank_relief_m"]),
                "paired_banks": bool(station["paired_banks"]),
                "multi_cell_floor": bool(station["multi_cell_floor"]),
            }
        )

    representative_profiles: list[dict[str, Any]] = []
    if station_rows:
        selected_stations = [
            ("first-measured-section", station_rows[0]),
            ("mid-measured-section", station_rows[len(station_rows) // 2]),
            ("last-measured-section", station_rows[-1]),
        ]
        offsets_m = np.arange(-15, 16, dtype=np.int32) * int(NATIVE_SPACING_M)
        for label, station in selected_stations:
            center_x = int(station["x_native_local"])
            center_z = int(station["z_native_local"])
            normal_x = float(station["normal_x"])
            normal_z = float(station["normal_z"])
            local_x = np.rint(center_x + (offsets_m / NATIVE_SPACING_M) * normal_x).astype(np.int32)
            local_z = np.rint(center_z + (offsets_m / NATIVE_SPACING_M) * normal_z).astype(np.int32)
            native_x = local_x + crop_x
            native_z = local_z + crop_z
            representative_profiles.append(
                {
                    "label": label,
                    "path_distance_m": float(station["path_distance_m"]),
                    "center_x_native": crop_x + center_x,
                    "center_z_native": crop_z + center_z,
                    "normal_x": normal_x,
                    "normal_z": normal_z,
                    "offset_m": offsets_m.astype(int).tolist(),
                    "sample_x_native": native_x.astype(int).tolist(),
                    "sample_z_native": native_z.astype(int).tolist(),
                    "elevation_m": source_elevation_m[native_z, native_x].astype(float).tolist(),
                }
            )
    return {
        "trace_reach_kind": trace_reach_kind,
        "diagnostic_only": diagnostic_only,
        "coordinate_basis": "absolute native 30m sample indices; axis mapping follows the pinned manifest world_x/world_z mapping",
        "endpoint_orientation": "arbitrary skeleton-path order; not upstream/downstream or flow validation",
        "path_length_m": float(metrics["longest_trunk_path_m"]),
        "endpoint_separation_m": float(metrics["endpoint_separation_m"]),
        "route_directness_ratio": float(metrics["route_directness_ratio"]),
        "endpoint_a": endpoint_record(path[0]),
        "endpoint_b": endpoint_record(path[-1]),
        "high_to_low_endpoint_orientation_is_a_to_b": bool(
            longitudinal["path_orientation_high_to_low_is_forward"]
        ),
        "longitudinal_bed_profile": {
            "path_end_to_end_fall_m": float(longitudinal["path_end_to_end_fall_m"]),
            "path_end_to_end_slope": float(longitudinal["path_end_to_end_slope"]),
            "maximum_downstream_adverse_rise_m": float(
                longitudinal["path_max_downstream_adverse_rise_m"]
            ),
            "terminal_rise_above_path_minimum_m": float(
                longitudinal["path_terminal_rise_above_minimum_m"]
            ),
            "orientation": "high smoothed endpoint to low smoothed endpoint; not flow direction",
            "resampled_spacing_m": NATIVE_SPACING_M,
            "smoothing_window_samples": LONGITUDINAL_SMOOTHING_WINDOW_SAMPLES,
            "smoothing_aperture_m": (LONGITUDINAL_SMOOTHING_WINDOW_SAMPLES - 1)
            * NATIVE_SPACING_M,
            "profile_samples": longitudinal_rows,
        },
        "station_measurements": station_table,
        "representative_transverse_profiles": representative_profiles,
        "profile_sampling": "first/middle/last valid cross-section in arbitrary skeleton-path order; 31 native elevations at +/-450m on the measured local normal",
    }


def _candidate_csv_fields() -> tuple[str, ...]:
    fields = [
        "diagnostic_rank",
        "eligible_rank",
        "status",
        "score",
        "selected_reach",
        "elevation_sha256",
        "manifest_path",
        "manifest_sha256",
        "alias_manifest_paths",
        "variant",
        "seed",
        "context_x",
        "context_z",
        "context_width",
        "context_height",
        "context_p05_m",
        "context_p50_m",
        "context_p95_m",
        "context_p05_p95_relief_m",
    ]
    suffixes = (
        "longest_trunk_path_m",
        "endpoint_separation_m",
        "endpoint_separation_fraction_of_diagonal",
        "route_directness_ratio",
        "path_span_x_m",
        "path_span_z_m",
        "path_span_fraction_of_longer_crop_axis",
        "valley_mask_fraction",
        "junction_cluster_count_diagnostic_only",
        "components_traced",
        "tested_count",
        "paired_bank_count",
        "paired_bank_ratio",
        "multi_cell_floor_count",
        "multi_cell_floor_ratio",
        "median_left_bank_relief_m",
        "median_right_bank_relief_m",
        "median_floor_width_m",
        "minimum_floor_width_m",
        "station_end_to_end_fall_m",
        "station_end_to_end_slope",
        "station_downhill_step_fraction",
        "path_endpoint_a_elevation_m",
        "path_endpoint_b_elevation_m",
        "path_end_to_end_fall_m",
        "path_end_to_end_slope",
        "path_max_downstream_adverse_rise_m",
        "path_terminal_rise_above_minimum_m",
        "path_downhill_step_fraction",
    )
    fields.extend(f"context_{suffix}" for suffix in suffixes)
    fields.extend(
        [
            "nested_x",
            "nested_z",
            "nested_width",
            "nested_height",
            "nested_p05_m",
            "nested_p50_m",
            "nested_p95_m",
            "nested_p05_p95_relief_m",
        ]
    )
    fields.extend(f"nested_{suffix}" for suffix in suffixes)
    fields.append("failure_reasons")
    return tuple(fields)


def _write_candidates_csv(results: list[WindowResult], output_path: Path) -> None:
    fields = _candidate_csv_fields()
    ranked = sorted(results, key=_candidate_rank_key)
    eligible = [item for item in ranked if item.status in ("eligible_context", "eligible_nested_reach")]
    eligible_rank = {id(item): rank for rank, item in enumerate(eligible, start=1)}
    with output_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for rank, item in enumerate(ranked, start=1):
            row: dict[str, Any] = {
                "diagnostic_rank": rank,
                "eligible_rank": eligible_rank.get(id(item), ""),
                "status": item.status,
                "score": f"{item.score:.9f}",
                "selected_reach": item.selected_reach,
                "elevation_sha256": item.asset.elevation_sha256,
                "manifest_path": PINNED._relative_path(item.asset.canonical.manifest_path, ROOT),
                "manifest_sha256": item.asset.canonical.manifest_sha256,
                "alias_manifest_paths": ";".join(
                    PINNED._relative_path(alias.manifest_path, ROOT) for alias in item.asset.aliases
                ),
                "variant": item.asset.canonical.variant,
                "seed": item.asset.canonical.seed,
                "context_x": item.x,
                "context_z": item.z,
                "context_width": CONTEXT_WIDTH,
                "context_height": CONTEXT_HEIGHT,
                "failure_reasons": ";".join(item.failures),
            }
            for prefix, metrics in (("context", item.context), ("nested", item.nested)):
                if metrics is None:
                    continue
                row.update(
                    {
                        f"{prefix}_p05_m": f"{metrics['p05_m']:.6f}",
                        f"{prefix}_p50_m": f"{metrics['p50_m']:.6f}",
                        f"{prefix}_p95_m": f"{metrics['p95_m']:.6f}",
                        f"{prefix}_p05_p95_relief_m": f"{metrics['p05_p95_relief_m']:.6f}",
                    }
                )
                if prefix == "nested":
                    row["nested_x"] = item.x + metrics["x_in_context"]
                    row["nested_z"] = item.z + metrics["z_in_context"]
                    row["nested_width"] = NESTED_WIDTH
                    row["nested_height"] = NESTED_HEIGHT
                for key in fields:
                    metric_key = key.removeprefix(f"{prefix}_")
                    if key.startswith(f"{prefix}_") and metric_key in metrics and metric_key not in (
                        "p05_m",
                        "p50_m",
                        "p95_m",
                        "p05_p95_relief_m",
                    ):
                        value = metrics[metric_key]
                        row[key] = f"{value:.9f}" if isinstance(value, float) else value
            writer.writerow(row)


def _preview_image(item: WindowResult, source_cache: dict[str, np.ndarray]) -> Image.Image:
    raw = source_cache[item.asset.elevation_sha256]
    patch = raw[item.z : item.z + CONTEXT_HEIGHT, item.x : item.x + CONTEXT_WIDTH]
    low, high = np.percentile(patch, (2.0, 98.0))
    if high <= low:
        high = low + 1.0
    gray = np.clip((patch - low) / (high - low), 0.0, 1.0)
    image = Image.fromarray(np.rint(gray * 255.0).astype(np.uint8), mode="L").convert("RGB")
    draw = ImageDraw.Draw(image)
    context_path = item.context["_path"]
    if len(context_path) > 1:
        xy = [(int(x), int(z)) for z, x in context_path]
        draw.line(xy, fill=(255, 130, 20), width=2)
    selected_path: np.ndarray | None = None
    reach_x0 = reach_z0 = 0
    if item.selected_reach == "context":
        selected_path = context_path
    elif item.nested is not None:
        selected_path = item.nested["_path"]
        reach_x0 = int(item.nested["x_in_context"])
        reach_z0 = int(item.nested["z_in_context"])
        draw.rectangle(
            [reach_x0, reach_z0, reach_x0 + NESTED_WIDTH - 1, reach_z0 + NESTED_HEIGHT - 1],
            outline=(170, 255, 0),
            width=2,
        )
    if selected_path is not None:
        sections = item.context["_cross_sections"] if item.selected_reach == "context" else item.nested["_cross_sections"]
        for section in sections:
            z = int(section["z_native_local"]) + reach_z0
            x = int(section["x_native_local"]) + reach_x0
            nx = float(section["normal_x"])
            nz = float(section["normal_z"])
            half = int(BANK_SAMPLE_FAR_M / NATIVE_SPACING_M)
            start = (int(round(x - nx * half)), int(round(z - nz * half)))
            end = (int(round(x + nx * half)), int(round(z + nz * half)))
            draw.line([start, end], fill=(18, 234, 247), width=1)
            draw.ellipse([x - 2, z - 2, x + 2, z + 2], fill=(255, 245, 80))
        if len(selected_path) > 1:
            selected_xy = [
                (int(x) + reach_x0, int(z) + reach_z0) for z, x in selected_path
            ]
            draw.line(selected_xy, fill=(255, 35, 235), width=3)
    return image


def _write_contact_sheet(
    diagnostics: list[WindowResult], source_cache: dict[str, np.ndarray], output_dir: Path
) -> Path:
    selected = diagnostics[:MAX_CONTACT_SHEET_CANDIDATES]
    if not selected:
        selected = []
    columns = 2
    rows = max(1, math.ceil(len(selected) / columns))
    header = 56
    footer = 44
    cell_width = CONTEXT_WIDTH
    cell_height = header + CONTEXT_HEIGHT + footer
    sheet = Image.new("RGB", (columns * cell_width, rows * cell_height), "#151a20")
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    if not selected:
        draw.text((16, 16), "No windows produced a traceable valley proxy; see candidates.csv failure reasons.", fill="white", font=font)
    for index, item in enumerate(selected):
        col = index % columns
        row = index // columns
        left = col * cell_width
        top = row * cell_height
        metrics = item.nested if item.selected_reach == "nested" else item.context
        draw.text(
            (left + 6, top + 5),
            f"{index + 1}. {item.status}  {item.asset.canonical.variant or 'default'} seed={item.asset.canonical.seed}",
            fill="#f2f4f6",
            font=font,
        )
        relief = float(item.context["p05_p95_relief_m"])
        reach_relief = float((item.nested or item.context)["p05_p95_relief_m"])
        draw.text(
            (left + 6, top + 21),
            f"context x,z={item.x},{item.z}  p05-p95={relief:.0f}m  reach={reach_relief:.0f}m  score={item.score:.3f}",
            fill="#cbd3da",
            font=font,
        )
        sheet.paste(_preview_image(item, source_cache), (left, top + header))
        if metrics is None:
            footer_text = "; ".join(item.failures[:2]) or "no path"
        else:
            footer_text = (
                f"path {float(metrics['longest_trunk_path_m'])/1000:.1f}km  "
                f"direct {float(metrics['route_directness_ratio']):.2f}  "
                f"grade {float(metrics['path_end_to_end_slope']) * 100.0:.2f}%  "
                f"adverse {float(metrics['path_max_downstream_adverse_rise_m']):.1f}m  "
                f"banks L/R {float(metrics['median_left_bank_relief_m']):.1f}/{float(metrics['median_right_bank_relief_m']):.1f}m"
            )
        draw.text((left + 6, top + header + CONTEXT_HEIGHT + 5), footer_text[:106], fill="#d4dbe1", font=font)
    result = output_dir / "reach-contact-sheet.png"
    sheet.save(result, format="PNG", optimize=True)
    return result


def _thresholds() -> dict[str, Any]:
    return {
        "context_shape_width_height_native": [CONTEXT_WIDTH, CONTEXT_HEIGHT],
        "nested_shape_width_height_native": [NESTED_WIDTH, NESTED_HEIGHT],
        "native_sample_spacing_m": NATIVE_SPACING_M,
        "context_scan_stride_xz_native_samples": [SCAN_STRIDE_X, SCAN_STRIDE_Z],
        "valley_depth_proxy": {
            "smoothing_sigma_cells": SMOOTH_SIGMA_CELLS,
            "closing_size_cells": VALLEY_CLOSING_SIZE_CELLS,
            "closing_extent_m": VALLEY_CLOSING_SIZE_CELLS * NATIVE_SPACING_M,
            "minimum_depth_m": VALLEY_DEPTH_THRESHOLD_M,
            "min_component_cells": MIN_COMPONENT_CELLS,
            "components_traced_max": MAX_COMPONENTS_TO_TRACE,
        },
        "eligibility": {
            "minimum_context_p05_p95_relief_m": MIN_CONTEXT_RELIEF_M,
            "primary_context_p05_p95_relief_max_m": MAX_PRIMARY_CONTEXT_RELIEF_M,
            "contrast_context_p05_p95_relief_max_m": MAX_CONTRAST_CONTEXT_RELIEF_M,
            "contrast_context_requires_nested_relief_max_m": MAX_NESTED_RELIEF_M,
            "contrast_nested_to_context_relief_ratio_max": MAX_NESTED_TO_CONTEXT_RELIEF_RATIO,
            "minimum_nested_p05_p95_relief_m": MIN_NESTED_RELIEF_M,
            "minimum_context_trunk_length_m": MIN_CONTEXT_TRUNK_LENGTH_M,
            "minimum_nested_trunk_length_m": MIN_NESTED_TRUNK_LENGTH_M,
            "minimum_endpoint_separation_fraction_of_crop_diagonal": MIN_ENDPOINT_SEPARATION_FRACTION_OF_DIAGONAL,
            "minimum_path_span_fraction_of_longer_crop_axis": MIN_PATH_SPAN_FRACTION_OF_LONGER_CROP_AXIS,
            "minimum_route_directness_ratio_endpoint_separation_over_path_length": MIN_ROUTE_DIRECTNESS_RATIO,
            "visual_shortlist_max_per_payload": MAX_SHORTLIST_CANDIDATES_PER_PAYLOAD,
            "visual_shortlist_max_reach_box_overlap_ratio_within_payload": MAX_SHORTLIST_REACH_OVERLAP_RATIO,
            "full_path_profile": {
                "resampled_spacing_m": NATIVE_SPACING_M,
                "smoothing_window_samples": LONGITUDINAL_SMOOTHING_WINDOW_SAMPLES,
                "smoothing_aperture_m": (LONGITUDINAL_SMOOTHING_WINDOW_SAMPLES - 1) * NATIVE_SPACING_M,
                "orientation": "higher smoothed endpoint to lower smoothed endpoint; not flow direction",
                "minimum_primary_end_to_end_slope_m_per_m": MIN_END_TO_END_SLOPE,
                "maximum_primary_gentle_slope_m_per_m": MAX_PRIMARY_END_TO_END_SLOPE,
                "maximum_steep_stream_contrast_slope_m_per_m": MAX_CONTRAST_END_TO_END_SLOPE,
                "maximum_primary_adverse_rise_m": MAX_PRIMARY_PATH_ADVERSE_RISE_M,
                "maximum_pool_or_stepped_stream_contrast_adverse_rise_m": MAX_CONTRAST_PATH_ADVERSE_RISE_M,
            },
            "cross_section_spacing_m": CROSS_SECTION_SPACING_M,
            "minimum_cross_sections": MIN_CROSS_SECTIONS,
            "floor_tolerance_m": FLOOR_TOLERANCE_M,
            "minimum_multi_cell_floor_width_m": MIN_FLOOR_WIDTH_M,
            "bank_sample_distance_m": [BANK_SAMPLE_NEAR_M, BANK_SAMPLE_FAR_M],
            "minimum_bank_relief_each_side_m": MIN_BANK_RELIEF_M,
            "minimum_paired_bank_ratio": MIN_PAIRED_BANK_RATIO,
            "minimum_multi_cell_floor_ratio": MIN_MULTI_CELL_FLOOR_RATIO,
            "minimum_downhill_station_step_fraction": MIN_DOWNHILL_STEP_FRACTION,
            "plausible_primary_end_to_end_slope_m_per_m": [MIN_END_TO_END_SLOPE, MAX_PRIMARY_END_TO_END_SLOPE],
            "steep_stream_contrast_end_to_end_slope_m_per_m": [MAX_PRIMARY_END_TO_END_SLOPE, MAX_CONTRAST_END_TO_END_SLOPE],
        },
        "score": "moderate regional relief preference near 260m multiplied by normalized path length, smaller bilateral median bank incision, repeated paired-bank ratio, and repeated multi-cell-floor ratio; junction count is diagnostic only",
    }


def run_scan(
    source_root: Path = ROOT / SOURCE_ROOT_RELATIVE,
    output_dir: Path = ROOT / OUTPUT_RELATIVE,
) -> dict[str, Any]:
    source_root = source_root.resolve()
    output_dir = output_dir.resolve()
    if output_dir.exists():
        raise FileExistsError(f"refusing to overwrite existing reach scan output: {output_dir}")
    if output_dir.is_relative_to(ROOT / Path("outputs/fluid/terrain-site-scan-v1-20260923")):
        raise ReachScanError("reach-scan output must not be placed inside the prior terrain-site scan")
    if output_dir.is_relative_to(ROOT / Path("outputs/fluid/terrain-site-water-v1-20260923")):
        raise ReachScanError("reach-scan output must not be placed inside the frozen solver study")
    assets, inventory_identity = load_pinned_assets(source_root)
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    output_dir.mkdir(parents=False, exist_ok=False)
    start = time.perf_counter()
    results, per_asset = scan_assets(assets, ROOT)
    ranked = sorted(results, key=_candidate_rank_key)
    eligible = [item for item in ranked if item.status in ("eligible_context", "eligible_nested_reach")]
    diagnostics = _diverse_shortlist(eligible if eligible else ranked)
    source_cache: dict[str, np.ndarray] = {}
    by_hash = {asset.elevation_sha256: asset for asset in assets}
    for digest, asset in by_hash.items():
        manifest = json.loads(asset.canonical.manifest_path.read_text())
        raw = np.memmap(
            asset.canonical.elevation_path,
            dtype="<f4",
            mode="r",
            shape=(asset.canonical.height, asset.canonical.width),
        )
        source_cache[digest] = transform_heightfield(raw, manifest)
    csv_path = output_dir / "candidates.csv"
    _write_candidates_csv(results, csv_path)
    contact_path = _write_contact_sheet(diagnostics, source_cache, output_dir)
    elapsed = time.perf_counter() - start
    eligible_records = [
        _as_candidate_record(item, rank)
        for rank, item in enumerate(eligible, start=1)
    ]
    near_misses = [
        _as_candidate_record(item, rank)
        for rank, item in enumerate(_near_miss_results(ranked, eligible)[:12], start=1)
    ]
    visual_shortlist_records: list[dict[str, Any]] = []
    for rank, item in enumerate(diagnostics, start=1):
        record = _as_candidate_record(item, rank)
        record["selected_reach_trace"] = _selected_reach_trace(
            item, source_cache[item.asset.elevation_sha256]
        )
        visual_shortlist_records.append(record)
    candidate_status_counts = {
        status: sum(item.status == status for item in results)
        for status in ("eligible_context", "eligible_nested_reach", "contrast_only", "ineligible")
    }
    status = "PASS_CANDIDATE_FOUND" if eligible else "NO_CREDIBLE_CANDIDATE"
    artifacts = {
        "candidates_csv": {
            "path": csv_path.name,
            "sha256": sha256_file(csv_path),
            "size_bytes": csv_path.stat().st_size,
        },
        "contact_sheet_png": {
            "path": contact_path.name,
            "sha256": sha256_file(contact_path),
            "size_bytes": contact_path.stat().st_size,
        },
    }
    index = {
        "schema": SCHEMA,
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "status": status,
        "discovery_only": True,
        "terrain_mutated": False,
        "solver_input_generated": False,
        "new_seed_generation_performed": False,
        "water_truth_claim": "none; these are elevation-only morphology heuristics",
        "fixture_or_default_promotion": "none",
        "source_root": PINNED._relative_path(source_root, ROOT),
        "source_inventory": inventory_identity,
        "source_scanner": {
            "path": PINNED._relative_path(PINNED_SCANNER_PATH, ROOT),
            "sha256": inventory_identity["pinned_scanner_sha256"],
            "source_scan_sha256": PINNED_SCAN_SHA256,
            "source_manifest_count": inventory_identity["manifest_count"],
            "unique_payload_count": inventory_identity["unique_payload_count"],
        },
        "reach_scan_runner": {
            "path": PINNED._relative_path(Path(__file__).resolve(), ROOT),
            "sha256": sha256_file(Path(__file__).resolve()),
        },
        "thresholds": _thresholds(),
        "limitations": [
            "The closing-minus-smoothed elevation is a local valley-depth proxy; it is not flow accumulation or a channel map.",
            "The longest skeleton route is a connected morphology path, not a downhill flow solution; endpoint fall and station slopes are only plausibility filters.",
            "Bank and floor measurements use fixed cross-sections perpendicular to the selected path and are sensitive to path tangent, local terrain texture, and the chosen scales.",
            "No climate, drainage network, depression filling, rainfall, solver response, or water truth is evaluated.",
            "A shortlisted reach needs primary visual review and a separate frozen-solver audition before any fixture or product claim.",
        ],
        "inventory": [PINNED._asset_inventory(asset, ROOT) for asset in assets],
        "scan_counts": {
            "context_windows_per_asset_nominal": len(candidate_origins(2048, 2048)),
            "context_windows_scanned": len(results),
            "prior_v2_overlap_windows_excluded": sum(
                row["previous_v2_overlaps_excluded"] for row in per_asset
            ),
            "eligible_context_count": candidate_status_counts["eligible_context"],
            "eligible_nested_reach_count": candidate_status_counts["eligible_nested_reach"],
            "contrast_only_count": candidate_status_counts["contrast_only"],
            "ineligible_count": candidate_status_counts["ineligible"],
            "elapsed_seconds": round(elapsed, 3),
            "per_asset": per_asset,
        },
        "ranking_policy": "Eligibility gates are applied before rank. Score rewards moderate regional relief and a long connected route with repeated bilateral banks and multi-cell floors; junction count is never a bonus.",
        "visual_shortlist_policy": {
            "selection_order": "eligible rank order; first pass prefers a distinct elevation payload per slot, second pass permits at most two per payload",
            "within_payload_overlap_metric": "intersection area divided by the smaller selected-reach crop area",
            "maximum_within_payload_overlap_ratio": MAX_SHORTLIST_REACH_OVERLAP_RATIO,
            "exact_duplicate_nested_coordinates_rejected": True,
            "source_diversity_preferred": True,
        },
        "eligible_candidates": eligible_records,
        "visual_shortlist": visual_shortlist_records,
        "diagnostic_near_misses": near_misses,
        "artifacts": artifacts,
    }
    (output_dir / "scan.json").write_text(json.dumps(index, indent=2, sort_keys=True) + "\n")
    return index


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT / SOURCE_ROOT_RELATIVE)
    parser.add_argument("--output-dir", type=Path, default=ROOT / OUTPUT_RELATIVE)
    parser.add_argument("--preflight-only", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        if args.preflight_only:
            assets, identity = load_pinned_assets(args.source_root)
            print(
                json.dumps(
                    {
                        "status": "PASS",
                        "inventory": identity,
                        "unique_elevation_sha256": sorted(asset.elevation_sha256 for asset in assets),
                    },
                    indent=2,
                    sort_keys=True,
                )
            )
            return 0
        result = run_scan(args.source_root, args.output_dir)
        print(
            json.dumps(
                {
                    "status": result["status"],
                    "output_dir": str(args.output_dir.resolve()),
                    "scan_counts": result["scan_counts"],
                },
                indent=2,
                sort_keys=True,
            )
        )
        return 0
    except (OSError, ValueError, KeyError, ReachScanError) as error:
        print(f"terrain reach scan failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
