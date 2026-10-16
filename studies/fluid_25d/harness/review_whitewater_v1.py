#!/usr/bin/env python3
"""Pinned whitewater replay evidence; no solver launch or hydraulic dispatch."""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path

import review_steep_flow_v1 as steep
from scenic_flow_reference import RETAINED_FLOW_CUES

review, ref = steep.review, steep.ref
VARIANTS = {
    "off": {"water_cascade_strength": 1},
    "both": {"water_cascade_strength": 1, "water_whitewater_strength": 1},
    "fast": {"water_cascade_strength": 1, "water_whitewater_strength": 1,
             "water_whitewater_speed": 2},
    "layer": {"water_whitewater_strength": 1},
    "network": {"water_cascade_strength": 1, "water_whitewater_strength": 1,
                "water_whitewater_speed": 2, "water_stream_foam_strength": 1},
}


def command(scene, path, tuning, *, frames=1, fps=30, advancing=False):
    args = review.command(scene, path, tuning)
    if frames > 1:
        args += ["--capture", "video", "--frames", str(frames), "--fps", str(fps),
                 "--fluid25d-recording-frame-interval-seconds",
                 str(10 if advancing else 1 / fps)]
    return args


def sources():
    result = review.sources()
    sim = ref.ROOT / "projects/fluid/sim/fluid_25d"
    paths = list(sim.glob("*whitewater*")) + list((sim / "shaders").glob("*whitewater*"))
    paths += [Path(__file__), Path(__file__).with_name("scenic_flow_reference.py"),
              ref.ROOT / "projects/fluid/fluid_25d/CMakeLists.txt"]
    for path in paths:
        if path.is_file():
            result[str(path.relative_to(ref.ROOT))] = ref.sha256_file(path)
    return result


def run(args):
    out = args.out.resolve()
    ref.reserve_directory(out)
    runtime, inputs, pins = review.runtime_identity(ref.APP), review.inputs(), sources()
    for name in pins:
        dest = out / "source" / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ref.ROOT / name, dest)
    rows = []
    for name in args.scenes:
        scene = next(s for s in review.SCENES if s[0] == name)
        for variant in args.variants:
            values = {**RETAINED_FLOW_CUES, **VARIANTS[variant]}
            if args.budget is not None:
                values["water_whitewater_budget"] = args.budget
            tuning = out / (variant + ".json")
            if not tuning.exists():
                ref.write_json_exclusive(tuning, {
                    "schema": "cubey.fluid25d.scenic-material.v2", **values})
            frames = args.fps * args.seconds if args.mode in ("motion", "advancing") else \
                120 if args.mode == "profile" else 1
            prefix = out / f"{name}-{variant}"
            path = prefix.with_suffix(".mp4" if frames > 1 else ".png")
            cmd = command(scene, path, tuning, frames=frames, fps=args.fps,
                          advancing=args.mode == "advancing")
            if args.bank is not None:
                cmd += ["--fluid25d-native-bank-view", args.bank]
            if args.mode == "profile":
                cmd += ["--profile-output", str(prefix), "--profile-warmup-frames", "12"]
            completed, receipt = ref.run_logged(cmd, out / "logs", prefix.name,
                                                timeout=240, expected_presentation="scenic")
            log = completed.stdout + completed.stderr
            ref.verify_capture_log(log, True)
            if "VUID-" in log or "Validation Error" in log:
                raise ValueError("Vulkan validation error")
            times = sorted({float(t) for t in re.findall(r"saved_s=([\d.]+)", log)})
            if not times or ((len(times) > 1) != (args.mode == "advancing")):
                raise ValueError("saved-field mode proof failed")
            row = {"scene": name, "variant": variant, "path": path.name,
                   "sha256": ref.sha256_file(path), "saved_times_s": times, "receipt": receipt}
            if args.mode in ("motion", "advancing"):
                browser = path.with_name(path.stem + "-browser.mp4")
                subprocess.run(["rtk", "proxy", "ffmpeg", "-nostdin", "-v", "error", "-ignore_editlist", "1",
                                "-i", str(path), "-an", "-c:v", "libx264", "-crf", "18", "-preset", "veryfast",
                                "-pix_fmt", "yuv420p", "-r", str(args.fps), "-frames:v", str(frames), "-movflags",
                                "+faststart", str(browser)], check=True, timeout=120)
                ref.validate_probe(ref.ffprobe_video(browser), frames, args.fps, 1280, 720)
                row.update(browser=browser.name, browser_sha256=ref.sha256_file(browser))
            if args.mode == "profile":
                row["summary"] = ref.profile_summary(prefix, 12, 108)
            rows.append(row)
            print("Captured " + str(path), flush=True)
    if runtime != review.runtime_identity(ref.APP) or inputs != review.inputs():
        raise ValueError("runtime/input changed during capture")
    for name, pin in pins.items():
        if ref.sha256_file(ref.ROOT / name) != pin:
            raise ValueError("source changed during capture: " + name)
    ref.write_json_exclusive(out / "manifest.json", {
        "runtime": runtime, "inputs": inputs, "source_files": pins, "assets": rows,
        "mode": args.mode, "fps": args.fps, "hydraulic_dispatches": 0,
        "scope": "Headless native replay; cosmetic motion is not solver advancement"})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--mode", choices=("stills", "motion", "advancing", "profile"), default="stills")
    parser.add_argument("--scenes", nargs="+", choices=[s[0] for s in review.SCENES], default=["streams", "lake", "wide"])
    parser.add_argument("--variants", nargs="+", choices=tuple(VARIANTS), default=["off", "both", "fast"])
    parser.add_argument("--fps", type=int, choices=(30, 60), default=30)
    parser.add_argument("--seconds", type=int, choices=(4, 12), default=12)
    parser.add_argument("--budget", type=int)
    parser.add_argument("--bank")
    run(parser.parse_args())
