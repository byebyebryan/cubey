"""Shared, read-only LOD0 raster sampling for derived simulation grids.

World X/Z and row-major Z/X match TerrainRasterHeightSource. Resampling never
edits the source or invents detail; callers own their derived artifacts.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path

import numpy as np

GRID_SCHEMA = "cubey.terrain.sampled-grid.v1"


def sha256(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            value.update(block)
    return value.hexdigest()


def load_source(path: Path) -> tuple[dict, np.ndarray]:
    path = path.resolve(strict=True)
    if path.stat().st_size > 65536:
        raise ValueError("oversized heightfield manifest")
    manifest = json.loads(path.read_text())
    meta, grid = manifest["files"]["elevation"], manifest["grid"]
    relative = Path(meta["path"])
    payload = path.parent / relative
    if (manifest["schema"] != "cubey.terrain.heightfield.v1" or relative.is_absolute()
            or ".." in relative.parts or payload.is_symlink()
            or not payload.resolve().is_relative_to(path.parent)
            or meta["dtype"] != "float32-le" or meta["layout"] != "row-major-zx"
            or meta["shape"] != [grid["height"], grid["width"]]
            or payload.stat().st_size != grid["width"] * grid["height"] * 4
            or sha256(payload) != meta["sha256"]):
        raise ValueError("invalid heightfield source/payload identity")
    values = np.fromfile(payload, dtype="<f4").reshape(grid["height"], grid["width"])
    if not np.isfinite(values).all():
        raise ValueError("nonfinite source elevation")
    return manifest, values


def grid_geometry(width: int, height: int, spacing: float,
                  center: tuple[float, float], bounds: tuple[float, float, float, float]) -> dict:
    if (type(width) is not int or type(height) is not int or len(center) != 2 or len(bounds) != 4
            or not 2 <= width <= 16384 or not 2 <= height <= 16384
            or width * height > 4194304 or not math.isfinite(spacing) or spacing <= 0
            or not all(math.isfinite(v) for v in (*center, *bounds))):
        raise ValueError("invalid target grid")
    half_x, half_z = (width - 1) * spacing / 2, (height - 1) * spacing / 2
    if bounds[2] - bounds[0] < 2 * half_x or bounds[3] - bounds[1] < 2 * half_z:
        raise ValueError("target grid exceeds source coverage")
    # Larger crops may shift at source borders; they need not be concentric.
    cx = min(max(center[0], bounds[0] + half_x), bounds[2] - half_x)
    cz = min(max(center[1], bounds[1] + half_z), bounds[3] - half_z)
    return {"schema": GRID_SCHEMA, "width": width, "height": height,
            "spacing_m": spacing, "sample_origin_xz_m": [cx - half_x, cz - half_z],
            "center_xz_m": [cx, cz], "footprint_m": [width * spacing, height * spacing],
            "sampling": "bilinear-lod0", "orientation": "row-major-x-fastest-z-rows"}


def sample_grid(manifest: dict, raw: np.ndarray, target: dict) -> np.ndarray:
    grid = manifest["grid"]
    spacing = np.float32(grid["sample_spacing_m"])
    origin = target["sample_origin_xz_m"]
    x = np.float32(origin[0]) + np.arange(target["width"], dtype=np.float32) * np.float32(target["spacing_m"])
    z = np.float32(origin[1]) + np.arange(target["height"], dtype=np.float32) * np.float32(target["spacing_m"])
    sx = (x - np.float32(grid["sample_origin_x_m"])) / spacing
    sz = (z - np.float32(grid["sample_origin_z_m"])) / spacing
    if sx.min() < 0 or sz.min() < 0 or sx.max() > raw.shape[1] - 1 or sz.max() > raw.shape[0] - 1:
        raise ValueError("target samples outside source")
    ix, iz = np.floor(sx).astype(int), np.floor(sz).astype(int)
    jx, jz = np.minimum(ix + 1, raw.shape[1] - 1), np.minimum(iz + 1, raw.shape[0] - 1)
    tx, tz = sx - ix.astype(np.float32), (sz - iz.astype(np.float32))[:, None]
    # Raw interpolation precedes float32 offset/scale, matching the shared C++ source.
    a = raw[iz[:, None], ix] * (1 - tx) + raw[iz[:, None], jx] * tx
    b = raw[jz[:, None], ix] * (1 - tx) + raw[jz[:, None], jx] * tx
    values = a * (1 - tz) + b * tz
    return ((values + np.float32(manifest["height"]["offset_m"]))
            * np.float32(manifest["height"]["scale"])).astype("<f4")
