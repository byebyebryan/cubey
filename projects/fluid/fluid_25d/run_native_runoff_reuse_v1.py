#!/usr/bin/env python3
"""Opt-in native SynxFlow 1.0.1 reuse controls; no Cubey solver changes."""

from __future__ import annotations

import argparse
from collections import deque
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from typing import Any

import numpy as np


ROOT = Path(__file__).resolve().parents[3]
STUDY_REL = Path("outputs/fluid/native-runoff-reuse-v1-20261002-trm3Ve")
STUDY_ROOT = ROOT / STUDY_REL
SETUP_ROOT = STUDY_ROOT / "synxflow-setup"
SETUP_JSON = SETUP_ROOT / "setup.json"
CONTRACT = STUDY_ROOT / "CONTRACT.md"
RAIN_FIXTURE = ROOT / "projects/fluid/fluid_25d/fixtures/hillside-rain-study-v1/temperate-mountain-rain.json"
MOUNTAIN_MANIFEST = ROOT / "cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json"
BASIN_SUMMARY = ROOT / "outputs/fluid/hillside-rain-v2-20261002-t6mh4w/basin-reading-final/summary.json"
DEPRESSION_LABELS = ROOT / "outputs/fluid/hillside-rain-v2-20261002-t6mh4w/basin-reading-final/analytical-depression-labels.npy"
SCHEMA = "cubey.fluid25d.native_runoff_reuse.v1"
NODATA = -9999.0
ASC_HALF_QUANTIZATION_M = 0.5e-6
DRY_NONZERO_HU = 0.0
RAIN_RATE_M_PER_S = 12.0e-3 / 3600.0
SHEET_H_M = 0.01
SHEET_N = 0.033
SHEET_SLOPES = (0.02, 0.04, 0.06, 0.08, 0.10)
SHEET_MEASURE_TIMES = (30.0, 45.0, 60.0)
INTERIOR_X_M = (640.0, 1280.0)
INTERIOR_Y_M = (60.0, 180.0)
MOUNTAIN_RAIN_RATE_M_PER_S = 12.0e-3 / 3600.0
MOUNTAIN_MANNING_N = 0.05
MOUNTAIN_CROP_XZWH = (1152, 1408, 512, 512)
MOUNTAIN_DX_M = 30.0
MOUNTAIN_EXPORT_INTERVAL_S = 30.0
MOUNTAIN_PROBE_DURATION_S = 300.0
MOUNTAIN_FULL_DURATION_S = 7200.0
MOUNTAIN_REPLAY_DURATION_S = 600.0
MOUNTAIN_EDGE_EXCLUSION_M = 120.0
MOUNTAIN_CORRIDOR_SPAN_M = 300.0
MOUNTAIN_CORRIDOR_SUPPORT_S = 900.0
MATERIAL_DEPTH_M = 0.01
ACTIVE_SPEED_M_PER_S = 0.02
FIXED_ROIS_XZWH = {
    "upper": (203, 203, 21, 21),
    "transit": (128, 224, 21, 21),
    "collection": (48, 241, 21, 21),
}
FIXED_DEPRESSION_LABELS = (20, 172, 119)
FIXED_DEPRESSION_MASK_SHA256 = {
    20: "3ec05f7ebddd1704bb16a8297176cc4cac91cbf493a0e859d89037a115821b41",
    172: "a41d9dea45ad22943e2a5a3ca05ec001d9172c3fb28bb9e4defe8ad534ab67d5",
    119: "f807c1c67a32a7a4c735e2e6a5d2498e18f70e3b07a637ea12eed993f856f5ba",
}


class RunoffError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def checked_under(root: Path, path: Path) -> Path:
    root = root.resolve()
    result = path.resolve()
    if result == root or root not in result.parents:
        raise RunoffError(f"path must be a strict descendant of {root}: {path}")
    return result


def create_fresh_leaf(root: Path, path: Path) -> Path:
    result = checked_under(root, path)
    if result.exists() or result.is_symlink():
        raise FileExistsError(f"refusing to overwrite existing output leaf: {result}")
    if not result.parent.is_dir():
        raise FileNotFoundError(f"output parent must already exist: {result.parent}")
    result.mkdir()
    return result


def read_ascii(path: Path) -> tuple[dict[str, float], np.ndarray, np.ndarray]:
    """Read a six-line ESRI ASCII raster without flipping its stored row order."""
    lines = path.read_text(encoding="ascii").splitlines()
    if len(lines) < 7:
        raise RunoffError(f"ASCII raster is truncated: {path}")
    header: dict[str, float] = {}
    for line in lines[:6]:
        pieces = line.split()
        if len(pieces) != 2:
            raise RunoffError(f"invalid ASCII header line in {path}: {line!r}")
        header[pieces[0].lower()] = float(pieces[1])
    required = {"ncols", "nrows", "xllcorner", "yllcorner", "cellsize", "nodata_value"}
    if set(header) != required:
        raise RunoffError(f"unexpected ASCII header keys in {path}: {sorted(header)}")
    rows = int(header["nrows"])
    cols = int(header["ncols"])
    values = np.fromstring(" ".join(lines[6:]), sep=" ", dtype=np.float64)
    if values.size != rows * cols:
        raise RunoffError(f"ASCII shape mismatch in {path}: expected {rows * cols}, got {values.size}")
    data = values.reshape((rows, cols))
    valid = data != header["nodata_value"]
    if not valid.any():
        raise RunoffError(f"ASCII raster has no valid cells: {path}")
    return header, data, valid


def write_ascii(path: Path, data: np.ndarray, cellsize_m: float, nodata: float = NODATA) -> None:
    array = np.asarray(data, dtype=np.float64)
    if array.ndim != 2 or not np.isfinite(array).all() or cellsize_m <= 0:
        raise ValueError("ASCII raster input must be a finite 2D array and positive cell size")
    rows, cols = array.shape
    with path.open("w", encoding="ascii", newline="\n") as stream:
        stream.write(f"ncols {cols}\n")
        stream.write(f"nrows {rows}\n")
        stream.write("xllcorner 0.000000\n")
        stream.write("yllcorner 0.000000\n")
        stream.write(f"cellsize {cellsize_m:.6f}\n")
        stream.write(f"NODATA_value {nodata:.6f}\n")
        for row in array:
            stream.write(" ".join(f"{value:.9f}" for value in row) + "\n")


def control_specs() -> list[dict[str, Any]]:
    specs: list[dict[str, Any]] = []
    for datum_m in (0.0, 2000.0):
        specs.append({
            "name": f"flat-rain-bed-{int(datum_m)}m",
            "family": "flat-rain",
            "bed_datum_m": datum_m,
            "rows": 32,
            "cols": 32,
            "dx_m": 10.0,
            "duration_s": 600,
            "output_interval_s": 30,
            "manning_n": 0.05,
            "rain_rate_m_per_s": RAIN_RATE_M_PER_S,
            "boundary": "rigid",
            "initial_depth_m": 0.0,
            "orientation": "flat",
        })
    for slope in SHEET_SLOPES:
        specs.append({
            "name": f"sheet-x-s{slope:.2f}-dx10m",
            "family": "thin-sheet",
            "rows": 24,
            "cols": 192,
            "dx_m": 10.0,
            "duration_s": 60,
            "output_interval_s": 5,
            "manning_n": SHEET_N,
            "rain_rate_m_per_s": 0.0,
            "boundary": "rigid",
            "initial_depth_m": SHEET_H_M,
            "slope": slope,
            "orientation": "x",
        })
    specs.append({
        "name": "sheet-x-s0.10-dx30m-intended-grid-diagnostic",
        "family": "thin-sheet",
        "rows": 8,
        "cols": 64,
        "dx_m": 30.0,
        "duration_s": 60,
        "output_interval_s": 5,
        "manning_n": SHEET_N,
        "rain_rate_m_per_s": 0.0,
        "boundary": "rigid",
        "initial_depth_m": SHEET_H_M,
        "slope": 0.10,
        "orientation": "x",
        "diagnostic_only": True,
    })
    specs.append({
        "name": "sheet-worldz-s0.10-dx10m-orientation",
        "family": "thin-sheet",
        "rows": 192,
        "cols": 24,
        "dx_m": 10.0,
        "duration_s": 60,
        "output_interval_s": 5,
        "manning_n": SHEET_N,
        "rain_rate_m_per_s": 0.0,
        "boundary": "rigid",
        "initial_depth_m": SHEET_H_M,
        "slope": 0.10,
        "orientation": "world-z-row-downhill",
    })
    return specs


def bed_grid(spec: dict[str, Any]) -> np.ndarray:
    rows, cols, dx = int(spec["rows"]), int(spec["cols"]), float(spec["dx_m"])
    orientation = spec["orientation"]
    if orientation == "flat":
        return np.full((rows, cols), float(spec["bed_datum_m"]), dtype=np.float64)
    slope = float(spec["slope"])
    if orientation == "x":
        axis = (np.arange(cols, dtype=np.float64) + 0.5) * dx
        return np.broadcast_to(1000.0 - slope * axis, (rows, cols)).copy()
    if orientation == "world-z-row-downhill":
        axis = (np.arange(rows, dtype=np.float64) + 0.5) * dx
        return np.broadcast_to((1000.0 - slope * axis)[:, None], (rows, cols)).copy()
    raise ValueError(f"unknown orientation: {orientation}")


def mountain_case_specs() -> dict[str, dict[str, Any]]:
    """Frozen D/F configurations; the CLI explicitly selects an authorized phase."""
    common = {
        "family": "mountain-rain",
        "rows": 512,
        "cols": 512,
        "dx_m": MOUNTAIN_DX_M,
        "manning_n": MOUNTAIN_MANNING_N,
        "rain_rate_m_per_s": MOUNTAIN_RAIN_RATE_M_PER_S,
        "boundary": "fall",
        "initial_depth_m": 0.0,
        "orientation": "world-z-row-downhill",
        "crop_xzwh": list(MOUNTAIN_CROP_XZWH),
    }
    return {
        "probe": {
            **common, "name": "mountain-30m-300s-probe", "duration_s": 300,
            "rain_history_end_s": 7200,
            "output_interval_s": 30, "purpose": "timing and essential native output health only",
        },
        "full": {
            **common, "name": "mountain-30m-7200s", "duration_s": 7200,
            "rain_history_end_s": 7200,
            "output_interval_s": 60, "purpose": "conditional two-hour primary observation",
        },
        "replay": {
            **common, "name": "mountain-30m-600s-replay", "duration_s": 600,
            "rain_history_end_s": 7200,
            "output_interval_s": 60, "purpose": "fresh deterministic short-horizon replay matching full cadence",
        },
        "refinement": {
            **common, "name": "mountain-15m-600s-refinement", "rows": 1024,
            "cols": 1024, "dx_m": 15.0, "duration_s": 600,
            "rain_history_end_s": 7200,
            "output_interval_s": 60, "purpose": "same-extent short-horizon sensitivity",
        },
    }


def mountain_source_arrays() -> tuple[dict[str, Any], np.ndarray, np.ndarray, dict[str, Any]]:
    """Verify frozen source identities and return transformed crop plus halo."""
    fixture = read_json(RAIN_FIXTURE)
    expected_fixture = {
        "schema": "cubey.fluid25d.hillside_rain_study.v1",
        "crop_xzwh": list(MOUNTAIN_CROP_XZWH),
        "cell_size_m": 30,
        "transformed_crop_sha256": "02f1577d96f3ea4f65b75c7b21b6e1b2c83248cebb8b87a528fff6193435bc3f",
        "regions_xzwh": {name: list(rect) for name, rect in FIXED_ROIS_XZWH.items()},
        "material_depth_m": "0.01",
        "active_speed_m_per_s": "0.02",
        "corridor_edge_exclusion_m": 120,
        "corridor_minimum_span_m": 300,
        "corridor_support_duration_seconds": 900,
    }
    for key, value in expected_fixture.items():
        if fixture.get(key) != value:
            raise RunoffError(f"frozen mountain fixture field changed: {key}")
    manifest_path = ROOT / fixture["manifest"]
    if manifest_path.resolve() != MOUNTAIN_MANIFEST.resolve():
        raise RunoffError("mountain manifest path differs from frozen study input")
    if sha256_file(RAIN_FIXTURE) != "4965d7a873df98a59e7c75764fa6191057e46f63cb2d5cf75aeb6b1af762cae2":
        raise RunoffError("mountain fixture hash differs from frozen study input")
    if sha256_file(manifest_path) != fixture["manifest_sha256"]:
        raise RunoffError("immutable terrain manifest hash mismatch")
    manifest = read_json(manifest_path)
    elevation_meta = manifest["files"]["elevation"]
    elevation_path = manifest_path.parent / elevation_meta["path"]
    if (manifest.get("schema") != "cubey.terrain.heightfield.v1"
            or manifest["grid"].get("sample_spacing_m") != 30
            or elevation_meta.get("dtype") != "float32-le"
            or elevation_meta.get("shape") != [2048, 2048]
            or elevation_meta.get("sha256") != fixture["elevation_sha256"]
            or sha256_file(elevation_path) != fixture["elevation_sha256"]):
        raise RunoffError("immutable terrain elevation identity/shape mismatch")
    x0, z0, width, height = MOUNTAIN_CROP_XZWH
    if x0 <= 0 or z0 <= 0 or x0 + width >= 2048 or z0 + height >= 2048:
        raise RunoffError("crop does not have the required immutable one-cell halo")
    raw = np.fromfile(elevation_path, dtype="<f4")
    if raw.size != 2048 * 2048:
        raise RunoffError("immutable elevation byte count does not match 2048x2048 float32")
    raw = raw.reshape((2048, 2048))
    raw_halo = raw[z0 - 1:z0 + height + 1, x0 - 1:x0 + width + 1]
    if raw_halo.shape != (height + 2, width + 2) or not np.isfinite(raw_halo).all():
        raise RunoffError("source crop/halo contains invalid elevation samples")
    offset = np.float32(manifest["height"]["offset_m"])
    scale = np.float32(manifest["height"]["scale"])
    transformed_halo = ((raw_halo + offset) * scale).astype("<f4", copy=False)
    transformed_crop = transformed_halo[1:-1, 1:-1].copy()
    crop_digest = sha256_bytes(transformed_crop.tobytes(order="C"))
    if crop_digest != fixture["transformed_crop_sha256"]:
        raise RunoffError(f"transformed crop hash mismatch: {crop_digest}")
    identity = {
        "fixture_path": str(RAIN_FIXTURE.resolve()),
        "fixture_sha256": sha256_file(RAIN_FIXTURE),
        "manifest_path": str(manifest_path.resolve()),
        "manifest_sha256": sha256_file(manifest_path),
        "elevation_path": str(elevation_path.resolve()),
        "elevation_sha256": sha256_file(elevation_path),
        "elevation_dtype": "little-endian float32",
        "elevation_shape": [2048, 2048],
        "crop_xzwh": list(MOUNTAIN_CROP_XZWH),
        "crop_cell_size_m": 30,
        "transformed_crop_sha256": crop_digest,
        "transformed_crop_shape": list(transformed_crop.shape),
        "transformed_crop_dtype": "little-endian float32",
        "transformed_halo_sha256": sha256_bytes(transformed_halo.tobytes(order="C")),
        "transformed_halo_shape": list(transformed_halo.shape),
        "transform": "float32 ((raw_float32 + float32(offset_m))*float32(scale))",
        "row_orientation": "preserved from source row-major z,x; row0 is low source world-Z and maps to native high northing; worldZ velocity=-native hUy/h",
        "physical_extent_m": [15360, 15360],
    }
    return fixture, transformed_crop, transformed_halo, identity


