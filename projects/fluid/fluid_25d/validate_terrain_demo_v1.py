#!/usr/bin/env python3
"""Bounded, headless managed-demo trial with owned-process/resource receipts.

Uses the same launcher and renderer controls as the GUI. No native windows,
NumPy, solver imports, installation, or mutation of prepared terrain.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import subprocess
import time

import run_terrain_rain_demo as demo


def descendants(parent):
    result = subprocess.run(["rtk", "proxy", "ps", "-eo", "pid=,ppid=,comm="],
                            text=True, capture_output=True, check=True)
    rows = [line.split(maxsplit=2) for line in result.stdout.splitlines()]
    owned = {parent}
    while True:
        added = {int(pid) for pid, ppid, _ in rows if int(ppid) in owned} - owned
        if not added:
            break
        owned.update(added)
    return [(int(pid), name) for pid, _, name in rows if int(pid) in owned]


def rss(pid):
    try:
        fields = Path(f"/proc/{pid}/status").read_text().splitlines()
        return next(float(line.split()[1]) / 1024 for line in fields if line.startswith("VmRSS:"))
    except (OSError, StopIteration):
        return 0


def percentiles(values):
    values = sorted(value for value in values if math.isfinite(value))
    if not values:
        return None
    def at(fraction):
        index = (len(values) - 1) * fraction
        low = int(index)
        return values[low] + (values[min(low + 1, len(values) - 1)] - values[low]) * (index - low)
    return {"count": len(values), "p50": at(.5), "p95": at(.95), "max": values[-1]}


def uninterrupted_run(actions):
    """A control/lifecycle trial is not a solver-throughput measurement."""
    return (isinstance(actions, list) and len(actions) == 1
            and isinstance(actions[0], dict) and actions[0].get("kind") == "resume")


def csv_rows(path):
    """Very short interrupted runs may end before profile warmup completes."""
    if not path.is_file():
        return []
    with path.open() as file:
        return list(csv.DictReader(file))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--study", type=Path, required=True)
    parser.add_argument("--site-catalogue", type=Path)
    parser.add_argument("--site", default="mountain")
    parser.add_argument("--configuration", choices=demo.CONFIGS, default="patch-1024-16m")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--script", type=Path, required=True)
    parser.add_argument("--seconds", type=float, default=120)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--warmup", type=float, default=20,
                        help="exclude this many seconds after first running sample from timings")
    parser.add_argument("--no-validation", action="store_true",
                        help="explicit matched profiling override; lifecycle trials retain validation")
    parser.add_argument("--profile-sample-interval", type=int, default=10)
    demo.add_environment_arguments(parser)
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or not 1 <= args.seconds <= 3600:
        parser.error("seconds must be 1..3600")
    if not math.isfinite(args.warmup) or not 0 <= args.warmup < args.seconds:
        parser.error("warmup must be finite, nonnegative and shorter than the trial")
    continuous = uninterrupted_run(demo.managed.read_json(args.script).get("actions"))
    out = demo.demo.fresh_output(args.out)
    out.mkdir()
    trial = out / "trial"
    # Supervise Python directly so proxy signal interception cannot bypass its
    # finally block; the launcher still owns/tracks each worker and viewer group.
    command = ["python3", str(demo.HERE / "run_terrain_rain_demo.py"),
        "--study", str(args.study.resolve()), "--out", str(trial), "--headless",
        "--run-seconds", str(args.seconds), "--width", str(args.width), "--height", str(args.height),
        "--switch-script", str(args.script.resolve()), "--profile-output", str(trial / "profile")]
    command += ["--profile-sample-interval",str(args.profile_sample_interval)]
    command += ["--site", args.site, "--configuration", args.configuration]
    command += ["--environment",args.environment,"--environment-cycle-speed",str(args.environment_cycle_speed),
        "--cloud-weather",args.cloud_weather,"--cloud-quality",args.cloud_quality,
        "--cloud-shadow-strength",str(args.cloud_shadow_strength)]
    if args.environment_time_hours is not None:
        command += ["--environment-time-hours",str(args.environment_time_hours)]
    if args.clouds: command.append("--clouds")
    if args.site_catalogue:
        command += ["--site-catalogue", str(args.site_catalogue.resolve(strict=True))]
    if args.no_validation:
        command.append("--no-validation")
    samples, errors = [], []
    started = time.monotonic()
    with (out / "launcher.log").open("xb") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            while process.poll() is None:
                if time.monotonic() - started > args.seconds + 180:
                    raise TimeoutError("bounded trial exceeded startup/shutdown allowance")
                row = {"wall_s": time.monotonic() - started, "unix_s": time.time()}
                row["processes"] = [{"pid": pid, "name": name, "rss_mib": rss(pid)}
                                    for pid, name in descendants(process.pid)]
                try:
                    status = json.loads((trial / "control/status.json").read_bytes())
                    row.update(phase=status["phase"], case_id=status["case_id"])
                    state = json.loads((trial / "sessions" / status["session_name"] / "state.json").read_bytes())
                    row.update(physical_s=state["frame"]["physical_time_s"],
                        lifecycle=state["frame"]["lifecycle"],
                        publication_age_s=time.time() - state["published_unix_s"])
                except (OSError, KeyError, ValueError):
                    pass  # Startup/atomic publication is not a failed sample.
                samples.append(row)
                time.sleep(.5)
            if process.returncode:
                errors.append(f"launcher returned {process.returncode}")
        except BaseException:
            # Only the group launched above; its handler closes its owned workers.
            demo.demo.stop_owned(process, grace_s=60)
            raise
    receipt = json.loads((trial / "launch.json").read_bytes())
    if not all(item.get("active_group_remaining") == [] for item in receipt["cleanup"]):
        errors.append("owned process cleanup did not pass")
    viewer_log = (trial / "viewer.log").read_text(errors="replace")
    if "Validation Error" in viewer_log or "VUID-" in viewer_log:
        errors.append("Vulkan validation error")
    events = [json.loads(line) for line in (trial / "control/events.jsonl").read_text().splitlines()]
    running = [row for row in samples if row.get("phase") == "active"
               and row.get("lifecycle") == "running"]
    measurement_start = running[0]["unix_s"] + args.warmup if running else math.inf
    running = [row for row in running if row["unix_s"] >= measurement_start]
    pace = None
    if continuous and len(running) > 2 and len({row["case_id"] for row in running}) == 1:
        pace = ((running[-1]["physical_s"] - running[0]["physical_s"]) /
                (running[-1]["wall_s"] - running[0]["wall_s"]))
    by_frame = {}
    for row in csv_rows(trial / "profile.metrics.csv"):
        by_frame.setdefault(row["frame_index"], {})[row["category"] + "." + row["name"]] = float(row["value"])
    indices = {index for index, values in by_frame.items()
               if values.get("fluid_25d.external.lifecycle_code") == 1
               and values.get("fluid_25d.external.physical_time_s", 0) > 0
               and values.get("fluid_25d.study.command_recorded_unix_s", 0) >= measurement_start}
    metrics, passes = {}, {}
    for index in indices:
        for key, value in by_frame[index].items():
            metrics.setdefault(key, []).append(value)
    for row in csv_rows(trial / "profile.passes.csv"):
        if row["frame_index"] in indices:
            passes.setdefault(row["kind"] + ":" + row["label"], []).append(float(row["duration_ms"]))
    frames = [row for row in csv_rows(trial / "profile.frames.csv") if row["frame_index"] in indices]
    if continuous and not frames:
        errors.append("no measured running frames for throughput trial")
    result = {"command": command, "app_sha256": receipt["app_sha256"], "headless_only": True,
        "returncode": process.returncode, "errors": errors, "samples": samples, "events": events,
        "cleanup": receipt["cleanup"], "physical_wall_ratio": pace,
        "publication_age_s": percentiles([row["publication_age_s"] for row in running]),
        "metrics": {key: percentiles(value) for key, value in metrics.items()},
        "passes_ms": {key: percentiles(value) for key, value in passes.items()},
        "frame_ms": percentiles([float(row["delta_ms"]) for row in frames]),
        "viewport": sorted({(int(row["width"]), int(row["height"])) for row in frames}),
        "warmup_seconds": args.warmup,
        "profile_files_available": all((trial / f"profile.{kind}.csv").is_file()
                                       for kind in ("metrics", "passes", "frames")),
        "uninterrupted_throughput_trial": continuous,
        "profile_scope": "Running lifecycle only; mixed-grid switch trials are not steady-state performance comparisons",
        "scope": "Offscreen Vulkan with live CUDA worker; no GUI, presentation, scanout or per-process GPU accounting"}
    demo.demo.ref.write_json_exclusive(out / "validation.json", result)
    print(json.dumps({key: result[key] for key in ("returncode", "errors", "physical_wall_ratio",
          "publication_age_s", "metrics")}, indent=2))
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
