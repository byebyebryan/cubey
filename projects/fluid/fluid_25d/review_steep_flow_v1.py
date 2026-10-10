#!/usr/bin/env python3
"""Pinned held/advancing replay evidence for opt-in steep-flow appearance."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path

import review_water_agitation_v1 as review
from scenic_flow_reference import RETAINED_FLOW_CUES

ref = review.ref
VARIANTS = {
    "off": {},
    "rapid": {"water_rapid_strength": 1, "water_rapid_scale_m": 192},
    "rapid-large": {"water_rapid_strength": 1, "water_rapid_scale_m": 384},
    "cascade-only": {"water_cascade_strength": 1},
    "cascade": {"water_cascade_strength": 1, "water_landing_strength": 1},
    "landing-only": {"water_landing_strength": 1},
}


def command(scene, path, tuning=None, *, frames=1, interval=1 / 30, view="shaded"):
    args = review.command(scene, path, tuning)
    args += ["--fluid25d-scenic-water-view", view]
    if frames > 1:
        args += ["--capture", "video", "--frames", str(frames), "--fps", "30",
                 "--fluid25d-recording-frame-interval-seconds", str(interval)]
    return args


def capture(args):
    phase = args.out / args.phase
    ref.reserve_directory(phase)
    inputs, runtime, sources = review.inputs(), review.runtime_identity(ref.APP), review.sources()
    for path in (Path(__file__), Path(__file__).with_name("fluid_25d_project_config.h"),
                 Path(__file__).with_name("CMakeLists.txt"),
                 Path(__file__).with_name("test_steep_flow_gpu.py"),
                 Path(__file__).with_name("scenic_flow_reference.py"),
                 ref.ROOT / "shaders/cubey/procedural/noise.glsl",
                 ref.ROOT / "shaders/cubey/procedural/random.glsl",
                 ref.ROOT / "shaders/cubey/procedural/operators.glsl"):
        sources[str(path.relative_to(ref.ROOT))] = ref.sha256_file(path)
    for name in sources:
        dest = phase / "source" / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ref.ROOT / name, dest)
    rows = []
    variants = ("off",) if args.phase == "baseline" else args.variants
    for variant in variants:
        tuning = phase / (variant + ".json")
        ref.write_json_exclusive(tuning, {"schema": "cubey.fluid25d.scenic-material.v2",
                                         **RETAINED_FLOW_CUES, **VARIANTS[variant]})
        scenes = review.SCENES[:2] if args.phase == "baseline" else review.SCENES[:3]
        for scene in scenes:
            views = ("shaded",)
            if args.phase == "mask":
                views = ("rapid-activity", "depth-bands", "film-weight")
            elif args.phase == "stills" and scene[0] == "streams" and variant == "rapid":
                views = ("shaded", "rapid-foam", "no-rapid-foam", "direct-only", "coverage")
            elif args.phase == "stills" and variant == "cascade":
                views = ("shaded", "cascade-weight", "landing-activity", "no-landing-foam")
            for view in views:
                frames = 360 if args.phase in ("baseline", "motion", "advancing") else 1
                # 12 capture seconds / 12 requested physical seconds holds rain512
                # and the lake's sparse saved fields. Advancing is explicit below.
                interval = 10 if args.advancing else 1 / 30
                suffix = ".mp4" if frames > 1 else ".png"
                path = phase / (variant + "-" + scene[0] + "-" + view + suffix)
                completed, receipt = ref.run_logged(
                    command(scene, path, tuning, frames=frames, interval=interval, view=view),
                    phase / "logs", path.stem, timeout=240, expected_presentation="scenic")
                log = completed.stdout + completed.stderr
                ref.verify_capture_log(log, True)
                receipt["capture_log_validated"] = True
                if "VUID-" in log or "Validation Error" in log:
                    raise ValueError("Vulkan validation error")
                times = sorted({float(t) for t in re.findall(r"saved_s=([\d.]+)", log)})
                if not times or (not args.advancing and len(times) != 1):
                    raise ValueError("held saved-field proof failed")
                if args.advancing and len(times) < 2:
                    raise ValueError("advancing replay did not advance saved fields")
                browser = None
                if frames > 1:
                    browser = path.with_name(path.stem + "-browser.mp4")
                    subprocess.run(["rtk", "proxy", "ffmpeg", "-nostdin", "-v", "error",
                                    "-ignore_editlist", "1", "-i", str(path), "-an", "-c:v",
                                    "libx264", "-crf", "18", "-preset", "veryfast", "-pix_fmt",
                                    "yuv420p", "-r", "30", "-frames:v", str(frames),
                                    "-movflags", "+faststart", str(browser)], check=True, timeout=120)
                    ref.validate_probe(ref.ffprobe_video(browser), frames, 30, 1280, 720)
                rows.append({"variant": variant, "scene": scene[0], "view": view,
                             "path": path.name, "sha256": ref.sha256_file(path),
                             "browser": browser.name if browser else None,
                             "browser_sha256": ref.sha256_file(browser) if browser else None,
                             "tuning": tuning.name, "tuning_sha256": ref.sha256_file(tuning),
                             "saved_times_s": times, "receipt": receipt})
                print("Captured " + str(path), flush=True)
    if runtime != review.runtime_identity(ref.APP) or inputs != review.inputs():
        raise ValueError("runtime or input identity changed during capture")
    for name, pin in sources.items():
        if ref.sha256_file(ref.ROOT / name) != pin:
            raise ValueError("source changed during capture: " + name)
    ref.write_json_exclusive(phase / "manifest.json", {
        "runtime": runtime, "inputs": inputs, "source_files": sources, "assets": rows,
        "advancing_replay": args.advancing, "hydraulic_dispatches": 0,
        "scope": "presentation-only replay; no solver launch"})


def profile(args):
    phase = args.out / "profiles"
    ref.reserve_directory(phase)
    runtime, pins, rows = review.runtime_identity(ref.APP), review.inputs(), []
    for scene in review.SCENES[:2]:
        for batch in range(3):
            measured = "cascade" if "cascade" in args.variants else "rapid"
            for variant in (("off", measured) if batch % 2 == 0 else (measured, "off")):
                tuning = args.out / "stills" / (variant + ".json")
                prefix = phase / f"{scene[0]}-{batch}-{variant}"
                cmd = command(scene, prefix.with_suffix(".mp4"), tuning, frames=120)
                cmd += ["--profile-output", str(prefix), "--profile-warmup-frames", "12"]
                _, receipt = ref.run_logged(cmd, phase / "logs", prefix.name,
                                             timeout=180, expected_presentation="scenic")
                rows.append({"scene": scene[0], "batch": batch, "variant": variant,
                             "summary": ref.profile_summary(prefix, 12, 108), "receipt": receipt})
                print("Profiled " + prefix.name, flush=True)
    if runtime != review.runtime_identity(ref.APP) or pins != review.inputs():
        raise ValueError("runtime or input identity changed during profiles")
    ref.write_json_exclusive(phase / "manifest.json", {
        "runtime": runtime, "inputs": pins, "rows": rows,
        "scope": "same-build off/on, alternating order, 1280x720, warm render-only replay"})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--phase", choices=("baseline", "mask", "stills", "motion", "advancing", "profile"), required=True)
    parser.add_argument("--variants", nargs="+", choices=tuple(VARIANTS), default=["off", "rapid", "rapid-large"])
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.advancing = args.phase == "advancing"
    if args.phase == "profile":
        profile(args)
    else:
        capture(args)


if __name__ == "__main__":
    main()
