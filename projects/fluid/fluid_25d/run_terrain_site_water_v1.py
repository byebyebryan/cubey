#!/usr/bin/env python3
"""Run the frozen three-site Fluid 2.5D terrain-water study."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
RECIPE_PATH = Path(__file__).resolve().with_name("terrain_site_water_v1_20260923.json")
RECIPE_SHA256 = "6345ac48aa573a2b6333d9ae035f512e48845dc8e7281ed50740b653989051e6"
SCAN_SHA256 = "ca29947bf1ec1d73c0f1af93a81fb25e46cd2ece7d7731c5a9f87470c5837f50"
OUTPUT_ROOT = Path("outputs/fluid/terrain-site-water-v1-20260923")
DEFAULT_APP = Path("build/dev/projects/fluid/fluid_25d/fluid_25d")
PINNED_SOURCE = {
    "generator": "terrain-diffusion",
    "id": "terrain-diffusion-30m",
    "code_revision": "82a0431281f21a6ec3d691a12ee61525de5b0790",
    "model_id": "xandergos/terrain-diffusion-30m",
    "model_revision": "9ef8030cb805b433b98ec25c5dddefbac07a9e26",
    "native_resolution_m": 30.0,
}
EXPECTED_CASES = (
    {
        "rank": 1,
        "id": "rank-01-canyon-basin-contrast",
        "variant": "canyon-candidate-4",
        "seed": 12345,
        "review_role": "basin-like-morphology-contrast",
        "manifest_path": "cache/terrain/sources/v1/desert-canyon-study/canyon-candidate-4/heightfield.json",
        "manifest_sha256": "e16b145fabdcb9c0c9d6d275461405fe27ef3cfb4c5c81855f5ffe46fdc5fc93",
        "elevation_sha256": "88cc6c1fdacbd9759f90e6781ddf4ff570e4516fb0652438dc28f7ad23086d8b",
        "crop": {"x": 256, "z": 576, "width": 512, "height": 256, "sample_spacing_m": 30.0},
    },
    {
        "rank": 2,
        "id": "rank-02-temperate-mountain-valley",
        "variant": "temperate-mountain-valley",
        "seed": 0,
        "review_role": "visually-clearer-trunk-tributary-candidate",
        "manifest_path": "cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json",
        "manifest_sha256": "744356b27e2c2f7895c93bd88a8969fa329b212b9b39c0a9ced239ac405d3d6c",
        "elevation_sha256": "a978ecd435d2a161598d78ecf71221cba53b371e98c3525d18ebeab6665aa737",
        "crop": {"x": 512, "z": 832, "width": 512, "height": 256, "sample_spacing_m": 30.0},
    },
    {
        "rank": 3,
        "id": "rank-03-canyon-trunk-tributary-candidate",
        "variant": "canyon-candidate-2",
        "seed": 0,
        "review_role": "visually-clearer-trunk-tributary-candidate",
        "manifest_path": "cache/terrain/sources/v1/desert-canyon-study/canyon-candidate-2/heightfield.json",
        "manifest_sha256": "b44351a32a33d27f73d78978f3a7e0bfacabfcb817866e88983a75c734184f8d",
        "elevation_sha256": "2a919b516d8ae4fb8c193cdd8db1a8ba055ba702e5cbd1ff50ad2b7fc6ab3c48",
        "crop": {"x": 640, "z": 896, "width": 512, "height": 256, "sample_spacing_m": 30.0},
    },
)
EXPECTED_TIERS = (
    {"id": "neutral", "rainfall_mm_per_hour": 12, "evidence_role": "neutral-discovery-baseline"},
    {
        "id": "visibility-stress",
        "rainfall_mm_per_hour": 60,
        "evidence_role": "visibility-stress-not-climate-or-hydrology-evidence",
    },
)
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


class StudyError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise StudyError(message)


def capture_frames(recipe: dict[str, Any]) -> list[int]:
    protocol = recipe["protocol"]
    first = protocol["sequence_first_frame"]
    interval = protocol["sequence_interval_frames"]
    total = protocol["total_frames"]
    return [first, *range(interval, total + 1, interval)]


def validate_recipe_structure(recipe: dict[str, Any]) -> None:
    require(recipe.get("schema") == "cubey.fluid25d.terrain_site_water_v1.recipe", "unsupported recipe schema")
    scan_identity = recipe.get("scan_identity")
    require(
        scan_identity
        == {
            "id": "terrain-site-scan-v1-20260923",
            "path": "outputs/fluid/terrain-site-scan-v1-20260923/scan.json",
            "sha256": SCAN_SHA256,
            "schema": "cubey.fluid25d.terrain_site_scan.v1",
        },
        "recipe scan identity/checksum does not match the frozen shortlist record",
    )
    require(recipe.get("source_identity") == PINNED_SOURCE, "recipe producer identity is not the pinned Terrain Diffusion source")
    require(
        recipe.get("terrain_crop")
        == {
            "width": 512,
            "height": 256,
            "sample_spacing_m": 30.0,
            "extent_m": {"x": 15360.0, "z": 7680.0},
        },
        "recipe must use the frozen 512x256 native 30 m crop contract",
    )
    require(
        recipe.get("solver_grid") == {"width": 512, "height": 256, "cell_size_m": 30},
        "solver grid must match the full frozen native crop, not a smaller subwindow",
    )

    protocol = recipe.get("protocol")
    require(isinstance(protocol, dict), "recipe protocol is missing")
    expected_protocol = {
        "solver": "finite-volume",
        "terrain_water_protocol": "rain-pulse",
        "fixed_delta_seconds": 2,
        "substeps": 8,
        "total_frames": 600,
        "rain_duration_seconds": 600,
        "sequence_first_frame": 1,
        "sequence_interval_frames": 15,
        "selected_frames": [1, 60, 150, 300, 450, 600],
        "video_fps": 4,
        "capture_width": 1280,
        "capture_height": 720,
        "profile_diagnostic_interval": 15,
        "slow_pooled_speed_threshold_m_per_s": 0.02,
        "max_conservation_residual_fraction_of_source": 0.0001,
        "tiers": list(EXPECTED_TIERS),
    }
    require(protocol == expected_protocol, "recipe protocol, tier matrix, or acceptance threshold was altered")
    require(capture_frames(recipe) == [1, *range(15, 601, 15)], "capture schedule must be f1 plus every 15 frames through f600")
    require(len(capture_frames(recipe)) == 41, "capture schedule must contain exactly 41 images per view")

    cases = recipe.get("cases")
    require(cases == list(EXPECTED_CASES), "recipe cases do not match frozen corrected scan ranks 1, 2, and 3")
    require(
        recipe.get("selection_boundary")
        == "The scan and role labels are review evidence only. Terrain samples remain immutable, and neither valley masks nor scan topology enter the solver. No fixture/default promotion is implied.",
        "recipe selection boundary is missing or changed",
    )


def resolve_repo_path(root: Path, relative_path: str) -> Path:
    candidate = Path(relative_path)
    require(not candidate.is_absolute() and ".." not in candidate.parts, f"recipe path must be repo-relative: {relative_path}")
    resolved = (root / candidate).resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError as error:
        raise StudyError(f"recipe path resolves outside the repository: {relative_path}") from error
    return resolved


def load_frozen_recipe(recipe_path: Path, root: Path) -> tuple[dict[str, Any], list[tuple[dict[str, Any], Path]]]:
    recipe_path = recipe_path.resolve()
    require(recipe_path == RECIPE_PATH.resolve(), f"only the checked-in frozen recipe is accepted: {RECIPE_PATH}")
    require(recipe_path.is_file(), f"frozen recipe is missing: {recipe_path}")
    recipe_hash = sha256_file(recipe_path)
    require(recipe_hash == RECIPE_SHA256, f"frozen recipe SHA-256 mismatch: {recipe_hash}")
    recipe = json.loads(recipe_path.read_text())
    validate_recipe_structure(recipe)

    validated: list[tuple[dict[str, Any], Path]] = []
    for case in recipe["cases"]:
        manifest_path = resolve_repo_path(root, case["manifest_path"])
        require(manifest_path.is_file(), f"heightfield manifest missing for {case['id']}: {manifest_path}")
        require(sha256_file(manifest_path) == case["manifest_sha256"], f"heightfield manifest SHA-256 mismatch for {case['id']}")
        manifest = json.loads(manifest_path.read_text())
        require(manifest.get("schema") == "cubey.terrain.heightfield.v1", f"unsupported manifest schema for {case['id']}")
        source = manifest.get("source")
        require(
            isinstance(source, dict) and all(source.get(key) == value for key, value in PINNED_SOURCE.items()),
            f"producer pins mismatch for {case['id']}",
        )

        grid = manifest.get("grid", {})
        require(
            grid.get("width") == 2048
            and grid.get("height") == 2048
            and grid.get("sample_spacing_m") == 30.0
            and grid.get("axis_mapping") == {"world_x": "model_j", "world_z": "model_i"},
            f"native 2048x2048 30 m grid mismatch for {case['id']}",
        )
        require(manifest.get("seed") == case["seed"], f"seed mismatch for {case['id']}")
        require(manifest.get("provenance", {}).get("landscape_variant") == case["variant"], f"variant mismatch for {case['id']}")
        elevation = manifest.get("files", {}).get("elevation", {})
        require(
            elevation.get("path") == "elevation.f32"
            and elevation.get("dtype") == "float32-le"
            and elevation.get("layout") == "row-major-zx"
            and elevation.get("shape") == [2048, 2048]
            and elevation.get("unit") == "m"
            and elevation.get("byte_count") == 2048 * 2048 * 4
            and elevation.get("sha256") == case["elevation_sha256"],
            f"elevation payload metadata mismatch for {case['id']}",
        )
        elevation_path = manifest_path.parent / "elevation.f32"
        require(elevation_path.is_file(), f"elevation payload missing for {case['id']}: {elevation_path}")
        require(elevation_path.stat().st_size == 2048 * 2048 * 4, f"elevation payload byte count mismatch for {case['id']}")
        require(sha256_file(elevation_path) == case["elevation_sha256"], f"elevation payload SHA-256 mismatch for {case['id']}")
        crop = case["crop"]
        require(
            crop["x"] >= 0
            and crop["z"] >= 0
            and crop["x"] + crop["width"] <= grid["width"]
            and crop["z"] + crop["height"] <= grid["height"],
            f"crop bounds exceed the immutable source for {case['id']}",
        )
        validated.append((case, manifest_path))
    return recipe, validated


def common_app_args(
    app: Path,
    case: dict[str, Any],
    manifest_path: Path,
    tier: dict[str, Any],
    frames: int,
    width: int,
    height: int,
) -> list[str]:
    crop = case["crop"]
    grid = {"width": 512, "height": 256}
    return [
        str(app),
        "--headless",
        "--capture",
        "png",
        "--frames",
        str(frames),
        "--width",
        str(width),
        "--height",
        str(height),
        "--fluid25d-solver",
        "finite-volume",
        "--fluid25d-fixed-delta-seconds",
        "2",
        "--fluid25d-substeps",
        "8",
        "--fluid25d-scenario",
        "terrain-case",
        "--terrain-heightfield",
        str(manifest_path.parent),
        "--grid-width",
        str(grid["width"]),
        "--grid-height",
        str(grid["height"]),
        "--fluid25d-terrain-crop-x",
        str(crop["x"]),
        "--fluid25d-terrain-crop-z",
        str(crop["z"]),
        "--fluid25d-terrain-water-protocol",
        "rain-pulse",
        "--fluid25d-rainfall-rate-mm-per-hour",
        str(tier["rainfall_mm_per_hour"]),
        "--fluid25d-source-active-duration-seconds",
        "600",
    ]


def view_args(view: str) -> list[str]:
    if view == "composite":
        return ["--fluid25d-view", "catchment", "--fluid25d-catchment-view", "composite"]
    if view == "water-depth":
        return ["--fluid25d-view", "diagnostics", "--debug-view", "depth"]
    if view == "wet-dry":
        return ["--fluid25d-view", "diagnostics", "--debug-view", "wet-dry"]
    if view == "flow":
        return ["--fluid25d-view", "diagnostics", "--debug-view", "flow"]
    raise StudyError(f"unsupported capture view: {view}")


def run_logged(
    command: list[str],
    root: Path,
    log_path: Path,
    commands_stream: Any,
) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    commands_stream.write(
        json.dumps({"argv": command, "cwd": str(root), "log": str(log_path)}) + "\n"
    )
    commands_stream.flush()
    with log_path.open("wb") as log_stream:
        completed = subprocess.run(command, cwd=root, stdout=log_stream, stderr=subprocess.STDOUT, check=False)
    if completed.returncode != 0:
        raise StudyError(f"command failed with exit {completed.returncode}; see {log_path}: {shlex.join(command)}")


def runtime_identity(log_path: Path, case: dict[str, Any]) -> dict[str, Any]:
    text = log_path.read_text(errors="replace")
    match = RUNTIME_IDENTITY.search(text)
    require(match is not None, f"terrain-backed runtime identity missing from {log_path}")
    identity = match.groupdict()
    crop = case["crop"]
    require(identity["elevation"] == case["elevation_sha256"], f"runtime elevation identity mismatch for {case['id']}")
    require(
        (int(identity["x"]), int(identity["z"]), int(identity["width"]), int(identity["height"]))
        == (crop["x"], crop["z"], crop["width"], crop["height"]),
        f"runtime crop identity mismatch for {case['id']}",
    )
    require(float(identity["spacing"]) == crop["sample_spacing_m"], f"runtime sample spacing mismatch for {case['id']}")
    return {
        "elevation_sha256": identity["elevation"],
        "crop_sha256": identity["crop_sha"],
        "crop": {key: crop[key] for key in ("x", "z", "width", "height", "sample_spacing_m")},
    }


def parse_profile(metrics_path: Path, total_frames: int, interval: int) -> dict[int, dict[str, float]]:
    require(metrics_path.is_file() and metrics_path.stat().st_size > 0, f"profile metrics missing: {metrics_path}")
    grouped: dict[int, dict[str, float]] = {}
    with metrics_path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        require(reader.fieldnames == ["frame_index", "category", "name", "value"], f"unexpected profile CSV header: {metrics_path}")
        for row in reader:
            frame = int(row["frame_index"])
            category = row["category"]
            name = row["name"]
            if category == "fluid_25d.solver" and name == "finite_volume_status_flags":
                key = name
            elif category == "fluid_25d.water" and name in PROFILE_FIELDS:
                key = name
            else:
                continue
            grouped.setdefault(frame, {})[key] = float(row["value"])
    expected_frames = list(range(0, total_frames, interval))
    require(sorted(grouped) == expected_frames, f"profile diagnostic frame coverage mismatch: {metrics_path}")
    required_fields = {"finite_volume_status_flags", *PROFILE_FIELDS}
    for frame, values in grouped.items():
        missing = required_fields.difference(values)
        require(not missing, f"profile frame {frame} missing metrics {sorted(missing)}: {metrics_path}")
    return grouped


def write_comparison(
    path: Path,
    profile_sets: list[dict[str, Any]],
    recipe: dict[str, Any],
) -> None:
    columns = [
        "tier",
        "tier_role",
        "rainfall_mm_per_hour",
        "rank",
        "case_id",
        "variant",
        "review_role",
        "profile_frame_index",
        "capture_frame_equivalent",
        "simulation_seconds",
        "finite_volume_status_flags",
        *PROFILE_FIELDS,
    ]
    tiers = {tier["id"]: tier for tier in recipe["protocol"]["tiers"]}
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        for item in profile_sets:
            tier = tiers[item["tier"]]
            for frame, values in sorted(item["frames"].items()):
                capture_frame = frame + 1
                row = {
                    "tier": tier["id"],
                    "tier_role": tier["evidence_role"],
                    "rainfall_mm_per_hour": tier["rainfall_mm_per_hour"],
                    "rank": item["case"]["rank"],
                    "case_id": item["case"]["id"],
                    "variant": item["case"]["variant"],
                    "review_role": item["case"]["review_role"],
                    "profile_frame_index": frame,
                    "capture_frame_equivalent": capture_frame,
                    "simulation_seconds": capture_frame * recipe["protocol"]["fixed_delta_seconds"],
                    **values,
                }
                writer.writerow(row)


def profile_acceptance(frames: dict[int, dict[str, float]], threshold: float) -> dict[str, Any]:
    max_status = max(int(round(values["finite_volume_status_flags"])) for values in frames.values())
    max_residual = max(abs(values["conservation_residual_m3"]) for values in frames.values())
    final_frame = max(frames)
    final_source = frames[final_frame]["cumulative_source_volume_m3"]
    require(final_source > 0.0, "profile source volume must be positive at the final sampled frame")
    fraction = max_residual / final_source
    return {
        "status_flags_max": max_status,
        "profile_frame_count": len(frames),
        "last_profile_frame_index": final_frame,
        "last_profile_capture_frame_equivalent": final_frame + 1,
        "max_abs_conservation_residual_m3": max_residual,
        "last_profile_source_volume_m3": final_source,
        "max_residual_fraction_of_last_profile_source": fraction,
        "residual_fraction_limit": threshold,
        "status_gate_pass": max_status == 0,
        "conservation_gate_pass": fraction <= threshold,
    }


def _tool_identity(path: Path) -> dict[str, Any]:
    return {"path": str(path), "sha256": sha256_file(path), "size_bytes": path.stat().st_size}


def _run_tool(
    command: list[str],
    root: Path,
    log_path: Path,
    commands_stream: Any,
) -> subprocess.CompletedProcess[str]:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    commands_stream.write(json.dumps({"argv": command, "cwd": str(root), "log": str(log_path)}) + "\n")
    commands_stream.flush()
    completed = subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    log_path.write_text(completed.stdout)
    if completed.returncode != 0:
        raise StudyError(f"tool command failed with exit {completed.returncode}; see {log_path}: {shlex.join(command)}")
    return completed


def encode_video(case_root: Path, output_root: Path, root: Path, fps: int, frame_count: int, commands: Any) -> dict[str, Any]:
    video_path = case_root / "videos" / "composite-water-depth.mp4"
    video_path.parent.mkdir(parents=True, exist_ok=True)
    video_log = case_root / "logs" / "video.log"
    ffmpeg = [
        "/usr/bin/ffmpeg",
        "-y",
        "-hide_banner",
        "-loglevel",
        "error",
        "-framerate",
        str(fps),
        "-start_number",
        "1",
        "-i",
        str(case_root / "captures" / "composite" / "frame-%04d.png"),
        "-framerate",
        str(fps),
        "-start_number",
        "1",
        "-i",
        str(case_root / "captures" / "water-depth" / "frame-%04d.png"),
        "-filter_complex",
        "[0:v]scale=640:360:force_original_aspect_ratio=decrease,pad=640:360:(ow-iw)/2:(oh-ih)/2[left];"
        "[1:v]scale=640:360:force_original_aspect_ratio=decrease,pad=640:360:(ow-iw)/2:(oh-ih)/2[right];"
        "[left][right]hstack=inputs=2,format=yuv420p[v]",
        "-map",
        "[v]",
        "-an",
        "-c:v",
        "libx264",
        "-preset",
        "medium",
        "-crf",
        "18",
        "-movflags",
        "+faststart",
        str(video_path),
    ]
    _run_tool(ffmpeg, root, video_log, commands)
    ffprobe = [
        "/usr/bin/ffprobe",
        "-v",
        "error",
        "-count_frames",
        "-show_entries",
        "format=duration:stream=codec_name,width,height,pix_fmt,nb_read_frames",
        "-of",
        "json",
        str(video_path),
    ]
    result = _run_tool(ffprobe, root, case_root / "videos" / "ffprobe.log", commands)
    probe = json.loads(result.stdout)
    stream = probe.get("streams", [{}])[0]
    duration = float(probe.get("format", {}).get("duration", 0.0))
    expected_duration = frame_count / fps
    accepted = (
        stream.get("codec_name") == "h264"
        and int(stream.get("width", 0)) == 1280
        and int(stream.get("height", 0)) == 360
        and stream.get("pix_fmt") == "yuv420p"
        and int(stream.get("nb_read_frames", 0)) == frame_count
        and abs(duration - expected_duration) <= 1e-6
        and video_path.is_file()
        and video_path.stat().st_size > 0
    )
    require(accepted, f"H.264 split-video acceptance failed for {case_root.name}: {probe}")
    compact_probe = {
        "codec": stream.get("codec_name"),
        "width": stream.get("width"),
        "height": stream.get("height"),
        "pix_fmt": stream.get("pix_fmt"),
        "frame_count": int(stream.get("nb_read_frames", 0)),
        "duration_seconds": duration,
        "expected_duration_seconds": expected_duration,
        "complete": True,
        "path": str(video_path.relative_to(output_root)),
    }
    return compact_probe


def make_overview(case_root: Path, root: Path, commands: Any) -> Path:
    path = case_root / "review" / "overview.png"
    path.parent.mkdir(parents=True, exist_ok=True)
    sequence_indices = [1, 21, 41]
    images = [
        case_root / "captures" / "composite" / f"frame-{sequence_index:04d}.png"
        for sequence_index in sequence_indices
    ] + [
        case_root / "captures" / "water-depth" / f"frame-{sequence_index:04d}.png"
        for sequence_index in sequence_indices
    ]
    require(all(image.is_file() for image in images), f"overview source images are incomplete for {case_root.name}")
    command = [
        "/usr/bin/magick",
        "montage",
        *[str(image) for image in images],
        "-thumbnail",
        "480x270",
        "-tile",
        "3x2",
        "-geometry",
        "+4+4",
        "-background",
        "#101820",
        str(path),
    ]
    _run_tool(command, root, case_root / "logs" / "overview.log", commands)
    return path


def check_capture_set(
    case_root: Path,
    frames: list[int],
    selected_frames: list[int],
    full: bool,
) -> dict[str, Any]:
    sequence_rows = 0
    if full:
        sequence_path = case_root / "sequence.csv"
        require(sequence_path.is_file(), f"sequence inventory missing: {sequence_path}")
        with sequence_path.open(newline="") as stream:
            rows = list(csv.DictReader(stream))
        require([int(row["capture_frame"]) for row in rows] == frames, f"sequence frame set mismatch: {sequence_path}")
        for row in rows:
            for key in ("composite_path", "water_depth_path"):
                image_path = case_root.parent.parent / row[key]
                require(image_path.is_file() and image_path.stat().st_size > 0, f"sequence capture missing: {image_path}")
        sequence_rows = len(rows)
    selected_dir = case_root / "captures" / "selected"
    selected = sorted(selected_dir.glob("*.png"))
    expected_selected = {"f0600-composite-profile.png"}
    if full:
        expected_selected.update(
            f"f{frame:04d}-{view}.png"
            for frame in selected_frames
            for view in ("wet-dry", "flow")
        )
    else:
        expected_selected.update(f"f0600-{view}.png" for view in ("water-depth", "wet-dry", "flow"))
    actual_selected = {path.name for path in selected}
    require(
        actual_selected == expected_selected,
        f"selected capture set mismatch under {case_root}: expected={sorted(expected_selected)} actual={sorted(actual_selected)}",
    )
    require(all(path.stat().st_size > 0 for path in selected), f"selected capture is empty under {case_root}")
    return {"sequence_frames": sequence_rows, "selected_stills": len(selected), "complete": True}


def execute_study(
    recipe: dict[str, Any],
    validated_cases: list[tuple[dict[str, Any], Path]],
    output_root: Path,
    app: Path,
    pilot: bool,
) -> dict[str, Any]:
    protocol = recipe["protocol"]
    frames = capture_frames(recipe)
    tiers = [tier for tier in recipe["protocol"]["tiers"] if not pilot or tier["id"] == "neutral"]
    cases = [case for case, _manifest in validated_cases]
    manifests = {case["id"]: manifest for case, manifest in validated_cases}
    started = time.perf_counter()
    profile_sets: list[dict[str, Any]] = []
    result_rows: list[dict[str, Any]] = []
    runtime_by_case: dict[str, dict[str, Any]] = {}
    output_root.mkdir(parents=True, exist_ok=False)
    (output_root / "profiles").mkdir()
    (output_root / "logs").mkdir()
    (output_root / "recipe.json").write_bytes(RECIPE_PATH.read_bytes())
    with (output_root / "commands.jsonl").open("w") as commands:
        for tier in tiers:
            tier_root = output_root / tier["id"]
            tier_root.mkdir()
            for case in cases:
                manifest_path = manifests[case["id"]]
                case_root = tier_root / case["id"]
                selected_root = case_root / "captures" / "selected"
                selected_root.mkdir(parents=True)
                (case_root / "logs").mkdir()

                profile_prefix = output_root / "profiles" / f"{tier['id']}-{case['id']}"
                profile_output = selected_root / "f0600-composite-profile.png"
                profile_log = output_root / "logs" / f"{tier['id']}-{case['id']}-profile.log"
                profile_command = common_app_args(
                    app,
                    case,
                    manifest_path,
                    tier,
                    protocol["total_frames"],
                    protocol["capture_width"],
                    protocol["capture_height"],
                )
                profile_command += view_args("composite")
                profile_command += [
                    "--profile-output",
                    str(profile_prefix),
                    "--profile-diagnostics",
                    "--profile-diagnostic-interval",
                    str(protocol["profile_diagnostic_interval"]),
                    "--output",
                    str(profile_output),
                ]
                run_logged(profile_command, ROOT, profile_log, commands)
                identity = runtime_identity(profile_log, case)
                previous_identity = runtime_by_case.setdefault(case["id"], identity)
                require(previous_identity == identity, f"terrain runtime identity changed across tiers for {case['id']}")
                frames_metrics = parse_profile(
                    profile_prefix.with_name(profile_prefix.name + ".metrics.csv"),
                    protocol["total_frames"],
                    protocol["profile_diagnostic_interval"],
                )
                profile_sets.append({"tier": tier["id"], "case": case, "frames": frames_metrics})
                gates = profile_acceptance(
                    frames_metrics,
                    protocol["max_conservation_residual_fraction_of_source"],
                )

                if pilot:
                    for view in ("water-depth", "wet-dry", "flow"):
                        image_path = selected_root / f"f0600-{view}.png"
                        command = common_app_args(
                            app,
                            case,
                            manifest_path,
                            tier,
                            protocol["total_frames"],
                            protocol["capture_width"],
                            protocol["capture_height"],
                        )
                        command += view_args(view) + ["--output", str(image_path)]
                        log_path = case_root / "logs" / f"f0600-{view}.log"
                        run_logged(command, ROOT, log_path, commands)
                else:
                    (case_root / "captures" / "composite").mkdir()
                    (case_root / "captures" / "water-depth").mkdir()
                    sequence_path = case_root / "sequence.csv"
                    with sequence_path.open("w", newline="") as stream:
                        writer = csv.DictWriter(
                            stream,
                            fieldnames=("sequence_index", "capture_frame", "simulation_seconds", "composite_path", "water_depth_path"),
                        )
                        writer.writeheader()
                        for sequence_index, capture_frame in enumerate(frames, start=1):
                            outputs: dict[str, Path] = {}
                            for view, directory in (("composite", "composite"), ("water-depth", "water-depth")):
                                image_path = case_root / "captures" / directory / f"frame-{sequence_index:04d}.png"
                                command = common_app_args(
                                    app,
                                    case,
                                    manifest_path,
                                    tier,
                                    capture_frame,
                                    protocol["capture_width"],
                                    protocol["capture_height"],
                                )
                                command += view_args(view) + ["--output", str(image_path)]
                                log_path = case_root / "logs" / f"{view}-f{capture_frame:04d}.log"
                                run_logged(command, ROOT, log_path, commands)
                                if sequence_index == 1 and view == "composite":
                                    capture_identity = runtime_identity(log_path, case)
                                    require(capture_identity == identity, f"profile/capture identity mismatch for {case['id']}")
                                outputs[view] = image_path
                            writer.writerow(
                                {
                                    "sequence_index": sequence_index,
                                    "capture_frame": capture_frame,
                                    "simulation_seconds": capture_frame * protocol["fixed_delta_seconds"],
                                    "composite_path": outputs["composite"].relative_to(output_root).as_posix(),
                                    "water_depth_path": outputs["water-depth"].relative_to(output_root).as_posix(),
                                }
                            )
                    for capture_frame in protocol["selected_frames"]:
                        for view in ("wet-dry", "flow"):
                            image_path = selected_root / f"f{capture_frame:04d}-{view}.png"
                            command = common_app_args(
                                app,
                                case,
                                manifest_path,
                                tier,
                                capture_frame,
                                protocol["capture_width"],
                                protocol["capture_height"],
                            )
                            command += view_args(view) + ["--output", str(image_path)]
                            log_path = case_root / "logs" / f"selected-f{capture_frame:04d}-{view}.log"
                            run_logged(command, ROOT, log_path, commands)

                capture_gate = check_capture_set(
                    case_root,
                    frames,
                    protocol["selected_frames"],
                    full=not pilot,
                )
                video = None
                if not pilot:
                    video = encode_video(case_root, output_root, ROOT, protocol["video_fps"], len(frames), commands)
                    overview = make_overview(case_root, ROOT, commands)
                    require(overview.is_file() and overview.stat().st_size > 0, f"overview image missing for {case['id']}")
                result_rows.append(
                    {
                        "tier": tier["id"],
                        "case_id": case["id"],
                        "rank": case["rank"],
                        "review_role": case["review_role"],
                        "profile": gates,
                        "captures": capture_gate,
                        "video": video,
                        "runtime_identity": identity,
                        "status_gate_pass": gates["status_gate_pass"],
                        "conservation_gate_pass": gates["conservation_gate_pass"],
                    }
                )

    comparison = output_root / "comparison.csv"
    write_comparison(comparison, profile_sets, recipe)
    required_count = len(cases) * len(tiers)
    all_profiles_pass = len(result_rows) == required_count and all(row["status_gate_pass"] for row in result_rows)
    all_residuals_pass = all(row["conservation_gate_pass"] for row in result_rows)
    all_captures_pass = len(result_rows) == required_count and all(row["captures"]["complete"] for row in result_rows)
    all_videos_pass = pilot or all(row["video"] and row["video"]["complete"] for row in result_rows)
    acceptance = {
        "schema": "cubey.fluid25d.terrain_site_water_v1.acceptance",
        "stage": "neutral-three-site-pilot" if pilot else "full-two-tier-matrix",
        "required_cases": [case["id"] for case in cases],
        "required_tiers": [tier["id"] for tier in tiers],
        "capture_sequence_frames": frames if not pilot else [],
        "required_sequence_frames_per_view": len(frames) if not pilot else 0,
        "max_conservation_residual_fraction_of_source": protocol["max_conservation_residual_fraction_of_source"],
        "status_zero_at_sampled_profile_frames": all_profiles_pass,
        "conservation_residual_gate": all_residuals_pass,
        "complete_capture_gate": all_captures_pass,
        "complete_h264_video_gate": all_videos_pass,
        "capture_process_exit_codes_zero_through_f600": True,
        "results": result_rows,
        "acceptance": "PASS" if all((all_profiles_pass, all_residuals_pass, all_captures_pass, all_videos_pass)) else "FAIL",
    }
    acceptance_path = output_root / "acceptance.json"
    acceptance_path.write_text(json.dumps(acceptance, indent=2, sort_keys=True) + "\n")
    write_report(output_root / "report.md", recipe, acceptance)
    return {
        "acceptance": acceptance,
        "comparison_path": comparison,
        "elapsed_seconds": round(time.perf_counter() - started, 3),
        "runtime_by_case": runtime_by_case,
        "profiles": profile_sets,
    }


def write_report(path: Path, recipe: dict[str, Any], acceptance: dict[str, Any]) -> None:
    lines = [
        "# Fluid 2.5D Terrain-Site Water Study V1",
        "",
        f"Stage: `{acceptance['stage']}`; acceptance: **{acceptance['acceptance']}**.",
        "",
        "Immutable terrain and the existing terrain-case finite-volume solver were used unchanged. "
        "The neutral tier is 12 mm/h; 60 mm/h is a visibility-stress probe, not climate or hydrology evidence. "
        "No scan masks or topology enter the solver and no fixture/default is promoted.",
        "",
        "Rain is active for frames 1–300 (600 simulated seconds), then the source is off through f600 "
        "(1,200 simulated seconds total). Each crop is the full 512×256 native 30 m window on a matching 512×256 solver grid.",
        "",
        "| Tier | Rank | Case / human-review role | Status max | Max residual (m³) | Fraction of final sampled source | Sequence | Video |",
        "|---|---:|---|---:|---:|---:|---:|---|",
    ]
    for row in acceptance["results"]:
        profile = row["profile"]
        lines.append(
            f"| {row['tier']} | {row['rank']} | {row['case_id']} ({row['review_role']}) | "
            f"{profile['status_flags_max']} | "
            f"{profile['max_abs_conservation_residual_m3']:.6f} | "
            f"{profile['max_residual_fraction_of_last_profile_source']:.8g} | "
            f"{row['captures']['sequence_frames']} | "
            f"{row['video']['complete'] if row['video'] else 'pilot'} |"
        )
    lines.extend(
        [
            "",
            "Profiles are emitted every 15 zero-based frame indices; the last profile sample is index 585, equivalent to capture f586. "
            "The separate f600 captures complete the requested horizon, and every app process must exit 0. "
            "Status and residual gates are evaluated on every profile row.",
            "",
            "The crop/site labels express shortlist review intent only. Terrain morphology is not a mapped river network or hydrologic truth; "
            "results remain human-review evidence for the shared neutral protocol.",
        ]
    )
    path.write_text("\n".join(lines) + "\n")


def collect_artifact_hashes(output_root: Path) -> list[dict[str, Any]]:
    inventory = []
    for path in sorted(output_root.rglob("*")):
        if not path.is_file() or path.name == "artifact_hashes.json":
            continue
        inventory.append({"path": path.relative_to(output_root).as_posix(), "sha256": sha256_file(path), "size_bytes": path.stat().st_size})
    return inventory


def preflight_tools(app: Path) -> dict[str, Any]:
    require(app.is_file() and os.access(app, os.X_OK), f"Fluid app is missing or not executable: {app}")
    tools = {}
    for name in ("ffmpeg", "ffprobe", "magick"):
        path = Path("/usr/bin") / name
        require(path.is_file() and os.access(path, os.X_OK), f"required tool is missing: {path}")
        tools[name] = _tool_identity(path)
    return {"app": _tool_identity(app), "tools": tools}


def write_provenance(output_root: Path, app_info: dict[str, Any], elapsed: float, pilot: bool, recipe: dict[str, Any]) -> None:
    git_head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, capture_output=True, check=False)
    git_status = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, text=True, capture_output=True, check=False)
    runner_identity = _tool_identity(Path(__file__).resolve())
    runner_identity["path"] = Path(__file__).resolve().relative_to(ROOT).as_posix()
    gpu = subprocess.run(
        ["/usr/bin/nvidia-smi", "--query-gpu=name,driver_version", "--format=csv,noheader"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    data = {
        "schema": "cubey.fluid25d.terrain_site_water_v1.provenance",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "stage": "neutral-three-site-pilot" if pilot else "full-two-tier-matrix",
        "recipe_path": RECIPE_PATH.relative_to(ROOT).as_posix(),
        "recipe_sha256": RECIPE_SHA256,
        "runner": runner_identity,
        "historical_scan_identity": recipe["scan_identity"],
        "runtime_scan_dependency": False,
        "source_identity": recipe["source_identity"],
        "cases": [
            {
                "rank": case["rank"],
                "id": case["id"],
                "role": case["review_role"],
                "manifest_path": case["manifest_path"],
                "manifest_sha256": case["manifest_sha256"],
                "elevation_sha256": case["elevation_sha256"],
                "crop": case["crop"],
            }
            for case in recipe["cases"]
        ],
        "app": app_info["app"],
        "tools": app_info["tools"],
        "git_head": git_head.stdout.strip() if git_head.returncode == 0 else "unavailable",
        "git_changed_path_count": len(git_status.stdout.splitlines()) if git_status.returncode == 0 else None,
        "host": os.uname().nodename,
        "gpu": gpu.stdout.strip() if gpu.returncode == 0 else "unavailable",
        "elapsed_seconds": elapsed,
        "protocol": recipe["protocol"],
        "solver_input_generated": False,
        "terrain_mutated": False,
        "fixture_or_default_promotion": "none",
    }
    (output_root / "provenance.json").write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=Path(os.environ.get("APP", ROOT / DEFAULT_APP)))
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--pilot", action="store_true", help="run neutral-only f600 profiles and review stills for all three sites")
    parser.add_argument("--preflight", action="store_true", help="validate the recipe, assets, app, and tools without creating output or running the solver")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        recipe, validated_cases = load_frozen_recipe(RECIPE_PATH, ROOT)
        app = args.app.resolve()
        app_info = preflight_tools(app)
        if args.preflight:
            print(
                json.dumps(
                    {
                        "recipe_sha256": RECIPE_SHA256,
                        "validated_cases": [case["id"] for case, _manifest in validated_cases],
                        "tiers": [tier["id"] for tier in recipe["protocol"]["tiers"]],
                        "scan_file_required_at_runtime": False,
                        "app": app_info["app"],
                        "tools": sorted(app_info["tools"]),
                    },
                    indent=2,
                )
            )
            return 0

        default_output = ROOT / OUTPUT_ROOT / ("pilot-neutral-f600" if args.pilot else "full-matrix")
        output_root = (args.output_dir or default_output).resolve()
        require(not output_root.exists(), f"refusing to overwrite existing study output: {output_root}")
        start = time.perf_counter()
        result = execute_study(recipe, validated_cases, output_root, app, args.pilot)
        elapsed = round(time.perf_counter() - start, 3)
        write_provenance(output_root, app_info, elapsed, args.pilot, recipe)
        artifact_hashes = collect_artifact_hashes(output_root)
        (output_root / "artifact_hashes.json").write_text(json.dumps(artifact_hashes, indent=2, sort_keys=True) + "\n")
        if result["acceptance"]["acceptance"] != "PASS":
            raise StudyError(f"study acceptance failed; inspect {output_root / 'acceptance.json'}")
        print(f"terrain site-water {'pilot' if args.pilot else 'full matrix'} wrote {output_root} ({elapsed:.3f}s)")
        return 0
    except (StudyError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"terrain site-water runner: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