def piecewise_linear_refine_2x(source_with_halo: np.ndarray) -> np.ndarray:
    """Sample V6's fixed diagonal triangles at 15 m centers over the same extent."""
    source = np.asarray(source_with_halo, dtype=np.float64)
    if source.shape != (514, 514) or not np.isfinite(source).all():
        raise ValueError("2x refinement requires a finite 514x514 source crop plus one-cell halo")
    fine_centers_m = (np.arange(1024, dtype=np.float64) + 0.5) * 15.0
    # Halo index 0 is the original sample center at local coordinate -15 m.
    coord = (fine_centers_m + 15.0) / 30.0
    row_coord = coord[:, None]
    col_coord = coord[None, :]
    row0 = np.floor(row_coord).astype(np.int32)
    col0 = np.floor(col_coord).astype(np.int32)
    fy = row_coord - row0
    fx = col_coord - col0
    z00 = source[row0, col0]
    z10 = source[row0, col0 + 1]
    z01 = source[row0 + 1, col0]
    z11 = source[row0 + 1, col0 + 1]
    on_00_11_10 = fx >= fy
    first = z00 * (1.0 - fx) + z10 * (fx - fy) + z11 * fy
    second = z00 * (1.0 - fy) + z01 * (fy - fx) + z11 * fx
    return np.where(on_00_11_10, first, second).astype("<f8")


def fixed_roi_masks(shape: tuple[int, int] = (512, 512)) -> dict[str, np.ndarray]:
    if shape != (512, 512):
        raise ValueError("frozen observation ROIs are defined on the 30m 512x512 crop")
    masks = {}
    for name, (x0, z0, width, height) in FIXED_ROIS_XZWH.items():
        mask = np.zeros(shape, dtype=bool)
        mask[z0:z0 + height, x0:x0 + width] = True
        masks[name] = mask
    return masks


def fixed_depression_masks() -> tuple[dict[str, np.ndarray], dict[str, Any]]:
    summary = read_json(BASIN_SUMMARY)
    labels_hash = sha256_file(DEPRESSION_LABELS)
    if (summary.get("schema") != "cubey.fluid25d.hillside_rain_basins.v2"
            or summary.get("shape_zx") != [512, 512]
            or summary.get("spacing_m") != 30.0
            or summary.get("input_identity", {}).get("transformed_crop_sha256")
                != "02f1577d96f3ea4f65b75c7b21b6e1b2c83248cebb8b87a528fff6193435bc3f"
            or summary.get("mask_payload_sha256") != labels_hash):
        raise RunoffError("frozen analytical-depression masks no longer match their summary identity")
    labels = np.load(DEPRESSION_LABELS, allow_pickle=False)
    if labels.shape != (512, 512) or labels.dtype != np.int32:
        raise RunoffError("frozen depression label raster has unexpected shape or dtype")
    names = {20: "label-20", 172: "label-172", 119: "label-119"}
    masks = {names[label]: labels == label for label in FIXED_DEPRESSION_LABELS}
    expected_counts = {20: 984, 172: 229, 119: 150}
    actual_counts = {label: int(np.count_nonzero(labels == label)) for label in FIXED_DEPRESSION_LABELS}
    if actual_counts != expected_counts:
        raise RunoffError(f"frozen depression label cells changed: {actual_counts}")
    mask_hashes = {}
    for label, name in names.items():
        digest = sha256_bytes(masks[name].astype(np.uint8).tobytes(order="C"))
        mask_hashes[str(label)] = digest
        if digest != FIXED_DEPRESSION_MASK_SHA256[label]:
            raise RunoffError(f"frozen depression boolean mask hash changed for label {label}: {digest}")
    identity = {
        "summary_path": str(BASIN_SUMMARY.resolve()),
        "summary_sha256": sha256_file(BASIN_SUMMARY),
        "labels_path": str(DEPRESSION_LABELS.resolve()),
        "labels_sha256": labels_hash,
        "labels_dtype": str(labels.dtype),
        "labels_shape": list(labels.shape),
        "fixed_labels_cell_counts": {str(key): actual_counts[key] for key in FIXED_DEPRESSION_LABELS},
        "fixed_boolean_mask_sha256": mask_hashes,
        "mask_meaning": "frozen analytical positive-fill depressions, not observed/verified lakes",
    }
    return masks, identity


def expanded_roi_mask(rect_xzwh: tuple[int, int, int, int], factor: int, shape: tuple[int, int]) -> np.ndarray:
    x0, z0, width, height = rect_xzwh
    mask = np.zeros(shape, dtype=bool)
    mask[z0 * factor:(z0 + height) * factor, x0 * factor:(x0 + width) * factor] = True
    return mask


def d4_components(mask: np.ndarray) -> list[np.ndarray]:
    """Return sorted flat indices for deterministic four-neighbor components."""
    candidate = np.asarray(mask, dtype=bool)
    if candidate.ndim != 2:
        raise ValueError("D4 component mask must be 2D")
    rows, cols = candidate.shape
    visited = np.zeros(candidate.size, dtype=bool)
    flat = candidate.ravel()
    components = []
    for start in np.flatnonzero(flat):
        if visited[start]:
            continue
        visited[start] = True
        pending = [int(start)]
        cells = []
        while pending:
            current = pending.pop()
            cells.append(current)
            row, col = divmod(current, cols)
            for neighbor in (
                current - cols if row > 0 else -1,
                current + cols if row + 1 < rows else -1,
                current - 1 if col > 0 else -1,
                current + 1 if col + 1 < cols else -1,
            ):
                if neighbor >= 0 and flat[neighbor] and not visited[neighbor]:
                    visited[neighbor] = True
                    pending.append(neighbor)
        components.append(np.asarray(sorted(cells), dtype=np.int64))
    components.sort(key=lambda cells: (int(cells[0]) if cells.size else -1, -int(cells.size)))
    return components


def _component_geometry(cells: np.ndarray, cols: int, dx_m: float) -> dict[str, Any]:
    rows = cells // cols
    x = cells % cols
    min_x, max_x = int(x.min()), int(x.max())
    min_z, max_z = int(rows.min()), int(rows.max())
    return {
        "cells": int(cells.size),
        "bbox_cells_inclusive_xz": [min_x, min_z, max_x, max_z],
        "span_x_m": float((max_x - min_x) * dx_m),
        "span_z_m": float((max_z - min_z) * dx_m),
        "maximum_axis_span_m": float(max(max_x - min_x, max_z - min_z) * dx_m),
    }


def _export_speed(fields: dict[str, Any]) -> tuple[np.ndarray, dict[str, Any]]:
    """Derive speed from saved h/hU without silently defining dry-cell speed."""
    depth = fields["h"]
    hux, huy = fields["hUx"], fields["hUy"]
    positive = depth > 0.0
    dry_nonzero = (depth == 0.0) & ((hux != 0.0) | (huy != 0.0))
    speed = np.full(depth.shape, np.nan, dtype=np.float64)
    with np.errstate(over="ignore", divide="ignore", invalid="ignore"):
        speed[positive] = np.hypot(hux[positive], huy[positive]) / depth[positive]
    finite_speed = speed[positive & np.isfinite(speed)]
    return speed, {
        "semantics": "export-derived speed=hypot(saved hUx,saved hUy)/saved h for h>0; h==0 is undefined, not assigned zero",
        "maximum_export_derived_speed_m_per_s": float(finite_speed.max()) if finite_speed.size else None,
        "positive_h_cell_count": int(np.count_nonzero(positive)),
        "positive_h_nonfinite_speed_cell_count": int(np.count_nonzero(positive & ~np.isfinite(speed))),
        "h_zero_nonzero_hU_count": int(np.count_nonzero(dry_nonzero)),
        "maximum_hU_magnitude_where_h_zero_m2_per_s": float(np.max(np.hypot(hux[dry_nonzero], huy[dry_nonzero]))) if dry_nonzero.any() else 0.0,
        "serialization_caveat": "h/hUx/hUy are six-decimal ASC exports; this is not an independently exported native velocity or exact pre-serialization maximum",
    }


