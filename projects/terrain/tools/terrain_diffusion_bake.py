#!/usr/bin/env python3
"""Bake pinned Terrain Diffusion worlds into Cubey raster-study fields."""

from __future__ import annotations

import argparse
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
EXPORT_CONTRACT_REVISION = 4
SURFACE_STUDY_SCHEMA = "cubey.terrain.surface-fields.study.v1"
SURFACE_STUDY_DOWNSAMPLE = 8
SURFACE_STUDY_SIZE = FIELD_SIZE // SURFACE_STUDY_DOWNSAMPLE
WORLDCLIM_URL = "https://geodata.ucdavis.edu/climate/worldclim/2_1/base/wc2.1_10m_bio.zip"
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
            urllib.request.urlretrieve(WORLDCLIM_URL, temporary)
            download_seconds = time.perf_counter() - start
            os.replace(temporary, archive)
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
        "worldclim_archive_sha256": sha256_file(archive),
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
            f"seed {seed} tile overlaps differ by {max_overlap_error:.9g} m"
        )

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
        default=Path("outputs/terrain/.terrain-diffusion-data"),
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
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.default_asset:
        bake_default_asset(args.reference_root, args.output_dir, args.data_cache)
    elif args.surface_study_asset:
        bake_surface_study_asset(args.reference_root, args.output_dir, args.data_cache)
    else:
        bake(args.reference_root, args.output_dir, args.data_cache)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
