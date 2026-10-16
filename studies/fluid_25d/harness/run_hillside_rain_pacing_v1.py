#!/usr/bin/env python3
"""Short serialized offscreen-windowed rain pacing; not live GUI acceptance."""

from __future__ import annotations

import argparse
from decimal import Decimal
import json
from pathlib import Path

import run_hillside_pacing_v5 as pacing
import run_hillside_rain_v1 as study

FRAMES = 900
ADVANCE = 3600
FIELDS = (*pacing.PLAYBACK_FIELDS, "rain_enabled", "rain_applied_rate_mm_per_hour",
          "rain_cumulative_depth_m", "rain_scheduled_volume_m3")


def command(app: Path, folder: Path, markers: bool) -> list[str]:
    return ["rtk", "proxy", "env", "-u", "WAYLAND_DISPLAY", "-u", "XAUTHORITY",
            "XDG_SESSION_TYPE=x11", "xvfb-run", "-a", str(app),
            *study.arguments(study.RATES["main"], headless=False, camera="travel", markers=markers),
            "--frames", str(FRAMES), "--fluid25d-natural-flow-home-pitch-radians", "-0.72",
            "--fluid25d-presentation-time-scale", "8",
            "--fluid25d-hillside-advance-and-continue-seconds", str(ADVANCE),
            "--profile-output", str(folder / "profile")]


def validate(frames: dict, metrics: dict, passes: list, markers: bool,
             *, count: int = FRAMES, advance: int = ADVANCE, minimum_continuous: int = 200) -> dict:
    if advance <= 0 or advance % study.DT or sorted(frames) != list(range(count)) or sorted(metrics) != list(range(count)):
        raise ValueError("incomplete windowed cadence or invalid advance")
    remaining, physical = advance // study.DT, Decimal(0)
    continuous, wall, elapsed = [], Decimal(0), Decimal(0)
    rate = study.RATES["main"]
    inflow = study.AREA * rate / Decimal(3600000)
    for index in range(count):
        values = metrics[index]
        if set(values) != {(pacing.PLAYBACK_CATEGORY, name) for name in FIELDS}:
            raise ValueError("windowed rain metric fields changed")
        get = lambda name: values[(pacing.PLAYBACK_CATEGORY, name)]
        if any(not value.is_finite() for value in values.values()):
            raise ValueError("nonfinite rain pacing metric")
        steps = pacing._as_int(get("fixed_steps"), "fixed_steps")
        advancing = remaining > 0
        if (steps < 0 or steps > 16 or
                (advancing and not 0 < steps <= remaining) or (not advancing and steps > 4)):
            raise ValueError("invalid rain pacing fixed-step count")
        if advancing:
            remaining -= steps
        physical += Decimal(steps * study.DT)
        depth = rate * physical / Decimal(3600000)
        scheduled = inflow * physical
        if (get("advance_remaining_steps") != remaining or get("paused") != int(remaining > 0) or
                get("manual_supply_override") != 0 or get("dropped_backlog_frames") != 0 or
                get("requested_playback_x") != 8 or get("physical_time_s") != physical or
                get("scheduled_source_volume_m3") != 0 or
                get("rain_enabled") != 1 or get("rain_applied_rate_mm_per_hour") != rate or
                abs(get("last_step_source_m3_per_s") - inflow) > Decimal("0.001") or
                abs(get("rain_cumulative_depth_m") - depth) > Decimal("0.0000006") or
                abs(get("rain_scheduled_volume_m3") - scheduled) >
                    max(Decimal("0.003"), scheduled * Decimal("0.000001"))):
            raise ValueError(f"rain pacing state or supply integral failed at frame {index}")
        expected = Decimal(0)
        if not advancing:
            continuous.append(index)
            delta = frames[index]["delta_ms"]
            if not delta.is_finite() or delta < 0:
                raise ValueError("invalid rendered frame interval")
            wall += delta / Decimal(1000)
            elapsed += Decimal(steps * study.DT)
            expected = elapsed / wall if wall else Decimal(0)
        if abs(get("achieved_continuous_playback_x") - expected) > Decimal("0.02"):
            raise ValueError("recorded playback disagrees with rendered continuous intervals")
    if (remaining or len(continuous) <= minimum_continuous or physical <= advance or
            not Decimal("7.5") <= expected <= Decimal("8.5")):
        raise ValueError("insufficient healthy continuous playback after real advance")
    marker_stats = {}
    for label in (pacing.MARKER_UPDATE_LABEL, pacing.MARKER_DRAW_LABEL):
        rows = [row for row in passes if row["kind"] == "gpu" and row["label"] == label]
        if not markers and rows:
            raise ValueError("disabled markers still recorded GPU work")
        samples = [row["duration_ms"] for row in rows if row["frame_index"] in continuous]
        if markers and not samples:
            raise ValueError("enabled markers lack continuous GPU samples")
        if samples:
            marker_stats[label] = pacing._sample_summary(samples)
    return {"passed": True, "rendered_frames": count, "advance_seconds": advance,
            "continuous_frames": len(continuous), "final_physical_seconds": str(physical),
            "final_scheduled_input_m3": str(scheduled), "achieved_continuous_playback_x": str(expected),
            "continuous_frame_ms": pacing._sample_summary([frames[index]["delta_ms"] for index in continuous]),
            "gpu_marker_ms": marker_stats, "full_field_diagnostics": False,
            "limits": "Short serialized Xvfb runs, not an isolated marker overhead benchmark or live desktop acceptance."}


