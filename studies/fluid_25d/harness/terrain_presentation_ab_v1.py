#!/usr/bin/env python3
"""Render a frozen rank-2 terrain presentation A/B at identical solver states."""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import io
import json
import math
import shlex
import struct
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
STUDY_RUNNER_PATH = Path(__file__).resolve().with_name("run_terrain_site_water_v1.py")
OUTPUT_BASE = ROOT / "outputs/fluid/terrain-site-water-presentation-ab-v1-20260923"
FROZEN_STUDY_OUTPUT = ROOT / "outputs/fluid/terrain-site-water-v1-20260923"
DEFAULT_OUTPUT = OUTPUT_BASE / "rank-02-visibility-stress"
DEFAULT_APP = ROOT / "build/dev/projects/fluid/fluid_25d/fluid_25d"
CASE_ID = "rank-02-temperate-mountain-valley"
TIER_ID = "visibility-stress"
PALETTE_LOW_QUANTILE = 5.0
PALETTE_HIGH_QUANTILE = 95.0
PALETTE_LOW_M = 874.896182
PALETTE_HIGH_M = 1798.506873
PALETTE_BOUND_TOLERANCE_M = 0.0005
CHECKPOINTS = (300, 600)
CAMERA_DISTANCE_M = 12000.0

VARIANTS: dict[str, dict[str, str]] = {
    "baseline": {},
    "palette-only": {
        "--fluid25d-terrain-palette-low-m": f"{PALETTE_LOW_M:.6f}",
        "--fluid25d-terrain-palette-high-m": f"{PALETTE_HIGH_M:.6f}",
    },
    "scale-only": {"--fluid25d-render-height-scale": "0.6"},
    "camera-only": {"--fluid25d-home-camera-distance-m": f"{CAMERA_DISTANCE_M:.1f}"},
    "combined": {
        "--fluid25d-terrain-palette-low-m": f"{PALETTE_LOW_M:.6f}",
        "--fluid25d-terrain-palette-high-m": f"{PALETTE_HIGH_M:.6f}",
        "--fluid25d-render-height-scale": "0.6",
        "--fluid25d-home-camera-distance-m": f"{CAMERA_DISTANCE_M:.1f}",
    },
}

PRESENTATION_SOURCE_FILES = (
    "projects/fluid/fluid_25d/fluid_25d_project_config.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_presentation.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_app.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_commands.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_commands.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_gpu_resources.cpp",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_terrain.frag",
)


class PresentationStudyError(RuntimeError):
    pass


