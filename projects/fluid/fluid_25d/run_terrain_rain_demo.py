#!/usr/bin/env python3
"""Prepared terrain demo: live 1k/16m at 60 Hz by default, explicit replay optional.

No installs, solver port, terrain generation, automatic builds or fallback.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import signal
import subprocess
import tempfile
import time

import run_mountain_rain_demo as demo
import terrain_demo_session as managed

HERE = Path(__file__).resolve().parent
SITES = ("mountain", "alpine", "rolling")
CONFIGS = ("patch-512-32m", "patch-1024-16m", "patch-2048-8m", "small-512-16m", "large-2048-16m")
RELEASE_APP = demo.ref.ROOT / "build/release/projects/fluid/fluid_25d/fluid_25d"
SITE_LABELS = {"mountain": "Mountain valley", "alpine": "Alpine terrain", "rolling": "Rolling hills"}
GRID_PRESETS = {"patch-512-32m": (512, 32, "Coarse", "same"),
                "patch-1024-16m": (1024, 16, "Default", "same"),
                "patch-2048-8m": (2048, 8, "Fine (slower solver)", "same"),
                "small-512-16m": (512, 16, "Smaller area", "small"),
                "large-2048-16m": (2048, 16, "Larger area (slower solver)", "large")}


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


def catalogue(study):
    """Admit only named existing cases; missing/invalid entries stay disabled."""
    entries = []
    for site in SITES:
        for configuration in CONFIGS:
            size, spacing, label, extent = GRID_PRESETS[configuration]
            case = (study / "cases" / f"{site}-{configuration}").absolute()
            entry = {"id": f"{site}-{configuration}", "site": site,
                     "site_label": SITE_LABELS[site], "configuration": configuration,
                     "label": label, "extent": extent, "width": size, "height": size,
                     "spacing_m": spacing, "source_spacing_m": 30,
                     "case_dir": str(case), "surface_grid": str(case / "surface-grid.json"),
                     "available": False, "error": "", "input_sha256": "",
                     "solver_bed_sha256": "", "protocol_sha256": "",
                     "climate_manifest_sha256": "", "duration_s": 72000,
                     "rain_mm_per_hour": 512}
            try:
                identity = case_identity(case)
                spec = identity["spec"]
                if (spec["site"], spec["configuration"], spec["cols"], spec["rows"], spec["dx_m"]) != (
                        site, configuration, size, size, spacing):
                    raise ValueError("prepared case does not match its named preset")
                protocol = json.loads((case / "case-protocol.json").read_bytes())
                native_identity = {**protocol["input_hashes"],
                    "../case-spec.json": demo.ref.sha256_file(case / "case-spec.json"),
                    "../case-protocol.json": identity["protocol_sha256"]}
                entry.update(available=True, duration_s=spec["duration_s"],
                    rain_mm_per_hour=spec["rain_rate_mm_per_h"],
                    input_sha256=hashlib.sha256(json.dumps(native_identity, sort_keys=True,
                        separators=(",", ":")).encode()).hexdigest(),
                    solver_bed_sha256=spec["terrain_source_identity"]["solver_bed_sha256"],
                    protocol_sha256=identity["protocol_sha256"],
                    climate_manifest_sha256=identity["climate_manifest_sha256"])
            except (ValueError, OSError, KeyError) as error:
                entry["error"] = str(error)[:512]
            entries.append(entry)
    return entries


def validate_catalogue_case(case, entries):
    expected = next(entry for entry in entries if Path(entry["case_dir"]) == case)
    identity = case_identity(case)
    if (identity["protocol_sha256"] != expected["protocol_sha256"] or
            identity["climate_manifest_sha256"] != expected["climate_manifest_sha256"]):
        raise ValueError("prepared case/climate changed since catalogue admission")
    return identity


def worker_command(case, session, speed=60):
    return ["rtk", "proxy", str(demo.LIVE_PYTHON), "-u", str(HERE / "external_synxflow/session_worker.py"),
            "--extension", str(demo.EXTENSION), "--case", str(case), "--out", str(session),
            "--speed", str(speed), "--publish-interval", ".1", "--paused", "--telemetry"]


def viewer_command(case, source, title, width=1920, height=1080, profile=None, live=True, start=0,
                   surface="legacy", camera="overview", app=None, surface_subdivision=2,
                   render_fps=None, control=None, headless_seconds=None, switch_script=None,
                   no_validation=False):
    if surface_subdivision not in (1, 2):
        raise ValueError("study display subdivision must be 1 or 2")
    command = ["rtk", "proxy", str(app or demo.ref.APP), "--width", str(width), "--height", str(height),
               "--frames", "0", "--title", title, "--fluid25d-recording-camera", camera,
               "--fluid25d-native-presentation", "scenic", "--fluid25d-scenic-material", "macro",
               "--fluid25d-native-surface-highlights", "off",
               "--fluid25d-motion-markers", "--fluid25d-rain-visuals",
               "--fluid25d-scenic-surface-grid", str(case / "surface-grid.json"),
               "--fluid25d-scenic-surface-mode", surface]
    # Existing study/capture callers retain their timing unless explicitly paced.
    if render_fps is not None:
        if not 0 <= render_fps <= 0xFFFFFFFF:
            raise ValueError("render fps must fit a nonnegative uint32")
        command += ["--render-fps", str(render_fps)]
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
    if control is not None:
        if not live:
            raise ValueError("managed terrain switching requires live service mode")
        command += ["--fluid25d-terrain-demo-control", str(control)]
    if headless_seconds is not None:
        if not math.isfinite(headless_seconds) or not 1 <= headless_seconds <= 3600:
            raise ValueError("headless duration must be finite and 1..3600 seconds")
        command += ["--headless", "--headless-render-only", "--run-seconds", str(headless_seconds)]
    if switch_script is not None:
        if control is None or headless_seconds is None:
            raise ValueError("switch scripts require managed headless live mode")
        command += ["--fluid25d-terrain-demo-script", str(switch_script)]
        command += ["--fluid25d-recording-gpu-validation"]
    if no_validation:
        command += ["--no-validation"]
    return command


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("live", "replay"), nargs="?", default="live",
                   help="default live; replay requires an explicit recording and runs no solver")
    p.add_argument("--study", required=True, type=Path)
    p.add_argument("--site", choices=SITES, default="mountain")
    p.add_argument("--configuration", choices=CONFIGS, default="patch-1024-16m")
    p.add_argument("--recording", type=Path, help="explicit binary-capture replay; never substituted for live")
    p.add_argument("--surface", choices=("legacy", "correlated"), default="legacy")
    p.add_argument("--camera", choices=("overview", "runoff", "collection"), default="overview")
    p.add_argument("--width", type=int, default=1920)
    p.add_argument("--height", type=int, default=1080)
    p.add_argument("--app", type=Path, default=RELEASE_APP,
                   help="existing renderer artifact; default measured Release build, no build/fallback")
    p.add_argument("--render-fps", type=int, default=60,
                   help="render pacing only: default 60 Hz; 0 unpaced, independent of solver speed")
    p.add_argument("--surface-subdivision", type=int, choices=(1, 2), default=2,
                   help="existing B-spline display density; opt-in 1x does not change simulation resolution")
    p.add_argument("--print-command", action="store_true")
    p.add_argument("--headless", action="store_true", help="Vulkan offscreen render loop; no GUI or display server")
    p.add_argument("--run-seconds", type=float, default=60, help="bounded headless wall duration, default 60 s")
    p.add_argument("--switch-script", type=Path, help="headless-only managed-control regression script")
    p.add_argument("--profile-output", type=Path)
    p.add_argument("--no-validation", action="store_true", help="explicit profiling override")
    p.add_argument("--out", type=Path, help="fresh owned launcher leaf under outputs/fluid")
    return p


def main(argv=None):
    p = parser()
    a = p.parse_args(argv)
    if not 0 <= a.render_fps <= 0xFFFFFFFF:
        p.error("render fps must fit a nonnegative uint32")
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
    if a.switch_script and (not a.headless or a.mode != "live"):
        p.error("switch scripts require headless live mode")
    if a.switch_script:
        a.switch_script = a.switch_script.resolve(strict=True)
    entries = catalogue(a.study.resolve(strict=True)) if a.mode == "live" else []
    if entries and not next(entry for entry in entries if entry["id"] == case.name)["available"]:
        raise ValueError("selected prepared case is unavailable")
    preview = demo.ref.OUTPUT_ROOT / "terrain-rain-launch-FRESH"
    title = f"Cubey Terrain Water | {a.mode.upper()}"
    session = preview / "sessions/generation-00000000000000000000"
    source = session if a.mode == "live" else a.recording.resolve()
    saved = json.loads(source.read_bytes())["protocol"]["duration_s"] if a.mode == "replay" else 0
    commands = {"viewer": viewer_command(case, source, title, a.width, a.height, start=saved,
                                         live=a.mode == "live", surface=a.surface, app=a.app,
                                         surface_subdivision=a.surface_subdivision, camera=a.camera,
                                         render_fps=a.render_fps, profile=a.profile_output,
                                         control=preview / "control" if a.mode == "live" else None,
                                         headless_seconds=a.run_seconds if a.headless else None,
                                         switch_script=a.switch_script, no_validation=a.no_validation)}
    if a.mode == "live":
        commands["worker"] = worker_command(case, session)
    print(json.dumps(identity, indent=2))
    for name, command in commands.items():
        print(name + ": " + shlex.join(command), flush=True)
    if a.print_command:
        return 0
    if not a.headless and not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        raise ValueError("no GUI display; use read-only --print-command remotely")
    out = demo.fresh_output(a.out) if a.out else Path(tempfile.mkdtemp(prefix="terrain-rain-launch-", dir=demo.ref.OUTPUT_ROOT))
    if a.out:
        out.mkdir()
    owned, cleanup = [], []
    supervisor = None
    interrupted = [False]
    prior = {sig: signal.getsignal(sig) for sig in (signal.SIGINT, signal.SIGTERM)}
    for sig in prior:
        signal.signal(sig, lambda *_: interrupted.__setitem__(0, True))
    try:
        with (out / "viewer.log").open("wb") as vlog:
            if a.mode == "live":
                supervisor = managed.ManagedTerrainDemo(out, entries, case.name,
                    lambda path: validate_catalogue_case(path, entries), worker_command, demo.stop_owned)
                supervisor.start_initial()
                while not interrupted[0] and supervisor.state["phase"] not in ("active", "error"):
                    supervisor.poll()
                    time.sleep(.05)
                if interrupted[0]:
                    return 0
                if supervisor.state["phase"] == "error":
                    raise RuntimeError(supervisor.state["error"])
                source = out / "sessions" / supervisor.state["session_name"]
            environment = os.environ.copy()
            if a.headless:
                for name in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_SESSION_TYPE",
                             "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS", "CUBEY_PRIVATE_XVFB"):
                    environment.pop(name, None)
            viewer = subprocess.Popen(viewer_command(case, source, title, a.width, a.height,
                live=a.mode == "live", start=saved, surface=a.surface, app=a.app,
                surface_subdivision=a.surface_subdivision, camera=a.camera,
                render_fps=a.render_fps, profile=a.profile_output,
                control=supervisor.control if supervisor else None,
                headless_seconds=a.run_seconds if a.headless else None,
                switch_script=a.switch_script, no_validation=a.no_validation), stdout=vlog,
                stderr=subprocess.STDOUT, start_new_session=True, env=environment)
            owned.append(viewer)
            print("Live starts dry/paused; Start/Space resumes. Closing this managed demo stops its owned worker."
                  if a.mode == "live" else "Recorded view holds the final captured state; no solver is running.", flush=True)
            while not interrupted[0] and viewer.poll() is None:
                if supervisor:
                    supervisor.poll()
                time.sleep(.1)
            if viewer.returncode:
                raise RuntimeError("viewer failed; see " + str(out / "viewer.log"))
    except KeyboardInterrupt:
        pass
    finally:
        for process in reversed(owned):
            cleanup.append(demo.stop_owned(process))
        if supervisor:
            supervisor.close()
            cleanup.extend(supervisor.cleanup)
        for sig, handler in prior.items():
            signal.signal(sig, handler)
        demo.ref.write_json_exclusive(out / "launch.json", {"identity": identity, "cleanup": cleanup,
            "mode": a.mode, "site": a.site, "configuration": a.configuration,
            "acceptance": "human review not automated", "surface": a.surface,
            "app": str(a.app), "app_sha256": app_sha256,
            "surface_subdivision": a.surface_subdivision, "camera": a.camera,
            "render_fps": a.render_fps, "headless": a.headless,
            "managed": supervisor is not None, "final_demo_state": supervisor.state if supervisor else None})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
