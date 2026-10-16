#!/usr/bin/env python3
"""Run bounded Xvfb windowed-pacing evidence for the V5 hillside.

This is an offscreen-windowed comparison, not desktop GUI acceptance and not
a statistically isolated marker-overhead benchmark. It serially captures a
900-render-frame response run with local motion markers off, then on.
"""

from __future__ import annotations

import argparse
import csv
from decimal import Decimal, InvalidOperation
import hashlib
import json
import math
import subprocess
import time
from pathlib import Path
from typing import Any

import run_hillside_readability_v5 as v5
import run_hillside_sustained_flow_v2 as v2

ROOT = v2.ROOT
APP_DEFAULT = v2.DEFAULT_APP
FRAME_COUNT = 900
WIDTH = 1280
HEIGHT = 720
DT = Decimal("2")
SUBSTEPS = 16
ADVANCE_SECONDS = 3550
REQUESTED_PLAYBACK_X = Decimal("8")
MAX_CONTINUOUS_STEPS_PER_FRAME = 4
PLAYBACK_CATEGORY = "fluid_25d.playback"
PLAYBACK_FIELDS = (
    "physical_time_s",
    "last_step_source_m3_per_s",
    "scheduled_source_volume_m3",
    "manual_supply_override",
    "fixed_steps",
    "paused",
    "advance_remaining_steps",
    "requested_playback_x",
    "achieved_continuous_playback_x",
    "dropped_backlog_frames",
)
MARKER_UPDATE_LABEL = "fluid_25d motion markers update"
MARKER_DRAW_LABEL = "fluid_25d motion markers draw"
ARTIFACT_SUFFIXES = (".frames.csv", ".passes.csv", ".metrics.csv", ".trace.json", ".summary.txt")
REQUIRED_SUFFIXES = (".frames.csv", ".passes.csv", ".metrics.csv")
CHILD_TIMEOUT_SECONDS = 600
DECIMAL_TOLERANCE = Decimal("0.000003")
PLAYBACK_RECOMPUTE_TOLERANCE = Decimal("0.02")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _as_decimal(value: Any, description: str) -> Decimal:
    try:
        number = Decimal(str(value))
    except (InvalidOperation, TypeError, ValueError) as error:
        raise ValueError(f"malformed {description}: {value!r}") from error
    if not number.is_finite():
        raise ValueError(f"nonfinite {description}: {value!r}")
    return number


def _as_int(value: Any, description: str) -> int:
    number = _as_decimal(value, description)
    if number != number.to_integral_value():
        raise ValueError(f"nonintegral {description}: {value!r}")
    return int(number)


def _read_frame_csv(path: Path, expected_frame_count: int = FRAME_COUNT) -> dict[int, dict[str, Any]]:
    rows: dict[int, dict[str, Any]] = {}
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"frame_index", "delta_ms", "width", "height"}
        if reader.fieldnames is None or not required.issubset(reader.fieldnames):
            raise ValueError(f"invalid or incomplete windowed frame CSV header: {path}")
        for row in reader:
            frame = _as_int(row["frame_index"], "frame index")
            if frame < 0 or frame >= expected_frame_count or frame in rows:
                raise ValueError(f"duplicate or out-of-range rendered frame: {frame}")
            delta_ms = _as_decimal(row["delta_ms"], f"frame {frame} delta_ms")
            width = _as_int(row["width"], f"frame {frame} width")
            height = _as_int(row["height"], f"frame {frame} height")
            if delta_ms < 0:
                raise ValueError(f"negative frame interval at frame {frame}")
            if (width, height) != (WIDTH, HEIGHT):
                raise ValueError(f"unexpected frame dimensions at frame {frame}: {width}x{height}")
            rows[frame] = {"frame_index": frame, "delta_ms": delta_ms,
                           "width": width, "height": height}
    if sorted(rows) != list(range(expected_frame_count)):
        raise ValueError(f"rendered frame CSV does not contain every frame 0..{expected_frame_count - 1}")
    return rows


