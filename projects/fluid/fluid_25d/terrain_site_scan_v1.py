#!/usr/bin/env python3

"""Inventory cached Terrain Diffusion rasters and rank untouched valley sites.

This is an offline discovery tool. Its elevation-only morphology outputs are
review evidence; it never writes terrain data or creates Fluid solver inputs.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage
from skimage.morphology import skeletonize


SCHEMA = "cubey.fluid25d.terrain_site_scan.v1"
PINNED_CODE_REVISION = "82a0431281f21a6ec3d691a12ee61525de5b0790"
PINNED_MODEL_ID = "xandergos/terrain-diffusion-30m"
PINNED_MODEL_REVISION = "9ef8030cb805b433b98ec25c5dddefbac07a9e26"
SOURCE_ROOT_RELATIVE = Path("cache/terrain/sources/v1")
FIELD_SHAPE = (2048, 2048)
NATIVE_SPACING_M = 30.0
CROP_WIDTH = 512
CROP_HEIGHT = 256
CROP_EXTENT_M = (CROP_WIDTH * NATIVE_SPACING_M, CROP_HEIGHT * NATIVE_SPACING_M)
SCAN_STRIDE_X = 128
SCAN_STRIDE_Z = 64
ANALYSIS_DOWNSAMPLE = 4
ANALYSIS_SPACING_M = NATIVE_SPACING_M * ANALYSIS_DOWNSAMPLE
MORPHOLOGY_CLOSING_SIZE = 9
MORPHOLOGY_MARGIN = MORPHOLOGY_CLOSING_SIZE // 2
DEEP_VALLEY_THRESHOLD_M = 100.0
MIN_DEEP_COMPONENT_PIXELS = 8
DEFAULT_SHORTLIST_SIZE = 6
DEFAULT_OUTPUT_RELATIVE = Path("outputs/fluid/terrain-site-scan-v1-20260923")

EIGHT_CONNECTED = np.ones((3, 3), dtype=np.uint8)
NEIGHBOR_KERNEL = np.ones((3, 3), dtype=np.uint8)
NEIGHBOR_KERNEL[1, 1] = 0


# Identities and 256x128 native crops retained by terrain-water audition V2.
# Exclusion uses the elevation payload SHA, so aliases of one raster cannot
# bypass this list.
PREVIOUS_V2_CROPS = (
    {
        "id": "mountain-valley-floodplain",
        "elevation_sha256": "2a919b516d8ae4fb8c193cdd8db1a8ba055ba702e5cbd1ff50ad2b7fc6ab3c48",
        "x": 1536,
        "z": 1664,
        "width": 256,
        "height": 128,
    },
    {
        "id": "rolling-hills-catchment",
        "elevation_sha256": "00fc8836855c52caba7d9b114b00d8ba426d0b9f716825f7e19c42f2e273c28d",
        "x": 128,
        "z": 1728,
        "width": 256,
        "height": 128,
    },
    {
        "id": "rolling-lowland-branching",
        "elevation_sha256": "fd52d7c3b25f139ac709b8ced673ccc77d749cc33e4c52e66745494a86dcf7a2",
        "x": 1216,
        "z": 1664,
        "width": 256,
        "height": 128,
    },
    {
        "id": "canyon-one-flash",
        "elevation_sha256": "4c6cda32de46801ca52b5edc37a925a947438de59537e55ec7b08c8883f68b51",
        "x": 1344,
        "z": 320,
        "width": 256,
        "height": 128,
    },
    {
        "id": "canyon-four-basin-hypothesis",
        "elevation_sha256": "88cc6c1fdacbd9759f90e6781ddf4ff570e4516fb0652438dc28f7ad23086d8b",
        "x": 640,
        "z": 128,
        "width": 256,
        "height": 128,
    },
)


@dataclass(frozen=True)
class ManifestRecord:
    manifest_path: Path
    elevation_path: Path
    manifest_sha256: str
    elevation_sha256: str
    width: int
    height: int
    spacing_m: float
    seed: int
    source_id: str
    source_generator: str
    code_revision: str
    model_id: str
    model_revision: str
    native_resolution_m: float
    variant: str


@dataclass(frozen=True)
class TerrainAsset:
    elevation_sha256: str
    canonical: ManifestRecord
    aliases: tuple[ManifestRecord, ...]


@dataclass(frozen=True)
class Candidate:
    elevation_sha256: str
    manifest_path: str
    alias_manifest_paths: tuple[str, ...]
    variant: str
    seed: int
    x: int
    z: int
    score: float
    deep_valley_fraction: float
    largest_component_fraction: float
    deep_component_count: int
    branch_junction_count: int
    p95_valley_depth_m: float
    maximum_valley_depth_m: float
    valley_edge_fraction: float

    def record(self, rank: int | None = None) -> dict[str, Any]:
        result = {
            "rank": rank,
            "elevation_sha256": self.elevation_sha256,
            "manifest_path": self.manifest_path,
            "alias_manifest_paths": list(self.alias_manifest_paths),
            "variant": self.variant,
            "seed": self.seed,
            "crop": {
                "x": self.x,
                "z": self.z,
                "width": CROP_WIDTH,
                "height": CROP_HEIGHT,
                "sample_spacing_m": NATIVE_SPACING_M,
                "extent_m": {
                    "x": CROP_EXTENT_M[0],
                    "z": CROP_EXTENT_M[1],
                },
            },
            "metrics": {
                "connected_branch_score": self.score,
                "deep_valley_fraction": self.deep_valley_fraction,
                "largest_component_fraction": self.largest_component_fraction,
                "deep_component_count": self.deep_component_count,
                "branch_junction_count": self.branch_junction_count,
                "p95_valley_depth_m": self.p95_valley_depth_m,
                "maximum_valley_depth_m": self.maximum_valley_depth_m,
                "valley_edge_fraction": self.valley_edge_fraction,
            },
        }
        return result


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _require_int(value: object, label: str, minimum: int = 1) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ValueError(f"{label} must be an integer >= {minimum}")
    return value


def load_manifest_record(
    manifest_path: Path,
    expected_shape: tuple[int, int] = FIELD_SHAPE,
) -> ManifestRecord:
    manifest_path = manifest_path.resolve()
    document = json.loads(manifest_path.read_text())
    if not isinstance(document, dict) or document.get("schema") != "cubey.terrain.heightfield.v1":
        raise ValueError(f"unsupported heightfield manifest: {manifest_path}")

    source = document.get("source")
    grid = document.get("grid")
    files = document.get("files")
    if not isinstance(source, dict) or not isinstance(grid, dict) or not isinstance(files, dict):
        raise ValueError(f"heightfield provenance/grid/files are incomplete: {manifest_path}")
    if source.get("generator") != "terrain-diffusion":
        raise ValueError(f"heightfield is not a Terrain Diffusion asset: {manifest_path}")
    if (
        source.get("code_revision") != PINNED_CODE_REVISION
        or source.get("model_id") != PINNED_MODEL_ID
        or source.get("model_revision") != PINNED_MODEL_REVISION
    ):
        raise ValueError(f"heightfield producer identity does not match the pinned source: {manifest_path}")
    native_resolution_m = float(source.get("native_resolution_m", math.nan))
    spacing_m = float(grid.get("sample_spacing_m", math.nan))
    if native_resolution_m != NATIVE_SPACING_M or spacing_m != NATIVE_SPACING_M:
        raise ValueError(f"heightfield is not native 30 m data: {manifest_path}")
    if grid.get("axis_mapping") != {"world_x": "model_j", "world_z": "model_i"}:
        raise ValueError(f"heightfield axis mapping is incompatible: {manifest_path}")

    width = _require_int(grid.get("width"), "grid width")
    height = _require_int(grid.get("height"), "grid height")
    if (height, width) != expected_shape:
        raise ValueError(
            f"heightfield shape {(height, width)} does not match {expected_shape}: {manifest_path}"
        )
    elevation = files.get("elevation")
    if not isinstance(elevation, dict):
        raise ValueError(f"elevation payload record is missing: {manifest_path}")
    if (
        elevation.get("dtype") != "float32-le"
        or elevation.get("layout") != "row-major-zx"
        or elevation.get("shape") != [height, width]
        or elevation.get("unit") != "m"
        or elevation.get("byte_count") != height * width * 4
    ):
        raise ValueError(f"elevation payload contract is incompatible: {manifest_path}")
    payload_name = elevation.get("path")
    if payload_name != "elevation.f32":
        raise ValueError(f"unexpected elevation payload path: {manifest_path}")
    elevation_path = manifest_path.parent / payload_name
    if not elevation_path.is_file() or elevation_path.stat().st_size != height * width * 4:
        raise ValueError(f"elevation payload size is missing or invalid: {elevation_path}")

    expected_hash = elevation.get("sha256")
    if not isinstance(expected_hash, str) or len(expected_hash) != 64:
        raise ValueError(f"elevation payload SHA-256 is invalid: {manifest_path}")
    actual_hash = sha256_file(elevation_path)
    if actual_hash != expected_hash:
        raise ValueError(f"elevation payload SHA-256 disagrees with manifest: {manifest_path}")

    provenance = document.get("provenance", {})
    if not isinstance(provenance, dict):
        raise ValueError(f"heightfield provenance is invalid: {manifest_path}")
    seed = document.get("seed")
    if isinstance(seed, bool) or not isinstance(seed, int) or seed < 0:
        raise ValueError(f"heightfield seed is invalid: {manifest_path}")
    return ManifestRecord(
        manifest_path=manifest_path,
        elevation_path=elevation_path.resolve(),
        manifest_sha256=sha256_file(manifest_path),
        elevation_sha256=actual_hash,
        width=width,
        height=height,
        spacing_m=spacing_m,
        seed=seed,
        source_id=str(source.get("id", "")),
        source_generator=str(source.get("generator", "")),
        code_revision=str(source.get("code_revision", "")),
        model_id=str(source.get("model_id", "")),
        model_revision=str(source.get("model_revision", "")),
        native_resolution_m=native_resolution_m,
        variant=str(provenance.get("landscape_variant", "")),
    )


def discover_manifests(source_root: Path) -> list[ManifestRecord]:
    manifests = sorted(source_root.rglob("heightfield.json"), key=lambda path: path.as_posix())
    if not manifests:
        raise FileNotFoundError(f"no cached heightfield manifests under {source_root}")
    records = [load_manifest_record(path) for path in manifests]
    return sorted(records, key=lambda item: item.manifest_path.as_posix())


def deduplicate_assets(records: list[ManifestRecord]) -> list[TerrainAsset]:
    grouped: dict[str, list[ManifestRecord]] = {}
    for record in records:
        grouped.setdefault(record.elevation_sha256, []).append(record)
    assets = []
    for digest, aliases in sorted(grouped.items()):
        aliases = sorted(aliases, key=lambda item: item.manifest_path.as_posix())
        canonical = aliases[0]
        for alias in aliases[1:]:
            if (
                (alias.width, alias.height, alias.spacing_m)
                != (canonical.width, canonical.height, canonical.spacing_m)
            ):
                raise ValueError(f"identical elevation payload has conflicting grid metadata: {digest}")
        assets.append(TerrainAsset(digest, canonical, tuple(aliases)))
    return assets


def candidate_origins(
    width: int,
    height: int,
    crop_width: int = CROP_WIDTH,
    crop_height: int = CROP_HEIGHT,
    stride_x: int = SCAN_STRIDE_X,
    stride_z: int = SCAN_STRIDE_Z,
) -> list[tuple[int, int]]:
    if width < crop_width or height < crop_height or stride_x <= 0 or stride_z <= 0:
        return []
    return [
        (x, z)
        for z in range(0, height - crop_height + 1, stride_z)
        for x in range(0, width - crop_width + 1, stride_x)
    ]


def rectangles_overlap(
    ax: int,
    az: int,
    aw: int,
    ah: int,
    bx: int,
    bz: int,
    bw: int,
    bh: int,
) -> bool:
    return ax < bx + bw and bx < ax + aw and az < bz + bh and bz < az + ah


def overlaps_previous_v2(elevation_sha256: str, x: int, z: int) -> bool:
    return any(
        previous["elevation_sha256"] == elevation_sha256
        and rectangles_overlap(
            x,
            z,
            CROP_WIDTH,
            CROP_HEIGHT,
            previous["x"],
            previous["z"],
            previous["width"],
            previous["height"],
        )
        for previous in PREVIOUS_V2_CROPS
    )


def build_morphology(elevation_30m: np.ndarray) -> tuple[np.ndarray, ...]:
    """Return 120 m valley depth/mask and a 1-pixel medial skeleton."""
    elevation = np.asarray(elevation_30m, dtype=np.float32)
    if elevation.ndim != 2 or elevation.shape[0] % ANALYSIS_DOWNSAMPLE or elevation.shape[1] % ANALYSIS_DOWNSAMPLE:
        raise ValueError("elevation field must be 2D and divisible by the 120 m analysis factor")
    coarse = elevation.reshape(
        elevation.shape[0] // ANALYSIS_DOWNSAMPLE,
        ANALYSIS_DOWNSAMPLE,
        elevation.shape[1] // ANALYSIS_DOWNSAMPLE,
        ANALYSIS_DOWNSAMPLE,
    ).mean(axis=(1, 3), dtype=np.float64).astype(np.float32)
    smooth = ndimage.gaussian_filter(coarse.astype(np.float64), sigma=1.25, mode="nearest")
    closed = ndimage.grey_closing(
        smooth,
        size=(MORPHOLOGY_CLOSING_SIZE, MORPHOLOGY_CLOSING_SIZE),
        mode="nearest",
    )
    valley_depth = np.maximum(closed - smooth, 0.0).astype(np.float32)
    valley_mask = valley_depth >= DEEP_VALLEY_THRESHOLD_M
    skeleton = skeletonize(valley_mask)
    junction_labels = skeleton_junction_labels(valley_mask, skeleton)
    return coarse, valley_depth, valley_mask, skeleton, junction_labels


def skeleton_junction_labels(valley_mask: np.ndarray, skeleton: np.ndarray | None = None) -> np.ndarray:
    if skeleton is None:
        skeleton = skeletonize(valley_mask)
    neighbor_count = ndimage.convolve(
        skeleton.astype(np.uint8), NEIGHBOR_KERNEL, mode="constant", cval=0
    )
    junction_pixels = skeleton & (neighbor_count >= 3)
    junction_labels, _ = ndimage.label(junction_pixels, structure=EIGHT_CONNECTED)
    return junction_labels


def measure_crop(
    valley_depth: np.ndarray,
    valley_mask: np.ndarray,
    junction_labels: np.ndarray,
    x_native: int,
    z_native: int,
) -> dict[str, float | int]:
    x = x_native // ANALYSIS_DOWNSAMPLE
    z = z_native // ANALYSIS_DOWNSAMPLE
    width = CROP_WIDTH // ANALYSIS_DOWNSAMPLE
    height = CROP_HEIGHT // ANALYSIS_DOWNSAMPLE
    inner = (
        slice(z + MORPHOLOGY_MARGIN, z + height - MORPHOLOGY_MARGIN),
        slice(x + MORPHOLOGY_MARGIN, x + width - MORPHOLOGY_MARGIN),
    )
    mask = valley_mask[inner]
    depth = valley_depth[inner]
    labels, _ = ndimage.label(mask, structure=EIGHT_CONNECTED)
    sizes = np.bincount(labels.ravel())
    component_sizes = sizes[1:]
    meaningful = component_sizes[component_sizes >= MIN_DEEP_COMPONENT_PIXELS]
    deep_count = int(meaningful.sum())
    largest = int(meaningful.max(initial=0))
    component_count = int(meaningful.size)
    meaningful_labels = np.flatnonzero(sizes[1:] >= MIN_DEEP_COMPONENT_PIXELS) + 1
    largest_label = (
        int(meaningful_labels[np.argmax(sizes[meaningful_labels])])
        if meaningful_labels.size
        else 0
    )
    valid_cells = int(mask.size)
    deep_fraction = deep_count / valid_cells if valid_cells else 0.0
    largest_fraction = largest / deep_count if deep_count else 0.0

    skeleton_region = junction_labels[inner]
    junction_ids = np.unique(
        skeleton_region[(skeleton_region > 0) & (labels == largest_label)]
    ) if largest_label else np.empty(0, dtype=np.int32)
    junction_count = int(junction_ids.size)
    selected_depth = depth[mask]
    p95 = float(np.percentile(selected_depth, 95.0)) if selected_depth.size else 0.0
    maximum = float(selected_depth.max(initial=0.0)) if selected_depth.size else 0.0
    if mask.size:
        edge = np.concatenate((mask[0, :], mask[-1, :], mask[:, 0], mask[:, -1]))
        valley_edge_fraction = float(np.count_nonzero(edge) / max(deep_count, 1))
    else:
        valley_edge_fraction = 0.0

    # A connected deep valley is the base signal. Junction clusters add a
    # transparent morphology bonus; this is not a routed flow graph.
    score = deep_fraction * largest_fraction * (1 + junction_count)
    if component_count == 0:
        score = 0.0
    return {
        "connected_branch_score": float(score),
        "deep_valley_fraction": float(deep_fraction),
        "largest_component_fraction": float(largest_fraction),
        "deep_component_count": component_count,
        "branch_junction_count": junction_count,
        "p95_valley_depth_m": p95,
        "maximum_valley_depth_m": maximum,
        "valley_edge_fraction": valley_edge_fraction,
    }


def _relative_path(path: Path, base: Path) -> str:
    try:
        return path.resolve().relative_to(base.resolve()).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def rank_key(candidate: Candidate) -> tuple[Any, ...]:
    return (
        -candidate.score,
        -candidate.branch_junction_count,
        -candidate.largest_component_fraction,
        -candidate.deep_valley_fraction,
        -candidate.p95_valley_depth_m,
        candidate.elevation_sha256,
        candidate.z,
        candidate.x,
    )


def _overlaps_candidate(a: Candidate, b: Candidate) -> bool:
    return a.elevation_sha256 == b.elevation_sha256 and rectangles_overlap(
        a.x,
        a.z,
        CROP_WIDTH,
        CROP_HEIGHT,
        b.x,
        b.z,
        CROP_WIDTH,
        CROP_HEIGHT,
    )


def select_shortlist(candidates: list[Candidate], limit: int = DEFAULT_SHORTLIST_SIZE) -> list[Candidate]:
    if limit <= 0:
        raise ValueError("shortlist size must be positive")
    ranked = sorted(
        (candidate for candidate in candidates if candidate.deep_component_count > 0),
        key=rank_key,
    )
    selected: list[Candidate] = []
    per_source: dict[str, int] = {}
    # First give each distinct immutable elevation a slot; only then permit a
    # second non-overlapping crop from a source if the shortlist needs it.
    for source_cap in (1, 2):
        for candidate in ranked:
            if len(selected) >= limit:
                break
            source_count = per_source.get(candidate.elevation_sha256, 0)
            if source_count >= source_cap:
                continue
            if any(_overlaps_candidate(candidate, item) for item in selected):
                continue
            selected.append(candidate)
            per_source[candidate.elevation_sha256] = source_count + 1
    return selected


def _candidate_from_metrics(asset: TerrainAsset, base: Path, x: int, z: int, metrics: dict[str, Any]) -> Candidate:
    canonical = asset.canonical
    aliases = tuple(_relative_path(item.manifest_path, base) for item in asset.aliases)
    return Candidate(
        elevation_sha256=asset.elevation_sha256,
        manifest_path=_relative_path(canonical.manifest_path, base),
        alias_manifest_paths=aliases,
        variant=canonical.variant,
        seed=canonical.seed,
        x=x,
        z=z,
        score=float(metrics["connected_branch_score"]),
        deep_valley_fraction=float(metrics["deep_valley_fraction"]),
        largest_component_fraction=float(metrics["largest_component_fraction"]),
        deep_component_count=int(metrics["deep_component_count"]),
        branch_junction_count=int(metrics["branch_junction_count"]),
        p95_valley_depth_m=float(metrics["p95_valley_depth_m"]),
        maximum_valley_depth_m=float(metrics["maximum_valley_depth_m"]),
        valley_edge_fraction=float(metrics["valley_edge_fraction"]),
    )


def _asset_inventory(asset: TerrainAsset, base: Path) -> dict[str, Any]:
    canonical = asset.canonical
    return {
        "elevation_sha256": asset.elevation_sha256,
        "canonical_manifest": _relative_path(canonical.manifest_path, base),
        "canonical_manifest_sha256": canonical.manifest_sha256,
        "elevation_path": _relative_path(canonical.elevation_path, base),
        "aliases": [
            {
                "manifest": _relative_path(alias.manifest_path, base),
                "manifest_sha256": alias.manifest_sha256,
                "variant": alias.variant,
                "seed": alias.seed,
            }
            for alias in asset.aliases
        ],
        "grid": {
            "width": canonical.width,
            "height": canonical.height,
            "sample_spacing_m": canonical.spacing_m,
            "layout": "row-major-zx",
        },
        "producer": {
            "id": canonical.source_id,
            "generator": canonical.source_generator,
            "code_revision": canonical.code_revision,
            "model_id": canonical.model_id,
            "model_revision": canonical.model_revision,
            "native_resolution_m": canonical.native_resolution_m,
        },
    }


def _write_preview(candidate: Candidate, context: tuple[np.ndarray, ...], asset: TerrainAsset, output_dir: Path, rank: int) -> Path:
    coarse, valley_depth, valley_mask, _skeleton, junction_labels = context
    canonical = asset.canonical
    field = np.memmap(
        canonical.elevation_path,
        dtype="<f4",
        mode="r",
        shape=(canonical.height, canonical.width),
    )
    elevation = np.asarray(
        field[candidate.z : candidate.z + CROP_HEIGHT, candidate.x : candidate.x + CROP_WIDTH],
        dtype=np.float32,
    )
    del field
    low, high = np.percentile(elevation, (2.0, 98.0))
    if high <= low:
        high = low + 1.0
    gray = np.clip((elevation - low) / (high - low), 0.0, 1.0)
    rgb = np.repeat(np.rint(gray * 255.0).astype(np.uint8)[:, :, None], 3, axis=2)

    ax = candidate.x // ANALYSIS_DOWNSAMPLE
    az = candidate.z // ANALYSIS_DOWNSAMPLE
    aw = CROP_WIDTH // ANALYSIS_DOWNSAMPLE
    ah = CROP_HEIGHT // ANALYSIS_DOWNSAMPLE
    crop_mask = valley_mask[az : az + ah, ax : ax + aw]
    crop_junctions = junction_labels[az : az + ah, ax : ax + aw] > 0
    mask_native = np.repeat(np.repeat(crop_mask, ANALYSIS_DOWNSAMPLE, axis=0), ANALYSIS_DOWNSAMPLE, axis=1)
    junction_native = np.repeat(
        np.repeat(crop_junctions, ANALYSIS_DOWNSAMPLE, axis=0), ANALYSIS_DOWNSAMPLE, axis=1
    )
    rgb[mask_native] = np.rint(rgb[mask_native] * 0.38 + np.array([255, 112, 28]) * 0.62).astype(np.uint8)
    rgb[junction_native] = np.array([26, 238, 250], dtype=np.uint8)
    preview = Image.fromarray(rgb, mode="RGB")
    path = output_dir / f"site-{rank:02d}.png"
    preview.save(path, format="PNG", optimize=True)
    return path


def _write_contact_sheet(candidates: list[Candidate], previews: list[Path], output_dir: Path) -> Path:
    columns = 3
    rows = max(1, math.ceil(len(candidates) / columns))
    header_height = 36
    footer_height = 24
    cell_width = CROP_WIDTH
    cell_height = header_height + CROP_HEIGHT + footer_height
    sheet = Image.new("RGB", (columns * cell_width, rows * cell_height), "#171b20")
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    for index, (candidate, path) in enumerate(zip(candidates, previews, strict=True)):
        column = index % columns
        row = index // columns
        left = column * cell_width
        top = row * cell_height
        title = f"{index + 1}. {candidate.variant or 'terrain'}  seed {candidate.seed}  x,z={candidate.x},{candidate.z}"
        draw.text((left + 6, top + 5), title[:80], fill="#f1f3f5", font=font)
        image = Image.open(path).convert("RGB")
        sheet.paste(image, (left, top + header_height))
        footer = (
            f"junctions {candidate.branch_junction_count}  connected {candidate.largest_component_fraction:.2f}"
            f"  deep area {candidate.deep_valley_fraction:.3f}"
        )
        draw.text((left + 6, top + header_height + CROP_HEIGHT + 5), footer, fill="#d0d5da", font=font)
    result = output_dir / "shortlist-contact-sheet.png"
    sheet.save(result, format="PNG", optimize=True)
    return result


def run_scan(source_root: Path, output_dir: Path, shortlist_size: int = DEFAULT_SHORTLIST_SIZE) -> dict[str, Any]:
    start = time.perf_counter()
    source_root = source_root.resolve()
    output_dir = output_dir.resolve()
    repo_root = Path(__file__).resolve().parents[3]
    if output_dir.exists():
        raise FileExistsError(f"refusing to overwrite existing scan output: {output_dir}")
    records = discover_manifests(source_root)
    assets = deduplicate_assets(records)
    all_candidates: list[Candidate] = []
    excluded_overlap_count = 0
    contexts: dict[str, tuple[np.ndarray, ...]] = {}
    per_asset_counts: list[dict[str, Any]] = []

    for asset in assets:
        canonical = asset.canonical
        elevation = np.memmap(
            canonical.elevation_path,
            dtype="<f4",
            mode="r",
            shape=(canonical.height, canonical.width),
        )
        context = build_morphology(elevation)
        del elevation
        contexts[asset.elevation_sha256] = context
        coarse, valley_depth, valley_mask, _skeleton, junction_labels = context
        generated = 0
        excluded = 0
        for x, z in candidate_origins(canonical.width, canonical.height):
            if overlaps_previous_v2(asset.elevation_sha256, x, z):
                excluded += 1
                excluded_overlap_count += 1
                continue
            metrics = measure_crop(valley_depth, valley_mask, junction_labels, x, z)
            all_candidates.append(_candidate_from_metrics(asset, repo_root, x, z, metrics))
            generated += 1
        per_asset_counts.append(
            {
                "elevation_sha256": asset.elevation_sha256,
                "canonical_manifest": _relative_path(canonical.manifest_path, repo_root),
                "alias_count": len(asset.aliases),
                "scanned_windows": generated,
                "excluded_prior_overlap_windows": excluded,
            }
        )

    all_candidates.sort(key=rank_key)
    shortlist = select_shortlist(all_candidates, shortlist_size)
    output_dir.mkdir(parents=True, exist_ok=False)
    preview_paths = []
    by_hash = {asset.elevation_sha256: asset for asset in assets}
    for rank, candidate in enumerate(shortlist, start=1):
        preview_paths.append(
            _write_preview(candidate, contexts[candidate.elevation_sha256], by_hash[candidate.elevation_sha256], output_dir, rank)
        )
    contact_sheet = _write_contact_sheet(shortlist, preview_paths, output_dir)

    csv_path = output_dir / "candidates.csv"
    csv_fields = (
        "rank",
        "elevation_sha256",
        "manifest_path",
        "variant",
        "seed",
        "crop_x",
        "crop_z",
        "crop_width",
        "crop_height",
        "sample_spacing_m",
        "score",
        "deep_valley_fraction",
        "largest_component_fraction",
        "deep_component_count",
        "branch_junction_count",
        "p95_valley_depth_m",
        "maximum_valley_depth_m",
        "valley_edge_fraction",
        "shortlisted",
    )
    shortlist_identity = {
        (item.elevation_sha256, item.x, item.z) for item in shortlist
    }
    rank_by_identity = {
        (item.elevation_sha256, item.x, item.z): rank
        for rank, item in enumerate(all_candidates, start=1)
    }
    with csv_path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=csv_fields)
        writer.writeheader()
        for candidate in all_candidates:
            identity = (candidate.elevation_sha256, candidate.x, candidate.z)
            writer.writerow(
                {
                    "rank": rank_by_identity[identity],
                    "elevation_sha256": candidate.elevation_sha256,
                    "manifest_path": candidate.manifest_path,
                    "variant": candidate.variant,
                    "seed": candidate.seed,
                    "crop_x": candidate.x,
                    "crop_z": candidate.z,
                    "crop_width": CROP_WIDTH,
                    "crop_height": CROP_HEIGHT,
                    "sample_spacing_m": NATIVE_SPACING_M,
                    "score": f"{candidate.score:.9f}",
                    "deep_valley_fraction": f"{candidate.deep_valley_fraction:.9f}",
                    "largest_component_fraction": f"{candidate.largest_component_fraction:.9f}",
                    "deep_component_count": candidate.deep_component_count,
                    "branch_junction_count": candidate.branch_junction_count,
                    "p95_valley_depth_m": f"{candidate.p95_valley_depth_m:.6f}",
                    "maximum_valley_depth_m": f"{candidate.maximum_valley_depth_m:.6f}",
                    "valley_edge_fraction": f"{candidate.valley_edge_fraction:.9f}",
                    "shortlisted": int(identity in shortlist_identity),
                }
            )

    elapsed = time.perf_counter() - start
    index = {
        "schema": SCHEMA,
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "discovery_only": True,
        "terrain_mutated": False,
        "solver_input_generated": False,
        "fixture_or_default_promotion": "none",
        "source_root": _relative_path(source_root, repo_root),
        "contract": {
            "source_manifests": len(records),
            "unique_elevation_payloads": len(assets),
            "deduplicated_manifest_aliases": len(records) - len(assets),
            "native_field_shape": list(FIELD_SHAPE),
            "native_sample_spacing_m": NATIVE_SPACING_M,
            "crop_shape_width_height": [CROP_WIDTH, CROP_HEIGHT],
            "crop_extent_km": [CROP_EXTENT_M[0] / 1000.0, CROP_EXTENT_M[1] / 1000.0],
            "scan_stride_native_samples_xz": [SCAN_STRIDE_X, SCAN_STRIDE_Z],
            "analysis_downsample": ANALYSIS_DOWNSAMPLE,
            "analysis_sample_spacing_m": ANALYSIS_SPACING_M,
            "closing_size_analysis_pixels": MORPHOLOGY_CLOSING_SIZE,
            "closing_extent_m": MORPHOLOGY_CLOSING_SIZE * ANALYSIS_SPACING_M,
            "deep_valley_threshold_m": DEEP_VALLEY_THRESHOLD_M,
            "minimum_connected_component_pixels": MIN_DEEP_COMPONENT_PIXELS,
            "prior_v2_crops_excluded_by_overlap": list(PREVIOUS_V2_CROPS),
            "scoring": "deep_valley_fraction * largest_component_fraction * (1 + branch_junction_count on the largest deep component), then deterministic metric/hash/coordinate tie-breaks",
            "branch_definition": "8-neighbour medial-skeleton junction clusters with skeleton degree >= 3 that intersect the largest 8-connected deep-valley component in the interior crop",
            "limitations": [
                "Morphological valley depth and skeleton convergence are topographic shape proxies, not mapped rivers or hydrology.",
                "No D8, flow accumulation, depression filling, channel painting, climate filter, or solver feedback is used.",
                "Valley masks and junctions are preview/ranking evidence only and are never passed to Fluid.",
                "120 m area averaging can omit features narrower than a few native samples; final site review must inspect native height and solver response.",
                "Crop-edge and source-edge effects remain possible; valley_edge_fraction is reported for review.",
            ],
        },
        "inventory": [_asset_inventory(asset, repo_root) for asset in assets],
        "scan_counts": {
            "candidate_windows_after_v2_overlap_exclusion": len(all_candidates),
            "candidate_windows_excluded_for_v2_overlap": excluded_overlap_count,
            "connected_valley_candidates": sum(item.deep_component_count > 0 for item in all_candidates),
            "shortlist_size_requested": shortlist_size,
            "shortlist_size_written": len(shortlist),
            "elapsed_seconds": round(elapsed, 3),
            "per_asset": per_asset_counts,
        },
        "shortlist": [
            {
                **candidate.record(rank),
                "preview_png": preview_paths[rank - 1].name,
            }
            for rank, candidate in enumerate(shortlist, start=1)
        ],
        "artifacts": {
            "candidate_csv": csv_path.name,
            "contact_sheet_png": contact_sheet.name,
            "preview_pngs": [path.name for path in preview_paths],
        },
    }
    (output_dir / "scan.json").write_text(json.dumps(index, indent=2, sort_keys=True) + "\n")
    return index


def _parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root",
        type=Path,
        default=repo_root / SOURCE_ROOT_RELATIVE,
        help="cached Terrain Diffusion source bundles (never generated by this tool)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=repo_root / DEFAULT_OUTPUT_RELATIVE,
        help="new output directory; existing paths are never overwritten",
    )
    parser.add_argument("--shortlist-size", type=int, default=DEFAULT_SHORTLIST_SIZE)
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    try:
        index = run_scan(args.source_root, args.output_dir, args.shortlist_size)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        raise SystemExit(f"terrain site scan failed: {error}") from error
    print(
        "terrain site scan wrote "
        f"{args.output_dir.resolve()} ({index['scan_counts']['candidate_windows_after_v2_overlap_exclusion']} windows, "
        f"{index['scan_counts']['elapsed_seconds']:.3f}s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
