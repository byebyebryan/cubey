#!/usr/bin/env python3
"""Bounded real Vulkan controls: offscreen or verified native Wayland, never Xvfb."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import time

import run_terrain_rain_demo as terrain
import run_mountain_rain_demo as demo

v1 = None  # Campaign helpers require the existing optional NumPy environment.

APP = Path("build/release/projects/fluid/fluid_25d/fluid_25d")


def environment(host):
    env = os.environ.copy()
    env.pop("DISPLAY", None)
    env.pop("CUBEY_PRIVATE_XVFB", None)
    if host == "offscreen":
        env.pop("WAYLAND_DISPLAY", None)
    elif host == "wayland":
        if not env.get("WAYLAND_DISPLAY"):
            raise ValueError("native Wayland unavailable; no X11 fallback")
        env["XDG_SESSION_TYPE"] = "wayland"
    else:
        raise ValueError("unknown host")
    return env


def viewer_command(case, source, title, args, live, start):
    argv = terrain.viewer_command(case, source, title, args.width, args.height,
        live=live, start=start, app=args.app, camera=args.camera)
    argv += ["--run-seconds", str(args.wall + args.warmup + 4),
             "--render-fps", str(args.render_fps), "--no-validation",
             "--profile-output", str(args.prefix), "--profile-warmup-frames", "0",
             "--profile-sample-interval", "10", "--profile-max-frames", "12000"]
    if args.host == "offscreen":
        argv += ["--headless", "--headless-render-only"]
    return argv


def wait_viewer(viewer, log, host, timeout=120):
    marker = "headless_render_only_ready:" if host == "offscreen" else "windowed_render_ready:"
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = log.read_text(errors="replace")
        if marker in raw:
            if "NVIDIA GeForce RTX 5070 Ti" not in raw:
                raise RuntimeError("unexpected Vulkan device; no software fallback")
            if host == "wayland" and "cubey_window_backend: wayland" not in raw:
                raise RuntimeError("actual backend is not native Wayland")
            return raw
        if viewer.poll() is not None:
            raise RuntimeError("viewer exited before readiness")
        time.sleep(.1)
    raise TimeoutError("viewer startup")


def size_owned_window(title, width, height):
    """Adjust only this trial's unique window; never change compositor config."""
    if not os.environ.get("NIRI_SOCKET"):
        return {"available": False, "reason": "not niri; actual viewport will still be checked"}
    windows = json.loads(subprocess.check_output(["rtk", "proxy", "niri", "msg", "--json", "windows"]))
    matches = [w for w in windows if w.get("title", "").startswith(title)]
    if len(matches) != 1:
        raise RuntimeError("could not identify the unique owned Wayland window")
    window = matches[0]
    commands = []
    if not window["is_floating"]:
        commands.append(["toggle-window-floating", "--id", str(window["id"])])
    commands += [["set-window-width", "--id", str(window["id"]), str(width)],
                 ["set-window-height", "--id", str(window["id"]), str(height)]]
    for command in commands:
        subprocess.run(["rtk", "proxy", "niri", "msg", "action", *command], check=True,
                       capture_output=True, timeout=10)
    return {"available": True, "window_id": window["id"], "commands": commands}


