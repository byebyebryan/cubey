#!/usr/bin/env python3
"""Opt-in prepared terrain launcher. No installs, solver port, generation or fallback."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import tempfile
import time

import run_mountain_rain_demo as demo

HERE = Path(__file__).resolve().parent
SITES = ("mountain", "alpine", "rolling")
CONFIGS = ("patch-512-32m", "patch-1024-16m", "patch-2048-8m", "small-512-16m", "large-2048-16m")


def case_identity(case):
    case = case.resolve(strict=True)
    spec = json.loads((case / "case-spec.json").read_bytes())
    protocol = json.loads((case / "case-protocol.json").read_bytes())
    if protocol.get("schema") != "cubey.fluid25d.live-perf-case.v1" or protocol.get("case") != spec:
        raise ValueError("case protocol/spec mismatch")
    for relative, expected in protocol["input_hashes"].items():
        path = case / "native/input" / relative
        if path.is_symlink() or not path.resolve().is_relative_to(case) or demo.ref.sha256_file(path) != expected:
            raise ValueError("prepared native input changed: " + relative)
    binding = json.loads((case / "surface-grid.json").read_bytes())
    if binding != protocol["terrain_source_identity"]:
        raise ValueError("world-coordinate source binding changed")
    for key, path in (("manifest_sha256", Path(binding["manifest_path"])),
                      ("source_bed_sha256", case / "source-bed.f32"),
                      ("solver_bed_sha256", case / "solver-bed.f32")):
        if demo.ref.sha256_file(path) != binding[key]:
            raise ValueError("prepared/source terrain identity changed")
    manifest_path = Path(binding["manifest_path"])
    manifest = json.loads(manifest_path.read_bytes())
    elevation = manifest_path.parent / manifest["files"]["elevation"]["path"]
    if demo.ref.sha256_file(elevation) != binding["elevation_sha256"]:
        raise ValueError("original elevation payload changed")
    return {"case": str(case), "spec": spec, "input_sha256": demo.input_tree(case / "native/input"),
            "protocol_sha256": demo.ref.sha256_file(case / "case-protocol.json"),
            "climate_manifest_sha256": demo.ref.sha256_file(manifest_path.parent / "surface-fields.json")}


def worker_command(case, session, speed=60):
    return ["rtk", "proxy", str(demo.LIVE_PYTHON), "-u", str(HERE / "external_synxflow/session_worker.py"),
            "--extension", str(demo.EXTENSION), "--case", str(case), "--out", str(session),
            "--speed", str(speed), "--publish-interval", ".1", "--paused", "--telemetry"]


def viewer_command(case, source, title, width=1920, height=1080, profile=None, live=True, start=0,
                   surface="legacy", camera="overview", app=None, surface_subdivision=2):
    if surface_subdivision not in (1, 2):
        raise ValueError("study display subdivision must be 1 or 2")
    command = ["rtk", "proxy", str(app or demo.ref.APP), "--width", str(width), "--height", str(height),
               "--frames", "0", "--title", title, "--fluid25d-recording-camera", camera,
               "--fluid25d-native-presentation", "scenic", "--fluid25d-scenic-material", "macro",
               "--fluid25d-native-surface-highlights", "off",
               "--fluid25d-motion-markers", "--fluid25d-rain-visuals",
               "--fluid25d-scenic-surface-grid", str(case / "surface-grid.json"),
               "--fluid25d-scenic-surface-mode", surface]
    if surface_subdivision == 2:
        command += ["--fluid25d-native-bank-view", "bspline-2x"]
    else:
        # Keep B-spline reconstruction; change only its existing display tessellation.
        # An explicit bank-view preset would override the advanced subdivision.
        command += ["--fluid25d-native-water-sampling", "bspline",
                    "--fluid25d-native-surface-subdivision", "1"]
    if live:
        command += ["--fluid25d-backend", "external", "--fluid25d-external-session", str(source)]
    else:
        command += ["--fluid25d-backend", "recording", "--fluid25d-recording", str(source),
                    "--fluid25d-recording-time-seconds", str(start)]
    if profile:
        command += ["--profile-output", str(profile), "--profile-warmup-frames", "120",
                    "--profile-sample-interval", "10", "--profile-max-frames", "12000"]
    return command


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("live", "replay"))
    p.add_argument("--study", required=True, type=Path)
    p.add_argument("--site", choices=SITES, default="mountain")
    p.add_argument("--configuration", choices=CONFIGS, default="patch-1024-16m")
    p.add_argument("--recording", type=Path, help="explicit binary-capture replay; never substituted for live")
    p.add_argument("--surface", choices=("legacy", "correlated"), default="legacy")
    p.add_argument("--camera", choices=("overview", "runoff", "collection"), default="overview")
    p.add_argument("--width", type=int, default=1920)
    p.add_argument("--height", type=int, default=1080)
    p.add_argument("--app", type=Path, default=demo.ref.APP,
                   help="explicit renderer artifact (for example Release); no build or fallback")
    p.add_argument("--surface-subdivision", type=int, choices=(1, 2), default=2,
                   help="existing B-spline display density; opt-in 1x does not change simulation resolution")
    p.add_argument("--print-command", action="store_true")
    a = p.parse_args(argv)
    if not 320 <= a.width <= 3840 or not 240 <= a.height <= 2160:
        p.error("viewport must fit the bounded 320x240..3840x2160 range")
    a.app = a.app.resolve(strict=True)
    if not a.app.is_file():
        p.error("renderer artifact must be an existing file")
    app_sha256 = demo.ref.sha256_file(a.app)
    case = (a.study / "cases" / f"{a.site}-{a.configuration}").resolve(strict=True)
    identity = case_identity(case)
    if a.mode == "live":
        if not demo.LIVE_PYTHON.is_file() or demo.ref.sha256_file(demo.EXTENSION) != demo.EXTENSION_SHA:
            raise ValueError("audited optional native runtime missing/changed; no build attempted")
        if a.recording:
            p.error("live cannot use a recording")
    elif not a.recording or not a.recording.is_file():
        p.error("replay requires an explicit completed --recording")
    preview = demo.ref.OUTPUT_ROOT / "terrain-rain-launch-FRESH"
    title = f"Cubey Terrain Rain | {a.site} | {a.configuration} | {a.mode}"
    session = preview / "session"
    source = session if a.mode == "live" else a.recording.resolve()
    saved = json.loads(source.read_bytes())["protocol"]["duration_s"] if a.mode == "replay" else 0
    commands = {"viewer": viewer_command(case, source, title, a.width, a.height, start=saved,
                                         live=a.mode == "live", surface=a.surface, app=a.app,
                                         surface_subdivision=a.surface_subdivision, camera=a.camera)}
    if a.mode == "live":
        commands["worker"] = worker_command(case, session)
    print(json.dumps(identity, indent=2))
    for name, command in commands.items():
        print(name + ": " + shlex.join(command), flush=True)
    if a.print_command:
        return 0
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        raise ValueError("no GUI display; use read-only --print-command remotely")
    out = Path(tempfile.mkdtemp(prefix="terrain-rain-launch-", dir=demo.ref.OUTPUT_ROOT))
    owned, cleanup = [], []
    interrupted = [False]
    prior = {sig: signal.getsignal(sig) for sig in (signal.SIGINT, signal.SIGTERM)}
    for sig in prior:
        signal.signal(sig, lambda *_: interrupted.__setitem__(0, True))
    try:
        with (out / "worker.log").open("wb") as wlog, (out / "viewer.log").open("wb") as vlog:
            worker = None
            if a.mode == "live":
                source = out / "session"
                worker = subprocess.Popen(worker_command(case, source), stdout=wlog, stderr=subprocess.STDOUT,
                                          start_new_session=True)
                owned.append(worker)
                demo.wait_ready(worker, source, 120, lambda: interrupted[0])
            viewer = subprocess.Popen(viewer_command(case, source, title, a.width, a.height,
                live=a.mode == "live", start=saved, surface=a.surface, app=a.app,
                surface_subdivision=a.surface_subdivision, camera=a.camera), stdout=vlog,
                stderr=subprocess.STDOUT, start_new_session=True)
            owned.append(viewer)
            print("Live starts dry/paused; Space resumes. Viewer close detaches; Ctrl-C stops owned worker."
                  if a.mode == "live" else "Recorded view holds the final captured state; no solver is running.", flush=True)
            while not interrupted[0] and ((worker is not None and worker.poll() is None) or
                   (worker is None and viewer.poll() is None)):
                if viewer.poll() is not None and viewer.returncode:
                    raise RuntimeError("viewer failed; see " + str(out / "viewer.log"))
                time.sleep(.1)
    except KeyboardInterrupt:
        pass
    finally:
        for process in reversed(owned):
            cleanup.append(demo.stop_owned(process))
        for sig, handler in prior.items():
            signal.signal(sig, handler)
        demo.ref.write_json_exclusive(out / "launch.json", {"identity": identity, "cleanup": cleanup,
            "mode": a.mode, "site": a.site, "configuration": a.configuration,
            "acceptance": "human review not automated", "surface": a.surface,
            "app": str(a.app), "app_sha256": app_sha256,
            "surface_subdivision": a.surface_subdivision, "camera": a.camera})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
