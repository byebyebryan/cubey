#!/usr/bin/env python3
"""Bake pinned Terrain Diffusion worlds into Cubey raster-study fields."""

from __future__ import annotations

import argparse
import array
import contextlib
import dataclasses
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import resource
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile

from export_heightfield_manifest import export_study_field


CODE_REVISION = "82a0431281f21a6ec3d691a12ee61525de5b0790"
MODEL_ID = "xandergos/terrain-diffusion-30m"
MODEL_REVISION = "9ef8030cb805b433b98ec25c5dddefbac07a9e26"
MODEL_NATIVE_RESOLUTION_M = 30.0
LATENTS_BATCH_SIZE = 1
SEEDS = (0, 9012, 12345)
FIELD_SIZE = 2048
CORE_TILE_SIZE = 1024
TILE_CONTEXT_HALO = 64
SEAM_VALIDATION_HALF_WIDTH = 8
COARSE_CELL_NATIVE_SAMPLES = 256
COARSE_WINDOW_CELLS = FIELD_SIZE // COARSE_CELL_NATIVE_SAMPLES
CANDIDATE_OFFSETS = tuple(range(-24, 25, COARSE_WINDOW_CELLS))
LAND_THRESHOLD = 0.8
ORDER_CHECK_SEED = 0
ORDER_TOLERANCE_M = 1.0e-4
COMPARISON_RELIEF_M = 3500.0
DEFAULT_ASSET_HEIGHT_OFFSET_M = 39.367266654968255
DEFAULT_ASSET_HEIGHT_SCALE = 0.8484453174300328
DEFAULT_ASSET_ELEVATION_SHA256 = "27b49f12f29ae24629a8ec03d12b53c6986404c0354069529be75a5ea02c45df"
DEFAULT_ASSET_CLIMATE_SHA256 = "cf56ae54e93ab45a10d0e93c2c39ab2a95b1593bf89639eeda3e3b7080497fea"
EXPORT_CONTRACT_REVISION = 4
SURFACE_STUDY_SCHEMA = "cubey.terrain.surface-fields.study.v1"
SURFACE_STUDY_DOWNSAMPLE = 8
SURFACE_STUDY_SIZE = FIELD_SIZE // SURFACE_STUDY_DOWNSAMPLE
CLIMATE_CALIBRATION_SCHEMA = "cubey.terrain.climate-calibration.v1"
CLIMATE_CALIBRATION_REGION_SCHEMA = "cubey.terrain.climate-calibration-region.v1"
CLIMATE_MACRO_SPACING_M = MODEL_NATIVE_RESOLUTION_M * COARSE_CELL_NATIVE_SAMPLES
CLIMATE_SCAN_BEGIN = -128
CLIMATE_SCAN_END = 136
CLIMATE_MIN_RELIEF_M = 1_000.0
CLIMATE_CALIBRATION_PACKAGE_LIMIT_BYTES = 128 * 1024 * 1024
CLIMATE_CALIBRATION_GENERATION_LIMIT_SECONDS = 300.0
CLIMATE_CONTROL_ORIGIN = (-8, -24)
CLIMATE_EXPECTED_ORIGINS = {
    "hot-dry": CLIMATE_CONTROL_ORIGIN,
    "hot-wet": (-56, 24),
    "cool-wet": (120, -112),
    "cold-dry": (-16, -32),
    "cold-wet": (-8, -88),
}
WORLDCLIM_URL = "https://geodata.ucdavis.edu/climate/worldclim/2_1/base/wc2.1_10m_bio.zip"
WORLDCLIM_ARCHIVE_SHA256 = "00513224583665ec0f2f955a4ec252730c4deb2004cce9e793492a3f26df4dcf"
WORLDCLIM_FILES = (
    "wc2.1_10m_bio_1.tif",
    "wc2.1_10m_bio_4.tif",
    "wc2.1_10m_bio_12.tif",
    "wc2.1_10m_bio_15.tif",
)
CLIMATE_CHANNELS = (
    ("temperature_mean", "deg_c"),
    ("temperature_stddev", "deg_c"),
    ("precipitation_annual", "mm_per_year"),
    ("precipitation_cv", "fraction"),
)


@dataclasses.dataclass(frozen=True)
class RegionCandidate:
    coarse_i: int
    coarse_j: int
    land_fraction: float
    land_p25_m: float | None
    land_p90_m: float | None
    relief_m: float
    distance_squared: int

    def as_json(self) -> dict[str, object]:
        return dataclasses.asdict(self)


@dataclasses.dataclass(frozen=True)
class ClimateRegimeSpec:
    name: str
    target_temperature_c: float
    target_precipitation_mm: float
    minimum_temperature_c: float | None = None
    maximum_temperature_c: float | None = None
    minimum_precipitation_mm: float | None = None
    maximum_precipitation_mm: float | None = None

    def accepts(self, temperature_c: float, precipitation_mm: float) -> bool:
        return (
            (self.minimum_temperature_c is None or temperature_c >= self.minimum_temperature_c)
            and (self.maximum_temperature_c is None or temperature_c <= self.maximum_temperature_c)
            and (
                self.minimum_precipitation_mm is None
                or precipitation_mm >= self.minimum_precipitation_mm
            )
            and (
                self.maximum_precipitation_mm is None
                or precipitation_mm <= self.maximum_precipitation_mm
            )
        )


CLIMATE_REGIMES = (
    ClimateRegimeSpec(
        "hot-dry",
        22.0,
        150.0,
        minimum_temperature_c=18.0,
        maximum_precipitation_mm=300.0,
    ),
    ClimateRegimeSpec(
        "hot-wet",
        22.0,
        2_000.0,
        minimum_temperature_c=18.0,
        minimum_precipitation_mm=1_000.0,
    ),
    ClimateRegimeSpec(
        "cool-wet",
        10.0,
        800.0,
        minimum_temperature_c=5.0,
        maximum_temperature_c=15.0,
        minimum_precipitation_mm=500.0,
    ),
    ClimateRegimeSpec(
        "cold-dry",
        -8.0,
        50.0,
        maximum_temperature_c=2.0,
        maximum_precipitation_mm=150.0,
    ),
    ClimateRegimeSpec(
        "cold-wet",
        -5.0,
        300.0,
        maximum_temperature_c=2.0,
        minimum_precipitation_mm=200.0,
    ),
)


@dataclasses.dataclass(frozen=True)
class ClimateRegionCandidate:
    coarse_i: int
    coarse_j: int
    land_fraction: float
    land_p25_m: float
    land_p90_m: float
    relief_m: float
    temperature_median_c: float
    temperature_stddev_median_c: float
    precipitation_median_mm: float
    precipitation_cv_median: float
    distance_squared: int

    def as_json(self) -> dict[str, object]:
        return dataclasses.asdict(self)


@dataclasses.dataclass(frozen=True)
class ClimateRegimeSelection:
    regime: ClimateRegimeSpec
    candidate: ClimateRegionCandidate
    score: float

    def as_json(self) -> dict[str, object]:
        return {
            "regime": self.regime.name,
            "target": {
                "temperature_mean_c": self.regime.target_temperature_c,
                "precipitation_annual_mm": self.regime.target_precipitation_mm,
            },
            "score": self.score,
            "candidate": self.candidate.as_json(),
        }


@dataclasses.dataclass
class QueriedTile:
    name: str
    bounds: tuple[int, int, int, int]
    elevation: object
    climate: object | None
    seconds: float


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_sha256_file(path: Path, expected: str, label: str) -> str:
    actual = sha256_file(path)
    if actual != expected:
        raise RuntimeError(f"{label} SHA-256 mismatch: expected {expected}, got {actual}")
    return actual


def _load_json(path: Path) -> dict[str, object]:
    document = json.loads(path.read_text())
    if not isinstance(document, dict):
        raise RuntimeError(f"expected an object in {path}")
    return document


def _validate_pinned_source(source: object) -> None:
    if not isinstance(source, dict):
        raise RuntimeError("terrain source metadata is missing")
    if (
        source.get("generator") != "terrain-diffusion"
        or source.get("code_revision") != CODE_REVISION
        or source.get("model_id") != MODEL_ID
        or source.get("model_revision") != MODEL_REVISION
    ):
        raise RuntimeError("terrain source provenance does not match the pinned producer")