def run_trial(case, out, args, mode, repeat):
    out.mkdir()
    args.prefix = out / "viewer-profile"
    session = out / "session"
    identity = terrain.case_identity(case)
    result = {"schema": "cubey.fluid25d.offscreen-perf-trial.v2", "case": case.name,
              "host": args.host if mode != "worker-only" else "none", "mode": mode,
              "repeat": repeat, "render_fps": args.render_fps, "requested_viewport": [args.width, args.height],
              "wall_target_s": args.wall, "warmup_s": args.warmup, "prewarm_physical_target_s": args.prewarm,
              "status": "failed", "errors": [], "input_identity": identity,
              "app_sha256": demo.ref.sha256_file(args.app), "controls": [], "samples": [], "cleanup": []}
    worker = viewer = initial = final = None
    logs = []
    owned = []
    controls_done = set()
    try:
        if mode != "renderer-only":
            log = (out / "worker.log").open("wb")
            logs.append(log)
            command = terrain.worker_command(case, session)
            result["worker_command"] = command
            worker = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            owned.append(worker)
            demo.wait_ready(worker, session, 180)
            initial = v1.snapshot(session)
            if args.prewarm:
                result["controls"].append(v1.command(session, "resume"))
                deadline = time.monotonic() + 600
                while v1.read_state(session)["frame"]["physical_time_s"] < args.prewarm:
                    if worker.poll() is not None or time.monotonic() > deadline:
                        raise TimeoutError("bounded worker prewarm did not reach target")
                    time.sleep(.2)
                result["controls"].append(v1.command(session, "pause"))
        if mode != "worker-only":
            live = mode == "live"
            source = session if live else args.recording
            start = 0 if live else json.loads(source.read_bytes())["protocol"]["duration_s"]
            title = "Cubey V2 " + out.name
            command = viewer_command(case, source, title, args, live, start)
            result["viewer_command"] = command
            result["display_environment"] = {k: environment(args.host).get(k) for k in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_SESSION_TYPE")}
            log = (out / "viewer.log").open("wb")
            logs.append(log)
            viewer = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                      env=environment(args.host), start_new_session=True)
            owned.append(viewer)
            wait_viewer(viewer, out / "viewer.log", args.host)
            if args.host == "wayland":
                result["owned_window"] = size_owned_window(title, args.width, args.height)
        if worker:
            result["controls"].append(v1.command(session, "resume"))
        time.sleep(args.warmup)
        started = time.monotonic()
        result["measurement_start_unix_s"] = time.time()
        result["initial_physical_s"] = v1.read_state(session)["frame"]["physical_time_s"] if worker else 0
        while time.monotonic() - started < args.wall:
            elapsed = time.monotonic() - started
            row = {"wall_s": elapsed, "unix_s": time.time(),
                   "worker_rss_mib": v1.rss_mib(worker.pid) if worker else 0,
                   "viewer_rss_mib": v1.rss_mib(viewer.pid) if viewer else 0}
            if worker:
                state = v1.read_state(session)
                row.update(physical_s=state["frame"]["physical_time_s"],
                    generation=state["frame"]["reset_generation"], sequence=state["frame"]["sequence"],
                    lifecycle=state["frame"]["lifecycle"], publication_age_s=time.time()-state["published_unix_s"])
                if row["lifecycle"] == "failed" or worker.poll() is not None:
                    raise RuntimeError("worker failed/exited during measurement")
            if viewer and viewer.poll() is not None:
                raise RuntimeError("viewer exited before measurement ended")
            if len(result["samples"]) % 5 == 0:
                gpu = subprocess.run(["rtk", "proxy", "nvidia-smi", "--query-gpu=utilization.gpu,memory.used,power.draw,temperature.gpu",
                    "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=10)
                row["whole_device_gpu"] = gpu.stdout.strip() if gpu.returncode == 0 else "unavailable"
            result["samples"].append(row)
            v1.write(out / "progress.json", row)
            if args.exercise_controls and worker:
                for fraction, name in ((.2, "pause"), (.4, "resume"), (.6, "reset"), (.65, "resume")):
                    if elapsed >= fraction * args.wall and fraction not in controls_done:
                        acknowledgement = v1.command(session, name)
                        result["controls"].append(acknowledgement)
                        if name == "reset" and viewer:
                            # Preserve the sole controller's reset command until
                            # the passive viewer observes its applied generation.
                            generation = acknowledgement["ack"]["reset_generation"]
                            deadline = time.monotonic() + 5
                            while f"generation={generation} sequence=" not in (out / "viewer.log").read_text(errors="replace"):
                                if time.monotonic() > deadline or viewer.poll() is not None:
                                    raise RuntimeError("viewer did not observe applied reset before next control")
                                time.sleep(.05)
                        controls_done.add(fraction)
            time.sleep(.5)
        result["measurement_end_unix_s"] = time.time()
        result["measurement_wall_s"] = time.monotonic()-started
        if worker:
            result["measurement_final_physical_s"] = v1.read_state(session)["frame"]["physical_time_s"]
            result["controls"].append(v1.command(session, "pause"))
            final = v1.snapshot(session)
            result["final_physical_s"] = final[0]["frame"]["physical_time_s"]
            result["physical_wall_ratio"] = None if args.exercise_controls else (result["measurement_final_physical_s"]-result["initial_physical_s"])/result["measurement_wall_s"]
            result["final_field_stats"] = v1.field_stats(final[1], identity["spec"]["dx_m"])
        if viewer:
            viewer.wait(timeout=20)
            if viewer.returncode:
                raise RuntimeError("viewer returned a failure")
            window = (result["measurement_start_unix_s"], result["measurement_end_unix_s"])
            result["profile"] = v1.profile_summary(args.prefix, window)
            if result["profile"]["viewport"] != [(args.width, args.height)]:
                raise RuntimeError("actual framebuffer does not match requested matched viewport")
            result["profile"]["boundary"] = "offscreen no-readback/encode" if args.host == "offscreen" else "native Wayland; UI and presentation retained, not monitor scanout"
        if worker and not args.exercise_controls and final[0]["frame"]["physical_time_s"] > 0:
            result["recording"] = str(v1.save_recording(case, session, initial, final, out / "static-recording"))
        result["status"] = "completed"
    except Exception as error:
        result["errors"].append(str(error))
    finally:
        if worker and worker.poll() is None:
            try:
                result["controls"].append(v1.command(session, "stop"))
                worker.wait(timeout=30)
            except Exception as error:
                result["errors"].append("worker stop: " + str(error))
        for process in reversed(owned):
            result["cleanup"].append(demo.stop_owned(process))
        for log in logs:
            log.close()
        for name in ("completion", "telemetry"):
            path = session / (name + ".json")
            result[name] = json.loads(path.read_bytes()) if path.exists() else None
        if worker and (not result["completion"] or not result["completion"].get("inputs_unchanged") or not result["completion"].get("reference_inputs_unchanged")):
            result["errors"].append("missing immutable native-input completion receipt")
        result["session_bytes"] = sum(p.stat().st_size for p in session.rglob("*") if p.is_file()) if session.exists() else 0
        result["input_identity_after"] = terrain.case_identity(case)
        if result["input_identity_after"] != identity:
            result["errors"].append("source/case identity changed")
        if result["errors"]:
            result["status"] = "failed"
        v1.write(out / "result.json", result)
    print(json.dumps({k: result.get(k) for k in ("case", "host", "mode", "repeat", "status", "physical_wall_ratio", "errors")}), flush=True)
    return result


def main():
    global v1
    import run_live_perf_terrain_v1 as v1
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--study", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--case", required=True)
    p.add_argument("--host", choices=("offscreen", "wayland"), default="offscreen")
    p.add_argument("--modes", nargs="+", choices=("worker-only", "renderer-only", "renderer-plus-worker", "live"), default=["live"])
    p.add_argument("--recording", type=Path)
    p.add_argument("--app", type=Path, default=APP)
    p.add_argument("--wall", type=float, default=45)
    p.add_argument("--warmup", type=float, default=5)
    p.add_argument("--prewarm", type=float, default=1800)
    p.add_argument("--repeats", type=int, default=3)
    p.add_argument("--width", type=int, default=1920)
    p.add_argument("--height", type=int, default=1080)
    p.add_argument("--render-fps", type=int, choices=(0, 60), default=0)
    p.add_argument("--camera", choices=("overview", "runoff", "collection"), default="overview")
    p.add_argument("--exercise-controls", action="store_true", help="validation trial, not steady-state performance")
    a = p.parse_args()
    if not (1 <= a.wall <= 600 and 0 <= a.warmup <= 30 and 0 <= a.prewarm <= 7200 and 1 <= a.repeats <= 3 and 320 <= a.width <= 3840 and 240 <= a.height <= 2160):
        p.error("outside approved bounds")
    if a.exercise_controls and (a.modes != ["live"] or a.repeats != 1):
        p.error("control exercise is one separate live validation trial")
    if any(m.startswith("renderer") for m in a.modes) and not a.recording:
        p.error("held controls need an explicit immutable recording")
    if Path(a.case).name != a.case:
        p.error("case name must be a leaf")
    a.app = a.app.resolve(strict=True)
    a.out = a.out.resolve()
    if a.out.exists() or not a.out.parent.is_dir() or not a.out.is_relative_to(demo.ref.OUTPUT_ROOT.resolve()):
        p.error("output must be a fresh leaf under outputs/fluid")
    environment(a.host)
    if demo.ref.sha256_file(demo.EXTENSION) != demo.EXTENSION_SHA:
        p.error("audited native extension changed")
    demo.ref.APP = a.app
    demo.ref.SHADER_ROOT = a.app.parent / "shaders"
    v1.PROBE = a.app.parent / "cubey_project_fluid_25d_external_probe"
    case = (a.study / "cases" / a.case).resolve(strict=True)
    a.out.mkdir()
    v1.write(a.out / "protocol.json", {"arguments": {k: str(v) if isinstance(v, Path) else v for k, v in vars(a).items()},
        "app_sha256": demo.ref.sha256_file(a.app), "extension_sha256": demo.ref.sha256_file(demo.EXTENSION),
        "worker_sha256": demo.ref.sha256_file(terrain.HERE / "external_synxflow/session_worker.py"),
        "runner_sha256": demo.ref.sha256_file(Path(__file__)),
        "source_head": subprocess.check_output(["rtk", "proxy", "git", "rev-parse", "HEAD"], text=True).strip(),
        "recording_sha256": demo.ref.sha256_file(a.recording) if a.recording else None,
        "shader_identity": demo.ref.shader_identity(), "created_unix_s": time.time(),
        "capture_scope": "no X11; actual backend verified; whole-device samples not per-process GPU accounting",
        "measurement_scope": "bounded wall window after setup and warmup; fixed held fields; live uses authoritative worker time"})
    rows = []
    for repeat in range(a.repeats):
        for mode in a.modes:
            rows.append(run_trial(case, a.out / f"{mode}-{repeat}", a, mode, repeat))
            v1.write(a.out / "results.json", {"trials": rows})
            if rows[-1]["status"] != "completed":
                return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