def _read_playback_metrics(path: Path, expected_frame_count: int = FRAME_COUNT) -> dict[int, dict[str, Decimal]]:
    rows: dict[int, dict[str, Decimal]] = {}
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"frame_index", "category", "name", "value"}
        if reader.fieldnames is None or not required.issubset(reader.fieldnames):
            raise ValueError(f"invalid or incomplete playback metrics CSV header: {path}")
        for row in reader:
            if row["category"] != PLAYBACK_CATEGORY:
                raise ValueError(f"unexpected windowed metric category: {row['category']!r}")
            name = row["name"]
            if name not in PLAYBACK_FIELDS:
                raise ValueError(f"unexpected playback metric: {name!r}")
            frame = _as_int(row["frame_index"], "playback metric frame index")
            if frame < 0 or frame >= expected_frame_count:
                raise ValueError(f"out-of-range playback metric frame: {frame}")
            values = rows.setdefault(frame, {})
            if name in values:
                raise ValueError(f"duplicate playback metric {name!r} at frame {frame}")
            values[name] = _as_decimal(row["value"], f"frame {frame} {name}")
    if sorted(rows) != list(range(expected_frame_count)):
        raise ValueError(f"playback metrics do not contain every frame 0..{expected_frame_count - 1}")
    expected_fields = set(PLAYBACK_FIELDS)
    for frame, values in rows.items():
        if set(values) != expected_fields:
            missing = sorted(expected_fields - values.keys())
            extra = sorted(values.keys() - expected_fields)
            raise ValueError(f"incomplete playback fields at frame {frame}; missing={missing}, extra={extra}")
    return rows


def _read_passes_csv(path: Path, expected_frame_count: int = FRAME_COUNT) -> list[dict[str, Any]]:
    passes: list[dict[str, Any]] = []
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"frame_index", "kind", "label", "start_ms", "duration_ms"}
        if reader.fieldnames is None or not required.issubset(reader.fieldnames):
            raise ValueError(f"invalid or incomplete GPU/CPU pass CSV header: {path}")
        for row in reader:
            frame = _as_int(row["frame_index"], "pass frame index")
            if frame < 0 or frame >= expected_frame_count:
                raise ValueError(f"out-of-range profiled pass frame: {frame}")
            if row["kind"] not in {"cpu", "gpu"} or not row["label"]:
                raise ValueError(f"malformed profiled pass at frame {frame}")
            start = _as_decimal(row["start_ms"], f"pass frame {frame} start_ms")
            duration = _as_decimal(row["duration_ms"], f"pass frame {frame} duration_ms")
            if start < 0 or duration < 0:
                raise ValueError(f"negative profiled pass timing at frame {frame}")
            passes.append({"frame_index": frame, "kind": row["kind"], "label": row["label"],
                           "start_ms": start, "duration_ms": duration})
    return passes


def _rate_at_step_start(step_start_seconds: Decimal) -> Decimal:
    """Independent Q100/Q150/Q50/Q100 half-open schedule for this run."""
    if step_start_seconds < Decimal("3600"):
        return Decimal("100")
    if step_start_seconds < Decimal("5400"):
        return Decimal("150")
    if step_start_seconds < Decimal("7200"):
        return Decimal("50")
    return Decimal("100")


def _percentile(samples: list[Decimal], fraction: Decimal) -> Decimal:
    if not samples:
        raise ValueError("cannot summarize an empty sample set")
    ordered = sorted(samples)
    if fraction == Decimal("0.5") and len(ordered) % 2 == 0:
        middle = len(ordered) // 2
        return (ordered[middle - 1] + ordered[middle]) / Decimal("2")
    rank = max(0, math.ceil(float(fraction * len(ordered))) - 1)
    return ordered[rank]


def _sample_summary(samples: list[Decimal]) -> dict[str, Any]:
    return {
        "samples": len(samples),
        "median_ms": str(_percentile(samples, Decimal("0.5"))),
        "p95_ms": str(_percentile(samples, Decimal("0.95"))),
    }


