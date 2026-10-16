#!/usr/bin/env python3
"""Run bounded, source-only two-hour hillside hydraulics and evidence phases.

The terrain, source, and numerical recipe are pinned to the accepted hillside
study. This runner does not generate or modify terrain, change solver defaults,
or infer strict CPU/GPU parity from a long hydraulic profile.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import subprocess
import time
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
FIXTURES = Path(__file__).resolve().parent / "fixtures/hillside-flow-study-v1"
RECIPES = {
    256: FIXTURES / "temperate-upland.json",
    512: FIXTURES / "temperate-upland-512-benchmark.json",
}
RECIPE_SHA256 = {
    256: "b445c6770566ea47beddef76379642f4dc5294a48de43aa687e4ebee3d2063ce",
    512: "6d2dd848d7ad528bd4e4c792c1e278f78e97316db736869a237753d35ecb56ef",
}
MANIFEST = ROOT / "cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json"
MANIFEST_SHA256 = "744356b27e2c2f7895c93bd88a8969fa329b212b9b39c0a9ced239ac405d3d6c"
ELEVATION_SHA256 = "a978ecd435d2a161598d78ecf71221cba53b371e98c3525d18ebeab6665aa737"
BASELINE_METRICS = ROOT / "outputs/fluid/hillside-flow-v1-20260925/final-256/hydraulics.metrics.csv"
DEFAULT_APP = ROOT / "build/dev/projects/fluid/fluid_25d/fluid_25d"

DT = 2
SUBSTEPS = 16
Q_M3_PER_S = 100
HORIZON_SECONDS = 7200
GRAVITY_M_PER_S2 = 9.81
DAMPING_PER_SECOND = 0.15
CELL_SIZE_M = 30.0
SOURCE_FULL_MAP_XZ = (1365, 1621)
EDGE_BAND_M = 120.0
ACTIVE_SPEED_THRESHOLD_M_PER_S = 0.02
MATERIAL_DEPTH_THRESHOLD_M = 0.01
CSV_SUM_TOLERANCE_M3 = 3e-6
WATER_LEDGER_FAILURE = "water ledger exceeds the existing 3e-4 relative tolerance"
SNAPSHOT_SECONDS = (600, 1800, 3600, 7200)
STRICT_ORACLE_SECONDS = (60, 600, 1800)
FPS = 30
PLAYBACK_ACCELERATION = DT * FPS

WATER = "fluid_25d.water"
SOLVER = "fluid_25d.solver"
PROGRESS = "fluid_25d.hillside.progress"
SPATIAL = "fluid_25d.hillside.spatial"

SPATIAL_FIELDS = (
    "material_water_volume_m3",
    "source_connected_material_wet_cells",
    "source_connected_material_water_volume_m3",
    "material_active_flow_cells",
    "material_active_water_volume_m3",
    "material_slow_water_volume_m3",
    "minimum_material_edge_distance_m",
    "material_edge_band_wet_cells",
)
WATER_FIELDS = (
    "cumulative_source_volume_m3",
    "cumulative_sink_volume_m3",
    "cumulative_boundary_outflow_volume_m3",
    "total_water_volume_m3",
    "maximum_depth_m",
    "conservation_residual_m3",
    "active_flow_cell_count",
    "active_mean_speed_m_per_s",
    "slow_pooled_wet_fraction",
)
PROGRESS_FIELDS = (
    "source_minimum_bed_m",
    "maximum_wetted_bed_drop_m",
    "farthest_materially_wet_distance_m",
    "source_region_water_volume_m3",
    "materially_wet_cell_count",
    *(f"below_source_wet_cells_{drop}m" for drop in (20, 50, 100, 200)),
    *(f"below_source_water_volume_{drop}m" for drop in (20, 50, 100, 200)),
)
REQUIRED_KEYS = {
    (SOLVER, "finite_volume_status_flags"),
    *((WATER, name) for name in WATER_FIELDS),
    *((PROGRESS, name) for name in PROGRESS_FIELDS),
    *((SPATIAL, name) for name in SPATIAL_FIELDS),
}
BASELINE_CORE_KEYS = {
    (SOLVER, "finite_volume_status_flags"),
    (WATER, "cumulative_source_volume_m3"),
    (WATER, "cumulative_sink_volume_m3"),
    (WATER, "cumulative_boundary_outflow_volume_m3"),
    (WATER, "total_water_volume_m3"),
    (WATER, "maximum_depth_m"),
    (WATER, "conservation_residual_m3"),
    (WATER, "active_flow_cell_count"),
    *((PROGRESS, name) for name in PROGRESS_FIELDS),
}
INTEGER_SPATIAL_FIELDS = (
    "source_connected_material_wet_cells",
    "material_active_flow_cells",
    "material_edge_band_wet_cells",
)
VOLUME_SPATIAL_FIELDS = (
    "material_water_volume_m3",
    "source_connected_material_water_volume_m3",
    "material_active_water_volume_m3",
    "material_slow_water_volume_m3",
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def shader_map_identity(shader_directory: Path) -> dict[str, Any]:
    """Hash every exported SPIR-V module beside the selected app.

    The exported directory may contain unused/stale modules. They remain in
    this build-provenance map because the app binary does not contain shader
    bytes and the runner cannot prove which modules a given capture loads.
    """
    shader_directory = shader_directory.resolve()
    if not shader_directory.is_dir():
        raise FileNotFoundError(f"missing exported shader directory: {shader_directory}")
    files = sorted(path for path in shader_directory.rglob("*.spv") if path.is_file())
    if not files:
        raise ValueError(f"exported shader directory has no .spv files: {shader_directory}")
    mapping = {path.relative_to(shader_directory).as_posix(): sha256_file(path) for path in files}
    payload = json.dumps(mapping, sort_keys=True, separators=(",", ":")).encode()
    return {
        "shader_directory": str(shader_directory),
        "scope": "complete exported *.spv file map; may include unused modules",
        "file_count": len(mapping),
        "files": mapping,
        "shader_map_sha256": hashlib.sha256(payload).hexdigest(),
    }


def require_stable_inputs(start: dict[str, str], end: dict[str, str]) -> None:
    if start == end:
        return
    changed = sorted(name for name in start.keys() | end.keys() if start.get(name) != end.get(name))
    raise ValueError("input identity changed during phase: " + ", ".join(changed))


def key(category: str, name: str) -> tuple[str, str]:
    return category, name


def validate_recipe_data(data: dict[str, Any], domain: int) -> None:
    """Reject anything except the two exact, source-only frozen recipes."""
    if domain not in RECIPES:
        raise ValueError(f"unsupported domain: {domain}")
    expected = {
        256: {
            "candidate_id": "temperate-mountain-valley-x1280-z1536-src1365-1621",
            "transformed_crop_sha256": "42cc62f823284c41d768bc05b6195cf291879fb4f6f04337a327ab79806ef5d8",
            "crop_xzwh": [1280, 1536, 256, 256],
            "source_center_cell_xz": [85, 85],
            "source_cells_xz": [[85, 85], [85, 84], [85, 86], [86, 85], [84, 85]],
        },
        512: {
            "candidate_id": "temperate-upland-same-source-512-context-benchmark",
            "transformed_crop_sha256": "02f1577d96f3ea4f65b75c7b21b6e1b2c83248cebb8b87a528fff6193435bc3f",
            "crop_xzwh": [1152, 1408, 512, 512],
            "source_center_cell_xz": [213, 213],
            "source_cells_xz": [[213, 213], [213, 212], [213, 214], [214, 213], [212, 213]],
        },
    }[domain]
    if data.get("schema") != "cubey.fluid25d.hillside_flow_study.v1":
        raise ValueError("runner requires the strict source-only hillside-flow recipe schema")
    if "expected_outlet" in data:
        raise ValueError("source-only hillside recipes must not define an expected outlet")
    for field, value in {
        **expected,
        "elevation_sha256": ELEVATION_SHA256,
        "cell_size_m": CELL_SIZE_M,
        "gauges": [],
    }.items():
        if data.get(field) != value:
            raise ValueError(f"recipe field {field!r} differs from the pinned domain {domain} fixture")


def forcing_identity(recipe: dict[str, Any], manifest_sha: str) -> dict[str, Any]:
    crop_x, crop_z, _, _ = recipe["crop_xzwh"]
    world_cells = [[crop_x + x, crop_z + z] for x, z in recipe["source_cells_xz"]]
    world_center = [crop_x + recipe["source_center_cell_xz"][0],
                    crop_z + recipe["source_center_cell_xz"][1]]
    if world_center != list(SOURCE_FULL_MAP_XZ):
        raise ValueError(f"fixture moved the frozen full-map source: {world_center}")
    return {
        "terrain_manifest_sha256": manifest_sha,
        "source_elevation_sha256": ELEVATION_SHA256,
        "native_cell_size_m": CELL_SIZE_M,
        "source_full_map_center_xz": world_center,
        "source_full_map_cells_xz": world_cells,
        "source_rate_m3_per_s": Q_M3_PER_S,
        "initial_water": "dry",
        "rain_m3_per_s": 0,
        "explicit_sink_m3_per_s": 0,
        "fixed_delta_seconds": DT,
        "substeps": SUBSTEPS,
        "gravity_m_per_s2": GRAVITY_M_PER_S2,
        "damping_per_second": DAMPING_PER_SECOND,
    }


def pinned_inputs(domain: int, app: Path) -> dict[str, Any]:
    recipe_path = RECIPES[domain].resolve()
    manifest_path = MANIFEST.resolve()
    if not app.is_file():
        raise FileNotFoundError(f"missing Fluid app: {app}")
    shader_identity = shader_map_identity(app.parent / "shaders")
    if sha256_file(recipe_path) != RECIPE_SHA256[domain]:
        raise ValueError(f"pinned domain {domain} recipe SHA-256 changed")
    manifest_sha = sha256_file(manifest_path)
    if manifest_sha != MANIFEST_SHA256:
        raise ValueError("pinned terrain manifest SHA-256 changed")
    recipe = json.loads(recipe_path.read_text())
    validate_recipe_data(recipe, domain)
    manifest = json.loads(manifest_path.read_text())
    elevation_info = manifest["files"]["elevation"]
    if (manifest.get("schema") != "cubey.terrain.heightfield.v1" or
            elevation_info.get("sha256") != ELEVATION_SHA256 or
            elevation_info.get("shape") != [2048, 2048] or
            manifest.get("grid", {}).get("sample_spacing_m") != CELL_SIZE_M):
        raise ValueError("terrain manifest contents do not match the frozen native-30m source")
    elevation_path = (manifest_path.parent / elevation_info["path"]).resolve()
    if sha256_file(elevation_path) != ELEVATION_SHA256:
        raise ValueError("immutable terrain elevation bytes changed")
    return {
        "app_sha256": sha256_file(app),
        "recipe_sha256": sha256_file(recipe_path),
        "recipe_path": str(recipe_path),
        "manifest_sha256": manifest_sha,
        "manifest_path": str(manifest_path),
        "elevation_sha256": ELEVATION_SHA256,
        "elevation_path": str(elevation_path),
        "input_identity": {
            "domain_cells": [domain, domain],
            "crop_xzwh": recipe["crop_xzwh"],
            "transformed_crop_sha256": recipe["transformed_crop_sha256"],
            "recipe_sha256": RECIPE_SHA256[domain],
            "shader_map_sha256": shader_identity["shader_map_sha256"],
            "forcing_identity": forcing_identity(recipe, manifest_sha),
        },
        "shader_identity": shader_identity,
        "shader_map_sha256": shader_identity["shader_map_sha256"],
    }


def arguments(domain: int) -> list[str]:
    recipe = RECIPES[domain].resolve()
    manifest = MANIFEST.resolve()
    data = json.loads(recipe.read_text())
    validate_recipe_data(data, domain)
    x, z, width, height = data["crop_xzwh"]
    return [
        "--headless", "--width", "1280", "--height", "720",
        "--fluid25d-scenario", "hillside-flow-study",
        "--fluid25d-solver", "finite-volume",
        "--fluid25d-natural-flow-recipe", str(recipe),
        "--fluid25d-natural-flow-source-m3-per-s", str(Q_M3_PER_S),
        "--terrain-heightfield", str(manifest),
        "--fluid25d-terrain-crop-x", str(x), "--fluid25d-terrain-crop-z", str(z),
        "--grid-width", str(width), "--grid-height", str(height),
        "--fluid25d-cell-size-m", str(int(CELL_SIZE_M)),
        "--fluid25d-fixed-delta-seconds", str(DT),
        "--fluid25d-substeps", str(SUBSTEPS),
        "--fluid25d-gravity-m-per-s2", str(GRAVITY_M_PER_S2),
        "--fluid25d-flow-damping-per-second", str(DAMPING_PER_SECOND),
        "--fluid25d-view", "catchment",
        "--fluid25d-render-height-scale", "1",
        "--fluid25d-terrain-palette-low-m", "770",
        "--fluid25d-terrain-palette-high-m", "2274",
    ]


def _read_metrics(path: Path, *, decimal_values: bool = False) -> dict[int, dict[tuple[str, str], Any]]:
    frames: dict[int, dict[tuple[str, str], Any]] = {}
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames is None or not {"frame_index", "category", "name", "value"}.issubset(reader.fieldnames):
            raise ValueError("profile has an invalid or missing CSV header")
        for row in reader:
            try:
                frame = int(row["frame_index"])
                metric = Decimal(row["value"])
            except (TypeError, ValueError, InvalidOperation) as error:
                raise ValueError("profile has a malformed frame index or metric value") from error
            if frame < 0 or not metric.is_finite():
                raise ValueError("profile has a negative frame index or nonfinite metric")
            category, name = row["category"], row["name"]
            if not category or not name:
                raise ValueError("profile has an empty metric category or name")
            values = frames.setdefault(frame, {})
            metric_key = (category, name)
            if metric_key in values:
                raise ValueError(f"profile has a duplicate metric at frame {frame}: {metric_key}")
            if decimal_values:
                values[metric_key] = metric
            else:
                converted = float(metric)
                if not math.isfinite(converted):
                    raise ValueError("profile metric is outside finite float range")
                values[metric_key] = converted
    if not frames:
        raise ValueError("profile has no diagnostics")
    return frames


def read_metrics(path: Path) -> dict[int, dict[tuple[str, str], float]]:
    return _read_metrics(path)


def compare_prefix_to_baseline(
    current_metrics: Path,
    baseline_metrics: Path = BASELINE_METRICS,
    *,
    frame_count: int = 900,
) -> dict[str, Any]:
    """Compare shared old metrics over the retained first 900 CSV frames.

    Values are compared as serialized decimal values quantized to the CSV's
    six-place precision. The current profile may contain additional categories,
    such as the new spatial observations.
    """
    if frame_count <= 0:
        raise ValueError("baseline prefix frame count must be positive")
    current = _read_metrics(current_metrics, decimal_values=True)
    baseline = _read_metrics(baseline_metrics, decimal_values=True)
    expected_frames = list(range(frame_count))
    if sorted(frame for frame in current if frame < frame_count) != expected_frames:
        raise ValueError("current profile does not contain the full requested baseline prefix")
    if sorted(baseline) != expected_frames:
        raise ValueError("retained baseline does not contain exactly the requested frame prefix")

    shared_across_frames: set[tuple[str, str]] | None = None
    mismatches: list[dict[str, Any]] = []
    compared = 0
    current_only = 0
    baseline_only = 0
    precision = Decimal("0.000001")

    def csv_precision(value: Decimal) -> Decimal:
        try:
            return value.quantize(precision)
        except InvalidOperation as error:
            raise ValueError("baseline comparison contains a metric outside supported CSV precision") from error

    for frame in expected_frames:
        old_values = baseline[frame]
        new_values = current[frame]
        shared = old_values.keys() & new_values.keys()
        shared_across_frames = set(shared) if shared_across_frames is None else shared_across_frames & shared
        current_only += len(new_values.keys() - old_values.keys())
        baseline_only += len(old_values.keys() - new_values.keys())
        for category, name in sorted(shared):
            old = csv_precision(old_values[(category, name)])
            new = csv_precision(new_values[(category, name)])
            compared += 1
            if old != new and len(mismatches) < 20:
                mismatches.append({
                    "frame_index": frame,
                    "category": category,
                    "name": name,
                    "baseline_value": str(old),
                    "current_value": str(new),
                })

    stable_shared = shared_across_frames or set()
    missing_core = sorted(BASELINE_CORE_KEYS - stable_shared)
    return {
        "passed": compared > 0 and not mismatches and not missing_core,
        "baseline_path": str(baseline_metrics.resolve()),
        "baseline_sha256": sha256_file(baseline_metrics),
        "current_path": str(current_metrics.resolve()),
        "frame_count": frame_count,
        "csv_precision_decimal_places": 6,
        "shared_metric_keys_present_in_every_frame": [list(item) for item in sorted(stable_shared)],
        "metric_values_compared": compared,
        "current_only_metric_rows": current_only,
        "baseline_only_metric_rows": baseline_only,
        "missing_required_old_metric_keys": [list(item) for item in missing_core],
        "mismatches": mismatches,
    }


def summarize(
    frames: dict[int, dict[tuple[str, str], float]],
    horizon_seconds: int = HORIZON_SECONDS,
    *,
    snapshot_seconds: tuple[int, ...] = SNAPSHOT_SECONDS,
) -> dict[str, Any]:
    if horizon_seconds <= 0 or horizon_seconds % DT:
        raise ValueError("hydraulic horizon must be a positive multiple of the frozen 2-second step")
    expected = list(range(horizon_seconds // DT))
    if sorted(frames) != expected:
        raise ValueError("profile does not contain every required fixed step exactly once")
    for second in snapshot_seconds:
        if second <= 0 or second > horizon_seconds or second % DT:
            raise ValueError(f"invalid summary snapshot time {second}s for {horizon_seconds}s run")

    def value(frame_values: dict, category: str, name: str) -> float:
        return frame_values[(category, name)]

    bad_flags: list[int] = []
    bad_frames: list[dict[str, Any]] = []
    max_abs_residual = 0.0
    max_abs_ledger_error = 0.0
    max_abs_source_error = 0.0
    first_trigger: dict[str, Any] | None = None
    previous_export = 0.0
    band_arrivals: dict[str, int | None] = {str(drop): None for drop in (20, 50, 100, 200)}

    for frame in expected:
        values = frames[frame]
        missing = sorted(REQUIRED_KEYS - values.keys())
        if missing:
            raise ValueError(f"frame {frame} is missing required metrics: {missing}")
        failures: list[str] = []
        physical_seconds = (frame + 1) * DT
        expected_source = Q_M3_PER_S * physical_seconds
        tolerance = max(0.003, expected_source * 3e-4)

        flags = value(values, SOLVER, "finite_volume_status_flags")
        if flags != 0:
            bad_flags.append(frame)
            failures.append("nonzero finite-volume status flags")

        source = value(values, WATER, "cumulative_source_volume_m3")
        sink = value(values, WATER, "cumulative_sink_volume_m3")
        exported = value(values, WATER, "cumulative_boundary_outflow_volume_m3")
        stored = value(values, WATER, "total_water_volume_m3")
        residual = value(values, WATER, "conservation_residual_m3")
        maximum_depth = value(values, WATER, "maximum_depth_m")
        source_error = abs(source - expected_source)
        ledger_error = source - stored - sink - exported
        max_abs_residual = max(max_abs_residual, abs(residual))
        max_abs_ledger_error = max(max_abs_ledger_error, abs(ledger_error))
        max_abs_source_error = max(max_abs_source_error, source_error)
        if source_error > tolerance:
            failures.append("source volume differs from frozen Q*time")
        if abs(residual) > tolerance or abs(ledger_error) > tolerance:
            failures.append(WATER_LEDGER_FAILURE)
        if sink != 0:
            failures.append("explicit sink volume is nonzero")
        if exported + 1e-6 < previous_export:
            failures.append("cumulative boundary export decreased")
        previous_export = exported
        if min(source, sink, exported, stored, maximum_depth) < 0:
            failures.append("water volume or depth is negative")

        for name in VOLUME_SPATIAL_FIELDS:
            if value(values, SPATIAL, name) < 0:
                failures.append(f"spatial volume {name} is negative")
        for name in INTEGER_SPATIAL_FIELDS:
            count = value(values, SPATIAL, name)
            if count < 0 or count != round(count):
                failures.append(f"spatial count {name} is negative or non-integral")

        material_volume = value(values, SPATIAL, "material_water_volume_m3")
        connected_cells = value(values, SPATIAL, "source_connected_material_wet_cells")
        connected_volume = value(values, SPATIAL, "source_connected_material_water_volume_m3")
        active_cells = value(values, SPATIAL, "material_active_flow_cells")
        active_volume = value(values, SPATIAL, "material_active_water_volume_m3")
        slow_volume = value(values, SPATIAL, "material_slow_water_volume_m3")
        edge_distance = value(values, SPATIAL, "minimum_material_edge_distance_m")
        edge_band_cells = value(values, SPATIAL, "material_edge_band_wet_cells")
        progress_wet_cells = value(values, PROGRESS, "materially_wet_cell_count")
        spatial_tolerance = CSV_SUM_TOLERANCE_M3
        if edge_distance < 0 and edge_distance != -1:
            failures.append("minimum material edge distance is neither nonnegative nor the -1 sentinel")
        if progress_wet_cells == 0 and (edge_distance != -1 or material_volume > spatial_tolerance):
            failures.append("zero materially-wet cells must agree with zero material water and the -1 edge-distance sentinel")
        if progress_wet_cells > 0 and (edge_distance < 0 or material_volume <= 0):
            failures.append("materially-wet cells require material water and a nonnegative edge distance")
        if abs(active_volume + slow_volume - material_volume) > spatial_tolerance:
            failures.append("active and slow material water do not partition total material water at CSV precision")
        if material_volume > stored + spatial_tolerance:
            failures.append("material water volume exceeds total stored water")
        if connected_volume > material_volume + spatial_tolerance:
            failures.append("source-connected material volume exceeds total material volume")
        if connected_cells > progress_wet_cells or active_cells > progress_wet_cells:
            failures.append("material subset count exceeds total materially-wet cells")
        if edge_band_cells > progress_wet_cells:
            failures.append("material edge-band count exceeds total materially-wet cells")
        if (active_volume > material_volume + spatial_tolerance or
                slow_volume > material_volume + spatial_tolerance):
            failures.append("active or slow material water volume exceeds total material water")

        trigger_reasons = []
        if 0 <= edge_distance <= EDGE_BAND_M:
            trigger_reasons.append("material_wet_within_120m_of_edge")
        if exported > 0:
            trigger_reasons.append("positive_boundary_export")
        if trigger_reasons and first_trigger is None:
            first_trigger = {
                "frame_index": frame,
                "physical_seconds": physical_seconds,
                "reasons": trigger_reasons,
                "minimum_material_edge_distance_m": edge_distance,
                "cumulative_boundary_export_m3": exported,
            }

        for drop in (20, 50, 100, 200):
            band_cells = value(values, PROGRESS, f"below_source_wet_cells_{drop}m")
            if band_cells > 0 and band_arrivals[str(drop)] is None:
                band_arrivals[str(drop)] = physical_seconds
        if failures:
            bad_frames.append({"frame_index": frame, "physical_seconds": physical_seconds,
                               "failures": failures})

    snapshots: dict[str, Any] = {}
    for second in snapshot_seconds:
        frame = second // DT - 1
        values = frames[frame]
        snapshots[str(second)] = {
            "physical_seconds": second,
            "frame_index": frame,
            "source_volume_m3": value(values, WATER, "cumulative_source_volume_m3"),
            "stored_water_m3": value(values, WATER, "total_water_volume_m3"),
            "boundary_export_m3": value(values, WATER, "cumulative_boundary_outflow_volume_m3"),
            "sink_volume_m3": value(values, WATER, "cumulative_sink_volume_m3"),
            "maximum_depth_m": value(values, WATER, "maximum_depth_m"),
            "active_flow_cell_count": value(values, WATER, "active_flow_cell_count"),
            "active_mean_speed_m_per_s": value(values, WATER, "active_mean_speed_m_per_s"),
            "slow_pooled_wet_fraction": value(values, WATER, "slow_pooled_wet_fraction"),
            "progress": {
                name: value(values, PROGRESS, name)
                for name in PROGRESS_FIELDS
            },
            "spatial": {
                name: value(values, SPATIAL, name)
                for name in SPATIAL_FIELDS
            },
        }

    final = frames[expected[-1]]
    tolerance_final = max(0.003, Q_M3_PER_S * horizon_seconds * 3e-4)
    numeric = not bad_frames and max_abs_residual <= tolerance_final and max_abs_ledger_error <= tolerance_final
    return {
        "numeric_profile_checks_passed": numeric,
        "strict_cpu_gpu_parity": "not checked by this hydraulic profile; see the separate oracles phase",
        "physical_horizon_seconds": horizon_seconds,
        "frame_time": "frame_index f is the state after (f+1)*2 physical seconds",
        "material_water_definition": f"depth >= {MATERIAL_DEPTH_THRESHOLD_M:.2f} m",
        "active_flow_definition": f"speed > {ACTIVE_SPEED_THRESHOLD_M_PER_S:.2f} m/s among material water",
        "edge_band_distance_m": EDGE_BAND_M,
        "edge_triggered": first_trigger is not None,
        "first_edge_trigger": first_trigger,
        "first_below_source_material_wet_seconds": band_arrivals,
        "maximum_absolute_water_residual_m3": max_abs_residual,
        "maximum_absolute_water_ledger_error_m3": max_abs_ledger_error,
        "maximum_absolute_source_volume_error_m3": max_abs_source_error,
        "water_ledger_relative_tolerance": 3e-4,
        "final_water_ledger_tolerance_m3": tolerance_final,
        "source_volume_m3": value(final, WATER, "cumulative_source_volume_m3"),
        "stored_water_m3": value(final, WATER, "total_water_volume_m3"),
        "boundary_export_m3": value(final, WATER, "cumulative_boundary_outflow_volume_m3"),
        "sink_volume_m3": value(final, WATER, "cumulative_sink_volume_m3"),
        "nonzero_solver_flag_frames": bad_flags,
        "unhealthy_frames": bad_frames,
        "snapshots": snapshots,
        "not_claimed": "strict 7200s CPU/GPU parity, real hydrology, a mapped river, erosion, or calibrated waterfall physics",
    }


def _csv_path(prefix: Path, suffix: str) -> Path:
    return prefix.with_suffix(suffix)


def ensure_absent(paths: list[Path]) -> None:
    existing = [str(path) for path in paths if path.exists()]
    if existing:
        raise FileExistsError("refusing to overwrite existing evidence: " + ", ".join(existing))


def write_report(path: Path | str, report: dict[str, Any]) -> None:
    Path(path).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")


def run_command(app: Path, args: list[str], log_path: Path) -> dict[str, Any]:
    if log_path.exists():
        raise FileExistsError(f"refusing to overwrite run log: {log_path}")
    command = ["rtk", "proxy", str(app), *args]
    started = time.perf_counter()
    with log_path.open("w") as stream:
        result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    return {
        "exit_code": result.returncode,
        "wall_seconds": time.perf_counter() - started,
        "command": command,
        "log": str(log_path.resolve()),
    }


def hydraulics_phase(app: Path, out: Path, domain: int, report: dict[str, Any]) -> None:
    prefix = out / "hydraulics-profile"
    image = out / "hydraulics-final-7200s.png"
    log = out / "hydraulics-child.log"
    expected_outputs = [
        log, image, _csv_path(prefix, ".metrics.csv"), _csv_path(prefix, ".passes.csv"),
        _csv_path(prefix, ".frames.csv"), _csv_path(prefix, ".trace.json"),
        _csv_path(prefix, ".summary.txt"),
    ]
    ensure_absent(expected_outputs)
    command_args = arguments(domain) + [
        "--frames", str(HORIZON_SECONDS // DT), "--capture", "png",
        "--fluid25d-catchment-view", "water-isolation",
        "--fluid25d-natural-flow-home-pitch-radians", "-1.55",
        "--profile-output", str(prefix), "--profile-diagnostics",
        "--profile-diagnostic-interval", "1", "--output", str(image),
    ]
    execution = run_command(app, command_args, log)
    execution["input_hashes"] = dict(report["start_input_hashes"])
    report["cases"].append(execution)
    report["hydraulic_child_completed"] = execution["exit_code"] == 0
    write_report(report["report_path"], report)
    if execution["exit_code"] != 0:
        report["error"] = f"hydraulics child failed with exit code {execution['exit_code']}"
        write_report(report["report_path"], report)
        return
    try:
        report["summary"] = summarize(read_metrics(_csv_path(prefix, ".metrics.csv")))
        report["profile_analysis_completed"] = True
        report.setdefault("phase_checks_passed", False)
        if domain == 256:
            report["baseline_prefix_comparison"] = compare_prefix_to_baseline(
                _csv_path(prefix, ".metrics.csv")
            )
    except (OSError, ValueError, KeyError) as error:
        report["profile_analysis_completed"] = False
        report["error"] = f"hydraulics profile analysis failed: {error}"
    if (domain == 512 and report.get("diagnostic_unhealthy_requested") and
            (report.get("inherited_parent_failure") or diagnostic_ledger_failure_only(report))):
        report["diagnostic_only"] = True
    write_report(report["report_path"], report)


def report_checks_passed(report: dict[str, Any]) -> bool:
    if report.get("error") or report.get("diagnostic_only") or report.get("inherited_parent_failure"):
        return False
    phase = report.get("phase")
    cases = report.get("cases", [])
    if phase == "hydraulics":
        if len(cases) != 1 or cases[0].get("exit_code") != 0:
            return False
        if not report.get("summary", {}).get("numeric_profile_checks_passed"):
            return False
        if report.get("domain") == 256:
            return bool(report.get("baseline_prefix_comparison", {}).get("passed"))
        return True
    if phase == "captures":
        return report.get("artifact_completion_passed") is True and len(cases) == 3 and all(
            case.get("exit_code") == 0 and case.get("raw_video_sha256") and
            case.get("labelled_video", {}).get("exit_code") == 0 and
            case.get("labelled_video", {}).get("sha256")
            for case in cases
        )
    if phase == "oracles":
        oracle = report.get("strict_oracle_coverage", {})
        return (oracle.get("complete") is True and
                [case.get("physical_seconds") for case in oracle.get("checks", [])] == list(STRICT_ORACLE_SECONDS) and
                all(case.get("execution", {}).get("exit_code") == 0 and
                    case.get("execution", {}).get("output_sha256")
                    for case in oracle.get("checks", [])))
    return False


def diagnostic_ledger_failure_only(report: dict[str, Any]) -> bool:
    """Allow a recorded failed hydraulic profile only for the ledger gate."""
    summary = report.get("summary", {})
    cases = report.get("cases", [])
    frames = summary.get("unhealthy_frames", [])
    start_hashes = report.get("start_input_hashes", {})
    if (report.get("phase") != "hydraulics" or report.get("error") or
            len(cases) != 1 or cases[0].get("exit_code") != 0 or
            not start_hashes or start_hashes != report.get("end_input_hashes") or
            cases[0].get("input_hashes") != start_hashes or
            report.get("phase_checks_passed") is not False or
            summary.get("physical_horizon_seconds") != HORIZON_SECONDS or
            summary.get("numeric_profile_checks_passed") is not False or
            not frames or summary.get("nonzero_solver_flag_frames") != [] or
            summary.get("maximum_absolute_source_volume_error_m3") != 0 or
            summary.get("sink_volume_m3") != 0):
        return False
    if any(frame.get("failures") != [WATER_LEDGER_FAILURE] for frame in frames):
        return False
    if report.get("domain") == 256:
        return (report.get("phase_checks_passed") is False and
                report.get("baseline_prefix_comparison", {}).get("passed") is True)
    if report.get("domain") != 512:
        return False
    return _diagnostic_parent_identity_is_valid(report)


def _has_edge_trigger(summary: dict[str, Any]) -> bool:
    trigger = summary.get("first_edge_trigger")
    if summary.get("edge_triggered") is not True or not isinstance(trigger, dict):
        return False
    seconds = trigger.get("physical_seconds")
    reasons = trigger.get("reasons")
    return (isinstance(seconds, int) and 0 < seconds <= HORIZON_SECONDS and
            isinstance(reasons, list) and bool(reasons))


def _diagnostic_parent_identity_is_valid(report: dict[str, Any]) -> bool:
    parent = report.get("parent_evidence", {})
    return (
        parent.get("domain") == 256 and bool(parent.get("sha256")) and
        parent.get("app_sha256") == report.get("app_sha256") and
        parent.get("shader_map_sha256") == report.get("shader_map_sha256") and
        parent.get("recipe_sha256") == RECIPE_SHA256[256] and
        parent.get("manifest_sha256") == MANIFEST_SHA256 and
        parent.get("elevation_sha256") == ELEVATION_SHA256 and
        parent.get("forcing_identity") ==
        report.get("input_identity", {}).get("forcing_identity")
    )


def diagnostic_hydraulics_eligible(report: dict[str, Any]) -> bool:
    """Accept only a clean hydraulic child with ledger-only or inherited failure."""
    if (report.get("phase") != "hydraulics" or report.get("error") or
            len(report.get("cases", [])) != 1 or
            report["cases"][0].get("exit_code") != 0):
        return False
    summary = report.get("summary", {})
    if (summary.get("nonzero_solver_flag_frames") != [] or
            summary.get("maximum_absolute_source_volume_error_m3") != 0 or
            summary.get("sink_volume_m3") != 0):
        return False
    numeric_healthy = (summary.get("numeric_profile_checks_passed") is True and
                       summary.get("unhealthy_frames") == [] and
                       summary.get("physical_horizon_seconds") == HORIZON_SECONDS)
    if numeric_healthy:
        return bool(report.get("domain") == 512 and
                    _diagnostic_parent_identity_is_valid(report) and
                    report.get("parent_evidence", {}).get("diagnostic_only") is True and
                    report.get("inherited_parent_failure") and
                    report.get("diagnostic_only"))
    if not diagnostic_ledger_failure_only(report):
        return False
    if report.get("domain") == 512:
        return _diagnostic_parent_identity_is_valid(report)
    return report.get("domain") == 256


def load_matching_hydraulics(
    out: Path,
    report: dict[str, Any],
    *,
    allow_diagnostic_unhealthy: bool = False,
) -> tuple[dict[str, Any], Path]:
    path = out / "hydraulics.json"
    if not path.is_file():
        raise ValueError("phase requires a successful hydraulics.json in --out")
    hydraulic = json.loads(path.read_text())
    start_hashes = hydraulic.get("start_input_hashes", {})
    end_hashes = hydraulic.get("end_input_hashes", {})
    hydraulic_cases = hydraulic.get("cases", [])
    matching = (
        hydraulic.get("phase") == "hydraulics" and
        hydraulic.get("domain") == report.get("domain") and
        hydraulic.get("app_sha256") == report.get("app_sha256") and
        hydraulic.get("shader_map_sha256") == report.get("shader_map_sha256") and
        hydraulic.get("recipe_sha256") == report.get("recipe_sha256") and
        hydraulic.get("manifest_sha256") == report.get("manifest_sha256") and
        hydraulic.get("input_identity") == report.get("input_identity") and
        bool(start_hashes) and start_hashes == end_hashes and
        start_hashes == report.get("start_input_hashes") and
        len(hydraulic_cases) == 1 and
        hydraulic_cases[0].get("exit_code") == 0 and
        hydraulic_cases[0].get("input_hashes") == start_hashes
    )
    if report.get("domain") == 512:
        matching = matching and (
            hydraulic.get("parent_evidence_sha256") == report.get("parent_evidence_sha256")
        )
    summary = hydraulic.get("summary", {})
    healthy = (
        hydraulic.get("phase_checks_passed") is True and
        summary.get("numeric_profile_checks_passed") is True and
        summary.get("unhealthy_frames") == []
    )
    if matching and healthy:
        return hydraulic, path
    if matching and allow_diagnostic_unhealthy and diagnostic_hydraulics_eligible(hydraulic):
        return hydraulic, path
    raise ValueError(
        "phase requires healthy matching hydraulics; --diagnostic-unhealthy accepts only "
        "the explicitly bounded ledger-only diagnostic case"
    )


def verify_parent_evidence(
    path: Path,
    app_sha: str,
    shader_map_sha: str,
    expected_forcing: dict[str, Any],
    *,
    allow_diagnostic_unhealthy: bool = False,
) -> dict[str, Any]:
    if not path.is_file():
        raise FileNotFoundError(f"missing --parent-evidence report: {path}")
    parent = json.loads(path.read_text())
    expected_hashes = {
        "app_sha256": app_sha,
        "recipe_sha256": RECIPE_SHA256[256],
        "manifest_sha256": MANIFEST_SHA256,
        "elevation_sha256": ELEVATION_SHA256,
        "shader_map_sha256": shader_map_sha,
    }
    start_hashes = parent.get("start_input_hashes", {})
    end_hashes = parent.get("end_input_hashes", {})
    hashes_match = (
        bool(start_hashes) and start_hashes == end_hashes and
        all(start_hashes.get(name) == value for name, value in expected_hashes.items())
    )
    input_identity = parent.get("input_identity", {})
    child_matches = (
        len(parent.get("cases", [])) == 1 and
        parent["cases"][0].get("exit_code") == 0 and
        parent["cases"][0].get("input_hashes") == start_hashes
    )
    fixed_crop = (
        input_identity.get("domain_cells") == [256, 256] and
        input_identity.get("crop_xzwh") == [1280, 1536, 256, 256] and
        input_identity.get("transformed_crop_sha256") ==
        "42cc62f823284c41d768bc05b6195cf291879fb4f6f04337a327ab79806ef5d8" and
        input_identity.get("recipe_sha256") == RECIPE_SHA256[256] and
        input_identity.get("shader_map_sha256") == shader_map_sha
    )
    identity_matches = (
        parent.get("phase") == "hydraulics" and
        parent.get("domain") == 256 and
        parent.get("app_sha256") == app_sha and
        parent.get("shader_map_sha256") == shader_map_sha and
        parent.get("recipe_sha256") == RECIPE_SHA256[256] and
        parent.get("manifest_sha256") == MANIFEST_SHA256 and
        parent.get("elevation_sha256") == ELEVATION_SHA256 and
        input_identity.get("forcing_identity") == expected_forcing and
        fixed_crop and hashes_match and child_matches and
        _has_edge_trigger(parent.get("summary", {}))
    )
    healthy = (
        parent.get("phase_checks_passed") is True and
        parent.get("summary", {}).get("numeric_profile_checks_passed") is True and
        parent.get("summary", {}).get("unhealthy_frames") == [] and
        parent.get("baseline_prefix_comparison", {}).get("passed") is True
    )
    diagnostic = allow_diagnostic_unhealthy and diagnostic_ledger_failure_only(parent)
    if not identity_matches or not (healthy or diagnostic):
        raise ValueError(
            "512 domain requires successful current-app 256 hydraulics, or the explicitly "
            "allowed ledger-only diagnostic parent, with matching forcing and a real edge trigger"
        )
    return {
        "path": str(path.resolve()),
        "sha256": sha256_file(path),
        "app_sha256": app_sha,
        "shader_map_sha256": shader_map_sha,
        "elevation_sha256": ELEVATION_SHA256,
        "recipe_sha256": RECIPE_SHA256[256],
        "manifest_sha256": MANIFEST_SHA256,
        "domain": 256,
        "forcing_identity": expected_forcing,
        "diagnostic_only": bool(diagnostic),
        "phase_checks_passed": bool(healthy),
        "baseline_prefix_passed": bool(parent.get("baseline_prefix_comparison", {}).get("passed")),
        "first_edge_trigger": parent["summary"]["first_edge_trigger"],
    }


def video_label_filter(*, diagnostic_only: bool) -> str:
    title = ("Hillside supply | dry start | 100 m3/s continuous source | "
             "no prescribed drain | 60x playback")
    # Captures are frame-indexed states after each 2-second step; avoid relying
    # on floating presentation timestamps for exact physical-time captions.
    physical = r"Physical time %{eif\:n*2+2\:d} s / 7200 s"
    filters = [
        f"drawtext=text='{title}':x=12:y=12:fontsize=18:fontcolor=white:box=1:boxcolor=black@0.8:boxborderw=6",
        f"drawtext=text='{physical}':x=12:y=44:fontsize=18:fontcolor=white:box=1:boxcolor=black@0.8:boxborderw=6",
    ]
    if diagnostic_only:
        filters.extend((
            "drawtext=text='DIAGNOSTIC ONLY | water ledger tolerance failed':"
            "x=(w-text_w)/2:y=82:fontsize=28:fontcolor=white:box=1:boxcolor=red@0.92:boxborderw=10",
            "drawtext=text='Moving highlight is render-only, not conserved dye':"
            "x=12:y=126:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.85:boxborderw=7",
        ))
    return ",".join(filters)


def label_video(
    source: Path,
    out: Path,
    *,
    diagnostic_only: bool = False,
    capture_label: str | None = None,
) -> dict[str, Any]:
    stem = capture_label or source.stem
    target = out / (stem + "-labelled.mp4")
    log = out / (stem + "-label.log")
    ensure_absent([target, log])
    filters = video_label_filter(diagnostic_only=diagnostic_only)
    command = [
        "rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-i", str(source.resolve()),
        "-vf", filters, "-c:v", "libx264", "-crf", "20", "-an", str(target.resolve()),
    ]
    started = time.perf_counter()
    with log.open("w") as stream:
        result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    return {
        "exit_code": result.returncode,
        "wall_seconds": time.perf_counter() - started,
        "command": command,
        "log": str(log.resolve()),
        "path": str(target.resolve()),
        "sha256": sha256_file(target) if result.returncode == 0 and target.is_file() else None,
        "source_sha256": sha256_file(source),
        "physical_evolution_seconds": HORIZON_SECONDS,
        "video_fps": FPS,
        "playback_acceleration": PLAYBACK_ACCELERATION,
        "expected_video_duration_seconds": HORIZON_SECONDS / PLAYBACK_ACCELERATION,
        "diagnostic_only": diagnostic_only,
        "diagnostic_banner": (
            "DIAGNOSTIC ONLY | water ledger tolerance failed; moving highlight is "
            "render-only, not conserved dye"
            if diagnostic_only else None
        ),
    }


def captures_phase(app: Path, out: Path, domain: int, report: dict[str, Any]) -> None:
    hydraulic, hydraulic_path = load_matching_hydraulics(
        out,
        report,
        allow_diagnostic_unhealthy=bool(report.get("diagnostic_unhealthy_requested")),
    )
    report["hydraulics_report_sha256"] = sha256_file(hydraulic_path)
    report["hydraulic_app_sha256"] = hydraulic["app_sha256"]
    diagnostic = not bool(hydraulic.get("phase_checks_passed"))
    report["diagnostic_only"] = diagnostic
    report["inherited_parent_failure"] = bool(
        hydraulic.get("inherited_parent_failure") or
        hydraulic.get("parent_evidence", {}).get("diagnostic_only") or
        (diagnostic and domain == 256)
    )
    if diagnostic:
        report["diagnostic_reason"] = "water ledger tolerance failed in inherited hydraulic evidence"
        report["review_status"] = "diagnostic only; numerical acceptance remains failed"
    report["hydraulic_summary_sha256"] = hashlib.sha256(
        json.dumps(hydraulic["summary"], sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    common = arguments(domain)
    capture_specs = (
        ("overview-composite", "composite", "-0.72", []),
        ("close-oblique-composite", "composite", "-0.72",
         ["--fluid25d-hillside-source-context"]),
        ("close-topdown-water-isolation", "water-isolation", "-1.55",
         ["--fluid25d-hillside-source-context"]),
    )
    planned_paths = []
    for label, _, _, _ in capture_specs:
        raw_name = f"{label}.diagnostic-unlabelled-raw.mp4" if diagnostic else f"{label}.mp4"
        raw = out / raw_name
        log = out / f"{label}.log"
        labelled = out / f"{label}-labelled.mp4"
        label_log = out / f"{label}-label.log"
        planned_paths.extend((raw, log, labelled, label_log))
    ensure_absent(planned_paths)
    for label, view, pitch, extra in capture_specs:
        raw_name = f"{label}.diagnostic-unlabelled-raw.mp4" if diagnostic else f"{label}.mp4"
        raw = out / raw_name
        command_args = common + [
            "--frames", str(HORIZON_SECONDS // DT), "--capture", "video", "--fps", str(FPS),
            "--fluid25d-catchment-view", view,
            "--fluid25d-natural-flow-home-pitch-radians", pitch,
            *extra, "--output", str(raw.resolve()),
        ]
        execution = run_command(app, command_args, out / f"{label}.log")
        execution.update({
            "label": label,
            "raw_video_path": str(raw.resolve()),
            "raw_video_sha256": sha256_file(raw) if execution["exit_code"] == 0 and raw.is_file() else None,
            "input_hashes": dict(report["start_input_hashes"]),
            "hydraulics_report_sha256": report["hydraulics_report_sha256"],
            "physical_evolution_seconds": HORIZON_SECONDS,
            "video_fps": FPS,
            "playback_acceleration": PLAYBACK_ACCELERATION,
            "expected_video_duration_seconds": HORIZON_SECONDS / PLAYBACK_ACCELERATION,
            "water_label": "dry start; 100 m3/s continuous source; no prescribed drain",
            "diagnostic_only": diagnostic,
            "raw_video_status": (
                "unlabelled; not reviewed; not validated; retained for provenance only"
                if diagnostic else "unlabelled source capture; labelled counterpart is the review artifact"
            ),
            "moving_highlight_interpretation": (
                "render-only, not conserved dye" if label == "close-oblique-composite" else None
            ),
        })
        report["cases"].append(execution)
        write_report(report["report_path"], report)
        if execution["exit_code"] != 0:
            report["error"] = f"capture child failed for {label} with exit code {execution['exit_code']}"
            break
        if execution["raw_video_sha256"] is None:
            report["error"] = f"capture child returned success without producing {raw}"
            break
        try:
            execution["labelled_video"] = label_video(
                raw, out, diagnostic_only=diagnostic, capture_label=label
            )
        except (OSError, FileExistsError) as error:
            execution["error"] = f"video labelling failed: {error}"
            report["error"] = execution["error"]
            break
        if execution["labelled_video"]["exit_code"] != 0:
            report["error"] = f"video labelling failed for {label}; raw video retained"
            break
        write_report(report["report_path"], report)
    if report.get("error") is None and sha256_file(hydraulic_path) != report["hydraulics_report_sha256"]:
        report["error"] = "matching hydraulics report changed during captures"
    report["capture_artifacts_completed"] = (
        len(report.get("cases", [])) == 3 and all(
            case.get("exit_code") == 0 and case.get("raw_video_sha256") and
            case.get("labelled_video", {}).get("exit_code") == 0 and
            case.get("labelled_video", {}).get("sha256")
            for case in report["cases"]
        )
    )
    report["artifact_completion_passed"] = report["capture_artifacts_completed"]
    write_report(report["report_path"], report)


def oracles_phase(app: Path, out: Path, domain: int, report: dict[str, Any]) -> None:
    report["strict_oracle_coverage"] = {
        "planned_physical_seconds": list(STRICT_ORACLE_SECONDS),
        "checks": [],
        "complete": False,
        "7200s_profile_is_not_strict_parity": True,
    }
    ensure_absent([
        *(out / f"strict-oracle-{seconds}s.png" for seconds in STRICT_ORACLE_SECONDS),
        *(out / f"strict-oracle-{seconds}s.log" for seconds in STRICT_ORACLE_SECONDS),
    ])
    for seconds in STRICT_ORACLE_SECONDS:
        frames = seconds // DT
        image = out / f"strict-oracle-{seconds}s.png"
        args = arguments(domain) + [
            "--frames", str(frames), "--capture", "png",
            "--fluid25d-catchment-view", "water-isolation",
            "--fluid25d-natural-flow-home-pitch-radians", "-1.55",
            "--fluid25d-gpu-oracle-validation", "--output", str(image.resolve()),
        ]
        execution = run_command(app, args, out / f"strict-oracle-{seconds}s.log")
        execution["output_path"] = str(image.resolve())
        execution["output_sha256"] = (
            sha256_file(image) if execution["exit_code"] == 0 and image.is_file() else None
        )
        check = {
            "physical_seconds": seconds,
            "frame_count": frames,
            "execution": execution,
            "input_hashes": dict(report["start_input_hashes"]),
            "strict_cpu_gpu_oracle": True,
        }
        report["strict_oracle_coverage"]["checks"].append(check)
        write_report(report["report_path"], report)
        if execution["exit_code"] != 0:
            report["strict_oracle_coverage"]["stopped_at_first_failure_seconds"] = seconds
            break
    coverage = report["strict_oracle_coverage"]["checks"]
    report["strict_oracle_coverage"]["complete"] = (
        [item["physical_seconds"] for item in coverage] == list(STRICT_ORACLE_SECONDS) and
        all(item["execution"]["exit_code"] == 0 for item in coverage)
    )
    report["strict_oracle_coverage"]["successful_physical_seconds"] = [
        item["physical_seconds"] for item in coverage if item["execution"]["exit_code"] == 0
    ]
    write_report(report["report_path"], report)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=DEFAULT_APP)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--domain", type=int, choices=(256, 512), default=256)
    parser.add_argument("--phase", choices=("hydraulics", "captures", "oracles"), required=True)
    parser.add_argument("--parent-evidence", type=Path,
                        help="successful triggered current-app 256 hydraulics.json required for domain 512")
    parser.add_argument("--diagnostic-unhealthy", action="store_true",
                        help="allow only bounded ledger-failure diagnostic captures or domain-512 hydraulics")
    return parser.parse_args()


def validate_diagnostic_scope(phase: str, domain: int, enabled: bool) -> None:
    if enabled and not (phase == "captures" or (phase == "hydraulics" and domain == 512)):
        raise ValueError(
            "--diagnostic-unhealthy is limited to captures or domain-512 hydraulics; "
            "it never relaxes oracle or domain-256 hydraulics gates"
        )


def concise_status(report: dict[str, Any], report_path: Path) -> dict[str, Any]:
    summary = report.get("summary", {})
    oracle = report.get("strict_oracle_coverage", {})
    return {
        "phase": report.get("phase"),
        "domain": report.get("domain"),
        "phase_checks_passed": report.get("phase_checks_passed") is True,
        "exit_code": phase_exit_code(report),
        "diagnostic_only": bool(report.get("diagnostic_only")),
        "inherited_parent_failure": bool(report.get("inherited_parent_failure")),
        "numeric_profile_checks_passed": summary.get("numeric_profile_checks_passed"),
        "hydraulic_child_completed": report.get("hydraulic_child_completed"),
        "profile_analysis_completed": report.get("profile_analysis_completed"),
        "artifact_completion_passed": report.get("artifact_completion_passed"),
        "strict_oracle_complete": oracle.get("complete"),
        "case_count": len(report.get("cases", [])),
        "first_edge_trigger": summary.get("first_edge_trigger"),
        "error": report.get("error"),
        "report_path": str(report_path),
    }


def phase_exit_code(report: dict[str, Any]) -> int:
    return 0 if report.get("phase_checks_passed") is True else 1


def main() -> None:
    args = parse_args()
    app = args.app.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report_path = out / f"{args.phase}.json"
    if report_path.exists():
        raise SystemExit(f"refusing to overwrite report: {report_path}")
    report: dict[str, Any] = {
        "schema": "cubey.fluid25d.hillside_sustained_flow_v2",
        "phase": args.phase,
        "domain": args.domain,
        "report_path": str(report_path),
        "app_path": str(app),
        "recipe_sha256": RECIPE_SHA256[args.domain],
        "manifest_sha256": MANIFEST_SHA256,
        "runner_sha256": sha256_file(Path(__file__).resolve()),
        "diagnostic_unhealthy_requested": args.diagnostic_unhealthy,
        "frozen_protocol": {
            "source_rate_m3_per_s": Q_M3_PER_S,
            "initial_water": "dry",
            "explicit_sinks": "none",
            "rain": "none",
            "fixed_delta_seconds": DT,
            "substeps": SUBSTEPS,
            "gravity_m_per_s2": GRAVITY_M_PER_S2,
            "damping_per_second": DAMPING_PER_SECOND,
            "native_cell_size_m": CELL_SIZE_M,
            "horizon_seconds": HORIZON_SECONDS,
            "source_full_map_center_xz": list(SOURCE_FULL_MAP_XZ),
            "material_depth_threshold_m": MATERIAL_DEPTH_THRESHOLD_M,
            "active_speed_threshold_m_per_s": ACTIVE_SPEED_THRESHOLD_M_PER_S,
        },
        "cases": [],
    }
    write_report(report_path, report)
    try:
        validate_diagnostic_scope(args.phase, args.domain, args.diagnostic_unhealthy)
        start = pinned_inputs(args.domain, app)
        report["app_sha256"] = start["app_sha256"]
        report["recipe_sha256"] = start["recipe_sha256"]
        report["manifest_sha256"] = start["manifest_sha256"]
        report["elevation_sha256"] = start["elevation_sha256"]
        report["recipe_path"] = start["recipe_path"]
        report["manifest_path"] = start["manifest_path"]
        report["elevation_path"] = start["elevation_path"]
        report["input_identity"] = start["input_identity"]
        report["shader_identity"] = start["shader_identity"]
        report["shader_map_sha256"] = start["shader_map_sha256"]
        report["start_input_hashes"] = {
            name: start[name]
            for name in ("app_sha256", "recipe_sha256", "manifest_sha256", "elevation_sha256",
                         "shader_map_sha256")
        }
        if args.domain == 512:
            if args.parent_evidence is None:
                raise ValueError("--domain 512 requires --parent-evidence")
            parent = args.parent_evidence.resolve()
            report["parent_evidence"] = verify_parent_evidence(
                parent,
                start["app_sha256"],
                start["shader_map_sha256"],
                start["input_identity"]["forcing_identity"],
                allow_diagnostic_unhealthy=args.diagnostic_unhealthy,
            )
            report["parent_evidence_path"] = str(parent)
            report["parent_evidence_sha256"] = report["parent_evidence"]["sha256"]
            report["inherited_parent_failure"] = bool(report["parent_evidence"]["diagnostic_only"])
            if report["inherited_parent_failure"]:
                report["diagnostic_only"] = True
        elif args.parent_evidence is not None:
            raise ValueError("--parent-evidence is only valid with --domain 512")
        write_report(report_path, report)

        if args.phase == "hydraulics":
            hydraulics_phase(app, out, args.domain, report)
        elif args.phase == "captures":
            captures_phase(app, out, args.domain, report)
        else:
            oracles_phase(app, out, args.domain, report)
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        report["error"] = str(error)

    try:
        end = pinned_inputs(args.domain, app)
        report["end_input_hashes"] = {
            name: end[name]
            for name in ("app_sha256", "recipe_sha256", "manifest_sha256", "elevation_sha256",
                         "shader_map_sha256")
        }
        report["end_shader_identity"] = end["shader_identity"]
        try:
            require_stable_inputs(report.get("start_input_hashes", {}), report["end_input_hashes"])
        except ValueError as error:
            report["error"] = (report.get("error", "") + f"; {error}").lstrip("; ")
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        report["end_input_hash_error"] = str(error)
        report["error"] = (report.get("error", "") + f"; end-of-phase input pin check failed: {error}").lstrip("; ")
    if report.get("parent_evidence_path"):
        try:
            if sha256_file(Path(report["parent_evidence_path"])) != report["parent_evidence_sha256"]:
                report["error"] = (report.get("error", "") + "; parent evidence changed during phase").lstrip("; ")
        except OSError as error:
            report["error"] = (report.get("error", "") + f"; parent evidence end check failed: {error}").lstrip("; ")
    if args.phase == "captures" and report.get("hydraulics_report_sha256"):
        try:
            if sha256_file(out / "hydraulics.json") != report["hydraulics_report_sha256"]:
                report["error"] = (report.get("error", "") + "; hydraulics report changed during captures").lstrip("; ")
        except OSError as error:
            report["error"] = (report.get("error", "") + f"; hydraulics report end check failed: {error}").lstrip("; ")
    if (args.phase == "hydraulics" and args.domain == 512 and
            args.diagnostic_unhealthy and
            (report.get("inherited_parent_failure") or diagnostic_ledger_failure_only(report))):
        report["diagnostic_only"] = True
    report["phase_checks_passed"] = report_checks_passed(report)
    report.pop("report_path", None)
    write_report(report_path, report)
    print(json.dumps(concise_status(report, report_path), sort_keys=True))
    status = phase_exit_code(report)
    if status:
        raise SystemExit(status)


if __name__ == "__main__":
    main()