def _require_integer(value: object, label: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise RuntimeError(f"{label} is invalid")
    return value


def _require_finite(value: object, label: str, *, positive: bool = False) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise RuntimeError(f"{label} is invalid")
    result = float(value)
    if not math.isfinite(result) or (positive and result <= 0.0):
        raise RuntimeError(f"{label} is invalid")
    return result


def _validate_grid(grid: object, label: str) -> tuple[int, int]:
    if not isinstance(grid, dict):
        raise RuntimeError(f"{label} grid is missing")
    width = _require_integer(grid.get("width"), f"{label} grid width", 2, 16_384)
    height = _require_integer(grid.get("height"), f"{label} grid height", 2, 16_384)
    _require_finite(grid.get("sample_spacing_m"), f"{label} sample spacing", positive=True)
    _require_finite(grid.get("sample_origin_x_m"), f"{label} x origin")
    _require_finite(grid.get("sample_origin_z_m"), f"{label} z origin")
    if grid.get("axis_mapping") != {"world_x": "model_j", "world_z": "model_i"}:
        raise RuntimeError(f"{label} axis mapping is incompatible")
    return width, height


def _read_float32_payload(path: Path, count: int, label: str) -> array.array:
    values = array.array("f")
    values.frombytes(path.read_bytes())
    if sys.byteorder != "little":
        values.byteswap()
    if len(values) != count:
        raise RuntimeError(f"{label} float count is invalid")
    return values


def _validate_payload_record(
    root: Path,
    record: object,
    expected_name: str,
    expected_sha256: str | None = None,
) -> str:
    if not isinstance(record, dict) or record.get("path") != expected_name:
        raise RuntimeError(f"terrain payload record must reference {expected_name}")
    payload = root / expected_name
    byte_count = record.get("byte_count")
    expected_hash = record.get("sha256")
    if (
        not isinstance(byte_count, int)
        or byte_count <= 0
        or not isinstance(expected_hash, str)
        or len(expected_hash) != 64
        or not payload.is_file()
        or payload.stat().st_size != byte_count
    ):
        raise RuntimeError(f"terrain payload contract is incomplete for {payload}")
    if expected_sha256 is not None and expected_hash != expected_sha256:
        raise RuntimeError(f"terrain payload identity changed for {payload}")
    return verify_sha256_file(payload, expected_hash, str(payload))


def _validate_float32_record(
    root: Path,
    record: object,
    expected_name: str,
    expected_shape: list[int],
    expected_layout: str,
    expected_sha256: str | None = None,
) -> tuple[str, array.array]:
    if (
        not isinstance(record, dict)
        or record.get("dtype") != "float32-le"
        or record.get("layout") != expected_layout
        or record.get("shape") != expected_shape
        or record.get("byte_count") != math.prod(expected_shape) * 4
    ):
        raise RuntimeError(f"terrain payload contract is incompatible for {expected_name}")
    payload_hash = _validate_payload_record(root, record, expected_name, expected_sha256)
    values = _read_float32_payload(
        root / expected_name, math.prod(expected_shape), expected_name
    )
    if not all(math.isfinite(value) for value in values):
        raise RuntimeError(f"terrain payload contains non-finite values: {expected_name}")
    return payload_hash, values


def _validate_heightfield_bundle(root: Path, expected_sha256: str | None = None) -> str:
    manifest = _load_json(root / "heightfield.json")
    if (
        manifest.get("schema") != "cubey.terrain.heightfield.v1"
        or isinstance(manifest.get("seed"), bool)
        or manifest.get("seed") != 0
    ):
        raise RuntimeError("terrain heightfield manifest identity is incompatible")
    source = manifest.get("source")
    _validate_pinned_source(source)
    if (
        not isinstance(source, dict)
        or not isinstance(source.get("id"), str)
        or not source["id"]
    ):
        raise RuntimeError("terrain heightfield source id is missing")
    width, height = _validate_grid(manifest.get("grid"), "terrain heightfield")
    height_contract = manifest.get("height")
    if not isinstance(height_contract, dict):
        raise RuntimeError("terrain heightfield transform is missing")
    _require_finite(height_contract.get("offset_m"), "terrain height offset")
    _require_finite(height_contract.get("scale"), "terrain height scale", positive=True)
    _require_finite(
        height_contract.get("relief_scale_m"), "terrain relief scale", positive=True
    )
    files = manifest.get("files")
    if not isinstance(files, dict):
        raise RuntimeError("terrain heightfield files are missing")
    elevation = files.get("elevation")
    if not isinstance(elevation, dict) or elevation.get("unit") != "m":
        raise RuntimeError("terrain elevation unit is incompatible")
    elevation_sha256, _ = _validate_float32_record(
        root,
        elevation,
        "elevation.f32",
        [height, width],
        "row-major-zx",
        expected_sha256,
    )
    return elevation_sha256


def _validate_surface_bundle(
    root: Path,
    expected_elevation_sha256: str,
    expected_climate_sha256: str | None = None,
) -> str:
    manifest = _load_json(root / "surface-fields.json")
    if (
        manifest.get("schema") != SURFACE_STUDY_SCHEMA
        or isinstance(manifest.get("seed"), bool)
        or manifest.get("seed") != 0
    ):
        raise RuntimeError("terrain surface manifest identity is incompatible")
    _validate_pinned_source(manifest.get("source"))
    heightfield = manifest.get("heightfield")
    if (
        not isinstance(heightfield, dict)
        or heightfield.get("schema") != "cubey.terrain.heightfield.v1"
        or heightfield.get("elevation_sha256") != expected_elevation_sha256
    ):
        raise RuntimeError("terrain surface fields are bound to another heightfield")
    width, height = _validate_grid(manifest.get("grid"), "terrain surface")
    files = manifest.get("files")
    if not isinstance(files, dict):
        raise RuntimeError("terrain surface files are missing")
    climate = files.get("climate")
    expected_channels = [
        {"name": name, "unit": unit} for name, unit in CLIMATE_CHANNELS
    ]
    if not isinstance(climate, dict) or climate.get("channels") != expected_channels:
        raise RuntimeError("terrain climate channel contract is incompatible")
    climate_sha256, values = _validate_float32_record(
        root,
        climate,
        "climate.f32",
        [len(CLIMATE_CHANNELS), height, width],
        "channel-major-zx",
        expected_climate_sha256,
    )
    plane = width * height
    if (
        any(value < 0.0 for value in values[plane : 2 * plane])
        or any(value < 0.0 for value in values[2 * plane : 3 * plane])
        or any(value < 0.0 or value > 1.0 for value in values[3 * plane : 4 * plane])
    ):
        raise RuntimeError("terrain climate values violate declared physical ranges")
    return climate_sha256


def validate_existing_asset(mode: str, output_dir: Path) -> bool:
    try:
        root = output_dir.resolve()
        if mode == "default":
            _validate_heightfield_bundle(root, DEFAULT_ASSET_ELEVATION_SHA256)
        elif mode == "surface-study":
            _validate_surface_bundle(
                root, DEFAULT_ASSET_ELEVATION_SHA256, DEFAULT_ASSET_CLIMATE_SHA256
            )
        elif mode == "climate-calibration":
            index = _load_json(root / "calibration-index.json")
            if index.get("schema") != CLIMATE_CALIBRATION_SCHEMA or index.get("seed") != 0:
                raise RuntimeError("terrain climate calibration index is incompatible")
            _validate_pinned_source(index.get("source"))
            for name in CLIMATE_EXPECTED_ORIGINS:
                region = root / name
                elevation_sha256 = _validate_heightfield_bundle(
                    region,
                    DEFAULT_ASSET_ELEVATION_SHA256 if name == "hot-dry" else None,
                )
                _validate_surface_bundle(
                    region,
                    elevation_sha256,
                    DEFAULT_ASSET_CLIMATE_SHA256 if name == "hot-dry" else None,
                )
        else:
            return False
        return True
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError, RuntimeError):
        return False


def git_revision(path: Path) -> str:
    return subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"], text=True
    ).strip()


def _seed_process_rngs(torch, seed: int) -> None:
    import random

    import numpy as np

    random.seed(seed)
    np.random.seed(seed & 0xFFFFFFFF)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def select_mountain_region(coarse_elevation) -> tuple[RegionCandidate, list[RegionCandidate]]:
    import numpy as np

    expected_size = CANDIDATE_OFFSETS[-1] + COARSE_WINDOW_CELLS - CANDIDATE_OFFSETS[0]
    if coarse_elevation.shape != (expected_size, expected_size):
        raise ValueError(
            f"coarse selection field must be {expected_size}x{expected_size}, "
            f"got {coarse_elevation.shape}"
        )

    candidates: list[RegionCandidate] = []
    base = CANDIDATE_OFFSETS[0]
    for coarse_i in CANDIDATE_OFFSETS:
        for coarse_j in CANDIDATE_OFFSETS:
            i0 = coarse_i - base
            j0 = coarse_j - base
            values = np.asarray(
                coarse_elevation[
                    i0 : i0 + COARSE_WINDOW_CELLS,
                    j0 : j0 + COARSE_WINDOW_CELLS,
                ],
                dtype=np.float64,
            )
            land = values[values > 0.0]
            land_fraction = float(land.size / values.size)
            if land.size:
                p25, p90 = np.percentile(land, (25.0, 90.0))
                p25_value = float(p25)
                p90_value = float(p90)
                relief = p90_value - p25_value
            else:
                p25_value = None
                p90_value = None
                relief = 0.0
            candidates.append(
                RegionCandidate(
                    coarse_i=coarse_i,
                    coarse_j=coarse_j,
                    land_fraction=land_fraction,
                    land_p25_m=p25_value,
                    land_p90_m=p90_value,
                    relief_m=relief,
                    distance_squared=coarse_i * coarse_i + coarse_j * coarse_j,
                )
            )

    qualified = [candidate for candidate in candidates if candidate.land_fraction >= LAND_THRESHOLD]

    def common_rank(candidate: RegionCandidate) -> tuple[float, float, int, int, int]:
        return (
            candidate.relief_m,
            candidate.land_p90_m if candidate.land_p90_m is not None else -1.0e30,
            -candidate.distance_squared,
            -candidate.coarse_i,
            -candidate.coarse_j,
        )

    if qualified:
        winner = max(qualified, key=common_rank)
    else:
        winner = max(candidates, key=lambda candidate: (candidate.land_fraction, *common_rank(candidate)))
    return winner, candidates


def climate_region_candidates(coarse_values) -> list[ClimateRegionCandidate]:
    """Summarize non-overlapping product windows from normalized coarse output."""
    import numpy as np

    coarse = np.asarray(coarse_values, dtype=np.float32)
    expected_size = CLIMATE_SCAN_END - CLIMATE_SCAN_BEGIN
    if coarse.shape != (6, expected_size, expected_size):
        raise ValueError(
            f"climate scan must be 6x{expected_size}x{expected_size}, got {coarse.shape}"
        )
    if not np.isfinite(coarse).all():
        raise ValueError("climate scan contains non-finite values")

    elevation = np.sign(coarse[0]) * np.square(np.abs(coarse[0]))
    temperature = coarse[2]
    temperature_stddev = np.maximum(coarse[3] * 0.01, 0.0)
    precipitation = np.maximum(coarse[4], 0.0)
    precipitation_cv = np.clip(coarse[5] * 0.01, 0.0, 1.0)
    candidates = []
    for coarse_i in range(CLIMATE_SCAN_BEGIN, CLIMATE_SCAN_END, COARSE_WINDOW_CELLS):
        for coarse_j in range(CLIMATE_SCAN_BEGIN, CLIMATE_SCAN_END, COARSE_WINDOW_CELLS):
            i0 = coarse_i - CLIMATE_SCAN_BEGIN
            j0 = coarse_j - CLIMATE_SCAN_BEGIN
            rows = slice(i0, i0 + COARSE_WINDOW_CELLS)
            columns = slice(j0, j0 + COARSE_WINDOW_CELLS)
            elevation_window = elevation[rows, columns]
            land = elevation_window > 0.0
            land_fraction = float(np.mean(land))
            if land_fraction < LAND_THRESHOLD:
                continue
            land_elevation = elevation_window[land]
            land_p25, land_p90 = np.percentile(land_elevation, (25.0, 90.0))
            relief = float(land_p90 - land_p25)
            if relief < CLIMATE_MIN_RELIEF_M:
                continue

            def land_median(values) -> float:
                return float(np.median(values[rows, columns][land]))

            candidates.append(
                ClimateRegionCandidate(
                    coarse_i=coarse_i,
                    coarse_j=coarse_j,
                    land_fraction=land_fraction,
                    land_p25_m=float(land_p25),
                    land_p90_m=float(land_p90),
                    relief_m=relief,
                    temperature_median_c=land_median(temperature),
                    temperature_stddev_median_c=land_median(temperature_stddev),
                    precipitation_median_mm=land_median(precipitation),
                    precipitation_cv_median=land_median(precipitation_cv),
                    distance_squared=coarse_i * coarse_i + coarse_j * coarse_j,
                )
            )
    return candidates