def validate_windowed_data(
    frames: dict[int, dict[str, Any]],
    metrics: dict[int, dict[str, Decimal]],
    passes: list[dict[str, Any]],
    *,
    markers_enabled: bool,
    expected_frame_count: int = FRAME_COUNT,
    advance_seconds: int = ADVANCE_SECONDS,
    minimum_continuous_frames: int = 200,
    minimum_final_physical_seconds: Decimal = Decimal("3602"),
    playback_bounds: tuple[Decimal, Decimal] = (Decimal("7.5"), Decimal("8.5")),
) -> dict[str, Any]:
    """Validate render cadence, windowed pacing, and the independent supply ledger."""
    if advance_seconds <= 0 or Decimal(advance_seconds) % DT != 0:
        raise ValueError("advance duration must be a positive whole number of fixed steps")
    expected_frames = list(range(expected_frame_count))
    if sorted(frames) != expected_frames or sorted(metrics) != expected_frames:
        raise ValueError("windowed frame and playback CSVs must contain each requested frame exactly once")

    expected_advance_steps = int(Decimal(advance_seconds) / DT)
    remaining = expected_advance_steps
    physical_time = Decimal(0)
    expected_scheduled_volume = Decimal(0)
    last_source_rate = Decimal("100")
    continuous_indices: list[int] = []
    first_zero_remaining_frame: int | None = None
    continuous_wall_seconds = Decimal(0)
    continuous_physical_seconds = Decimal(0)

    for frame_index in expected_frames:
        values = metrics[frame_index]
        if set(values) != set(PLAYBACK_FIELDS):
            raise ValueError(f"frame {frame_index} does not have exactly the required playback metrics")
        if frames[frame_index].get("frame_index") != frame_index:
            raise ValueError(f"frame CSV row index mismatch at {frame_index}")
        delta_ms = _as_decimal(frames[frame_index].get("delta_ms"), f"frame {frame_index} delta_ms")
        fixed_steps = _as_int(values["fixed_steps"], f"frame {frame_index} fixed_steps")
        if fixed_steps < 0 or fixed_steps > SUBSTEPS:
            raise ValueError(f"invalid fixed-step count at frame {frame_index}: {fixed_steps}")

        advance_before = remaining
        if advance_before > 0:
            if fixed_steps <= 0 or fixed_steps > advance_before:
                raise ValueError(f"advance made no progress or overshot at frame {frame_index}")
            remaining -= fixed_steps
        elif fixed_steps > MAX_CONTINUOUS_STEPS_PER_FRAME:
            raise ValueError(f"continuous pacing exceeded four-step catch-up cap at frame {frame_index}")

        if _as_int(values["advance_remaining_steps"], f"frame {frame_index} remaining advance") != remaining:
            raise ValueError(f"advance_remaining_steps is inconsistent at frame {frame_index}")
        if values["paused"] != (Decimal(1) if remaining > 0 else Decimal(0)):
            raise ValueError(f"paused state disagrees with advance/continue phase at frame {frame_index}")
        if values["manual_supply_override"] != 0:
            raise ValueError(f"manual supply override was enabled at frame {frame_index}")
        if values["requested_playback_x"] != REQUESTED_PLAYBACK_X:
            raise ValueError(f"requested playback changed at frame {frame_index}")
        if values["dropped_backlog_frames"] != 0:
            raise ValueError(f"dropped pacing backlog at frame {frame_index}")

        for _ in range(fixed_steps):
            step_rate = _rate_at_step_start(physical_time)
            expected_scheduled_volume += step_rate * DT
            last_source_rate = step_rate
            physical_time += DT
        if abs(values["physical_time_s"] - physical_time) > DECIMAL_TOLERANCE:
            raise ValueError(f"physical time does not reconcile with fixed steps at frame {frame_index}")
        if abs(values["scheduled_source_volume_m3"] - expected_scheduled_volume) > DECIMAL_TOLERANCE:
            raise ValueError(f"recorded supply integral differs from independent schedule at frame {frame_index}")
        if values["last_step_source_m3_per_s"] != last_source_rate:
            raise ValueError(f"last-step source rate disagrees with half-open schedule at frame {frame_index}")

        if advance_before > 0:
            expected_achieved = Decimal(0)
            if remaining == 0 and first_zero_remaining_frame is None:
                # This zero row completed the explicit advance; it is not a
                # continuous-playback sample because the app still marked the
                # frame as an inspection-advance frame while recording it.
                first_zero_remaining_frame = frame_index
        else:
            continuous_indices.append(frame_index)
            continuous_wall_seconds += delta_ms / Decimal(1000)
            continuous_physical_seconds += Decimal(fixed_steps) * DT
            expected_achieved = (continuous_physical_seconds / continuous_wall_seconds
                                 if continuous_wall_seconds > 0 else Decimal(0))
        if abs(values["achieved_continuous_playback_x"] - expected_achieved) > PLAYBACK_RECOMPUTE_TOLERANCE:
            raise ValueError(f"achieved playback does not reconcile with continuous frames at frame {frame_index}")

    if remaining != 0 or first_zero_remaining_frame is None:
        raise ValueError("the full configured advance did not finish within the captured frames")
    if continuous_indices != list(range(first_zero_remaining_frame + 1, expected_frame_count)):
        raise ValueError("automatic continuous frames must begin after the last advance frame and remain contiguous")
    if len(continuous_indices) <= minimum_continuous_frames:
        raise ValueError(f"only {len(continuous_indices)} continuous frames; need more than {minimum_continuous_frames}")
    final_metrics = metrics[expected_frame_count - 1]
    if physical_time <= minimum_final_physical_seconds:
        raise ValueError(f"physical time did not advance beyond {minimum_final_physical_seconds} s")
    if last_source_rate != Decimal("150"):
        raise ValueError("captured response did not finish with the scheduled Q150 source rate")
    final_playback = final_metrics["achieved_continuous_playback_x"]
    if not playback_bounds[0] <= final_playback <= playback_bounds[1]:
        raise ValueError(f"final achieved playback {final_playback}x is outside {playback_bounds}")

    continuous_start = continuous_indices[0]
    delta_samples = [frames[index]["delta_ms"] for index in continuous_indices]
    marker_stats: dict[str, Any] = {}
    marker_rows = [row for row in passes
                   if row["kind"] == "gpu" and
                   row["label"] in {MARKER_UPDATE_LABEL, MARKER_DRAW_LABEL}]
    if markers_enabled:
        for label in (MARKER_UPDATE_LABEL, MARKER_DRAW_LABEL):
            samples = [row["duration_ms"] for row in marker_rows
                       if row["label"] == label and row["frame_index"] >= continuous_start]
            if not samples:
                raise ValueError(f"missing continuous GPU marker timing samples for {label!r}")
            marker_stats[label] = _sample_summary(samples)
    elif marker_rows:
        raise ValueError("GPU marker passes were recorded even though marker rendering was disabled")

    return {
        "passed": True,
        "rendered_frames": expected_frame_count,
        "resolution": [WIDTH, HEIGHT],
        "advance_seconds": advance_seconds,
        "advance_steps": expected_advance_steps,
        "last_advance_frame_index": first_zero_remaining_frame,
        "continuous_start_frame_index": continuous_start,
        "continuous_frames": len(continuous_indices),
        "continuous_interval_ms": _sample_summary(delta_samples),
        "final_physical_time_s": str(physical_time),
        "final_last_step_source_m3_per_s": str(last_source_rate),
        "final_scheduled_source_volume_m3": str(expected_scheduled_volume),
        "final_achieved_continuous_playback_x": str(final_playback),
        "dropped_backlog_frames": 0,
        "manual_supply_override": False,
        "requested_playback_x": str(REQUESTED_PLAYBACK_X),
        "gpu_marker_timing_ms": marker_stats,
        "marker_scopes_filtered_to_continuous_frames": True,
        "claim_boundary": (
            "short serialized Xvfb windowed runs; measurements are not a statistically isolated "
            "marker-overhead estimate and do not establish desktop GUI acceptance"
        ),
    }