def mountain_metrics(
    spec: dict[str, Any],
    snapshots: dict[float, dict[str, Any]],
    source_bed: np.ndarray,
    native_bed: np.ndarray,
    roi_masks_30m: dict[str, np.ndarray],
    depression_masks_30m: dict[str, np.ndarray],
) -> dict[str, Any]:
    """D/E observations from released center fields; no face-flux inference."""
    dx = float(spec["dx_m"])
    rows, cols = int(spec["rows"]), int(spec["cols"])
    if source_bed.shape != (rows, cols) or native_bed.shape != (rows, cols):
        raise RunoffError("bed comparison fields do not match the saved native grid")
    factor = round(MOUNTAIN_DX_M / dx)
    if factor not in (1, 2) or not math.isclose(dx * factor, MOUNTAIN_DX_M):
        raise RunoffError("mountain observation grid must be frozen 30m or 15m")
    area = dx * dx
    rate = float(spec["rain_rate_m_per_s"])
    expected_roi_names = set(FIXED_ROIS_XZWH)
    if set(roi_masks_30m) != expected_roi_names:
        raise RunoffError("fixed observation ROI names differ from the frozen fixture")
    for name, mask in roi_masks_30m.items():
        if mask.shape != (512, 512) or not np.asarray(mask, dtype=bool).any():
            raise RunoffError(f"invalid frozen 30m ROI mask: {name}")
    if any(mask.shape != (512, 512) for mask in depression_masks_30m.values()):
        raise RunoffError("fixed depression mask shape differs from native 30m grid")
    edge_cells = math.ceil(MOUNTAIN_EDGE_EXCLUSION_M / dx)
    global_observations = []
    roi_observations = {name: [] for name in FIXED_ROIS_XZWH}
    depression_storage_observations = {name: [] for name in depression_masks_30m}
    corridor_observations = []
    pond_observations = {name: [] for name in depression_masks_30m}
    output_times = sorted(snapshots)
    consecutive_corridor_support = 0.0
    maximum_corridor_support = 0.0
    previous_time: float | None = None
    previous_corridor = False
    pond_support = {name: 0.0 for name in depression_masks_30m} if factor == 1 else {}
    pond_max_support = {name: 0.0 for name in depression_masks_30m} if factor == 1 else {}
    previous_pond_time: float | None = None
    previous_pond_flags = {name: False for name in depression_masks_30m} if factor == 1 else {}
    roi_masks = {
        name: np.repeat(np.repeat(np.asarray(mask, dtype=bool), factor, axis=0), factor, axis=1)
        for name, mask in roi_masks_30m.items()
    }
    depression_masks = {
        name: np.repeat(np.repeat(mask, factor, axis=0), factor, axis=1)
        for name, mask in depression_masks_30m.items()
    }
    for timestamp in output_times:
        fields = snapshots[timestamp]
        depth, hux, huy = fields["h"], fields["hUx"], fields["hUy"]
        speed, speed_audit = _export_speed(fields)
        speed_finite = np.isfinite(speed)
        wet = depth > 0.0
        material = depth >= MATERIAL_DEPTH_M
        direct_depth = rate * timestamp
        excess = depth - direct_depth
        wet_volume = float(depth.sum(dtype=np.float64) * area)
        material_water_volume = float(depth[material].sum(dtype=np.float64) * area)
        wet_weights = depth[wet] * area
        material_weights = depth[material] * area
        wet_speed = speed[wet]
        material_speed = speed[material]
        wet_speed_ok = np.isfinite(wet_speed)
        material_speed_ok = np.isfinite(material_speed)
        positive_speed_values = speed[wet & np.isfinite(speed)]
        global_rain_quantization = rows * cols * area * ASC_HALF_QUANTIZATION_M
        global_rain_residual = wet_volume - direct_depth * rows * cols * area
        all_wet_weighted_speed = (float(np.sum(wet_speed[wet_speed_ok] * wet_weights[wet_speed_ok])
                                         / np.sum(wet_weights[wet_speed_ok]))
                                 if wet_speed_ok.any() else None)
        global_observations.append({
            "time_s": timestamp,
            "direct_uniform_rain_depth_m": direct_depth,
            "direct_uniform_rain_volume_m3": direct_depth * rows * cols * area,
            "saved_water_volume_m3": wet_volume,
            "saved_minus_direct_rain_volume_m3": global_rain_residual,
            "global_volume_output_quantization_interval_m3": [-global_rain_quantization, global_rain_quantization],
            "saved_minus_direct_rain_volume_interval_m3": [global_rain_residual - global_rain_quantization,
                                                           global_rain_residual + global_rain_quantization],
            "saved_storage_exceeds_total_direct_rain_beyond_output_quantization": global_rain_residual > global_rain_quantization,
            "saved_depth_min_m": float(depth.min()),
            "saved_depth_max_m": float(depth.max()),
            "saved_depth_nonnegative": bool(np.all(depth >= 0.0)),
            "saved_h_hUx_hUy_finite": bool(np.isfinite(depth).all() and np.isfinite(hux).all() and np.isfinite(huy).all()),
            "material_h_ge_0.01_cell_count": int(np.count_nonzero(material)),
            "material_h_ge_0.01_water_volume_m3": material_water_volume,
            "maximum_material_export_derived_speed_m_per_s": float(material_speed[material_speed_ok].max()) if material_speed_ok.any() else None,
            "material_export_derived_speed_p99_m_per_s": float(np.percentile(material_speed[material_speed_ok], 99)) if material_speed_ok.any() else None,
            "material_water_volume_weighted_export_derived_speed_m_per_s": float(np.sum(material_speed[material_speed_ok] * material_weights[material_speed_ok]) / np.sum(material_weights[material_speed_ok])) if material_speed_ok.any() else None,
            "all_wet_export_derived_speed_p50_p90_p99_m_per_s": np.percentile(positive_speed_values, [50, 90, 99]).tolist() if positive_speed_values.size else None,
            "all_wet_water_volume_weighted_export_derived_speed_m_per_s": all_wet_weighted_speed,
            "maximum_to_all_wet_volume_weighted_speed_ratio": (float(positive_speed_values.max() / all_wet_weighted_speed)
                                                                  if positive_speed_values.size and all_wet_weighted_speed else None),
            "speed_review_note": "thin-film-inclusive maximum and material/all-wet distributions are exposed for review; no suspicious-speed cutoff is invented here",
            **speed_audit,
            "mapped_worldz_momentum_semantics": "native hUy is northing momentum; world-Z momentum=-hUy; speed magnitude is unchanged",
        })
        for name, base_mask in roi_masks.items():
            selected_depth = depth[base_mask]
            direct = direct_depth * int(np.count_nonzero(base_mask)) * area
            observed = float(selected_depth.sum(dtype=np.float64) * area)
            quantization = int(np.count_nonzero(base_mask)) * area * ASC_HALF_QUANTIZATION_M
            roi_observations[name].append({
                "time_s": timestamp,
                "cell_count": int(np.count_nonzero(base_mask)),
                "water_volume_m3": observed,
                "direct_local_rain_volume_m3": direct,
                "net_lateral_storage_m3": observed - direct,
                "output_quantization_interval_m3": [-quantization, quantization],
                "net_lateral_storage_interval_m3": [observed - direct - quantization, observed - direct + quantization],
                "maximum_depth_m": float(selected_depth.max()),
                "export_precision": "ASC six-decimal depth; interval is cell_count*dx^2*0.5e-6m3",
            })
        for name, mask in depression_masks.items():
            selected_depth = depth[mask]
            raster_cell_count = int(np.count_nonzero(mask))
            physical_mask_area = raster_cell_count * area
            observed = float(selected_depth.sum(dtype=np.float64) * area)
            direct = direct_depth * physical_mask_area
            quantization = physical_mask_area * ASC_HALF_QUANTIZATION_M
            depression_storage_observations[name].append({
                "time_s": timestamp,
                "raster_cell_count": raster_cell_count,
                "equivalent_30m_cell_count": int(np.count_nonzero(depression_masks_30m[name])),
                "physical_mask_area_m2": physical_mask_area,
                "water_volume_m3": observed,
                "direct_local_rain_volume_m3": direct,
                "net_lateral_storage_m3": observed - direct,
                "output_quantization_interval_m3": [-quantization, quantization],
                "net_lateral_storage_interval_m3": [observed - direct - quantization,
                                                     observed - direct + quantization],
                "export_precision": "ASC six-decimal depth; interval is physical_mask_area*0.5e-6m",
            })
        route = (
            material
            & (excess >= MATERIAL_DEPTH_M)
            & (speed >= ACTIVE_SPEED_M_PER_S)
        )
        if edge_cells:
            interior = np.zeros((rows, cols), dtype=bool)
            interior[edge_cells:rows - edge_cells, edge_cells:cols - edge_cells] = True
            route &= interior
        route_components = d4_components(route)
        component_records = []
        for cells in route_components:
            geometry = _component_geometry(cells, cols, dx)
            geometry["meets_300m_span"] = geometry["maximum_axis_span_m"] >= MOUNTAIN_CORRIDOR_SPAN_M
            component_records.append(geometry)
        component_records.sort(key=lambda item: (-item["maximum_axis_span_m"], -item["cells"], item["bbox_cells_inclusive_xz"]))
        qualifies = any(item["meets_300m_span"] for item in component_records)
        if qualifies and previous_corridor and previous_time is not None:
            consecutive_corridor_support += timestamp - previous_time
        elif qualifies:
            consecutive_corridor_support = 0.0
        else:
            consecutive_corridor_support = 0.0
        maximum_corridor_support = max(maximum_corridor_support, consecutive_corridor_support)
        corridor_observations.append({
            "time_s": timestamp,
            "candidate_cell_count": int(np.count_nonzero(route)),
            "moving_concentrated_water_volume_m3": float(depth[route].sum(dtype=np.float64) * area),
            "moving_concentrated_excess_volume_m3": float(np.maximum(excess[route], 0.0).sum(dtype=np.float64) * area),
            "material_water_volume_h_ge_0.01_m3": material_water_volume,
            "d4_component_count": len(component_records),
            "components": component_records,
            "largest_component": component_records[0] if component_records else None,
            "any_component_span_ge_300m": qualifies,
            "successive_observation_support_seconds": consecutive_corridor_support,
            "interpretation": "D4 component of h>=.01m, h-R*t>=.01m and export-derived speed>=.02m/s, excluding 120m edge; support does not track a parcel/path or prove between-export continuity",
        })
        previous_corridor = qualifies
        previous_time = timestamp
        for name, mask in depression_masks.items():
            wet_depression = mask & (depth >= MATERIAL_DEPTH_M)
            pond_components = d4_components(wet_depression)
            records = []
            any_candidate = False
            for cells in pond_components:
                geometry = _component_geometry(cells, cols, dx)
                z_values = native_bed.ravel()[cells].astype(np.float64)
                h_values = depth.ravel()[cells]
                u_values = speed.ravel()[cells]
                surface = z_values + h_values
                weights = h_values * area
                valid_speed = np.isfinite(u_values)
                weighted_speed = float(np.sum(u_values[valid_speed] * weights[valid_speed]) / np.sum(weights[valid_speed])) if valid_speed.any() else None
                surface_range = float(surface.max() - surface.min())
                connected_area = geometry["cells"] * area
                minimum_candidate_area = 4 * MOUNTAIN_DX_M * MOUNTAIN_DX_M
                candidate = (
                    connected_area >= minimum_candidate_area
                    and surface_range <= 0.05
                    and weighted_speed is not None
                    and weighted_speed <= 0.02
                )
                any_candidate = any_candidate or candidate
                original_z = source_bed.ravel()[cells].astype(np.float64)
                records.append({
                    **geometry,
                    "connected_area_m2": connected_area,
                    "minimum_candidate_area_m2": minimum_candidate_area,
                    "minimum_depth_m": float(h_values.min()),
                    "maximum_depth_m": float(h_values.max()),
                    "native_float32_bed_plus_depth_surface_range_m": surface_range,
                    "native_float32_bed_min_m": float(z_values.min()),
                    "native_float32_bed_max_m": float(z_values.max()),
                    "case_source_bed_plus_depth_surface_range_m": float((original_z + h_values).max() - (original_z + h_values).min()),
                    "volume_weighted_export_derived_speed_m_per_s": weighted_speed,
                    "quiet_near_level_candidate_at_this_observation": candidate,
                })
            records.sort(key=lambda item: (-item["cells"], item["bbox_cells_inclusive_xz"]))
            if factor == 1:
                if any_candidate and previous_pond_flags[name] and previous_pond_time is not None:
                    pond_support[name] += timestamp - previous_pond_time
                elif any_candidate:
                    pond_support[name] = 0.0
                else:
                    pond_support[name] = 0.0
                pond_max_support[name] = max(pond_max_support[name], pond_support[name])
                previous_pond_flags[name] = any_candidate
            pond_observations[name].append({
                "time_s": timestamp,
                "wet_cell_count_h_ge_0.01": int(np.count_nonzero(wet_depression)),
                "components": records,
                "minimum_candidate_area_m2": 4 * MOUNTAIN_DX_M * MOUNTAIN_DX_M,
                "any_quiet_near_level_candidate": any_candidate,
                "candidate_classification_scope": (
                    "conditional 30m observation; temporal support is sampled between exports"
                    if factor == 1 else "15m refinement-only diagnostic, not the 30m pond gate"
                ),
                "successive_candidate_observation_support_seconds": pond_support[name] if factor == 1 else None,
                "15min_candidate_support_observed": (pond_support[name] >= 900.0) if factor == 1 else None,
            })
        if factor == 1:
            previous_pond_time = timestamp
    return {
        "family": "native continuous-rain mountain runoff",
        "case_purpose": spec.get("purpose", spec["name"]),
        "status": "healthy" if all(item["saved_depth_nonnegative"] and item["saved_h_hUx_hUy_finite"] and item["positive_h_nonfinite_speed_cell_count"] == 0 for item in global_observations) else "health_failed",
        "saved_field_health_gate_passed": all(item["saved_depth_nonnegative"] and item["saved_h_hUx_hUy_finite"] and item["positive_h_nonfinite_speed_cell_count"] == 0 for item in global_observations),
        "gate_passed": None,
        "gate_semantics": "saved-field health is reported separately; route observations are sampled corridor support, and quiet near-level ponds remain conditional observations rather than calibrated/pass claims",
        "saved_times_s": output_times,
        "global_observations": global_observations,
        "fixed_roi_observations": roi_observations,
        "fixed_depression_net_rain_observations": depression_storage_observations,
        "moving_concentrated_corridor_observations": corridor_observations,
        "longest_successive_300m_corridor_support_s": maximum_corridor_support,
        "corridor_900s_support_criterion_met": maximum_corridor_support >= MOUNTAIN_CORRIDOR_SUPPORT_S,
        "corridor_sampled_support_status": "sampled_support_observed" if maximum_corridor_support >= MOUNTAIN_CORRIDOR_SUPPORT_S else "required_sampled_support_not_observed",
        "corridor_support_semantics": "criterion requires any qualifying D4 component at successive saved observations spanning900s; component identity is not tracked and continuity between exports is not established",
        "pond_observations_by_frozen_depression": pond_observations,
        "longest_successive_pond_candidate_support_s": pond_max_support,
        "pond_15min_criteria_met_by_frozen_depression": {name: value >= 900.0 for name, value in pond_max_support.items()},
        "pond_classification_scope": "conditional D4-connected near-level candidates in frozen masks only; not proof of equilibrium, spill, upstream branches or calibrated lake behavior",
        "source_bed_comparison": "original immutable float32-transformed point bed retained separately; pond surface uses actual native Scalar float32 bed from z.dat plus saved depth",
        "unobservable": ["native applied face flux", "intermediate-stage positivity/clipping", "exact pre-serialization native speed maximum"],
    }


def time_grid(spec: dict[str, Any]) -> list[float]:
    end = float(spec["duration_s"])
    interval = float(spec["output_interval_s"])
    count = round(end / interval)
    if count <= 0 or not math.isclose(count * interval, end, rel_tol=0.0, abs_tol=1e-9):
        raise RunoffError("duration must be an exact multiple of output interval")
    return [float(i * interval) for i in range(count + 1)]


def _input_hashes(input_dir: Path) -> dict[str, str]:
    return {
        path.relative_to(input_dir).as_posix(): sha256_file(path)
        for path in sorted(input_dir.rglob("*"))
        if path.is_file()
    }


def _element_records(path: Path) -> tuple[np.ndarray, np.ndarray]:
    """Read released cell-ID rows and all serialized scalar/vector columns."""
    lines = path.read_text(encoding="ascii").splitlines()
    marker = next((index for index, line in enumerate(lines) if line.startswith("$Element_id")), None)
    if marker is None:
        raise RunoffError(f"missing element-value marker: {path}")
    element_ids = []
    values = []
    for line in lines[marker + 1:]:
        if line.startswith("$"):
            break
        pieces = line.split()
        if len(pieces) >= 2:
            element_ids.append(int(pieces[0]))
            values.append([float(value) for value in pieces[1:]])
    if not values:
        raise RunoffError(f"empty element-value section: {path}")
    widths = {len(row) for row in values}
    if len(widths) != 1:
        raise RunoffError(f"inconsistent number of field values in {path}")
    return np.asarray(element_ids, dtype=np.int64), np.asarray(values, dtype=np.float64)


def _element_pairs(path: Path) -> tuple[np.ndarray, np.ndarray]:
    ids, values = _element_records(path)
    if values.shape[1] != 1:
        raise RunoffError(f"expected a single-value field in {path}, found {values.shape[1]}")
    return ids, values[:, 0]


def _element_values(path: Path) -> np.ndarray:
    """Return values from a released `Element_id Value` field file."""
    return _element_pairs(path)[1]


def _grid_from_native_ids(path: Path, rows: int, cols: int) -> tuple[np.ndarray, np.ndarray]:
    """Map native IDs back to ASC row order using released bottom-up IDs."""
    element_ids, serialized_values = _element_pairs(path)
    expected = rows * cols
    if element_ids.size != expected or not np.array_equal(np.sort(element_ids), np.arange(expected)):
        raise RunoffError(f"native field IDs do not cover the complete raster in {path}")
    serialized_grid = np.empty((rows, cols), dtype=np.float64)
    for element_id, value in zip(element_ids, serialized_values):
        row_from_bottom, col = divmod(int(element_id), cols)
        row = rows - 1 - row_from_bottom
        serialized_grid[row, col] = value
    return serialized_grid, serialized_grid.astype("<f4")


def _difference_stats(actual: np.ndarray, expected: np.ndarray) -> dict[str, float]:
    delta = np.asarray(actual, dtype=np.float64) - np.asarray(expected, dtype=np.float64)
    if delta.shape != np.asarray(expected).shape or not np.isfinite(delta).all():
        raise RunoffError("cannot compare non-finite or differently shaped bed arrays")
    return {
        "maximum_absolute_error_m": float(np.max(np.abs(delta))),
        "rms_error_m": float(np.sqrt(np.mean(delta * delta))),
    }