def climate_regime_score(
    regime: ClimateRegimeSpec, candidate: ClimateRegionCandidate
) -> float:
    temperature_distance = (
        (candidate.temperature_median_c - regime.target_temperature_c) / 8.0
    )
    precipitation_distance = (
        math.log10(max(candidate.precipitation_median_mm, 1.0))
        - math.log10(regime.target_precipitation_mm)
    ) / 0.45
    return (
        temperature_distance * temperature_distance
        + precipitation_distance * precipitation_distance
    )


def select_climate_calibration_regions(
    coarse_values, *, require_expected: bool = False
) -> tuple[list[ClimateRegimeSelection], list[ClimateRegionCandidate]]:
    candidates = climate_region_candidates(coarse_values)
    by_origin = {
        (candidate.coarse_i, candidate.coarse_j): candidate for candidate in candidates
    }
    selections = []
    used_origins = set()
    for regime in CLIMATE_REGIMES:
        if regime.name == "hot-dry":
            candidate = by_origin.get(CLIMATE_CONTROL_ORIGIN)
            if candidate is None or not regime.accepts(
                candidate.temperature_median_c, candidate.precipitation_median_mm
            ):
                raise RuntimeError(
                    "canonical hot-dry control does not satisfy its climate contract"
                )
            score = climate_regime_score(regime, candidate)
        else:
            eligible = [
                candidate
                for candidate in candidates
                if (candidate.coarse_i, candidate.coarse_j) not in used_origins
                and regime.accepts(
                    candidate.temperature_median_c, candidate.precipitation_median_mm
                )
            ]
            if not eligible:
                raise RuntimeError(f"no qualified coarse window for climate regime {regime.name}")
            candidate = min(
                eligible,
                key=lambda value: (
                    climate_regime_score(regime, value),
                    -value.relief_m,
                    -value.land_fraction,
                    value.distance_squared,
                    value.coarse_i,
                    value.coarse_j,
                ),
            )
            score = climate_regime_score(regime, candidate)
        origin = (candidate.coarse_i, candidate.coarse_j)
        if origin in used_origins:
            raise RuntimeError(f"climate calibration selected duplicate window {origin}")
        used_origins.add(origin)
        selections.append(ClimateRegimeSelection(regime, candidate, score))

    if require_expected:
        actual = {
            selection.regime.name: (
                selection.candidate.coarse_i,
                selection.candidate.coarse_j,
            )
            for selection in selections
        }
        if actual != CLIMATE_EXPECTED_ORIGINS:
            raise RuntimeError(
                f"climate calibration selection changed: expected {CLIMATE_EXPECTED_ORIGINS}, "
                f"got {actual}"
            )
    return selections, candidates


def comparison_calibration(fields) -> dict[str, float]:
    import numpy as np

    values = np.concatenate([np.asarray(field, dtype=np.float32).reshape(-1) for field in fields])
    p05, p95 = np.percentile(values, (5.0, 95.0))
    if not math.isfinite(float(p05)) or not math.isfinite(float(p95)) or p95 <= p05:
        raise ValueError("terrain fields do not have a finite nonzero aggregate p05-p95 range")
    return {
        "aggregate_p05_raw_m": float(p05),
        "aggregate_p95_raw_m": float(p95),
        "height_offset_m": -float(p05),
        "height_scale": COMPARISON_RELIEF_M / float(p95 - p05),
        "target_relief_m": COMPARISON_RELIEF_M,
    }


def apply_calibration(field, calibration: dict[str, float]):
    import numpy as np

    return (
        (np.asarray(field, dtype=np.float32) + calibration["height_offset_m"])
        * calibration["height_scale"]
    ).astype(np.float32)


def overlapping_max_abs(first: QueriedTile, second: QueriedTile) -> tuple[int, float]:
    import numpy as np

    ai1, aj1, ai2, aj2 = first.bounds
    bi1, bj1, bi2, bj2 = second.bounds
    oi1, oj1 = max(ai1, bi1), max(aj1, bj1)
    oi2, oj2 = min(ai2, bi2), min(aj2, bj2)
    if oi1 >= oi2 or oj1 >= oj2:
        return 0, 0.0
    first_overlap = first.elevation[oi1 - ai1 : oi2 - ai1, oj1 - aj1 : oj2 - aj1]
    second_overlap = second.elevation[oi1 - bi1 : oi2 - bi1, oj1 - bj1 : oj2 - bj1]
    difference = np.abs(first_overlap.astype(np.float64) - second_overlap.astype(np.float64))
    return int(difference.size), float(np.max(difference, initial=0.0))


def seam_max_abs(first: QueriedTile, second: QueriedTile) -> tuple[int, float]:
    ai1, aj1, ai2, aj2 = first.bounds
    bi1, bj1, bi2, bj2 = second.bounds
    oi1, oj1 = max(ai1, bi1), max(aj1, bj1)
    oi2, oj2 = min(ai2, bi2), min(aj2, bj2)
    if oi1 >= oi2 or oj1 >= oj2:
        return 0, 0.0

    if (ai1, ai2) != (bi1, bi2):
        center_i = (oi1 + oi2) // 2
        oi1 = center_i - SEAM_VALIDATION_HALF_WIDTH
        oi2 = center_i + SEAM_VALIDATION_HALF_WIDTH
    if (aj1, aj2) != (bj1, bj2):
        center_j = (oj1 + oj2) // 2
        oj1 = center_j - SEAM_VALIDATION_HALF_WIDTH
        oj2 = center_j + SEAM_VALIDATION_HALF_WIDTH

    first_view = QueriedTile(
        first.name,
        (oi1, oj1, oi2, oj2),
        first.elevation[oi1 - ai1 : oi2 - ai1, oj1 - aj1 : oj2 - aj1],
        None,
        first.seconds,
    )
    second_view = QueriedTile(
        second.name,
        (oi1, oj1, oi2, oj2),
        second.elevation[oi1 - bi1 : oi2 - bi1, oj1 - bj1 : oj2 - bj1],
        None,
        second.seconds,
    )
    return overlapping_max_abs(first_view, second_view)


