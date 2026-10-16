#!/usr/bin/env python3
"""Run and verify the bounded hillside motion/readability V4 evidence.

The profile phase compares marker-enabled runs with retained V3 profiles. The
capture phase records marker-on/off views with a fixed physical-time mapping.
Neither phase changes the pinned terrain, source, or finite-volume recipe.
"""

from __future__ import annotations

import argparse
from decimal import Decimal
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import subprocess
import time
from typing import Any

import run_hillside_sustained_flow_v2 as v2
import run_hillside_transport_v3 as v3

ROOT = v2.ROOT
APP_DEFAULT = v2.DEFAULT_APP
V3_ROOT = ROOT / "outputs/fluid/hillside-conservation-v3-20260928-SlPlGa"
V3_REVIEW_PATH = V3_ROOT / "aligned-evidence-review.json"
DEFAULT_OUT = ROOT / "outputs/fluid/hillside-motion-v4-20260930-OqfDt7"

HORIZON_SECONDS = v2.HORIZON_SECONDS
FRAME_COUNT = HORIZON_SECONDS // v2.DT
FPS = v2.FPS
PLAYBACK_ACCELERATION = v2.PLAYBACK_ACCELERATION
PULSE_START_SECONDS = 3600
PULSE_DURATION_SECONDS = 120
SMOKE_FRAME_COUNT = 60
CSV_RESIDUAL_AGREEMENT_TOLERANCE = Decimal("0.000003")
MARKER_METRIC_CATEGORY = "fluid_25d.motion_markers"
MARKER_METRICS = (
    "active_count",
    "farthest_from_source_m",
    "mean_from_source_m",
    "maximum_age_s",
    "hash_hi_u32",
    "hash_lo_u32",
)
EXPECTED_WIDTH = 1280
EXPECTED_HEIGHT = 720
PALETTE_LEGEND = (
    "Dye concentration: fixed log scale | clear below 0.01% | "
    "0.1%, 1%, 10%, 100% of input"
)

HYDRAULIC_IDENTITY = v3.HYDRAULIC_IDENTITY
SOLVER = v3.SOLVER
WATER = v3.WATER
TRACER = v3.TRACER
HASH_METRICS = {
    (HYDRAULIC_IDENTITY, "hash_hi_u32"),
    (HYDRAULIC_IDENTITY, "hash_lo_u32"),
}
WATER_BUDGET_METRICS = {
    (SOLVER, "finite_volume_status_flags"),
    (WATER, "cumulative_source_volume_m3"),
    (WATER, "cumulative_sink_volume_m3"),
    (WATER, "cumulative_boundary_outflow_volume_m3"),
    (WATER, "total_water_volume_m3"),
    (WATER, "conservation_residual_m3"),
}

SOLVER_SPIRV_MODULES = (
    "fluid_25d_fv_cfl.comp.spv",
    "fluid_25d_fv_cfl_finalize.comp.spv",
    "fluid_25d_fv_commit.comp.spv",
    "fluid_25d_fv_reset.comp.spv",
    "fluid_25d_fv_update.comp.spv",
)
SOLVER_SOURCE_PATHS = (
    "projects/fluid/sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_finite_volume_oracle.h",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_conserved_arithmetic.glsl",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_fv_cfl.comp",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_fv_commit.comp",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_fv_reset.comp",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_fv_update.comp",
)