def _native_input_audit(spec: dict[str, Any], case_dir: Path) -> dict[str, Any]:
    native_input = case_dir / "native" / "input"
    field = native_input / "field"
    rows, cols = int(spec["rows"]), int(spec["cols"])
    _, source_bed, source_valid = read_ascii(case_dir / "DEM.asc")
    if source_bed.shape != (rows, cols) or not source_valid.all():
        raise RunoffError("native input audit found an unexpected DEM shape or NODATA")
    serialized_bed, native_float_bed = _grid_from_native_ids(field / "z.dat", rows, cols)
    difference = serialized_bed - source_bed
    float_difference = native_float_bed.astype(np.float64) - source_bed
    mesh_header, mesh_bed, mesh_valid = read_ascii(native_input / "mesh" / "DEM.txt")
    if mesh_bed.shape != (rows, cols) or not mesh_valid.all():
        raise RunoffError("native mesh DEM shape or validity differs from source raster")
    boundary_h = _serialized_boundary_codes(field / "h.dat")
    boundary_hu = _serialized_boundary_codes(field / "hU.dat")
    expected_boundary = (3, 0, 0) if spec["boundary"] == "fall" else (2, 0, 0)
    expected_hu = (3, 0, 0) if spec["boundary"] == "fall" else (2, 2, 0)
    if not boundary_h or not boundary_hu or any(code != expected_boundary for code in boundary_h) or any(code != expected_hu for code in boundary_hu):
        raise RunoffError(f"serialized perimeter boundary is not the declared {spec['boundary']} mapping")
    manning_values = _element_values(field / "manning.dat")
    if not np.allclose(manning_values, float(spec["manning_n"]), rtol=0.0, atol=1e-7):
        raise RunoffError("serialized native Manning field differs from the declared uniform value")
    sink_names = ("sewer_sink", "cumulative_depth", "hydraulic_conductivity", "capillary_head", "water_content_diff")
    sink_values = {name: _element_values(field / f"{name}.dat") for name in sink_names}
    if any(np.any(values != 0.0) for values in sink_values.values()):
        raise RunoffError("serialized native sink/source parameter is nonzero")
    rain_mask_values = _element_values(field / "precipitation_mask.dat")
    if np.any(rain_mask_values != 0.0):
        raise RunoffError("uniform-rain control unexpectedly serialized a masked-out precipitation cell")
    rain_source = np.loadtxt(field / "precipitation_source_all.dat", skiprows=1, ndmin=2, dtype=np.float64)
    expected_rain = float(spec["rain_rate_m_per_s"])
    rain_end = float(spec.get("rain_history_end_s", spec["duration_s"]))
    if (rain_source.shape != (2, 2)
            or not np.allclose(rain_source[:, 0], [0.0, rain_end], rtol=0.0, atol=1e-8)
            or not np.allclose(rain_source[:, 1], expected_rain, rtol=2e-6, atol=1e-14)):
        raise RunoffError("serialized continuous-rain schedule differs from the frozen case rate/horizon")
    h_ids, h_values = _element_records(field / "h.dat")
    hu_ids, hu_values = _element_records(field / "hU.dat")
    expected_initial = float(spec["initial_depth_m"])
    if (h_values.shape != (rows * cols, 1) or not np.all(h_values[:, 0] == expected_initial)
            or not np.array_equal(hu_ids, h_ids) or hu_values.shape != (rows * cols, 2)
            or not np.all(hu_values == 0.0)):
        raise RunoffError("serialized native initial h/hU differs from the declared zero-momentum state")
    runtime = (native_input / "times_setup.dat").read_text(encoding="ascii").split()
    if runtime != ["0", str(int(spec["duration_s"])), str(int(spec["output_interval_s"])), str(int(spec["duration_s"]))]:
        raise RunoffError(f"serialized native runtime/export schedule mismatch: {runtime}")
    return {
        "native_id_mapping": "released InputModel indep_functions._get_cell_id_array: IDs are bottom row first, each row left-to-right; map id//ncols to row=nrows-1-row_from_bottom",
        "z_field_path": str((field / "z.dat").resolve()),
        "z_field_sha256": sha256_file(field / "z.dat"),
        "z_element_count": int(serialized_bed.size),
        "serialized_z_matches_field_ids": True,
        "serialized_z_uses_six_significant_digit_%g": True,
        "serialized_z_max_abs_error_vs_9_decimal_input_dem_m": float(np.max(np.abs(difference))),
        "serialized_z_rms_error_vs_9_decimal_input_dem_m": float(np.sqrt(np.mean(difference * difference))),
        "native_scalar_dtype": "float32 (per official 1.0.1 sdist Scalar.h; wheel extension hash recorded separately)",
        "native_float32_z_max_abs_error_vs_9_decimal_input_dem_m": float(np.max(np.abs(float_difference))),
        "native_float32_z_rms_error_vs_9_decimal_input_dem_m": float(np.sqrt(np.mean(float_difference * float_difference))),
        "native_float32_bed_grid_row_order": "restored to input ASC order before ROI/pond analysis",
        "mesh_dem_header": mesh_header,
        "mesh_dem_max_abs_error_vs_source_m": float(np.max(np.abs(mesh_bed - source_bed))),
        "serialized_boundary_h_counts": {str(code): boundary_h.count(code) for code in set(boundary_h)},
        "serialized_boundary_hU_counts": {str(code): boundary_hu.count(code) for code in set(boundary_hu)},
        "declared_boundary": spec["boundary"],
        "serialized_manning_unique_values": np.unique(manning_values).tolist(),
        "serialized_sink_unique_values": {name: np.unique(values).tolist() for name, values in sink_values.items()},
        "serialized_rain_mask_unique_values": np.unique(rain_mask_values).tolist(),
        "serialized_continuous_rain_source_rows": rain_source.tolist(),
        "serialized_initial_h_unique_values": np.unique(h_values[:, 0]).tolist(),
        "serialized_initial_hU_max_abs_m2_per_s": float(np.max(np.abs(hu_values))),
        "serialized_runtime_values": runtime,
        "native_z_values_for_pond_bed_plus_depth": "field/z.dat values mapped by released Element_id ordering and cast to native float32; mesh DEM.txt is retained separately and is not treated as native z precision",
    }


def _serialized_boundary_codes(path: Path) -> list[tuple[int, int, int]]:
    lines = path.read_text(encoding="ascii").splitlines()
    try:
        marker = lines.index("$Boundary Numbers")
    except ValueError as error:
        raise RunoffError(f"missing serialized boundary section: {path}") from error
    count = int(lines[marker + 1])
    codes = []
    # The released writer emits a second `$Element_id Value` heading inside
    # the boundary section before the boundary records themselves.
    start = marker + 3
    for line in lines[start:start + count]:
        pieces = line.split()
        if len(pieces) != 4:
            raise RunoffError(f"malformed serialized boundary code in {path}: {line!r}")
        codes.append(tuple(int(value) for value in pieces[1:]))
    if len(codes) != count:
        raise RunoffError(f"serialized boundary count mismatch in {path}")
    return codes


def _make_model_inputs(spec: dict[str, Any], case_dir: Path, input_model_type: Any = None) -> dict[str, str]:
    """Use released InputModel APIs; this path does not launch the GPU."""
    if input_model_type is None:
        from synxflow.IO.InputModel import InputModel as input_model_type

    dem_path = case_dir / "DEM.asc"
    native_dir = case_dir / "native"
    if not dem_path.is_file():
        raise FileNotFoundError(f"case DEM is missing: {dem_path}")
    if native_dir.exists():
        raise FileExistsError(f"refusing to overwrite native case folder: {native_dir}")
    rows, cols = int(spec["rows"]), int(spec["cols"])
    model = input_model_type(dem_data=str(dem_path), case_folder=str(native_dir))
    model.set_initial_condition("h0", np.full((rows, cols), float(spec["initial_depth_m"]), dtype=np.float32))
    model.set_initial_condition("hU0x", np.zeros((rows, cols), dtype=np.float32))
    model.set_initial_condition("hU0y", np.zeros((rows, cols), dtype=np.float32))
    boundary = str(spec.get("boundary", "rigid"))
    model.set_boundary_condition([], outline_boundary=boundary)
    duration = float(spec["duration_s"])
    rain_end = float(spec.get("rain_history_end_s", duration))
    rate = float(spec["rain_rate_m_per_s"])
    model.set_rainfall(rain_mask=0, rain_source=np.asarray([[0.0, rate], [rain_end, rate]], dtype=np.float64))
    model.set_grid_parameter(
        manning=float(spec["manning_n"]),
        sewer_sink=0.0,
        cumulative_depth=0.0,
        hydraulic_conductivity=0.0,
        capillary_head=0.0,
        water_content_diff=0.0,
    )
    model.set_runtime([0.0, duration, float(spec["output_interval_s"]), duration])
    model.set_device_no([0])
    model.write_input_files()
    return _input_hashes(native_dir / "input")


def _child(case_dir: Path) -> int:
    from synxflow import __version__, flood
    from synxflow.IO.InputModel import InputModel

    case_dir = case_dir.resolve()
    spec = read_json(case_dir / "case-spec.json")
    if __version__ != "1.0.1":
        raise RunoffError(f"expected SynxFlow 1.0.1, got {__version__}")
    print(f"RUNNER_SYNXFLOW_VERSION={__version__}", flush=True)
    print(f"RUNNER_PYTHON={sys.executable}", flush=True)
    body_started = time.perf_counter()
    before = _make_model_inputs(spec, case_dir, input_model_type=InputModel)
    print("RUNNER_INPUT_HASHES_BEFORE_RUN=" + json.dumps(before, sort_keys=True), flush=True)
    print("RUNNER_NATIVE_INPUT_AUDIT=" + json.dumps(_native_input_audit(spec, case_dir), sort_keys=True), flush=True)
    flood_started = time.perf_counter()
    flood.run(str(case_dir / "native"))
    print(f"RUNNER_FLOOD_RUN_WALL_SECONDS={time.perf_counter() - flood_started:.9f}", flush=True)
    print(f"RUNNER_MODEL_BODY_SECONDS={time.perf_counter() - body_started:.9f}", flush=True)
    return 0


def snapshot_paths(output_dir: Path) -> dict[str, dict[float, Path]]:
    result: dict[str, dict[float, Path]] = {field: {} for field in ("h", "hUx", "hUy")}
    pattern = re.compile(r"^(hUx|hUy|h)_(\d+(?:\.\d+)?)\.asc$")
    for path in output_dir.glob("*.asc"):
        match = pattern.fullmatch(path.name)
        if match is None:
            continue
        field, timestamp = match.group(1), float(match.group(2))
        if timestamp in result[field]:
            raise RunoffError(f"duplicate saved field time {field}@{timestamp}")
        result[field][timestamp] = path
    if not result["h"] or any(set(result[field]) != set(result["h"]) for field in ("hUx", "hUy")):
        raise RunoffError("h, hUx, and hUy snapshot times differ or are missing")
    return result


def parse_timestep_log(path: Path, duration_s: float) -> dict[str, Any]:
    rows = np.loadtxt(path, dtype=np.float64, ndmin=2)
    if rows.shape[1] != 2 or rows.shape[0] < 2 or not np.isfinite(rows).all():
        raise RunoffError(f"invalid native timestep log: {path}")
    current, next_dt = rows[:, 0], rows[:, 1]
    current_steps = np.diff(np.concatenate(([0.0], current)))
    if np.any(np.diff(current) < 0) or np.any(next_dt < 0):
        raise RunoffError("native timestep log has decreasing time or negative dt")
    if not math.isclose(float(current[-1]), duration_s, rel_tol=0.0, abs_tol=max(1e-4, duration_s * 1e-8)):
        raise RunoffError(f"native timestep log ended at {current[-1]}, expected {duration_s}")
    consumed_positive = current_steps[current_steps > 0]
    positive_next = next_dt[next_dt > 0]
    return {
        "rows": int(rows.shape[0]),
        "logged_current_time_first_s": float(current[0]),
        "logged_current_time_last_s": float(current[-1]),
        "next_dt_semantics": "dt column is the next proposed step after forward/updateByCFL; final row is terminal",
        "next_dt_min_positive_s": float(positive_next.min()) if positive_next.size else None,
        "next_dt_max_s": float(next_dt.max()),
        "terminal_zero_next_dt_rows": int(np.count_nonzero(next_dt == 0)),
        "rounded_duplicate_current_time_rows": int(np.count_nonzero(current_steps == 0)),
        "derived_consumed_dt_min_positive_s": float(consumed_positive.min()) if consumed_positive.size else None,
        "derived_consumed_dt_max_positive_s": float(consumed_positive.max()) if consumed_positive.size else None,
        "derived_consumed_dt_precision": "current_time is emitted through default C++ stream precision; approximate log-derived interval; rounded duplicate times are not proof of native stagnation",
    }


def read_case_snapshots(output_dir: Path, spec: dict[str, Any]) -> tuple[dict[float, dict[str, Any]], dict[str, Any]]:
    paths = snapshot_paths(output_dir)
    expected_times = time_grid(spec)
    actual_times = sorted(paths["h"])
    if len(actual_times) != len(expected_times) or any(
        not math.isclose(a, e, rel_tol=0.0, abs_tol=1e-5)
        for a, e in zip(actual_times, expected_times)
    ):
        raise RunoffError(f"saved output times {actual_times} do not match expected {expected_times}")
    snapshots: dict[float, dict[str, Any]] = {}
    shape_expected = (int(spec["rows"]), int(spec["cols"]))
    reference_header: dict[str, float] | None = None
    for timestamp in actual_times:
        fields: dict[str, Any] = {}
        for field in ("h", "hUx", "hUy"):
            header, data, valid = read_ascii(paths[field][timestamp])
            if data.shape != shape_expected:
                raise RunoffError(f"unexpected {field} raster shape at {timestamp}: {data.shape}")
            if not np.isfinite(data).all():
                raise RunoffError(f"non-finite {field} raster at {timestamp}")
            if reference_header is None:
                reference_header = header
            elif header != reference_header:
                raise RunoffError(f"ASCII header changed at saved time {timestamp}")
            fields[field] = data
            fields[field + "_valid"] = valid
        valid = fields["h_valid"]
        if not np.array_equal(valid, fields["hUx_valid"]) or not np.array_equal(valid, fields["hUy_valid"]):
            raise RunoffError(f"field validity masks differ at {timestamp}")
        if not valid.all():
            raise RunoffError("synthetic B/C controls unexpectedly contain NODATA cells")
        if np.any(fields["h"][valid] < 0):
            raise RunoffError(f"negative saved water depth at {timestamp}")
        zero_nonzero = valid & (fields["h"] == 0) & ((fields["hUx"] != 0) | (fields["hUy"] != 0))
        fields["h_zero_nonzero_hU_count"] = int(np.count_nonzero(zero_nonzero))
        snapshots[timestamp] = fields
    assert reference_header is not None
    if not math.isclose(reference_header["cellsize"], float(spec["dx_m"]), rel_tol=0.0, abs_tol=1e-9):
        raise RunoffError("saved cell size differs from case specification")
    return snapshots, {
        "times_s": actual_times,
        "grid_shape": list(shape_expected),
        "valid_cells": int(shape_expected[0] * shape_expected[1]),
        "cellsize_m": reference_header["cellsize"],
        "nodata_value": reference_header["nodata_value"],
        "all_saved_h_finite_nonnegative": True,
        "all_saved_hUx_hUy_finite": True,
        "h_zero_with_nonzero_hU_by_time": {
            f"{timestamp:g}": fields["h_zero_nonzero_hU_count"]
            for timestamp, fields in snapshots.items()
        },
        "serialization": "native ASC writer uses %f, six fractional decimal places",
    }


def flat_metrics(spec: dict[str, Any], snapshots: dict[float, dict[str, Any]]) -> dict[str, Any]:
    cells = int(spec["rows"]) * int(spec["cols"])
    area = float(spec["dx_m"]) ** 2
    depth_tolerance = ASC_HALF_QUANTIZATION_M + 1e-9
    observations = []
    passed = True
    for timestamp, fields in snapshots.items():
        expected_depth = float(spec["rain_rate_m_per_s"]) * timestamp
        expected_volume = expected_depth * cells * area
        observed_volume = float(fields["h"].sum() * area)
        volume_residual = observed_volume - expected_volume
        volume_tolerance = 51200 * 1e-6 + max(1e-8, expected_volume * 1e-8)
        depth_error = float(np.max(np.abs(fields["h"] - expected_depth)))
        momentum_max = float(max(np.max(np.abs(fields["hUx"])), np.max(np.abs(fields["hUy"]))))
        at_pass = (
            depth_error <= depth_tolerance
            and abs(volume_residual) <= volume_tolerance
            and momentum_max == 0.0
            and fields["h_zero_nonzero_hU_count"] == 0
        )
        passed = passed and at_pass
        observations.append({
            "time_s": timestamp,
            "expected_depth_m": expected_depth,
            "maximum_depth_error_m": depth_error,
            "depth_tolerance_m": depth_tolerance,
            "expected_volume_m3": expected_volume,
            "observed_volume_m3": observed_volume,
            "volume_residual_m3": volume_residual,
            "volume_tolerance_m3": volume_tolerance,
            "maximum_absolute_momentum_m2_per_s": momentum_max,
            "gate_passed": at_pass,
        })
    return {
        "family": "closed flat excess rainfall",
        "status": "pass" if passed else "fail",
        "gate_passed": passed,
        "formula": "h=12 mm/h * t; V=h*102400 m^2; hUx=hUy=0",
        "observations": observations,
    }