def _load_frozen_study_runner() -> Any:
    spec = importlib.util.spec_from_file_location("terrain_site_water_v1_frozen", STUDY_RUNNER_PATH)
    if spec is None or spec.loader is None:
        raise PresentationStudyError(f"cannot load frozen study runner: {STUDY_RUNNER_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


FROZEN_STUDY = _load_frozen_study_runner()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def linear_percentile(sorted_values: list[float], quantile: float) -> float:
    if not sorted_values or not 0.0 <= quantile <= 100.0:
        raise PresentationStudyError("percentile requires values and a quantile within 0..100")
    position = (len(sorted_values) - 1) * quantile / 100.0
    lower = math.floor(position)
    upper = math.ceil(position)
    blend = position - lower
    return sorted_values[lower] * (1.0 - blend) + sorted_values[upper] * blend


def _float32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def crop_elevation_statistics(manifest_path: Path, case: dict[str, Any]) -> dict[str, Any]:
    manifest = json.loads(manifest_path.read_text())
    file_record = manifest["files"]["elevation"]
    elevation_path = manifest_path.parent / file_record["path"]
    if not elevation_path.is_file():
        raise PresentationStudyError(f"pinned elevation payload is missing: {elevation_path}")
    if sha256_file(elevation_path) != case["elevation_sha256"]:
        raise PresentationStudyError("rank-2 elevation payload SHA-256 does not match the frozen recipe")

    native_width = int(manifest["grid"]["width"])
    native_height = int(manifest["grid"]["height"])
    if file_record.get("dtype") != "float32-le" or manifest["grid"].get("axis_mapping") != {
        "world_x": "model_j",
        "world_z": "model_i",
    }:
        raise PresentationStudyError("rank-2 elevation layout differs from the pinned row-major source")
    crop = case["crop"]
    x0, z0, width, height = (int(crop[key]) for key in ("x", "z", "width", "height"))
    if x0 < 0 or z0 < 0 or x0 + width > native_width or z0 + height > native_height:
        raise PresentationStudyError("rank-2 frozen crop is outside the source elevation grid")

    # Match TerrainRasterHeightSource's source contract exactly: manifest
    # transform values and every arithmetic stage are float32, not Python's
    # default float64. The runtime provenance hash covers this transformed
    # crop in z-major order, so derive palette percentiles from those same
    # physical values rather than the model's raw elevation samples.
    height_offset_m = _float32(float(manifest["height"]["offset_m"]))
    height_scale = _float32(float(manifest["height"]["scale"]))
    values: list[float] = []
    crop_digest = hashlib.sha256()
    with elevation_path.open("rb") as stream:
        for z in range(z0, z0 + height):
            stream.seek((z * native_width + x0) * 4)
            row = stream.read(width * 4)
            if len(row) != width * 4:
                raise PresentationStudyError("rank-2 crop row is truncated")
            for (raw_height,) in struct.iter_unpack("<f", row):
                transformed_height = _float32(
                    _float32(raw_height + height_offset_m) * height_scale
                )
                values.append(transformed_height)
                crop_digest.update(struct.pack("<f", transformed_height))
    if len(values) != width * height or not all(math.isfinite(value) for value in values):
        raise PresentationStudyError("rank-2 crop contains missing or non-finite elevation values")
    ordered = sorted(values)
    low = linear_percentile(ordered, PALETTE_LOW_QUANTILE)
    high = linear_percentile(ordered, PALETTE_HIGH_QUANTILE)
    if abs(low - PALETTE_LOW_M) > PALETTE_BOUND_TOLERANCE_M or abs(
        high - PALETTE_HIGH_M
    ) > PALETTE_BOUND_TOLERANCE_M:
        raise PresentationStudyError(
            "rank-2 crop percentiles differ from the frozen p05/p95 palette bounds"
        )
    return {
        "elevation_path": str(elevation_path),
        "elevation_sha256": case["elevation_sha256"],
        "crop_sha256": crop_digest.hexdigest(),
        "crop": crop,
        "source_height_transform": {
            "offset_m_float32": height_offset_m,
            "scale_float32": height_scale,
        },
        "minimum_m": ordered[0],
        "maximum_m": ordered[-1],
        "palette_percentiles": {
            "method": "linear",
            "low_quantile": PALETTE_LOW_QUANTILE,
            "low_m": low,
            "high_quantile": PALETTE_HIGH_QUANTILE,
            "high_m": high,
        },
    }


def preflight(app_path: Path) -> dict[str, Any]:
    if not app_path.is_file():
        raise PresentationStudyError(f"fluid_25d application is missing: {app_path}")
    if not app_path.stat().st_mode & 0o111:
        raise PresentationStudyError(f"fluid_25d application is not executable: {app_path}")

    recipe, validated_cases = FROZEN_STUDY.load_frozen_recipe(
        FROZEN_STUDY.RECIPE_PATH, ROOT
    )
    case_match = [
        (case, manifest_path)
        for case, manifest_path in validated_cases
        if case["id"] == CASE_ID
    ]
    if len(case_match) != 1:
        raise PresentationStudyError("frozen recipe does not contain exactly one rank-2 case")
    case, manifest_path = case_match[0]
    tier_match = [tier for tier in recipe["protocol"]["tiers"] if tier["id"] == TIER_ID]
    if len(tier_match) != 1:
        raise PresentationStudyError("frozen recipe does not contain exactly one visibility-stress tier")
    tier = tier_match[0]
    terrain = crop_elevation_statistics(manifest_path, case)

    shader_dir = app_path.parent / "shaders"
    shader_files = sorted(shader_dir.glob("*.spv"))
    if not shader_files:
        raise PresentationStudyError(f"built shader assets are missing: {shader_dir}")
    source_hashes = {}
    for relative in PRESENTATION_SOURCE_FILES:
        source_path = ROOT / relative
        if not source_path.is_file():
            raise PresentationStudyError(f"presentation source is missing: {source_path}")
        source_hashes[relative] = sha256_file(source_path)

    return {
        "recipe": recipe,
        "case": case,
        "manifest_path": manifest_path,
        "tier": tier,
        "terrain": terrain,
        "app": {
            "path": str(app_path),
            "sha256": sha256_file(app_path),
        },
        "shader_assets": {
            path.name: sha256_file(path)
            for path in shader_files
        },
        "presentation_sources": source_hashes,
    }


def render_options(variant: str) -> list[str]:
    if variant not in VARIANTS:
        raise PresentationStudyError(f"unsupported render variant: {variant}")
    return [argument for option, value in VARIANTS[variant].items() for argument in (option, value)]


def build_capture_command(
    app: Path,
    case: dict[str, Any],
    manifest_path: Path,
    tier: dict[str, Any],
    frame: int,
    variant: str,
    capture_path: Path,
    profile_prefix: Path,
) -> list[str]:
    base = FROZEN_STUDY.common_app_args(
        app,
        case,
        manifest_path,
        tier,
        frame,
        1280,
        720,
    )
    return [
        *base,
        *FROZEN_STUDY.view_args("composite"),
        *render_options(variant),
        "--profile-output",
        str(profile_prefix),
        "--profile-diagnostics",
        "--profile-diagnostic-interval",
        str(frame - 1),
        "--output",
        str(capture_path),
    ]


def physics_metric_rows(metrics_path: Path, profile_frame: int) -> dict[str, str]:
    if not metrics_path.is_file():
        raise PresentationStudyError(f"profile metrics are missing: {metrics_path}")
    rows: dict[str, str] = {}
    with metrics_path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["frame_index", "category", "name", "value"]:
            raise PresentationStudyError(f"unexpected profile metrics header: {metrics_path}")
        for row in reader:
            category = row["category"]
            if int(row["frame_index"]) != profile_frame or not category.startswith("fluid_25d."):
                continue
            key = f"{category}.{row['name']}"
            if key in rows:
                raise PresentationStudyError(
                    f"duplicate profile metric row at frame {profile_frame}: {key}"
                )
            rows[key] = row["value"]
    required = {
        "fluid_25d.solver.finite_volume_status_flags",
        "fluid_25d.water.total_water_volume_m3",
        "fluid_25d.water.maximum_depth_m",
        "fluid_25d.water.cumulative_source_volume_m3",
        "fluid_25d.water.cumulative_boundary_outflow_volume_m3",
    }
    if not required.issubset(rows):
        raise PresentationStudyError(
            f"profile metrics lack required solver fields at profile frame {profile_frame}: "
            f"{sorted(required - rows.keys())}"
        )
    if float(rows["fluid_25d.solver.finite_volume_status_flags"]) != 0.0:
        raise PresentationStudyError(
            f"finite-volume status flags are nonzero at profile frame {profile_frame}"
        )
    return rows


def canonical_profile_csv_sha256(profile_frame: int, rows: dict[str, str]) -> str:
    """Hash all selected solver rows in stable key order and CSV encoding."""
    stream = io.StringIO(newline="")
    writer = csv.writer(stream, lineterminator="\n")
    writer.writerow(["frame_index", "category", "name", "value"])
    for key, value in sorted(rows.items()):
        category, name = key.rsplit(".", 1)
        writer.writerow([profile_frame, category, name, value])
    return hashlib.sha256(stream.getvalue().encode("utf-8")).hexdigest()


def compare_checkpoint_profiles(
    baseline: dict[int, dict[str, str]],
    candidate: dict[int, dict[str, str]],
) -> None:
    if set(baseline) != set(candidate):
        raise PresentationStudyError("variant checkpoints do not match the baseline frame set")
    for frame in sorted(baseline):
        if baseline[frame] != candidate[frame]:
            differing = sorted(
                key
                for key in baseline[frame].keys() | candidate[frame].keys()
                if baseline[frame].get(key) != candidate[frame].get(key)
            )
            raise PresentationStudyError(
                f"solver profile differs from baseline at profile frame {frame}: {differing}"
            )


def _run_capture(command: list[str], log_path: Path, commands_path: Path) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with commands_path.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps({"argv": command, "cwd": str(ROOT), "log": str(log_path)}) + "\n")
        stream.flush()
    with log_path.open("w", encoding="utf-8") as stream:
        completed = subprocess.run(
            command,
            cwd=ROOT,
            text=True,
            stdout=stream,
            stderr=subprocess.STDOUT,
            check=False,
        )
    if completed.returncode != 0:
        raise PresentationStudyError(
            f"capture failed with exit {completed.returncode}; see {log_path}: {shlex.join(command)}"
        )