def write_f32(path: Path, values) -> dict[str, object]:
    import numpy as np

    array = np.ascontiguousarray(values, dtype="<f4")
    if not np.isfinite(array).all():
        raise ValueError(f"refusing to write non-finite field: {path}")
    array.tofile(path)
    return {
        "path": path.name,
        "dtype": "float32-le",
        "byte_count": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def sha256_f32(values) -> str:
    import numpy as np

    array = np.ascontiguousarray(values, dtype="<f4")
    if not np.isfinite(array).all():
        raise ValueError("refusing to hash non-finite float field")
    return hashlib.sha256(array.tobytes()).hexdigest()


def normalize_climate_channels(values):
    """Convert Terrain Diffusion's native climate scaling to explicit units."""
    import numpy as np

    climate = np.asarray(values, dtype=np.float32)
    if climate.ndim != 3 or climate.shape[0] != len(CLIMATE_CHANNELS):
        raise ValueError(f"expected four climate channels, got {climate.shape}")
    if not np.isfinite(climate).all():
        raise ValueError("climate field contains non-finite values")
    normalized = climate.copy()
    normalized[1] = np.maximum(normalized[1] * 0.01, 0.0)
    normalized[2] = np.maximum(normalized[2], 0.0)
    normalized[3] = np.clip(normalized[3] * 0.01, 0.0, 1.0)
    return normalized


def area_average_field(values, factor: int):
    import numpy as np

    field = np.asarray(values, dtype=np.float32)
    if field.ndim != 3 or factor <= 0 or field.shape[1] % factor or field.shape[2] % factor:
        raise ValueError(f"field shape {field.shape} is not divisible by factor {factor}")
    channels, height, width = field.shape
    averaged = field.reshape(
        channels, height // factor, factor, width // factor, factor
    ).mean(axis=(2, 4), dtype=np.float64)
    return averaged.astype(np.float32)


def surface_study_manifest(
    climate_file: dict[str, object], generated: dict[str, object], generation_seconds: float
) -> dict[str, object]:
    half_span = (FIELD_SIZE - 1) * MODEL_NATIVE_RESOLUTION_M * 0.5
    block_center_offset = (SURFACE_STUDY_DOWNSAMPLE - 1) * MODEL_NATIVE_RESOLUTION_M * 0.5
    return {
        "schema": SURFACE_STUDY_SCHEMA,
        "source": {
            "id": "terrain-diffusion-30m",
            "generator": "terrain-diffusion",
            "code_revision": CODE_REVISION,
            "model_id": MODEL_ID,
            "model_revision": MODEL_REVISION,
            "native_resolution_m": MODEL_NATIVE_RESOLUTION_M,
            "settings": {
                "climate_output": True,
                "custom_conditioning": False,
                "device": "cuda",
                "dtype": "fp32",
                "latents_batch_size": LATENTS_BATCH_SIZE,
                "caching_strategy": "direct",
                "process_rng_seeding": "seed-value-v1",
                "torch_compile": False,
            },
        },
        "seed": ORDER_CHECK_SEED,
        "heightfield": {
            "schema": "cubey.terrain.heightfield.v1",
            "elevation_sha256": DEFAULT_ASSET_ELEVATION_SHA256,
        },
        "grid": {
            "width": SURFACE_STUDY_SIZE,
            "height": SURFACE_STUDY_SIZE,
            "sample_spacing_m": MODEL_NATIVE_RESOLUTION_M * SURFACE_STUDY_DOWNSAMPLE,
            "sample_origin_x_m": -half_span + block_center_offset,
            "sample_origin_z_m": -half_span + block_center_offset,
            "axis_mapping": {"world_x": "model_j", "world_z": "model_i"},
        },
        "files": {
            "climate": {
                **climate_file,
                "layout": "channel-major-zx",
                "shape": [len(CLIMATE_CHANNELS), SURFACE_STUDY_SIZE, SURFACE_STUDY_SIZE],
                "channels": [
                    {"name": name, "unit": unit} for name, unit in CLIMATE_CHANNELS
                ],
            }
        },
        "processing": {
            "method": "area-average",
            "factor": SURFACE_STUDY_DOWNSAMPLE,
            "source_spacing_m": MODEL_NATIVE_RESOLUTION_M,
        },
        "provenance": {
            "purpose": "cubey-terrain-surface-semantics-study-v1",
            "selection": "fixed-grid-land-relief-v1",
            "model_native_origin": generated["model_native_origin"],
            "generation_seconds": generation_seconds,
        },
    }


def climate_calibration_heightfield_manifest(
    elevation_file: dict[str, object],
    generated: dict[str, object],
    common_source: dict[str, object],
) -> dict[str, object]:
    half_span = (FIELD_SIZE - 1) * MODEL_NATIVE_RESOLUTION_M * 0.5
    selection: ClimateRegimeSelection = generated["selection"]
    return {
        "schema": "cubey.terrain.heightfield.v1",
        "source": common_source,
        "seed": ORDER_CHECK_SEED,
        "grid": {
            "width": FIELD_SIZE,
            "height": FIELD_SIZE,
            "sample_spacing_m": MODEL_NATIVE_RESOLUTION_M,
            "sample_origin_x_m": -half_span,
            "sample_origin_z_m": -half_span,
            "axis_mapping": {"world_x": "model_j", "world_z": "model_i"},
        },
        "height": {
            "offset_m": DEFAULT_ASSET_HEIGHT_OFFSET_M,
            "scale": DEFAULT_ASSET_HEIGHT_SCALE,
            "relief_scale_m": COMPARISON_RELIEF_M,
        },
        "files": {
            "elevation": {
                **elevation_file,
                "layout": "row-major-zx",
                "shape": [FIELD_SIZE, FIELD_SIZE],
                "unit": "m",
            }
        },
        "provenance": {
            "purpose": "cubey-terrain-climate-calibration-v1",
            "selection": "fixed-seed-climate-regime-v1",
            "regime": selection.regime.name,
            "model_native_origin": generated["model_native_origin"],
            "generation_seconds": generated["total_seconds"],
        },
    }


def climate_calibration_surface_manifest(
    climate_file: dict[str, object],
    elevation_sha256: str,
    generated: dict[str, object],
    common_source: dict[str, object],
) -> dict[str, object]:
    half_span = (FIELD_SIZE - 1) * MODEL_NATIVE_RESOLUTION_M * 0.5
    block_center_offset = (
        (SURFACE_STUDY_DOWNSAMPLE - 1) * MODEL_NATIVE_RESOLUTION_M * 0.5
    )
    selection: ClimateRegimeSelection = generated["selection"]
    return {
        "schema": SURFACE_STUDY_SCHEMA,
        "source": common_source,
        "seed": ORDER_CHECK_SEED,
        "heightfield": {
            "schema": "cubey.terrain.heightfield.v1",
            "elevation_sha256": elevation_sha256,
        },
        "grid": {
            "width": SURFACE_STUDY_SIZE,
            "height": SURFACE_STUDY_SIZE,
            "sample_spacing_m": MODEL_NATIVE_RESOLUTION_M
            * SURFACE_STUDY_DOWNSAMPLE,
            "sample_origin_x_m": -half_span + block_center_offset,
            "sample_origin_z_m": -half_span + block_center_offset,
            "axis_mapping": {"world_x": "model_j", "world_z": "model_i"},
        },
        "files": {
            "climate": {
                **climate_file,
                "layout": "channel-major-zx",
                "shape": [
                    len(CLIMATE_CHANNELS),
                    SURFACE_STUDY_SIZE,
                    SURFACE_STUDY_SIZE,
                ],
                "channels": [
                    {"name": name, "unit": unit}
                    for name, unit in CLIMATE_CHANNELS
                ],
            }
        },
        "processing": {
            "method": "area-average",
            "factor": SURFACE_STUDY_DOWNSAMPLE,
            "source_spacing_m": MODEL_NATIVE_RESOLUTION_M,
        },
        "provenance": {
            "purpose": "cubey-terrain-climate-calibration-v1",
            "selection": "fixed-seed-climate-regime-v1",
            "regime": selection.regime.name,
            "model_native_origin": generated["model_native_origin"],
            "generation_seconds": generated["total_seconds"],
        },
    }


def _write_gray16(path: Path, values, low: float, high: float) -> None:
    import numpy as np
    from PIL import Image

    if not high > low:
        high = low + 1.0
    normalized = np.clip((np.asarray(values, dtype=np.float64) - low) / (high - low), 0.0, 1.0)
    pixels = np.rint(normalized * 65535.0).astype(np.uint16)
    Image.fromarray(pixels).save(path)


def height_preview_rgb(values):
    import numpy as np

    t = np.clip(np.asarray(values, dtype=np.float64) / COMPARISON_RELIEF_M, 0.0, 1.0)
    low = np.array([25.0, 31.0, 36.0])
    high = np.array([242.0, 240.0, 235.0])
    return np.rint(low + t[..., None] * (high - low)).astype(np.uint8)


def slope_preview_rgb(values):
    import numpy as np

    t = np.clip(np.asarray(values, dtype=np.float64) / 1.5, 0.0, 1.0)
    low = np.array([31.0, 87.0, 121.0])
    middle = np.array([224.0, 198.0, 70.0])
    high = np.array([170.0, 45.0, 45.0])
    first = low + (t * 2.0)[..., None] * (middle - low)
    second = middle + ((t - 0.5) * 2.0)[..., None] * (high - middle)
    return np.where((t < 0.5)[..., None], first, second).astype(np.uint8)


def _write_rgb8(path: Path, pixels) -> None:
    from PIL import Image

    Image.fromarray(pixels).save(path)


def _write_calibration_climate_previews(root: Path, climate) -> dict[str, object]:
    import numpy as np

    previews = {
        "temperature_mean": {
            "path": "climate-temperature-mean.png",
            "scale": {"minimum": -20.0, "maximum": 35.0, "unit": "deg_c"},
        },
        "temperature_stddev": {
            "path": "climate-temperature-stddev.png",
            "scale": {"minimum": 0.0, "maximum": 20.0, "unit": "deg_c"},
        },
        "precipitation_annual": {
            "path": "climate-precipitation-annual.png",
            "scale": {
                "minimum": 0.0,
                "maximum": 3_000.0,
                "unit": "mm_per_year",
                "transfer": "log1p",
            },
        },
        "precipitation_cv": {
            "path": "climate-precipitation-cv.png",
            "scale": {"minimum": 0.0, "maximum": 1.0, "unit": "fraction"},
        },
    }
    _write_gray16(
        root / previews["temperature_mean"]["path"], climate[0], -20.0, 35.0
    )
    _write_gray16(
        root / previews["temperature_stddev"]["path"], climate[1], 0.0, 20.0
    )
    _write_gray16(
        root / previews["precipitation_annual"]["path"],
        np.log1p(climate[2]),
        0.0,
        math.log1p(3_000.0),
    )
    _write_gray16(
        root / previews["precipitation_cv"]["path"], climate[3], 0.0, 1.0
    )
    return previews


def _write_selection_preview(path: Path, coarse_elevation, winner: RegionCandidate) -> None:
    import numpy as np
    from PIL import Image, ImageDraw

    values = np.asarray(coarse_elevation, dtype=np.float64)
    low, high = np.percentile(values, (2.0, 98.0))
    if not high > low:
        high = low + 1.0
    gray = np.rint(np.clip((values - low) / (high - low), 0.0, 1.0) * 255.0).astype(np.uint8)
    scale = 8
    image = Image.fromarray(gray, mode="L").resize(
        (gray.shape[1] * scale, gray.shape[0] * scale), Image.Resampling.NEAREST
    ).convert("RGB")
    draw = ImageDraw.Draw(image)
    base = CANDIDATE_OFFSETS[0]
    for coarse_i in CANDIDATE_OFFSETS:
        for coarse_j in CANDIDATE_OFFSETS:
            x0 = (coarse_j - base) * scale
            y0 = (coarse_i - base) * scale
            x1 = x0 + COARSE_WINDOW_CELLS * scale - 1
            y1 = y0 + COARSE_WINDOW_CELLS * scale - 1
            selected = coarse_i == winner.coarse_i and coarse_j == winner.coarse_j
            draw.rectangle((x0, y0, x1, y1), outline=(255, 64, 32) if selected else (208, 208, 208), width=3 if selected else 1)
    image.save(path)


def _prepare_data_cache(reference_root: Path, data_cache: Path) -> dict[str, object]:
    runtime_global = data_cache / "data" / "global"
    runtime_global.mkdir(parents=True, exist_ok=True)
    source_etopo = reference_root / "data" / "global" / "etopo_10m.tif"
    if not source_etopo.is_file():
        raise FileNotFoundError(f"pinned reference is missing {source_etopo}")
    runtime_etopo = runtime_global / source_etopo.name
    if not runtime_etopo.exists() or sha256_file(runtime_etopo) != sha256_file(source_etopo):
        shutil.copy2(source_etopo, runtime_etopo)

    archive = data_cache / "wc2.1_10m_bio.zip"
    missing = [name for name in WORLDCLIM_FILES if not (runtime_global / name).is_file()]
    download_seconds = 0.0
    if missing:
        if not archive.is_file():
            temporary = archive.with_suffix(".zip.part")
            start = time.perf_counter()
            try:
                urllib.request.urlretrieve(WORLDCLIM_URL, temporary)
                download_seconds = time.perf_counter() - start
                verify_sha256_file(temporary, WORLDCLIM_ARCHIVE_SHA256, "WorldClim archive")
                os.replace(temporary, archive)
            finally:
                temporary.unlink(missing_ok=True)
        verify_sha256_file(archive, WORLDCLIM_ARCHIVE_SHA256, "WorldClim archive")
        with zipfile.ZipFile(archive) as bundle:
            by_name = {Path(member).name: member for member in bundle.namelist()}
            for name in missing:
                member = by_name.get(name)
                if member is None:
                    raise RuntimeError(f"WorldClim archive does not contain {name}")
                destination = runtime_global / name
                with bundle.open(member) as source, destination.open("wb") as target:
                    shutil.copyfileobj(source, target)

    return {
        "worldclim_url": WORLDCLIM_URL,
        "worldclim_archive_sha256": verify_sha256_file(
            archive, WORLDCLIM_ARCHIVE_SHA256, "WorldClim archive"
        ),
        "worldclim_download_seconds": download_seconds,
        "etopo_sha256": sha256_file(runtime_etopo),
        "runtime_root": str(data_cache.resolve()),
    }


@contextlib.contextmanager
def _working_directory(path: Path):
    previous = Path.cwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(previous)


def _cuda_sync(torch) -> None:
    if torch.cuda.is_available():
        torch.cuda.synchronize()


def _query_tile(world, torch, name: str, bounds: tuple[int, int, int, int], with_climate: bool) -> QueriedTile:
    import numpy as np

    _cuda_sync(torch)
    start = time.perf_counter()
    result = world.get(*bounds, with_climate=with_climate)
    _cuda_sync(torch)
    seconds = time.perf_counter() - start
    elevation = result["elev"].detach().cpu().numpy().astype(np.float32, copy=True)
    climate = None
    if with_climate:
        climate = result["climate"][:4].detach().cpu().numpy().astype(np.float32, copy=True)
    return QueriedTile(name=name, bounds=bounds, elevation=elevation, climate=climate, seconds=seconds)


def _tile_requests(native_i: int, native_j: int) -> list[tuple[str, tuple[int, int, int, int]]]:
    requests = []
    for row, row_name in enumerate(("north", "south")):
        for column, column_name in enumerate(("west", "east")):
            core_i = native_i + row * CORE_TILE_SIZE
            core_j = native_j + column * CORE_TILE_SIZE
            requests.append(
                (
                    f"{row_name}-{column_name}",
                    (
                        core_i - TILE_CONTEXT_HALO,
                        core_j - TILE_CONTEXT_HALO,
                        core_i + CORE_TILE_SIZE + TILE_CONTEXT_HALO,
                        core_j + CORE_TILE_SIZE + TILE_CONTEXT_HALO,
                    ),
                )
            )
    return requests


def _stitch_tiles(tiles: list[QueriedTile], with_climate: bool = True):
    import numpy as np

    elevation = np.empty((FIELD_SIZE, FIELD_SIZE), dtype=np.float32)
    climate = np.empty((4, FIELD_SIZE, FIELD_SIZE), dtype=np.float32) if with_climate else None
    for index, tile in enumerate(tiles):
        row, column = divmod(index, 2)
        core = slice(TILE_CONTEXT_HALO, TILE_CONTEXT_HALO + CORE_TILE_SIZE)
        elevation[
            row * CORE_TILE_SIZE : (row + 1) * CORE_TILE_SIZE,
            column * CORE_TILE_SIZE : (column + 1) * CORE_TILE_SIZE,
        ] = tile.elevation[core, core]
        if with_climate:
            if tile.climate is None:
                raise RuntimeError("climate output is missing from a primary tile")
            climate[
                :,
                row * CORE_TILE_SIZE : (row + 1) * CORE_TILE_SIZE,
                column * CORE_TILE_SIZE : (column + 1) * CORE_TILE_SIZE,
            ] = tile.climate[:, core, core]
    return elevation, climate


def _coarse_selection(world):
    import numpy as np

    begin = CANDIDATE_OFFSETS[0]
    end = CANDIDATE_OFFSETS[-1] + COARSE_WINDOW_CELLS
    coarse_weighted = world.coarse[:, begin:end, begin:end]
    coarse = (coarse_weighted[:-1] / coarse_weighted[-1:]).detach().cpu().numpy()
    elevation = np.square(np.maximum(coarse[0], 0.0)).astype(np.float32)
    winner, candidates = select_mountain_region(elevation)
    return elevation, winner, candidates


def _coarse_climate_scan(world):
    begin = CLIMATE_SCAN_BEGIN
    end = CLIMATE_SCAN_END
    coarse_weighted = world.coarse[:, begin:end, begin:end]
    coarse = (
        (coarse_weighted[:-1] / coarse_weighted[-1:])
        .detach()
        .cpu()
        .numpy()
        .astype("float32", copy=True)
    )
    selections, candidates = select_climate_calibration_regions(
        coarse, require_expected=True
    )
    return coarse, selections, candidates


def _tile_overlap_validation(
    tiles: list[QueriedTile], label: str
) -> tuple[list[dict[str, object]], float]:
    overlap_records = []
    max_overlap_error = 0.0
    for first_index, first in enumerate(tiles):
        for second in tiles[first_index + 1 :]:
            count, maximum = seam_max_abs(first, second)
            if count:
                overlap_records.append(
                    {
                        "first": first.name,
                        "second": second.name,
                        "sample_count": count,
                        "max_abs_m": maximum,
                    }
                )
                max_overlap_error = max(max_overlap_error, maximum)
    if max_overlap_error > ORDER_TOLERANCE_M:
        raise RuntimeError(
            f"{label} tile overlaps differ by {max_overlap_error:.9g} m"
        )
    return overlap_records, max_overlap_error


def _generate_climate_region(
    world,
    torch,
    selection: ClimateRegimeSelection,
) -> dict[str, object]:
    generation_start = time.perf_counter()
    candidate = selection.candidate
    native_i = candidate.coarse_i * COARSE_CELL_NATIVE_SAMPLES
    native_j = candidate.coarse_j * COARSE_CELL_NATIVE_SAMPLES
    requests = _tile_requests(native_i, native_j)
    tiles = [
        _query_tile(world, torch, name, bounds, True) for name, bounds in requests
    ]
    elevation, climate = _stitch_tiles(tiles, with_climate=True)
    overlaps, _ = _tile_overlap_validation(tiles, selection.regime.name)
    return {
        "seed": ORDER_CHECK_SEED,
        "selection": selection,
        "model_native_origin": {"i": native_i, "j": native_j},
        "elevation": elevation,
        "climate": climate,
        "tiles": tiles,
        "overlaps": overlaps,
        "total_seconds": time.perf_counter() - generation_start,
    }


def _generate_seed(
    world, torch, seed: int, verify_order: bool, with_climate: bool = True
) -> dict[str, object]:
    import numpy as np

    seed_start = time.perf_counter()
    selection_start = time.perf_counter()
    coarse, winner, candidates = _coarse_selection(world)
    _cuda_sync(torch)
    selection_seconds = time.perf_counter() - selection_start
    native_i = winner.coarse_i * COARSE_CELL_NATIVE_SAMPLES
    native_j = winner.coarse_j * COARSE_CELL_NATIVE_SAMPLES
    requests = _tile_requests(native_i, native_j)
    tiles = [
        _query_tile(world, torch, name, bounds, with_climate) for name, bounds in requests
    ]
    elevation, climate = _stitch_tiles(tiles, with_climate)

    overlap_records, _ = _tile_overlap_validation(tiles, f"seed {seed}")

    order_check = None
    if verify_order:
        _seed_process_rngs(torch, seed)
        world.rebuild()
        reverse_tiles_by_name: dict[str, QueriedTile] = {}
        for name, bounds in reversed(requests):
            reverse_tiles_by_name[name] = _query_tile(world, torch, name, bounds, False)
        maximum = 0.0
        tile_errors = []
        for tile in tiles:
            reverse = reverse_tiles_by_name[tile.name]
            difference = np.abs(
                tile.elevation.astype(np.float64) - reverse.elevation.astype(np.float64)
            )
            tile_maximum = float(np.max(difference, initial=0.0))
            maximum = max(maximum, tile_maximum)
            tile_errors.append({"tile": tile.name, "max_abs_m": tile_maximum})
        order_check = {
            "forward_order": [name for name, _ in requests],
            "reverse_order": [name for name, _ in reversed(requests)],
            "tolerance_m": ORDER_TOLERANCE_M,
            "max_abs_m": maximum,
            "tiles": tile_errors,
            "passed": maximum <= ORDER_TOLERANCE_M,
        }
        if maximum > ORDER_TOLERANCE_M:
            raise RuntimeError(
                f"seed {seed} reverse-order output differs by {maximum:.9g} m"
            )

    return {
        "seed": seed,
        "coarse": coarse,
        "winner": winner,
        "candidates": candidates,
        "model_native_origin": {"i": native_i, "j": native_j},
        "elevation": elevation,
        "climate": climate,
        "tiles": tiles,
        "overlaps": overlap_records,
        "order_check": order_check,
        "selection_seconds": selection_seconds,
        "total_seconds": time.perf_counter() - seed_start,
    }


def _field_stats(values) -> dict[str, float]:
    import numpy as np

    array = np.asarray(values, dtype=np.float64)
    p01, p05, p25, p50, p75, p95, p99 = np.percentile(
        array, (1.0, 5.0, 25.0, 50.0, 75.0, 95.0, 99.0)
    )
    return {
        "min": float(np.min(array)),
        "p01": float(p01),
        "p05": float(p05),
        "p25": float(p25),
        "p50": float(p50),
        "p75": float(p75),
        "p95": float(p95),
        "p99": float(p99),
        "max": float(np.max(array)),
        "mean": float(np.mean(array)),
        "stddev": float(np.std(array)),
    }


def _write_climate_calibration_region(
    root: Path,
    generated: dict[str, object],
    common_source: dict[str, object],
) -> dict[str, object]:
    import numpy as np

    selection: ClimateRegimeSelection = generated["selection"]
    region_dir = root / selection.regime.name
    region_dir.mkdir(parents=True, exist_ok=True)

    elevation = np.asarray(generated["elevation"], dtype=np.float32)
    climate = area_average_field(
        normalize_climate_channels(generated["climate"]),
        SURFACE_STUDY_DOWNSAMPLE,
    )
    rendered = (
        (elevation + DEFAULT_ASSET_HEIGHT_OFFSET_M) * DEFAULT_ASSET_HEIGHT_SCALE
    ).astype(np.float32)
    elevation_file = write_f32(region_dir / "elevation.f32", elevation)
    climate_file = write_f32(region_dir / "climate.f32", climate)

    if selection.regime.name == "hot-dry":
        if elevation_file["sha256"] != DEFAULT_ASSET_ELEVATION_SHA256:
            raise RuntimeError(
                "hot-dry control elevation changed: expected "
                f"{DEFAULT_ASSET_ELEVATION_SHA256}, got {elevation_file['sha256']}"
            )
        if climate_file["sha256"] != DEFAULT_ASSET_CLIMATE_SHA256:
            raise RuntimeError(
                "hot-dry control climate changed: expected "
                f"{DEFAULT_ASSET_CLIMATE_SHA256}, got {climate_file['sha256']}"
            )

    heightfield = climate_calibration_heightfield_manifest(
        elevation_file, generated, common_source
    )
    surface_fields = climate_calibration_surface_manifest(
        climate_file, elevation_file["sha256"], generated, common_source
    )
    heightfield_path = region_dir / "heightfield.json"
    surface_fields_path = region_dir / "surface-fields.json"
    heightfield_path.write_text(
        json.dumps(heightfield, indent=2, sort_keys=True, allow_nan=False) + "\n"
    )
    surface_fields_path.write_text(
        json.dumps(surface_fields, indent=2, sort_keys=True, allow_nan=False) + "\n"
    )

    preview_step = 4
    _write_rgb8(
        region_dir / "height.png", height_preview_rgb(rendered[::preview_step, ::preview_step])
    )
    gradient_z, gradient_x = np.gradient(
        rendered.astype(np.float64), MODEL_NATIVE_RESOLUTION_M
    )
    slope = np.sqrt(gradient_x * gradient_x + gradient_z * gradient_z)
    _write_rgb8(
        region_dir / "slope.png", slope_preview_rgb(slope[::preview_step, ::preview_step])
    )
    climate_previews = _write_calibration_climate_previews(region_dir, climate)

    tile_records = [
        {
            "name": tile.name,
            "bounds_native": list(tile.bounds),
            "seconds": tile.seconds,
        }
        for tile in generated["tiles"]
    ]
    summary = {
        "schema": CLIMATE_CALIBRATION_REGION_SCHEMA,
        "regime": selection.as_json(),
        "seed": ORDER_CHECK_SEED,
        "model_native_origin": generated["model_native_origin"],
        "files": {
            "heightfield": {
                "path": heightfield_path.name,
                "sha256": sha256_file(heightfield_path),
            },
            "surface_fields": {
                "path": surface_fields_path.name,
                "sha256": sha256_file(surface_fields_path),
            },
            "elevation": elevation_file,
            "climate": climate_file,
        },
        "previews": {
            "height": {
                "path": "height.png",
                "scale": {
                    "minimum": 0.0,
                    "maximum": COMPARISON_RELIEF_M,
                    "unit": "m",
                },
            },
            "slope": {
                "path": "slope.png",
                "scale": {"minimum": 0.0, "maximum": 1.5},
            },
            "climate": climate_previews,
        },
        "statistics": {
            "elevation_raw_m": _field_stats(elevation),
            "elevation_product_m": _field_stats(rendered),
            "slope": _field_stats(slope),
            "climate": {
                name: {"unit": unit, **_field_stats(climate[index])}
                for index, (name, unit) in enumerate(CLIMATE_CHANNELS)
            },
        },
        "generation": {
            "seconds": generated["total_seconds"],
            "tiles": tile_records,
        },
        "validation": {
            "tile_context_halo_samples": TILE_CONTEXT_HALO,
            "seam_validation_half_width_samples": SEAM_VALIDATION_HALF_WIDTH,
            "overlap_tolerance_m": ORDER_TOLERANCE_M,
            "overlaps": generated["overlaps"],
            "canonical_control": selection.regime.name == "hot-dry",
        },
    }
    summary_path = region_dir / "region-summary.json"
    summary_path.write_text(
        json.dumps(summary, indent=2, sort_keys=True, allow_nan=False) + "\n"
    )
    return {
        "name": selection.regime.name,
        "directory": selection.regime.name,
        "selection": selection.as_json(),
        "heightfield": str(heightfield_path.relative_to(root)),
        "heightfield_sha256": sha256_file(heightfield_path),
        "surface_fields": str(surface_fields_path.relative_to(root)),
        "surface_fields_sha256": sha256_file(surface_fields_path),
        "summary": str(summary_path.relative_to(root)),
        "summary_sha256": sha256_file(summary_path),
        "elevation_sha256": elevation_file["sha256"],
        "climate_sha256": climate_file["sha256"],
        "generation_seconds": generated["total_seconds"],
    }


def _directory_size(path: Path) -> int:
    return sum(item.stat().st_size for item in path.rglob("*") if item.is_file())


def _write_seed_artifacts(
    root: Path,
    generated: dict[str, object],
    calibration: dict[str, float],
    common_source: dict[str, object],
) -> dict[str, object]:
    import numpy as np

    seed = int(generated["seed"])
    seed_dir = root / "fields" / f"seed-{seed}"
    seed_dir.mkdir(parents=True, exist_ok=True)
    elevation = generated["elevation"]
    climate = normalize_climate_channels(generated["climate"])
    rendered = apply_calibration(elevation, calibration)
    elevation_file = write_f32(seed_dir / "elevation.f32", elevation)
    climate_file = write_f32(seed_dir / "climate.f32", climate)

    _write_rgb8(seed_dir / "height.png", height_preview_rgb(rendered))
    gradient_z, gradient_x = np.gradient(rendered.astype(np.float64), MODEL_NATIVE_RESOLUTION_M)
    slope = np.sqrt(gradient_x * gradient_x + gradient_z * gradient_z)
    _write_rgb8(seed_dir / "slope.png", slope_preview_rgb(slope))
    _write_selection_preview(seed_dir / "selection.png", generated["coarse"], generated["winner"])

    climate_previews = []
    for index, (name, unit) in enumerate(CLIMATE_CHANNELS):
        low, high = np.percentile(climate[index], (2.0, 98.0))
        preview = f"climate-{name}.png"
        _write_gray16(seed_dir / preview, climate[index], float(low), float(high))
        climate_previews.append(
            {"name": name, "unit": unit, "path": preview, "preview_p02": float(low), "preview_p98": float(high)}
        )

    winner: RegionCandidate = generated["winner"]
    tile_records = [
        {"name": tile.name, "bounds_native": list(tile.bounds), "seconds": tile.seconds}
        for tile in generated["tiles"]
    ]
    half_span = (FIELD_SIZE - 1) * MODEL_NATIVE_RESOLUTION_M * 0.5
    manifest = {
        "schema": "cubey.terrain.raster-study.v1",
        "source": common_source,
        "seed": seed,
        "grid": {
            "width": FIELD_SIZE,
            "height": FIELD_SIZE,
            "sample_spacing_m": MODEL_NATIVE_RESOLUTION_M,
            "sample_origin_x_m": -half_span,
            "sample_origin_z_m": -half_span,
            "axis_mapping": {"world_x": "model_j", "world_z": "model_i"},
        },
        "files": {
            "elevation": {**elevation_file, "layout": "row-major-zx", "shape": [FIELD_SIZE, FIELD_SIZE], "unit": "m"},
            "climate": {**climate_file, "layout": "channel-major-zx", "shape": [4, FIELD_SIZE, FIELD_SIZE], "channels": climate_previews},
        },
        "comparison": calibration,
        "selection": {
            "method": "fixed-grid-land-relief-v1",
            "land_threshold": LAND_THRESHOLD,
            "candidate_offsets": list(CANDIDATE_OFFSETS),
            "winner": winner.as_json(),
            "model_native_origin": generated["model_native_origin"],
            "candidates": [candidate.as_json() for candidate in generated["candidates"]],
        },
        "generation": {
            "selection_seconds": generated["selection_seconds"],
            "total_seconds": generated["total_seconds"],
            "within_five_minute_gate": generated["total_seconds"] <= 300.0,
            "tiles": tile_records,
        },
        "validation": {
            "tile_context_halo_samples": TILE_CONTEXT_HALO,
            "seam_validation_half_width_samples": SEAM_VALIDATION_HALF_WIDTH,
            "overlap_tolerance_m": ORDER_TOLERANCE_M,
            "overlaps": generated["overlaps"],
            "reverse_order": generated["order_check"],
        },
        "statistics": {
            "elevation_raw_m": _field_stats(elevation),
            "elevation_comparison_m": _field_stats(rendered),
            "slope": _field_stats(slope),
        },
        "previews": {
            "height": "height.png",
            "slope": "slope.png",
            "selection": "selection.png",
        },
    }
    manifest_path = seed_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True, allow_nan=False) + "\n")
    heightfield_manifest_path = export_study_field(seed_dir)
    return {
        "seed": seed,
        "manifest": str(manifest_path.relative_to(root)),
        "manifest_sha256": sha256_file(manifest_path),
        "heightfield_manifest": str(heightfield_manifest_path.relative_to(root)),
        "heightfield_manifest_sha256": sha256_file(heightfield_manifest_path),
        "total_seconds": generated["total_seconds"],
        "within_five_minute_gate": generated["total_seconds"] <= 300.0,
    }