def sheet_measure_mask(spec: dict[str, Any]) -> np.ndarray:
    rows, cols, dx = int(spec["rows"]), int(spec["cols"]), float(spec["dx_m"])
    row_centers = (np.arange(rows) + 0.5) * dx
    col_centers = (np.arange(cols) + 0.5) * dx
    if spec["orientation"] == "world-z-row-downhill":
        downhill = row_centers[:, None]
        cross = col_centers[None, :]
    else:
        downhill = col_centers[None, :]
        cross = row_centers[:, None]
    in_downhill = (downhill >= INTERIOR_X_M[0]) & (downhill <= INTERIOR_X_M[1])
    in_cross = (cross >= INTERIOR_Y_M[0]) & (cross <= INTERIOR_Y_M[1])
    return np.broadcast_to(in_downhill & in_cross, (rows, cols)).copy()


def sheet_metrics(spec: dict[str, Any], snapshots: dict[float, dict[str, Any]]) -> dict[str, Any]:
    measure = sheet_measure_mask(spec)
    if not measure.any():
        raise RunoffError("physical interior selects no cells at this resolution")
    area = float(spec["dx_m"]) ** 2
    cells = int(spec["rows"]) * int(spec["cols"])
    initial_volume = float(spec["initial_depth_m"]) * cells * area
    volume_tolerance = cells * area * ASC_HALF_QUANTIZATION_M + max(1e-8, initial_volume * 1e-8)
    slope = float(spec["slope"])
    reference_u = SHEET_H_M ** (2.0 / 3.0) * math.sqrt(slope) / SHEET_N
    orientation = spec["orientation"]
    normal_field, transverse_field = ("hUy", "hUx") if orientation == "world-z-row-downhill" else ("hUx", "hUy")
    observations = []
    max_relative_depth = 0.0
    max_relative_normal_u = 0.0
    max_transverse = 0.0
    max_abs_water_residual = 0.0
    volume_observations = []
    sign_ok = True
    wet_nonzero = 0
    saved_health = True
    for timestamp, fields in sorted(snapshots.items()):
        volume = float(fields["h"].sum() * area)
        residual = volume - initial_volume
        max_abs_water_residual = max(max_abs_water_residual, abs(residual))
        volume_observations.append({
            "time_s": timestamp,
            "saved_water_volume_m3": volume,
            "initial_water_volume_m3": initial_volume,
            "water_volume_residual_m3": residual,
            "water_volume_quantization_tolerance_m3": volume_tolerance,
            "gate_passed": abs(residual) <= volume_tolerance,
        })
        if timestamp not in SHEET_MEASURE_TIMES:
            continue
        depth = fields["h"][measure]
        normal_momentum = fields[normal_field][measure]
        transverse_momentum = fields[transverse_field][measure]
        undefined = (depth == 0) & ((normal_momentum != 0) | (transverse_momentum != 0))
        zero_wet = depth == 0
        wet_nonzero += int(np.count_nonzero(undefined))
        normal_u_native = np.full(depth.shape, np.nan, dtype=np.float64)
        transverse_u = np.full(depth.shape, np.nan, dtype=np.float64)
        wet = depth > 0
        normal_u_native[wet] = normal_momentum[wet] / depth[wet]
        transverse_u[wet] = transverse_momentum[wet] / depth[wet]
        normal_u_downhill = -normal_u_native if orientation == "world-z-row-downhill" else normal_u_native
        depth_error = np.abs(depth - SHEET_H_M) / SHEET_H_M
        normal_error = np.abs(normal_u_downhill - reference_u) / reference_u
        if undefined.any() or zero_wet.any():
            saved_health = False
        if orientation == "world-z-row-downhill":
            sign_ok = sign_ok and bool(np.all(normal_momentum[wet] < 0))
            mapped_worldz_u = -normal_u_native
        else:
            sign_ok = sign_ok and bool(np.all(normal_momentum[wet] > 0))
            mapped_worldz_u = None
        max_relative_depth = max(max_relative_depth, float(np.max(depth_error)))
        if np.isfinite(normal_error).all():
            max_relative_normal_u = max(max_relative_normal_u, float(np.max(normal_error)))
        else:
            saved_health = False
        if np.isfinite(transverse_u).all():
            max_transverse = max(max_transverse, float(np.max(np.abs(transverse_u))))
        else:
            saved_health = False
        max_abs_water_residual = max(max_abs_water_residual, abs(residual))
        observations.append({
            "time_s": timestamp,
            "interior_cell_count": int(measure.sum()),
            "maximum_relative_depth_error": float(np.max(depth_error)),
            "reference_normal_velocity_m_per_s": reference_u,
            "maximum_relative_normal_velocity_error": float(np.max(normal_error)) if np.isfinite(normal_error).all() else None,
            "maximum_absolute_transverse_velocity_m_per_s": float(np.max(np.abs(transverse_u))) if np.isfinite(transverse_u).all() else None,
            "saved_water_volume_m3": volume,
            "initial_water_volume_m3": initial_volume,
            "downhill_velocity_m_per_s": float(np.mean(normal_u_downhill)),
            "native_downhill_momentum_mean_m2_per_s": float(np.mean(normal_momentum)),
            "mapped_worldz_velocity_mean_m_per_s": float(np.mean(mapped_worldz_u)) if mapped_worldz_u is not None else None,
            "h_zero_nonzero_hU_count_in_measurement": int(np.count_nonzero(undefined)),
        })
    gate_passed = (
        saved_health
        and max_relative_depth <= 0.05
        and max_relative_normal_u <= 0.05
        and max_transverse <= 1e-3
        and max_abs_water_residual <= volume_tolerance
        and sign_ok
        and wet_nonzero == 0
    )
    return {
        "family": "thin uniform Manning sheet",
        "status": "pass" if gate_passed else "fail",
        "gate_passed": gate_passed,
        "diagnostic_only": bool(spec.get("diagnostic_only", False)),
        "interior_selection": {
            "cell_center_coordinates_m": {"downhill": list(INTERIOR_X_M), "transverse": list(INTERIOR_Y_M)},
            "orientation": orientation,
        },
        "reference": {
            "formula": "u=h^(2/3)*sqrt(S)/n using predeclared h=0.01m, independent of candidate output",
            "depth_m": SHEET_H_M,
            "manning_n": SHEET_N,
            "slope": slope,
            "normal_velocity_m_per_s": reference_u,
        },
        "maximum_relative_depth_error": max_relative_depth,
        "maximum_relative_normal_velocity_error": max_relative_normal_u,
        "maximum_absolute_transverse_velocity_m_per_s": max_transverse,
        "maximum_absolute_closed_water_residual_m3": max_abs_water_residual,
        "closed_water_quantization_tolerance_m3": volume_tolerance,
        "orientation_sign_gate_passed": sign_ok,
        "dry_zero_depth_nonzero_momentum_in_measured_cells": wet_nonzero,
        "observations": observations,
        "closed_volume_observations_all_saved_times": volume_observations,
    }


def inspect_source_identity() -> dict[str, Any]:
    setup = read_json(SETUP_JSON)
    wheel = SETUP_ROOT / setup["provenance"]["wheel"]["path"]
    extension = SETUP_ROOT / setup["provenance"]["loaded_native_extension"]["path"]
    sdist = SETUP_ROOT / setup["provenance"]["source_distribution"]["path"]
    actual = {
        "setup_json_sha256": sha256_file(SETUP_JSON),
        "wheel_sha256": sha256_file(wheel),
        "extension_sha256": sha256_file(extension),
        "sdist_sha256": sha256_file(sdist),
    }
    expected = {
        "wheel_sha256": setup["provenance"]["wheel"]["sha256"],
        "extension_sha256": setup["provenance"]["loaded_native_extension"]["sha256"],
        "sdist_sha256": setup["provenance"]["source_distribution"]["sha256"],
    }
    for name, digest in expected.items():
        if actual[name] != digest:
            raise RunoffError(f"setup artifact identity changed: {name}")
    return actual


def hardware_identity() -> dict[str, Any]:
    completed = subprocess.run(
        ["nvidia-smi", "--query-gpu=index,name,driver_version,memory.total", "--format=csv,noheader"],
        check=False, capture_output=True, text=True,
    )
    return {
        "command": ["nvidia-smi", "--query-gpu=index,name,driver_version,memory.total", "--format=csv,noheader"],
        "exit_code": completed.returncode,
        "stdout": completed.stdout.strip(),
        "stderr": completed.stderr.strip(),
    }


def _protocol(source_identity: dict[str, str]) -> dict[str, Any]:
    return {
        "schema": SCHEMA + ".protocol",
        "frozen_before_native_control_execution": True,
        "frozen_at_utc": datetime.now(timezone.utc).isoformat(),
        "contract_sha256": sha256_file(CONTRACT),
        "setup_identity": source_identity,
        "common": {
            "native_fields": ["h", "hUx", "hUy"],
            "ascii_writer_precision": "%f, six fractional decimal places",
            "export_half_quantization_depth_m": ASC_HALF_QUANTIZATION_M,
            "unobservable": ["applied face flux", "intermediate stage minima/clipping"],
            "saved_speed_semantics": "future speed is hU/h derived only from saved native fields; dry h with nonzero hU is counted and never assigned zero speed",
            "boundary": "native rigid perimeter; h zero-gradient and hU whole-vector odd ghost reflection in released code",
            "sinks": {"sewer_sink": 0.0, "cumulative_depth": 0.0, "hydraulic_conductivity": 0.0, "capillary_head": 0.0, "water_content_diff": 0.0},
        },
        "B_flat_rain": {
            "grid": [32, 32], "cell_size_m": 10.0, "datums_m": [0.0, 2000.0],
            "rain_mm_per_hour": 12.0, "duration_s": 600, "output_interval_s": 30,
            "manning_n": 0.05, "initial_h_m": 0.0, "initial_hUx_hUy_m2_per_s": [0.0, 0.0],
            "expected_h": "12 mm/h * t", "expected_volume": "rain_rate_m_per_s * t * 102400 m^2",
            "depth_tolerance_m": "0.5e-6 + 1e-9",
            "volume_tolerance_m3": "51200*1e-6 + max(1e-8, expected_rain_volume*1e-8)",
            "momentum_gate": "exactly zero in saved hUx and hUy",
            "datum_comparison": "record exact h/hUx/hUy equality and maximum absolute difference for all common outputs; pairwise serialization bound is 1e-6 + 2e-9 m for h; each datum independently uses its frozen analytic gates",
        },
        "C_thin_sheets": {
            "depth_m": SHEET_H_M, "manning_n": SHEET_N,
            "slopes_10m": list(SHEET_SLOPES), "slope_30m_diagnostic": 0.10,
            "rotated_worldz_slope": 0.10, "duration_s": 60, "output_interval_s": 5,
            "reference_velocity": "0.01^(2/3)*sqrt(S)/0.033, independent of candidate output",
            "interior_cell_center_coordinates_m": {"downhill": list(INTERIOR_X_M), "transverse": list(INTERIOR_Y_M)},
            "depth_relative_error_max": 0.05, "normal_velocity_relative_error_max": 0.05,
            "transverse_velocity_max_m_per_s": 1e-3,
            "closed_volume_tolerance_m3": "valid_cells*dx_m^2*0.5e-6 + max(1e-8, initial_volume*1e-8); initial h=0.01 is exactly represented at six decimals",
            "rotated_orientation_gate": "in measured world-Z-downhill interior all wet native hUy<0 and mapped worldZ u=-hUy/h>0",
            "closed_boundary": "native rigid outline; no sources and all sinks explicitly zero",
        },
        "case_names": [spec["name"] for spec in control_specs()],
    }


def _mountain_protocol(
    source_identity: dict[str, str],
    fixture_identity: dict[str, Any],
    depression_identity: dict[str, Any],
    harness_provenance: dict[str, Any],
    spec: dict[str, Any],
) -> dict[str, Any]:
    return {
        "schema": SCHEMA + ".mountain-probe-protocol",
        "frozen_before_native_execution": True,
        "frozen_at_utc": datetime.now(timezone.utc).isoformat(),
        "contract_sha256": sha256_file(CONTRACT),
        "native_backend_identity": source_identity,
        "harness_source_identity": harness_provenance,
        "terrain_source_identity": fixture_identity,
        "depression_mask_identity": depression_identity,
        "case": spec,
        "settings": {
            "only_probe_executed_at_this_checkpoint": True,
            "grid_rows_cols": [int(spec["rows"]), int(spec["cols"])],
            "cell_size_m": float(spec["dx_m"]),
            "physical_extent_m": [15360, 15360],
            "initial_h_hUx_hUy": [0.0, 0.0, 0.0],
            "rainfall": {
                "mask": 0,
                "rate_mm_per_hour": 12.0,
                "constant_history_start_s": 0.0,
                "constant_history_end_s": float(spec["rain_history_end_s"]),
                "probe_duration_s": float(spec["duration_s"]),
            },
            "manning_n": MOUNTAIN_MANNING_N,
            "native_boundary": {
                "api": "InputModel.set_boundary_condition([], outline_boundary='fall')",
                "serialized_h_and_hU_type_secondary_tertiary": [3, 0, 0],
                "semantics": "fixed dry-zero h and hU exterior values on every native perimeter",
            },
            "all_sinks_and_other_terms": {
                "sewer_sink": 0.0,
                "cumulative_depth": 0.0,
                "hydraulic_conductivity": 0.0,
                "capillary_head": 0.0,
                "water_content_diff": 0.0,
                "interior_sources_drains": 0.0,
            },
            "row_orientation": fixture_identity["row_orientation"],
            "native_output_fields": ["h", "hUx", "hUy"],
            "saved_output_times_s_expected": time_grid(spec),
            "export_writer_precision": "%f, six fractional decimal places",
            "unsupported_observability": [
                "native applied face flux",
                "intermediate-stage positivity/clipping history",
                "independently exported native velocity",
                "exact pre-serialization maximum speed",
            ],
        },
        "acceptance_scope": "300s operational/input/output-health and timing probe only; no corridor/pond temporal-support gate, full mountain validation, or accuracy gate is decided here",
    }


def _copy_harness_sources(phase_root: Path) -> dict[str, Any]:
    source_paths = {
        "runner": Path(__file__).resolve(),
        "tests": Path(__file__).resolve().with_name("test_run_native_runoff_reuse_v1.py"),
    }
    harness_dir = phase_root / "harness"
    harness_dir.mkdir()
    identities = {}
    for name, source in source_paths.items():
        before = sha256_file(source)
        copied = harness_dir / source.name
        shutil.copyfile(source, copied)
        after = sha256_file(source)
        copied_hash = sha256_file(copied)
        if before != after or before != copied_hash:
            raise RunoffError(f"harness source changed during provenance copy: {source}")
        identities[name] = {
            "repository_source_path": str(source),
            "repository_source_sha256_before_copy": before,
            "repository_source_sha256_after_copy": after,
            "archived_copy_path": str(copied.resolve()),
            "archived_copy_sha256": copied_hash,
        }
    record = {"copies": identities, "verified_identical_before_native_execution": True}
    write_json(phase_root / "harness-provenance.json", record)
    return record


