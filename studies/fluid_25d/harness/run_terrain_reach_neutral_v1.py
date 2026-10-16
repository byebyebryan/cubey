#!/usr/bin/env python3
"""Run the frozen two-crop neutral terrain-reach audition."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
import shlex
import struct
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
RECIPE_PATH = Path(__file__).resolve().with_name("terrain_reach_neutral_v1_20260923.json")
RECIPE_SHA256 = "cbcfb4040ed3e99d08a4fd54f17159a7f07b2c7476ed306f2c42fbfc95d9004b"
SCAN_PATH = ROOT / "outputs/fluid/terrain-reach-scan-v1-final-20260923/scan.json"
SCAN_SHA256 = "60bdd07c9a7fb6db6bb4c0de890fa2a49715284c5a5bdaf5e14061a449d09428"
OUTPUT_ROOT = ROOT / "outputs/fluid/terrain-reach-neutral-v1-20260923"
DEFAULT_APP = ROOT / "build/dev/projects/fluid/fluid_25d/fluid_25d"
SCHEMA = "cubey.fluid25d.terrain_reach_neutral_v1.recipe"
PINNED_SOURCE = {
    "generator": "terrain-diffusion",
    "id": "terrain-diffusion-30m",
    "code_revision": "82a0431281f21a6ec3d691a12ee61525de5b0790",
    "model_id": "xandergos/terrain-diffusion-30m",
    "model_revision": "9ef8030cb805b433b98ec25c5dddefbac07a9e26",
    "native_resolution_m": 30.0,
}
PROFILE_FIELDS = (
    "wet_cell_count",
    "wet_cell_ratio",
    "total_water_volume_m3",
    "maximum_depth_m",
    "wet_mean_depth_m",
    "active_flow_cell_count",
    "active_flow_cell_ratio",
    "maximum_speed_m_per_s",
    "active_mean_speed_m_per_s",
    "slow_pooled_wet_fraction",
    "cumulative_source_volume_m3",
    "cumulative_sink_volume_m3",
    "cumulative_boundary_outflow_volume_m3",
    "conservation_residual_m3",
)
RUNTIME_IDENTITY = re.compile(
    r"terrain-v1:elevation-sha256=(?P<elevation>[0-9a-f]{64}):"
    r"crop-sha256=(?P<crop_sha>[0-9a-f]{64}):"
    r"crop=(?P<x>\d+),(?P<z>\d+),(?P<width>\d+)x(?P<height>\d+):"
    r"spacing-m=(?P<spacing>[0-9.]+)"
)
SOURCE_HASH_PATHS = (
    "projects/fluid/fluid_25d/fluid_25d_project_config.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_app.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_commands.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_commands.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_config.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_diagnostics.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_diagnostics.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_finite_volume_oracle.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_gpu_resources.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_gpu_resources.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_presentation.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_scenarios.h",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d.vert",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_render.frag",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_terrain.vert",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_terrain.frag",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_water.vert",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_water.frag",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_fv_cfl.comp",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_fv_cfl_finalize.comp",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_fv_commit.comp",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_fv_reset.comp",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_fv_update.comp",
)


class NeutralReachError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise NeutralReachError(message)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _float32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _linear_percentile(values: list[float], quantile: float) -> float:
    position = (len(values) - 1) * quantile / 100.0
    lower = math.floor(position)
    upper = math.ceil(position)
    blend = position - lower
    return values[lower] * (1.0 - blend) + values[upper] * blend


def resolve_repo_path(root: Path, relative_path: str) -> Path:
    path = Path(relative_path)
    require(not path.is_absolute() and ".." not in path.parts, f"expected repo-relative path: {relative_path}")
    resolved = (root / path).resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError as error:
        raise NeutralReachError(f"path escapes repository root: {relative_path}") from error
    return resolved


def validate_recipe_structure(recipe: dict[str, Any]) -> None:
    require(recipe.get("schema") == SCHEMA, "unsupported neutral terrain-reach recipe schema")
    require(
        recipe.get("scan_identity")
        == {
            "id": "terrain-reach-scan-v1-final-20260923",
            "path": "outputs/fluid/terrain-reach-scan-v1-final-20260923/scan.json",
            "sha256": SCAN_SHA256,
            "schema": "cubey.fluid25d.terrain_reach_scan.v1",
        },
        "recipe does not pin the final terrain-reach scan",
    )
    source = recipe.get("source_identity")
    expected_source = {
        **PINNED_SOURCE,
        "manifest_path": "cache/terrain/sources/v1/landscape-variations/rolling-wet-lowland/heightfield.json",
        "manifest_sha256": "bdf0d86185147947ea02402cd5f215fab28eb629e7b1dc9785c034d77e85aefc",
        "elevation_sha256": "fd52d7c3b25f139ac709b8ced673ccc77d749cc33e4c52e66745494a86dcf7a2",
        "landscape_variant": "rolling-wet-lowland",
        "seed": 12345,
    }
    require(source == expected_source, "recipe producer/manifest/source pins differ from the approved final scan")
    require(
        recipe.get("source_grid")
        == {
            "width": 2048,
            "height": 2048,
            "sample_spacing_m": 30.0,
            "axis_mapping": {"world_x": "model_j", "world_z": "model_i"},
            "dtype": "float32-le",
            "layout": "row-major-zx",
            "unit": "m",
        },
        "recipe does not use the pinned native terrain sample layout",
    )
    require(
        recipe.get("solver_grid")
        == {"width": 256, "height": 128, "cell_size_m": 30, "extent_m": {"x": 7680.0, "z": 3840.0}},
        "solver grid is not a native 256x128 crop at 30 metre cell spacing",
    )
    expected_protocol = {
        "solver": "finite-volume",
        "scenario": "terrain-case",
        "terrain_water_protocol": "rain-pulse",
        "initial_water_depth_m": 0.0,
        "rainfall_mm_per_hour": 12,
        "rain_duration_seconds": 600,
        "drain_duration_seconds": 600,
        "fixed_delta_seconds": 2,
        "substeps": 8,
        "total_frames": 600,
        "total_simulation_seconds": 1200,
        "explicit_inlet": False,
        "explicit_sink": False,
        "boundary_policy": "existing-terrain-case-open-outflow-only-perimeter",
        "profile_diagnostic_interval_frames": 15,
        "profile_final_capture_frame": 600,
        "profile_exact_final_frame_sample": True,
        "profile_exact_final_sample_interval_frames": 599,
        "composite_capture_frames": [1, 300, 600],
        "water_depth_capture_frames": [300, 600],
        "flow_capture_frames": [300, 600],
        "capture_width": 1280,
        "capture_height": 720,
        "max_status_flags": 0,
        "max_conservation_residual_fraction_of_source": 0.0001,
        "expected_uniform_rain_volume_m3": 58982.4,
    }
    require(recipe.get("protocol") == expected_protocol, "solver protocol or capture/profile schedule was altered")
    expected_render = {
        "physics_independent": True,
        "height_scale": 0.6,
        "palette_low_physical_m": 74.773376,
        "palette_high_physical_m": 212.025720,
        "palette_basis": "common envelope of the two runtime-transformed crop p05/p95 ranges; bounds remain in physical metres",
        "home_camera_distance_m": 8043.75,
        "camera_basis": "same 256x128x30m domain and identical explicit distance for both crops",
    }
    require(recipe.get("render_policy") == expected_render, "common render-only policy was altered")

    expected_cases = [
        {
            "id": "broad-valley-contrast-x384-z837",
            "rank": 1,
            "review_role": "broad-valley-morphology-contrast",
            "morphology_is_not_flow_truth": True,
            "manifest_path": source["manifest_path"],
            "manifest_sha256": source["manifest_sha256"],
            "elevation_sha256": source["elevation_sha256"],
            "context_crop": {"x": 128, "z": 768, "width": 512, "height": 256},
            "crop": {"x": 384, "z": 837, "width": 256, "height": 128, "sample_spacing_m": 30.0},
            "transformed_crop_sha256": "1cabf75012716793b510dc6958c5cb05f0916356d86736fd02e59461f0016052",
            "runtime_transformed_crop_percentiles_m": {
                "minimum": 60.027912,
                "p05": 74.773376,
                "p50": 105.001789,
                "p95": 212.025720,
                "maximum": 288.912018,
            },
            "scan_metrics": {
                "regional_relief_p05_p95_m": 137.252344,
                "median_floor_width_m": 210.0,
                "paired_bank_ratio": 1.0,
                "trunk_length_m": 6121.980515,
                "grade": 0.0054803,
                "max_smoothed_adverse_rise_m": 0.536258,
            },
        },
        {
            "id": "narrower-river-reach-candidate-x843-z809",
            "rank": 2,
            "review_role": "narrower-channel-morphology-candidate",
            "morphology_is_not_flow_truth": True,
            "manifest_path": source["manifest_path"],
            "manifest_sha256": source["manifest_sha256"],
            "elevation_sha256": source["elevation_sha256"],
            "context_crop": {"x": 640, "z": 768, "width": 512, "height": 256},
            "crop": {"x": 843, "z": 809, "width": 256, "height": 128, "sample_spacing_m": 30.0},
            "transformed_crop_sha256": "6a9818526757333925536ba3abd0d3394981797dea4e83d7b2f182ea432d3485",
            "runtime_transformed_crop_percentiles_m": {
                "minimum": 64.735695,
                "p05": 85.791412,
                "p50": 117.497948,
                "p95": 151.275988,
                "maximum": 186.255173,
            },
            "scan_metrics": {
                "regional_relief_p05_p95_m": 65.484576,
                "median_floor_width_m": 90.0,
                "paired_bank_ratio": 1.0,
                "trunk_length_m": 8168.376618,
                "grade": 0.008407016,
                "max_smoothed_adverse_rise_m": 2.749917,
            },
            "review_limitation": "Last measured transverse station has a weaker right bank; treat as possible overbank/spill limitation, not hydrologic truth.",
        },
    ]
    require(recipe.get("cases") == expected_cases, "recipe crops/roles/bytes differ from the reviewed scan shortlist")
    require(
        recipe.get("selection_boundary")
        == "This bounded shared-forcing audition compares two scanner-selected morphology candidates only. It uses immutable terrain and an existing neutral solver protocol; it does not validate hydrology, add endpoint forcing, or promote a terrain fixture/default.",
        "recipe selection boundary is missing or changed",
    )


def load_frozen_recipe(recipe_path: Path = RECIPE_PATH, root: Path = ROOT) -> dict[str, Any]:
    require(recipe_path.resolve() == RECIPE_PATH.resolve(), f"only the checked-in recipe is accepted: {RECIPE_PATH}")
    require(recipe_path.is_file(), f"frozen recipe is missing: {recipe_path}")
    digest = sha256_file(recipe_path)
    require(digest == RECIPE_SHA256, f"frozen recipe SHA-256 mismatch: {digest}")
    recipe = json.loads(recipe_path.read_text())
    validate_recipe_structure(recipe)
    return recipe


def _scan_shortlist_case(scan: dict[str, Any], case: dict[str, Any]) -> dict[str, Any]:
    matches = [
        item
        for item in scan.get("visual_shortlist", [])
        if item.get("rank") == case["rank"]
        and item.get("manifest_path") == case["manifest_path"]
        and item.get("nested_reach", {}).get("x") == case["crop"]["x"]
        and item.get("nested_reach", {}).get("z") == case["crop"]["z"]
    ]
    require(len(matches) == 1, f"final scan does not contain exactly one frozen shortlist row for {case['id']}")
    return matches[0]


def _compare_scan_case(case: dict[str, Any], item: dict[str, Any]) -> None:
    require(item.get("manifest_sha256") == case["manifest_sha256"], f"scan manifest pin mismatch for {case['id']}")
    require(item.get("elevation_sha256") == case["elevation_sha256"], f"scan elevation pin mismatch for {case['id']}")
    nested = item["nested_reach"]
    require(
        (nested["x"], nested["z"], nested["width"], nested["height"])
        == (case["crop"]["x"], case["crop"]["z"], case["crop"]["width"], case["crop"]["height"]),
        f"scan nested crop mismatch for {case['id']}",
    )
    metrics = nested["metrics"]
    expected_metrics = case["scan_metrics"]
    metric_keys = {
        "regional_relief_p05_p95_m": "p05_p95_relief_m",
        "median_floor_width_m": "median_floor_width_m",
        "paired_bank_ratio": "paired_bank_ratio",
        "trunk_length_m": "longest_trunk_path_m",
        "grade": "path_end_to_end_slope",
        "max_smoothed_adverse_rise_m": "path_max_downstream_adverse_rise_m",
    }
    for recipe_key, scan_key in metric_keys.items():
        require(
            math.isclose(float(metrics[scan_key]), float(expected_metrics[recipe_key]), rel_tol=1.0e-6, abs_tol=1.0e-5),
            f"scan metric {scan_key} differs from frozen recipe for {case['id']}",
        )


def _crop_statistics(elevation_path: Path, manifest: dict[str, Any], case: dict[str, Any]) -> dict[str, Any]:
    grid = manifest["grid"]
    crop = case["crop"]
    width = int(grid["width"])
    source_width = int(crop["width"])
    source_height = int(crop["height"])
    x0 = int(crop["x"])
    z0 = int(crop["z"])
    require(
        x0 >= 0 and z0 >= 0 and x0 + source_width <= width and z0 + source_height <= int(grid["height"]),
        f"crop bounds exceed source raster for {case['id']}",
    )
    height_offset = _float32(float(manifest["height"]["offset_m"]))
    height_scale = _float32(float(manifest["height"]["scale"]))
    values: list[float] = []
    digest = hashlib.sha256()
    expected_bytes = int(grid["width"]) * int(grid["height"]) * 4
    require(elevation_path.stat().st_size == expected_bytes, "pinned elevation payload byte size mismatch")
    with elevation_path.open("rb") as stream:
        for z in range(z0, z0 + source_height):
            stream.seek((z * width + x0) * 4)
            row = stream.read(source_width * 4)
            require(len(row) == source_width * 4, f"truncated elevation crop row for {case['id']}")
            for (raw_height,) in struct.iter_unpack("<f", row):
                value = _float32(_float32(raw_height + height_offset) * height_scale)
                require(math.isfinite(value), f"non-finite transformed crop value for {case['id']}")
                values.append(value)
                digest.update(struct.pack("<f", value))
    require(len(values) == source_width * source_height, f"transformed crop sample count mismatch for {case['id']}")
    require(digest.hexdigest() == case["transformed_crop_sha256"], f"transformed crop bytes mismatch for {case['id']}")
    values.sort()
    percentiles = case["runtime_transformed_crop_percentiles_m"]
    actual = {
        "minimum": values[0],
        "p05": _linear_percentile(values, 5.0),
        "p50": _linear_percentile(values, 50.0),
        "p95": _linear_percentile(values, 95.0),
        "maximum": values[-1],
    }
    for name, expected in percentiles.items():
        require(math.isclose(actual[name], float(expected), rel_tol=0.0, abs_tol=0.00002), f"transformed crop {name} mismatch for {case['id']}")
    return {
        "transformed_crop_sha256": digest.hexdigest(),
        "source_height_transform": {"offset_float32": height_offset, "scale_float32": height_scale},
        "runtime_transformed_crop_percentiles_m": actual,
    }


def _source_preflight(root: Path, recipe: dict[str, Any]) -> tuple[Path, Path, dict[str, Any]]:
    scan_path = resolve_repo_path(root, recipe["scan_identity"]["path"])
    require(scan_path == SCAN_PATH.resolve(), "recipe scan path differs from the final pinned scan")
    require(sha256_file(scan_path) == SCAN_SHA256, "final terrain-reach scan SHA-256 mismatch")
    scan = json.loads(scan_path.read_text())
    require(scan.get("schema") == recipe["scan_identity"]["schema"], "final scan schema mismatch")
    require(scan.get("status") == "PASS_CANDIDATE_FOUND", "final scan did not pass its candidate-found gate")
    require(scan.get("solver_input_generated") is False, "final scanner unexpectedly generated solver input")
    require(scan.get("new_seed_generation_performed") is False, "final scanner unexpectedly generated terrain seeds")

    manifest_path = resolve_repo_path(root, recipe["source_identity"]["manifest_path"])
    require(sha256_file(manifest_path) == recipe["source_identity"]["manifest_sha256"], "source manifest SHA-256 mismatch")
    manifest = json.loads(manifest_path.read_text())
    require(manifest.get("schema") == "cubey.terrain.heightfield.v1", "unsupported Terrain Diffusion manifest schema")
    require(
        isinstance(manifest.get("source"), dict)
        and all(manifest["source"].get(key) == value for key, value in PINNED_SOURCE.items()),
        "Terrain Diffusion generator/model revision pin mismatch",
    )
    require(manifest.get("seed") == recipe["source_identity"]["seed"], "source seed differs from the frozen reach scan")
    require(manifest.get("provenance", {}).get("landscape_variant") == recipe["source_identity"]["landscape_variant"], "source landscape variant mismatch")
    grid = manifest.get("grid", {})
    require(
        grid.get("width") == 2048
        and grid.get("height") == 2048
        and grid.get("sample_spacing_m") == 30.0
        and grid.get("axis_mapping") == {"world_x": "model_j", "world_z": "model_i"},
        "pinned source is not the expected native 2048x2048 30m raster",
    )
    elevation_record = manifest.get("files", {}).get("elevation", {})
    require(
        elevation_record.get("path") == "elevation.f32"
        and elevation_record.get("dtype") == "float32-le"
        and elevation_record.get("layout") == "row-major-zx"
        and elevation_record.get("shape") == [2048, 2048]
        and elevation_record.get("unit") == "m"
        and elevation_record.get("byte_count") == 2048 * 2048 * 4
        and elevation_record.get("sha256") == recipe["source_identity"]["elevation_sha256"],
        "pinned source elevation metadata mismatch",
    )
    elevation_path = manifest_path.parent / elevation_record["path"]
    require(elevation_path.is_file() and sha256_file(elevation_path) == recipe["source_identity"]["elevation_sha256"], "pinned source elevation payload SHA-256 mismatch")

    crop_stats: dict[str, Any] = {}
    for case in recipe["cases"]:
        require(case["manifest_path"] == recipe["source_identity"]["manifest_path"], f"case source path differs for {case['id']}")
        require(case["manifest_sha256"] == recipe["source_identity"]["manifest_sha256"], f"case manifest pin differs for {case['id']}")
        require(case["elevation_sha256"] == recipe["source_identity"]["elevation_sha256"], f"case elevation pin differs for {case['id']}")
        _compare_scan_case(case, _scan_shortlist_case(scan, case))
        crop_stats[case["id"]] = _crop_statistics(elevation_path, manifest, case)
    return manifest_path, elevation_path, {"scan": scan, "crop_statistics": crop_stats}


def preflight(app_path: Path, root: Path = ROOT) -> dict[str, Any]:
    app_path = app_path.resolve()
    require(app_path.is_file(), f"fluid_25d application is missing: {app_path}")
    require(bool(app_path.stat().st_mode & 0o111), f"fluid_25d application is not executable: {app_path}")
    recipe = load_frozen_recipe(RECIPE_PATH, root)
    manifest_path, elevation_path, sources = _source_preflight(root, recipe)
    shader_dir = app_path.parent / "shaders"
    shader_assets = sorted(shader_dir.glob("*.spv"))
    require(bool(shader_assets), f"built Fluid 2.5D shader assets are missing: {shader_dir}")
    source_hashes: dict[str, str] = {}
    for relative in SOURCE_HASH_PATHS:
        source_path = resolve_repo_path(root, relative)
        require(source_path.is_file(), f"required audited source is missing: {source_path}")
        source_hashes[relative] = sha256_file(source_path)
    return {
        "recipe": recipe,
        "manifest_path": manifest_path,
        "elevation_path": elevation_path,
        "scan": sources["scan"],
        "crop_statistics": sources["crop_statistics"],
        "app": {"path": str(app_path), "sha256": sha256_file(app_path), "size_bytes": app_path.stat().st_size},
        "shader_assets": {path.name: sha256_file(path) for path in shader_assets},
        "source_hashes": source_hashes,
    }


def render_args(recipe: dict[str, Any]) -> list[str]:
    render = recipe["render_policy"]
    return [
        "--fluid25d-terrain-palette-low-m",
        f"{render['palette_low_physical_m']:.6f}",
        "--fluid25d-terrain-palette-high-m",
        f"{render['palette_high_physical_m']:.6f}",
        "--fluid25d-render-height-scale",
        f"{render['height_scale']:.6f}",
        "--fluid25d-home-camera-distance-m",
        f"{render['home_camera_distance_m']:.2f}",
    ]


def common_app_args(
    app: Path,
    case: dict[str, Any],
    manifest_path: Path,
    recipe: dict[str, Any],
    frames: int,
) -> list[str]:
    protocol = recipe["protocol"]
    crop = case["crop"]
    return [
        str(app),
        "--headless",
        "--capture",
        "png",
        "--frames",
        str(frames),
        "--width",
        str(protocol["capture_width"]),
        "--height",
        str(protocol["capture_height"]),
        "--grid-width",
        str(recipe["solver_grid"]["width"]),
        "--grid-height",
        str(recipe["solver_grid"]["height"]),
        "--fluid25d-solver",
        protocol["solver"],
        "--fluid25d-fixed-delta-seconds",
        str(protocol["fixed_delta_seconds"]),
        "--fluid25d-substeps",
        str(protocol["substeps"]),
        "--fluid25d-cell-size-m",
        str(recipe["solver_grid"]["cell_size_m"]),
        "--fluid25d-scenario",
        protocol["scenario"],
        "--terrain-heightfield",
        str(manifest_path.parent),
        "--fluid25d-terrain-crop-x",
        str(crop["x"]),
        "--fluid25d-terrain-crop-z",
        str(crop["z"]),
        "--fluid25d-terrain-water-protocol",
        protocol["terrain_water_protocol"],
        "--fluid25d-rainfall-rate-mm-per-hour",
        str(protocol["rainfall_mm_per_hour"]),
        "--fluid25d-source-active-duration-seconds",
        str(protocol["rain_duration_seconds"]),
    ]


def view_args(view: str) -> list[str]:
    if view == "composite":
        return ["--fluid25d-view", "catchment", "--fluid25d-catchment-view", "composite"]
    if view == "water-depth":
        return ["--fluid25d-view", "diagnostics", "--debug-view", "depth"]
    if view == "flow":
        return ["--fluid25d-view", "diagnostics", "--debug-view", "flow"]
    raise NeutralReachError(f"unsupported capture view: {view}")


def runtime_identity(log_path: Path, case: dict[str, Any]) -> dict[str, Any]:
    match = RUNTIME_IDENTITY.search(log_path.read_text(errors="replace"))
    require(match is not None, f"terrain runtime identity missing from {log_path}")
    identity = match.groupdict()
    crop = case["crop"]
    require(identity["elevation"] == case["elevation_sha256"], f"runtime elevation bytes differ for {case['id']}")
    require(identity["crop_sha"] == case["transformed_crop_sha256"], f"runtime transformed crop bytes differ for {case['id']}")
    require(
        (int(identity["x"]), int(identity["z"]), int(identity["width"]), int(identity["height"]))
        == (crop["x"], crop["z"], crop["width"], crop["height"]),
        f"runtime crop coordinates/dimensions differ for {case['id']}",
    )
    require(float(identity["spacing"]) == crop["sample_spacing_m"], f"runtime terrain spacing differs for {case['id']}")
    return {
        "elevation_sha256": identity["elevation"],
        "transformed_crop_sha256": identity["crop_sha"],
        "crop": {key: crop[key] for key in ("x", "z", "width", "height", "sample_spacing_m")},
    }


def _run_capture(command: list[str], root: Path, log_path: Path, commands_path: Path) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with commands_path.open("a", encoding="utf-8") as commands:
        commands.write(json.dumps({"argv": command, "cwd": str(root), "log": str(log_path)}) + "\n")
        commands.flush()
    with log_path.open("wb") as log_stream:
        completed = subprocess.run(command, cwd=root, stdout=log_stream, stderr=subprocess.STDOUT, check=False)
    if completed.returncode != 0:
        raise NeutralReachError(f"capture failed with exit {completed.returncode}; see {log_path}: {shlex.join(command)}")


def build_view_command(
    app: Path,
    case: dict[str, Any],
    manifest_path: Path,
    recipe: dict[str, Any],
    frame: int,
    view: str,
    capture_path: Path,
    profile_prefix: Path | None = None,
    profile_interval: int | None = None,
) -> list[str]:
    command = [
        *common_app_args(app, case, manifest_path, recipe, frame),
        *view_args(view),
    ]
    if view == "composite":
        command += render_args(recipe)
    if profile_prefix is not None:
        require(profile_interval is not None and profile_interval > 0, "profile output requires a positive interval")
        command += [
            "--profile-output",
            str(profile_prefix),
            "--profile-diagnostics",
            "--profile-diagnostic-interval",
            str(profile_interval),
        ]
    command += ["--output", str(capture_path)]
    return command


def _parse_profile_rows(metrics_path: Path, allowed_frames: set[int]) -> dict[int, dict[str, str]]:
    require(metrics_path.is_file() and metrics_path.stat().st_size > 0, f"profile metrics missing: {metrics_path}")
    grouped: dict[int, dict[str, str]] = {}
    with metrics_path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        require(reader.fieldnames == ["frame_index", "category", "name", "value"], f"unexpected profile CSV header: {metrics_path}")
        for row in reader:
            frame = int(row["frame_index"])
            if frame not in allowed_frames:
                continue
            if row["category"] == "fluid_25d.solver" and row["name"] == "finite_volume_status_flags":
                key = row["name"]
            elif row["category"] == "fluid_25d.water" and row["name"] in PROFILE_FIELDS:
                key = row["name"]
            else:
                continue
            values = grouped.setdefault(frame, {})
            require(key not in values, f"duplicate profile metric at frame {frame}: {key}")
            values[key] = row["value"]
    required = {"finite_volume_status_flags", *PROFILE_FIELDS}
    for frame, values in grouped.items():
        require(required.issubset(values), f"profile frame {frame} missing fields {sorted(required - values.keys())}: {metrics_path}")
    return grouped


def _profile_acceptance(frames: dict[int, dict[str, str]], recipe: dict[str, Any]) -> dict[str, Any]:
    require(599 in frames, "exact f600 profile row (profile frame index 599) is missing")
    max_flags = max(int(round(float(rows["finite_volume_status_flags"]))) for rows in frames.values())
    max_residual = max(abs(float(rows["conservation_residual_m3"])) for rows in frames.values())
    final = frames[599]
    final_source = float(final["cumulative_source_volume_m3"])
    final_sink = float(final["cumulative_sink_volume_m3"])
    require(final_source > 0.0, "f600 cumulative rain-source volume must be positive")
    expected_source = float(recipe["protocol"]["expected_uniform_rain_volume_m3"])
    source_error_fraction = abs(final_source - expected_source) / expected_source
    residual_fraction = max_residual / final_source
    residual_limit = float(recipe["protocol"]["max_conservation_residual_fraction_of_source"])
    return {
        "max_status_flags": max_flags,
        "sampled_profile_frames": sorted(frames),
        "profile_sample_count": len(frames),
        "exact_final_profile_frame_index": 599,
        "exact_final_capture_frame": 600,
        "f600_total_water_volume_m3": float(final["total_water_volume_m3"]),
        "f600_maximum_depth_m": float(final["maximum_depth_m"]),
        "f600_wet_cell_ratio": float(final["wet_cell_ratio"]),
        "f600_active_mean_speed_m_per_s": float(final["active_mean_speed_m_per_s"]),
        "f600_cumulative_source_volume_m3": final_source,
        "f600_cumulative_sink_volume_m3": final_sink,
        "f600_cumulative_boundary_outflow_volume_m3": float(final["cumulative_boundary_outflow_volume_m3"]),
        "maximum_abs_conservation_residual_m3": max_residual,
        "maximum_residual_fraction_of_f600_source": residual_fraction,
        "residual_fraction_limit": residual_limit,
        "rain_source_expected_uniform_volume_m3": expected_source,
        "rain_source_volume_relative_error": source_error_fraction,
        "status_gate_pass": max_flags == int(recipe["protocol"]["max_status_flags"]),
        "conservation_gate_pass": residual_fraction <= residual_limit,
        "uniform_rain_volume_gate_pass": source_error_fraction <= 0.001,
        "no_sink_gate_pass": abs(final_sink) <= 1.0e-9,
    }


def _write_profile_csv(path: Path, rows: dict[int, dict[str, str]], fixed_delta: float) -> None:
    columns = ["profile_frame_index", "capture_frame_equivalent", "simulation_seconds", "finite_volume_status_flags", *PROFILE_FIELDS]
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        for frame, values in sorted(rows.items()):
            writer.writerow(
                {
                    "profile_frame_index": frame,
                    "capture_frame_equivalent": frame + 1,
                    "simulation_seconds": (frame + 1) * fixed_delta,
                    **values,
                }
            )


def _check_png(path: Path, expected_width: int, expected_height: int) -> None:
    require(path.is_file() and path.stat().st_size > 24, f"PNG capture is missing or empty: {path}")
    with path.open("rb") as stream:
        header = stream.read(24)
    require(header[:8] == b"\x89PNG\r\n\x1a\n", f"capture is not a PNG: {path}")
    width, height = struct.unpack(">II", header[16:24])
    require((width, height) == (expected_width, expected_height), f"capture dimensions differ for {path}: {width}x{height}")


def _run_view_capture(
    app: Path,
    case: dict[str, Any],
    manifest_path: Path,
    recipe: dict[str, Any],
    frame: int,
    view: str,
    capture_path: Path,
    log_path: Path,
    commands_path: Path,
    root: Path,
    profile_prefix: Path | None = None,
    profile_interval: int | None = None,
) -> dict[str, Any]:
    command = build_view_command(
        app,
        case,
        manifest_path,
        recipe,
        frame,
        view,
        capture_path,
        profile_prefix,
        profile_interval,
    )
    _run_capture(command, root, log_path, commands_path)
    _check_png(capture_path, recipe["protocol"]["capture_width"], recipe["protocol"]["capture_height"])
    identity = runtime_identity(log_path, case)
    return {
        "path": str(capture_path),
        "sha256": sha256_file(capture_path),
        "size_bytes": capture_path.stat().st_size,
        "runtime_identity": identity,
        "log_path": str(log_path),
        "log_sha256": sha256_file(log_path),
        "profile_prefix": str(profile_prefix) if profile_prefix is not None else None,
        "profile_interval": profile_interval,
    }


def _capture_case(
    app: Path,
    case: dict[str, Any],
    manifest_path: Path,
    recipe: dict[str, Any],
    case_root: Path,
    root: Path,
    commands_path: Path,
) -> dict[str, Any]:
    capture_root = case_root / "captures"
    profiles_root = case_root / "profiles"
    logs_root = case_root / "logs"
    captures: dict[str, dict[str, Any]] = {}
    profile_interval = int(recipe["protocol"]["profile_diagnostic_interval_frames"])
    exact_final_interval = int(recipe["protocol"]["profile_exact_final_sample_interval_frames"])
    for frame in recipe["protocol"]["composite_capture_frames"]:
        label = f"f{frame:04d}"
        capture_path = capture_root / "composite" / f"{label}-composite.png"
        series_prefix = profiles_root / "f0600-periodic" if frame == 600 else None
        captures[f"composite_f{frame:04d}"] = _run_view_capture(
            app,
            case,
            manifest_path,
            recipe,
            frame,
            "composite",
            capture_path,
            logs_root / f"composite-{label}.log",
            commands_path,
            root,
            series_prefix,
            profile_interval if series_prefix is not None else None,
        )

    for view in ("water-depth", "flow"):
        for frame in recipe["protocol"][f"{view.replace('-', '_')}_capture_frames"]:
            label = f"f{frame:04d}"
            capture_path = capture_root / view / f"{label}-{view}.png"
            final_prefix = profiles_root / "f0600-exact" if view == "water-depth" and frame == 600 else None
            captures[f"{view.replace('-', '_')}_f{frame:04d}"] = _run_view_capture(
                app,
                case,
                manifest_path,
                recipe,
                frame,
                view,
                capture_path,
                logs_root / f"{view}-{label}.log",
                commands_path,
                root,
                final_prefix,
                exact_final_interval if final_prefix is not None else None,
            )

    periodic_prefix = profiles_root / "f0600-periodic"
    exact_prefix = profiles_root / "f0600-exact"
    periodic_frames = set(range(0, int(recipe["protocol"]["total_frames"]), profile_interval))
    periodic_rows = _parse_profile_rows(periodic_prefix.with_suffix(".metrics.csv"), periodic_frames)
    require(set(periodic_rows) == periodic_frames, "15-frame profile series is incomplete")
    exact_rows = _parse_profile_rows(exact_prefix.with_suffix(".metrics.csv"), {0, 599})
    require(set(exact_rows) == {0, 599}, "exact f600 profile run did not report frame indexes 0 and 599")
    merged_rows = dict(periodic_rows)
    merged_rows[599] = exact_rows[599]
    combined_profile_path = profiles_root / "profile-samples.csv"
    _write_profile_csv(combined_profile_path, merged_rows, float(recipe["protocol"]["fixed_delta_seconds"]))
    acceptance = _profile_acceptance(merged_rows, recipe)
    require(acceptance["status_gate_pass"], f"finite-volume status flags are nonzero for {case['id']}")
    require(acceptance["conservation_gate_pass"], f"conservation residual gate failed for {case['id']}")
    require(acceptance["uniform_rain_volume_gate_pass"], f"uniform rain source volume differs from the frozen protocol for {case['id']}")
    require(acceptance["no_sink_gate_pass"], f"unexpected explicit sink volume for {case['id']}")

    return {
        "case_id": case["id"],
        "rank": case["rank"],
        "review_role": case["review_role"],
        "crop": case["crop"],
        "scan_metrics": case["scan_metrics"],
        "runtime_transformed_crop": case["transformed_crop_sha256"],
        "captures": captures,
        "profile": {
            "path": str(combined_profile_path),
            "sha256": sha256_file(combined_profile_path),
            "size_bytes": combined_profile_path.stat().st_size,
            "periodic_profile_source_path": str(periodic_prefix.with_suffix(".metrics.csv")),
            "periodic_profile_source_sha256": sha256_file(periodic_prefix.with_suffix(".metrics.csv")),
            "exact_final_profile_source_path": str(exact_prefix.with_suffix(".metrics.csv")),
            "exact_final_profile_source_sha256": sha256_file(exact_prefix.with_suffix(".metrics.csv")),
            "periodic_frame_indices": sorted(periodic_rows),
            "exact_final_frame_index": 599,
            "acceptance": acceptance,
        },
    }


def _path_identity(path: Path, root: Path) -> dict[str, Any]:
    return {
        "path": path.resolve().relative_to(root.resolve()).as_posix(),
        "sha256": sha256_file(path),
        "size_bytes": path.stat().st_size,
    }


def assert_output_available(output_root: Path) -> None:
    require(not output_root.exists(), f"refusing to overwrite existing neutral output: {output_root}")
    resolved = output_root.resolve()
    protected_roots = (
        ROOT / "outputs/fluid/terrain-site-water-v1-20260923",
        ROOT / "outputs/fluid/terrain-site-water-presentation-ab-v1-20260923",
        ROOT / "outputs/fluid/terrain-reach-neutral-v1-20260923",
        ROOT / "outputs/fluid/terrain-reach-scan-v1-20260923",
        ROOT / "outputs/fluid/terrain-reach-scan-v1-directness-20260923",
        ROOT / "outputs/fluid/terrain-reach-scan-v1-trace-20260923",
        ROOT / "outputs/fluid/terrain-reach-scan-v1-final-20260923",
    )
    require(
        all(not resolved.is_relative_to(protected.resolve()) for protected in protected_roots),
        "neutral output may not be nested inside historical site-water, presentation, or scanner evidence",
    )


def execute_study(app_path: Path, output_root: Path = OUTPUT_ROOT, root: Path = ROOT) -> dict[str, Any]:
    output_root = output_root if output_root.is_absolute() else root / output_root
    output_root = output_root.resolve()
    assert_output_available(output_root)
    checked = preflight(app_path, root)
    recipe = checked["recipe"]

    # Create the requested root atomically after all immutable-input preflight checks pass.
    output_root.parent.mkdir(parents=True, exist_ok=True)
    output_root.mkdir(parents=False, exist_ok=False)
    cases_root = output_root / "cases"
    cases_root.mkdir()
    commands_path = output_root / "commands.jsonl"
    commands_path.touch()
    (output_root / "recipe.json").write_bytes(RECIPE_PATH.read_bytes())

    started = time.perf_counter()
    case_results = []
    runtime_identities: dict[str, dict[str, Any]] = {}
    manifest_path = checked["manifest_path"]
    for case in recipe["cases"]:
        case_root = cases_root / case["id"]
        for directory in (case_root / "captures" / "composite", case_root / "captures" / "water-depth", case_root / "captures" / "flow", case_root / "profiles", case_root / "logs"):
            directory.mkdir(parents=True, exist_ok=True)
        result = _capture_case(app_path, case, manifest_path, recipe, case_root, root, commands_path)
        identities = [capture["runtime_identity"] for capture in result["captures"].values()]
        require(all(identity == identities[0] for identity in identities), f"runtime terrain identity changed between captures for {case['id']}")
        runtime_identities[case["id"]] = identities[0]
        case_results.append(result)

    # Both commands differ only in crop coordinates and evidence destinations.
    common_protocol_keys = (
        "--grid-width", "--grid-height", "--fluid25d-solver", "--fluid25d-fixed-delta-seconds",
        "--fluid25d-substeps", "--fluid25d-cell-size-m", "--fluid25d-scenario",
        "--fluid25d-terrain-water-protocol", "--fluid25d-rainfall-rate-mm-per-hour",
        "--fluid25d-source-active-duration-seconds",
    )
    physics_arguments: list[dict[str, str]] = []
    for case in recipe["cases"]:
        args = common_app_args(app_path, case, manifest_path, recipe, 600)
        physics_arguments.append({key: args[args.index(key) + 1] for key in common_protocol_keys if key in args})
    require(physics_arguments[0] == physics_arguments[1], "physics command contract differs between selected crops")
    render_arguments = dict(zip(render_args(recipe)[::2], render_args(recipe)[1::2]))

    elapsed = time.perf_counter() - started
    (output_root / "commands.jsonl").write_text(commands_path.read_text())
    provenance = {
        "schema": "cubey.fluid25d.terrain_reach_neutral_v1.provenance",
        "created_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "runner": _path_identity(Path(__file__), root),
        "recipe": _path_identity(RECIPE_PATH, root),
        "final_scan": _path_identity(SCAN_PATH, root),
        "source_manifest": _path_identity(manifest_path, root),
        "source_elevation": _path_identity(checked["elevation_path"], root),
        "app": checked["app"],
        "shader_assets": checked["shader_assets"],
        "source_hashes": checked["source_hashes"],
        "runtime_identities": runtime_identities,
        "render_policy": recipe["render_policy"],
        "physics_protocol": recipe["protocol"],
        "identical_protocol_across_crops": physics_arguments[0] == physics_arguments[1],
        "physics_cli_values_by_case": {case["id"]: physics_arguments[index] for index, case in enumerate(recipe["cases"])},
        "common_composite_render_cli_values": render_arguments,
        "elapsed_seconds": elapsed,
        "video_generated": False,
        "endpoint_source_sink_added": False,
        "fixture_or_default_promotion": False,
    }
    result_record = {
        "schema": "cubey.fluid25d.terrain_reach_neutral_v1.result",
        "status": "complete",
        "created_utc": provenance["created_utc"],
        "output_root": str(output_root),
        "scan_identity": recipe["scan_identity"],
        "source_identity": recipe["source_identity"],
        "solver_grid": recipe["solver_grid"],
        "protocol": recipe["protocol"],
        "render_policy": recipe["render_policy"],
        "composite_render_cli_values": render_arguments,
        "interpretation_boundary": recipe["selection_boundary"],
        "cases": case_results,
        "commands_path": "commands.jsonl",
        "provenance_path": "provenance.json",
        "video_generated": False,
    }
    (output_root / "provenance.json").write_text(json.dumps(provenance, indent=2, sort_keys=True) + "\n")
    (output_root / "result.json").write_text(json.dumps(result_record, indent=2, sort_keys=True) + "\n")
    return result_record


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=DEFAULT_APP)
    parser.add_argument("--output", type=Path, default=OUTPUT_ROOT)
    parser.add_argument("--preflight-only", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.preflight_only:
            result = preflight(args.app)
            print(json.dumps({
                "preflight": "pass",
                "recipe_sha256": sha256_file(RECIPE_PATH),
                "scan_sha256": SCAN_SHA256,
                "app": result["app"],
                "crop_statistics": result["crop_statistics"],
                "case_ids": [case["id"] for case in result["recipe"]["cases"]],
            }, indent=2, sort_keys=True))
            return 0
        result = execute_study(args.app, args.output)
        print(json.dumps({"status": result["status"], "output_root": result["output_root"], "cases": [case["case_id"] for case in result["cases"]]}, indent=2))
        return 0
    except (NeutralReachError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"terrain-reach neutral audition failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