PROFILE_CASES = (
    ("profile-256-no-dye", 256, False),
    ("profile-512-dye", 512, True),
)
EXCERPT_SPECS = (
    ("initial-00-15min", 0, 15 * 60),
    ("mature-30-45min", 30 * 60, 45 * 60),
    ("pulse-55-85min", 55 * 60, 85 * 60),
)
VIDEO_CASES = (
    ("source-markers", "source", "-0.72", True, "Source"),
    ("source-markers-off", "source", "-0.72", False, "Source matched overlay off"),
    ("branch-markers", "branch", "-0.72", True, "Downstream branch"),
    ("source-topdown-markers", "source", "-1.55", True, "Source topdown"),
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def ensure_absent(paths: list[Path]) -> None:
    existing = [str(path) for path in paths if path.exists()]
    if existing:
        raise FileExistsError("refusing to overwrite existing evidence: " + ", ".join(existing))


def create_phase_directory(out: Path, phase: str) -> Path:
    resolved = out.resolve()
    baseline = V3_ROOT.resolve()
    if resolved == baseline or baseline in resolved.parents:
        raise ValueError("V4 output must not be inside the retained V3 evidence root")
    resolved.mkdir(parents=True, exist_ok=True)
    target = resolved / phase
    target.mkdir()
    return target


def write_report(path: Path, report: dict[str, Any]) -> None:
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")


def read_report(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text())
    if not isinstance(value, dict):
        raise ValueError(f"evidence report is not a JSON object: {path}")
    return value


def failed_case_record(label: str, case_dir: Path, error: Exception) -> dict[str, Any]:
    record: dict[str, Any] = {
        "label": label,
        "error": f"{type(error).__name__}: {error}",
    }
    receipt_path = case_dir / "execution.json"
    if receipt_path.is_file():
        record["execution_receipt_path"] = str(receipt_path.resolve())
        record["execution_receipt_sha256"] = sha256_file(receipt_path)
        record["execution_receipt"] = read_report(receipt_path)
    return record


def _is_timing_metric(metric: tuple[str, str]) -> bool:
    category, name = (part.lower() for part in metric)
    timing_terms = ("timing", "elapsed", "duration", "wall_time", "gpu_time", "gpu",
                    "cpu_time", "milliseconds", "microseconds", "nanoseconds")
    return (any(term in category for term in timing_terms) or
            any(term in name for term in timing_terms) or
            name.endswith(("_ms", "_us", "_µs", "_ns")))


def _validate_marker_metrics(values: dict[tuple[str, str], Decimal], frame: int) -> None:
    missing = {(MARKER_METRIC_CATEGORY, name) for name in MARKER_METRICS} - values.keys()
    if missing:
        raise ValueError(f"marker profile is missing required state metrics at frame {frame}: "
                         f"{sorted(missing)}")
    active = values[(MARKER_METRIC_CATEGORY, "active_count")]
    farthest = values[(MARKER_METRIC_CATEGORY, "farthest_from_source_m")]
    mean_distance = values[(MARKER_METRIC_CATEGORY, "mean_from_source_m")]
    maximum_age = values[(MARKER_METRIC_CATEGORY, "maximum_age_s")]
    hash_words = [values[(MARKER_METRIC_CATEGORY, name)]
                  for name in ("hash_hi_u32", "hash_lo_u32")]
    if (active < 0 or active != active.to_integral_value() or
            min(farthest, mean_distance, maximum_age) < 0 or
            mean_distance > farthest + Decimal("0.000000001")):
        raise ValueError(f"marker profile has invalid counts, distances, or ages at frame {frame}")
    if active == 0 and (farthest != 0 or mean_distance != 0 or maximum_age != 0):
        raise ValueError(f"inactive marker profile has nonzero motion summary at frame {frame}")
    for word in hash_words:
        if (word < 0 or word > 0xFFFFFFFF or
                word != word.to_integral_value()):
            raise ValueError(f"marker profile has an invalid uint32 state hash at frame {frame}")


def compare_full_common_profiles(
    baseline: dict[int, dict[tuple[str, str], Decimal]],
    current: dict[int, dict[tuple[str, str], Decimal]],
    *,
    frame_count: int = FRAME_COUNT,
) -> dict[str, Any]:
    expected_frames = list(range(frame_count))
    if sorted(baseline) != expected_frames or sorted(current) != expected_frames:
        raise ValueError("profiles do not contain every requested fixed-step frame exactly once")
    v3._check_identity_metrics(baseline, "V3 baseline", frame_count)
    v3._check_identity_metrics(current, "V4 marker-enabled", frame_count)

    common_metrics: set[tuple[str, str]] | None = None
    added_metrics: set[tuple[str, str]] = set()
    ignored_timing_metrics: set[tuple[str, str]] = set()
    compared_values = 0
    marker_frames_with_metrics = 0
    for frame in expected_frames:
        baseline_values = baseline[frame]
        current_values = current[frame]
        baseline_numerical = {key: value for key, value in baseline_values.items()
                              if not _is_timing_metric(key)}
        current_numerical = {key: value for key, value in current_values.items()
                             if not _is_timing_metric(key)}
        _validate_marker_metrics(current_numerical, frame)
        marker_frames_with_metrics += 1
        missing = baseline_numerical.keys() - current_numerical.keys()
        if missing:
            raise ValueError(f"V4 profile omits baseline numerical metrics at frame {frame}: "
                             f"{sorted(missing)[:5]}")
        common = baseline_numerical.keys() & current_numerical.keys()
        common_metrics = set(common) if common_metrics is None else common_metrics & common
        added_here = current_numerical.keys() - baseline_numerical.keys()
        unexpected = {key for key in added_here if key[0] != MARKER_METRIC_CATEGORY}
        if unexpected:
            raise ValueError("V4 profile adds non-marker numerical metrics: "
                             f"{sorted(unexpected)[:5]}")
        added_metrics.update(added_here)
        ignored_timing_metrics.update(
            key for key in baseline_values.keys() | current_values.keys() if _is_timing_metric(key))
        changed = [key for key in common if baseline_values[key] != current_values[key]]
        if changed:
            details = [(key, str(baseline_values[key]), str(current_values[key]))
                       for key in sorted(changed[:5])]
            raise ValueError(f"V3/V4 numerical profile metrics differ at frame {frame}: {details}")
        compared_values += len(common)

    stable_common = common_metrics or set()
    if not HASH_METRICS.issubset(stable_common):
        raise ValueError("full profile comparison did not include both hydraulic identity hash words")
    if added_metrics and any(key[0] != MARKER_METRIC_CATEGORY for key in added_metrics):
        raise ValueError("V4 profile has an unapproved marker-only metric category")
    return {
        "passed": True,
        "frames_compared": frame_count,
        "marker_metric_frames": marker_frames_with_metrics,
        "metric_values_compared": compared_values,
        "common_metric_keys": [list(key) for key in sorted(stable_common)],
        "current_only_metric_keys": [list(key) for key in sorted(added_metrics)],
        "current_only_metric_category_allowed": MARKER_METRIC_CATEGORY,
        "timing_metrics_excluded": [list(key) for key in sorted(ignored_timing_metrics)],
        "identity_hash_metrics_compared": [list(key) for key in sorted(HASH_METRICS)],
        "comparison": "exact Decimal equality for every common non-timing metric at every frame",
    }


def validate_water_budget_profile(
    frames: dict[int, dict[tuple[str, str], Decimal]],
    *,
    frame_count: int = FRAME_COUNT,
) -> dict[str, Any]:
    if sorted(frames) != list(range(frame_count)):
        raise ValueError("water profile does not contain every requested fixed-step frame exactly once")
    previous_source = Decimal(0)
    previous_export = Decimal(0)
    maximum_residual = Decimal(0)
    maximum_source_error = Decimal(0)
    for frame, values in frames.items():
        missing = WATER_BUDGET_METRICS - values.keys()
        if missing:
            raise ValueError(f"water profile is missing required budget metrics at frame {frame}: "
                             f"{sorted(missing)}")
        flags = values[(SOLVER, "finite_volume_status_flags")]
        if flags != 0:
            raise ValueError(f"water profile has nonzero finite-volume status flags at frame {frame}")
        source = values[(WATER, "cumulative_source_volume_m3")]
        sink = values[(WATER, "cumulative_sink_volume_m3")]
        exported = values[(WATER, "cumulative_boundary_outflow_volume_m3")]
        stored = values[(WATER, "total_water_volume_m3")]
        reported = values[(WATER, "conservation_residual_m3")]
        expected_source = Decimal(v2.Q_M3_PER_S * v2.DT * (frame + 1))
        tolerance = max(Decimal("0.003"), expected_source * Decimal("0.0003"))
        recomputed = stored - source + sink + exported
        if abs(source - expected_source) > tolerance:
            raise ValueError(f"water source differs from the frozen 100 m3/s schedule at frame {frame}")
        if sink != 0:
            raise ValueError(f"water profile has a nonzero explicit sink at frame {frame}")
        if min(source, sink, exported, stored) < 0 or source < previous_source or exported < previous_export:
            raise ValueError(f"water profile has a negative or decreasing cumulative quantity at frame {frame}")
        if abs(reported - recomputed) > CSV_RESIDUAL_AGREEMENT_TOLERANCE:
            raise ValueError(f"reported water residual disagrees with its independently recomputed ledger "
                             f"at frame {frame}")
        if abs(reported) > tolerance or abs(recomputed) > tolerance:
            raise ValueError(f"water conservation residual exceeds the existing 3e-4 tolerance "
                             f"at frame {frame}")
        maximum_residual = max(maximum_residual, abs(reported), abs(recomputed))
        maximum_source_error = max(maximum_source_error, abs(source - expected_source))
        previous_source, previous_export = source, exported
    return {
        "passed": True,
        "frames_checked": frame_count,
        "maximum_absolute_water_residual_m3": float(maximum_residual),
        "maximum_absolute_source_schedule_error_m3": float(maximum_source_error),
        "tolerance_m3": "max(0.003, cumulative_source_volume_m3 * 0.0003) per frame",
        "reported_ledger_agreement_tolerance_m3": float(CSV_RESIDUAL_AGREEMENT_TOLERANCE),
        "nonzero_solver_flag_frames": [],
        "explicit_sink_m3": 0,
    }


def _current_solver_source_hashes() -> dict[str, str]:
    return {name: sha256_file(ROOT / name) for name in SOLVER_SOURCE_PATHS}


def _solver_bridge(
    pinned: dict[str, Any],
    review: dict[str, Any],
) -> dict[str, Any]:
    baseline_shaders = review.get("shader_hashes")
    baseline_sources = review.get("source_hashes")
    if not isinstance(baseline_shaders, dict) or not isinstance(baseline_sources, dict):
        raise ValueError("retained V3 review lacks its full shader or source hash map")
    current_shaders = pinned["shader_identity"]["files"]
    shader_bridge: dict[str, dict[str, Any]] = {}
    for name in SOLVER_SPIRV_MODULES:
        before = baseline_shaders.get(name)
        after = current_shaders.get(name)
        if not before or not after or before != after:
            raise ValueError(f"finite-volume solver SPIR-V changed from retained V3: {name}")
        shader_bridge[name] = {"v3_sha256": before, "v4_sha256": after, "matches": True}

    current_sources = _current_solver_source_hashes()
    source_bridge: dict[str, dict[str, Any]] = {}
    for name in SOLVER_SOURCE_PATHS:
        before = baseline_sources.get(name)
        after = current_sources.get(name)
        if not before or before != after:
            raise ValueError(f"finite-volume solver CPU/source hash changed from retained V3: {name}")
        source_bridge[name] = {"v3_sha256": before, "v4_sha256": after, "matches": True}
    return {
        "passed": True,
        "v3_review_path": str(V3_REVIEW_PATH.resolve()),
        "v3_review_sha256": sha256_file(V3_REVIEW_PATH),
        "v3_app_sha256": review["app_sha256"],
        "v4_app_sha256": pinned["app_sha256"],
        "solver_spirv_hashes": shader_bridge,
        "solver_cpu_and_glsl_source_hashes": source_bridge,
        "scope": "retained strict results are bridged by unchanged solver SPIR-V and CPU/source hashes plus exact full profiles; no V4 strict rerun is claimed",
    }


def _execution_identity(app: Path, domain: int) -> dict[str, Any]:
    pinned = v2.pinned_inputs(domain, app.resolve())
    return {
        "app_sha256": pinned["app_sha256"],
        "shader_map_sha256": pinned["shader_map_sha256"],
        "shader_hashes": pinned["shader_identity"]["files"],
        "recipe_sha256": pinned["recipe_sha256"],
        "manifest_sha256": pinned["manifest_sha256"],
        "elevation_sha256": pinned["elevation_sha256"],
        "solver_spirv_hashes": {
            name: pinned["shader_identity"]["files"][name]
            for name in SOLVER_SPIRV_MODULES
        },
        "solver_source_hashes": _current_solver_source_hashes(),
    }


def _program_identity(identity: dict[str, Any]) -> dict[str, Any]:
    """Identity fields shared across the 256 and 512 pinned recipe variants."""
    return {name: identity[name] for name in (
        "app_sha256", "shader_map_sha256", "shader_hashes",
        "solver_spirv_hashes", "solver_source_hashes",
    )}


def load_v3_baselines() -> dict[str, Any]:
    if not V3_REVIEW_PATH.is_file():
        raise FileNotFoundError(f"missing retained V3 aligned evidence review: {V3_REVIEW_PATH}")
    review = read_report(V3_REVIEW_PATH)
    if (review.get("schema") != "cubey.fluid25d.hillside_transport_v3.final_local_review" or
            review.get("completion_checks_passed") is not True):
        raise ValueError("retained V3 aligned evidence review is not a passing closure record")
    baseline_app_sha = review.get("app_sha256")
    baseline_shaders = review.get("shader_hashes")
    if not baseline_app_sha or not isinstance(baseline_shaders, dict):
        raise ValueError("retained V3 review is missing its original app/shader identity")

    metrics_256 = V3_ROOT / "aligned-256/hydraulics-profile.metrics.csv"
    report_256 = read_report(V3_ROOT / "aligned-256/hydraulics.json")
    summary_256 = report_256.get("summary", {})
    if (report_256.get("domain") != 256 or summary_256.get("physical_horizon_seconds") != HORIZON_SECONDS or
            summary_256.get("numeric_profile_checks_passed") is not True or
            summary_256.get("nonzero_solver_flag_frames") != [] or
            summary_256.get("unhealthy_frames") != [] or
            report_256.get("app_sha256") != baseline_app_sha):
        raise ValueError("retained V3 256 profile does not pass its all-frame numerical/status gate")
    recorded_256_hash = None
    for check in review.get("byte_identity_checks", []):
        paths = {str(Path(check.get(key, "")).resolve()) for key in ("left", "right") if check.get(key)}
        if str(metrics_256.resolve()) in paths and check.get("byte_identical") is True:
            recorded_256_hash = check.get("sha256")
    if not metrics_256.is_file() or not recorded_256_hash or sha256_file(metrics_256) != recorded_256_hash:
        raise ValueError("retained V3 256 profile metrics do not match their aligned-evidence receipt")

    metrics_512_dye = V3_ROOT / "aligned-512-dye/profile.metrics.csv"
    metrics_512_water = V3_ROOT / "aligned-512-water/profile.metrics.csv"
    execution_dye_path = V3_ROOT / "aligned-512-dye/execution.json"
    execution_water_path = V3_ROOT / "aligned-512-water/execution.json"
    execution_dye = read_report(execution_dye_path)
    execution_water = read_report(execution_water_path)
    for label, execution, expected_dye in (
        ("512 dye", execution_dye, True), ("512 water", execution_water, False)
    ):
        if (execution.get("exit_code") != 0 or execution.get("case") != "512" or
                execution.get("frames") != FRAME_COUNT or execution.get("interval") != 1 or
                execution.get("dye") is not expected_dye or
                execution.get("before") != execution.get("after") or
                execution.get("before", {}).get("app_sha256") != baseline_app_sha or
                execution.get("before", {}).get("shader_hashes") != baseline_shaders):
            raise ValueError(f"retained V3 {label} execution receipt is incomplete or changed")
        expected_receipt_sha = review.get("final_run_receipts", {}).get(
            "aligned-512-dye" if expected_dye else "aligned-512-water", {}).get("sha256")
        if not expected_receipt_sha or sha256_file(
                execution_dye_path if expected_dye else execution_water_path) != expected_receipt_sha:
            raise ValueError(f"retained V3 {label} receipt is not the reviewed final execution")
    for path, execution in ((metrics_512_dye, execution_dye), (metrics_512_water, execution_water)):
        digest = execution.get("artifact_hashes", {}).get(path.name)
        if not digest or not path.is_file() or sha256_file(path) != digest:
            raise ValueError(f"retained V3 profile artifact is missing or changed: {path}")

    transport_profile_path = V3_ROOT / "transport/profile.json"
    transport_profile = read_report(transport_profile_path)
    dye_summary = transport_profile.get("dye_profile", {}).get("summary", {})
    if (transport_profile.get("phase_checks_passed") is not True or dye_summary.get("passed") is not True or
            transport_profile.get("dye_profile", {}).get("metrics_sha256") != sha256_file(metrics_512_dye)):
        raise ValueError("retained V3 transport profile does not validate the full 512 dye run")

    strict_receipt_path = V3_ROOT / "aligned-hillside-strict/execution.json"
    strict_receipt = read_report(strict_receipt_path)
    expected_strict_sha = review.get("final_run_receipts", {}).get("aligned-hillside-strict", {}).get("sha256")
    if (not expected_strict_sha or sha256_file(strict_receipt_path) != expected_strict_sha or
            strict_receipt.get("strict") is not True or strict_receipt.get("exit_code") != 0 or
            strict_receipt.get("before") != strict_receipt.get("after") or
            strict_receipt.get("before", {}).get("app_sha256") != baseline_app_sha):
        raise ValueError("retained V3 hillside strict receipt is not the reviewed successful historical run")

    return {
        "review": review,
        "review_path": str(V3_REVIEW_PATH.resolve()),
        "review_sha256": sha256_file(V3_REVIEW_PATH),
        "metrics_256_path": metrics_256,
        "metrics_256_sha256": recorded_256_hash,
        "metrics_256": v3.read_profile(metrics_256),
        "metrics_512_dye_path": metrics_512_dye,
        "metrics_512_dye_sha256": execution_dye["artifact_hashes"][metrics_512_dye.name],
        "metrics_512_dye": v3.read_profile(metrics_512_dye),
        "metrics_512_water_path": metrics_512_water,
        "metrics_512_water_sha256": execution_water["artifact_hashes"][metrics_512_water.name],
        "metrics_512_water": v3.read_profile(metrics_512_water),
        "strict_receipt": {
            "path": str(strict_receipt_path.resolve()),
            "sha256": expected_strict_sha,
            "v3_app_sha256": strict_receipt["before"]["app_sha256"],
            "current_app_rerun": False,
        },
        "profile_comparisons": {
            "profile-256-no-dye": str(metrics_256.resolve()),
            "profile-512-dye": str(metrics_512_dye.resolve()),
        },
    }


def profile_arguments(domain: int, dye: bool, profile_prefix: Path, output: Path) -> list[str]:
    if domain not in (256, 512) or (dye and domain != 512):
        raise ValueError("V4 profiles are 256 no-dye or 512 with the frozen dye pulse")
    args = v2.arguments(domain) + ["--frames", str(FRAME_COUNT), "--capture", "png"]
    if dye:
        args += ["--fluid25d-dye-pulse-start-seconds", str(PULSE_START_SECONDS),
                 "--fluid25d-dye-pulse-duration-seconds", str(PULSE_DURATION_SECONDS)]
    args += [
        "--fluid25d-catchment-view", "transport-inspection" if dye else "water-isolation",
        "--fluid25d-hillside-camera", "source",
        "--fluid25d-natural-flow-home-pitch-radians", "-0.72",
        "--fluid25d-motion-markers",
        "--profile-output", str(profile_prefix.resolve()),
        "--profile-diagnostics", "--profile-diagnostic-interval", "1",
        "--output", str(output.resolve()),
    ]
    return args


def _profile_artifact_paths(case_dir: Path, prefix: Path, output: Path, log: Path) -> dict[str, Path]:
    paths = {
        "child.log": log,
        "final.png": output,
    }
    for suffix in (".frames.csv", ".passes.csv", ".metrics.csv", ".trace.json", ".summary.txt"):
        path = prefix.with_suffix(suffix)
        paths[path.name] = path
    return paths


def _run_profile_case(app: Path, case_dir: Path, label: str, domain: int,
                      dye: bool) -> dict[str, Any]:
    case_dir.mkdir()
    prefix = case_dir / "profile"
    output = case_dir / "final.png"
    log_path = case_dir / "child.log"
    artifacts = _profile_artifact_paths(case_dir, prefix, output, log_path)
    ensure_absent(list(artifacts.values()) + [case_dir / "execution.json"])
    before = _execution_identity(app, domain)
    args = profile_arguments(domain, dye, prefix, output)
    command = ["rtk", "proxy", str(app.resolve()), *args]
    run = _run_command(command, log_path)
    after = _execution_identity(app, domain)
    artifact_hashes = {name: sha256_file(path) for name, path in artifacts.items() if path.is_file()}
    receipt = {
        "schema": "cubey.fluid25d.hillside_motion_v4.execution",
        "label": label,
        "domain": domain,
        "frames": FRAME_COUNT,
        "interval": 1,
        "dye": dye,
        "motion_markers": True,
        "command": command,
        "exit_code": run["exit_code"],
        "launch_error": run["launch_error"],
        "wall_seconds": run["wall_seconds"],
        "log_path": run["log_path"],
        "log_sha256": run["log_sha256"],
        "before": before,
        "after": after,
        "artifact_hashes": artifact_hashes,
        "artifact_paths": {name: str(path.resolve()) for name, path in artifacts.items()},
    }
    write_report(case_dir / "execution.json", receipt)
    required = set(artifacts)
    if run["exit_code"] != 0:
        raise RuntimeError(f"{label} app child failed with exit code {run['exit_code']}")
    if before != after:
        raise ValueError(f"{label} app, shader, source, or frozen input identity changed during its run")
    if not required.issubset(artifact_hashes):
        missing = sorted(required - artifact_hashes.keys())
        raise ValueError(f"{label} profile child omitted required artifacts: {missing}")
    return receipt


def _validate_profile_case(label: str, receipt: dict[str, Any], baselines: dict[str, Any]) -> dict[str, Any]:
    metrics_path = Path(receipt["artifact_paths"]["profile.metrics.csv"])
    metrics = v3.read_profile(metrics_path)
    water_budget = validate_water_budget_profile(metrics)
    baseline_metrics = (baselines["metrics_256"] if receipt["domain"] == 256
                        else baselines["metrics_512_dye"])
    # First prove that the complete profile differs only by the explicitly
    # allowed marker category. The historical dye validator predates markers
    # and requires identical hydraulic metric sets, so only that observational
    # category is excluded from its input, never a hydraulic/tracer metric.
    comparison = compare_full_common_profiles(baseline_metrics, metrics)
    if receipt["domain"] == 256:
        summary = v2.summarize(v2.read_metrics(metrics_path))
        if (summary.get("numeric_profile_checks_passed") is not True or
                summary.get("physical_horizon_seconds") != HORIZON_SECONDS or
                summary.get("nonzero_solver_flag_frames") != [] or
                summary.get("unhealthy_frames") != []):
            raise ValueError("V4 256 profile fails the existing full-horizon water/spatial gate")
    else:
        summary = validate_dye_without_marker_metrics(metrics, baselines["metrics_512_water"])
        if summary.get("passed") is not True:
            raise ValueError("V4 512 dye profile fails the existing tracer budget/motion gate")
    return {
        "label": label,
        "domain": receipt["domain"],
        "dye": receipt["dye"],
        "motion_markers": True,
        "profile_metrics_path": str(metrics_path.resolve()),
        "profile_metrics_sha256": sha256_file(metrics_path),
        "water_budget": water_budget,
        "numerical_gate": summary,
        "v3_full_profile_comparison": comparison,
    }


def validate_dye_without_marker_metrics(metrics, reference):
    filtered = {frame: {key: value for key, value in values.items()
                        if key[0] != MARKER_METRIC_CATEGORY}
                for frame, values in metrics.items()}
    return v3.validate_dye_profile(filtered, reference)


def revalidate_profiles_phase(app: Path, out: Path) -> dict[str, Any]:
    """Validate retained complete executions without overwriting or rerunning them."""
    phase_dir = out.resolve() / "profiles"
    original_path = phase_dir / "profiles.json"
    report_path = phase_dir / "revalidated-profiles.json"
    ensure_absent([report_path])
    original = read_report(original_path)
    identity = _execution_identity(app.resolve(), 512)
    if (original.get("schema") != "cubey.fluid25d.hillside_motion_v4.profiles" or
            original.get("phase_checks_passed") is not False or
            original.get("started_before_app_identity") != identity):
        raise ValueError("profile revalidation requires a retained rejected phase with unchanged inputs")
    original_sha = sha256_file(original_path)
    baselines = load_v3_baselines()
    report = dict(original)
    report["cases"] = []
    for label, domain, dye in PROFILE_CASES:
        case_dir = phase_dir / label
        receipt_path = case_dir / "execution.json"
        receipt = read_report(receipt_path)
        current_identity = _execution_identity(app.resolve(), domain)
        expected_command = ["rtk", "proxy", str(app.resolve()),
                            *profile_arguments(domain, dye, case_dir / "profile", case_dir / "final.png")]
        expected_artifacts = _profile_artifact_paths(case_dir, case_dir / "profile",
                                                     case_dir / "final.png", case_dir / "child.log")
        if (receipt.get("schema") != "cubey.fluid25d.hillside_motion_v4.execution" or
                receipt.get("label") != label or receipt.get("domain") != domain or
                receipt.get("frames") != FRAME_COUNT or receipt.get("interval") != 1 or
                receipt.get("dye") is not dye or receipt.get("motion_markers") is not True or
                receipt.get("exit_code") != 0 or receipt.get("command") != expected_command or
                receipt.get("before") != current_identity or receipt.get("after") != current_identity or
                set(receipt.get("artifact_hashes", {})) != set(expected_artifacts) or
                set(receipt.get("artifact_paths", {})) != set(expected_artifacts)):
            raise ValueError(f"cannot revalidate an incomplete, changed, or unsuccessful execution: {label}")
        for name, path in expected_artifacts.items():
            if (receipt["artifact_paths"][name] != str(path.resolve()) or not path.is_file() or
                    sha256_file(path) != receipt["artifact_hashes"][name]):
                raise ValueError(f"profile revalidation artifact changed: {path}")
        record = _validate_profile_case(label, receipt, baselines)
        record.update({"execution_before": receipt["before"], "execution_after": receipt["after"],
                       "execution_receipt_path": str(receipt_path.resolve()),
                       "execution_receipt_sha256": sha256_file(receipt_path)})
        report["cases"].append(record)
    if _execution_identity(app.resolve(), 512) != identity or sha256_file(original_path) != original_sha:
        raise ValueError("profile revalidation inputs or historical report changed")
    report.update({
        "phase_checks_passed": True,
        "full_horizon_gate_satisfied": True,
        "app_path": str(app.resolve()), "app_sha256": identity["app_sha256"],
        "shader_map_sha256": identity["shader_map_sha256"], "end_input_identity": identity,
        "solver_bridge": _solver_bridge(v2.pinned_inputs(512, app.resolve()), baselines["review"]),
        "retained_rejected_phase": {"path": str(original_path.resolve()), "sha256": original_sha},
        "revalidation_scope": "Same successful original executions and immutable artifacts; separates marker-only diagnostics before the historical dye gate. No GPU rerun or numerical tolerance change.",
    })
    write_report(report_path, report)
    report["report_path"] = str(report_path.resolve())
    report["report_sha256"] = sha256_file(report_path)
    return report


def profiles_phase(app: Path, out: Path) -> dict[str, Any]:
    app = app.resolve()
    start_identity = _execution_identity(app, 512)
    baselines = load_v3_baselines()
    phase_dir = create_phase_directory(out, "profiles")
    report_path = phase_dir / "profiles.json"
    ensure_absent([report_path])
    report: dict[str, Any] = {
        "schema": "cubey.fluid25d.hillside_motion_v4.profiles",
        "phase": "profiles",
        "phase_checks_passed": False,
        "started_before_app_identity": start_identity,
        "cases": [],
        "baseline_evidence": {
            "review_path": baselines["review_path"],
            "review_sha256": baselines["review_sha256"],
            "metrics_256_path": str(baselines["metrics_256_path"].resolve()),
            "metrics_256_sha256": baselines["metrics_256_sha256"],
            "metrics_512_dye_path": str(baselines["metrics_512_dye_path"].resolve()),
            "metrics_512_dye_sha256": baselines["metrics_512_dye_sha256"],
            "retained_strict_receipt": baselines["strict_receipt"],
        },
        "historical_strict_boundary": baselines["review"]["limits"],
        "full_horizon_seconds": HORIZON_SECONDS,
        "fixed_delta_seconds": v2.DT,
        "frames": FRAME_COUNT,
        "diagnostic_interval": 1,
        "source_rate_m3_per_s": v2.Q_M3_PER_S,
        "explicit_sink": "none",
        "rain": "none",
        "timing_metrics": "excluded from exact profile value comparisons",
    }
    write_report(report_path, report)

    for label, domain, dye in PROFILE_CASES:
        case_dir = phase_dir / label
        try:
            receipt = _run_profile_case(app, case_dir, label, domain, dye)
        except Exception as error:
            receipt_path = case_dir / "execution.json"
            failed = {"label": label, "error": f"{type(error).__name__}: {error}"}
            if receipt_path.is_file():
                failed["execution_receipt_path"] = str(receipt_path.resolve())
                failed["execution_receipt_sha256"] = sha256_file(receipt_path)
            report["failed_case"] = failed
            write_report(report_path, report)
            raise
        if _program_identity(receipt["before"]) != _program_identity(start_identity):
            raise ValueError(f"{label} did not use the initial V4 app/shader/input identity")
        record = _validate_profile_case(label, receipt, baselines)
        record["execution_before"] = receipt["before"]
        record["execution_after"] = receipt["after"]
        record["execution_receipt_path"] = str((case_dir / "execution.json").resolve())
        record["execution_receipt_sha256"] = sha256_file(case_dir / "execution.json")
        report["cases"].append(record)
        write_report(report_path, report)

    end_bridge_pinned = v2.pinned_inputs(512, app)
    bridge = _solver_bridge(end_bridge_pinned, baselines["review"])
    end_identity = _execution_identity(app, 512)
    if end_identity != start_identity:
        raise ValueError("app/shader/source/frozen-input identity changed across serialized profiles")
    report["solver_bridge"] = bridge
    report["app_path"] = str(app)
    report["app_sha256"] = end_bridge_pinned["app_sha256"]
    report["shader_map_sha256"] = end_bridge_pinned["shader_map_sha256"]
    report["end_input_identity"] = end_identity
    report["phase_checks_passed"] = len(report["cases"]) == len(PROFILE_CASES)
    if not report["phase_checks_passed"]:
        raise ValueError("V4 profile phase did not complete both serialized profile cases")
    write_report(report_path, report)
    report["report_path"] = str(report_path.resolve())
    report["report_sha256"] = sha256_file(report_path)
    return report


def capture_arguments(camera: str, pitch: str, markers: bool, output: Path,
                      *, frame_count: int = FRAME_COUNT) -> list[str]:
    if camera not in ("source", "branch"):
        raise ValueError("capture camera must be source or branch")
    if pitch not in ("-0.72", "-1.55"):
        raise ValueError("capture pitch must be the approved source-oblique or source-topdown angle")
    if frame_count <= 0 or frame_count > FRAME_COUNT:
        raise ValueError("capture frame count must be positive and no longer than the V4 horizon")
    if camera == "branch" and pitch != "-0.72":
        raise ValueError("branch view only supports the approved oblique angle")
    args = v2.arguments(512) + [
        "--fluid25d-dye-pulse-start-seconds", str(PULSE_START_SECONDS),
        "--fluid25d-dye-pulse-duration-seconds", str(PULSE_DURATION_SECONDS),
        "--frames", str(frame_count), "--capture", "video", "--fps", str(FPS),
        "--fluid25d-catchment-view", "transport-inspection",
        "--fluid25d-hillside-camera", camera,
        "--fluid25d-natural-flow-home-pitch-radians", pitch,
    ]
    if markers:
        args.append("--fluid25d-motion-markers")
    args += ["--output", str(output.resolve())]
    return args


def _video_filter(title: str, *, physical_horizon: int, markers: bool,
                  smoke: bool = False) -> str:
    lines = [
        f"Hillside motion V4 | {title}",
        f"{EXPECTED_WIDTH}x{EXPECTED_HEIGHT} | 30 fps | 60x playback",
        "Water source: 100 m3/s continuous | conserved dye: physical 60-62 min",
        PALETTE_LEGEND,
        ("Markers follow depth-averaged velocity | render-only overlay" if markers else
         "Motion markers OFF | matched overlay control"),
        r"Physical time %{eif\:n*2+2\:d} s / " + str(physical_horizon) + " s",
    ]
    if smoke:
        lines.insert(1, "SMOKE QA ONLY | short capture; does not satisfy the full-horizon gate")
    return ",".join(
        _drawtext_clause(line, 12 + 28 * index,
                         expansion="none" if line == PALETTE_LEGEND else "normal")
        for index, line in enumerate(lines)
    )


def _escape_drawtext_text(text: str) -> str:
    # Preserve the two escaped colons needed by %{eif:...}; escape other
    # filter-option separators inside arbitrary caption text.
    expression_colon = "\x00eif-colon\x00"
    text = text.replace(r"\:", expression_colon)
    text = text.replace("\\", "\\\\").replace(":", r"\:").replace(",", r"\,")
    text = text.replace("'", r"\'")
    return text.replace(expression_colon, r"\:")


def _drawtext_clause(text: str, y: int, *, expansion: str = "normal") -> str:
    return (f"drawtext=text='{_escape_drawtext_text(text)}':x=12:y={y}:fontsize=17:"
            f"expansion={expansion}:fontcolor=white:box=1:boxcolor=black@0.82:boxborderw=5")


def _run_command(command: list[str], log_path: Path) -> dict[str, Any]:
    ensure_absent([log_path])
    started = time.perf_counter()
    launch_error = None
    try:
        with log_path.open("w") as stream:
            child = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
        exit_code = child.returncode
    except OSError as error:
        launch_error = f"{type(error).__name__}: {error}"
        log_path.write_text(launch_error + "\n")
        exit_code = None
    return {
        "command": command,
        "exit_code": exit_code,
        "launch_error": launch_error,
        "wall_seconds": time.perf_counter() - started,
        "log_path": str(log_path.resolve()),
        "log_sha256": sha256_file(log_path),
    }


def probe_video(path: Path, log_path: Path, expected_frames: int) -> dict[str, Any]:
    if expected_frames <= 0:
        raise ValueError("video probe requires a positive expected frame count")
    receipt_path = log_path.with_suffix(".json")
    command = [
        "rtk", "proxy", "ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
        "-show_entries", "stream=codec_name,width,height,r_frame_rate,avg_frame_rate,duration,nb_read_frames",
        "-show_entries", "frame=pts_time",
        "-show_entries", "format=duration", "-of", "json", str(path.resolve()),
    ]
    ensure_absent([log_path, receipt_path])
    started = time.perf_counter()
    launch_error = None
    try:
        child = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        log_path.write_text(child.stdout + ("\n" + child.stderr if child.stderr else ""))
        exit_code = child.returncode
        payload = json.loads(child.stdout) if child.returncode == 0 else None
    except (OSError, json.JSONDecodeError) as error:
        launch_error = f"{type(error).__name__}: {error}"
        log_path.write_text(launch_error + "\n")
        exit_code = None
        payload = None
    result: dict[str, Any] = {
        "command": command,
        "exit_code": exit_code,
        "launch_error": launch_error,
        "wall_seconds": time.perf_counter() - started,
        "log_path": str(log_path.resolve()),
        "log_sha256": sha256_file(log_path),
        "path": str(path.resolve()),
        "file_sha256": sha256_file(path) if path.is_file() else None,
        "expected_frames": expected_frames,
        "metadata": payload,
    }
    def fail(message: str) -> None:
        result["validation_error"] = message
        write_report(receipt_path, result)
        raise ValueError(message)

    if exit_code != 0 or not isinstance(payload, dict):
        fail(f"ffprobe failed for {path}")
    streams = payload.get("streams", [])
    if len(streams) != 1:
        fail(f"video must have exactly one selected video stream: {path}")
    stream = streams[0]
    try:
        frames = int(stream["nb_read_frames"])
        rate = Fraction(stream["r_frame_rate"])
        average_rate = Fraction(stream["avg_frame_rate"])
        duration = float(stream["duration"])
        width, height = int(stream["width"]), int(stream["height"])
        timestamps = [float(frame["pts_time"]) for frame in payload["frames"]]
    except (KeyError, TypeError, ValueError, ZeroDivisionError) as error:
        fail(f"ffprobe returned incomplete video metadata for {path}: {error}")
    expected_duration = expected_frames / FPS
    if (frames != expected_frames or width != EXPECTED_WIDTH or height != EXPECTED_HEIGHT or
            not math.isfinite(duration) or any(not math.isfinite(value) for value in timestamps) or
            rate != FPS or abs(duration - expected_duration) > 1 / FPS + 1e-6 or
            len(timestamps) != expected_frames):
        fail(f"video has invalid dimensions, cadence, duration, or frame count: {path}")
    maximum_timestamp_error = max(abs(value - index / FPS)
                                  for index, value in enumerate(timestamps))
    if not math.isfinite(maximum_timestamp_error) or maximum_timestamp_error > 1e-6:
        fail(f"video has invalid fixed-cadence frame timestamps: {path}")
    result["validated"] = True
    result["frame_count"] = frames
    result["fps"] = float(rate)
    result["container_average_fps"] = float(average_rate)
    result["maximum_timestamp_error_seconds"] = maximum_timestamp_error
    result["cadence_interpretation"] = (
        "Every frame timestamp matches n/30 within 1 microsecond. The raw encoder "
        "may omit the final packet duration; container average fps is not the cadence gate."
    )
    result["duration_seconds"] = duration
    result["dimensions"] = [width, height]
    write_report(receipt_path, result)
    result["receipt_path"] = str(receipt_path.resolve())
    result["receipt_sha256"] = sha256_file(receipt_path)
    return result


def _encode_labelled_video(source: Path, target: Path, log_path: Path,
                           title: str, frame_count: int, *, markers: bool,
                           smoke: bool = False) -> dict[str, Any]:
    ensure_absent([target])
    command = [
        "rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-i", str(source.resolve()),
        "-vf", _video_filter(title, physical_horizon=HORIZON_SECONDS,
                             markers=markers, smoke=smoke),
        "-map_metadata", "-1", "-metadata", "creation_time=1970-01-01T00:00:00Z",
        "-fflags", "+bitexact", "-flags:v", "+bitexact", "-threads", "1",
        "-c:v", "libx264", "-crf", "20", "-pix_fmt", "yuv420p", "-an",
        str(target.resolve()),
    ]
    result = _run_command(command, log_path)
    result["path"] = str(target.resolve())
    result["sha256"] = sha256_file(target) if target.is_file() else None
    if result["exit_code"] != 0 or result["sha256"] is None:
        result["error"] = f"video labelling failed for {source}"
        return result
    try:
        result["probe"] = probe_video(
            target, log_path.with_name(log_path.stem + ".ffprobe.log"), frame_count)
    except Exception as error:
        result["probe_error"] = f"{type(error).__name__}: {error}"
        receipt_path = log_path.with_name(log_path.stem + ".ffprobe.json")
        if receipt_path.is_file():
            result["probe_failure_receipt_path"] = str(receipt_path.resolve())
            result["probe_failure_receipt_sha256"] = sha256_file(receipt_path)
    return result


def _make_excerpt(source: Path, target: Path, log_path: Path, label: str,
                  physical_start: int, physical_end: int) -> dict[str, Any]:
    if physical_start < 0 or physical_end <= physical_start or physical_end > HORIZON_SECONDS:
        raise ValueError("excerpt window is outside the fixed physical horizon")
    if physical_start % v2.DT or physical_end % v2.DT:
        raise ValueError("excerpt boundaries must align with the fixed 2-second capture cadence")
    start_frame = physical_start // v2.DT
    end_frame = physical_end // v2.DT
    frame_count = end_frame - start_frame
    title = f"Excerpt: {label} | physical window {physical_start // 60:02d}-{physical_end // 60:02d} min"
    # The full-video physical-time overlay is already burned in. Trim by the
    # fixed-step frame index so each excerpt is an exact 30 fps sample window.
    filter_text = (
        f"trim=start_frame={start_frame}:end_frame={end_frame},setpts=PTS-STARTPTS,"
        f"{_drawtext_clause(title, 660)}"
    )
    ensure_absent([target])
    command = [
        "rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-i", str(source.resolve()),
        "-vf", filter_text, "-frames:v", str(frame_count),
        "-map_metadata", "-1", "-metadata", "creation_time=1970-01-01T00:00:00Z",
        "-fflags", "+bitexact", "-flags:v", "+bitexact", "-threads", "1",
        "-c:v", "libx264", "-crf", "20", "-pix_fmt", "yuv420p", "-an",
        str(target.resolve()),
    ]
    result = _run_command(command, log_path)
    result.update({
        "label": label,
        "path": str(target.resolve()),
        "sha256": sha256_file(target) if target.is_file() else None,
        "physical_window_seconds": [physical_start, physical_end],
        "physical_window_label": title,
        "source_frame_range_half_open": [start_frame, end_frame],
        "expected_frames": frame_count,
    })
    if result["exit_code"] != 0 or result["sha256"] is None:
        result["error"] = f"excerpt generation failed for {label}"
        return result
    try:
        result["probe"] = probe_video(
            target, log_path.with_name(log_path.stem + ".ffprobe.log"), frame_count)
    except Exception as error:
        result["probe_error"] = f"{type(error).__name__}: {error}"
        receipt_path = log_path.with_name(log_path.stem + ".ffprobe.json")
        if receipt_path.is_file():
            result["probe_failure_receipt_path"] = str(receipt_path.resolve())
            result["probe_failure_receipt_sha256"] = sha256_file(receipt_path)
    return result


def _load_passed_profiles(out: Path, app: Path) -> tuple[dict[str, Any], Path, str]:
    report_path = out.resolve() / "profiles" / "profiles.json"
    revalidated = report_path.with_name("revalidated-profiles.json")
    if revalidated.is_file():
        report_path = revalidated
    if not report_path.is_file():
        raise FileNotFoundError(f"missing passing V4 profiles report: {report_path}")
    report = read_report(report_path)
    if report_path == revalidated:
        previous = report.get("retained_rejected_phase", {})
        original_path = revalidated.with_name("profiles.json")
        if (previous.get("path") != str(original_path.resolve()) or not original_path.is_file() or
                previous.get("sha256") != sha256_file(original_path)):
            raise ValueError("revalidated profile report lost its retained rejected-phase provenance")
    identity = _execution_identity(app.resolve(), 512)
    if (report.get("schema") != "cubey.fluid25d.hillside_motion_v4.profiles" or
            report.get("phase_checks_passed") is not True or
            report.get("app_path") != str(app.resolve()) or
            report.get("end_input_identity") != identity or
            len(report.get("cases", [])) != len(PROFILE_CASES)):
        raise ValueError("V4 captures require a passing full-horizon profile from the same app/input identity")
    baselines = load_v3_baselines()
    for record, expected in zip(report["cases"], PROFILE_CASES, strict=True):
        label, domain, dye = expected
        case_dir = out.resolve() / "profiles" / label
        metrics = case_dir / "profile.metrics.csv"
        receipt_path = case_dir / "execution.json"
        if (record.get("label") != label or record.get("domain") != domain or
                record.get("dye") is not dye or record.get("motion_markers") is not True or
                record.get("v3_full_profile_comparison", {}).get("passed") is not True or
                Path(record.get("profile_metrics_path", "")).resolve() != metrics.resolve() or
                Path(record.get("execution_receipt_path", "")).resolve() != receipt_path.resolve() or
                not metrics.is_file() or sha256_file(metrics) != record.get("profile_metrics_sha256") or
                not receipt_path.is_file() or
                sha256_file(receipt_path) != record.get("execution_receipt_sha256")):
            raise ValueError("V4 profile evidence artifact or comparison changed before captures")
        receipt = read_report(receipt_path)
        input_identity = _execution_identity(app.resolve(), domain)
        artifacts = receipt.get("artifact_hashes", {})
        artifact_paths = receipt.get("artifact_paths", {})
        expected_command = [
            "rtk", "proxy", str(app.resolve()),
            *profile_arguments(domain, dye, case_dir / "profile", case_dir / "final.png"),
        ]
        if (receipt.get("schema") != "cubey.fluid25d.hillside_motion_v4.execution" or
                receipt.get("label") != label or receipt.get("domain") != domain or
                receipt.get("frames") != FRAME_COUNT or receipt.get("interval") != 1 or
                receipt.get("dye") is not dye or receipt.get("motion_markers") is not True or
                receipt.get("exit_code") != 0 or receipt.get("command") != expected_command or
                receipt.get("before") != input_identity or receipt.get("after") != input_identity or
                record.get("execution_before") != input_identity or
                record.get("execution_after") != input_identity):
            raise ValueError("V4 profile execution receipt does not match its frozen protocol and identity")
        if not artifacts or set(artifacts) != set(artifact_paths):
            raise ValueError("V4 profile execution receipt has an incomplete artifact hash/path map")
        for name, digest in artifacts.items():
            artifact_path = Path(artifact_paths[name])
            if (not artifact_path.is_file() or sha256_file(artifact_path) != digest or
                    artifact_path.parent.resolve() != case_dir.resolve()):
                raise ValueError(f"V4 profile artifact changed before capture: {artifact_path}")
        current_metrics = v3.read_profile(metrics)
        baseline_metrics = (baselines["metrics_256"] if domain == 256
                            else baselines["metrics_512_dye"])
        comparison = compare_full_common_profiles(baseline_metrics, current_metrics)
        if comparison != record.get("v3_full_profile_comparison"):
            raise ValueError("V4 exact full-profile comparison changed or was incompletely recorded")
        water_budget = validate_water_budget_profile(current_metrics)
        if water_budget != record.get("water_budget"):
            raise ValueError("V4 all-frame water-budget validation changed or was incompletely recorded")
        if domain == 256:
            summary = v2.summarize(v2.read_metrics(metrics))
            if (summary.get("numeric_profile_checks_passed") is not True or
                    summary.get("physical_horizon_seconds") != HORIZON_SECONDS or
                    summary.get("nonzero_solver_flag_frames") != [] or
                    summary.get("unhealthy_frames") != []):
                raise ValueError("V4 256 profile no longer passes its full-horizon gate")
        else:
            summary = validate_dye_without_marker_metrics(current_metrics, baselines["metrics_512_water"])
            if summary.get("passed") is not True:
                raise ValueError("V4 512 profile no longer passes its conserved-tracer gate")
    if (report.get("baseline_evidence", {}).get("review_sha256") != baselines["review_sha256"] or
            report.get("baseline_evidence", {}).get("metrics_256_sha256") != baselines["metrics_256_sha256"] or
            report.get("baseline_evidence", {}).get("metrics_512_dye_sha256") !=
            baselines["metrics_512_dye_sha256"]):
        raise ValueError("V3 baseline evidence changed since the V4 full-horizon comparison")
    return report, report_path, sha256_file(report_path)


def _capture_one(app: Path, case_dir: Path, label: str, camera: str, pitch: str,
                 markers: bool, *, frame_count: int) -> dict[str, Any]:
    raw = case_dir / f"{label}.mp4"
    app_log = case_dir / f"{label}.app.log"
    labelled = case_dir / f"{label}-labelled.mp4"
    label_log = case_dir / f"{label}.label.log"
    app_command = ["rtk", "proxy", str(app.resolve()),
                   *capture_arguments(camera, pitch, markers, raw, frame_count=frame_count)]
    before = _execution_identity(app.resolve(), 512)
    run = _run_command(app_command, app_log)
    after = _execution_identity(app.resolve(), 512)
    case = {
        "label": label,
        "camera": camera,
        "pitch_radians": pitch,
        "markers": markers,
        "frame_count": frame_count,
        "physical_horizon_seconds": frame_count * v2.DT,
        "playback_acceleration": v2.DT * FPS,
        "water_source_m3_per_s": v2.Q_M3_PER_S,
        "dye_pulse_seconds": [PULSE_START_SECONDS, PULSE_START_SECONDS + PULSE_DURATION_SECONDS],
        "command": app_command,
        "exit_code": run["exit_code"],
        "launch_error": run["launch_error"],
        "wall_seconds": run["wall_seconds"],
        "log_path": run["log_path"],
        "log_sha256": run["log_sha256"],
        "before": before,
        "after": after,
        "raw_video_path": str(raw.resolve()),
        "raw_video_sha256": sha256_file(raw) if raw.is_file() else None,
    }
    write_report(case_dir / "execution.json", case)
    if case["exit_code"] != 0 or case["raw_video_sha256"] is None:
        raise RuntimeError(f"{label} capture child failed or omitted its video")
    if before != after:
        raise ValueError(f"{label} capture changed app/shader/source/frozen-input identity")
    try:
        case["raw_video_probe"] = probe_video(
            raw, case_dir / f"{label}.raw.ffprobe.log", frame_count)
    except Exception as error:
        case["raw_video_probe_error"] = f"{type(error).__name__}: {error}"
        probe_receipt = case_dir / f"{label}.raw.ffprobe.json"
        if probe_receipt.is_file():
            case["raw_video_probe_failure_receipt_path"] = str(probe_receipt.resolve())
            case["raw_video_probe_failure_receipt_sha256"] = sha256_file(probe_receipt)
        write_report(case_dir / "execution.json", case)
        raise
    display_name = dict((name, title) for name, _, _, _, title in VIDEO_CASES)[label]
    case["labelled_video"] = _encode_labelled_video(
        raw, labelled, label_log, display_name, frame_count, markers=markers,
        smoke=frame_count != FRAME_COUNT)
    case["labelled_video_path"] = str(labelled.resolve())
    case["labelled_video_sha256"] = case["labelled_video"]["sha256"]
    case["palette_legend"] = PALETTE_LEGEND
    case["motion_marker_interpretation"] = "depth-averaged velocity direction and magnitude; render-only"
    write_report(case_dir / "execution.json", case)
    if (case["labelled_video"].get("exit_code") != 0 or
            not case["labelled_video"].get("sha256") or
            case["labelled_video"].get("probe", {}).get("validated") is not True):
        raise RuntimeError(f"{label} labelled video failed encoding or metadata validation")
    case["excerpts"] = []
    if frame_count == FRAME_COUNT:
        for excerpt_label, start, end in EXCERPT_SPECS:
            excerpt = _make_excerpt(
                labelled,
                case_dir / f"{label}-{excerpt_label}.mp4",
                case_dir / f"{label}-{excerpt_label}.ffmpeg.log",
                excerpt_label, start, end,
            )
            case["excerpts"].append(excerpt)
            write_report(case_dir / "execution.json", case)
            if (excerpt.get("exit_code") != 0 or not excerpt.get("sha256") or
                    excerpt.get("probe", {}).get("validated") is not True):
                raise RuntimeError(f"{label} excerpt failed encoding or metadata validation: "
                                   f"{excerpt_label}")
    receipt_path = case_dir / "execution.json"
    case["execution_receipt_path"] = str(receipt_path.resolve())
    case["execution_receipt_sha256"] = sha256_file(receipt_path)
    return case


def _capture_cases(source_only: bool) -> tuple[tuple[str, str, str, bool, str], ...]:
    if source_only:
        return VIDEO_CASES[:2]
    return VIDEO_CASES


def _require_hashed_file(path_text: str, expected_sha256: str, label: str) -> None:
    path = Path(path_text)
    if not path.is_file() or sha256_file(path) != expected_sha256:
        raise ValueError(f"{label} is missing or changed: {path}")


def _validate_probe_record(record: dict[str, Any], label: str) -> None:
    if record.get("validated") is not True or record.get("exit_code") != 0:
        raise ValueError(f"{label} lacks successful media metadata validation")
    _require_hashed_file(record.get("log_path", ""), record.get("log_sha256", ""),
                         f"{label} ffprobe log")
    _require_hashed_file(record.get("receipt_path", ""), record.get("receipt_sha256", ""),
                         f"{label} ffprobe receipt")


def _validate_capture_case(case: dict[str, Any], source_only: bool) -> None:
    label = case.get("label")
    expected = {item[0]: item for item in _capture_cases(source_only)}.get(label)
    if expected is None:
        raise ValueError(f"capture phase contains an unexpected camera case: {label}")
    name, camera, pitch, markers, title = expected
    frames = case.get("frame_count")
    if (case.get("camera") != camera or case.get("pitch_radians") != pitch or
            case.get("markers") is not markers or frames != FRAME_COUNT or
            case.get("exit_code") != 0 or case.get("before") != case.get("after") or
            case.get("palette_legend") != PALETTE_LEGEND or
            case.get("labelled_video", {}).get("exit_code") != 0 or
            case.get("labelled_video", {}).get("probe", {}).get("validated") is not True or
            case.get("raw_video_probe", {}).get("validated") is not True):
        raise ValueError(f"capture case does not match its frozen full-horizon protocol: {name}")
    app_command = case.get("command", [])
    if ("--fluid25d-hillside-source-context" in app_command or
            app_command[app_command.index("--fluid25d-hillside-camera") + 1] != camera or
            app_command[app_command.index("--fluid25d-natural-flow-home-pitch-radians") + 1] != pitch or
            ("--fluid25d-motion-markers" in app_command) is not markers or
            app_command[app_command.index("--output") + 1] != case.get("raw_video_path")):
        raise ValueError(f"capture command does not match its camera/marker receipt: {name}")
    if (not isinstance(case.get("raw_video_sha256"), str) or
            case["labelled_video"].get("sha256") != case.get("labelled_video_sha256")):
        raise ValueError(f"capture case is missing stable raw/labelled video hashes: {name}")
    expected_label_line = ("Markers follow depth-averaged velocity | render-only overlay" if markers else
                           "Motion markers OFF | matched overlay control")
    filter_text = case["labelled_video"].get("command", [])[case["labelled_video"].get("command", []).index("-vf") + 1]
    if (title not in filter_text or _escape_drawtext_text(PALETTE_LEGEND) not in filter_text or
            expected_label_line not in filter_text or "n*2+2" not in filter_text):
        raise ValueError(f"labelled video omits its camera, palette, or physical-time labels: {name}")
    _require_hashed_file(case.get("log_path", ""), case.get("log_sha256", ""),
                         f"{name} app log")
    _require_hashed_file(case["labelled_video"].get("log_path", ""),
                         case["labelled_video"].get("log_sha256", ""),
                         f"{name} label log")
    _validate_probe_record(case["raw_video_probe"], f"{name} raw video")
    _validate_probe_record(case["labelled_video"]["probe"], f"{name} labelled video")
    _require_hashed_file(case.get("execution_receipt_path", ""),
                         case.get("execution_receipt_sha256", ""),
                         f"{name} execution receipt")
    execution_receipt = read_report(Path(case["execution_receipt_path"]))
    if (execution_receipt.get("command") != app_command or
            execution_receipt.get("exit_code") != 0 or
            execution_receipt.get("raw_video_sha256") != case.get("raw_video_sha256") or
            execution_receipt.get("before") != case.get("before") or
            execution_receipt.get("after") != case.get("after")):
        raise ValueError(f"{name} execution receipt diverges from the capture report")
    excerpts = case.get("excerpts", [])
    if len(excerpts) != len(EXCERPT_SPECS):
        raise ValueError(f"capture case is missing one or more required physical-time excerpts: {name}")
    for record, (expected_label, start, end) in zip(excerpts, EXCERPT_SPECS, strict=True):
        if (record.get("label") != expected_label or
                record.get("physical_window_seconds") != [start, end] or
                record.get("expected_frames") != (end - start) // PLAYBACK_ACCELERATION * FPS or
                record.get("source_frame_range_half_open") !=
                [start // v2.DT, end // v2.DT] or
                record.get("physical_window_label") !=
                f"Excerpt: {expected_label} | physical window {start // 60:02d}-{end // 60:02d} min" or
                record.get("probe", {}).get("validated") is not True or
                record.get("exit_code") != 0 or not record.get("sha256")):
            raise ValueError(f"capture excerpt has invalid physical-time mapping: {name}/{expected_label}")
        _require_hashed_file(record.get("log_path", ""), record.get("log_sha256", ""),
                             f"{name}/{expected_label} ffmpeg log")
        excerpt_command = record.get("command", [])
        if ("-vf" not in excerpt_command or
                _escape_drawtext_text(record["physical_window_label"]) not in
                excerpt_command[excerpt_command.index("-vf") + 1]):
            raise ValueError(f"{name}/{expected_label} excerpt lacks its physical-clock title")
        _validate_probe_record(record["probe"], f"{name}/{expected_label} excerpt")
        _require_hashed_file(record.get("path", ""), record.get("sha256", ""),
                             f"{name}/{expected_label} video")


def _validate_matched_overlay_control(cases: list[dict[str, Any]]) -> None:
    by_label = {case.get("label"): case for case in cases}
    if "source-markers" not in by_label or "source-markers-off" not in by_label:
        raise ValueError("capture set lacks the source marker-on/off matched-camera pair")
    with_markers = list(by_label["source-markers"]["command"])
    without_markers = list(by_label["source-markers-off"]["command"])
    if (with_markers.count("--fluid25d-motion-markers") != 1 or
            "--fluid25d-motion-markers" in without_markers):
        raise ValueError("source comparison does not have one marker-on and one marker-off command")
    for command, case in ((with_markers, by_label["source-markers"]),
                          (without_markers, by_label["source-markers-off"])):
        output_indices = [index for index, arg in enumerate(command) if arg == "--output"]
        if (len(output_indices) != 1 or output_indices[0] + 1 >= len(command) or
                command[output_indices[0] + 1] != case.get("raw_video_path")):
            raise ValueError("source marker comparison command output differs from its receipt")
        if (command[command.index("--fluid25d-hillside-camera") + 1] != "source" or
                command[command.index("--fluid25d-natural-flow-home-pitch-radians") + 1] != "-0.72"):
            raise ValueError("source marker comparison does not use one matched oblique camera")
    with_markers.remove("--fluid25d-motion-markers")
    with_markers[with_markers.index("--output") + 1] = "<separate-output>"
    without_markers[without_markers.index("--output") + 1] = "<separate-output>"
    if with_markers != without_markers:
        raise ValueError("source marker-on/off captures differ by more than marker flag and output path")
    if (by_label["source-markers"]["before"] != by_label["source-markers-off"]["before"] or
            by_label["source-markers"]["after"] != by_label["source-markers-off"]["after"]):
        raise ValueError("source marker-on/off captures did not use the same app/input identity")


def smoke_phase(app: Path, out: Path) -> dict[str, Any]:
    app = app.resolve()
    start_identity = _execution_identity(app, 512)
    phase_dir = create_phase_directory(out, "smoke")
    report_path = phase_dir / "smoke.json"
    ensure_absent([report_path])
    report: dict[str, Any] = {
        "schema": "cubey.fluid25d.hillside_motion_v4.smoke",
        "phase": "smoke",
        "phase_checks_passed": False,
        "full_horizon_gate_satisfied": False,
        "frames": SMOKE_FRAME_COUNT,
        "physical_seconds": SMOKE_FRAME_COUNT * v2.DT,
        "purpose": "annotation/media/marker render QA only; not a numerical or full-horizon gate",
        "palette_rendered_dye_not_validated": True,
        "start_input_identity": start_identity,
        "cases": [],
    }
    write_report(report_path, report)
    for label, markers in (("source-markers", True), ("source-markers-off", False)):
        case_dir = phase_dir / label
        case_dir.mkdir()
        try:
            run_case = _capture_one(app, case_dir, label, "source", "-0.72", markers,
                                    frame_count=SMOKE_FRAME_COUNT)
        except Exception as error:
            report["failed_case"] = failed_case_record(label, case_dir, error)
            write_report(report_path, report)
            raise
        if run_case["before"] != start_identity or run_case["after"] != start_identity:
            raise ValueError("smoke child did not preserve its app/shader/source/frozen-input identity")
        report["cases"].append(run_case)
        write_report(report_path, report)
    end_identity = _execution_identity(app, 512)
    if end_identity != start_identity:
        raise ValueError("app/shader/source/frozen-input identity changed during smoke captures")
    if len(report["cases"]) != 2 or any(
            item.get("raw_video_probe", {}).get("validated") is not True or
            item.get("labelled_video", {}).get("probe", {}).get("validated") is not True
            for item in report["cases"]):
        raise ValueError("smoke media did not pass exact short-capture metadata validation")
    _validate_matched_overlay_control(report["cases"])
    report["end_input_identity"] = end_identity
    report["phase_checks_passed"] = True
    report["full_horizon_gate_satisfied"] = False
    write_report(report_path, report)
    report["report_path"] = str(report_path.resolve())
    report["report_sha256"] = sha256_file(report_path)
    return report


def captures_phase(app: Path, out: Path, *, source_only: bool = False) -> dict[str, Any]:
    app = app.resolve()
    profiles, profiles_path, profiles_sha256 = _load_passed_profiles(out, app)
    start_identity = _execution_identity(app, 512)
    if start_identity != profiles.get("end_input_identity"):
        raise ValueError("app/shader/source/frozen-input identity changed after V4 profiles")
    phase_dir = create_phase_directory(out, "captures")
    report_path = phase_dir / "captures.json"
    ensure_absent([report_path])
    report: dict[str, Any] = {
        "schema": "cubey.fluid25d.hillside_motion_v4.captures",
        "phase": "captures",
        "phase_checks_passed": False,
        "full_horizon_gate_satisfied": True,
        "source_only_fallback": source_only,
        "source_only_interpretation": (
            "required source marker-on/off full-video pair and three excerpts captured; "
            "branch/topdown optional camera captures omitted"
            if source_only else "all source, branch, and source-topdown camera cases required"
        ),
        "profiles_report_path": str(profiles_path.resolve()),
        "profiles_report_sha256": profiles_sha256,
        "profiles_app_sha256": profiles["app_sha256"],
        "start_input_identity": start_identity,
        "full_horizon_seconds": HORIZON_SECONDS,
        "frame_count": FRAME_COUNT,
        "fps": FPS,
        "playback_acceleration": PLAYBACK_ACCELERATION,
        "physical_time_convention": "frame n shows the state after (n+1)*2 simulated seconds",
        "source_rate_m3_per_s": v2.Q_M3_PER_S,
        "dye_pulse_physical_seconds": [PULSE_START_SECONDS,
                                         PULSE_START_SECONDS + PULSE_DURATION_SECONDS],
        "palette_legend": PALETTE_LEGEND,
        "excerpt_windows": [
            {"label": label, "physical_seconds": [start, end],
             "video_seconds": [start / PLAYBACK_ACCELERATION, end / PLAYBACK_ACCELERATION],
             "frame_count": (end - start) // PLAYBACK_ACCELERATION * FPS}
            for label, start, end in EXCERPT_SPECS
        ],
        "cases": [],
    }
    write_report(report_path, report)
    for label, camera, pitch, markers, _ in _capture_cases(source_only):
        case_dir = phase_dir / label
        case_dir.mkdir()
        try:
            case = _capture_one(app, case_dir, label, camera, pitch, markers,
                                frame_count=FRAME_COUNT)
            if case["before"] != start_identity or case["after"] != start_identity:
                raise ValueError(f"{label} used a changed app/shader/source/frozen-input identity")
            _validate_capture_case(case, source_only)
        except Exception as error:
            report["failed_case"] = failed_case_record(label, case_dir, error)
            write_report(report_path, report)
            raise
        report["cases"].append(case)
        write_report(report_path, report)
    _validate_matched_overlay_control(report["cases"])
    end_identity = _execution_identity(app, 512)
    if end_identity != start_identity:
        raise ValueError("app/shader/source/frozen-input identity changed during V4 captures")
    if sha256_file(profiles_path) != profiles_sha256:
        raise ValueError("passing full-horizon profile report changed during V4 captures")
    report["end_input_identity"] = end_identity
    report["phase_checks_passed"] = len(report["cases"]) == len(_capture_cases(source_only))
    if not report["phase_checks_passed"]:
        raise ValueError("V4 capture phase did not complete all required camera cases")
    write_report(report_path, report)
    report["report_path"] = str(report_path.resolve())
    report["report_sha256"] = sha256_file(report_path)
    return report


def review_phase(app: Path, out: Path) -> dict[str, Any]:
    app = app.resolve()
    profiles, profiles_path, profiles_sha256 = _load_passed_profiles(out, app)
    captures_path = out.resolve() / "captures" / "captures.json"
    if not captures_path.is_file():
        raise FileNotFoundError(f"missing V4 captures report: {captures_path}")
    captures = read_report(captures_path)
    captures_sha256 = sha256_file(captures_path)
    identity = _execution_identity(app, 512)
    source_only = captures.get("source_only_fallback") is True
    if (captures.get("schema") != "cubey.fluid25d.hillside_motion_v4.captures" or
            captures.get("phase_checks_passed") is not True or
            captures.get("profiles_report_path") != str(profiles_path.resolve()) or
            captures.get("profiles_report_sha256") != profiles_sha256 or
            captures.get("start_input_identity") != identity or
            captures.get("end_input_identity") != identity):
        raise ValueError("V4 review requires complete captures matched to the passing profile receipt")
    expected_cases = _capture_cases(source_only)
    if len(captures.get("cases", [])) != len(expected_cases):
        raise ValueError("V4 capture review has an incomplete or unexpected camera set")
    for case, expected in zip(captures["cases"], expected_cases, strict=True):
        _validate_capture_case(case, source_only)
        if case.get("label") != expected[0] or case.get("before") != identity:
            raise ValueError("V4 capture review contains an unexpected or stale case identity")
        case_dir = Path(case["raw_video_path"]).parent
        for path_key, hash_key in (("raw_video_path", "raw_video_sha256"),
                                   ("labelled_video_path", "labelled_video_sha256")):
            artifact = Path(case[path_key])
            if not artifact.is_file() or sha256_file(artifact) != case.get(hash_key):
                raise ValueError(f"V4 capture artifact changed since its receipt: {artifact}")
        for excerpt in case["excerpts"]:
            artifact = Path(excerpt["path"])
            if not artifact.is_file() or sha256_file(artifact) != excerpt.get("sha256"):
                raise ValueError(f"V4 excerpt changed since its receipt: {artifact}")
        if case_dir.parent.name != "captures":
            raise ValueError("V4 capture artifacts are outside this output's captures phase")
    _validate_matched_overlay_control(captures["cases"])
    if sha256_file(profiles_path) != profiles_sha256:
        raise ValueError("V4 profiles report changed while final review was assembled")
    phase_dir = create_phase_directory(out, "review")
    report_path = phase_dir / "review.json"
    ensure_absent([report_path])
    report = {
        "schema": "cubey.fluid25d.hillside_motion_v4.review",
        "phase": "review",
        "phase_checks_passed": True,
        "full_horizon_gate_satisfied": True,
        "full_horizon_profiles_passed": profiles.get("phase_checks_passed") is True,
        "captures_passed": captures.get("phase_checks_passed") is True,
        "source_only_fallback": source_only,
        "app_path": str(app),
        "app_sha256": identity["app_sha256"],
        "shader_map_sha256": identity["shader_map_sha256"],
        "solver_bridge": profiles.get("solver_bridge"),
        "historical_strict_boundary": profiles.get("historical_strict_boundary"),
        "profiles_report": {"path": str(profiles_path.resolve()), "sha256": profiles_sha256},
        "captures_report": {"path": str(captures_path.resolve()), "sha256": captures_sha256},
        "profile_cases": [case["label"] for case in profiles["cases"]],
        "capture_cases": [case["label"] for case in captures["cases"]],
        "physical_time_acceleration": PLAYBACK_ACCELERATION,
        "palette_legend": PALETTE_LEGEND,
        "strict_rerun_claimed": False,
        "strict_evidence_interpretation": (
            "V3 strict results remain historical retained evidence bridged by unchanged solver "
            "SPIR-V/CPU-source hashes and exact full profiles; no V4 strict rerun is claimed"
        ),
    }
    write_report(report_path, report)
    report["report_path"] = str(report_path.resolve())
    report["report_sha256"] = sha256_file(report_path)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("smoke", "profiles", "captures", "review"), required=True)
    parser.add_argument("--app", type=Path, default=APP_DEFAULT)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--source-only", action="store_true",
                        help="capture only source marker-on/off videos; branch/topdown are optional")
    parser.add_argument("--revalidate-profiles", action="store_true",
                        help="validate retained complete profile executions without rerunning or overwriting them")
    options = parser.parse_args(argv)
    if options.source_only and options.phase != "captures":
        parser.error("--source-only is valid only with --phase captures")
    if options.revalidate_profiles and options.phase != "profiles":
        parser.error("--revalidate-profiles is valid only with --phase profiles")
    app = options.app.resolve()
    out = options.out.resolve()
    if options.phase == "smoke":
        report = smoke_phase(app, out)
    elif options.phase == "profiles":
        report = (revalidate_profiles_phase(app, out) if options.revalidate_profiles
                  else profiles_phase(app, out))
    elif options.phase == "captures":
        report = captures_phase(app, out, source_only=options.source_only)
    else:
        report = review_phase(app, out)
    print(json.dumps({
        "phase": options.phase,
        "phase_checks_passed": report.get("phase_checks_passed"),
        "full_horizon_gate_satisfied": report.get("full_horizon_gate_satisfied", False),
        "report_path": report.get("report_path"),
        "report_sha256": report.get("report_sha256"),
    }, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