def windowed_arguments(markers_enabled: bool, profile_prefix: Path) -> list[str]:
    args = v2.arguments(512)
    if args.count("--headless") != 1:
        raise ValueError("frozen V2 arguments must provide exactly one headless switch to remove")
    args.remove("--headless")
    args += [
        "--frames", str(FRAME_COUNT),
        "--fluid25d-catchment-view", "water-isolation",
        "--fluid25d-hillside-camera", "source",
        "--fluid25d-natural-flow-home-pitch-radians", "-0.72",
        "--fluid25d-presentation-time-scale", str(REQUESTED_PLAYBACK_X),
        "--fluid25d-hillside-advance-and-continue-seconds", str(ADVANCE_SECONDS),
        "--fluid25d-hillside-depth-cues", "--fluid25d-hillside-supply-response",
        "--profile-output", str(profile_prefix.resolve()),
    ]
    if markers_enabled:
        args += ["--fluid25d-motion-markers", "--fluid25d-motion-marker-mode", "local"]
    return args


def launcher_command(app: Path, markers_enabled: bool, profile_prefix: Path) -> list[str]:
    return [
        "rtk", "proxy", "env", "-u", "WAYLAND_DISPLAY", "-u", "XAUTHORITY",
        "XDG_SESSION_TYPE=x11", "xvfb-run", "-a", str(app.resolve()),
        *windowed_arguments(markers_enabled, profile_prefix),
    ]