def execute_study(app_path: Path, output_dir: Path) -> dict[str, Any]:
    if output_dir.exists():
        raise PresentationStudyError(f"refusing to overwrite existing presentation output: {output_dir}")
    if output_dir.resolve().is_relative_to(FROZEN_STUDY_OUTPUT.resolve()):
        raise PresentationStudyError("presentation output may not be placed inside the frozen solver study")

    checked = preflight(app_path)
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    output_dir.mkdir(parents=True, exist_ok=False)
    (output_dir / "captures").mkdir()
    (output_dir / "profiles").mkdir()
    (output_dir / "logs").mkdir()
    recipe_copy = output_dir / "recipe.json"
    recipe_copy.write_bytes(FROZEN_STUDY.RECIPE_PATH.read_bytes())
    commands_path = output_dir / "commands.jsonl"
    commands_path.touch()

    case = checked["case"]
    tier = checked["tier"]
    manifest_path = checked["manifest_path"]
    runtime_identities: dict[str, dict[str, Any]] = {}
    checkpoint_metrics: dict[str, dict[int, dict[str, str]]] = {}
    output_hashes: dict[str, str] = {}

    for variant in VARIANTS:
        variant_metrics: dict[int, dict[str, str]] = {}
        for frame in CHECKPOINTS:
            frame_label = f"f{frame:04d}"
            variant_root = output_dir / "captures" / variant
            variant_root.mkdir(exist_ok=True)
            capture_path = variant_root / f"{frame_label}-composite.png"
            profile_prefix = output_dir / "profiles" / f"{variant}-{frame_label}"
            log_path = output_dir / "logs" / f"{variant}-{frame_label}.log"
            command = build_capture_command(
                app_path,
                case,
                manifest_path,
                tier,
                frame,
                variant,
                capture_path,
                profile_prefix,
            )
            _run_capture(command, log_path, commands_path)
            if not capture_path.is_file() or capture_path.stat().st_size == 0:
                raise PresentationStudyError(f"headless Composite capture is missing: {capture_path}")
            runtime_identity = FROZEN_STUDY.runtime_identity(log_path, case)
            if runtime_identity["crop_sha256"] != checked["terrain"]["crop_sha256"]:
                raise PresentationStudyError("runtime crop bytes do not match the pinned rank-2 source crop")
            previous_identity = runtime_identities.setdefault(variant, runtime_identity)
            if previous_identity != runtime_identity:
                raise PresentationStudyError(f"runtime terrain identity changed within {variant}")

            metrics_path = profile_prefix.with_name(profile_prefix.name + ".metrics.csv")
            profile_frame = frame - 1
            rows = physics_metric_rows(metrics_path, profile_frame)
            variant_metrics[frame] = rows
            output_hashes[str(capture_path.relative_to(output_dir))] = sha256_file(capture_path)
        checkpoint_metrics[variant] = variant_metrics

    baseline = checkpoint_metrics["baseline"]
    equivalence: dict[str, dict[str, Any]] = {}
    for variant, metrics in checkpoint_metrics.items():
        compare_checkpoint_profiles(baseline, metrics)
        equivalence[variant] = {
            f"f{frame:04d}": {
                "profile_frame_index": frame - 1,
                "simulation_seconds": frame * 2,
                "matching_baseline_metric_count": len(metrics[frame]),
                "canonical_profile_csv_sha256": canonical_profile_csv_sha256(
                    frame - 1, metrics[frame]
                ),
                "metrics": metrics[frame],
            }
            for frame in CHECKPOINTS
        }

    metadata = {
        "schema": "cubey.fluid25d.terrain_presentation_ab_v1.evidence",
        "status": "PASS",
        "decision_boundary": (
            "Render-only comparison on the frozen rank-2 visibility-stress case. "
            "The earlier terrain-site solver recipe and outputs are unchanged."
        ),
        "source_recipe_sha256": FROZEN_STUDY.sha256_file(FROZEN_STUDY.RECIPE_PATH),
        "presentation_runner_sha256": sha256_file(Path(__file__).resolve()),
        "app": checked["app"],
        "shader_assets": checked["shader_assets"],
        "presentation_sources": checked["presentation_sources"],
        "case": case,
        "tier": tier,
        "terrain": checked["terrain"],
        "solver_protocol": {
            key: checked["recipe"]["protocol"][key]
            for key in (
                "solver",
                "terrain_water_protocol",
                "fixed_delta_seconds",
                "substeps",
                "rain_duration_seconds",
                "capture_width",
                "capture_height",
            )
        },
        "variants": {
            variant: {
                "render_cli": options,
                "height_scale": 0.6 if variant in ("scale-only", "combined") else 0.08,
                "palette_bounds_m": [PALETTE_LOW_M, PALETTE_HIGH_M]
                if variant in ("palette-only", "combined")
                else None,
                "home_camera_distance_m": CAMERA_DISTANCE_M
                if variant in ("camera-only", "combined")
                else 16096.5,
                "runtime_identity": runtime_identities[variant],
            }
            for variant, options in VARIANTS.items()
        },
        "physics_profile_equivalence": equivalence,
        "capture_sha256": output_hashes,
    }
    metadata_path = output_dir / "metadata.json"
    metadata_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")

    artifact_hashes = {}
    for path in sorted(output_dir.rglob("*")):
        if path.is_file() and path.name != "artifact_hashes.json":
            artifact_hashes[str(path.relative_to(output_dir))] = {
                "size_bytes": path.stat().st_size,
                "sha256": sha256_file(path),
            }
    (output_dir / "artifact_hashes.json").write_text(
        json.dumps(artifact_hashes, indent=2, sort_keys=True) + "\n"
    )
    return metadata


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=DEFAULT_APP)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--preflight-only",
        action="store_true",
        help="validate the frozen recipe, rank-2 crop, app, shaders, and p05/p95 palette without captures",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        app_path = args.app if args.app.is_absolute() else ROOT / args.app
        if args.preflight_only:
            checked = preflight(app_path.resolve())
            result = {
                "status": "PASS",
                "app": checked["app"],
                "case_id": checked["case"]["id"],
                "tier_id": checked["tier"]["id"],
                "terrain": checked["terrain"],
                "shader_asset_count": len(checked["shader_assets"]),
            }
            print(json.dumps(result, indent=2, sort_keys=True))
            return 0
        output_dir = args.output_dir if args.output_dir.is_absolute() else ROOT / args.output_dir
        result = execute_study(app_path.resolve(), output_dir.resolve())
        print(json.dumps({"status": result["status"], "output_dir": str(output_dir.resolve())}, indent=2))
        return 0
    except (OSError, ValueError, KeyError, PresentationStudyError, FROZEN_STUDY.StudyError) as error:
        print(f"terrain presentation A/B failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
