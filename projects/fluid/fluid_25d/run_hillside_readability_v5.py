#!/usr/bin/env python3
"""Run fail-closed V5 hillside supply/readability evidence phases.

V5 retains the frozen 512x512 native-30m hillside recipe and uses only
opt-in presentation and supply-response controls. Profiles compare against
the retained V3 water and V4 dye evidence; captures are separately receipted
and remain offscreen evidence pending live GUI review.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from decimal import Decimal
import hashlib
import json
from pathlib import Path
from typing import Any

import run_hillside_motion_v4 as v4
import run_hillside_transport_v3 as v3
import run_hillside_sustained_flow_v2 as v2

ROOT = v2.ROOT
APP_DEFAULT = v2.DEFAULT_APP
V3_ROOT = ROOT / "outputs/fluid/hillside-conservation-v3-20260928-SlPlGa"
V3_WATER_PROFILE = V3_ROOT / "aligned-512-water/profile.metrics.csv"
V4_ROOT = ROOT / "outputs/fluid/hillside-motion-v4-20260930-OqfDt7/final-surface"
V4_PROFILE_REPORT = V4_ROOT / "profiles/profiles.json"
V4_DYE_PROFILE = V4_ROOT / "profiles/profile-512-dye/profile.metrics.csv"

HORIZON_SECONDS = 7200
FRAME_COUNT = HORIZON_SECONDS // v2.DT
FPS = 30
PLAYBACK_ACCELERATION = v2.DT * FPS
PULSE_START_SECONDS = 3600
PULSE_DURATION_SECONDS = 120
RESPONSE_TRANSITIONS = ((0, 100), (3600, 150), (5400, 50), (7200, 100))
EXPECTED_FINAL_SCHEDULED_VOLUME_M3 = Decimal("720000")
CSV_RESIDUAL_AGREEMENT_TOLERANCE = Decimal("0.000003")
SUPPLY_CATEGORY = "fluid_25d.supply"
MARKER_CATEGORY = v4.MARKER_METRIC_CATEGORY
OBSERVATION_CATEGORY = "fluid_25d.hillside_observation"
ALLOWED_OBSERVATION_CATEGORIES = {MARKER_CATEGORY, SUPPLY_CATEGORY, OBSERVATION_CATEGORY}
SUPPLY_METRICS = ("step_start_seconds", "source_m3_per_s", "scheduled_volume_m3")
OBSERVATION_METRICS = (
    "travel_cell_x", "travel_cell_z", "collection_cell_x", "collection_cell_z",
    "collection_depth_m",
)
WATER_BUDGET_METRICS = {
    (v3.SOLVER, "finite_volume_status_flags"),
    (v3.WATER, "cumulative_source_volume_m3"),
    (v3.WATER, "cumulative_sink_volume_m3"),
    (v3.WATER, "cumulative_boundary_outflow_volume_m3"),
    (v3.WATER, "total_water_volume_m3"),
    (v3.WATER, "conservation_residual_m3"),
}
EXPECTED_WIDTH = 1280
EXPECTED_HEIGHT = 720
FIXED_DEPTH_LEGEND = "Water depth: fixed log scale | 0.01 m | 0.1 m | 1 m | 10 m"
LOCAL_MARKER_CAVEAT = "Local markers show movement where seeded; they are not source-released parcels."
EXCERPT_START_SECONDS = 55 * 60
EXCERPT_END_SECONDS = HORIZON_SECONDS

# One travel video per supply treatment. Other physical camera views are
# single end-state PNGs. The source legacy/cued pair isolates the depth cue.
VIDEO_CASES = (
    ("travel-reference", "travel", "-0.72", False, True, "constant Q100"),
    ("travel-response", "travel", "-0.72", True, True, "Q100 / Q150 / Q50 / Q100"),
)
STILL_CASES = (
    ("source-reference-legacy-depth", "source", "-0.72", False, False, "constant Q100"),
    ("source-reference-depth-cues", "source", "-0.72", False, True, "constant Q100"),
    ("source-response", "source", "-0.72", True, True, "Q100 / Q150 / Q50 / Q100"),
    ("collection-reference", "collection", "-0.72", False, True, "constant Q100"),
    ("collection-response", "collection", "-0.72", True, True, "Q100 / Q150 / Q50 / Q100"),
    ("overview-topdown-reference", "overview", "-1.55", False, True, "constant Q100"),
)
PROFILE_CASES = (
    ("constant-water", False, False),
    ("constant-dye", True, False),
    ("supply-response", False, True),
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def read_report(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text())
    if not isinstance(value, dict):
        raise ValueError(f"evidence report is not a JSON object: {path}")
    return value


def write_report(path: Path, report: dict[str, Any]) -> None:
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")


def ensure_absent(paths: list[Path]) -> None:
    existing = [str(path) for path in paths if path.exists()]
    if existing:
        raise FileExistsError("refusing to overwrite existing evidence: " + ", ".join(existing))


def create_phase_directory(out: Path, phase: str) -> Path:
    if phase not in {"profiles", "captures"}:
        raise ValueError(f"unsupported writable V5 phase: {phase}")
    resolved = out.resolve()
    protected = (V3_ROOT.resolve(), V4_ROOT.resolve(), V4_ROOT.parent.resolve())
    if any(resolved == root or root in resolved.parents for root in protected):
        raise ValueError("V5 output must not be inside retained V3 or V4 evidence")
    resolved.mkdir(parents=True, exist_ok=True)
    target = resolved / phase
    target.mkdir()
    return target


def _execution_identity(app: Path) -> dict[str, Any]:
    return v4._execution_identity(app.resolve(), 512)


def _rate_at(step_start_seconds: Decimal, response: bool) -> Decimal:
    if not response or step_start_seconds < Decimal(3600) or step_start_seconds >= Decimal(7200):
        return Decimal(100)
    if step_start_seconds < Decimal(5400):
        return Decimal(150)
    return Decimal(50)


def _is_timing_metric(metric: tuple[str, str]) -> bool:
    return v4._is_timing_metric(metric)


def compare_common_profiles(
    baseline: dict[int, dict[tuple[str, str], Decimal]],
    current: dict[int, dict[tuple[str, str], Decimal]],
    *,
    frame_count: int = FRAME_COUNT,
    allowed_categories: set[str] | None = None,
) -> dict[str, Any]:
    """Compare every retained non-timing metric except named new observations."""
    allowed = ALLOWED_OBSERVATION_CATEGORIES if allowed_categories is None else allowed_categories
    if not allowed.issubset(ALLOWED_OBSERVATION_CATEGORIES):
        raise ValueError("profile comparison may exclude only the named marker, supply, and observation categories")
    expected_frames = list(range(frame_count))
    if sorted(baseline) != expected_frames or sorted(current) != expected_frames:
        raise ValueError("profiles do not contain every requested fixed-step frame exactly once")
    v3._check_identity_metrics(baseline, "retained baseline", frame_count)
    v3._check_identity_metrics(current, "V5", frame_count)

    common_metrics: set[tuple[str, str]] | None = None
    current_only: set[tuple[str, str]] = set()
    timing: set[tuple[str, str]] = set()
    compared_values = 0
    for frame in expected_frames:
        old_all, new_all = baseline[frame], current[frame]
        old = {key: value for key, value in old_all.items()
               if not _is_timing_metric(key) and key[0] not in allowed}
        new = {key: value for key, value in new_all.items()
               if not _is_timing_metric(key) and key[0] not in allowed}
        missing = old.keys() - new.keys()
        if missing:
            raise ValueError(f"V5 profile omits baseline numerical metrics at frame {frame}: "
                             f"{sorted(missing)[:5]}")
        extras = {key for key in new_all.keys() - old_all.keys()
                  if not _is_timing_metric(key) and key[0] not in allowed}
        if extras:
            raise ValueError(f"V5 profile adds unapproved numerical metrics at frame {frame}: "
                             f"{sorted(extras)[:5]}")
        common = old.keys() & new.keys()
        common_metrics = set(common) if common_metrics is None else common_metrics & common
        changed = [key for key in common if old[key] != new[key]]
        if changed:
            details = [(key, str(old[key]), str(new[key])) for key in sorted(changed[:5])]
            raise ValueError(f"retained/V5 numerical profile metrics differ at frame {frame}: {details}")
        current_only.update(key for key in new_all.keys() - old_all.keys()
                            if not _is_timing_metric(key))
        timing.update(key for key in old_all.keys() | new_all.keys() if _is_timing_metric(key))
        compared_values += len(common)

    stable_common = common_metrics or set()
    if not v4.HASH_METRICS.issubset(stable_common):
        raise ValueError("full profile comparison omitted hydraulic identity hash words")
    for frame in expected_frames:
        values = current[frame]
        v4._validate_marker_metrics(values, frame)
        missing_supply = {(SUPPLY_CATEGORY, name) for name in SUPPLY_METRICS} - values.keys()
        missing_observation = {(OBSERVATION_CATEGORY, name) for name in OBSERVATION_METRICS} - values.keys()
        if missing_supply or missing_observation:
            raise ValueError(f"V5 profile lacks required supply/observation metrics at frame {frame}: "
                             f"{sorted(missing_supply | missing_observation)}")
    return {
        "passed": True,
        "frames_compared": frame_count,
        "metric_values_compared": compared_values,
        "common_metric_keys": [list(key) for key in sorted(stable_common)],
        "current_only_observation_keys": [list(key) for key in sorted(current_only)],
        "excluded_categories": sorted(allowed),
        "timing_metrics_excluded": [list(key) for key in sorted(timing)],
        "identity_hash_metrics_compared": [list(key) for key in sorted(v4.HASH_METRICS)],
        "comparison": "exact Decimal equality for every common non-timing metric outside approved observational categories",
    }


def compare_response_prefix(
    baseline: dict[int, dict[tuple[str, str], Decimal]],
    response: dict[int, dict[tuple[str, str], Decimal]],
    *,
    frame_count: int = FRAME_COUNT,
    prefix_end_seconds: int = 3600,
) -> dict[str, Any]:
    """Require exact reference identity until the first response transition."""
    prefix_frames = prefix_end_seconds // v2.DT
    if (prefix_end_seconds <= 0 or prefix_end_seconds % v2.DT or
            prefix_frames > frame_count or sorted(response) != list(range(frame_count)) or
            sorted(baseline) != list(range(frame_count))):
        raise ValueError("response and retained reference do not contain the complete pre-transition prefix")
    report = compare_common_profiles(
        {frame: baseline[frame] for frame in range(prefix_frames)},
        {frame: response[frame] for frame in range(prefix_frames)},
        frame_count=prefix_frames,
    )
    for frame in range(prefix_frames, frame_count):
        old, new = baseline[frame], response[frame]
        old_numerical = {key for key in old if not _is_timing_metric(key) and
                         key[0] not in ALLOWED_OBSERVATION_CATEGORIES}
        new_numerical = {key for key in new if not _is_timing_metric(key) and
                         key[0] not in ALLOWED_OBSERVATION_CATEGORIES}
        missing = old_numerical - new_numerical
        extras = {key for key in new_numerical - old_numerical
                  if key[0] not in ALLOWED_OBSERVATION_CATEGORIES}
        if missing:
            raise ValueError(f"response profile omits retained numerical metrics at frame {frame}: "
                             f"{sorted(missing)[:5]}")
        if extras:
            raise ValueError(f"response profile adds unapproved numerical metrics at frame {frame}: "
                             f"{sorted(extras)[:5]}")
        v4._validate_marker_metrics(new, frame)
        required_observations = (
            {(SUPPLY_CATEGORY, name) for name in SUPPLY_METRICS} |
            {(OBSERVATION_CATEGORY, name) for name in OBSERVATION_METRICS}
        )
        missing_observations = required_observations - new.keys()
        if missing_observations:
            raise ValueError(f"response profile lacks required schedule/observation metrics at frame {frame}: "
                             f"{sorted(missing_observations)}")
    report["comparison_window_physical_seconds"] = [0, prefix_end_seconds]
    report["response_after_prefix"] = "hydraulic differences are allowed only after the recorded supply transition"
    return report


def validate_supply_profile(
    frames: dict[int, dict[tuple[str, str], Decimal]],
    *,
    response: bool,
    frame_count: int = FRAME_COUNT,
) -> dict[str, Any]:
    if sorted(frames) != list(range(frame_count)):
        raise ValueError("supply profile does not contain every requested fixed-step frame exactly once")
    scheduled = Decimal(0)
    maximum_schedule_error = Decimal(0)
    rates: set[Decimal] = set()
    for frame in range(frame_count):
        values = frames[frame]
        expected_start = Decimal(frame * v2.DT)
        start_key = (SUPPLY_CATEGORY, "step_start_seconds")
        rate_key = (SUPPLY_CATEGORY, "source_m3_per_s")
        volume_key = (SUPPLY_CATEGORY, "scheduled_volume_m3")
        missing = {start_key, rate_key, volume_key} - values.keys()
        if missing:
            raise ValueError(f"supply profile is missing schedule metrics at frame {frame}: {sorted(missing)}")
        actual_start = values[start_key]
        if actual_start != expected_start:
            raise ValueError(f"supply step-start time is invalid at frame {frame}: {actual_start}")
        expected_rate = _rate_at(expected_start, response)
        actual_rate = values[rate_key]
        if actual_rate != expected_rate:
            raise ValueError(f"recorded source rate differs from the frozen schedule at frame {frame}: "
                             f"expected {expected_rate}, got {actual_rate}")
        rates.add(actual_rate)
        scheduled += expected_rate * v2.DT
        actual_scheduled = values[volume_key]
        if actual_scheduled != scheduled:
            raise ValueError(f"recorded scheduled volume differs from the exact schedule at frame {frame}: "
                             f"expected {scheduled}, got {actual_scheduled}")
        maximum_schedule_error = max(maximum_schedule_error, abs(actual_scheduled - scheduled))

    if frame_count == FRAME_COUNT and scheduled != EXPECTED_FINAL_SCHEDULED_VOLUME_M3:
        raise ValueError(f"full-horizon scheduled source integral is {scheduled}, expected 720000 m3")
    return {
        "passed": True,
        "frames_checked": frame_count,
        "response_enabled": response,
        "rates_m3_per_s": sorted(str(rate) for rate in rates),
        "final_scheduled_volume_m3": str(scheduled),
        "required_final_scheduled_volume_m3": str(EXPECTED_FINAL_SCHEDULED_VOLUME_M3)
        if frame_count == FRAME_COUNT else None,
        "maximum_schedule_error_m3": str(maximum_schedule_error),
        "schedule_source": "fluid_25d.supply metrics at step_start_seconds == frame_index * DT",
    }


def validate_water_budget_profile(
    frames: dict[int, dict[tuple[str, str], Decimal]],
    *,
    response: bool,
    frame_count: int = FRAME_COUNT,
) -> dict[str, Any]:
    if sorted(frames) != list(range(frame_count)):
        raise ValueError("water profile does not contain every requested fixed-step frame exactly once")
    expected_integral = Decimal(0)
    previous_source = Decimal(0)
    previous_export = Decimal(0)
    maximum_residual = Decimal(0)
    maximum_source_error = Decimal(0)
    for frame in range(frame_count):
        values = frames[frame]
        missing = WATER_BUDGET_METRICS - values.keys()
        if missing:
            raise ValueError(f"water profile is missing required budget metrics at frame {frame}: "
                             f"{sorted(missing)}")
        rate = _rate_at(Decimal(frame * v2.DT), response)
        expected_integral += rate * v2.DT
        flags = values[(v3.SOLVER, "finite_volume_status_flags")]
        source = values[(v3.WATER, "cumulative_source_volume_m3")]
        sink = values[(v3.WATER, "cumulative_sink_volume_m3")]
        exported = values[(v3.WATER, "cumulative_boundary_outflow_volume_m3")]
        stored = values[(v3.WATER, "total_water_volume_m3")]
        reported = values[(v3.WATER, "conservation_residual_m3")]
        if flags != 0:
            raise ValueError(f"water profile has nonzero finite-volume status flags at frame {frame}")
        tolerance = max(Decimal("0.003"), expected_integral * Decimal("0.0003"))
        recomputed = stored - source + sink + exported
        if abs(source - expected_integral) > tolerance:
            raise ValueError(f"water source ledger differs from recorded supply schedule at frame {frame}")
        if sink != 0:
            raise ValueError(f"water profile has a nonzero explicit sink at frame {frame}")
        if min(source, sink, exported, stored) < 0 or source < previous_source or exported < previous_export:
            raise ValueError(f"water profile has a negative or decreasing cumulative quantity at frame {frame}")
        if abs(reported - recomputed) > CSV_RESIDUAL_AGREEMENT_TOLERANCE:
            raise ValueError(f"reported water residual disagrees with independently recomputed ledger at frame {frame}")
        if abs(reported) > tolerance or abs(recomputed) > tolerance:
            raise ValueError(f"water conservation residual exceeds the existing 3e-4 tolerance at frame {frame}")
        maximum_residual = max(maximum_residual, abs(reported), abs(recomputed))
        maximum_source_error = max(maximum_source_error, abs(source - expected_integral))
        previous_source, previous_export = source, exported
    if frame_count == FRAME_COUNT and expected_integral != EXPECTED_FINAL_SCHEDULED_VOLUME_M3:
        raise ValueError("full-horizon water source integral is not 720000 m3")
    return {
        "passed": True,
        "frames_checked": frame_count,
        "maximum_absolute_water_residual_m3": str(maximum_residual),
        "maximum_absolute_source_schedule_error_m3": str(maximum_source_error),
        "scheduled_source_integral_m3": str(expected_integral),
        "tolerance_m3": "max(0.003, cumulative scheduled source volume * 0.0003) per frame",
        "reported_ledger_agreement_tolerance_m3": str(CSV_RESIDUAL_AGREEMENT_TOLERANCE),
        "nonzero_solver_flag_frames": [],
        "explicit_sink_m3": "0",
    }


def validate_observation_profile(
    frames: dict[int, dict[tuple[str, str], Decimal]], *, frame_count: int = FRAME_COUNT,
) -> dict[str, Any]:
    if sorted(frames) != list(range(frame_count)):
        raise ValueError("observation profile does not contain every requested fixed-step frame exactly once")
    required = {(OBSERVATION_CATEGORY, name) for name in OBSERVATION_METRICS}
    final: dict[str, str] = {}
    for frame in range(frame_count):
        values = frames[frame]
        missing = required - values.keys()
        if missing:
            raise ValueError(f"hillside observation metrics are missing at frame {frame}: {sorted(missing)}")
        x_values = [values[(OBSERVATION_CATEGORY, name)] for name in
                    ("travel_cell_x", "collection_cell_x")]
        z_values = [values[(OBSERVATION_CATEGORY, name)] for name in
                    ("travel_cell_z", "collection_cell_z")]
        depth = values[(OBSERVATION_CATEGORY, "collection_depth_m")]
        if any(value < 0 or value > 511 for value in (*x_values, *z_values)) or depth < 0:
            raise ValueError(f"hillside observation metric is outside the 512x512 domain at frame {frame}")
        if frame == frame_count - 1:
            cx, cz = x_values[1], z_values[1]
            distance_squared_m2 = ((cx - 213) ** 2 + (cz - 213) ** 2) * 30 ** 2
            if depth < Decimal("0.01") or distance_squared_m2 < 2000 ** 2:
                raise ValueError("final hillside observation has no material collection beyond 2 km")
            final = {name: str(values[(OBSERVATION_CATEGORY, name)]) for name in OBSERVATION_METRICS}
    return {"passed": True, "frames_checked": frame_count, "final_observation": final,
            "interpretation": "readback observation for study context; not a camera center, forcing, route, or outlet"}


def _load_retained_baselines(app: Path) -> dict[str, Any]:
    v3_baselines = v4.load_v3_baselines()
    if Path(v3_baselines["metrics_512_water_path"]).resolve() != V3_WATER_PROFILE.resolve():
        raise ValueError("V3 baseline loader returned a different 512 water profile")

    if not V4_PROFILE_REPORT.is_file() or not V4_DYE_PROFILE.is_file():
        raise FileNotFoundError("retained V4 512 dye profile evidence is incomplete")
    profiles = read_report(V4_PROFILE_REPORT)
    if (profiles.get("schema") != "cubey.fluid25d.hillside_motion_v4.profiles" or
            profiles.get("phase_checks_passed") is not True or profiles.get("frames") != FRAME_COUNT):
        raise ValueError("retained V4 profiles report is not a passing full-horizon profile record")
    closure_path = V4_ROOT / "final-local-closure.json"
    closure = read_report(closure_path)
    closure_profile = closure.get("artifact_receipts", {}).get("profiles", {})
    if (closure_profile.get("path") != str(V4_PROFILE_REPORT.resolve()) or
            closure_profile.get("sha256") != sha256_file(V4_PROFILE_REPORT)):
        raise ValueError("retained V4 profiles report does not match its closure artifact receipt")
    case = next((record for record in profiles.get("cases", [])
                 if record.get("label") == "profile-512-dye"), None)
    receipt_path = V4_ROOT / "profiles/profile-512-dye/execution.json"
    if case is None or not receipt_path.is_file():
        raise ValueError("retained V4 dye profile case or execution receipt is missing")
    receipt = read_report(receipt_path)
    metrics_path = V4_ROOT / "profiles/profile-512-dye/profile.metrics.csv"
    if (case.get("domain") != 512 or case.get("dye") is not True or
            case.get("motion_markers") is not True or case.get("v3_full_profile_comparison", {}).get("passed") is not True or
            case.get("execution_receipt_path") != str(receipt_path.resolve()) or
            case.get("execution_receipt_sha256") != sha256_file(receipt_path) or
            case.get("profile_metrics_path") != str(metrics_path.resolve()) or
            case.get("profile_metrics_sha256") != sha256_file(metrics_path) or
            receipt.get("schema") != "cubey.fluid25d.hillside_motion_v4.execution" or
            receipt.get("label") != "profile-512-dye" or receipt.get("frames") != FRAME_COUNT or
            receipt.get("interval") != 1 or receipt.get("dye") is not True or
            receipt.get("motion_markers") is not True or receipt.get("exit_code") != 0 or
            receipt.get("before") != receipt.get("after")):
        raise ValueError("retained V4 dye profile identity, receipt, or metric artifact is malformed")
    artifact = receipt.get("artifact_hashes", {}).get(metrics_path.name)
    artifact_path = receipt.get("artifact_paths", {}).get(metrics_path.name)
    if (artifact != sha256_file(metrics_path) or artifact_path != str(metrics_path.resolve())):
        raise ValueError("retained V4 dye metrics do not match their execution artifact receipt")
    pinned = v2.pinned_inputs(512, app.resolve())
    solver_bridge = v4._solver_bridge(pinned, v3_baselines["review"])
    return {
        "v3_review_path": v3_baselines["review_path"],
        "v3_review_sha256": v3_baselines["review_sha256"],
        "v3_water_profile_path": str(V3_WATER_PROFILE.resolve()),
        "v3_water_profile_sha256": sha256_file(V3_WATER_PROFILE),
        "v3_water_profile": v3_baselines["metrics_512_water"],
        "v4_profiles_report_path": str(V4_PROFILE_REPORT.resolve()),
        "v4_profiles_report_sha256": sha256_file(V4_PROFILE_REPORT),
        "v4_dye_profile_path": str(V4_DYE_PROFILE.resolve()),
        "v4_dye_profile_sha256": sha256_file(V4_DYE_PROFILE),
        "v4_dye_profile": v3.read_profile(V4_DYE_PROFILE),
        "v4_dye_execution_receipt_path": str(receipt_path.resolve()),
        "v4_dye_execution_receipt_sha256": sha256_file(receipt_path),
        "solver_bridge": solver_bridge,
    }


def profile_arguments(
    dye: bool,
    response: bool,
    profile_prefix: Path,
    output: Path,
) -> list[str]:
    if dye and response:
        raise ValueError("supply response profiles are water-only; dye must remain off")
    args = v2.arguments(512) + ["--frames", str(FRAME_COUNT), "--capture", "png"]
    if dye:
        args += ["--fluid25d-dye-pulse-start-seconds", str(PULSE_START_SECONDS),
                 "--fluid25d-dye-pulse-duration-seconds", str(PULSE_DURATION_SECONDS)]
    args += [
        "--fluid25d-catchment-view", "transport-inspection" if dye else "water-isolation",
        "--fluid25d-hillside-camera", "source",
        "--fluid25d-natural-flow-home-pitch-radians", "-0.72",
        "--fluid25d-motion-markers", "--fluid25d-motion-marker-mode", "local",
        "--fluid25d-hillside-depth-cues",
    ]
    if response:
        args.append("--fluid25d-hillside-supply-response")
    args += ["--profile-output", str(profile_prefix.resolve()),
             "--profile-diagnostics", "--profile-diagnostic-interval", "1",
             "--output", str(output.resolve())]
    return args


def capture_arguments(
    camera: str,
    pitch: str,
    response: bool,
    depth_cues: bool,
    output: Path,
    *,
    capture: str,
    frame_count: int = FRAME_COUNT,
) -> list[str]:
    if camera not in {"source", "travel", "collection", "overview"}:
        raise ValueError("capture camera must be source, travel, collection, or overview")
    if pitch not in {"-0.72", "-1.55"}:
        raise ValueError("capture pitch must be the approved oblique or topdown angle")
    if (camera == "overview") != (pitch == "-1.55"):
        raise ValueError("overview uses the topdown angle and other camera views use the oblique angle")
    if capture not in {"png", "video"}:
        raise ValueError("capture output must be PNG or video")
    if frame_count != FRAME_COUNT:
        raise ValueError("V5 captures must advance the complete 3600-frame horizon")
    args = v2.arguments(512) + ["--frames", str(frame_count), "--capture", capture]
    if capture == "video":
        args += ["--fps", str(FPS)]
    args += ["--fluid25d-catchment-view", "water-isolation",
             "--fluid25d-hillside-camera", camera,
             "--fluid25d-natural-flow-home-pitch-radians", pitch,
             "--fluid25d-motion-markers", "--fluid25d-motion-marker-mode", "local"]
    if depth_cues:
        args.append("--fluid25d-hillside-depth-cues")
    if response:
        args.append("--fluid25d-hillside-supply-response")
    args += ["--output", str(output.resolve())]
    return args


def _profile_artifact_paths(case_dir: Path, prefix: Path, output: Path, log: Path) -> dict[str, Path]:
    return v4._profile_artifact_paths(case_dir, prefix, output, log)


def _run_command(command: list[str], log_path: Path) -> dict[str, Any]:
    return v4._run_command(command, log_path)


def _validate_artifact_receipt(
    receipt: dict[str, Any], expected_paths: dict[str, Path], *, case_dir: Path,
) -> dict[str, str]:
    hashes = receipt.get("artifact_hashes")
    paths = receipt.get("artifact_paths")
    if not isinstance(hashes, dict) or not isinstance(paths, dict) or set(hashes) != set(expected_paths) or set(paths) != set(expected_paths):
        raise ValueError("execution receipt has an incomplete or unexpected artifact map")
    checked: dict[str, str] = {}
    for name, expected_path in expected_paths.items():
        path = Path(paths[name])
        if (path.resolve() != expected_path.resolve() or path.parent.resolve() != case_dir.resolve() or
                not path.is_file() or hashes[name] != sha256_file(path)):
            raise ValueError(f"execution artifact is missing, misplaced, or changed: {name}")
        checked[name] = hashes[name]
    return checked


def _validate_profile_case(
    label: str,
    receipt: dict[str, Any],
    baselines: dict[str, Any],
) -> dict[str, Any]:
    _, dye, response = next((case for case in PROFILE_CASES if case[0] == label), (None, None, None))
    if dye is None:
        raise ValueError(f"unexpected V5 profile case: {label}")
    metrics_path = Path(receipt["artifact_paths"]["profile.metrics.csv"])
    metrics = v3.read_profile(metrics_path)
    baseline = baselines["v4_dye_profile"] if dye else baselines["v3_water_profile"]
    comparison = (compare_response_prefix(baseline, metrics) if response
                  else compare_common_profiles(baseline, metrics))
    supply = validate_supply_profile(metrics, response=response)
    water = validate_water_budget_profile(metrics, response=response)
    observation = validate_observation_profile(metrics)
    dye_summary = None
    if dye:
        filtered = {frame: {key: value for key, value in values.items()
                            if key[0] not in ALLOWED_OBSERVATION_CATEGORIES}
                    for frame, values in metrics.items()}
        dye_summary = v3.validate_dye_profile(filtered, baselines["v3_water_profile"])
        if dye_summary.get("passed") is not True:
            raise ValueError("V5 constant-dye profile fails the retained tracer budget/motion gate")
    return {
        "label": label,
        "domain": 512,
        "dye": dye,
        "supply_response": response,
        "motion_markers": True,
        "motion_marker_mode": "local",
        "hillside_depth_cues": True,
        "profile_metrics_path": str(metrics_path.resolve()),
        "profile_metrics_sha256": sha256_file(metrics_path),
        "retained_profile_comparison": comparison,
        "supply_schedule": supply,
        "water_budget": water,
        "hillside_observation": observation,
        "dye_gate": dye_summary,
    }


def _run_profile_case(
    app: Path, case_dir: Path, label: str, dye: bool, response: bool,
) -> dict[str, Any]:
    case_dir.mkdir()
    prefix, output, log_path = case_dir / "profile", case_dir / "final.png", case_dir / "child.log"
    artifacts = _profile_artifact_paths(case_dir, prefix, output, log_path)
    ensure_absent(list(artifacts.values()) + [case_dir / "execution.json"])
    before = _execution_identity(app)
    command = ["rtk", "proxy", str(app.resolve()),
               *profile_arguments(dye, response, prefix, output)]
    run = _run_command(command, log_path)
    after = _execution_identity(app)
    artifact_hashes = {name: sha256_file(path) for name, path in artifacts.items() if path.is_file()}
    receipt = {
        "schema": "cubey.fluid25d.hillside_readability_v5.execution",
        "label": label,
        "domain": 512,
        "frames": FRAME_COUNT,
        "interval": 1,
        "dye": dye,
        "supply_response": response,
        "motion_markers": True,
        "motion_marker_mode": "local",
        "hillside_depth_cues": True,
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
    if run["exit_code"] != 0:
        raise RuntimeError(f"{label} app child failed with exit code {run['exit_code']}")
    if before != after:
        raise ValueError(f"{label} app, terrain, shader, or solver identity changed during the run")
    if set(artifact_hashes) != set(artifacts):
        raise ValueError(f"{label} profile child omitted required artifacts: {sorted(set(artifacts) - set(artifact_hashes))}")
    return receipt


def profiles_phase(app: Path, out: Path, *, profile_workers: int = 1) -> dict[str, Any]:
    if profile_workers not in (1, 3):
        raise ValueError("profile workers must be 1 or 3")
    app = app.resolve()
    start_identity = _execution_identity(app)
    baselines = _load_retained_baselines(app)
    phase_dir = create_phase_directory(out, "profiles")
    report_path = phase_dir / "profiles.json"
    report: dict[str, Any] = {
        "schema": "cubey.fluid25d.hillside_readability_v5.profiles",
        "phase": "profiles",
        "phase_checks_passed": False,
        "started_before_app_identity": start_identity,
        "cases": [],
        "baseline_evidence": {key: value for key, value in baselines.items()
                              if not key.endswith("_profile")},
        "full_horizon_seconds": HORIZON_SECONDS,
        "fixed_delta_seconds": v2.DT,
        "substeps": v2.SUBSTEPS,
        "frames": FRAME_COUNT,
        "diagnostic_interval": 1,
        "reference_source_m3_per_s": v2.Q_M3_PER_S,
        "response_schedule": [[start, rate] for start, rate in RESPONSE_TRANSITIONS],
        "explicit_sink": "none",
        "rain": "none",
        "dye_pulse_seconds": [PULSE_START_SECONDS, PULSE_START_SECONDS + PULSE_DURATION_SECONDS],
        "timing_metrics": "excluded from exact retained-profile value comparisons",
        "profile_workers": profile_workers,
        "execution_claim": "independent fixed-step correctness cases; child wall times are not performance evidence",
        "opt_ins": ["--fluid25d-motion-markers", "--fluid25d-motion-marker-mode local",
                    "--fluid25d-hillside-depth-cues"],
    }
    write_report(report_path, report)
    with ThreadPoolExecutor(max_workers=profile_workers) as executor:
        # Parallelism is opt-in and affects only independent, fully pinned
        # fixed-step correctness jobs. Never use these timings as benchmarks.
        # Result checks and report assembly retain the canonical case order.
        jobs = ({label: executor.submit(_run_profile_case, app, phase_dir / label,
                                        label, dye, response)
                 for label, dye, response in PROFILE_CASES} if profile_workers > 1 else {})
        for label, dye, response in PROFILE_CASES:
            case_dir = phase_dir / label
            try:
                receipt = (jobs[label].result() if jobs else
                           _run_profile_case(app, case_dir, label, dye, response))
                if receipt["before"] != start_identity or receipt["after"] != start_identity:
                    raise ValueError(f"{label} did not use the initial app/terrain/shader identity")
                record = _validate_profile_case(label, receipt, baselines)
                record.update({
                    "execution_before": receipt["before"],
                    "execution_after": receipt["after"],
                    "execution_receipt_path": str((case_dir / "execution.json").resolve()),
                    "execution_receipt_sha256": sha256_file(case_dir / "execution.json"),
                    "execution_artifacts": receipt["artifact_hashes"],
                })
                report["cases"].append(record)
            except Exception as error:
                report["failed_case"] = {"label": label, "error": f"{type(error).__name__}: {error}"}
                if (case_dir / "execution.json").is_file():
                    report["failed_case"]["execution_receipt_path"] = str((case_dir / "execution.json").resolve())
                    report["failed_case"]["execution_receipt_sha256"] = sha256_file(case_dir / "execution.json")
                write_report(report_path, report)
                raise
            write_report(report_path, report)

    end_identity = _execution_identity(app)
    if end_identity != start_identity:
        raise ValueError("app/terrain/shader identity changed across V5 profiles")
    report.update({
        "app_path": str(app),
        "app_sha256": start_identity["app_sha256"],
        "end_input_identity": end_identity,
        "solver_bridge": baselines["solver_bridge"],
        "phase_checks_passed": len(report["cases"]) == len(PROFILE_CASES),
    })
    if report["phase_checks_passed"] is not True:
        raise ValueError("V5 profiles did not pass all three full-horizon cases")
    write_report(report_path, report)
    report["report_path"] = str(report_path.resolve())
    report["report_sha256"] = sha256_file(report_path)
    return report


def _validate_passing_profiles(out: Path, app: Path) -> tuple[dict[str, Any], Path, str]:
    report_path = out.resolve() / "profiles/profiles.json"
    report = read_report(report_path)
    identity = _execution_identity(app.resolve())
    if (report.get("schema") != "cubey.fluid25d.hillside_readability_v5.profiles" or
            report.get("phase_checks_passed") is not True or report.get("app_path") != str(app.resolve()) or
            report.get("app_sha256") != identity["app_sha256"] or
            report.get("started_before_app_identity") != identity or
            report.get("end_input_identity") != identity or len(report.get("cases", [])) != len(PROFILE_CASES)):
        raise ValueError("V5 captures require passing full-horizon profiles from the same pinned app/input identity")
    baselines = _load_retained_baselines(app)
    expected_baseline_evidence = {key: value for key, value in baselines.items()
                                  if not key.endswith("_profile")}
    if report.get("baseline_evidence") != expected_baseline_evidence:
        raise ValueError("V5 profile report no longer matches the retained V3/V4 baseline receipts")
    for (label, dye, response), record in zip(PROFILE_CASES, report["cases"], strict=True):
        case_dir = report_path.parent / label
        receipt_path = case_dir / "execution.json"
        receipt = read_report(receipt_path)
        prefix, output, log = case_dir / "profile", case_dir / "final.png", case_dir / "child.log"
        expected_command = ["rtk", "proxy", str(app.resolve()),
                            *profile_arguments(dye, response, prefix, output)]
        expected_artifacts = _profile_artifact_paths(case_dir, prefix, output, log)
        if (record.get("label") != label or record.get("dye") is not dye or
                record.get("supply_response") is not response or
                record.get("motion_marker_mode") != "local" or record.get("hillside_depth_cues") is not True or
                record.get("profile_metrics_path") != str(expected_artifacts["profile.metrics.csv"].resolve()) or
                record.get("profile_metrics_sha256") != sha256_file(expected_artifacts["profile.metrics.csv"]) or
                record.get("execution_receipt_path") != str(receipt_path.resolve()) or
                record.get("execution_receipt_sha256") != sha256_file(receipt_path) or
                receipt.get("schema") != "cubey.fluid25d.hillside_readability_v5.execution" or
                receipt.get("label") != label or receipt.get("domain") != 512 or
                receipt.get("frames") != FRAME_COUNT or receipt.get("interval") != 1 or
                receipt.get("dye") is not dye or receipt.get("supply_response") is not response or
                receipt.get("motion_markers") is not True or receipt.get("motion_marker_mode") != "local" or
                receipt.get("hillside_depth_cues") is not True or
                receipt.get("command") != expected_command or receipt.get("exit_code") != 0 or
                receipt.get("before") != identity or receipt.get("after") != identity or
                record.get("execution_before") != identity or record.get("execution_after") != identity or
                record.get("execution_artifacts") != receipt.get("artifact_hashes")):
            raise ValueError(f"V5 profile receipt is invalid or changed: {label}")
        _validate_artifact_receipt(receipt, expected_artifacts, case_dir=case_dir)
        recomputed = _validate_profile_case(label, receipt, baselines)
        for key in ("retained_profile_comparison", "supply_schedule", "water_budget", "hillside_observation", "dye_gate"):
            if record.get(key) != recomputed.get(key):
                raise ValueError(f"V5 profile validation result changed for {label}: {key}")
    return report, report_path, sha256_file(report_path)


def _escape_drawtext_text(text: str) -> str:
    expression_colon = "\x00eif-colon\x00"
    text = text.replace(r"\:", expression_colon)
    text = text.replace("\\", "\\\\").replace(":", r"\:").replace(",", r"\,")
    text = text.replace("'", r"\'")
    return text.replace(expression_colon, r"\:")


def _drawtext(text: str, y: int, *, expansion: str = "normal") -> str:
    return (f"drawtext=text='{_escape_drawtext_text(text)}':x=12:y={y}:fontsize=17:"
            f"expansion={expansion}:fontcolor=white:box=1:boxcolor=black@0.82:boxborderw=5")


def _labelled_excerpt_spec(source: Path, target: Path, case_label: str,
                           response: bool) -> dict[str, Any]:
    start_frame = EXCERPT_START_SECONDS // v2.DT - 1
    end_frame = EXCERPT_END_SECONDS // v2.DT
    frame_count = end_frame - start_frame
    if start_frame < 0 or end_frame > FRAME_COUNT or frame_count <= 0:
        raise ValueError("V5 labelled excerpt lies outside the full 7200-second horizon")
    schedule_text = "Supply: Q100 continuous" if not response else "Supply: Q100 0-60m; Q150 60-90m; Q50 90-120m"
    lines = (
        f"Hillside readability V5 | {case_label}",
        "1280x720 | 30 fps | 60x playback",
        schedule_text,
        FIXED_DEPTH_LEGEND,
        LOCAL_MARKER_CAVEAT,
        rf"Physical time %{{eif\:(n+{start_frame})*2+2\:d}} s / 7200 s",
    )
    filter_text = ",".join(_drawtext(line, 12 + 28 * index,
                                      expansion="none" if line == FIXED_DEPTH_LEGEND else "normal")
                             for index, line in enumerate(lines))
    # Trim before drawing text. The frame offset keeps the physical clock tied
    # to the original 3600-frame capture rather than restarting at zero.
    vf = (f"trim=start_frame={start_frame}:end_frame={end_frame},setpts=PTS-STARTPTS," + filter_text)
    command = [
        "rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-i", str(source.resolve()),
        "-vf", vf, "-frames:v", str(frame_count), "-map_metadata", "-1",
        "-metadata", "creation_time=1970-01-01T00:00:00Z", "-fflags", "+bitexact",
        "-flags:v", "+bitexact", "-threads", "1", "-c:v", "libx264", "-crf", "20",
        "-pix_fmt", "yuv420p", "-an", str(target.resolve()),
    ]
    return {"command": command, "filter": vf, "expected_frames": frame_count,
            "source_frame_range_half_open": [start_frame, end_frame]}


def _validate_excerpt_encoding(record: dict[str, Any], source: Path, target: Path,
                               case_label: str, response: bool) -> None:
    spec = _labelled_excerpt_spec(source, target, case_label, response)
    if any(record.get(key) != value for key, value in spec.items()):
        raise ValueError(f"labelled excerpt input, trim, clock, or encoder command differs: {case_label}")


def _encode_labelled_excerpt(source: Path, target: Path, log_path: Path, case_label: str,
                             response: bool) -> dict[str, Any]:
    spec = _labelled_excerpt_spec(source, target, case_label, response)
    ensure_absent([target])
    command = spec["command"]
    result = _run_command(command, log_path)
    result.update({
        "case": case_label,
        "path": str(target.resolve()),
        "sha256": sha256_file(target) if target.is_file() else None,
        "physical_window_seconds": [EXCERPT_START_SECONDS, EXCERPT_END_SECONDS],
        **spec,
    })
    if result["exit_code"] != 0 or result["sha256"] is None:
        raise RuntimeError(f"labelled V5 excerpt failed for {case_label}")
    result["probe"] = v4.probe_video(target, log_path.with_name(log_path.stem + ".ffprobe.log"), spec["expected_frames"])
    return result


def _validate_png(path: Path) -> None:
    # Header checks alone accept truncated files. Verify container checksums,
    # then decode every pixel before treating a capture as usable evidence.
    from PIL import Image
    try:
        with Image.open(path) as image:
            if image.format != "PNG" or image.size != (EXPECTED_WIDTH, EXPECTED_HEIGHT):
                raise ValueError("wrong format or dimensions")
            image.verify()
        with Image.open(path) as image:
            image.load()
    except (OSError, ValueError, SyntaxError, EOFError) as error:
        raise ValueError(f"capture is not a decoded {EXPECTED_WIDTH}x{EXPECTED_HEIGHT} PNG: {path}") from error


def _validate_probe_binding(record: dict[str, Any], expected_log: Path, label: str) -> None:
    v4._validate_probe_record(record, label)
    expected_receipt = expected_log.with_suffix(".json").resolve()
    if (record.get("log_path") != str(expected_log.resolve()) or
            record.get("receipt_path") != str(expected_receipt)):
        raise ValueError(f"video probe log or receipt is misplaced: {label}")
    payload = {key: value for key, value in record.items()
               if key not in {"receipt_path", "receipt_sha256"}}
    if read_report(expected_receipt) != payload:
        raise ValueError(f"video probe receipt differs from its recorded result: {label}")


def _capture_one(app: Path, case_dir: Path, label: str, camera: str, pitch: str,
                 response: bool, depth_cues: bool, media: str, supply_label: str) -> dict[str, Any]:
    case_dir.mkdir()
    suffix = ".mp4" if media == "video" else ".png"
    raw = case_dir / (label + suffix)
    log = case_dir / f"{label}.app.log"
    command = ["rtk", "proxy", str(app.resolve()),
               *capture_arguments(camera, pitch, response, depth_cues, raw,
                                  capture=media, frame_count=FRAME_COUNT)]
    before = _execution_identity(app)
    run = _run_command(command, log)
    after = _execution_identity(app)
    case: dict[str, Any] = {
        "label": label,
        "camera": camera,
        "pitch_radians": pitch,
        "supply": supply_label,
        "supply_response": response,
        "depth_cues": depth_cues,
        "motion_markers": True,
        "motion_marker_mode": "local",
        "media": media,
        "frames_advanced": FRAME_COUNT,
        "physical_horizon_seconds": HORIZON_SECONDS,
        "command": command,
        "exit_code": run["exit_code"],
        "log_path": run["log_path"],
        "log_sha256": run["log_sha256"],
        "before": before,
        "after": after,
        "raw_path": str(raw.resolve()),
        "raw_sha256": sha256_file(raw) if raw.is_file() else None,
    }
    receipt_path = case_dir / "execution.json"
    write_report(receipt_path, case)
    if run["exit_code"] != 0:
        raise RuntimeError(f"{label} capture child failed with exit code {run['exit_code']}")
    if before != after:
        raise ValueError(f"{label} capture changed app, terrain, or shader identity")
    if not raw.is_file():
        raise ValueError(f"{label} capture child omitted its output: {raw}")
    if media == "png":
        _validate_png(raw)
        write_report(receipt_path, case)
    if media == "video":
        case["raw_video_probe"] = v4.probe_video(raw, case_dir / f"{label}.raw.ffprobe.log", FRAME_COUNT)
        excerpt = _encode_labelled_excerpt(raw, case_dir / f"{label}-55-120-labelled.mp4",
                                           case_dir / f"{label}.label.log", label, response)
        case["labelled_excerpt"] = excerpt
        case["labelled_excerpt_path"] = excerpt["path"]
        case["labelled_excerpt_sha256"] = excerpt["sha256"]
        case["legend"] = FIXED_DEPTH_LEGEND
        case["marker_interpretation"] = LOCAL_MARKER_CAVEAT
        write_report(receipt_path, case)
    case["execution_receipt_path"] = str(receipt_path.resolve())
    case["execution_receipt_sha256"] = sha256_file(receipt_path)
    return case


def _validate_capture_record(case: dict[str, Any], expected: tuple[Any, ...], app: Path,
                            case_dir: Path) -> None:
    label, camera, pitch, response, depth_cues, supply_label, media = expected
    suffix = ".mp4" if media == "video" else ".png"
    expected_raw = (case_dir / (label + suffix)).resolve()
    expected_app_log = (case_dir / f"{label}.app.log").resolve()
    command = ["rtk", "proxy", str(app.resolve()),
               *capture_arguments(camera, pitch, response, depth_cues,
                                  expected_raw, capture=media)]
    if (case.get("label") != label or case.get("camera") != camera or
            case.get("pitch_radians") != pitch or case.get("supply_response") is not response or
            case.get("depth_cues") is not depth_cues or case.get("supply") != supply_label or
            case.get("media") != media or case.get("frames_advanced") != FRAME_COUNT or
            case.get("exit_code") != 0 or case.get("before") != case.get("after") or
            case.get("motion_marker_mode") != "local" or case.get("raw_path") != str(expected_raw) or
            case.get("log_path") != str(expected_app_log) or case.get("command") != command):
        raise ValueError(f"capture record does not match the frozen V5 protocol: {label}")
    expected_identity = _execution_identity(app.resolve())
    if case.get("before") != expected_identity or case.get("after") != expected_identity:
        raise ValueError(f"capture app, terrain, or shader identity differs from the passing profile: {label}")
    raw = Path(case.get("raw_path", ""))
    if not raw.is_file() or case.get("raw_sha256") != sha256_file(raw):
        raise ValueError(f"capture output is missing or changed: {label}")
    if not Path(case.get("log_path", "")).is_file() or case.get("log_sha256") != sha256_file(Path(case["log_path"])):
        raise ValueError(f"capture child log is missing or changed: {label}")
    if media == "png":
        _validate_png(raw)
    if media == "video":
        raw_probe = case.get("raw_video_probe", {})
        _validate_probe_binding(raw_probe, case_dir / f"{label}.raw.ffprobe.log", f"{label} raw video")
        if raw_probe.get("log_path") != str((case_dir / f"{label}.raw.ffprobe.log").resolve()):
            raise ValueError(f"raw video probe log is misplaced: {label}")
        if (raw_probe.get("path") != str(raw.resolve()) or
                raw_probe.get("file_sha256") != case.get("raw_sha256") or
                raw_probe.get("expected_frames") != FRAME_COUNT or raw_probe.get("frame_count") != FRAME_COUNT or
                raw_probe.get("fps") != FPS or raw_probe.get("dimensions") != [EXPECTED_WIDTH, EXPECTED_HEIGHT]):
            raise ValueError(f"raw video metadata receipt does not match the frozen cadence: {label}")
        excerpt = case.get("labelled_excerpt", {})
        expected_excerpt = (case_dir / f"{label}-55-120-labelled.mp4").resolve()
        if (excerpt.get("probe", {}).get("validated") is not True or
                excerpt.get("physical_window_seconds") != [EXCERPT_START_SECONDS, EXCERPT_END_SECONDS] or
                excerpt.get("expected_frames") != (EXCERPT_END_SECONDS - EXCERPT_START_SECONDS) // PLAYBACK_ACCELERATION * FPS + 1 or
                excerpt.get("path") != str(expected_excerpt) or
                excerpt.get("sha256") != case.get("labelled_excerpt_sha256") or
                case.get("legend") != FIXED_DEPTH_LEGEND or
                case.get("marker_interpretation") != LOCAL_MARKER_CAVEAT):
            raise ValueError(f"labelled physical-time excerpt is invalid: {label}")
        _validate_probe_binding(excerpt["probe"], case_dir / f"{label}.label.ffprobe.log", f"{label} labelled excerpt")
        if (excerpt.get("probe", {}).get("log_path") !=
                str((case_dir / f"{label}.label.ffprobe.log").resolve()) or
                excerpt.get("log_path") != str((case_dir / f"{label}.label.log").resolve())):
            raise ValueError(f"labelled excerpt probe or encoder log is misplaced: {label}")
        if not Path(excerpt.get("path", "")).is_file() or sha256_file(Path(excerpt["path"])) != excerpt.get("sha256"):
            raise ValueError(f"labelled excerpt file is missing or changed: {label}")
        if not Path(excerpt.get("log_path", "")).is_file() or sha256_file(Path(excerpt["log_path"])) != excerpt.get("log_sha256"):
            raise ValueError(f"labelled excerpt encoder log is missing or changed: {label}")
        _validate_excerpt_encoding(excerpt, raw, expected_excerpt, label, response)
        excerpt_probe = excerpt["probe"]
        if (excerpt_probe.get("path") != excerpt.get("path") or
                excerpt_probe.get("file_sha256") != excerpt.get("sha256") or
                excerpt_probe.get("expected_frames") != excerpt.get("expected_frames") or
                excerpt_probe.get("frame_count") != excerpt.get("expected_frames") or
                excerpt_probe.get("fps") != FPS or
                excerpt_probe.get("dimensions") != [EXPECTED_WIDTH, EXPECTED_HEIGHT]):
            raise ValueError(f"labelled excerpt metadata receipt does not match its artifact: {label}")


def captures_phase(app: Path, out: Path) -> dict[str, Any]:
    app = app.resolve()
    profiles, profile_path, profile_sha = _validate_passing_profiles(out, app)
    identity = _execution_identity(app)
    phase_dir = create_phase_directory(out, "captures")
    report_path = phase_dir / "captures.json"
    report: dict[str, Any] = {
        "schema": "cubey.fluid25d.hillside_readability_v5.captures",
        "phase": "captures",
        "phase_checks_passed": False,
        "profile_report_path": str(profile_path.resolve()),
        "profile_report_sha256": profile_sha,
        "started_before_app_identity": identity,
        "cases": [],
        "full_horizon_seconds": HORIZON_SECONDS,
        "frames": FRAME_COUNT,
        "fps": FPS,
        "view_matrix": "travel reference/response full videos; source/collection/overview end-state PNGs; matched source depth-cue PNG pair",
        "human_animation_and_live_gui_acceptance_pending": True,
        "observation_context_from_full_horizon_profile": next(
            record["hillside_observation"] for record in profiles["cases"]
            if record["label"] == "constant-water"),
    }
    write_report(report_path, report)
    expected_cases: list[tuple[Any, ...]] = []
    for label, camera, pitch, response, depth_cues, supply_label in VIDEO_CASES:
        expected_cases.append((label, camera, pitch, response, depth_cues, supply_label, "video"))
    for label, camera, pitch, response, depth_cues, supply_label in STILL_CASES:
        expected_cases.append((label, camera, pitch, response, depth_cues, supply_label, "png"))
    for expected in expected_cases:
        label, camera, pitch, response, depth_cues, supply_label, media = expected
        case_dir = phase_dir / label
        try:
            case = _capture_one(app, case_dir, label, camera, pitch, response,
                                depth_cues, media, supply_label)
            _validate_capture_record(case, expected, app, case_dir)
        except Exception as error:
            report["failed_case"] = {"label": label, "error": f"{type(error).__name__}: {error}"}
            failed_receipt = case_dir / "execution.json"
            if failed_receipt.is_file():
                report["failed_case"]["execution_receipt_path"] = str(failed_receipt.resolve())
                report["failed_case"]["execution_receipt_sha256"] = sha256_file(failed_receipt)
            write_report(report_path, report)
            raise
        case["execution_receipt_path"] = str((case_dir / "execution.json").resolve())
        case["execution_receipt_sha256"] = sha256_file(case_dir / "execution.json")
        report["cases"].append(case)
        write_report(report_path, report)
    end_identity = _execution_identity(app)
    if end_identity != identity or end_identity != profiles["end_input_identity"]:
        raise ValueError("app, terrain, or shader identity changed across V5 captures")
    report.update({
        "end_input_identity": end_identity,
        "phase_checks_passed": len(report["cases"]) == len(expected_cases),
    })
    if report["phase_checks_passed"] is not True:
        raise ValueError("V5 capture phase did not produce every required view")
    write_report(report_path, report)
    report["report_path"] = str(report_path.resolve())
    report["report_sha256"] = sha256_file(report_path)
    return report


def review_phase(app: Path, out: Path) -> dict[str, Any]:
    app = app.resolve()
    profiles, profile_path, profile_sha = _validate_passing_profiles(out, app)
    captures_path = out.resolve() / "captures/captures.json"
    captures = read_report(captures_path)
    expected: list[tuple[Any, ...]] = [
        (*case, "video") for case in VIDEO_CASES
    ] + [(*case, "png") for case in STILL_CASES]
    if (captures.get("schema") != "cubey.fluid25d.hillside_readability_v5.captures" or
            captures.get("phase_checks_passed") is not True or
            captures.get("profile_report_path") != str(profile_path.resolve()) or
            captures.get("profile_report_sha256") != profile_sha or
            captures.get("end_input_identity") != profiles["end_input_identity"] or
            len(captures.get("cases", [])) != len(expected)):
        raise ValueError("V5 review requires the complete capture phase from the passing profile identity")
    for case, case_spec in zip(captures["cases"], expected, strict=True):
        label, camera, pitch, response, depth_cues, supply_label, media = case_spec
        case_dir = out.resolve() / "captures" / label
        receipt_path = case_dir / "execution.json"
        if (case.get("execution_receipt_path") != str(receipt_path.resolve()) or
                case.get("execution_receipt_sha256") != sha256_file(receipt_path)):
            raise ValueError(f"V5 capture receipt is missing or changed: {label}")
        receipt = read_report(receipt_path)
        report_case = {key: value for key, value in case.items()
                       if key not in {"execution_receipt_path", "execution_receipt_sha256"}}
        if receipt != report_case:
            raise ValueError(f"V5 capture receipt differs from its complete phase record: {label}")
        _validate_capture_record(receipt, case_spec, app, case_dir)
    review_path = out.resolve() / "review.json"
    ensure_absent([review_path])
    review = {
        "schema": "cubey.fluid25d.hillside_readability_v5.review",
        "phase_checks_passed": True,
        "profiles_report_path": str(profile_path.resolve()),
        "profiles_report_sha256": profile_sha,
        "captures_report_path": str(captures_path.resolve()),
        "captures_report_sha256": sha256_file(captures_path),
        "app_path": str(app),
        "app_sha256": profiles["app_sha256"],
        "input_identity": profiles["end_input_identity"],
        "solver_bridge": profiles.get("solver_bridge"),
        "profile_case_count": len(profiles["cases"]),
        "capture_case_count": len(captures["cases"]),
        "observation_context_from_full_horizon_profile": captures.get("observation_context_from_full_horizon_profile"),
        "response_final_scheduled_volume_m3": str(EXPECTED_FINAL_SCHEDULED_VOLUME_M3),
        "limits": [
            "offscreen deterministic evidence only",
            "local markers are local movement observations, not source-released parcels",
            "hillside observation metrics choose camera context only and do not prescribe routing or a drain",
            "human animation and live GUI acceptance remain pending",
            "not calibrated hydrology or a promoted solver/default claim",
        ],
        "human_animation_and_live_gui_acceptance_pending": True,
    }
    write_report(review_path, review)
    review["report_path"] = str(review_path.resolve())
    review["report_sha256"] = sha256_file(review_path)
    return review


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("profiles", "captures", "review"), default="profiles")
    parser.add_argument("--app", type=Path, default=APP_DEFAULT)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--profile-workers", type=int, choices=(1, 3), default=1,
                        help="independent correctness cases only; never use their timings as benchmarks")
    args = parser.parse_args(argv)
    if args.phase == "profiles":
        report = profiles_phase(args.app, args.out, profile_workers=args.profile_workers)
    elif args.phase == "captures":
        report = captures_phase(args.app, args.out)
    else:
        report = review_phase(args.app, args.out)
    print(json.dumps({key: value for key, value in report.items()
                      if key not in {"cases", "baseline_evidence"}}, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