def _installed_packages() -> list[str]:
    from importlib.metadata import distributions

    packages = {
        f"{name}=={distribution.version}"
        for distribution in distributions()
        if (name := distribution.metadata.get("Name"))
    }
    return sorted(packages, key=str.casefold)


def bake(reference_root: Path, output_dir: Path, data_cache: Path) -> None:
    import torch
    from huggingface_hub import snapshot_download

    reference_root = reference_root.resolve()
    output_dir = output_dir.resolve()
    data_cache = data_cache.resolve()
    if git_revision(reference_root) != CODE_REVISION:
        raise RuntimeError(
            f"terrain-diffusion checkout must be {CODE_REVISION}, got {git_revision(reference_root)}"
        )
    if not torch.cuda.is_available():
        raise RuntimeError("Terrain Diffusion bakeoff requires CUDA")

    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    torch.set_float32_matmul_precision("highest")
    torch.use_deterministic_algorithms(True, warn_only=True)

    data_record = _prepare_data_cache(reference_root, data_cache)
    snapshot_start = time.perf_counter()
    snapshot = Path(snapshot_download(repo_id=MODEL_ID, revision=MODEL_REVISION)).resolve()
    snapshot_seconds = time.perf_counter() - snapshot_start

    sys.path.insert(0, str(reference_root))
    from terrain_diffusion.inference.world_pipeline import WorldPipeline

    torch.cuda.reset_peak_memory_stats()
    pipeline_start = time.perf_counter()
    with _working_directory(data_cache):
        _seed_process_rngs(torch, SEEDS[0])
        world = WorldPipeline.from_pretrained(
            str(snapshot),
            seed=SEEDS[0],
            latents_batch_size=LATENTS_BATCH_SIZE,
            torch_compile=False,
            dtype=None,
            caching_strategy="direct",
            cache_limit=None,
            log_mode="info",
        )
        world.to("cuda")
        world.bind()
        pipeline_seconds = time.perf_counter() - pipeline_start
        if float(world.native_resolution) != MODEL_NATIVE_RESOLUTION_M:
            raise RuntimeError(
                f"expected {MODEL_NATIVE_RESOLUTION_M:g} m model, got {world.native_resolution}"
            )

        generated_fields = []
        try:
            for index, seed in enumerate(SEEDS):
                if index:
                    _seed_process_rngs(torch, seed)
                    world.change_seed(seed)
                generated_fields.append(
                    _generate_seed(world, torch, seed, verify_order=seed == ORDER_CHECK_SEED)
                )
        finally:
            world.close()

    calibration = comparison_calibration(
        [generated["elevation"] for generated in generated_fields]
    )
    stats_cache = data_cache / "data" / "global" / "synthetic_map_stats.json"
    if not stats_cache.is_file():
        raise RuntimeError("upstream synthetic-map statistics were not produced")

    environment = {
        "python": sys.version,
        "platform": platform.platform(),
        "torch": torch.__version__,
        "cuda_runtime": torch.version.cuda,
        "gpu": torch.cuda.get_device_name(0),
        "driver": torch.cuda.driver_version() if hasattr(torch.cuda, "driver_version") else None,
        "installed_packages": _installed_packages(),
    }
    common_source = {
        "id": "terrain-diffusion-30m",
        "generator": "terrain-diffusion",
        "code_revision": CODE_REVISION,
        "model_id": MODEL_ID,
        "model_revision": MODEL_REVISION,
        "model_snapshot": str(snapshot),
        "native_resolution_m": MODEL_NATIVE_RESOLUTION_M,
        "settings": {
            "device": "cuda",
            "dtype": "fp32",
            "latents_batch_size": LATENTS_BATCH_SIZE,
            "torch_compile": False,
            "caching_strategy": "direct",
            "custom_conditioning": False,
            "process_rng_seeding": "seed-value-v1",
        },
    }

    output_dir.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f"{output_dir.name}.tmp.", dir=output_dir.parent))
    try:
        summaries = [
            _write_seed_artifacts(temporary, generated, calibration, common_source)
            for generated in generated_fields
        ]
        report = {
            "schema": "cubey.terrain.diffusion-bakeoff-generation.v1",
            "export_contract_revision": EXPORT_CONTRACT_REVISION,
            "source": common_source,
            "field_contract": {
                "seeds": list(SEEDS),
                "size": [FIELD_SIZE, FIELD_SIZE],
                "sample_spacing_m": MODEL_NATIVE_RESOLUTION_M,
                "calibration": calibration,
            },
            "data": {
                **data_record,
                "synthetic_map_stats_sha256": sha256_file(stats_cache),
            },
            "timings": {
                "model_snapshot_seconds": snapshot_seconds,
                "pipeline_load_bind_seconds": pipeline_seconds,
                "seed_generation_seconds": {
                    str(generated["seed"]): generated["total_seconds"]
                    for generated in generated_fields
                },
            },
            "memory": {
                "peak_cuda_allocated_bytes": int(torch.cuda.max_memory_allocated()),
                "peak_cuda_reserved_bytes": int(torch.cuda.max_memory_reserved()),
                "peak_rss_kib": int(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss),
            },
            "environment": environment,
            "fields": summaries,
            "acceptance": {
                "determinism": True,
                "all_seeds_within_five_minutes": all(
                    summary["within_five_minute_gate"] for summary in summaries
                ),
            },
        }
        report_path = temporary / "generation-report.json"
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n")
        if output_dir.exists():
            shutil.rmtree(output_dir)
        os.replace(temporary, output_dir)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise

    print(f"terrain diffusion bakeoff: wrote {output_dir}")