def _field_output_hashes(output_dir: Path) -> dict[str, str]:
    return {
        path.name: sha256_file(path)
        for path in sorted(output_dir.iterdir())
        if path.is_file()
    }


def _parse_child_marker(text: str, prefix: str) -> Any:
    for line in text.splitlines():
        if line.startswith(prefix):
            value = line[len(prefix):]
            return json.loads(value) if prefix.endswith("=") and value.startswith("{") else value
    return None


def run_one_case(
    spec: dict[str, Any],
    case_dir: Path,
    timeout_s: float = 600.0,
    *,
    input_dem: np.ndarray | None = None,
    mountain_context: dict[str, Any] | None = None,
    harness_provenance: dict[str, Any] | None = None,
) -> dict[str, Any]:
    case_started = time.perf_counter()
    case_dir = create_fresh_leaf(STUDY_ROOT, case_dir)
    bed_path = case_dir / "DEM.asc"
    bed_values = bed_grid(spec) if input_dem is None else np.asarray(input_dem, dtype=np.float64)
    if bed_values.shape != (int(spec["rows"]), int(spec["cols"])):
        raise RunoffError("supplied case DEM shape differs from the frozen case grid")
    write_ascii(bed_path, bed_values, float(spec["dx_m"]))
    write_json(case_dir / "case-spec.json", spec)
    command = [sys.executable, "-u", str(Path(__file__).resolve()), "--child", str(case_dir)]
    case_harness_provenance = harness_provenance
    case_protocol_path: Path | None = None
    if mountain_context is not None:
        case_harness_provenance = _copy_harness_sources(case_dir)
        source_identity = inspect_source_identity()
        source_bed_for_protocol = np.asarray(mountain_context["source_bed"], dtype=np.float64)
        if source_bed_for_protocol.shape != bed_values.shape:
            raise RunoffError("case source-bed reference shape differs from the native raster")
        case_protocol = {
            "schema": SCHEMA + ".mountain-case-protocol",
            "frozen_before_native_execution": True,
            "frozen_at_utc": datetime.now(timezone.utc).isoformat(),
            "contract_sha256": sha256_file(CONTRACT),
            "native_backend_identity": source_identity,
            "harness_source_identity": case_harness_provenance,
            "terrain_source_identity": mountain_context.get("terrain_identity"),
            "depression_mask_identity": mountain_context.get("depression_identity"),
            "case": spec,
            "input_dem_ascii_sha256": sha256_file(bed_path),
            "input_dem_source_bed_semantics": mountain_context["source_bed_semantics"],
            "input_dem_source_bed_float64_sha256": sha256_bytes(source_bed_for_protocol.astype("<f8").tobytes(order="C")),
            "input_dem_ascii_vs_source_bed_error": _difference_stats(read_ascii(bed_path)[1], source_bed_for_protocol),
            "expected_saved_times_s": time_grid(spec),
            "rain_source_schedule": [[0.0, float(spec["rain_rate_m_per_s"])],
                                     [float(spec["rain_history_end_s"]), float(spec["rain_rate_m_per_s"])]],
            "serialized_perimeter": {"boundary": spec["boundary"], "h_type": [3, 0, 0], "hU_type": [3, 0, 0]},
            "manning_n": float(spec["manning_n"]),
            "all_sinks_zero": True,
            "command": command,
        }
        case_protocol_path = case_dir / "case-protocol.json"
        write_json(case_protocol_path, case_protocol)
    env = os.environ.copy()
    local_cache = SETUP_ROOT / "cache"
    for subdir in ("matplotlib", "xdg", "pip", "uv"):
        (local_cache / subdir).mkdir(parents=True, exist_ok=True)
    (SETUP_ROOT / "python" / "bin").mkdir(parents=True, exist_ok=True)
    env.update({
        "MPLBACKEND": "Agg",
        "MPLCONFIGDIR": str(local_cache / "matplotlib"),
        "XDG_CACHE_HOME": str(local_cache / "xdg"),
        "PIP_CACHE_DIR": str(local_cache / "pip"),
        "UV_CACHE_DIR": str(local_cache / "uv"),
        "UV_PYTHON_BIN_DIR": str(SETUP_ROOT / "python" / "bin"),
        "PYTHONDONTWRITEBYTECODE": "1",
    })
    stdout_path, stderr_path = case_dir / "native.stdout", case_dir / "native.stderr"
    child_started = time.perf_counter()
    exit_code: int | None = None
    timeout = False
    try:
        with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
            result = subprocess.run(command, cwd=case_dir, env=env, stdout=stdout, stderr=stderr,
                                    check=False, timeout=timeout_s)
        exit_code = result.returncode
    except subprocess.TimeoutExpired:
        timeout = True
    child_wall = time.perf_counter() - child_started
    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace") if stdout_path.exists() else ""
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace") if stderr_path.exists() else ""
    output_dir = case_dir / "native" / "output"
    input_dir = case_dir / "native" / "input"
    before_hashes = _parse_child_marker(stdout_text, "RUNNER_INPUT_HASHES_BEFORE_RUN=")
    native_input_audit = _parse_child_marker(stdout_text, "RUNNER_NATIVE_INPUT_AUDIT=")
    after_hashes = _input_hashes(input_dir) if input_dir.is_dir() else {}
    inputs_unchanged = before_hashes == after_hashes and bool(after_hashes)
    result_record: dict[str, Any] = {
        "schema": SCHEMA + ".case-result",
        "case": spec,
        "command": command,
        "working_directory": str(case_dir.resolve()),
        "process_exit_code": exit_code,
        "process_timeout": timeout,
        "child_process_wall_seconds_including_imports": child_wall,
        "body_seconds_after_package_imports": _parse_child_marker(stdout_text, "RUNNER_MODEL_BODY_SECONDS="),
        "flood_run_wall_seconds_after_imports_and_input_write": _parse_child_marker(stdout_text, "RUNNER_FLOOD_RUN_WALL_SECONDS="),
        "python": _parse_child_marker(stdout_text, "RUNNER_PYTHON="),
        "synxflow_version": _parse_child_marker(stdout_text, "RUNNER_SYNXFLOW_VERSION="),
        "native_gpu_success_marker": "Simulation successfully finished!" in stdout_text,
        "native_cuda_event_runtime_ms": None,
        "raw_stdout": str(stdout_path.resolve()),
        "raw_stderr": str(stderr_path.resolve()),
        "source_identity": inspect_source_identity(),
        "case_protocol_path": str(case_protocol_path.resolve()) if case_protocol_path is not None else None,
        "input_provenance": {
            "case_dem_ascii_sha256": sha256_file(bed_path),
            "dem_input_ascii_max_abs_error_m": float(np.max(np.abs(read_ascii(bed_path)[1] - bed_values))),
            "native_input_hashes_before_run": before_hashes,
            "native_input_hashes_after_run": after_hashes,
            "native_inputs_unchanged_during_run": inputs_unchanged,
            "native_input_audit_before_solver": native_input_audit,
        },
        "harness_provenance": case_harness_provenance,
        "output_hashes": _field_output_hashes(output_dir) if output_dir.is_dir() else {},
        "status": "timeout" if timeout else "process_failed",
    }
    output_validation_started = time.perf_counter()
    native_times = re.findall(r"Total runtime\s+([0-9.]+)ms", stdout_text)
    if native_times:
        result_record["native_cuda_event_runtime_ms"] = float(native_times[-1])
    if timeout or exit_code != 0 or not result_record["native_gpu_success_marker"]:
        result_record["error"] = "native child did not complete successfully; see raw streams"
    else:
        try:
            snapshots, health = read_case_snapshots(output_dir, spec)
            timestep = parse_timestep_log(output_dir / "timestep_log.txt", float(spec["duration_s"]))
            result_record["output_health"] = health
            result_record["timestep_log"] = timestep
            if not inputs_unchanged:
                raise RunoffError("generated native inputs changed during native run")
            if spec["family"] == "flat-rain":
                metrics = flat_metrics(spec, snapshots)
            elif spec["family"] == "thin-sheet":
                metrics = sheet_metrics(spec, snapshots)
            elif spec["family"] == "mountain-rain" and mountain_context is not None:
                native_bed, native_bed_f32 = _grid_from_native_ids(input_dir / "field" / "z.dat", int(spec["rows"]), int(spec["cols"]))
                case_source_bed = np.asarray(mountain_context["source_bed"], dtype=np.float64)
                if case_source_bed.shape != bed_values.shape:
                    raise RunoffError("case source-bed reference shape differs from the native raster")
                result_record["input_provenance"]["native_z_field_audit"] = native_input_audit
                result_record["input_provenance"]["native_z_serialized_grid_sha256"] = sha256_bytes(native_bed.astype("<f8").tobytes(order="C"))
                result_record["input_provenance"]["native_z_float32_grid_sha256"] = sha256_bytes(native_bed_f32.tobytes(order="C"))
                dem_ascii_values = read_ascii(bed_path)[1]
                result_record["input_provenance"]["case_source_bed_semantics"] = mountain_context["source_bed_semantics"]
                result_record["input_provenance"]["case_source_bed_reference_float64_sha256"] = sha256_bytes(case_source_bed.astype("<f8").tobytes(order="C"))
                result_record["input_provenance"]["dem_ascii_vs_case_source_bed_error"] = _difference_stats(dem_ascii_values, case_source_bed)
                result_record["input_provenance"]["serialized_native_z_vs_9_decimal_dem_ascii_error"] = _difference_stats(native_bed, dem_ascii_values)
                result_record["input_provenance"]["native_float32_z_vs_9_decimal_dem_ascii_error"] = _difference_stats(native_bed_f32, dem_ascii_values)
                result_record["input_provenance"]["serialized_native_z_vs_case_source_bed_error"] = _difference_stats(native_bed, case_source_bed)
                result_record["input_provenance"]["native_float32_z_vs_case_source_bed_error"] = _difference_stats(native_bed_f32, case_source_bed)
                if spec["dx_m"] == MOUNTAIN_DX_M and mountain_context.get("immutable_point_crop") is not None:
                    original_points = np.asarray(mountain_context["immutable_point_crop"], dtype=np.float64)
                    result_record["input_provenance"]["serialized_native_z_vs_immutable_transformed_point_crop_error"] = _difference_stats(native_bed, original_points)
                    result_record["input_provenance"]["native_float32_z_vs_immutable_transformed_point_crop_error"] = _difference_stats(native_bed_f32, original_points)
                metrics = mountain_metrics(
                    spec, snapshots, mountain_context["source_bed"], native_bed_f32,
                    mountain_context["roi_masks"], mountain_context["depression_masks"],
                )
            else:
                raise RunoffError(f"no metric implementation for case family {spec['family']}")
            result_record["metrics"] = metrics
            if spec["family"] == "mountain-rain":
                result_record["status"] = metrics["status"]
            else:
                result_record["status"] = "pass" if metrics["gate_passed"] else "gate_failed"
        except Exception as error:
            result_record["status"] = "output_validation_failed"
            result_record["error"] = f"{type(error).__name__}: {error}"
    result_record["output_validation_wall_seconds"] = time.perf_counter() - output_validation_started
    result_record["case_end_to_end_wall_seconds_including_dem_write_and_output_checks"] = time.perf_counter() - case_started
    result_record["case_end_to_end_timing_scope"] = (
        "captured after output parsing, health gates, and metrics, immediately before case-result JSON serialization; "
        "includes DEM/input generation, child imports/native exports, and output hashing/checks"
    )
    write_json(case_dir / "case-result.json", result_record)
    return result_record


def compare_flat_datums(results: list[dict[str, Any]]) -> dict[str, Any]:
    by_name = {record["case"]["name"]: record for record in results}
    low, high = by_name.get("flat-rain-bed-0m"), by_name.get("flat-rain-bed-2000m")
    if low is None or high is None or low["status"] not in ("pass", "gate_failed") or high["status"] not in ("pass", "gate_failed"):
        return {"available": False, "reason": "one or both flat case outputs unavailable"}
    low_dir = Path(low["working_directory"]) / "native/output"
    high_dir = Path(high["working_directory"]) / "native/output"
    low_paths, high_paths = snapshot_paths(low_dir), snapshot_paths(high_dir)
    common = sorted(set(low_paths["h"]) & set(high_paths["h"]))
    exact = True
    max_depth_delta = max_momentum_delta = 0.0
    for timestamp in common:
        for field in ("h", "hUx", "hUy"):
            _, a, _ = read_ascii(low_paths[field][timestamp])
            _, b, _ = read_ascii(high_paths[field][timestamp])
            exact = exact and bool(np.array_equal(a, b))
            delta = float(np.max(np.abs(a - b)))
            if field == "h":
                max_depth_delta = max(max_depth_delta, delta)
            else:
                max_momentum_delta = max(max_momentum_delta, delta)
    return {
        "available": True,
        "common_saved_times_s": common,
        "h_hUx_hUy_exact_array_equality": exact,
        "maximum_absolute_depth_difference_m": max_depth_delta,
        "maximum_absolute_momentum_component_difference_m2_per_s": max_momentum_delta,
        "pairwise_export_depth_bound_m": 1e-6 + 2e-9,
        "pairwise_depth_within_bound": max_depth_delta <= 1e-6 + 2e-9,
        "comparison_is_diagnostic_not_a_replacement_for_each_case_analytic_gate": True,
    }


def aggregate_2x2_area_mean(values: np.ndarray) -> np.ndarray:
    """Area-average a 1024x1024 fine-grid field onto the 30m grid."""
    array = np.asarray(values, dtype=np.float64)
    if array.shape != (1024, 1024) or not np.isfinite(array).all():
        raise RunoffError("2x area aggregation requires one finite 1024x1024 field")
    return array.reshape(512, 2, 512, 2).mean(axis=(1, 3), dtype=np.float64)


def _field_at(output_dir: Path, field: str, timestamp: float) -> np.ndarray:
    paths = snapshot_paths(output_dir)
    if timestamp not in paths[field]:
        raise RunoffError(f"missing saved {field}@{timestamp:g}s in {output_dir}")
    return read_ascii(paths[field][timestamp])[1]


def _field_difference_summary(actual: np.ndarray, expected: np.ndarray, area_m2: float | None = None) -> dict[str, Any]:
    delta = np.asarray(actual, dtype=np.float64) - np.asarray(expected, dtype=np.float64)
    if delta.shape != np.asarray(expected).shape or not np.isfinite(delta).all():
        raise RunoffError("refinement comparison has a shape mismatch or non-finite value")
    result: dict[str, Any] = {
        "maximum_absolute_difference": float(np.max(np.abs(delta))),
        "rms_difference": float(np.sqrt(np.mean(delta * delta))),
        "mean_signed_difference": float(np.mean(delta)),
        "mean_absolute_difference": float(np.mean(np.abs(delta))),
    }
    if area_m2 is not None:
        result["absolute_difference_area_integral"] = float(np.abs(delta).sum(dtype=np.float64) * area_m2)
    return result


