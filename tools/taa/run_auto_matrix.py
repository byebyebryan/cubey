#!/usr/bin/env python3
"""Run each project in an owned virtual X11 window with Auto and Off."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
STATUS = re.compile(r"^anti_aliasing: requested=(\w+) effective=(\w+) reason=(.+)$", re.M)
VALIDATION = re.compile(r"vulkan validation (warning|error):", re.I)


def terrain_water_fixture(output):
    # Use the same explicit recording producer as the renderer's parity test.
    sys.path.insert(0, str(ROOT / "studies/fluid_25d/harness"))
    from run_native_shoreline_raster_v1 import fixture

    manifest = fixture(output / "terrain-water-fixture", "fully-wet-lake")
    return ["--fluid25d-backend", "recording", "--fluid25d-recording", str(manifest),
            "--fluid25d-native-presentation", "scenic"]


def run_window(binary, options, mode, frames, prefix):
    command = ["xvfb-run", "-a", "-s", "-screen 0 1024x768x24", str(binary),
               "--width", "320", "--height", "180", "--frames", str(frames),
               "--require-validation", "--profile-output", str(prefix), *options]
    if mode == "off":
        command.extend(("--anti-aliasing", "off"))
    environment = dict(os.environ, XDG_SESSION_TYPE="x11", VK_LAYER_VALIDATE_SYNC="1")
    environment.pop("WAYLAND_DISPLAY", None)
    log_path = prefix.with_suffix(".log")
    with log_path.open("w") as log:
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = process.wait(timeout=120)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            code = "timeout"
    text = log_path.read_text(errors="replace")
    observed = [dict(zip(("requested", "effective", "reason"), row))
                for row in STATUS.findall(text)]
    recorded_frames = []
    frame_csv = prefix.with_suffix(".frames.csv")
    if frame_csv.is_file():
        with frame_csv.open() as stream:
            recorded_frames = [int(row["frame_index"]) for row in csv.DictReader(stream)]
    history = []
    metrics = prefix.with_suffix(".metrics.csv")
    if metrics.is_file():
        with metrics.open() as stream:
            for row in csv.DictReader(stream):
                if row["category"] == "taa" and row["name"] == "history_age":
                    history.append(int(float(row["value"])))
    launched = (code == 0 and "cubey_window_backend: x11" in text
                and recorded_frames == list(range(frames)) and bool(observed)
                and all(row["requested"] == mode for row in observed)
                and (mode != "off" or all(row["effective"] == "off" for row in observed)))
    clean = not VALIDATION.search(text)
    return {"command": command, "exit": code, "profiled_frames": len(recorded_frames),
            "status": observed, "launch_passed": launched,
            "history_rows": len(history), "maximum_history_age": max(history, default=None),
            "validation_clean": clean, "log": str(log_path), "passed": launched and clean}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/dev")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=24)
    args = parser.parse_args()
    if args.frames < 2:
        parser.error("--frames must allow a prior submitted view")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    apps = json.loads((args.build / "documented-apps.json").read_text())["applications"]
    projects = [app for app in apps if app["source"].startswith("projects/")]
    receipt = {"frames_per_launch": args.frames, "projects": []}
    for app in projects:
        binary = Path(app["binary"]).resolve(strict=True)
        options = terrain_water_fixture(output) if binary.name == "terrain_water" else []
        row = {"project": app["source"],
               "scenario": "explicit frozen recording" if options else "stock interactive",
               "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
        for mode in ("auto", "off"):
            attempt = run_window(binary, options, mode, args.frames, output / f"{binary.name}-{mode}")
            row[mode] = attempt
            print(binary.name, mode, f"launched={attempt['launch_passed']}",
                  f"validation_clean={attempt['validation_clean']}", attempt["status"], flush=True)
        receipt["projects"].append(row)
        (output / "matrix.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return 0 if all(row[mode]["passed"] for row in receipt["projects"] for mode in ("auto", "off")) else 1


if __name__ == "__main__":
    raise SystemExit(main())