def bake_default_asset(reference_root: Path, output_dir: Path, data_cache: Path) -> None:
    """Generate the pinned elevation-only field used by the terrain product."""
    import torch
    from huggingface_hub import snapshot_download

    reference_root = reference_root.resolve()
    output_dir = output_dir.resolve()
    data_cache = data_cache.resolve()
    actual_revision = git_revision(reference_root)
    if actual_revision != CODE_REVISION:
        raise RuntimeError(
            f"terrain-diffusion checkout must be {CODE_REVISION}, got {actual_revision}"
        )
    if not torch.cuda.is_available():
        raise RuntimeError("Terrain Diffusion default asset generation requires CUDA")

    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    torch.set_float32_matmul_precision("highest")
    torch.use_deterministic_algorithms(True, warn_only=True)

    _prepare_data_cache(reference_root, data_cache)
    snapshot = Path(
        snapshot_download(repo_id=MODEL_ID, revision=MODEL_REVISION)
    ).resolve()

    sys.path.insert(0, str(reference_root))
    from terrain_diffusion.inference.world_pipeline import WorldPipeline

    generation_start = time.perf_counter()
    with _working_directory(data_cache):
        _seed_process_rngs(torch, ORDER_CHECK_SEED)
        world = WorldPipeline.from_pretrained(
            str(snapshot),
            seed=ORDER_CHECK_SEED,
            latents_batch_size=LATENTS_BATCH_SIZE,
            torch_compile=False,
            dtype=None,
            caching_strategy="direct",
            cache_limit=None,
            log_mode="info",
        )
        world.to("cuda")
        world.bind()
        if float(world.native_resolution) != MODEL_NATIVE_RESOLUTION_M:
            raise RuntimeError(
                f"expected {MODEL_NATIVE_RESOLUTION_M:g} m model, got {world.native_resolution}"
            )
        try:
            generated = _generate_seed(
                world,
                torch,
                ORDER_CHECK_SEED,
                verify_order=False,
                with_climate=False,
            )
        finally:
            world.close()

    output_dir.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f"{output_dir.name}.tmp.", dir=output_dir.parent))
    try:
        elevation_file = write_f32(temporary / "elevation.f32", generated["elevation"])
        if elevation_file["sha256"] != DEFAULT_ASSET_ELEVATION_SHA256:
            raise RuntimeError(
                "default terrain elevation changed: expected "
                f"{DEFAULT_ASSET_ELEVATION_SHA256}, got {elevation_file['sha256']}"
            )

        half_span = (FIELD_SIZE - 1) * MODEL_NATIVE_RESOLUTION_M * 0.5
        manifest = {
            "schema": "cubey.terrain.heightfield.v1",
            "source": {
                "id": "terrain-diffusion-30m",
                "generator": "terrain-diffusion",
                "code_revision": CODE_REVISION,
                "model_id": MODEL_ID,
                "model_revision": MODEL_REVISION,
                "native_resolution_m": MODEL_NATIVE_RESOLUTION_M,
                "settings": {
                    "device": "cuda",
                    "dtype": "fp32",
                    "latents_batch_size": LATENTS_BATCH_SIZE,
                    "torch_compile": False,
                    "caching_strategy": "direct",
                    "custom_conditioning": False,
                    "process_rng_seeding": "seed-value-v1",
                    "climate_output": False,
                },
            },
            "seed": ORDER_CHECK_SEED,
            "grid": {
                "width": FIELD_SIZE,
                "height": FIELD_SIZE,
                "sample_spacing_m": MODEL_NATIVE_RESOLUTION_M,
                "sample_origin_x_m": -half_span,
                "sample_origin_z_m": -half_span,
                "axis_mapping": {"world_x": "model_j", "world_z": "model_i"},
            },
            "height": {
                "offset_m": DEFAULT_ASSET_HEIGHT_OFFSET_M,
                "scale": DEFAULT_ASSET_HEIGHT_SCALE,
                "relief_scale_m": COMPARISON_RELIEF_M,
            },
            "files": {
                "elevation": {
                    **elevation_file,
                    "layout": "row-major-zx",
                    "shape": [FIELD_SIZE, FIELD_SIZE],
                    "unit": "m",
                }
            },
            "provenance": {
                "purpose": "cubey-terrain-default-backdrop-v1",
                "selection": "fixed-grid-land-relief-v1",
                "model_native_origin": generated["model_native_origin"],
                "generation_seconds": time.perf_counter() - generation_start,
            },
        }
        (temporary / "heightfield.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True, allow_nan=False) + "\n"
        )
        if output_dir.exists():
            shutil.rmtree(output_dir)
        os.replace(temporary, output_dir)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise

    print(f"terrain default asset: wrote {output_dir}")


