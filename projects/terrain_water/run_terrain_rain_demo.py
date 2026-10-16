#!/usr/bin/env python3
"""Terrain Water: live 1k/16m at 60 Hz by default, explicit replay optional.

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
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from projects.terrain_water import terrain_demo_session as managed
from projects.terrain_water.tools import runtime
from projects.terrain_water.tools.terrain_water_sites import DEFAULT_CATALOGUE, load_sites, validate_world_site

NATIVE_SUPPORT = ROOT / "projects/terrain_water/external_synxflow"
LEGACY_HELPERS = ROOT / "studies/fluid_25d/harness"

# Historical study/performance tools use HERE as their helper/artifact root.
# Preserve that API while the canonical launcher lives in a different project.
PROJECT_DIR = Path(__file__).resolve().parent
HERE = LEGACY_HELPERS
sys.path.insert(0, str(runtime.ROOT / "projects/terrain/tools"))
SITES = tuple(load_sites())
CONFIGS = ("patch-512-32m", "patch-1024-16m", "patch-2048-8m", "small-512-16m", "large-2048-16m")
RELEASE_APP = runtime.ROOT / "build/release/projects/terrain_water/terrain_water"
SITE_LABELS = {key: value["label"] for key, value in load_sites().items()}
GRID_PRESETS = {"patch-512-32m": (512, 32, "Coarse", "same"),
                "patch-1024-16m": (1024, 16, "Default", "same"),
                "patch-2048-8m": (2048, 8, "Fine (slower solver)", "same"),
                "small-512-16m": (512, 16, "Smaller area", "small"),
                "large-2048-16m": (2048, 16, "Larger area (slower solver)", "large")}


def check_native_runtime(native_python, extension):
    """Opt-in ABI/import check, never a CUDA execution or lifecycle certificate."""
    with tempfile.TemporaryDirectory(prefix="cubey-terrain-water-preflight-") as temp:
        environment = os.environ.copy()
        environment.update(MPLBACKEND="Agg", MPLCONFIGDIR=str(Path(temp) / "matplotlib"),
                           XDG_CACHE_HOME=str(Path(temp) / "cache"))
        result = subprocess.run([str(native_python), "-B",
            str(NATIVE_SUPPORT / "check_runtime.py"),
            "--extension", str(extension)], env=environment, text=True,
            capture_output=True, timeout=60, check=False)
        if result.returncode:
            raise ValueError("native runtime import check failed: " +
                             (result.stdout + result.stderr)[-4000:])
        report = json.loads(result.stdout)
        if report.get("passed") is not True or report.get("solver_executed") is not False:
            raise ValueError("runtime checker did not attest import-only success")
        return report


def case_identity(case):
    case = case.resolve(strict=True)
    spec = json.loads((case / "case-spec.json").read_bytes())
    protocol = json.loads((case / "case-protocol.json").read_bytes())
    if protocol.get("schema") != "cubey.fluid25d.live-perf-case.v1" or protocol.get("case") != spec:
        raise ValueError("case protocol/spec mismatch")
    for relative, expected in protocol["input_hashes"].items():
        path = case / "native/input" / relative
        if path.is_symlink() or not path.resolve().is_relative_to(case) or runtime.sha256_file(path) != expected:
            raise ValueError("prepared native input changed: " + relative)
    binding = json.loads((case / "surface-grid.json").read_bytes())
    if binding != protocol["terrain_source_identity"]:
        raise ValueError("world-coordinate source binding changed")
    site = binding.get("terrain_site")
    if site:
        from terrain_site_package import load_site
        path = Path(site["manifest_path"])
        if runtime.sha256_file(path) != site["manifest_sha256"]:
            raise ValueError("prepared terrain site changed")
        package = load_site(path)
        if (str(package["heightfield"]) != binding["manifest_path"] or
                package["site"]["world_id"] != site["world_id"] or
                package["site"]["center_xz_m"] != binding["target_grid"]["center_xz_m"]):
            raise ValueError("prepared site/source/grid mismatch")
    for key, path in (("manifest_sha256", Path(binding["manifest_path"])),
                      ("source_bed_sha256", case / "source-bed.f32"),
                      ("solver_bed_sha256", case / "solver-bed.f32")):
        if runtime.sha256_file(path) != binding[key]:
            raise ValueError("prepared/source terrain identity changed")
    manifest_path = Path(binding["manifest_path"])
    manifest = json.loads(manifest_path.read_bytes())
    elevation = manifest_path.parent / manifest["files"]["elevation"]["path"]
    if runtime.sha256_file(elevation) != binding["elevation_sha256"]:
        raise ValueError("original elevation payload changed")
    return {"case": str(case), "spec": spec, "input_sha256": runtime.input_tree(case / "native/input"),
            "protocol_sha256": runtime.sha256_file(case / "case-protocol.json"),
            "climate_manifest_sha256": runtime.sha256_file(manifest_path.parent / "surface-fields.json")}


def catalogue(study, sites=None):
    """Admit only named existing cases; missing/invalid entries stay disabled."""
    entries = []
    sites = load_sites() if sites is None else sites
    for site, recipe in sites.items():
        for configuration in CONFIGS:
            size, spacing, label, extent = GRID_PRESETS[configuration]
            case = (study / "cases" / f"{site}-{configuration}").absolute()
            entry = {"id": f"{site}-{configuration}", "site": site,
                     "site_label": recipe["label"], "configuration": configuration,
                     "label": label, "extent": extent, "width": size, "height": size,
                     "spacing_m": spacing, "source_spacing_m": 30,
                     "case_dir": str(case), "surface_grid": str(case / "surface-grid.json"),
                     "available": False, "error": "", "input_sha256": "",
                     "solver_bed_sha256": "", "protocol_sha256": "",
                     "climate_manifest_sha256": "", "duration_s": 72000,
                     "rain_mm_per_hour": 512}
            try:
                world_site = validate_world_site(recipe)
                identity = case_identity(case)
                spec = identity["spec"]
                binding = spec["terrain_source_identity"]
                if binding.get("terrain_site") != world_site:
                    raise ValueError("case does not match its prepared world site")
                if (Path(binding["manifest_path"]) != (runtime.ROOT / recipe["manifest"]).resolve() or
                        binding["elevation_sha256"] != recipe["elevation_sha256"]):
                    raise ValueError("prepared case does not match its catalogued terrain source")
                if (spec["site"], spec["configuration"], spec["cols"], spec["rows"], spec["dx_m"]) != (
                        site, configuration, size, size, spacing):
                    raise ValueError("prepared case does not match its named preset")
                # Pin crop coordinates too, including the established border
                # clamp for the original mountain large-area preset. This is
                # admission geometry only; sampling remains shared Terrain code.
                cg = json.loads((Path(binding["manifest_path"]).parent / "surface-fields.json").read_bytes())["grid"]
                source_grid = binding["source_grid"]
                half = (size - 1) * spacing / 2
                expected_center = []
                for axis, dimension in (("x", "width"), ("z", "height")):
                    lo = max(g[f"sample_origin_{axis}_m"] for g in (source_grid, cg))
                    hi = min(g[f"sample_origin_{axis}_m"] + (g[dimension]-1)*g["sample_spacing_m"] for g in (source_grid, cg))
                    expected_center.append(min(max(recipe["center_xz_m"][len(expected_center)], lo+half), hi-half))
                target = binding["target_grid"]
                if ((target["width"], target["height"], target["spacing_m"]) != (size, size, spacing) or
                        target["center_xz_m"] != expected_center or
                        target["sample_origin_xz_m"] != [v-half for v in expected_center]):
                    raise ValueError("prepared crop does not match its catalogued world coordinates")
                protocol = json.loads((case / "case-protocol.json").read_bytes())
                native_identity = {**protocol["input_hashes"],
                    "../case-spec.json": runtime.sha256_file(case / "case-spec.json"),
                    "../case-protocol.json": identity["protocol_sha256"]}
                entry.update(available=True, duration_s=spec["duration_s"],
                    rain_mm_per_hour=spec["rain_rate_mm_per_h"],
                    input_sha256=hashlib.sha256(json.dumps(native_identity, sort_keys=True,
                        separators=(",", ":")).encode()).hexdigest(),
                    solver_bed_sha256=spec["terrain_source_identity"]["solver_bed_sha256"],
                    protocol_sha256=identity["protocol_sha256"],
                    climate_manifest_sha256=identity["climate_manifest_sha256"])
            except (ValueError, OSError, KeyError, TypeError) as error:
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


def worker_command(case, session, speed=60, *, native_python=None, extension=None):
    return ["rtk", "proxy", str(native_python or runtime.LIVE_PYTHON), "-u", str(NATIVE_SUPPORT / "session_worker.py"),
            "--extension", str(extension or runtime.EXTENSION), "--case", str(case), "--out", str(session),
            "--speed", str(speed), "--publish-interval", ".1", "--paused", "--telemetry"]


def viewer_command(case, source, title, width=1920, height=1080, profile=None, live=True, start=0,
                   surface="correlated", camera="overview", app=None, surface_subdivision=2,
                   render_fps=None, control=None, headless_seconds=None, switch_script=None,
                   no_validation=False, shadow_projection="scene", environment="fixed",
                   environment_time_hours=None, environment_cycle_speed=0,
                   profile_sample_interval=10):
    if not isinstance(profile_sample_interval, int) or not 1 <= profile_sample_interval <= 12000:
        raise ValueError("profile sample interval must be an integer in 1..12000")
    if surface_subdivision not in (1, 2):
        raise ValueError("study display subdivision must be 1 or 2")
    if shadow_projection not in ("legacy", "scene"):
        raise ValueError("unknown scene shadow projection")
    command = ["rtk", "proxy", str(app or RELEASE_APP), "--width", str(width), "--height", str(height),
               "--frames", "0", "--title", title, "--fluid25d-recording-camera", camera,
               "--fluid25d-native-presentation", "scenic", "--fluid25d-scenic-material", "macro",
               "--fluid25d-native-surface-highlights", "off",
               "--fluid25d-motion-markers", "--fluid25d-rain-visuals",
               "--fluid25d-scenic-surface-grid", str(case / "surface-grid.json"),
               "--fluid25d-scenic-surface-mode", surface,
               "--fluid25d-native-shadow-projection", shadow_projection,
               "--fluid25d-native-terrain-context", "off"]
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
                    "--profile-sample-interval", str(profile_sample_interval), "--profile-max-frames", "12000"]
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
    if environment not in ("fixed","shared"):
        raise ValueError("unknown environment mode")
    if not math.isfinite(environment_cycle_speed) or not 0 <= environment_cycle_speed <= 1:
        raise ValueError("environment cycle speed must be in 0..1 hours per wall second")
    if environment_time_hours is not None and (not math.isfinite(environment_time_hours) or not 0 <= environment_time_hours <= 24):
        raise ValueError("environment time must be in 0..24 hours")
    if environment=="fixed" and (environment_time_hours is not None or environment_cycle_speed):
        raise ValueError("time options require the shared environment")
    if environment=="shared":
        command += ["--fluid25d-environment","shared","--time-speed-hours-per-second",str(environment_cycle_speed),
            "--set","atmosphere.time_paused="+("false" if environment_cycle_speed else "true")]
        if environment_time_hours is not None or environment_cycle_speed:
            command += ["--time-of-day-mode","solar","--time-hours",str(environment_time_hours if environment_time_hours is not None else 12)]
    return command


def add_environment_arguments(p):
    p.add_argument("--environment",choices=("fixed","shared"),default="fixed")
    p.add_argument("--environment-time-hours",type=float)
    p.add_argument("--environment-cycle-speed",type=float,default=0,
                   help="render-only hours per wall second, independent of solver pace")


def environment_options(args):
    return {name:getattr(args,name) for name in ("environment","environment_time_hours",
        "environment_cycle_speed")}


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("live", "replay"), nargs="?", default="live",
                   help="default live; replay requires an explicit recording and runs no solver")
    p.add_argument("--study", required=True, type=Path)
    p.add_argument("--site", default="mountain")
    p.add_argument("--site-catalogue", type=Path, default=DEFAULT_CATALOGUE)
    p.add_argument("--configuration", choices=CONFIGS, default="patch-1024-16m")
    p.add_argument("--recording", type=Path, help="explicit binary-capture replay; never substituted for live")
    p.add_argument("--surface", choices=("legacy", "landform", "climate", "correlated"), default="correlated")
    p.add_argument("--camera", choices=("overview", "runoff", "collection"), default="overview")
    p.add_argument("--width", type=int, default=1920)
    p.add_argument("--height", type=int, default=1080)
    p.add_argument("--app", type=Path, default=RELEASE_APP,
                   help="existing renderer artifact; default measured Release build, no build/fallback")
    p.add_argument("--native-python", type=Path,
                   help="explicit existing worker environment; no installation or native import in launcher")
    p.add_argument("--extension", type=Path,
                   help="explicit existing service extension; must match the qualified binary hash")
    p.add_argument("--render-fps", type=int, default=60,
                   help="render pacing only: default 60 Hz; 0 unpaced, independent of solver speed")
    p.add_argument("--surface-subdivision", type=int, choices=(1, 2), default=2,
                   help="existing B-spline display density; opt-in 1x does not change simulation resolution")
    p.add_argument("--print-command", action="store_true")
    p.add_argument("--check-runtime", action="store_true",
                   help="with live --print-command, verify qualified ABI/imports without running CUDA")
    p.add_argument("--headless", action="store_true", help="Vulkan offscreen render loop; no GUI or display server")
    p.add_argument("--run-seconds", type=float, default=60, help="bounded headless wall duration, default 60 s")
    p.add_argument("--switch-script", type=Path, help="headless-only managed-control regression script")
    p.add_argument("--profile-output", type=Path)
    p.add_argument("--profile-sample-interval", type=int, default=10,
                   help="use 1 to include intermittent sky/cloud capture bursts")
    p.add_argument("--no-validation", action="store_true", help="explicit profiling override")
    add_environment_arguments(p)
    p.add_argument("--out", type=Path, help="fresh owned launcher leaf under outputs/fluid")
    return p


def main(argv=None):
    p = parser()
    a = p.parse_args(argv)
    if a.check_runtime and (not a.print_command or a.mode != "live"):
        p.error("--check-runtime requires live --print-command; replay never imports the worker")
    sites = load_sites(a.site_catalogue)
    if a.site not in sites:
        p.error("site not in the selected catalogue")
    if not 0 <= a.render_fps <= 0xFFFFFFFF:
        p.error("render fps must fit a nonnegative uint32")
    if not 320 <= a.width <= 3840 or not 240 <= a.height <= 2160:
        p.error("viewport must fit the bounded 320x240..3840x2160 range")
    a.app = a.app.resolve(strict=True)
    if not a.app.is_file():
        p.error("renderer artifact must be an existing file")
    app_sha256 = runtime.sha256_file(a.app)
    case = (a.study / "cases" / f"{a.site}-{a.configuration}").resolve(strict=True)
    identity = case_identity(case)
    # Do not resolve the interpreter symlink: its venv path selects sys.prefix.
    native_python = (a.native_python or runtime.LIVE_PYTHON).absolute()
    extension = (a.extension or runtime.EXTENSION).absolute()
    if a.mode == "live":
        if not native_python.is_file() or not extension.is_file() or runtime.sha256_file(extension) != runtime.EXTENSION_SHA:
            raise ValueError("audited optional native runtime missing/changed; no build attempted")
        if a.recording:
            p.error("live cannot use a recording")
    elif not a.recording or not a.recording.is_file():
        p.error("replay requires an explicit completed --recording")
    if a.switch_script and (not a.headless or a.mode != "live"):
        p.error("switch scripts require headless live mode")
    if a.switch_script:
        a.switch_script = a.switch_script.resolve(strict=True)
    entries = catalogue(a.study.resolve(strict=True), sites) if a.mode == "live" else []
    if entries and not next(entry for entry in entries if entry["id"] == case.name)["available"]:
        raise ValueError("selected prepared case is unavailable")
    preview = runtime.OUTPUT_ROOT / "terrain-rain-launch-FRESH"
    title = f"Cubey Terrain Water | {a.mode.upper()}"
    session = preview / "sessions/generation-00000000000000000000"
    source = session if a.mode == "live" else a.recording.resolve()
    saved = json.loads(source.read_bytes())["protocol"]["duration_s"] if a.mode == "replay" else 0
    commands = {"viewer": viewer_command(case, source, title, a.width, a.height, start=saved,
                                         live=a.mode == "live", surface=a.surface, app=a.app,
                                         surface_subdivision=a.surface_subdivision, camera=a.camera,
                                         render_fps=a.render_fps, profile=a.profile_output,
                                         profile_sample_interval=a.profile_sample_interval,
                                         control=preview / "control" if a.mode == "live" else None,
                                         headless_seconds=a.run_seconds if a.headless else None,
                                         switch_script=a.switch_script, no_validation=a.no_validation,
                                         **environment_options(a))}
    if a.mode == "live":
        commands["worker"] = worker_command(case, session, native_python=native_python, extension=extension)
    if a.check_runtime:
        print(json.dumps({"runtime_import_check": check_native_runtime(native_python, extension)}, indent=2))
    print(json.dumps(identity, indent=2))
    for name, command in commands.items():
        print(name + ": " + shlex.join(command), flush=True)
    if a.print_command:
        return 0
    if not a.headless and not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        raise ValueError("no GUI display; use read-only --print-command remotely")
    out = runtime.fresh_output(a.out) if a.out else Path(tempfile.mkdtemp(prefix="terrain-rain-launch-", dir=runtime.OUTPUT_ROOT))
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
                    lambda path: validate_catalogue_case(path, entries),
                    lambda path, session, speed: worker_command(path, session, speed,
                        native_python=native_python, extension=extension), runtime.stop_owned)
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
                profile_sample_interval=a.profile_sample_interval,
                control=supervisor.control if supervisor else None,
                headless_seconds=a.run_seconds if a.headless else None,
                switch_script=a.switch_script, no_validation=a.no_validation,
                **environment_options(a)), stdout=vlog,
                stderr=subprocess.STDOUT, start_new_session=True, env=environment)
            owned.append(viewer)
            print("Live starts dry/paused; Start/Space guides the validated Mountain 1k case, "
                  "or resumes in Manual/other sites. Closing this managed demo stops its owned worker."
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
            cleanup.append(runtime.stop_owned(process))
        if supervisor:
            supervisor.close()
            cleanup.extend(supervisor.cleanup)
        for sig, handler in prior.items():
            signal.signal(sig, handler)
        runtime.write_json_exclusive(out / "launch.json", {"identity": identity, "cleanup": cleanup,
            "mode": a.mode, "site": a.site, "configuration": a.configuration,
            "acceptance": "human review not automated", "surface": a.surface,
            "app": str(a.app), "app_sha256": app_sha256,
            "worker_runtime": {"python": str(native_python), "extension": str(extension),
                "extension_sha256": runtime.EXTENSION_SHA} if a.mode == "live" else None,
            "surface_subdivision": a.surface_subdivision, "camera": a.camera,
            "render_fps": a.render_fps, "headless": a.headless,
            "managed": supervisor is not None, "final_demo_state": supervisor.state if supervisor else None})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