def compare_mountain_replay(full_record: dict[str, Any], replay_record: dict[str, Any]) -> dict[str, Any]:
    """Byte and numeric equality for every shared exported h/hUx/hUy field."""
    full_output = Path(full_record["working_directory"]) / "native" / "output"
    replay_output = Path(replay_record["working_directory"]) / "native" / "output"
    full_paths, replay_paths = snapshot_paths(full_output), snapshot_paths(replay_output)
    full_times, replay_times = sorted(full_paths["h"]), sorted(replay_paths["h"])
    if not set(replay_times).issubset(full_times):
        raise RunoffError("replay exported times are not a subset of full-run times")
    time_records = []
    exact_all = replay_record.get("status") == "healthy" and full_record.get("status") == "healthy"
    first_mismatch = None
    for timestamp in replay_times:
        field_records = {}
        for field in ("h", "hUx", "hUy"):
            full_path, replay_path = full_paths[field][timestamp], replay_paths[field][timestamp]
            full_array, replay_array = read_ascii(full_path)[1], read_ascii(replay_path)[1]
            array_equal = bool(np.array_equal(full_array, replay_array))
            full_sha, replay_sha = sha256_file(full_path), sha256_file(replay_path)
            byte_equal = full_sha == replay_sha and full_path.read_bytes() == replay_path.read_bytes()
            field_records[field] = {
                "exact_array_equality": array_equal,
                "exact_file_byte_equality": byte_equal,
                "full_sha256": full_sha,
                "replay_sha256": replay_sha,
                "maximum_absolute_difference": float(np.max(np.abs(full_array - replay_array))),
            }
            if not array_equal or not byte_equal:
                exact_all = False
                first_mismatch = first_mismatch or {"time_s": timestamp, "field": field}
        time_records.append({"time_s": timestamp, "fields": field_records})
    return {
        "shared_times_s": replay_times,
        "full_export_times_s": full_times,
        "replay_export_times_s": replay_times,
        "all_h_hUx_hUy_exact_array_and_file_equality": exact_all,
        "first_mismatch": first_mismatch,
        "compared_export_count": len(replay_times) * 3,
        "per_time_and_field": time_records,
        "comparison_semantics": "fresh30m600s replay with matched60s exports and identical constant rainfall history; compares raw saved h/hUx/hUy bytes and parsed arrays",
    }


def compare_mountain_refinement(
    coarse_record: dict[str, Any],
    fine_record: dict[str, Any],
    coarse_source_bed: np.ndarray,
    fine_source_bed: np.ndarray,
) -> dict[str, Any]:
    """Compare 15m exports after conservative 2x2 area-mean aggregation."""
    timestamp = MOUNTAIN_REPLAY_DURATION_S
    coarse_output = Path(coarse_record["working_directory"]) / "native" / "output"
    fine_output = Path(fine_record["working_directory"]) / "native" / "output"
    coarse_fields = {field: _field_at(coarse_output, field, timestamp) for field in ("h", "hUx", "hUy")}
    fine_fields = {field: _field_at(fine_output, field, timestamp) for field in ("h", "hUx", "hUy")}
    if coarse_fields["h"].shape != (512, 512) or fine_fields["h"].shape != (1024, 1024):
        raise RunoffError("coarse/refined native output shapes differ from frozen full physical extent")
    fine_area_means = {field: aggregate_2x2_area_mean(fine_fields[field]) for field in ("h", "hUx", "hUy")}
    field_comparisons = {
        field: _field_difference_summary(fine_area_means[field], coarse_fields[field], MOUNTAIN_DX_M ** 2)
        for field in ("h", "hUx", "hUy")
    }
    coarse_area = MOUNTAIN_DX_M ** 2
    fine_area = (MOUNTAIN_DX_M / 2.0) ** 2
    coarse_volume = float(coarse_fields["h"].sum(dtype=np.float64) * coarse_area)
    fine_volume = float(fine_fields["h"].sum(dtype=np.float64) * fine_area)
    volume_difference = fine_volume - coarse_volume
    storage_quantization = (512 * 512 * coarse_area + 1024 * 1024 * fine_area) * ASC_HALF_QUANTIZATION_M
    coarse_native_bed = _grid_from_native_ids(Path(coarse_record["working_directory"]) / "native" / "input" / "field" / "z.dat", 512, 512)[1].astype(np.float64)
    fine_native_bed = _grid_from_native_ids(Path(fine_record["working_directory"]) / "native" / "input" / "field" / "z.dat", 1024, 1024)[1].astype(np.float64)
    source_fine_area_mean_bed = aggregate_2x2_area_mean(fine_source_bed)
    native_fine_area_mean_bed = aggregate_2x2_area_mean(fine_native_bed)
    coarse_metrics = coarse_record["metrics"]
    fine_metrics = fine_record["metrics"]
    coarse_at_time = next(item for item in coarse_metrics["global_observations"] if item["time_s"] == timestamp)
    fine_at_time = next(item for item in fine_metrics["global_observations"] if item["time_s"] == timestamp)
    coarse_corridor = next(item for item in coarse_metrics["moving_concentrated_corridor_observations"] if item["time_s"] == timestamp)
    fine_corridor = next(item for item in fine_metrics["moving_concentrated_corridor_observations"] if item["time_s"] == timestamp)

    def _record_at(container: dict[str, list[dict[str, Any]]], key: str) -> dict[str, Any]:
        return next(item for item in container[key] if item["time_s"] == timestamp)

    coarse_collection = _record_at(coarse_metrics["fixed_roi_observations"], "collection")
    fine_collection = _record_at(fine_metrics["fixed_roi_observations"], "collection")
    coarse_depressions = {name: _record_at(coarse_metrics["fixed_depression_net_rain_observations"], name)
                          for name in coarse_metrics["fixed_depression_net_rain_observations"]}
    fine_depressions = {name: _record_at(fine_metrics["fixed_depression_net_rain_observations"], name)
                        for name in fine_metrics["fixed_depression_net_rain_observations"]}
    depression_comparisons = {}
    for name in coarse_depressions:
        coarse_obs, fine_obs = coarse_depressions[name], fine_depressions[name]
        if not math.isclose(coarse_obs["physical_mask_area_m2"], fine_obs["physical_mask_area_m2"], rel_tol=0.0, abs_tol=1e-8):
            raise RunoffError(f"refined depression mask does not preserve physical area: {name}")
        depression_comparisons[name] = {
            "coarse_30m_observation": coarse_obs,
            "fine_15m_observation": fine_obs,
            "water_volume_difference_fine_minus_coarse_m3": fine_obs["water_volume_m3"] - coarse_obs["water_volume_m3"],
            "net_lateral_storage_difference_fine_minus_coarse_m3": fine_obs["net_lateral_storage_m3"] - coarse_obs["net_lateral_storage_m3"],
            "shared_physical_mask_area_m2": coarse_obs["physical_mask_area_m2"],
        }
    return {
        "comparison_time_s": timestamp,
        "coarsening_method": "2x2 arithmetic area means independently for saved h, hUx and hUy; do not average or coarsen velocity",
        "native_fine_grid_extremes_retained_separately": {
            "maximum_depth_m": fine_at_time["saved_depth_max_m"],
            "maximum_export_derived_speed_m_per_s": fine_at_time["maximum_export_derived_speed_m_per_s"],
            "all_wet_speed_p50_p90_p99_m_per_s": fine_at_time["all_wet_export_derived_speed_p50_p90_p99_m_per_s"],
            "maximum_hUx_m2_per_s": float(np.max(np.abs(fine_fields["hUx"]))),
            "maximum_hUy_m2_per_s": float(np.max(np.abs(fine_fields["hUy"]))),
        },
        "area_mean_h_hUx_hUy_against_native_30m_fields": field_comparisons,
        "depth_L1_area_integral_m3": field_comparisons["h"]["absolute_difference_area_integral"],
        "global_storage": {
            "coarse_30m_volume_m3": coarse_volume,
            "fine_15m_volume_m3": fine_volume,
            "fine_minus_coarse_m3": volume_difference,
            "combined_ASC_quantization_half_interval_m3": [-storage_quantization, storage_quantization],
            "difference_within_combined_export_quantization": abs(volume_difference) <= storage_quantization,
        },
        "moving_concentrated_volume_and_components": {
            "coarse_30m": coarse_corridor,
            "fine_15m": fine_corridor,
            "fine_minus_coarse_moving_concentrated_volume_m3": fine_corridor["moving_concentrated_water_volume_m3"] - coarse_corridor["moving_concentrated_water_volume_m3"],
            "fine_minus_coarse_material_water_volume_m3": fine_at_time["material_h_ge_0.01_water_volume_m3"] - coarse_at_time["material_h_ge_0.01_water_volume_m3"],
        },
        "collection_roi": {
            "coarse_30m": coarse_collection,
            "fine_15m": fine_collection,
            "fine_minus_coarse_net_lateral_storage_m3": fine_collection["net_lateral_storage_m3"] - coarse_collection["net_lateral_storage_m3"],
        },
        "fixed_depression_storage": depression_comparisons,
        "point_vs_area_mean_bed": {
            "input_triangle_sample_area_mean_vs_immutable_30m_point_samples": _field_difference_summary(source_fine_area_mean_bed, coarse_source_bed),
            "native_float32_fine_z_area_mean_vs_native_float32_30m_z_point_samples": _field_difference_summary(native_fine_area_mean_bed, coarse_native_bed),
            "native_bed_values": "both levels use each case z.dat restored by released IDs and cast to the separately audited float32 Scalar representation",
        },
        "interpretation": "15m/600s resolution sensitivity at a shared exported time; not long-horizon convergence or calibrated hydrology",
    }


def run_controls(output_path: Path, timeout_s: float = 600.0) -> dict[str, Any]:
    phase_root = create_fresh_leaf(STUDY_ROOT, output_path)
    source_identity = inspect_source_identity()
    protocol = _protocol(source_identity)
    write_json(phase_root / "protocol.json", protocol)
    hardware = hardware_identity()
    write_json(phase_root / "hardware.json", hardware)
    results = []
    for spec in control_specs():
        results.append(run_one_case(spec, phase_root / spec["name"], timeout_s=timeout_s))
    datum_comparison = compare_flat_datums(results)
    gates_passed = all(record["status"] == "pass" for record in results)
    summary = {
        "schema": SCHEMA + ".phase-summary",
        "phase": "B/C controls only",
        "status": "pass" if gates_passed else "fail",
        "all_predeclared_gates_passed": gates_passed,
        "protocol_path": str((phase_root / "protocol.json").resolve()),
        "contract_sha256": protocol["contract_sha256"],
        "hardware": hardware,
        "source_identity": source_identity,
        "flat_datum_comparison": datum_comparison,
        "cases": results,
        "custom_mountain_case_executed": False,
        "native_applied_face_flux_observable": False,
        "intermediate_stage_positivity_observable": False,
    }
    write_json(phase_root / "phase-summary.json", summary)
    return summary


def run_mountain_probe(output_path: Path, timeout_s: float = 600.0) -> dict[str, Any]:
    """Run only the frozen 30m/300s mountain timing and output-health probe."""
    phase_root = create_fresh_leaf(STUDY_ROOT, output_path)
    setup = read_json(SETUP_JSON)
    expected_python = (SETUP_ROOT / setup["provenance"]["python_runtime"]["interpreter"]).resolve()
    actual_python = Path(sys.executable).resolve()
    if actual_python != expected_python:
        raise RunoffError(f"mountain probe must use isolated setup interpreter {expected_python}, got {actual_python}")
    source_identity = inspect_source_identity()
    fixture, source_bed, source_halo, fixture_identity = mountain_source_arrays()
    roi_masks = fixed_roi_masks()
    depression_masks, depression_identity = fixed_depression_masks()
    harness_provenance = _copy_harness_sources(phase_root)
    protocol = _mountain_protocol(source_identity, fixture_identity, depression_identity,
                                  harness_provenance, mountain_case_specs()["probe"])
    protocol["setup_environment"] = setup.get("environment")
    protocol["python_runtime"] = setup.get("provenance", {}).get("python_runtime")
    protocol["setup_status"] = setup.get("setup_status")
    protocol["stock_smoke_scope"] = setup.get("stock_smoke", {}).get("quickstart_vs_documented_tutorial")
    write_json(phase_root / "phase-protocol.json", protocol)
    hardware = hardware_identity()
    write_json(phase_root / "hardware.json", hardware)
    case_source_sha = harness_provenance["copies"]["runner"]["repository_source_sha256_before_copy"]
    if sha256_file(Path(__file__).resolve()) != case_source_sha:
        raise RunoffError("runner source changed after it was frozen into probe provenance")
    spec = mountain_case_specs()["probe"]
    case_record = run_one_case(
        spec,
        phase_root / spec["name"],
        timeout_s=timeout_s,
        input_dem=source_bed,
        mountain_context={
            "source_bed": source_bed,
            "source_bed_semantics": "immutable transformed 30m point crop, float32 raw+offset then scale; source row order preserved",
            "immutable_point_crop": source_bed,
            "source_halo_sha256": fixture_identity["transformed_halo_sha256"],
            "roi_masks": roi_masks,
            "depression_masks": depression_masks,
        },
        harness_provenance=harness_provenance,
    )
    end_to_end = case_record.get("case_end_to_end_wall_seconds_including_dem_write_and_output_checks")
    flood_call = case_record.get("flood_run_wall_seconds_after_imports_and_input_write")
    simulated_seconds = float(spec["duration_s"])
    projection = (float(end_to_end) * float(MOUNTAIN_FULL_DURATION_S) / simulated_seconds
                  if end_to_end is not None else None)
    flood_projection = (float(flood_call) * float(MOUNTAIN_FULL_DURATION_S) / simulated_seconds
                        if flood_call is not None else None)
    native_cuda_ms = case_record.get("native_cuda_event_runtime_ms")
    summary = {
        "schema": SCHEMA + ".mountain-probe-summary",
        "phase": "mountain 30m/300s/30s-export timing and operational output-health probe only",
        "status": "healthy" if case_record["status"] == "healthy" else case_record["status"],
        "probe_only_checkpoint": True,
        "full_7200s_executed": False,
        "replay_executed": False,
        "refinement_executed": False,
        "phase_protocol_path": str((phase_root / "phase-protocol.json").resolve()),
        "contract_sha256": protocol["contract_sha256"],
        "setup_source_identity": source_identity,
        "harness_provenance": harness_provenance,
        "terrain_source_identity": fixture_identity,
        "depression_mask_identity": depression_identity,
        "hardware": hardware,
        "case_result_path": str((phase_root / spec["name"] / "case-result.json").resolve()),
        "native_stdout_path": case_record["raw_stdout"],
        "native_stderr_path": case_record["raw_stderr"],
        "actual_saved_times_s": case_record.get("output_health", {}).get("times_s"),
        "output_health": case_record.get("output_health"),
        "native_input_provenance": case_record.get("input_provenance"),
        "native_timestep_log": case_record.get("timestep_log"),
        "timing": {
            "case_end_to_end_wall_seconds_including_dem_write_and_output_checks": end_to_end,
            "case_timer_scope": case_record.get("case_end_to_end_timing_scope"),
            "child_process_wall_seconds_including_imports": case_record.get("child_process_wall_seconds_including_imports"),
            "body_seconds_after_package_imports_including_input_write": case_record.get("body_seconds_after_package_imports"),
            "flood_run_wall_seconds_after_imports_and_input_write": flood_call,
            "native_cuda_event_runtime_ms": native_cuda_ms,
            "probe_physical_seconds": simulated_seconds,
            "end_to_end_physical_seconds_per_wall_second": (simulated_seconds / end_to_end if end_to_end else None),
            "projection_7200s_linear_end_to_end_wall_seconds": projection,
            "projection_7200s_linear_flood_call_wall_seconds": flood_projection,
            "projection_7200s_within_contract_30min_wall_cap": projection is not None and projection <= 1800.0,
            "projection_7200s_meets_8x_end_to_end_target": projection is not None and projection <= 900.0,
            "projection_caveat": "simple probe wall ratio; full-run startup/export overhead and changing adaptive dt are not modeled; projection is a review input, not authorization to run the full case",
        },
        "case": case_record,
        "unsupported_observability": [
            "native applied face flux",
            "intermediate-stage positivity/clipping history",
            "independently exported native velocity",
            "exact pre-serialization maximum speed",
        ],
        "scope_note": "stock Native SynxFlow wheel is used without numerical/source changes; a healthy probe is not mountain accuracy validation",
    }
    write_json(phase_root / "phase-summary.json", summary)
    return summary