def bake_surface_study_asset(reference_root: Path, output_dir: Path, data_cache: Path) -> None:
    """Generate the climate companion bound to the canonical terrain heightfield."""
    import torch
    from huggingface_hub import snapshot_download

    reference_root = reference_root.resolve()
    output_dir = output_dir.resolve()
    data_cache = data_cache.resolve()
    actual_revision = git_revision(reference_root)
    if actual_revision != CODE_REVISION:
        raise RuntimeError(
            f"terrain-diffusion checkout must be {CODE_REVISION}, got {actual_revision}"
        )
    if not torch.cuda.is_available():
        raise RuntimeError("Terrain Diffusion surface study generation requires CUDA")

    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    torch.set_float32_matmul_precision("highest")
    torch.use_deterministic_algorithms(True, warn_only=True)

    _prepare_data_cache(reference_root, data_cache)
    snapshot = Path(
        snapshot_download(repo_id=MODEL_ID, revision=MODEL_REVISION)
    ).resolve()

    sys.path.insert(0, str(reference_root))
    from terrain_diffusion.inference.world_pipeline import WorldPipeline

    generation_start = time.perf_counter()
    with _working_directory(data_cache):
        _seed_process_rngs(torch, ORDER_CHECK_SEED)
        world = WorldPipeline.from_pretrained(
            str(snapshot),
            seed=ORDER_CHECK_SEED,
            latents_batch_size=LATENTS_BATCH_SIZE,
            torch_compile=False,
            dtype=None,
            caching_strategy="direct",
            cache_limit=None,
            log_mode="info",
        )
        world.to("cuda")
        world.bind()
        if float(world.native_resolution) != MODEL_NATIVE_RESOLUTION_M:
            raise RuntimeError(
                f"expected {MODEL_NATIVE_RESOLUTION_M:g} m model, got {world.native_resolution}"
            )
        try:
            generated = _generate_seed(
                world,
                torch,
                ORDER_CHECK_SEED,
                verify_order=False,
                with_climate=True,
            )
        finally:
            world.close()

    if sha256_f32(generated["elevation"]) != DEFAULT_ASSET_ELEVATION_SHA256:
        raise RuntimeError("surface study elevation does not match the canonical terrain asset")
    climate = area_average_field(
        normalize_climate_channels(generated["climate"]), SURFACE_STUDY_DOWNSAMPLE
    )

    output_dir.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f"{output_dir.name}.tmp.", dir=output_dir.parent))
    try:
        climate_file = write_f32(temporary / "climate.f32", climate)
        manifest = surface_study_manifest(
            climate_file, generated, time.perf_counter() - generation_start
        )
        (temporary / "surface-fields.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True, allow_nan=False) + "\n"
        )
        if output_dir.exists():
            shutil.rmtree(output_dir)
        os.replace(temporary, output_dir)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise

    print(f"terrain surface study asset: wrote {output_dir}")