def _write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def _run_child(command: list[str], log_path: Path) -> dict[str, Any]:
    start = time.monotonic()
    try:
        completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                                   timeout=CHILD_TIMEOUT_SECONDS, check=False)
        log_path.write_text(completed.stdout + ("\n--- stderr ---\n" if completed.stderr else "") +
                            completed.stderr)
        return {"exit_code": completed.returncode, "timed_out": False,
                "wall_seconds": time.monotonic() - start}
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
        log_path.write_text(str(stdout) + ("\n--- stderr ---\n" if stderr else "") + str(stderr))
        return {"exit_code": None, "timed_out": True,
                "timeout_seconds": CHILD_TIMEOUT_SECONDS,
                "wall_seconds": time.monotonic() - start}
    except OSError as error:
        log_path.write_text(f"failed to launch child: {error}\n")
        return {"exit_code": None, "timed_out": False,
                "launch_error": str(error), "wall_seconds": time.monotonic() - start}


def _artifact_map(prefix: Path, log_path: Path) -> dict[str, Path]:
    return {"child.log": log_path,
            **{prefix.name + suffix: prefix.with_suffix(suffix) for suffix in ARTIFACT_SUFFIXES}}


def require_stable_identity(before: dict[str, Any], after: dict[str, Any]) -> None:
    if not before or before != after:
        raise ValueError("app, terrain, shader, recipe, or solver-source identity changed during the run")


def require_windowed_artifacts(paths: dict[str, Path], prefix: Path) -> None:
    required_names = {"child.log", *(prefix.name + suffix for suffix in REQUIRED_SUFFIXES)}
    missing = [name for name in sorted(required_names) if name not in paths or not paths[name].is_file()]
    if missing:
        raise FileNotFoundError("windowed run omitted required artifacts: " + ", ".join(missing))


def require_comparable_run_identities(runs: list[dict[str, Any]]) -> dict[str, Any]:
    if not runs:
        raise ValueError("windowed comparison has no completed runs")
    reference = runs[0].get("before")
    if not isinstance(reference, dict) or not reference:
        raise ValueError("first windowed run has no pinned execution identity")
    for run in runs:
        label = run.get("label", "unknown")
        if run.get("passed") is not True:
            raise ValueError(f"windowed run {label} did not pass its individual evidence gate")
        if run.get("before") != reference or run.get("after") != reference:
            raise ValueError(f"windowed run {label} identity differs from the comparison identity")
    return reference


def _run_case(app: Path, case_dir: Path, markers_enabled: bool) -> dict[str, Any]:
    case_dir.mkdir()
    label = "marker-on" if markers_enabled else "marker-off"
    prefix = case_dir / f"windowed-{label}"
    log_path = case_dir / "child.log"
    receipt_path = case_dir / "execution.json"
    paths = _artifact_map(prefix, log_path)
    if any(path.exists() for path in [*paths.values(), receipt_path]):
        raise FileExistsError(f"refusing to overwrite existing {label} evidence in {case_dir}")

    command = launcher_command(app, markers_enabled, prefix)
    before: dict[str, Any] | None = None
    after: dict[str, Any] | None = None
    child: dict[str, Any] | None = None
    validation: dict[str, Any] = {"passed": False}
    failure: str | None = None
    try:
        before = v5._execution_identity(app)
        child = _run_child(command, log_path)
        after = v5._execution_identity(app)
        require_stable_identity(before, after)
        if child["exit_code"] != 0:
            raise ValueError(f"windowed child failed or timed out: {child}")
        require_windowed_artifacts(paths, prefix)
        frames = _read_frame_csv(prefix.with_suffix(".frames.csv"))
        metrics = _read_playback_metrics(prefix.with_suffix(".metrics.csv"))
        passes = _read_passes_csv(prefix.with_suffix(".passes.csv"))
        validation = validate_windowed_data(frames, metrics, passes,
                                            markers_enabled=markers_enabled)
    except Exception as error:
        failure = f"{type(error).__name__}: {error}"

    artifact_paths = {name: str(path.resolve()) for name, path in paths.items() if path.is_file()}
    artifact_hashes = {name: sha256_file(paths[name]) for name in artifact_paths}
    receipt = {
        "schema": "cubey.fluid25d.hillside_pacing_v5.execution",
        "label": label,
        "markers_enabled": markers_enabled,
        "marker_mode": "local" if markers_enabled else "disabled",
        "depth_cues": True,
        "supply_response": True,
        "frames": FRAME_COUNT,
        "resolution": [WIDTH, HEIGHT],
        "fixed_delta_seconds": str(DT),
        "substeps": SUBSTEPS,
        "camera": "source",
        "camera_pitch_radians": "-0.72",
        "requested_playback_x": str(REQUESTED_PLAYBACK_X),
        "advance_seconds": ADVANCE_SECONDS,
        "command": command,
        "before": before,
        "after": after,
        "identity_unchanged": before is not None and after is not None and before == after,
        "child": child,
        "artifact_paths": artifact_paths,
        "artifact_hashes": artifact_hashes,
        "validation": validation,
        "passed": failure is None and validation.get("passed") is True,
        "failure": failure,
    }
    _write_json(receipt_path, receipt)
    receipt["execution_receipt_path"] = str(receipt_path.resolve())
    receipt["execution_receipt_sha256"] = sha256_file(receipt_path)
    return receipt


