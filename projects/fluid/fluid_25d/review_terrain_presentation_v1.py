#!/usr/bin/env python3
"""Matched terrain presentation review on sealed native fields; Vulkan headless only."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

import run_terrain_rain_demo as terrain
import run_mountain_rain_demo as demo

ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / "build/release/projects/fluid/fluid_25d/fluid_25d"
SHADERS = APP.parent / "shaders"
STUDY = ROOT / "outputs/fluid/site-expansion-20261011-jtIA3s"
MOUNTAIN = ROOT / "outputs/fluid/offscreen-perf-20261010-CwWtK3/soak-600s-offscreen/live-0/static-recording/recording.json"
# Frozen observation-only targets: slope maximum and developed-water basin
# within the central 65% of each patch. They do not move water or crop elevation.
CAMERAS = {
    "mountain": {"slope": [.741, .562, 900], "basin": [.634, .521, 1800]},
    "upland-basins": {"slope": [.176, .474, 900], "basin": [.532, .283, 1800]},
    "joining-valleys": {"slope": [.181, .513, 900], "basin": [.697, .416, 1800]},
    "steep-ridges": {"slope": [.820, .285, 900], "basin": [.183, .655, 1800]},
}
VIEWS = ("shaded", "terrain-only", "albedo", "base-normal", "detail-normal",
         "constant-albedo", "no-shadows", "no-detail", "face-normal")


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")


def environment():
    result = os.environ.copy()
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "CUBEY_PRIVATE_XVFB"):
        result.pop(key, None)
    return result


def scene(site):
    recording = MOUNTAIN if site == "mountain" else STUDY / "runs" / (
        site + "-patch-1024-16m") / "recording/recording.json"
    case = STUDY / "cases" / (site + "-patch-1024-16m")
    manifest = json.loads(recording.read_bytes())
    if manifest["grid"] != dict(width=1024, height=1024, cell_size_m=16,
        sample_location="cell-center", storage_order="row-major", row_direction="world-z-positive"):
        raise ValueError("review requires the frozen 1k/16m grid")
    return case, recording, manifest


def input_identity(site):
    case, recording, manifest = scene(site)
    paths = [recording, recording.parent / manifest["bed"]["path"], case / "surface-grid.json",
             case / "case-protocol.json"]
    paths += [recording.parent / frame["path"] for frame in (manifest["frames"][0], manifest["frames"][-1])]
    return {str(path.relative_to(ROOT)): digest(path) for path in paths}


def runtime():
    return {"app_sha256": digest(APP), "shaders": {p.name: digest(p) for p in sorted(SHADERS.glob("*.spv"))}}


def command(site, camera, view, output, *, surface="legacy", subdivision=2,
            dry=False, cues=False, video=False, sweep=0, profile=None, seconds=0,
            extra=()):
    case, recording, manifest = scene(site)
    start = 0 if dry else manifest["frames"][-1]["time_s"]
    argv = terrain.viewer_command(case, recording, "Terrain presentation review", 1280, 720,
        live=False, start=start, surface=surface, app=APP, surface_subdivision=1)
    if not cues:
        argv.remove("--fluid25d-motion-markers")
        argv.remove("--fluid25d-rain-visuals")
    argv[argv.index("--fluid25d-native-surface-subdivision") + 1] = str(subdivision)
    argv += ["--headless", "--fluid25d-scenic-terrain-view", view]
    if camera != "overview":
        x, z, span = CAMERAS[site][camera]
        argv += ["--fluid25d-native-camera-target-x", str(x),
                 "--fluid25d-native-camera-target-z", str(z),
                 "--fluid25d-native-camera-span-m", str(span)]
    if profile is not None:
        # Held-state render cost: no PNG readback, video encoding, or hydraulic work.
        argv[argv.index("--width") + 1] = "1920"
        argv[argv.index("--height") + 1] = "1080"
        argv += ["--headless-render-only", "--run-seconds", str(seconds), "--render-fps", "60",
                 "--no-validation", "--profile-output", str(profile),
                 "--profile-warmup-frames", "120", "--profile-sample-interval", "10",
                 "--profile-max-frames", "12000"]
    else:
        argv += ["--fluid25d-recording-gpu-validation", "--output", str(output)]
    if video:
        argv[argv.index("--frames") + 1] = "120"
        argv += ["--capture", "video", "--fps", "24",
                 "--fluid25d-recording-frame-interval-seconds", "0"]
        if sweep:
            argv += ["--fluid25d-native-camera-sweep", str(sweep)]
    return argv + list(extra)


def run_capture(argv, log_path, timeout=180):
    started = time.monotonic()
    with log_path.open("xb") as log:
        process = subprocess.Popen(argv, cwd=ROOT, env=environment(), stdout=log,
            stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = process.wait(timeout=timeout)
            if code:
                raise subprocess.CalledProcessError(code, argv)
        finally:
            cleanup = demo.stop_owned(process, grace_s=5)
    raw = log_path.read_text(errors="replace")
    if "NVIDIA GeForce RTX 5070 Ti" not in raw:
        raise ValueError("unexpected Vulkan device")
    if "validation error" in raw.lower() or "Validation Error" in raw:
        raise ValueError("Vulkan validation failed")
    if "--fluid25d-recording-gpu-validation" in argv and "fluid_25d_recording_upload: PASS bit-exact" not in raw:
        raise ValueError("native-field GPU validation receipt missing")
    return {"command": argv, "wall_s_including_startup_capture": time.monotonic()-started,
            "cleanup": cleanup}


def capture(args):
    out = args.out.resolve()
    if out.parent != (ROOT / "outputs/fluid").resolve() or not out.name.startswith("terrain-presentation-v1-"):
        raise ValueError("owned terrain-presentation-v1-* output leaf required")
    if not args.phase.replace("-", "").isalnum():
        raise ValueError("unsafe phase")
    phase = out / args.phase
    phase.mkdir()
    before = runtime()
    frozen_app = phase / "fluid_25d"
    shutil.copy2(APP, frozen_app)
    identities = {site: input_identity(site) for site in args.sites}
    write(phase / "identity.json", {"runtime": before, "inputs": identities, "cameras": CAMERAS})
    rows = []
    for site in args.sites:
        for camera in args.cameras:
            for view in args.views:
                name = f"{site}-{camera}-{view}"
                path = phase / (name + (".mp4" if args.video else ".png"))
                argv = command(site, camera, view, path, surface=args.surface,
                    subdivision=args.subdivision, dry=args.dry, cues=args.cues,
                    video=args.video, sweep=args.sweep,
                    profile=phase/name if args.seconds else None, seconds=args.seconds,
                    extra=args.extra)
                argv[2] = str(frozen_app)
                receipt = run_capture(argv, phase/(name+".log"))
                rows.append({"site": site, "camera": camera, "view": view,
                    "surface": args.surface, "subdivision": args.subdivision,
                    "dry": args.dry, "cues": args.cues, "path": path.name if path.exists() else None,
                    "sha256": digest(path) if path.exists() else None, **receipt})
                write(phase / (name+"-receipt.json"), rows[-1])
                print("captured " + args.phase + "/" + name, flush=True)
    current = runtime()
    current["app_sha256"] = digest(frozen_app)
    if before != current or identities != {site: input_identity(site) for site in args.sites}:
        raise ValueError("runtime or inputs changed during capture")
    write(phase/"manifest.json", {"runtime": before, "inputs": identities, "assets": rows,
        "boundary": "Vulkan offscreen; sealed native fields; visual acceptance deferred"})


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--phase", required=True)
    p.add_argument("--sites", nargs="+", choices=CAMERAS, default=list(CAMERAS))
    p.add_argument("--cameras", nargs="+", choices=("overview", "slope", "basin"), default=["overview", "slope", "basin"])
    p.add_argument("--views", nargs="+", choices=VIEWS, default=["shaded"])
    p.add_argument("--surface", choices=("legacy", "landform", "climate", "correlated"), default="legacy")
    p.add_argument("--subdivision", type=int, choices=(1, 2, 4), default=2)
    p.add_argument("--dry", action="store_true")
    p.add_argument("--cues", action="store_true")
    p.add_argument("--video", action="store_true")
    p.add_argument("--sweep", type=float, default=0)
    p.add_argument("--seconds", type=float, default=0, help="held-state 1080p render-only profiling")
    args, args.extra = p.parse_known_args()
    if args.seconds and not 3 <= args.seconds <= 60:
        raise ValueError("profile duration must be 3..60 seconds")
    if args.video and args.seconds:
        raise ValueError("video and render-only timings are separate evidence")
    capture(args)


if __name__ == "__main__":
    main()