def bake_climate_calibration_assets(
    reference_root: Path, output_dir: Path, data_cache: Path
) -> None:
    """Generate deterministic cross-climate terrain calibration regions."""
    import torch
    from huggingface_hub import snapshot_download

    reference_root = reference_root.resolve()
    output_dir = output_dir.resolve()
    data_cache = data_cache.resolve()
    actual_revision = git_revision(reference_root)
    if actual_revision != CODE_REVISION:
        raise RuntimeError(
            f"terrain-diffusion checkout must be {CODE_REVISION}, got {actual_revision}"
        )
    if not torch.cuda.is_available():
        raise RuntimeError("Terrain climate calibration generation requires CUDA")

    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    torch.set_float32_matmul_precision("highest")
    torch.use_deterministic_algorithms(True, warn_only=True)

    _prepare_data_cache(reference_root, data_cache)
    snapshot = Path(
        snapshot_download(repo_id=MODEL_ID, revision=MODEL_REVISION)
    ).resolve()

    sys.path.insert(0, str(reference_root))
    from terrain_diffusion.inference.world_pipeline import WorldPipeline

    with _working_directory(data_cache):
        _seed_process_rngs(torch, ORDER_CHECK_SEED)
        world = WorldPipeline.from_pretrained(
            str(snapshot),
            seed=ORDER_CHECK_SEED,
            latents_batch_size=LATENTS_BATCH_SIZE,
            torch_compile=False,
            dtype=None,
            caching_strategy="direct",
            cache_limit=None,
            log_mode="info",
        )
        world.to("cuda")
        world.bind()
        if float(world.native_resolution) != MODEL_NATIVE_RESOLUTION_M:
            raise RuntimeError(
                f"expected {MODEL_NATIVE_RESOLUTION_M:g} m model, got {world.native_resolution}"
            )

        generation_start = time.perf_counter()
        scan_start = time.perf_counter()
        coarse, selections, candidates = _coarse_climate_scan(world)
        _cuda_sync(torch)
        scan_seconds = time.perf_counter() - scan_start
        try:
            generated_regions = [
                _generate_climate_region(world, torch, selection)
                for selection in selections
            ]
        finally:
            world.close()

    common_source = {
        "id": "terrain-diffusion-30m",
        "generator": "terrain-diffusion",
        "code_revision": CODE_REVISION,
        "model_id": MODEL_ID,
        "model_revision": MODEL_REVISION,
        "model_snapshot": str(snapshot),
        "native_resolution_m": MODEL_NATIVE_RESOLUTION_M,
        "settings": {
            "device": "cuda",
            "dtype": "fp32",
            "latents_batch_size": LATENTS_BATCH_SIZE,
            "torch_compile": False,
            "caching_strategy": "direct",
            "custom_conditioning": False,
            "process_rng_seeding": "seed-value-v1",
            "climate_output": True,
        },
    }

    output_dir.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(
        tempfile.mkdtemp(prefix=f"{output_dir.name}.tmp.", dir=output_dir.parent)
    )
    try:
        region_summaries = [
            _write_climate_calibration_region(
                temporary, generated, common_source
            )
            for generated in generated_regions
        ]
        generation_seconds = time.perf_counter() - generation_start
        index = {
            "schema": CLIMATE_CALIBRATION_SCHEMA,
            "source": common_source,
            "seed": ORDER_CHECK_SEED,
            "scan": {
                "coarse_bounds": {
                    "begin_i": CLIMATE_SCAN_BEGIN,
                    "begin_j": CLIMATE_SCAN_BEGIN,
                    "end_i": CLIMATE_SCAN_END,
                    "end_j": CLIMATE_SCAN_END,
                },
                "shape": list(coarse.shape),
                "sample_spacing_m": CLIMATE_MACRO_SPACING_M,
                "window_cells": COARSE_WINDOW_CELLS,
                "window_extent_m": FIELD_SIZE * MODEL_NATIVE_RESOLUTION_M,
                "minimum_land_fraction": LAND_THRESHOLD,
                "minimum_relief_m": CLIMATE_MIN_RELIEF_M,
                "selection_seconds": scan_seconds,
                "candidate_count": len(candidates),
                "candidates": [candidate.as_json() for candidate in candidates],
            },
            "selection": {
                "method": "fixed-seed-climate-regime-v1",
                "temperature_distance_scale_c": 8.0,
                "precipitation_distance_scale_log10": 0.45,
                "canonical_control": "hot-dry",
                "expected_origins": {
                    name: {"coarse_i": origin[0], "coarse_j": origin[1]}
                    for name, origin in CLIMATE_EXPECTED_ORIGINS.items()
                },
                "regions": [selection.as_json() for selection in selections],
            },
            "field_contract": {
                "elevation_size": [FIELD_SIZE, FIELD_SIZE],
                "elevation_spacing_m": MODEL_NATIVE_RESOLUTION_M,
                "climate_size": [SURFACE_STUDY_SIZE, SURFACE_STUDY_SIZE],
                "climate_spacing_m": MODEL_NATIVE_RESOLUTION_M
                * SURFACE_STUDY_DOWNSAMPLE,
                "height_offset_m": DEFAULT_ASSET_HEIGHT_OFFSET_M,
                "height_scale": DEFAULT_ASSET_HEIGHT_SCALE,
                "target_relief_m": COMPARISON_RELIEF_M,
            },
            "regions": region_summaries,
            "validation": {
                "canonical_elevation_sha256": DEFAULT_ASSET_ELEVATION_SHA256,
                "canonical_climate_sha256": DEFAULT_ASSET_CLIMATE_SHA256,
                "all_expected_origins_selected": True,
                "generation_seconds": generation_seconds,
                "generation_limit_seconds": CLIMATE_CALIBRATION_GENERATION_LIMIT_SECONDS,
                "within_generation_limit": generation_seconds
                <= CLIMATE_CALIBRATION_GENERATION_LIMIT_SECONDS,
                "package_limit_bytes": CLIMATE_CALIBRATION_PACKAGE_LIMIT_BYTES,
                "package_bytes": 0,
                "within_package_limit": True,
            },
        }
        index_path = temporary / "calibration-index.json"
        for _ in range(8):
            index_path.write_text(
                json.dumps(index, indent=2, sort_keys=True, allow_nan=False) + "\n"
            )
            package_bytes = _directory_size(temporary)
            if index["validation"]["package_bytes"] == package_bytes:
                break
            index["validation"]["package_bytes"] = package_bytes
        else:
            raise RuntimeError("climate calibration package size did not stabilize")

        package_bytes = _directory_size(temporary)
        if package_bytes > CLIMATE_CALIBRATION_PACKAGE_LIMIT_BYTES:
            raise RuntimeError(
                f"climate calibration package is {package_bytes} bytes, over the "
                f"{CLIMATE_CALIBRATION_PACKAGE_LIMIT_BYTES}-byte limit"
            )
        if generation_seconds > CLIMATE_CALIBRATION_GENERATION_LIMIT_SECONDS:
            raise RuntimeError(
                f"climate calibration generation took {generation_seconds:.3f} seconds, "
                f"over the {CLIMATE_CALIBRATION_GENERATION_LIMIT_SECONDS:.0f}-second limit"
            )
        if output_dir.exists():
            shutil.rmtree(output_dir)
        os.replace(temporary, output_dir)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise

    print(f"terrain climate calibration assets: wrote {output_dir}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--reference-root",
        type=Path,
        default=Path.home() / "code" / "ref" / "terrain-diffusion",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("outputs/terrain/terrain-diffusion-bakeoff-v1/generated"),
    )
    parser.add_argument(
        "--data-cache",
        type=Path,
        default=Path("cache/terrain/tooling/v1/terrain-diffusion-data"),
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="regenerate even when the complete pinned output bundle validates",
    )
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help=argparse.SUPPRESS,
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--default-asset",
        action="store_true",
        help="generate only the canonical seed-0 runtime heightfield",
    )
    mode.add_argument(
        "--surface-study-asset",
        action="store_true",
        help="generate the seed-0 climate companion for the surface semantics study",
    )
    mode.add_argument(
        "--climate-calibration-assets",
        action="store_true",
        help="generate five deterministic cross-climate calibration regions",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    mode = (
        "default"
        if args.default_asset
        else "surface-study"
        if args.surface_study_asset
        else "climate-calibration"
        if args.climate_calibration_assets
        else ""
    )
    if args.validate_only:
        if mode and validate_existing_asset(mode, args.output_dir):
            print(f"terrain diffusion {mode}: reused {args.output_dir.resolve()}")
            return 0
        return 1
    if mode and not args.force and validate_existing_asset(mode, args.output_dir):
        print(f"terrain diffusion {mode}: reused {args.output_dir.resolve()}")
        return 0
    if args.default_asset:
        bake_default_asset(args.reference_root, args.output_dir, args.data_cache)
    elif args.surface_study_asset:
        bake_surface_study_asset(args.reference_root, args.output_dir, args.data_cache)
    elif args.climate_calibration_assets:
        bake_climate_calibration_assets(
            args.reference_root, args.output_dir, args.data_cache
        )
    else:
        bake(args.reference_root, args.output_dir, args.data_cache)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
