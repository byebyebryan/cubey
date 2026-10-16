#!/usr/bin/env python3
"""Serial follow-on study phases; waits for coverage cleanup before any work.

Keeps original Debug coverage. Release is a labelled build-only experiment,
not a default change. Failed phases are retained and never replaced by replay.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import signal
import subprocess
import time

import run_mountain_rain_demo as demo

HERE = Path(__file__).resolve().parent
ROOT = demo.ref.ROOT


def write(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--study", required=True, type=Path)
    p.add_argument("--display-experiment-only", action="store_true",
                   help="wait for the primary follow-on cleanup, then compare existing 2x/1x B-spline tessellation")
    a = p.parse_args()
    study = a.study.resolve(strict=True)
    phases = study / ("display-follow-on" if a.display_experiment_only else "follow-on")
    phases.mkdir(exist_ok=False)
    stop = [False]
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: stop.__setitem__(0, True))
    deadline = json.loads((study / "campaign-start.json").read_bytes())["unix_s"] + 8 * 3600
    ready = study / ("follow-on/finished.json" if a.display_experiment_only else "coverage/display-cleanup.json")
    while not ready.exists():
        if stop[0]:
            return 130
        if time.time() >= deadline:
            write(phases / "finished.json", {"unix_s": time.time(), "status": "campaign-budget-skipped",
                                             "defaults_changed": False, "pushed": False})
            return 1
        time.sleep(1)
    tasks = []

    def run(label, argv, timeout=1800):
        if stop[0] or time.time() >= deadline:
            tasks.append({"phase": label, "status": "budget-or-interrupt-skipped"})
            write(phases / "results.json", {"phases": tasks})
            return False
        start = time.monotonic()
        with (phases / f"{label}.log").open("wb") as log:
            process = subprocess.Popen(argv, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                while process.poll() is None:
                    if stop[0] or time.time() >= deadline or time.monotonic() - start >= timeout:
                        # Python study runners unwind owned worker/viewer groups on SIGINT.
                        signal_name = signal.SIGINT if "run_live_perf_terrain_v1.py" in " ".join(argv) else signal.SIGTERM
                        import os
                        os.killpg(process.pid, signal_name)
                        try:
                            process.wait(timeout=90)
                        except subprocess.TimeoutExpired:
                            demo.stop_owned(process)
                        break
                    time.sleep(.5)
            finally:
                if process.poll() is None:
                    demo.stop_owned(process)
                process.wait()
        row = {"phase": label, "command": argv, "exit_code": process.returncode,
               "wall_s": time.monotonic() - start, "status": "completed" if process.returncode == 0 else "failed"}
        tasks.append(row)
        write(phases / "results.json", {"phases": tasks})
        print(json.dumps(row), flush=True)
        return process.returncode == 0

    python = ["rtk", "proxy", str(demo.LIVE_PYTHON), "-u"]
    runner = python + [str(HERE / "run_live_perf_terrain_v1.py"), "--study", str(study)]
    if a.display_experiment_only:
        app = ROOT / "build/release/projects/fluid/fluid_25d/fluid_25d"
        runner += ["--app", str(app)]
        name = "mountain-patch-2048-8m"
        recording = study / "coverage" / (name + "-live-0/static-recording/recording.json")
        recording.resolve(strict=True)  # no fallback to a different site or field
        for camera, repeats in (("overview", 3), ("runoff", 1)):
            for subdivision in (2, 1):
                label = f"display-{subdivision}x-{camera}"
                run(label, runner + ["--out", str(study / label), "--cases", name,
                    "--modes", "renderer-only", "--recording", str(recording), "--wall", "60",
                    "--repeats", str(repeats), "--surface-subdivision", str(subdivision),
                    "--camera", camera, "--capture"], timeout=1200)
        run("display-live-1x", runner + ["--out", str(study / "display-live-1x"), "--cases", name,
            "--modes", "live", "--surface-subdivision", "1", "--capture", "--control-probe"], timeout=1500)
        run("derive-analysis", ["rtk", "proxy", "python3", str(HERE / "analyze_live_perf_terrain_v1.py"),
            "--study", str(study)])
        run("publish-report", ["rtk", "proxy", "python3", str(HERE / "review_live_perf_terrain_v1.py"),
            "--study", str(study)])
        write(phases / "finished.json", {"unix_s": time.time(), "phases": tasks,
                                        "defaults_changed": False, "pushed": False})
        return 0
    historical = study / "cases/mountain-historical-512-30m"
    prepared = run("prepare-historical", python + [str(HERE / "prepare_live_perf_terrain_v1.py"),
        "--site", "mountain", "--configuration", "historical-512-30m", "--out", str(historical)])
    if prepared:
        run("historical-control", runner + ["--out", str(study / "historical"), "--cases", historical.name,
            "--capture", "--control-probe"], timeout=1800)
    # Build only after all baseline timings finish; do not contaminate coverage with compiler load.
    built = run("release-build", ["rtk", "proxy", "cmake", "--build", "build/release", "--target",
        "cubey_project_fluid_25d", "cubey_project_fluid_25d_external_probe", "cubey_project_fluid_25d_recording_tests",
        "cubey_project_fluid_25d_terrain_surface_tests", "-j", "6"], timeout=1800)
    if not built:
        return 1
    app = ROOT / "build/release/projects/fluid/fluid_25d/fluid_25d"
    runner += ["--app", str(app)]
    run("release-unit-tests", ["rtk", "proxy", "ctest", "--test-dir", "build/release", "-R",
        "^(fluid_25d_recording_contract|fluid_25d_terrain_surface|fluid_25d_terrain_demo_launcher)$", "--output-on-failure"])
    reference = "mountain-patch-1024-16m"
    demanding = "mountain-patch-2048-8m"
    demanding_result = study / "coverage" / (demanding + "-live-0/result.json")
    if not demanding_result.exists() or not json.loads(demanding_result.read_bytes()).get("static_recording"):
        demanding = "alpine-patch-1024-16m"
    for name in (reference, demanding):
        recording = Path(json.loads((study / "coverage" / (name + "-live-0/result.json")).read_bytes())["static_recording"])
        run(name + "-isolation", runner + ["--out", str(study / ("isolation-" + name)), "--cases", name,
            "--modes", "renderer-only", "renderer-plus-worker", "--recording", str(recording), "--wall", "60",
            "--repeats", "3", "--control-probe"], timeout=1800)
        run(name + "-release-live", runner + ["--out", str(study / ("release-" + name)), "--cases", name,
            "--repeats", "3", "--capture", "--control-probe"], timeout=5400)
    for label, viewport, rain in (("1440p", (2560, 1440), 512), ("4k", (3840, 2160), 512),
                                  ("rain1024", (1920, 1080), 1024)):
        run("stress-" + label, runner + ["--out", str(study / ("stress-" + label)), "--cases", reference,
            "--modes", "live", "--width", str(viewport[0]), "--height", str(viewport[1]), "--rain", str(rain),
            "--capture", "--control-probe"], timeout=1500)
    for name in (reference, "rolling-patch-1024-16m"):
        run("soak-" + name, runner + ["--out", str(study / ("soak-" + name)), "--cases", name,
            "--modes", "live", "--wall", "600", "--capture", "--control-probe"], timeout=1500)
    run("publish-report", ["rtk", "proxy", "python3", str(HERE / "review_live_perf_terrain_v1.py"), "--study", str(study)])
    write(phases / "finished.json", {"unix_s": time.time(), "phases": tasks,
                                    "defaults_changed": False, "pushed": False})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
