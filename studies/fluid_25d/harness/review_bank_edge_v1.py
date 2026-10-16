#!/usr/bin/env python3
"""Frozen saved-field bank edge ablations; no solver launch or default promotion."""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
from pathlib import Path

import review_whitewater_v1 as white

review, ref = white.review, white.ref
VARIANTS = {
    "baseline": {"water_bank_irregularity_m": 0, "water_bank_motion_m": 0},
    "static": {"water_bank_irregularity_m": 3, "water_bank_motion_m": 0},
    "moving": {"water_bank_irregularity_m": 3, "water_bank_motion_m": 1},
    "strong": {"water_bank_irregularity_m": 8, "water_bank_motion_m": 3},
    "bold-static": {"water_bank_irregularity_m": 24, "water_bank_motion_m": 0,
                    "water_bank_scale_m": 128, "water_bank_band_m": 64},
    "bold-moving": {"water_bank_irregularity_m": 24, "water_bank_motion_m": 12,
                    "water_bank_scale_m": 128, "water_bank_band_m": 64},
}


def command(scene, path, tuning, *, frames=1, fps=30, advancing=False, bank="bspline-2x", view="shaded"):
    cmd = white.command(scene, path, tuning, frames=frames, fps=fps, advancing=advancing)
    return cmd + ["--fluid25d-native-bank-view", bank, "--fluid25d-scenic-water-view", view]


def sources():
    pins = white.sources()
    paths = [Path(__file__), Path(__file__).with_name("fluid_25d_project_config.h")]
    paths += list((ref.ROOT / "projects/fluid/sim/fluid_25d/shaders").glob("*.vert"))
    paths += [ref.ROOT / "shaders/cubey/procedural" / name
              for name in ("noise.glsl", "random.glsl", "operators.glsl")]
    for path in paths:
        pins[str(path.relative_to(ref.ROOT))] = ref.sha256_file(path)
    return pins


def run(args):
    out = args.out.resolve()
    ref.reserve_directory(out)
    runtime, inputs, pins = review.runtime_identity(ref.APP), review.inputs(), sources()
    for name in pins:
        dest = out / "source" / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ref.ROOT / name, dest)
    rows = []
    for variant in args.variants:
        tuning = out / (variant + ".json")
        ref.write_json_exclusive(tuning, {"schema": "cubey.fluid25d.scenic-material.v2",
            "water_bank_scale_m": 48, "water_bank_band_m": 12, **VARIANTS[variant]})
        for name in args.scenes:
            scene = next(s for s in review.SCENES if s[0] == name)
            frames = args.fps * args.seconds if args.mode in ("motion", "advancing") else \
                120 if args.mode == "profile" else 1
            prefix = out / f"{name}-{variant}"
            path = prefix.with_suffix(".mp4" if frames > 1 else ".png")
            cmd = command(scene, path, tuning, frames=frames, fps=args.fps,
                          advancing=args.mode == "advancing", bank=args.bank, view=args.view)
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
                raise ValueError("saved-field advancement proof failed")
            row = {"scene": name, "variant": variant, "path": path.name,
                   "sha256": ref.sha256_file(path), "saved_times_s": times, "receipt": receipt}
            if args.mode in ("motion", "advancing"):
                browser = path.with_name(path.stem + "-browser.mp4")
                subprocess.run(["rtk", "proxy", "ffmpeg", "-nostdin", "-v", "error", "-ignore_editlist", "1",
                                "-i", str(path), "-an", "-c:v", "libx264", "-crf", "18", "-preset", "veryfast",
                                "-pix_fmt", "yuv420p", "-r", str(args.fps), "-frames:v", str(frames),
                                "-movflags", "+faststart", str(browser)], check=True, timeout=120)
                ref.validate_probe(ref.ffprobe_video(browser), frames, args.fps, 1280, 720)
                row.update(browser=browser.name, browser_sha256=ref.sha256_file(browser))
            rows.append(row)
            print("Captured " + str(path), flush=True)
    if runtime != review.runtime_identity(ref.APP) or inputs != review.inputs():
        raise ValueError("runtime/input changed during capture")
    for name, pin in pins.items():
        if ref.sha256_file(ref.ROOT / name) != pin:
            raise ValueError("source changed during capture: " + name)
    ref.write_json_exclusive(out / "manifest.json", {
        "runtime": runtime, "inputs": inputs, "source_files": pins, "assets": rows,
        "mode": args.mode, "fps": args.fps, "bank": args.bank, "view": args.view,
        "hydraulic_dispatches": 0,
        "scope": "Headless native replay, cosmetic water-edge trimming only, not simulated erosion or GUI acceptance"})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--mode", choices=("stills", "motion", "advancing", "profile"), default="stills")
    parser.add_argument("--scenes", nargs="+", choices=[s[0] for s in review.SCENES], default=["streams", "lake", "wide"])
    parser.add_argument("--variants", nargs="+", choices=tuple(VARIANTS), default=["baseline", "static", "moving"])
    parser.add_argument("--fps", type=int, choices=(30, 60), default=30)
    parser.add_argument("--seconds", type=int, choices=(4, 12, 16), default=16)
    parser.add_argument("--bank", choices=("reference", "bspline-2x"), default="bspline-2x")
    parser.add_argument("--view", choices=("shaded", "bank-edge", "no-bank-edge", "coverage", "depth-bands"), default="shaded")
    run(parser.parse_args())