def read_data(folder: Path) -> tuple[dict, dict, list]:
    return (pacing._read_frame_csv(folder / "profile.frames.csv"),
            study.v2._read_metrics(folder / "profile.metrics.csv", decimal_values=True),
            pacing._read_passes_csv(folder / "profile.passes.csv"))


def check_execution(app: Path, case: Path, markers: bool, expected_identity: dict) -> dict:
    receipt = study.read_json(case / "execution.json")
    if (receipt["command"] != command(app, case, markers) or receipt["before"] != expected_identity or
            receipt["after"] != expected_identity or receipt["child"]["exit_code"] != 0 or
            set(receipt["artifacts"]) != {"child.log", *("profile" + suffix for suffix in pacing.ARTIFACT_SUFFIXES)}):
        raise ValueError("rain pacing execution differs")
    for name, record in receipt["artifacts"].items():
        if record["path"] != str((case / name).resolve()) or record["sha256"] != study.sha(case / name):
            raise ValueError("rain pacing artifact changed")
    return receipt


def run(app: Path, out: Path, *, resume: bool = False) -> dict:
    folder = out / "windowed-pacing" if resume else study.new_phase(out, "windowed-pacing")
    history = []
    if resume:
        old = study.read_json(folder / "pacing.json")
        if old.get("passed") is not False or old["input_identity"] != study.identity(app):
            raise ValueError("resume requires a failed unchanged pacing phase")
        retained = folder / "pacing-validation-attempt1.json"
        if retained.exists():
            raise ValueError("pacing validation history already exists; do not overwrite it")
        study.write_json(retained, old)
        history.append({"path": str(retained.resolve()), "sha256": study.sha(retained)})
    report = {"schema": "cubey.fluid25d.hillside_rain_v1.pacing", "passed": False,
              "input_identity": study.identity(app), "runs": [], "validation_history": history,
              "validator_sha256": study.sha(Path(__file__))}
    study.write_json(folder / "pacing.json", report)
    try:
        for markers in (False, True):
            label = "marker-on" if markers else "marker-off"
            case = folder / label
            if case.exists():
                if not resume:
                    raise ValueError("existing pacing execution must not be overwritten")
                # Only reuse an unchanged successful child, never a failed,
                # timed-out, incomplete or modified execution.
                check_execution(app, case, markers, report["input_identity"])
            else:
                case.mkdir()
                before = study.identity(app)
                child = pacing._run_child(command(app, case, markers), case / "child.log")
                after = study.identity(app)
                receipt = {"label": label, "command": command(app, case, markers), "child": child,
                           "before": before, "after": after,
                           "artifacts": {path.name: {"path": str(path.resolve()), "sha256": study.sha(path)}
                                         for path in case.iterdir() if path.is_file()}}
                study.write_json(case / "execution.json", receipt)
                check_execution(app, case, markers, report["input_identity"])
            summary = validate(*read_data(case), markers)
            report["runs"].append({"label": label, "validation": summary,
                                    "receipt_sha256": study.sha(case / "execution.json")})
            study.write_json(folder / "pacing.json", report)
        report["passed"] = True
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        study.write_json(folder / "pacing.json", report)
        raise
    study.write_json(folder / "pacing.json", report)
    return report


def checked(app: Path, out: Path) -> dict:
    folder = out / "windowed-pacing"
    report = study.read_json(folder / "pacing.json")
    if (report.get("passed") is not True or report["input_identity"] != study.identity(app) or
            report["validator_sha256"] != study.sha(Path(__file__)) or
            [item["label"] for item in report["runs"]] != ["marker-off", "marker-on"]):
        raise ValueError("complete unchanged rain pacing required")
    for markers, item in zip((False, True), report["runs"]):
        case = folder / item["label"]
        if study.sha(case / "execution.json") != item["receipt_sha256"]:
            raise ValueError("rain pacing receipt changed")
        check_execution(app, case, markers, report["input_identity"])
        if validate(*read_data(case), markers) != item["validation"]:
            raise ValueError("rain pacing summary differs from source data")
    for item in report["validation_history"]:
        if item["path"] != str((folder / "pacing-validation-attempt1.json").resolve()) or study.sha(Path(item["path"])) != item["sha256"]:
            raise ValueError("retained pacing validation attempt changed")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=study.v2.DEFAULT_APP)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--resume", action="store_true", help="Revalidate successful frozen child receipts from a failed phase; preserve its report.")
    options = parser.parse_args()
    try:
        result = run(options.app.resolve(), options.out.resolve(), resume=options.resume)
        print(json.dumps({"passed": result["passed"], "runs": len(result["runs"])}))
        return 0
    except Exception as error:
        print(json.dumps({"passed": False, "error": f"{type(error).__name__}: {error}"}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
