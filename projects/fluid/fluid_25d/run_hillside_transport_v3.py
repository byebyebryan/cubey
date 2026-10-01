#!/usr/bin/env python3
"""Validate matched 512x512 hillside water/dye transport and capture evidence.

This runner reuses the frozen V2 terrain, source, and numerical arguments. It
does not alter solver settings or V2 reports. The historical 256 prefix is
recorded as a comparison observation; a healthy current-app profile can pass
the V3 gate even when that old bit-exact comparison does not.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
import time
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Any

import run_hillside_sustained_flow_v2 as v2

ROOT = v2.ROOT
APP_DEFAULT = v2.DEFAULT_APP
HORIZON_SECONDS = 7200
FRAME_COUNT = HORIZON_SECONDS // v2.DT
FPS = 30
DYE_START_SECONDS = 3600
DYE_DURATION_SECONDS = 120
DYE_SOURCE_RATE_M3_PER_S = v2.Q_M3_PER_S
POSTPULSE_SECONDS = 4500
HYDRAULIC_IDENTITY = "fluid_25d.hydraulic_identity"
TRACER = "fluid_25d.tracer"
WATER = "fluid_25d.water"
SOLVER = "fluid_25d.solver"
SITE_A_RECIPE = ROOT / "projects/fluid/fluid_25d/fixtures/native-flow-study-v1/site-a.json"
SITE_A_RECIPE_SHA256 = "1ca0cbf03fefa503a4edc5b80d5a699efc1b30d615ba74bb8ca31b8da321444b"
SITE_A_MANIFEST = ROOT / "cache/terrain/sources/v1/desert-canyon-study/desert-low-relief/heightfield.json"
SITE_A_MANIFEST_SHA256 = "7f5177826084241133bd43b20a3d33fbe3880152fd0df2b6e95bb3ad01d4f4a3"
SITE_A_ELEVATION_SHA256 = "f072dd7bc0991d0de1b4ef2c6839373271e0b40e65a23f80aec9fdfd08476cd0"

TRACER_REQUIRED = {
    (SOLVER, "finite_volume_status_flags"),
    (TRACER, "amount_weighted_centroid_cell_x"),
    (TRACER, "amount_weighted_centroid_cell_y"),
    (TRACER, "conservation_residual_m3"),
    (TRACER, "cumulative_boundary_outflow_amount_m3"),
    (TRACER, "cumulative_sink_amount_m3"),
    (TRACER, "cumulative_source_amount_m3"),
    (TRACER, "dyed_wet_cell_count"),
    (TRACER, "total_tracer_amount_m3"),
}


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


def write_report(path: Path, report: dict[str, Any]) -> None:
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")


def evidence_receipt(path: Path) -> tuple[Path, Path]:
    """Return receipt and containing directory for a folder or execution.json."""
    resolved = path.resolve()
    receipt = resolved / "execution.json" if resolved.is_dir() else resolved
    if receipt.name != "execution.json" or not receipt.is_file():
        raise FileNotFoundError(f"expected an execution.json or its evidence folder: {resolved}")
    return receipt, receipt.parent


def read_profile(path: Path) -> dict[int, dict[tuple[str, str], Decimal]]:
    return v2._read_metrics(path, decimal_values=True)


def _probe_expected_command(
    app: Path,
    folder: Path,
    *,
    case: str,
    dye: bool,
    strict: bool,
) -> list[str]:
    """Exact run_probe.py command contract, including frame/profile cadence."""
    command = [
        "rtk", "proxy", str(app.resolve()), "--headless", "--width", "1280", "--height", "720",
        "--fluid25d-solver", "finite-volume", "--frames", str(FRAME_COUNT), "--capture", "png",
    ]
    if case in ("256", "512"):
        domain = int(case)
        x, z = (1280, 1536) if domain == 256 else (1152, 1408)
        command += [
            "--fluid25d-scenario", "hillside-flow-study",
            "--fluid25d-natural-flow-recipe", str(v2.RECIPES[domain].resolve()),
            "--terrain-heightfield", str(v2.MANIFEST.resolve()),
            "--fluid25d-natural-flow-source-m3-per-s", str(v2.Q_M3_PER_S),
            "--fluid25d-terrain-crop-x", str(x), "--fluid25d-terrain-crop-z", str(z),
            "--grid-width", case, "--grid-height", case,
            "--fluid25d-view", "catchment", "--fluid25d-catchment-view", "water-isolation",
            "--fluid25d-natural-flow-home-pitch-radians", "-1.55",
            "--fluid25d-render-height-scale", "1",
            "--fluid25d-terrain-palette-low-m", "770",
            "--fluid25d-terrain-palette-high-m", "2274",
        ]
        dt, substeps, cell = "2", "16", "30"
    elif case == "site-a":
        command += [
            "--fluid25d-scenario", "natural-flow-study",
            "--fluid25d-natural-flow-recipe", str(SITE_A_RECIPE.resolve()),
            "--terrain-heightfield", str(SITE_A_MANIFEST.resolve()),
            "--fluid25d-natural-flow-source-m3-per-s", "30",
            "--fluid25d-terrain-crop-x", "1556", "--fluid25d-terrain-crop-z", "1834",
            "--grid-width", "48", "--grid-height", "48",
        ]
        dt, substeps, cell = "2", "8", "30"
    else:
        raise ValueError(f"unsupported strict/profile case: {case}")
    command += [
        "--fluid25d-fixed-delta-seconds", dt, "--fluid25d-substeps", substeps,
        "--fluid25d-cell-size-m", cell,
        "--fluid25d-gravity-m-per-s2", "9.81",
        "--fluid25d-flow-damping-per-second", "0.15",
    ]
    if strict:
        command.append("--fluid25d-gpu-oracle-validation")
    if dye:
        command += [
            "--fluid25d-dye-pulse-start-seconds", str(DYE_START_SECONDS),
            "--fluid25d-dye-pulse-duration-seconds", str(DYE_DURATION_SECONDS),
        ]
    command += [
        "--profile-output", str((folder / "profile").resolve()),
        "--profile-diagnostics", "--profile-diagnostic-interval", "1",
        "--output", str((folder / "final.png").resolve()),
    ]
    return command


def _validate_receipt_artifacts(receipt: dict[str, Any], folder: Path) -> dict[str, str]:
    artifacts = receipt.get("artifact_hashes")
    if not isinstance(artifacts, dict) or not {"child.log", "profile.metrics.csv", "final.png"}.issubset(artifacts):
        raise ValueError("execution receipt is missing child.log, profile metrics, or final.png integrity")
    checked: dict[str, str] = {}
    for name, expected_hash in sorted(artifacts.items()):
        if not isinstance(name, str) or Path(name).name != name:
            raise ValueError("execution receipt contains a non-local artifact name")
        artifact = folder / name
        if not artifact.is_file() or not isinstance(expected_hash, str):
            raise ValueError(f"missing or malformed execution artifact: {name}")
        actual_hash = sha256_file(artifact)
        if actual_hash != expected_hash:
            raise ValueError(f"execution artifact hash mismatch: {name}")
        checked[name] = actual_hash
    return checked


def _expected_shader_hashes(pinned: dict[str, Any]) -> dict[str, str]:
    return {Path(name).name: digest for name, digest in pinned["shader_identity"]["files"].items()}


def _validate_probe_receipt(
    path: Path,
    app: Path,
    pinned: dict[str, Any],
    *,
    case: str,
    dye: bool,
    strict: bool,
) -> dict[str, Any]:
    receipt_path, folder = evidence_receipt(path)
    receipt = json.loads(receipt_path.read_text())
    if (receipt.get("case") != case or receipt.get("frames") != FRAME_COUNT or
            receipt.get("interval") != 1 or receipt.get("strict") is not strict or
            receipt.get("dye") is not dye or receipt.get("exit_code") != 0):
        raise ValueError(f"{case} execution receipt does not match the required successful frozen run")
    if receipt.get("command") != _probe_expected_command(app, folder, case=case, dye=dye, strict=strict):
        raise ValueError(f"{case} execution command differs from the frozen run contract")
    identity = receipt.get("before")
    if (not isinstance(identity, dict) or identity != receipt.get("after") or
            identity.get("app_sha256") != pinned["app_sha256"] or
            identity.get("shader_hashes") != _expected_shader_hashes(pinned)):
        raise ValueError(f"{case} execution app/SPIR-V identity is missing, changed, or mismatched")
    artifacts = _validate_receipt_artifacts(receipt, folder)
    if not {"profile.metrics.csv", "final.png"}.issubset(artifacts):
        raise ValueError(f"{case} execution receipt lacks profile metrics or final.png")
    child_log = (folder / "child.log").read_text(errors="replace")
    if strict:
        scenario = "natural-flow-study" if case == "site-a" else "hillside-flow-study"
        marker = f"fluid_25d_gpu_oracle: PASS solver=finite-volume scenario={scenario}"
        if marker not in child_log:
            raise ValueError(f"{case} strict child log lacks the passing finite-volume oracle marker")
    if case == "site-a":
        if (sha256_file(SITE_A_RECIPE) != SITE_A_RECIPE_SHA256 or
                sha256_file(SITE_A_MANIFEST) != SITE_A_MANIFEST_SHA256):
            raise ValueError("pinned Site A recipe or terrain manifest changed")
        manifest = json.loads(SITE_A_MANIFEST.read_text())
        elevation = (SITE_A_MANIFEST.parent / manifest["files"]["elevation"]["path"]).resolve()
        if (manifest["files"]["elevation"].get("sha256") != SITE_A_ELEVATION_SHA256 or
                sha256_file(elevation) != SITE_A_ELEVATION_SHA256):
            raise ValueError("pinned Site A elevation changed")
    return {
        "path": str(receipt_path.resolve()),
        "sha256": sha256_file(receipt_path),
        "case": case,
        "frames": FRAME_COUNT,
        "strict": strict,
        "dye": dye,
        "exit_code": 0,
        "app_sha256": identity["app_sha256"],
        "shader_hashes": identity["shader_hashes"],
        "artifacts": artifacts,
    }


def _verify_parent_prefix_gate(
    report: dict[str, Any], recomputed: dict[str, Any], prefix: dict[str, Any]
) -> dict[str, Any]:
    """Accept only healthy V2 numerics or its exact historical-prefix-only miss."""
    summary = report.get("summary", {})
    numeric = (recomputed.get("numeric_profile_checks_passed") is True and
               recomputed.get("unhealthy_frames") == [] and
               recomputed.get("physical_horizon_seconds") == HORIZON_SECONDS and
               recomputed.get("nonzero_solver_flag_frames") == [] and
               recomputed.get("sink_volume_m3") == 0)
    if not numeric or summary.get("numeric_profile_checks_passed") is not True:
        raise ValueError("256 parent has a numeric, status-flag, sink, or cadence failure")
    if (summary.get("unhealthy_frames") != [] or
            summary.get("physical_horizon_seconds") != HORIZON_SECONDS or
            summary.get("nonzero_solver_flag_frames") != [] or
            summary.get("sink_volume_m3") != 0):
        raise ValueError("256 parent stored summary disagrees with healthy recomputed numerics")
    if report.get("error") or report.get("diagnostic_only") or report.get("inherited_parent_failure"):
        raise ValueError("256 parent contains an error or diagnostic/inherited failure")
    if prefix.get("frame_count") != 900 or prefix.get("missing_required_old_metric_keys") != []:
        raise ValueError("256 historical-prefix comparison lacks its full bounded core")
    if prefix.get("metric_values_compared") != 96300:
        raise ValueError("256 historical-prefix comparison did not compare the required 96,300 values")
    if prefix.get("passed") is True:
        if report.get("phase_checks_passed") is not True:
            raise ValueError("256 parent phase failed although its required historical prefix passed")
        status = "passed"
    else:
        if (report.get("phase_checks_passed") is not False or
                not prefix.get("mismatches")):
            raise ValueError("256 parent phase failure is not the bounded historical-prefix-only case")
        status = "historical_prefix_mismatch_only"
    return {
        "status": status,
        "historical_prefix_passed": prefix["passed"],
        "metric_values_compared": prefix["metric_values_compared"],
        "missing_required_old_metric_keys": prefix["missing_required_old_metric_keys"],
        "mismatch_count_recorded": len(prefix.get("mismatches", [])),
        "numeric_profile_checks_passed_recomputed": True,
        "interpretation": (
            "historical first-900-frame prefix differs; current 256 hydraulic numerics, "
            "full cadence, forcing identity, and edge trigger are independently accepted"
            if status != "passed" else
            "historical first-900-frame prefix passed"
        ),
    }


def _parent_expected_command(app: Path, folder: Path) -> list[str]:
    return [
        "rtk", "proxy", str(app.resolve()), *v2.arguments(256),
        "--frames", str(FRAME_COUNT), "--capture", "png",
        "--fluid25d-catchment-view", "water-isolation",
        "--fluid25d-natural-flow-home-pitch-radians", "-1.55",
        "--profile-output", str((folder / "hydraulics-profile").resolve()),
        "--profile-diagnostics", "--profile-diagnostic-interval", "1",
        "--output", str((folder / "hydraulics-final-7200s.png").resolve()),
    ]


def validate_parent_evidence(path: Path, app: Path, pinned512: dict[str, Any]) -> dict[str, Any]:
    report_path = path.resolve()
    if not report_path.is_file():
        raise FileNotFoundError(f"missing --parent-evidence report: {report_path}")
    report = json.loads(report_path.read_text())
    pinned256 = v2.pinned_inputs(256, app)
    if (report.get("phase") != "hydraulics" or report.get("domain") != 256 or
            report.get("app_sha256") != pinned512["app_sha256"] or
            report.get("shader_map_sha256") != pinned512["shader_map_sha256"] or
            report.get("recipe_sha256") != v2.RECIPE_SHA256[256] or
            report.get("manifest_sha256") != v2.MANIFEST_SHA256 or
            report.get("elevation_sha256") != v2.ELEVATION_SHA256 or
            report.get("input_identity") != pinned256["input_identity"]):
        raise ValueError("256 parent is not the matching current-app frozen recipe/crop/forcing")
    expected_hashes = {
        "app_sha256": pinned256["app_sha256"],
        "recipe_sha256": pinned256["recipe_sha256"],
        "manifest_sha256": pinned256["manifest_sha256"],
        "elevation_sha256": pinned256["elevation_sha256"],
        "shader_map_sha256": pinned256["shader_map_sha256"],
    }
    start, end = report.get("start_input_hashes"), report.get("end_input_hashes")
    cases = report.get("cases", [])
    if (start != expected_hashes or end != start or len(cases) != 1 or
            cases[0].get("exit_code") != 0 or cases[0].get("input_hashes") != start or
            cases[0].get("command") != _parent_expected_command(app, report_path.parent)):
        raise ValueError("256 parent child exit or pinned input/command identity is invalid")
    metrics_path = report_path.parent / "hydraulics-profile.metrics.csv"
    if not metrics_path.is_file():
        raise FileNotFoundError(f"256 parent profile metrics missing: {metrics_path}")
    metrics_sha = sha256_file(metrics_path)
    frames = v2.read_metrics(metrics_path)
    recomputed = v2.summarize(frames)
    if not v2._has_edge_trigger(recomputed):
        raise ValueError("256 parent did not establish a real edge trigger")
    prefix_recomputed = v2.compare_prefix_to_baseline(metrics_path)
    prefix_reported = report.get("baseline_prefix_comparison")
    if not isinstance(prefix_reported, dict):
        raise ValueError("256 parent lacks its historical-prefix comparison")
    for field in ("passed", "frame_count", "metric_values_compared", "missing_required_old_metric_keys",
                  "mismatches", "baseline_sha256", "baseline_path", "current_path"):
        if prefix_reported.get(field) != prefix_recomputed.get(field):
            raise ValueError("256 parent stored historical-prefix result differs from recomputation")
    prefix_gate = _verify_parent_prefix_gate(report, recomputed, prefix_recomputed)
    return {
        "path": str(report_path),
        "sha256": sha256_file(report_path),
        "metrics_path": str(metrics_path.resolve()),
        "metrics_sha256": metrics_sha,
        "app_sha256": pinned256["app_sha256"],
        "shader_map_sha256": pinned256["shader_map_sha256"],
        "recipe_sha256": pinned256["recipe_sha256"],
        "manifest_sha256": pinned256["manifest_sha256"],
        "elevation_sha256": pinned256["elevation_sha256"],
        "forcing_identity": pinned256["input_identity"]["forcing_identity"],
        "physical_horizon_seconds": recomputed["physical_horizon_seconds"],
        "first_edge_trigger": recomputed["first_edge_trigger"],
        "summary": recomputed,
        "prefix_gate": prefix_gate,
    }


def _check_identity_metrics(frames: dict[int, dict], label: str, frame_count: int) -> None:
    needed = {(HYDRAULIC_IDENTITY, "hash_hi_u32"), (HYDRAULIC_IDENTITY, "hash_lo_u32")}
    for frame in range(frame_count):
        values = frames.get(frame)
        if values is None or not needed.issubset(values):
            raise ValueError(f"{label} profile lacks full-step hydraulic identity hashes at frame {frame}")
        for metric in needed:
            try:
                word = Decimal(str(values[metric]))
            except (InvalidOperation, ValueError) as error:
                raise ValueError(f"{label} profile has an invalid uint32 state hash at frame {frame}") from error
            if (not word.is_finite() or word != word.to_integral_value() or
                    word < 0 or word > 0xFFFFFFFF):
                raise ValueError(f"{label} profile has a non-uint32 state hash at frame {frame}")


def _is_tracer_metric(metric: tuple[str, str]) -> bool:
    category, name = metric
    return category == TRACER or "tracer" in name.lower()


def compare_hydraulic_profiles(
    reference: dict[int, dict[tuple[str, str], Decimal]],
    dyed: dict[int, dict[tuple[str, str], Decimal]],
    *,
    frame_count: int = FRAME_COUNT,
) -> dict[str, Any]:
    expected = list(range(frame_count))
    if sorted(reference) != expected or sorted(dyed) != expected:
        raise ValueError("water/dye profiles do not contain every fixed-step frame exactly once")
    _check_identity_metrics(reference, "no-dye water", frame_count)
    _check_identity_metrics(dyed, "dye", frame_count)
    compared = 0
    for frame in expected:
        left = {key: value for key, value in reference[frame].items() if not _is_tracer_metric(key)}
        right = {key: value for key, value in dyed[frame].items() if not _is_tracer_metric(key)}
        if left.keys() != right.keys():
            raise ValueError(f"water/dye hydraulic metric set differs at frame {frame}")
        if left != right:
            changed = sorted(key for key in left if left[key] != right[key])
            raise ValueError(f"water/dye hydraulic metrics differ at frame {frame}: {changed[:5]}")
        compared += len(left)
    return {"passed": True, "frames_compared": frame_count,
            "hydraulic_metrics_compared": compared,
            "identity_hashes": ["hash_hi_u32", "hash_lo_u32"],
            "comparison": "exact CSV decimal equality for every non-tracer metric and per-frame state hash"}


def validate_dye_profile(
    frames: dict[int, dict[tuple[str, str], Decimal]],
    reference: dict[int, dict[tuple[str, str], Decimal]],
    *,
    frame_count: int = FRAME_COUNT,
    dt: int = v2.DT,
    source_rate: float = DYE_SOURCE_RATE_M3_PER_S,
    pulse_start: int = DYE_START_SECONDS,
    pulse_duration: int = DYE_DURATION_SECONDS,
    postpulse_seconds: int = POSTPULSE_SECONDS,
) -> dict[str, Any]:
    if sorted(frames) != list(range(frame_count)):
        raise ValueError("dye profile does not contain every fixed-step frame exactly once")
    for frame in range(frame_count):
        flags_key = (SOLVER, "finite_volume_status_flags")
        if flags_key not in frames[frame]:
            raise ValueError(f"dye profile is missing finite-volume status flags at frame {frame}")
        flags = float(frames[frame][flags_key])
        if not math.isfinite(flags) or flags != 0:
            raise ValueError(f"dye profile has a nonzero finite-volume status flag at frame {frame}")
    comparison = compare_hydraulic_profiles(reference, frames, frame_count=frame_count)
    previous_source = previous_boundary = 0.0
    maximum_residual = Decimal(0)
    maximum_reported_residual = Decimal(0)
    first_dyed: int | None = None
    source_centroid: tuple[float, float] | None = None
    maximum_dyed_cells = 0.0
    last_postpulse: dict | None = None
    for frame in range(frame_count):
        values = frames[frame]
        missing = sorted(TRACER_REQUIRED - values.keys())
        if missing:
            raise ValueError(f"dye profile is missing required tracer metrics at frame {frame}: {missing}")
        flags = float(values[(SOLVER, "finite_volume_status_flags")])
        if flags != 0:
            raise ValueError(f"dye profile has a nonzero finite-volume status flag at frame {frame}")
        time_seconds = (frame + 1) * dt
        injected_seconds = max(0, min(pulse_duration, time_seconds - pulse_start))
        expected_source = source_rate * injected_seconds
        source_decimal = Decimal(str(values[(TRACER, "cumulative_source_amount_m3")]))
        sink_decimal = Decimal(str(values[(TRACER, "cumulative_sink_amount_m3")]))
        boundary_decimal = Decimal(str(values[(TRACER, "cumulative_boundary_outflow_amount_m3")]))
        total_decimal = Decimal(str(values[(TRACER, "total_tracer_amount_m3")]))
        reported_residual_decimal = Decimal(str(values[(TRACER, "conservation_residual_m3")]))
        source = float(source_decimal)
        sink = float(sink_decimal)
        boundary = float(boundary_decimal)
        total = float(total_decimal)
        residual = float(reported_residual_decimal)
        count = float(values[(TRACER, "dyed_wet_cell_count")])
        x = float(values[(TRACER, "amount_weighted_centroid_cell_x")])
        y = float(values[(TRACER, "amount_weighted_centroid_cell_y")])
        tolerance = max(0.003, expected_source * 3e-4)
        if not all(math.isfinite(v) for v in (source, sink, boundary, total, residual, count, x, y)):
            raise ValueError(f"dye profile contains a nonfinite tracer value at frame {frame}")
        residual_tolerance = max(Decimal("0.003"), total_decimal * Decimal("0.0003"))
        recomputed_residual = total_decimal - source_decimal + sink_decimal + boundary_decimal
        csv_residual_agreement_tolerance = Decimal("0.000003")
        if count < 0 or count != round(count):
            raise ValueError(f"dye spread count is negative or non-integral at frame {frame}")
        if abs(source - expected_source) > tolerance:
            raise ValueError(f"dye source differs from the frozen 60-62 minute pulse at frame {frame}")
        if sink != 0:
            raise ValueError(f"dye profile has a nonzero tracer sink at frame {frame}")
        if min(boundary, total, count) < 0 or boundary + 1e-6 < previous_boundary:
            raise ValueError(f"dye profile has a negative or decreasing tracer quantity at frame {frame}")
        if abs(reported_residual_decimal - recomputed_residual) > csv_residual_agreement_tolerance:
            raise ValueError(f"reported tracer residual disagrees with independently recomputed ledger at frame {frame}")
        if abs(reported_residual_decimal) > residual_tolerance:
            raise ValueError(f"reported dye residual exceeds the unchanged tolerance at frame {frame}")
        if abs(recomputed_residual) > residual_tolerance:
            raise ValueError(f"independently recomputed dye residual exceeds the unchanged tolerance at frame {frame}")
        if source + 1e-6 < previous_source:
            raise ValueError(f"cumulative dye source decreased at frame {frame}")
        previous_source, previous_boundary = source, boundary
        maximum_residual = max(maximum_residual, abs(recomputed_residual))
        maximum_reported_residual = max(maximum_reported_residual, abs(reported_residual_decimal))
        maximum_dyed_cells = max(maximum_dyed_cells, count)
        if total > 0 and first_dyed is None:
            first_dyed = frame
            source_centroid = (x, y)
        if time_seconds == postpulse_seconds:
            last_postpulse = {"physical_seconds": time_seconds, "total_tracer_amount_m3": total,
                              "centroid_cell_x": x, "centroid_cell_y": y}
    if abs(previous_source - source_rate * pulse_duration) > max(0.003, source_rate * pulse_duration * 3e-4):
        raise ValueError("dye pulse did not inject the frozen 12,000 m3 total")
    if first_dyed is None or source_centroid is None:
        raise ValueError("dye parcel was never materialized")
    if last_postpulse is None:
        raise ValueError("dye profile lacks the required postpulse 75-minute observation")
    displacement = math.dist(source_centroid, (last_postpulse["centroid_cell_x"],
                                               last_postpulse["centroid_cell_y"]))
    if last_postpulse["total_tracer_amount_m3"] <= 0 or displacement <= 0.5:
        raise ValueError("dye parcel lacks postpulse centroid movement in the 512 domain")
    if maximum_dyed_cells <= 1:
        raise ValueError("dye parcel did not spread beyond one materially dyed cell")
    return {
        "passed": True,
        "hydraulic_profile_comparison": comparison,
        "dye_source_amount_m3": previous_source,
        "expected_dye_source_amount_m3": source_rate * pulse_duration,
        "dye_sink_amount_m3": 0.0,
        "maximum_absolute_tracer_residual_m3": float(maximum_residual),
        "maximum_absolute_reported_tracer_residual_m3": float(maximum_reported_residual),
        "reported_residual_csv_precision_tolerance_m3": float(csv_residual_agreement_tolerance),
        "residual_tolerance_m3": "max(0.003, total_tracer_amount_m3 * 0.0003) per frame",
        "first_dyed_frame": first_dyed,
        "postpulse_observation": last_postpulse,
        "centroid_displacement_cells": displacement,
        "maximum_dyed_wet_cell_count": maximum_dyed_cells,
        "parcel_motion_interpretation": "generic centroid displacement and dyed-cell spread; no directional/downhill claim",
    }


def _validate_water_evidence(path: Path, app: Path, pinned512: dict[str, Any]) -> dict[str, Any]:
    receipt = _validate_probe_receipt(path, app, pinned512, case="512", dye=False, strict=False)
    _, folder = evidence_receipt(path)
    metrics_path = folder / "profile.metrics.csv"
    frames = v2.read_metrics(metrics_path)
    summary = v2.summarize(frames)
    if (summary.get("numeric_profile_checks_passed") is not True or
            summary.get("unhealthy_frames") != []):
        raise ValueError("512 no-dye water profile fails recomputed V2 numerical controls")
    metrics_decimal = read_profile(metrics_path)
    _check_identity_metrics(metrics_decimal, "no-dye water", FRAME_COUNT)
    receipt.update({
        "metrics_path": str(metrics_path.resolve()),
        "metrics_sha256": sha256_file(metrics_path),
        "summary": summary,
        "edge_trigger_required": False,
    })
    return {"receipt": receipt, "frames": metrics_decimal, "summary": summary}


def _validate_current_inputs(app: Path, expected: dict[str, Any] | None = None) -> dict[str, Any]:
    pinned = v2.pinned_inputs(512, app.resolve())
    if expected is not None:
        v2.require_stable_inputs(expected["start_input_hashes"], {
            key: pinned[key] for key in ("app_sha256", "recipe_sha256", "manifest_sha256",
                                         "elevation_sha256", "shader_map_sha256")
        })
    return pinned


def _snapshot(pinned: dict[str, Any]) -> dict[str, str]:
    return {name: pinned[name] for name in (
        "app_sha256", "recipe_sha256", "manifest_sha256", "elevation_sha256", "shader_map_sha256")}


def _run_app(app: Path, args: list[str], log_path: Path) -> dict[str, Any]:
    ensure_absent([log_path])
    command = ["rtk", "proxy", str(app.resolve()), *args]
    started = time.perf_counter()
    with log_path.open("w") as stream:
        child = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    return {"command": command, "exit_code": child.returncode,
            "wall_seconds": time.perf_counter() - started,
            "log_path": str(log_path.resolve()),
            "log_sha256": sha256_file(log_path)}


def _load_profile_report(path: Path, app: Path, parent: dict[str, Any],
                         water: dict[str, Any]) -> dict[str, Any]:
    report_path = path.resolve()
    if not report_path.is_file():
        raise FileNotFoundError(f"missing V3 profile report: {report_path}")
    report = json.loads(report_path.read_text())
    if (report.get("schema") != "cubey.fluid25d.hillside_transport_v3.profile" or
            report.get("phase_checks_passed") is not True or
            report.get("parent_evidence", {}).get("sha256") != parent.get("sha256") or
            report.get("water_evidence", {}).get("receipt", {}).get("sha256") !=
            water.get("receipt", {}).get("sha256")):
        raise ValueError("V3 profile report does not contain passing matched parent/water evidence")
    if report.get("app_sha256") != sha256_file(app.resolve()):
        raise ValueError("V3 dye profile app identity differs from the selected current app")
    pinned = _validate_current_inputs(app)
    if (report.get("start_input_hashes") != _snapshot(pinned) or
            report.get("end_input_hashes") != _snapshot(pinned)):
        raise ValueError("V3 dye profile input pins no longer match the selected app/recipe/terrain")
    expected_protocol = {
        "domain": 512, "fixed_delta_seconds": v2.DT, "substeps": v2.SUBSTEPS,
        "horizon_seconds": HORIZON_SECONDS, "source_rate_m3_per_s": v2.Q_M3_PER_S,
        "dye_start_seconds": DYE_START_SECONDS, "dye_duration_seconds": DYE_DURATION_SECONDS,
        "explicit_sink": "none", "rain": "none",
    }
    if report.get("frozen_protocol") != expected_protocol:
        raise ValueError("V3 dye profile forcing or timing metadata differs from the frozen protocol")
    metrics = report.get("dye_profile", {})
    metrics_path = Path(metrics.get("metrics_path", ""))
    if not metrics_path.is_file() or sha256_file(metrics_path) != metrics.get("metrics_sha256"):
        raise ValueError("V3 dye profile metrics are missing or changed")
    water_metrics_path = Path(water["receipt"]["metrics_path"])
    recomputed_dye = validate_dye_profile(read_profile(metrics_path), read_profile(water_metrics_path))
    if metrics.get("summary") != recomputed_dye:
        raise ValueError("V3 dye profile stored gate summary differs from recomputed metrics")
    dye_execution = report.get("dye_execution", {})
    imported = dye_execution.get("imported_execution_receipt")
    if imported:
        imported_current = _validate_probe_receipt(
            Path(imported.get("path", "")), app, pinned,
            case="512", dye=True, strict=False)
        if imported_current.get("sha256") != imported.get("sha256"):
            raise ValueError("imported dye execution receipt changed since profile acceptance")
    else:
        if dye_execution.get("exit_code") != 0:
            raise ValueError("V3 dye-profile child did not complete successfully")
        command = ["rtk", "proxy", str(app.resolve()), *v2.arguments(512),
                   "--frames", str(FRAME_COUNT), "--capture", "png",
                   "--fluid25d-catchment-view", "water-isolation",
                   "--fluid25d-natural-flow-home-pitch-radians", "-1.55",
                   "--fluid25d-dye-pulse-start-seconds", str(DYE_START_SECONDS),
                   "--fluid25d-dye-pulse-duration-seconds", str(DYE_DURATION_SECONDS),
                   "--profile-output", str(metrics_path.with_name(
                       metrics_path.name[:-len(".metrics.csv")]).resolve()),
                   "--profile-diagnostics", "--profile-diagnostic-interval", "1",
                   "--output", str(Path(dye_execution.get("output_path", "")).resolve())]
        if (dye_execution.get("command") != command or
                not dye_execution.get("log_path") or
                not Path(dye_execution["log_path"]).is_file() or
                sha256_file(Path(dye_execution["log_path"])) != dye_execution.get("log_sha256") or
                not dye_execution.get("output_sha256") or
                not Path(dye_execution["output_path"]).is_file() or
                sha256_file(Path(dye_execution["output_path"])) != dye_execution.get("output_sha256")):
            raise ValueError("V3 dye-profile child command, log, or final image is incomplete or changed")
    return {"path": str(report_path), "sha256": sha256_file(report_path), "report": report}


def profile_phase(app: Path, out: Path, parent_path: Path, water_path: Path,
                  dye_evidence: Path | None) -> dict[str, Any]:
    report_path = out / "profile.json"
    ensure_absent([report_path])
    start_pinned = _validate_current_inputs(app)
    parent = validate_parent_evidence(parent_path, app, start_pinned)
    water = _validate_water_evidence(water_path, app, start_pinned)
    if dye_evidence is None:
        prefix = out / "dye-profile"
        image = out / "dye-profile-final.png"
        log = out / "dye-profile-child.log"
        ensure_absent([prefix.with_suffix(".metrics.csv"), prefix.with_suffix(".passes.csv"),
                       prefix.with_suffix(".frames.csv"), prefix.with_suffix(".trace.json"),
                       prefix.with_suffix(".summary.txt"), image, log])
        args = v2.arguments(512) + [
            "--frames", str(FRAME_COUNT), "--capture", "png",
            "--fluid25d-catchment-view", "water-isolation",
            "--fluid25d-natural-flow-home-pitch-radians", "-1.55",
            "--fluid25d-dye-pulse-start-seconds", str(DYE_START_SECONDS),
            "--fluid25d-dye-pulse-duration-seconds", str(DYE_DURATION_SECONDS),
            "--profile-output", str(prefix.resolve()), "--profile-diagnostics",
            "--profile-diagnostic-interval", "1", "--output", str(image.resolve()),
        ]
        metrics_path = prefix.with_suffix(".metrics.csv")
        partial: dict[str, Any] = {
            "schema": "cubey.fluid25d.hillside_transport_v3.profile",
            "phase": "profile", "phase_checks_passed": False,
            "app_sha256": start_pinned["app_sha256"],
            "start_input_hashes": _snapshot(start_pinned),
            "dye_execution": {
                "command": ["rtk", "proxy", str(app.resolve()), *args],
                "exit_code": None,
                "log_path": str(log.resolve()),
                "metrics_path": str(metrics_path.resolve()),
                "output_path": str(image.resolve()),
            },
        }
        write_report(report_path, partial)
        execution = _run_app(app, args, log)
        execution.update({"metrics_path": str(metrics_path.resolve()),
                          "metrics_sha256": sha256_file(metrics_path) if metrics_path.is_file() else None,
                          "output_path": str(image.resolve()),
                          "output_sha256": sha256_file(image) if image.is_file() else None})
        partial["dye_execution"] = execution
        partial["dye_profile"] = {
            "metrics_path": str(metrics_path.resolve()),
            "metrics_sha256": execution["metrics_sha256"],
        }
        write_report(report_path, partial)
        if execution["exit_code"] != 0:
            raise RuntimeError(f"dye-profile child failed with exit code {execution['exit_code']}")
        if not metrics_path.is_file():
            raise ValueError("dye-profile child succeeded but did not produce profile metrics")
        metrics_digest = sha256_file(metrics_path)
        if execution["output_sha256"] is None:
            raise ValueError("dye-profile child succeeded without its final PNG")
        frames = read_profile(metrics_path)
    else:
        receipt = _validate_probe_receipt(dye_evidence, app, start_pinned,
                                          case="512", dye=True, strict=False)
        _, folder = evidence_receipt(dye_evidence)
        metrics_path = folder / "profile.metrics.csv"
        execution = {"imported_execution_receipt": receipt}
        metrics_digest = sha256_file(metrics_path)
        frames = read_profile(metrics_path)
        execution["metrics_path"] = str(metrics_path.resolve())
        execution["metrics_sha256"] = metrics_digest
        partial = {
            "schema": "cubey.fluid25d.hillside_transport_v3.profile",
            "phase": "profile", "phase_checks_passed": False,
            "app_sha256": start_pinned["app_sha256"],
            "start_input_hashes": _snapshot(start_pinned),
            "dye_execution": execution,
            "dye_profile": {"metrics_path": str(metrics_path.resolve()),
                             "metrics_sha256": metrics_digest},
        }
        write_report(report_path, partial)
    summary = validate_dye_profile(frames, water["frames"])
    end_pinned = _validate_current_inputs(app)
    v2.require_stable_inputs(_snapshot(start_pinned), _snapshot(end_pinned))
    if sha256_file(Path(parent["path"])) != parent["sha256"]:
        raise ValueError("256 parent evidence changed during profile analysis")
    if sha256_file(Path(water["receipt"]["path"])) != water["receipt"]["sha256"] or \
            sha256_file(Path(water["receipt"]["metrics_path"])) != water["receipt"]["metrics_sha256"]:
        raise ValueError("512 no-dye water evidence changed during profile analysis")
    _assert_receipt_stable(water["receipt"], "512 no-dye water receipt")
    report: dict[str, Any] = {
        "schema": "cubey.fluid25d.hillside_transport_v3.profile",
        "phase": "profile",
        "phase_checks_passed": True,
        "app_sha256": start_pinned["app_sha256"],
        "start_input_hashes": _snapshot(start_pinned),
        "end_input_hashes": _snapshot(end_pinned),
        "frozen_protocol": {
            "domain": 512, "fixed_delta_seconds": v2.DT, "substeps": v2.SUBSTEPS,
            "horizon_seconds": HORIZON_SECONDS, "source_rate_m3_per_s": v2.Q_M3_PER_S,
            "dye_start_seconds": DYE_START_SECONDS, "dye_duration_seconds": DYE_DURATION_SECONDS,
            "explicit_sink": "none", "rain": "none",
        },
        "parent_evidence": parent,
        "water_evidence": {"receipt": water["receipt"], "summary": water["summary"]},
        "dye_execution": execution,
        "dye_profile": {"metrics_path": str(metrics_path.resolve()),
                         "metrics_sha256": metrics_digest, "summary": summary},
        "not_claimed": "x-direction/downhill parcel route, calibrated natural hydrology, or strict 7200s CPU parity",
    }
    write_report(report_path, report)
    return report


def _strict_gates(app: Path, pinned: dict[str, Any], site_a_path: Path,
                  hillside_path: Path) -> dict[str, Any]:
    return {
        "site_a": _validate_probe_receipt(site_a_path, app, pinned,
                                           case="site-a", dye=True, strict=True),
        "hillside": _validate_probe_receipt(hillside_path, app, pinned,
                                             case="256", dye=False, strict=True),
    }


VIDEO_SPECS = (
    ("overview-transport", "-0.72", ()),
    ("close-oblique-transport", "-0.72", ("--fluid25d-hillside-source-context",)),
    ("topdown-transport", "-1.55", ("--fluid25d-hillside-source-context",)),
)
STILL_SPECS = (("10min", 600), ("30min", 1800), ("62min", 3720),
               ("75min-postpulse", POSTPULSE_SECONDS), ("120min", HORIZON_SECONDS))
VIDEO_LABELS = {label: (pitch, extra) for label, pitch, extra in VIDEO_SPECS}
STILL_SECONDS_BY_LABEL = dict(STILL_SPECS)
STILL_LABEL_BY_SECONDS = {seconds: label for label, seconds in STILL_SPECS}


def _transport_common_arguments() -> list[str]:
    return v2.arguments(512) + [
        "--fluid25d-dye-pulse-start-seconds", str(DYE_START_SECONDS),
        "--fluid25d-dye-pulse-duration-seconds", str(DYE_DURATION_SECONDS),
    ]


def _transport_video_arguments(label: str, output: Path) -> list[str]:
    try:
        pitch, extra = VIDEO_LABELS[label]
    except KeyError as error:
        raise ValueError(f"unsupported transport video label: {label}") from error
    return _transport_common_arguments() + [
        "--frames", str(FRAME_COUNT), "--capture", "video", "--fps", str(FPS),
        "--fluid25d-catchment-view", "transport-inspection",
        "--fluid25d-natural-flow-home-pitch-radians", pitch, *extra,
        "--output", str(output.resolve()),
    ]


def _transport_still_arguments(physical_seconds: int, output: Path) -> list[str]:
    if physical_seconds not in STILL_LABEL_BY_SECONDS:
        raise ValueError(f"unsupported transport still time: {physical_seconds}")
    frames = physical_seconds // v2.DT
    extra = ["--fluid25d-hillside-source-context"] if physical_seconds in (3720, POSTPULSE_SECONDS) else []
    return _transport_common_arguments() + [
        "--frames", str(frames), "--capture", "png",
        "--fluid25d-catchment-view", "transport-inspection",
        "--fluid25d-natural-flow-home-pitch-radians", "-0.72", *extra,
        "--output", str(output.resolve()),
    ]


def _transport_app_command(app: Path, arguments: list[str]) -> list[str]:
    return ["rtk", "proxy", str(app.resolve()), *arguments]


def _label_still_command(source: Path, target: Path, physical_seconds: int) -> list[str]:
    return ["rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-i", str(source.resolve()),
            "-vf", _transport_still_label_filter(physical_seconds), "-frames:v", "1",
            "-update", "1", str(target.resolve())]


def _label_video_command(source: Path, target: Path) -> list[str]:
    return ["rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-i", str(source.resolve()),
            "-vf", _transport_video_label_filter(), "-c:v", "libx264", "-crf", "20",
            "-an", str(target.resolve())]


def _transport_video_label_filter() -> str:
    lines = (
        "Hillside Transport Inspection",
        "512x512 | 60x playback",
        "Water 100 m3/s continuous | no prescribed drain",
        "Conserved dye pulse at physical 60-62 min",
        r"Physical time %{eif\:n*2+2\:d} s / 7200 s",
        "Magenta is concentration; blue is water depth (not speed)",
        "Green ring marks input; only crop edges can export",
    )
    return ",".join(
        f"drawtext=text='{line}':x=12:y={12 + 30 * index}:fontsize=17:fontcolor=white:"
        "box=1:boxcolor=black@0.8:boxborderw=5"
        for index, line in enumerate(lines)
    )


def _transport_still_label_filter(physical_seconds: int) -> str:
    lines = (
        "Water 100 m3/s continuous | no prescribed drain",
        f"Physical time {physical_seconds} s / {HORIZON_SECONDS} s",
        "Conserved dye pulse at physical 60-62 min",
        "Magenta is concentration; blue is water depth (not speed)",
        "Green ring marks input; only crop edges can export",
    )
    return ",".join(
        f"drawtext=text='{line}':x=12:y={12 + 29 * index}:fontsize=17:fontcolor=white:"
        "box=1:boxcolor=black@0.8:boxborderw=5"
        for index, line in enumerate(lines)
    )


def label_video(source: Path, out: Path, label: str) -> dict[str, Any]:
    target = out / f"{label}-labelled.mp4"
    log = out / f"{label}-label.log"
    ensure_absent([target, log])
    command = _label_video_command(source, target)
    started = time.perf_counter()
    with log.open("w") as stream:
        child = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    return {"exit_code": child.returncode, "command": command,
            "wall_seconds": time.perf_counter() - started,
            "path": str(target.resolve()),
            "sha256": sha256_file(target) if child.returncode == 0 and target.is_file() else None,
            "log_path": str(log.resolve()), "log_sha256": sha256_file(log),
            "source_sha256": sha256_file(source), "label": label,
            "physical_evolution_seconds": HORIZON_SECONDS, "video_fps": FPS,
            "playback_acceleration": v2.DT * FPS,
            "interpretation": "Magenta represents dye concentration; blue represents water depth, not speed"}


def label_still(source: Path, out: Path, label: str, physical_seconds: int) -> dict[str, Any]:
    target = out / f"transport-{label}-labelled.png"
    log = out / f"transport-{label}-label.log"
    ensure_absent([target, log])
    command = _label_still_command(source, target, physical_seconds)
    started = time.perf_counter()
    with log.open("w") as stream:
        child = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    return {"exit_code": child.returncode, "command": command,
            "wall_seconds": time.perf_counter() - started,
            "path": str(target.resolve()),
            "sha256": sha256_file(target) if child.returncode == 0 and target.is_file() else None,
            "log_path": str(log.resolve()), "log_sha256": sha256_file(log),
            "source_sha256": sha256_file(source),
            "physical_seconds": physical_seconds,
            "label": label,
            "interpretation": "Magenta represents dye concentration; blue represents water depth, not speed"}


def captures_phase(app: Path, out: Path, parent_path: Path, water_path: Path,
                   profile_path: Path, site_a_path: Path,
                   hillside_path: Path) -> dict[str, Any]:
    report_path = out / "captures.json"
    ensure_absent([report_path])
    start_pinned = _validate_current_inputs(app)
    parent = validate_parent_evidence(parent_path, app, start_pinned)
    water = _validate_water_evidence(water_path, app, start_pinned)
    profile = _load_profile_report(profile_path, app, parent, water)
    strict = _strict_gates(app, start_pinned, site_a_path, hillside_path)
    video_paths = [out / f"{label}.mp4" for label, _, _ in VIDEO_SPECS]
    label_video_paths = [out / f"{label}-labelled.mp4" for label, _, _ in VIDEO_SPECS]
    still_paths = [out / f"transport-{label}.png" for label, _ in STILL_SPECS]
    label_still_paths = [out / f"transport-{label}-labelled.png" for label, _ in STILL_SPECS]
    logs = [out / f"{label}.log" for label, _, _ in VIDEO_SPECS]
    logs += [out / f"transport-{label}.log" for label, _ in STILL_SPECS]
    label_logs = [out / f"{label}-label.log" for label, _, _ in VIDEO_SPECS]
    label_logs += [out / f"transport-{label}-label.log" for label, _ in STILL_SPECS]
    ensure_absent(video_paths + label_video_paths + still_paths + label_still_paths + logs + label_logs)
    report: dict[str, Any] = {
        "schema": "cubey.fluid25d.hillside_transport_v3.captures",
        "phase": "captures", "phase_checks_passed": False,
        "app_sha256": start_pinned["app_sha256"],
        "start_input_hashes": _snapshot(start_pinned),
        "parent_evidence": parent, "water_evidence": water["receipt"],
        "profile_evidence": profile, "strict_receipts": strict,
        "video_cases": [], "still_cases": [],
        "frozen_capture_interpretation": {
            "water_source": "100 m3/s continuous through the full 7200 seconds",
            "dye": "conserved concentration pulse from physical 3600 to 3720 seconds",
            "explicit_drain": "none prescribed",
            "physical_time_overlay": "seconds; capture frame f shows state after (f+1)*2 seconds",
            "view": "Transport Inspection; magenta concentration, blue water depth (not speed)",
            "video_fps": FPS, "playback_acceleration": v2.DT * FPS,
        },
    }
    write_report(report_path, report)
    for label, _, _ in VIDEO_SPECS:
        raw = out / f"{label}.mp4"
        args = _transport_video_arguments(label, raw)
        execution = _run_app(app, args, out / f"{label}.log")
        execution.update({"label": label, "raw_video_path": str(raw.resolve()),
                         "raw_video_sha256": sha256_file(raw) if raw.is_file() else None,
                         "physical_evolution_seconds": HORIZON_SECONDS,
                         "video_fps": FPS, "playback_acceleration": v2.DT * FPS,
                         "dye_pulse_seconds": [DYE_START_SECONDS, DYE_START_SECONDS + DYE_DURATION_SECONDS],
                         "water_rate_m3_per_s": v2.Q_M3_PER_S,
                         "explicit_drain": "none prescribed"})
        report["video_cases"].append(execution)
        write_report(report_path, report)
        if execution["exit_code"] != 0 or execution["raw_video_sha256"] is None:
            raise RuntimeError(f"transport video child failed or omitted output: {label}")
        labelled = label_video(raw, out, label)
        execution["labelled_video"] = labelled
        write_report(report_path, report)
        if labelled["exit_code"] != 0 or labelled["sha256"] is None:
            raise RuntimeError(f"transport video labelling failed: {label}")
    for label, seconds in STILL_SPECS:
        image = out / f"transport-{label}.png"
        frames = seconds // v2.DT
        args = _transport_still_arguments(seconds, image)
        execution = _run_app(app, args, out / f"transport-{label}.log")
        execution.update({"label": label, "physical_seconds": seconds,
                         "frame_count": frames, "path": str(image.resolve()),
                         "sha256": sha256_file(image) if image.is_file() else None})
        report["still_cases"].append(execution)
        write_report(report_path, report)
        if execution["exit_code"] != 0 or execution["sha256"] is None:
            raise RuntimeError(f"transport still child failed or omitted output: {label}")
        labelled = label_still(image, out, label, seconds)
        execution["labelled_still"] = labelled
        write_report(report_path, report)
        if labelled["exit_code"] != 0 or labelled["sha256"] is None:
            raise RuntimeError(f"transport still labelling failed: {label}")
    end_pinned = _validate_current_inputs(app)
    v2.require_stable_inputs(_snapshot(start_pinned), _snapshot(end_pinned))
    _assert_same_file(parent["path"], parent["sha256"], "256 parent report")
    _assert_same_file(water["receipt"]["path"], water["receipt"]["sha256"], "512 water receipt")
    _assert_same_file(water["receipt"]["metrics_path"], water["receipt"]["metrics_sha256"],
                      "512 no-dye profile")
    _assert_same_file(profile["path"], profile["sha256"], "V3 dye profile report")
    _assert_same_file(profile["report"]["dye_profile"]["metrics_path"],
                      profile["report"]["dye_profile"]["metrics_sha256"], "V3 dye profile")
    for receipt in strict.values():
        _assert_receipt_stable(receipt, "strict-oracle receipt")
    report["end_input_hashes"] = _snapshot(end_pinned)
    report["phase_checks_passed"] = (
        len(report["video_cases"]) == len(VIDEO_SPECS) and len(report["still_cases"]) == len(STILL_SPECS) and
        all(c.get("exit_code") == 0 and c.get("raw_video_sha256") and
            c.get("labelled_video", {}).get("exit_code") == 0 and c.get("labelled_video", {}).get("sha256")
            for c in report["video_cases"]) and
        all(c.get("exit_code") == 0 and c.get("sha256") and
            c.get("labelled_still", {}).get("exit_code") == 0 and
            c.get("labelled_still", {}).get("sha256")
            for c in report["still_cases"]))
    write_report(report_path, report)
    return report


def _assert_same_file(path: str, expected: str, label: str) -> None:
    target = Path(path)
    if not target.is_file() or sha256_file(target) != expected:
        raise ValueError(f"{label} changed during the evidence phase")


def _assert_receipt_stable(record: dict[str, Any], label: str) -> None:
    receipt_path = Path(record["path"])
    _assert_same_file(str(receipt_path), record["sha256"], label)
    for name, digest in record["artifacts"].items():
        _assert_same_file(str(receipt_path.parent / name), digest, f"{label} artifact {name}")


def _validate_still_capture_case(case: dict[str, Any], app: Path,
                                 capture_dir: Path) -> None:
    label = case.get("label")
    seconds = STILL_SECONDS_BY_LABEL.get(label)
    if seconds is None:
        raise ValueError("capture report contains an unknown transport-still label")
    raw = capture_dir / f"transport-{label}.png"
    labelled_path = capture_dir / f"transport-{label}-labelled.png"
    expected_log = capture_dir / f"transport-{label}.log"
    expected_label_log = capture_dir / f"transport-{label}-label.log"
    label_record = case.get("labelled_still", {})
    if (case.get("exit_code") != 0 or case.get("physical_seconds") != seconds or
            case.get("frame_count") != seconds // v2.DT or
            case.get("command") != _transport_app_command(
                app, _transport_still_arguments(seconds, raw)) or
            case.get("path") != str(raw.resolve()) or
            case.get("log_path") != str(expected_log.resolve()) or
            label_record.get("exit_code") != 0 or
            label_record.get("command") != _label_still_command(raw, labelled_path, seconds) or
            label_record.get("path") != str(labelled_path.resolve()) or
            label_record.get("label") != label or
            label_record.get("physical_seconds") != seconds or
            label_record.get("source_sha256") != case.get("sha256") or
            label_record.get("log_path") != str(expected_label_log.resolve()) or
            label_record.get("interpretation") !=
            "Magenta represents dye concentration; blue represents water depth, not speed"):
        raise ValueError("capture report contains a failed or changed transport-still protocol")
    _assert_same_file(case["log_path"], case.get("log_sha256", ""), "transport still log")
    _assert_same_file(label_record["log_path"], label_record.get("log_sha256", ""),
                      "transport still label log")
    _assert_same_file(case["path"], case.get("sha256", ""), "raw transport still")
    _assert_same_file(label_record["path"], label_record.get("sha256", ""),
                      "labelled transport still")


def report_phase(app: Path, out: Path, profile_path: Path, captures_path: Path,
                 parent_path: Path, water_path: Path,
                 strict_gates: dict[str, Any]) -> dict[str, Any]:
    report_path = out / "report.json"
    ensure_absent([report_path])
    app = app.resolve()
    pinned = _validate_current_inputs(app)
    parent = validate_parent_evidence(parent_path, app, pinned)
    water = _validate_water_evidence(water_path, app, pinned)
    profile = _load_profile_report(profile_path, app, parent, water)
    captures_resolved = captures_path.resolve()
    if not captures_resolved.is_file():
        raise FileNotFoundError(f"missing V3 captures report: {captures_resolved}")
    captures = json.loads(captures_resolved.read_text())
    if (captures.get("schema") != "cubey.fluid25d.hillside_transport_v3.captures" or
            captures.get("phase_checks_passed") is not True or
            captures.get("profile_evidence", {}).get("sha256") != profile["sha256"] or
            captures.get("parent_evidence", {}).get("sha256") != parent.get("sha256") or
            captures.get("water_evidence", {}).get("sha256") != water["receipt"].get("sha256") or
            captures.get("app_sha256") != pinned["app_sha256"] or
            captures.get("start_input_hashes") != _snapshot(pinned) or
            captures.get("end_input_hashes") != _snapshot(pinned)):
        raise ValueError("capture report does not prove completed captures from the passing dye profile")
    if captures.get("strict_receipts") != strict_gates:
        raise ValueError("capture report does not reference the currently verified strict receipts")
    videos = captures.get("video_cases", [])
    stills = captures.get("still_cases", [])
    if ({case.get("label") for case in videos} !=
            set(VIDEO_LABELS) or len(videos) != len(VIDEO_SPECS) or
            len(stills) != len(STILL_SPECS) or
            {case.get("physical_seconds") for case in stills} != {600, 1800, 3720, 4500, 7200}):
        raise ValueError("capture report is missing a required matched video or physical-time still")
    capture_dir = captures_resolved.parent
    for case in videos:
        label = case.get("label")
        raw = capture_dir / f"{label}.mp4"
        labelled_path = capture_dir / f"{label}-labelled.mp4"
        expected_command = _transport_app_command(app, _transport_video_arguments(label, raw))
        label_record = case.get("labelled_video", {})
        expected_label_log = capture_dir / f"{label}-label.log"
        if (case.get("exit_code") != 0 or case.get("physical_evolution_seconds") != HORIZON_SECONDS or
                case.get("video_fps") != FPS or case.get("playback_acceleration") != v2.DT * FPS or
                case.get("dye_pulse_seconds") != [DYE_START_SECONDS, DYE_START_SECONDS + DYE_DURATION_SECONDS] or
                case.get("water_rate_m3_per_s") != v2.Q_M3_PER_S or
                case.get("explicit_drain") != "none prescribed" or
                case.get("command") != expected_command or
                case.get("raw_video_path") != str(raw.resolve()) or
                case.get("log_path") != str((capture_dir / f"{label}.log").resolve()) or
                label_record.get("exit_code") != 0 or
                label_record.get("command") != _label_video_command(raw, labelled_path) or
                label_record.get("path") != str(labelled_path.resolve()) or
                label_record.get("label") != label or
                label_record.get("source_sha256") != case.get("raw_video_sha256") or
                label_record.get("log_path") != str(expected_label_log.resolve()) or
                label_record.get("interpretation") !=
                "Magenta represents dye concentration; blue represents water depth, not speed"):
            raise ValueError("capture report contains a failed or changed transport-video protocol")
        _assert_same_file(case["log_path"], case.get("log_sha256", ""), "transport video log")
        _assert_same_file(label_record["log_path"], label_record.get("log_sha256", ""),
                          "transport video label log")
    for case in stills:
        _validate_still_capture_case(case, app, capture_dir)
    artifacts: list[dict[str, str]] = []
    for group in ("video_cases", "still_cases"):
        for case in captures.get(group, []):
            if group == "video_cases":
                paths = ((case.get("raw_video_path"), case.get("raw_video_sha256")),
                         (case.get("labelled_video", {}).get("path"), case.get("labelled_video", {}).get("sha256")))
            else:
                paths = ((case.get("path"), case.get("sha256")),
                         (case.get("labelled_still", {}).get("path"),
                          case.get("labelled_still", {}).get("sha256")))
            for path, digest in paths:
                if not path or not digest:
                    raise ValueError("capture report has an incomplete media artifact record")
                _assert_same_file(path, digest, "capture artifact")
                artifacts.append({"path": path, "sha256": digest})
    report = {
        "schema": "cubey.fluid25d.hillside_transport_v3.review",
        "phase": "report", "phase_checks_passed": True,
        "app_sha256": pinned["app_sha256"],
        "parent_evidence": parent,
        "water_evidence": {"receipt": water["receipt"], "summary": water["summary"]},
        "profile_evidence": profile,
        "captures_evidence": {"path": str(captures_resolved),
                              "sha256": sha256_file(captures_resolved),
                              "video_count": len(captures.get("video_cases", [])),
                              "still_count": len(captures.get("still_cases", []))},
        "media_artifacts": artifacts,
        "strict_receipts": strict_gates,
        "historical_prefix_note": parent["prefix_gate"],
    }
    write_report(report_path, report)
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=APP_DEFAULT)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--phase", choices=("profile", "captures", "report"), required=True)
    parser.add_argument("--parent-evidence", type=Path, required=True,
                        help="current-app 256 V2 hydraulics.json")
    parser.add_argument("--water-evidence", type=Path, required=True,
                        help="512 no-dye run_probe evidence folder or execution.json")
    parser.add_argument("--dye-evidence", type=Path,
                        help="optional existing 512 dye run_probe profile; otherwise profile phase runs the app")
    parser.add_argument("--site-a-strict-evidence", type=Path,
                        help="7200s Site A strict-oracle execution folder/receipt for capture promotion")
    parser.add_argument("--hillside-strict-evidence", type=Path,
                        help="7200s final 256 hillside strict-oracle execution folder/receipt")
    parser.add_argument("--profile-evidence", type=Path,
                        help="profile.json path; defaults to --out/profile.json")
    parser.add_argument("--captures-evidence", type=Path,
                        help="captures.json path; defaults to --out/captures.json")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    app = args.app.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    phase_report = out / ("report.json" if args.phase == "report" else f"{args.phase}.json")
    if phase_report.exists():
        raise SystemExit(f"refusing to overwrite existing evidence report: {phase_report}")
    try:
        if args.dye_evidence is not None and args.phase != "profile":
            raise ValueError("--dye-evidence is only valid with --phase profile")
        if args.phase == "profile":
            if args.dye_evidence and (args.site_a_strict_evidence or args.hillside_strict_evidence):
                raise ValueError("strict receipt options are only meaningful for captures/report phases")
            report = profile_phase(app, out, args.parent_evidence, args.water_evidence, args.dye_evidence)
        elif args.phase == "captures":
            if args.site_a_strict_evidence is None or args.hillside_strict_evidence is None:
                raise ValueError("captures phase requires both --site-a-strict-evidence and --hillside-strict-evidence")
            report = captures_phase(
                app, out, args.parent_evidence, args.water_evidence,
                (args.profile_evidence or (out / "profile.json")).resolve(),
                args.site_a_strict_evidence, args.hillside_strict_evidence,
            )
        else:
            if args.site_a_strict_evidence is None or args.hillside_strict_evidence is None:
                raise ValueError("report phase requires both --site-a-strict-evidence and --hillside-strict-evidence")
            # Reports retain the strict receipt paths through captures.json;
            # validate those exact receipts again before final promotion.
            pinned = _validate_current_inputs(app)
            strict_gates = _strict_gates(
                app, pinned, args.site_a_strict_evidence, args.hillside_strict_evidence)
            report = report_phase(
                app, out, (args.profile_evidence or (out / "profile.json")).resolve(),
                (args.captures_evidence or (out / "captures.json")).resolve(),
                args.parent_evidence, args.water_evidence,
                strict_gates,
            )
        print(json.dumps({"phase": args.phase, "phase_checks_passed": report["phase_checks_passed"],
                          "report": str(out / ("report.json" if args.phase == "report" else f"{args.phase}.json"))},
                         sort_keys=True))
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError, RuntimeError) as error:
        path = out / f"{args.phase}.json"
        if path.exists():
            try:
                failed = json.loads(path.read_text())
            except (OSError, json.JSONDecodeError):
                failed = {}
        else:
            failed = {}
        failed.update({"schema": failed.get("schema", f"cubey.fluid25d.hillside_transport_v3.{args.phase}"),
                       "phase": args.phase, "phase_checks_passed": False, "error": str(error)})
        write_report(path, failed)
        print(json.dumps({"phase": args.phase, "phase_checks_passed": False,
                          "error": str(error), "report": str(path)}, sort_keys=True))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
