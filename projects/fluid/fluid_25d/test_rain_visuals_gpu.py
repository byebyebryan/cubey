#!/usr/bin/env python3
"""Synthetic rain presentation controls; never runs a solver or archive input."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

import run_native_shoreline_raster_v1 as fixtures
import review_scenic_water as review
from test_scenic_water_gpu import runtime_identity


def run(out, target, video_controls=False):
    review.ref.reserve_directory(out)
    identity = runtime_identity(target)
    rows = []
    # Calm frozen surface: animation must come only from the render-only rain.
    tuning = out / "calm.json"
    review.ref.write_json_exclusive(tuning, {
        "schema": "cubey.fluid25d.scenic-material.v2", "water_ripple_strength": 0,
        "water_normal_strength": 0})
    manifests = {}
    for rate in (0, 120, 512, 1024):
        manifest = fixtures.fixture(out / "fixtures" / str(rate), "fully-wet-lake")
        document = json.loads(manifest.read_text())
        document["protocol"]["rain_rate_m_per_s"] = rate / 3.6e6
        document["protocol"]["rainfall_history"] = [[0, rate / 3.6e6], [.5, rate / 3.6e6],
                                                    [1.5, 0], [2, 0]]
        manifest.write_text(json.dumps(document, indent=2) + "\n")
        manifests[rate] = manifest
    inputs = {str(p): review.ref.sha256_file(p) for p in (out / "fixtures").rglob("*") if p.is_file()}

    def render(name, rate=512, enabled=False, extra=(), t=0, video=False, interval=.1):
        path = out / (name + (".mp4" if video else ".png"))
        command = ["rtk", "proxy", str(target.resolve()), "--headless", "--width", "384", "--height", "288",
                   "--fluid25d-recording", str(manifests[rate]), "--fluid25d-recording-time-seconds", str(t),
                   "--fluid25d-recording-gpu-validation", "--fluid25d-native-presentation", "scenic",
                   "--fluid25d-scenic-material", "macro", "--fluid25d-scenic-tuning", str(tuning),
                   "--output", str(path), *extra]
        if enabled:
            command += ["--fluid25d-rain-visuals"]
        if video:
            command += ["--capture", "video", "--frames", "4", "--fps", "15",
                        "--fluid25d-recording-frame-interval-seconds", str(interval)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=90)
        log = result.stdout + result.stderr
        review.ref.write_text_exclusive(out / (name + ".log"), log)
        if result.returncode:
            raise RuntimeError(log)
        review.ref.verify_capture_log(log, True)
        receipts = re.findall(r"fluid_25d_rain_capture: output_frame=(\d+) applied_mm_h=([\d.]+) "
                              r"known=1 clock_s=([\d.]+) streaks=(\d+).*hydraulic_writes=0", log)
        if len(receipts) != (4 if video else 1):
            raise ValueError("rain frame receipts missing")
        if any("Validation Error" in line or "VUID-" in line for line in log.splitlines()):
            raise ValueError("Vulkan validation error")
        rows.append({"name": name, "path": path.name, "sha256": review.ref.sha256_file(path),
                     "rain_receipts": receipts, "upload_validation": "PASS", "hydraulic_dispatches": 0})
        print("Rain GPU control " + name, flush=True)
        return path, receipts

    def equal(a, b):
        if a.read_bytes() != b.read_bytes():
            raise ValueError(f"expected identical pixels: {a.name} / {b.name}")

    off, _ = render("off")
    default_zero, _ = render("zero-off", rate=0)
    zero, _ = render("zero-on", rate=0, enabled=True)
    equal(off, default_zero)
    equal(off, zero)
    strength_zero, _ = render("strength-zero", enabled=True,
                              extra=("--fluid25d-rain-visual-strength", "0"))
    equal(off, strength_zero)
    rain, receipt = render("on", enabled=True)
    repeat, _ = render("repeat", enabled=True)
    equal(rain, repeat)
    slow_still, _ = render("speed1-initial-phase", enabled=True,
                           extra=("--fluid25d-rain-visual-speed", "1"))
    equal(rain, slow_still)
    if rain.read_bytes() == off.read_bytes():
        raise ValueError("rain has no visible effect")
    low, lo = render("rate120", rate=120, enabled=True)
    high, hi = render("rate1024", rate=1024, enabled=True)
    if not 0 < int(lo[0][3]) < int(receipt[0][3]) < int(hi[0][3]) <= 24000:
        raise ValueError("rain density is not monotone/capped")
    for view, arguments in (("coverage", ("--fluid25d-scenic-water-view", "coverage")),
                            ("terrain", ("--fluid25d-scenic-terrain-view", "terrain-only")),
                            ("raw", ("--fluid25d-view", "diagnostics", "--debug-view", "depth"))):
        a, _ = render(view + "-off", extra=arguments)
        b, records = render(view + "-on", enabled=True, extra=arguments)
        equal(a, b)
        if int(records[0][3]) != 0:
            raise ValueError("rain leaked into a diagnostic view")
    taper, tr = render("taper", enabled=True, t=1)
    if not 0 < float(tr[0][1]) < 512 or not 0 < int(tr[0][3]) < int(receipt[0][3]):
        raise ValueError("forcing taper did not reduce visible rain")
    late, lr = render("after-rain", enabled=True, t=2)
    equal(late, off)
    if int(lr[0][3]) != 0:
        raise ValueError("rain continues after supply ends")
    # Held native fields, same presentation fps, radically different physical
    # playback cadence. Calm base and rain must produce identical video pixels.
    videos = [render("motion-" + str(i), enabled=True, video=True, interval=i)[0]
              for i in (.01, .1)] if video_controls else []
    if video_controls:
        slow, _ = render("motion-speed1", enabled=True, video=True, interval=.01,
                         extra=("--fluid25d-rain-visual-speed", "1"))
        videos.append(slow)
    decoded = []
    for video in videos:
        # Cubey's native MP4 edit list can hide the first captured frame from a
        # default decoder. Compare all encoded output frames, as browser remux
        # helpers do, rather than mistaking the container trim for missing rain.
        r = subprocess.run(["rtk", "proxy", "ffmpeg", "-v", "error", "-ignore_editlist", "1", "-i", str(video),
                            "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"],
                           capture_output=True, check=True, timeout=30)
        decoded.append(r.stdout)
    if decoded and (decoded[0] != decoded[1] or len(decoded[0]) != 4 * 384 * 288 * 3):
        raise ValueError("physical playback speed changes rain motion or frame count")
    frame_size = 384 * 288 * 3
    if decoded and decoded[0][:frame_size] == decoded[0][-frame_size:]:
        raise ValueError("rain fails to animate over held native fields")
    if decoded and (len(decoded[2]) != 4 * frame_size or
                    decoded[0][-frame_size:] == decoded[2][-frame_size:]):
        raise ValueError("fall speed must preserve initial phase and change future motion")
    if identity != runtime_identity(target):
        raise ValueError("runtime changed during rain GPU controls")
    if inputs != {str(p): review.ref.sha256_file(p) for p in (out / "fixtures").rglob("*") if p.is_file()}:
        raise ValueError("rain renderer mutated synthetic inputs")
    review.ref.write_json_exclusive(out / "result.json", {
        "status": "PASS", "runtime": identity, "rows": rows,
        "scope": "synthetic frozen renderer fixtures; no solver execution or visual acceptance",
        "checks": ["off/zero/zero-strength exact", "repeat exact", "monotone capped density",
                   "diagnostics exact", "forcing taper/off", "immutable fields/upload parity"],
        "video_controls": "PASS: motion on held fields; physical-speed independence; adjustable fall speed"
                          if video_controls else "not requested"})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--video", action="store_true")
    args = parser.parse_args()
    if args.out:
        run(args.out.resolve(), args.target, args.video)
    else:
        with tempfile.TemporaryDirectory(prefix="cubey-rain-visual-gpu-") as temp:
            run(Path(temp) / "controls", args.target, args.video)


if __name__ == "__main__":
    main()