def run_mountain_followup(output_path: Path) -> dict[str, Any]:
    """Run the authorized full, matched replay, and short 15m cases sequentially."""
    phase_root = create_fresh_leaf(STUDY_ROOT, output_path)
    setup = read_json(SETUP_JSON)
    expected_python = (SETUP_ROOT / setup["provenance"]["python_runtime"]["interpreter"]).resolve()
    actual_python = Path(sys.executable).resolve()
    if actual_python != expected_python:
        raise RunoffError(f"mountain followup must use isolated setup interpreter {expected_python}, got {actual_python}")
    source_identity = inspect_source_identity()
    _, coarse_source_bed, source_halo, fixture_identity = mountain_source_arrays()
    fine_source_bed = piecewise_linear_refine_2x(source_halo)
    if fine_source_bed.shape != (1024, 1024) or fine_source_bed.dtype != np.dtype("<f8"):
        raise RunoffError("frozen 15m triangle construction did not preserve the expected float64 refinement")
    roi_masks = fixed_roi_masks()
    depression_masks, depression_identity = fixed_depression_masks()
    phase_harness_provenance = _copy_harness_sources(phase_root)
    specs = mountain_case_specs()
    hardware = hardware_identity()
    protocol = {
        "schema": SCHEMA + ".mountain-followup-protocol",
        "frozen_before_native_execution": True,
        "frozen_at_utc": datetime.now(timezone.utc).isoformat(),
        "contract_sha256": sha256_file(CONTRACT),
        "native_backend_identity": source_identity,
        "phase_harness_source_identity": phase_harness_provenance,
        "terrain_source_identity": fixture_identity,
        "depression_mask_identity": depression_identity,
        "setup_environment": setup.get("environment"),
        "python_runtime": setup.get("provenance", {}).get("python_runtime"),
        "hardware": hardware,
        "case_order": ["full", "replay", "refinement"],
        "case_specs": specs,
        "timeout_caps_seconds": {"full": 1800.0, "replay": 600.0, "refinement": 2400.0},
        "shared_rain_history_end_s": 7200.0,
        "full_and_replay_exact_comparison": "all saved h/hUx/hUy at replay shared times must be byte and array identical; a discrepancy stops refinement",
        "refinement_dem": {
            "method": "existing V6 piecewise-linear triangles split (00,11,10)/(00,01,11) sampled at 15m centers, source crop plus immutable one-cell halo",
            "dtype": "float64 through nine-decimal DEM ASCII serialization",
            "sampled_bed_shape": list(fine_source_bed.shape),
            "sampled_bed_float64_sha256": sha256_bytes(fine_source_bed.astype("<f8").tobytes(order="C")),
        },
        "per_run_archival": "each native case archives runner/tests, setup/backend identity, contract, fixture/crop/masks, case input DEM hash and frozen case protocol before launching its child",
        "interpretation": "health, sampled corridor support and conditional pond observations remain separate; 15m is short-horizon sensitivity only",
    }
    write_json(phase_root / "phase-protocol.json", protocol)
    write_json(phase_root / "hardware.json", hardware)
    if sha256_file(Path(__file__).resolve()) != phase_harness_provenance["copies"]["runner"]["repository_source_sha256_before_copy"]:
        raise RunoffError("runner source changed after followup phase provenance was frozen")

    common_context = {
        "terrain_identity": fixture_identity,
        "depression_identity": depression_identity,
        "roi_masks": roi_masks,
        "depression_masks": depression_masks,
    }
    full_context = {
        **common_context,
        "source_bed": coarse_source_bed,
        "source_bed_semantics": "immutable transformed 30m point crop; float32 raw+offset then scale; source row order preserved",
        "immutable_point_crop": coarse_source_bed,
    }
    full_record = run_one_case(
        specs["full"], phase_root / specs["full"]["name"], timeout_s=1800.0,
        input_dem=coarse_source_bed, mountain_context=full_context,
    )
    progress: dict[str, Any] = {
        "schema": SCHEMA + ".mountain-followup-progress",
        "phase_protocol_path": str((phase_root / "phase-protocol.json").resolve()),
        "full_case_result_path": str((phase_root / specs["full"]["name"] / "case-result.json").resolve()),
        "full_status": full_record["status"],
        "full_end_to_end_wall_seconds": full_record.get("case_end_to_end_wall_seconds_including_dem_write_and_output_checks"),
        "full_native_stdout": full_record["raw_stdout"],
        "full_native_stderr": full_record["raw_stderr"],
        "replay_launched": False,
        "refinement_launched": False,
    }
    write_json(phase_root / "phase-progress.json", progress)
    if full_record["status"] != "healthy":
        result = {
            "schema": SCHEMA + ".mountain-followup-summary",
            "status": "full_case_failed_or_unhealthy",
            "phase_protocol_path": str((phase_root / "phase-protocol.json").resolve()),
            "contract_sha256": protocol["contract_sha256"],
            "hardware": hardware,
            "full_case_result_path": progress["full_case_result_path"],
            "full": full_record,
            "replay_executed": False,
            "refinement_executed": False,
        }
        write_json(phase_root / "phase-summary.json", result)
        return result

    replay_context = {**common_context, **{key: full_context[key] for key in ("source_bed", "source_bed_semantics", "immutable_point_crop")}}
    replay_record = run_one_case(
        specs["replay"], phase_root / specs["replay"]["name"], timeout_s=600.0,
        input_dem=coarse_source_bed, mountain_context=replay_context,
    )
    replay_comparison = compare_mountain_replay(full_record, replay_record) if replay_record["status"] == "healthy" else {
        "all_h_hUx_hUy_exact_array_and_file_equality": False,
        "first_mismatch": None,
        "reason": "replay did not complete with healthy validated outputs",
    }
    write_json(phase_root / "replay-comparison.json", replay_comparison)
    progress.update({
        "replay_launched": True,
        "replay_case_result_path": str((phase_root / specs["replay"]["name"] / "case-result.json").resolve()),
        "replay_status": replay_record["status"],
        "replay_end_to_end_wall_seconds": replay_record.get("case_end_to_end_wall_seconds_including_dem_write_and_output_checks"),
        "replay_comparison_path": str((phase_root / "replay-comparison.json").resolve()),
        "replay_exact": replay_comparison.get("all_h_hUx_hUy_exact_array_and_file_equality", False),
        "refinement_launched": False,
    })
    write_json(phase_root / "phase-progress.json", progress)
    if replay_record["status"] != "healthy" or not replay_comparison.get("all_h_hUx_hUy_exact_array_and_file_equality", False):
        result = {
            "schema": SCHEMA + ".mountain-followup-summary",
            "status": "replay_failed_or_exact_replay_diverged",
            "phase_protocol_path": str((phase_root / "phase-protocol.json").resolve()),
            "contract_sha256": protocol["contract_sha256"],
            "hardware": hardware,
            "full": full_record,
            "replay": replay_record,
            "replay_comparison": replay_comparison,
            "refinement_executed": False,
        }
        write_json(phase_root / "phase-summary.json", result)
        return result

    refinement_context = {
        **common_context,
        "source_bed": fine_source_bed,
        "source_bed_semantics": "15m float64 sample of frozen V6 piecewise-linear triangles over immutable 30m point crop plus one-cell halo",
        "immutable_point_crop": None,
    }
    progress["refinement_launched"] = True
    write_json(phase_root / "phase-progress.json", progress)
    refinement_record = run_one_case(
        specs["refinement"], phase_root / specs["refinement"]["name"], timeout_s=2400.0,
        input_dem=fine_source_bed, mountain_context=refinement_context,
    )
    resolution_comparison = None
    if refinement_record["status"] == "healthy":
        resolution_comparison = compare_mountain_refinement(full_record, refinement_record,
                                                            coarse_source_bed, fine_source_bed)
        write_json(phase_root / "resolution-comparison-600s.json", resolution_comparison)
    progress.update({
        "refinement_case_result_path": str((phase_root / specs["refinement"]["name"] / "case-result.json").resolve()),
        "refinement_status": refinement_record["status"],
        "refinement_end_to_end_wall_seconds": refinement_record.get("case_end_to_end_wall_seconds_including_dem_write_and_output_checks"),
        "resolution_comparison_path": (str((phase_root / "resolution-comparison-600s.json").resolve())
                                       if resolution_comparison is not None else None),
    })
    write_json(phase_root / "phase-progress.json", progress)
    result = {
        "schema": SCHEMA + ".mountain-followup-summary",
        "status": "complete_health_and_comparisons_recorded" if refinement_record["status"] == "healthy" else "refinement_failed_or_unhealthy",
        "phase_protocol_path": str((phase_root / "phase-protocol.json").resolve()),
        "phase_progress_path": str((phase_root / "phase-progress.json").resolve()),
        "contract_sha256": protocol["contract_sha256"],
        "setup_source_identity": source_identity,
        "phase_harness_provenance": phase_harness_provenance,
        "terrain_source_identity": fixture_identity,
        "depression_mask_identity": depression_identity,
        "hardware": hardware,
        "full_case_result_path": progress["full_case_result_path"],
        "replay_case_result_path": progress["replay_case_result_path"],
        "refinement_case_result_path": progress["refinement_case_result_path"],
        "full": {
            "status": full_record["status"],
            "end_to_end_wall_seconds": full_record.get("case_end_to_end_wall_seconds_including_dem_write_and_output_checks"),
            "child_wall_seconds_including_imports": full_record.get("child_process_wall_seconds_including_imports"),
            "flood_call_wall_seconds": full_record.get("flood_run_wall_seconds_after_imports_and_input_write"),
            "native_cuda_event_runtime_ms": full_record.get("native_cuda_event_runtime_ms"),
            "saved_times_s": full_record.get("output_health", {}).get("times_s"),
            "field_health": full_record.get("metrics", {}).get("saved_field_health_gate_passed"),
            "corridor_sampled_support_status": full_record.get("metrics", {}).get("corridor_sampled_support_status"),
            "pond_15min_criteria_by_frozen_mask": full_record.get("metrics", {}).get("pond_15min_criteria_met_by_frozen_depression"),
            "case_result_path": progress["full_case_result_path"],
            "raw_stdout": full_record["raw_stdout"],
            "raw_stderr": full_record["raw_stderr"],
        },
        "replay": {
            "status": replay_record["status"],
            "end_to_end_wall_seconds": replay_record.get("case_end_to_end_wall_seconds_including_dem_write_and_output_checks"),
            "case_result_path": progress["replay_case_result_path"],
            "raw_stdout": replay_record["raw_stdout"],
            "raw_stderr": replay_record["raw_stderr"],
        },
        "replay_comparison": replay_comparison,
        "refinement": {
            "status": refinement_record["status"],
            "end_to_end_wall_seconds": refinement_record.get("case_end_to_end_wall_seconds_including_dem_write_and_output_checks"),
            "case_result_path": progress["refinement_case_result_path"],
            "raw_stdout": refinement_record["raw_stdout"],
            "raw_stderr": refinement_record["raw_stderr"],
        },
        "resolution_comparison_600s": resolution_comparison,
        "all_raster_fields_retained": True,
        "native_applied_face_flux_observable": False,
        "intermediate_stage_positivity_observable": False,
        "interpretation": "status records process and saved-field health/comparison availability, not accuracy certification; corridor and pond observations are not promoted to a calibrated-hydrology pass",
    }
    write_json(phase_root / "phase-summary.json", result)
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--child", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--phase", choices=("controls", "mountain-probe", "mountain-followup"), help="execute one explicit predeclared checkpoint")
    parser.add_argument("--out", type=Path, help="fresh phase leaf under the native runoff study root")
    parser.add_argument("--timeout-s", type=float, default=600.0)
    args = parser.parse_args(argv)
    if args.child is not None:
        return _child(args.child)
    if args.phase not in ("controls", "mountain-probe", "mountain-followup"):
        parser.error("choose one of controls, mountain-probe or mountain-followup explicitly; the runner never starts cases implicitly")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    phase_prefix = {"controls": "native-controls", "mountain-probe": "native-mountain-probe",
                    "mountain-followup": "native-mountain-followup"}[args.phase]
    output = args.out if args.out is not None else STUDY_ROOT / f"{phase_prefix}-{stamp}"
    try:
        if args.phase == "controls":
            result = run_controls(output, timeout_s=args.timeout_s)
        elif args.phase == "mountain-probe":
            result = run_mountain_probe(output, timeout_s=args.timeout_s)
        else:
            result = run_mountain_followup(output)
    except Exception as error:
        print(f"NATIVE_RUNOFF_REUSE_FAILURE={type(error).__name__}: {error}", file=sys.stderr)
        return 1
    print(json.dumps({
        "status": result["status"],
        "summary_path": result.get("phase_protocol_path") and str(Path(result["phase_protocol_path"]).with_name("phase-summary.json"))
            or (str((Path(result["cases"][0]["working_directory"]).parent / "phase-summary.json").resolve()) if result.get("cases") else None),
        "case_statuses": ({case["case"]["name"]: case["status"] for case in result["cases"]}
                          if "cases" in result else ({result.get("case", {}).get("case", {}).get("name", "mountain-probe"): result.get("status")}
                          if "case" in result else {key: result[key].get("status") for key in ("full", "replay", "refinement") if isinstance(result.get(key), dict)})),
    }, indent=2, sort_keys=True))
    successful_statuses = {"healthy", "complete_health_and_comparisons_recorded"}
    return 0 if result.get("all_predeclared_gates_passed", result.get("status") in successful_statuses) else 1


if __name__ == "__main__":
    raise SystemExit(main())