def run_windowed_pacing(out: Path, app: Path = APP_DEFAULT) -> dict[str, Any]:
    out = out.resolve()
    app = app.resolve()
    retained_roots = (v5.V3_ROOT.resolve(), v5.V4_ROOT.resolve(), v5.V4_ROOT.parent.resolve())
    if any(out == root or root in out.parents for root in retained_roots):
        raise ValueError("pacing output must not be inside retained V3 or V4 evidence")
    if not app.is_file():
        raise FileNotFoundError(f"missing Fluid app: {app}")
    out.mkdir(parents=True, exist_ok=True)
    phase_dir = out / "windowed-pacing"
    phase_dir.mkdir()

    runs: list[dict[str, Any]] = []
    for markers_enabled in (False, True):
        case_dir = phase_dir / ("marker-on" if markers_enabled else "marker-off")
        result = _run_case(app, case_dir, markers_enabled)
        runs.append(result)
        if not result["passed"]:
            report = {
                "schema": "cubey.fluid25d.hillside_pacing_v5",
                "passed": False,
                "human_gui_acceptance": "pending; this phase uses Xvfb and is offscreen-windowed evidence only",
                "runs": runs,
            }
            _write_json(phase_dir / "windowed-pacing.json", report)
            return report

    try:
        comparison_identity = require_comparable_run_identities(runs)
    except ValueError as error:
        report = {
            "schema": "cubey.fluid25d.hillside_pacing_v5",
            "passed": False,
            "app": str(app),
            "app_sha256": runs[0].get("before", {}).get("app_sha256"),
            "execution_identity": runs[0].get("before"),
            "identity_comparison_failure": str(error),
            "serialized_order": ["marker-off", "marker-on"],
            "run_count": len(runs),
            "runs": runs,
        }
        _write_json(phase_dir / "windowed-pacing.json", report)
        return report

    report = {
        "schema": "cubey.fluid25d.hillside_pacing_v5",
        "passed": True,
        "app": str(app),
        "app_sha256": comparison_identity["app_sha256"],
        "execution_identity": comparison_identity,
        "serialized_order": ["marker-off", "marker-on"],
        "run_count": len(runs),
        "frames_per_run": FRAME_COUNT,
        "resolution": [WIDTH, HEIGHT],
        "fixed_delta_seconds": str(DT),
        "substeps": SUBSTEPS,
        "camera": "source",
        "camera_pitch_radians": "-0.72",
        "presentation_time_scale": str(REQUESTED_PLAYBACK_X),
        "supply_response": "Q100 before 3600 s, Q150 on [3600,5400), Q50 on [5400,7200), Q100 thereafter",
        "human_gui_acceptance": "pending; this phase uses Xvfb and is offscreen-windowed evidence only",
        "claim_boundary": (
            "short serialized runs are not a statistically isolated marker-overhead estimate; "
            "historical V4 measurements are not promoted as V5 evidence"
        ),
        "runs": runs,
    }
    _write_json(phase_dir / "windowed-pacing.json", report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True,
                        help="existing/new V5 evidence root; windowed-pacing must be absent")
    parser.add_argument("--app", type=Path, default=APP_DEFAULT)
    options = parser.parse_args()
    report = run_windowed_pacing(options.out, options.app)
    print(json.dumps({"passed": report["passed"],
                      "report": str((options.out.resolve() / "windowed-pacing/windowed-pacing.json")),
                      "runs": len(report["runs"])}, sort_keys=True))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
