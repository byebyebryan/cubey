#!/usr/bin/env python3
"""Bounded opt-in live campaign on owned X11 display. No solver/math changes.

Run in the existing optional NumPy environment. All children are owned process
groups. Artifacts are genuine Vulkan GUI output, not physical-monitor evidence.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import select
import shutil
import struct
import subprocess
import time

import numpy as np

import run_terrain_rain_demo as terrain
import run_mountain_rain_demo as demo
from probe_mountain_rain_demo import PrivateKeys, wait_viewer_frame

PROBE = demo.ref.APP.parent / "cubey_project_fluid_25d_external_probe"


def write(path, value):
    raw = json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n"
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(raw)
    temporary.replace(path)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def read_state(session):
    return json.loads((session / "state.json").read_bytes())


def snapshot(session):
    for _ in range(10):
        state_raw = (session / "state.json").read_bytes()
        state = json.loads(state_raw)
        packet = (session / f"slot-{state['slot']}.bin").read_bytes()
        magic, generation, sequence, physical = struct.unpack("<8sQQd", packet[:32])
        if (magic == b"CBWTRV1\0" and digest(packet) == state["payload_sha256"] and
                generation == state["frame"]["reset_generation"] and
                sequence == state["frame"]["sequence"] and physical == state["frame"]["physical_time_s"]):
            return state, packet[32:], {"physical_time_s": physical, "state_sha256": digest(state_raw),
                                      "packet_sha256": digest(packet), "_state_raw": state_raw.decode()}
        time.sleep(.01)
    raise RuntimeError("could not retain an intact published snapshot")


def field_stats(planes, spacing):
    values = np.frombuffer(planes, dtype="<f4").reshape(3, -1)
    h, qx, qz = values
    if not np.isfinite(values).all() or (h < 0).any():
        raise ValueError("invalid fields")
    speed = np.hypot(qx.astype(np.float64), qz.astype(np.float64)) / np.maximum(h, 1e-30)
    dry_bad = int(np.count_nonzero((h == 0) & ((qx != 0) | (qz != 0))))
    if dry_bad:
        raise ValueError("dry nonzero momentum")
    return {"water_volume_m3": float(h.sum(dtype=np.float64) * spacing**2),
            "max_depth_m": float(h.max()), "max_speed_m_per_s": float(speed.max()),
            "dry_nonzero_momentum_cells": dry_bad}


def save_recording(case, session, initial, final, out):
    """Two exact snapshots: a static isolation control, NOT a smooth replay."""
    out.mkdir()
    spec = json.loads((case / "case-spec.json").read_bytes())
    duration = final[0]["frame"]["physical_time_s"]
    if duration <= 0:
        raise ValueError("no advancing state to seal")
    for name, src in (("bed.f32", session / "bed.f32"), ("source-bed.f32", case / "source-bed.f32")):
        shutil.copy2(src, out / name)
    frames = []
    for index, (state, planes, capture) in enumerate((initial, final)):
        path = out / f"frame-{index}.f32"
        path.write_bytes(planes)
        (out / f"capture-{index}.state.json").write_text(capture["_state_raw"])
        frames.append({"time_s": state["frame"]["physical_time_s"], "path": path.name,
                       "sha256": digest(planes), **field_stats(planes, spec["dx_m"])})
    applied = initial[0]["rain_m_per_s"]
    if final[0]["rain_m_per_s"] != applied:
        raise ValueError("two-state isolation recording cannot claim a constant changed rainfall history")
    protocol = dict(spec, duration_s=duration, output_interval_s=duration,
                    rain_rate_m_per_s=applied, rain_rate_mm_per_h=applied * 3600000,
                    rainfall_history=[[0, applied], [duration, applied]])
    document = {"schema": "cubey.fluid25d.recording.v1", "encoding": "float32-little-endian", "field_layout": "planar",
                "fields": ["h_m", "qx_m2_per_s", "qz_m2_per_s"],
                "grid": {"width": spec["cols"], "height": spec["rows"], "cell_size_m": spec["dx_m"],
                         "storage_order": "row-major", "row_direction": "world-z-positive", "sample_location": "cell-center"},
                "bed": {"path": "bed.f32", "sha256": demo.ref.sha256_file(out / "bed.f32")},
                "source_bed": {"path": "source-bed.f32", "sha256": demo.ref.sha256_file(out / "source-bed.f32")},
                "frames": frames, "protocol": protocol,
                "provenance": {"capture_schema": "cubey.fluid25d.external-binary-capture.v1",
                    "field_source": "validated external service binary snapshots",
                    "purpose": "two exact physical states; static final-state contention control, not timeline replay",
                    "captures": [{k: v for k, v in item[2].items() if k != "_state_raw"} for item in (initial, final)],
                    "source_sha256": {"case_spec": demo.ref.sha256_file(case / "case-spec.json"),
                        "case_protocol": demo.ref.sha256_file(case / "case-protocol.json"),
                        "metadata": demo.ref.sha256_file(session / "metadata.json"),
                        "native_numerical_bed": demo.ref.sha256_file(session / "bed.f32")}}}
    write(out / "recording.json", document)
    return out / "recording.json"


def command(session, kind, value=None):
    args = ["rtk", "proxy", str(PROBE), str(session), kind]
    if value is not None:
        args.append(str(value))
    process = subprocess.run(args, capture_output=True, text=True, timeout=30)
    if process.returncode:
        raise RuntimeError(process.stderr.strip() or "solver command failed")
    return json.loads(process.stdout)


def percentiles(values):
    finite = sorted(v for v in values if math.isfinite(v))
    if not finite:
        return {"samples": 0}
    return {"samples": len(finite), "p50": finite[math.ceil(.5 * len(finite))-1],
            "p95": finite[math.ceil(.95 * len(finite))-1], "max": finite[-1]}


def profile_summary(prefix, window=None):
    path = Path(str(prefix) + ".frames.csv")
    if not path.exists():
        return {"missing": True}
    with path.open() as stream:
        frames = list(csv.DictReader(stream))
    with Path(str(prefix) + ".metrics.csv").open() as stream:
        metrics = list(csv.DictReader(stream))
    with Path(str(prefix) + ".passes.csv").open() as stream:
        spans = list(csv.DictReader(stream))
    by_frame = {}
    for row in metrics:
        by_frame.setdefault(row["frame_index"], {})[(row["category"], row["name"])] = float(row["value"])
    # Exclude paused dry startup, post-measurement captures, and shutdown. Running=1.
    live = any(row["category"] == "fluid_25d.external" for row in metrics)
    indices = set()
    for row in frames:
        values = by_frame.get(row["frame_index"], {})
        timestamp = values.get(("fluid_25d.study", "command_recorded_unix_s"), 0)
        running = (not live or values.get(("fluid_25d.external", "physical_time_s"), 0) > 0 and
                   values.get(("fluid_25d.external", "lifecycle_code")) == 1)
        if running and (window is None or window[0] <= timestamp <= window[1]):
            indices.add(row["frame_index"])
    selected = [r for r in frames if r["frame_index"] in indices]
    groups = {}
    for row in metrics:
        if row["frame_index"] in indices:
            groups.setdefault(row["category"] + "." + row["name"], []).append(float(row["value"]))
    passes = {}
    for row in spans:
        if row["frame_index"] in indices:
            passes.setdefault(row["kind"] + ":" + row["label"], []).append(float(row["duration_ms"]))
    return {"frame_ms": percentiles([float(r["delta_ms"]) for r in selected]),
            "viewport": sorted({(int(r["width"]), int(r["height"])) for r in selected}),
            "triangles": percentiles([float(r["triangles"]) for r in selected]),
            "device_local_mib": percentiles([float(r["device_local_usage_mib"]) for r in selected]),
            "metrics": {k: percentiles(v) for k, v in groups.items()},
            "passes_ms": {k: percentiles(v) for k, v in passes.items()},
            "retained_frame_records": len(frames), "sampling_interval": 10, "maximum_records": 12000,
            "boundary": "sampled frame-loop / Vulkan command recording; not physical scanout"}


def rss_mib(group):
    value = 0.
    for pid in demo.active_owned_group(group):
        try:
            for line in Path(f"/proc/{pid}/status").read_text().splitlines():
                if line.startswith("VmRSS:"):
                    value += int(line.split()[1]) / 1024
        except (OSError, ValueError):
            pass
    return value


class Display:
    def __init__(self, root):
        self.old = {k: os.environ.get(k) for k in ("DISPLAY", "WAYLAND_DISPLAY", "CUBEY_PRIVATE_XVFB")}
        read, fd = os.pipe()
        self.log = (root / "xvfb.log").open("wb")
        self.process = subprocess.Popen(["rtk", "proxy", "Xvfb", "-displayfd", str(fd),
            "-screen", "0", "3840x2160x24", "-nolisten", "tcp"], pass_fds=(fd,),
            stdout=self.log, stderr=subprocess.STDOUT, start_new_session=True)
        os.close(fd)
        try:
            if not select.select([read], [], [], 20)[0]:
                raise TimeoutError("private Xvfb initialization")
            number = os.read(read, 32).decode().strip()
            if not number.isdecimal():
                raise RuntimeError("private X display allocation failed")
            os.environ["DISPLAY"] = ":" + number
            os.environ.pop("WAYLAND_DISPLAY", None)
            os.environ["CUBEY_PRIVATE_XVFB"] = "1"
            self.keys = PrivateKeys()
        except BaseException:
            demo.stop_owned(self.process)
            self.log.close()
            raise
        finally:
            os.close(read)

    def close(self):
        self.keys.close()
        result = demo.stop_owned(self.process)
        self.log.close()
        for key, value in self.old.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        return result


def run_trial(case, out, mode, display, *, horizon=7200, cap=600, wall=None,
              viewport=(1920, 1080), recording=None, capture=False, rain=None, control_probe=False):
    if out.exists():
        raise ValueError("trial output must be fresh")
    out.mkdir()
    identity = terrain.case_identity(case)
    owned, logs, cleanup, controls, samples = [], [], [], [], []
    result = {"case": case.name, "mode": mode, "identity": identity,
              "viewport_requested": viewport, "requested_speed": 60,
              "horizon_s": horizon, "wall_limit_s": wall or cap, "status": "failed", "errors": []}
    worker, viewer, initial, final = None, None, None, None
    session = out / "session"
    prefix = out / "viewer-profile"
    started = None
    last_gpu = -math.inf
    try:
        if mode != "renderer-only":
            wlog = (out / "worker.log").open("wb")
            logs.append(wlog)
            argv = terrain.worker_command(case, session)
            result["worker_command"] = argv
            worker = subprocess.Popen(argv, stdout=wlog, stderr=subprocess.STDOUT, start_new_session=True)
            owned.append(worker)
            demo.wait_ready(worker, session, 180)
            if rain is not None:
                controls.append(command(session, "set_rain", rain / 3600000))
            initial = snapshot(session)
        if mode != "worker-only":
            title = f"Cubey live performance | {case.name} | {mode}"
            start = json.loads(recording.read_bytes())["protocol"]["duration_s"] if recording else 0
            argv = terrain.viewer_command(case, session if mode == "live" else recording,
                title, *viewport, profile=prefix, live=mode == "live", start=start)
            result["viewer_command"] = argv
            vlog = (out / "viewer.log").open("wb")
            logs.append(vlog)
            viewer = subprocess.Popen(argv, stdout=vlog, stderr=subprocess.STDOUT, start_new_session=True)
            owned.append(viewer)
            wait_viewer_frame(display.keys, viewer, title)
            display.keys.focus()
            # Recording is paused on an exact mature snapshot. Live starts dry/paused.
            time.sleep(2)
        if worker:
            controls.append(command(session, "resume"))
        started = time.monotonic()
        measure_unix = time.time()
        initial_physical = read_state(session)["frame"]["physical_time_s"] if worker else 0
        result["measurement_start_unix_s"] = measure_unix
        result["measurement_initial_physical_s"] = initial_physical
        next_capture = 1800
        result["captures"] = []
        while True:
            elapsed = time.monotonic() - started
            state = read_state(session) if worker else None
            physical = state["frame"]["physical_time_s"] if state else 0
            row = {"wall_s": elapsed, "physical_s": physical, "unix_s": time.time(),
                   "worker_rss_mib": rss_mib(worker.pid) if worker else 0,
                   "viewer_rss_mib": rss_mib(viewer.pid) if viewer else 0}
            if state:
                row.update(lifecycle=state["frame"]["lifecycle"], sequence=state["frame"]["sequence"],
                           publication_age_s=row["unix_s"] - state["published_unix_s"], next_dt_s=state["next_step_s"])
                if row["lifecycle"] == "failed":
                    raise RuntimeError(state["frame"]["failure_message"])
            if elapsed - last_gpu >= 5:
                gpu = subprocess.run(["rtk", "proxy", "nvidia-smi", "--query-gpu=utilization.gpu,memory.used,power.draw,temperature.gpu",
                    "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=10)
                row["gpu"] = gpu.stdout.strip() if gpu.returncode == 0 else "unavailable"
                last_gpu = elapsed
            samples.append(row)
            write(out / "progress.json", {"case": case.name, "mode": mode, "latest": row})
            if worker and worker.poll() is not None:
                raise RuntimeError("worker exited during measurement")
            if viewer and viewer.poll() is not None:
                raise RuntimeError("viewer exited during measurement")
            if capture and viewer and physical >= next_capture:
                name = f"live-{int(physical)}s.png"
                subprocess.run(["rtk", "proxy", "import", "-window", hex(display.keys.window_id), str(out / name)],
                               check=True, timeout=15, capture_output=True)
                result["captures"].append({"path": name, "observed_physical_s": physical, "unix_s": time.time(),
                                            "kind": "actual live Vulkan GUI on private X11, worker running"})
                next_capture += 1800
            if wall is not None and elapsed >= wall or wall is None and worker and physical >= horizon:
                result["status"] = "completed"
                break
            if elapsed >= cap:
                result["status"] = "timeout"
                break
            time.sleep(1)
        result["measurement_wall_s"] = time.monotonic() - started
        result["measurement_final_physical_s"] = physical
        result["physical_wall_ratio"] = (physical - initial_physical) / result["measurement_wall_s"] if worker else None
        if worker:
            final = snapshot(session)
        if capture and viewer:
            subprocess.run(["rtk", "proxy", "import", "-window", hex(display.keys.window_id), str(out / "final.png")],
                           check=True, timeout=15, capture_output=True)
            result["captures"].append({"path": "final.png", "observed_physical_s": physical, "unix_s": time.time(),
                                        "kind": "actual live Vulkan GUI, worker running"})
            video = subprocess.Popen(["rtk", "proxy", "ffmpeg", "-nostdin", "-y", "-loglevel", "error",
                "-f", "x11grab", "-framerate", "24", "-video_size", f"{viewport[0]}x{viewport[1]}",
                "-i", os.environ["DISPLAY"] + ".0", "-t", "8", "-c:v", "libx264", "-preset", "veryfast",
                "-crf", "21", "-pix_fmt", "yuv420p", str(out / "live.mp4")], stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE, start_new_session=True)
            try:
                _, err = video.communicate(timeout=30)
                if video.returncode:
                    result["errors"].append("video: " + err.decode()[-500:])
                else:
                    result["video"] = "live.mp4"
            finally:
                if video.poll() is None:
                    demo.stop_owned(video)
        result["measurement_end_unix_s"] = measure_unix + result["measurement_wall_s"]
        if control_probe and worker:
            warm = [command(session, "set_time_scale", 60) for _ in range(8)]
            controls.extend(warm)
            result["warm_ack_ms"] = percentiles([c["acknowledgement_ms"] for c in warm])
    except Exception as error:
        result["errors"].append(str(error))
        result["measurement_wall_s"] = time.monotonic() - started if started else 0
    finally:
        if worker and worker.poll() is None and (session / "state.json").exists():
            try:
                controls.append(command(session, "stop"))
                worker.wait(timeout=30)
            except Exception as error:
                result["errors"].append("stop: " + str(error))
        # Escape flushes viewer profile on the private display, no physical GUI targeted.
        if viewer and viewer.poll() is None:
            try:
                display.keys.focus()
                display.keys.key("Escape")
                viewer.wait(timeout=20)
            except Exception as error:
                result["errors"].append("viewer close: " + str(error))
        for process in reversed(owned):
            cleanup.append(demo.stop_owned(process))
        for log in logs:
            log.close()
        result["cleanup"] = cleanup
        result["controls_outside_primary_timing"] = controls
        result["samples"] = samples
        window = (result.get("measurement_start_unix_s", 0), result.get("measurement_end_unix_s", time.time()))
        result["profile"] = profile_summary(prefix, window) if viewer else None
        completion = session / "completion.json"
        result["completion"] = json.loads(completion.read_bytes()) if completion.exists() else None
        telemetry = session / "telemetry.json"
        result["worker_telemetry"] = json.loads(telemetry.read_bytes()) if telemetry.exists() else None
        if worker and (not result["completion"] or not result["completion"].get("inputs_unchanged") or
                       not result["completion"].get("reference_inputs_unchanged")):
            result["errors"].append("missing/changed immutable-input completion receipt")
        result["session_bytes"] = sum(p.stat().st_size for p in session.rglob("*") if p.is_file()) if session.exists() else 0
        result["input_identity_after"] = terrain.case_identity(case)["input_sha256"]
        if result["input_identity_after"] != identity["input_sha256"]:
            result["errors"].append("case input changed")
        if result["errors"] and result["status"] == "completed":
            result["status"] = "completed-with-errors"
        write(out / "result.json", result)
    if initial and final and final[0]["frame"]["physical_time_s"] > 0:
        result["static_recording"] = str(save_recording(case, session, initial, final, out / "static-recording"))
        write(out / "result.json", result)
    print(json.dumps({k: result.get(k) for k in ("case", "mode", "status", "measurement_wall_s", "physical_wall_ratio", "errors")}), flush=True)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--study", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--cases", nargs="+", required=True, help="prepared case directory names")
    p.add_argument("--modes", nargs="+", choices=("worker-only", "live", "renderer-only", "renderer-plus-worker"), default=("worker-only", "live"))
    p.add_argument("--horizon", type=float, default=7200)
    p.add_argument("--cap", type=float, default=600)
    p.add_argument("--wall", type=float)
    p.add_argument("--recording", type=Path)
    p.add_argument("--width", type=int, default=1920)
    p.add_argument("--height", type=int, default=1080)
    p.add_argument("--repeats", type=int, default=1)
    p.add_argument("--capture", action="store_true")
    p.add_argument("--rain", type=float, choices=(512, 1024))
    p.add_argument("--app", type=Path, default=demo.ref.APP, help="explicit renderer artifact; no auto build/fallback")
    p.add_argument("--control-probe", action="store_true", help="eight warm same-pace commands after primary window")
    a = p.parse_args()
    demo.ref.APP = a.app.resolve(strict=True)
    demo.ref.SHADER_ROOT = demo.ref.APP.parent / "shaders"
    global PROBE
    PROBE = demo.ref.APP.parent / "cubey_project_fluid_25d_external_probe"
    out = a.out.resolve()
    if out.exists() or not out.parent.is_dir() or not out.is_relative_to(demo.ref.OUTPUT_ROOT.resolve()):
        p.error("output must be a fresh leaf under outputs/fluid")
    if not (0 < a.horizon <= 72000 and 1 <= a.cap <= 600 and 1 <= a.repeats <= 3 and
            (a.wall is None or 1 <= a.wall <= a.cap) and 320 <= a.width <= 3840 and 240 <= a.height <= 2160):
        p.error("outside approved bounds")
    if any(Path(name).name != name for name in a.cases):
        p.error("case names must be leaf directories")
    if any(mode.startswith("renderer") for mode in a.modes) and not a.recording:
        p.error("isolation modes require an explicit completed binary-capture recording")
    if demo.ref.sha256_file(demo.EXTENSION) != demo.EXTENSION_SHA:
        p.error("audited native extension changed; no solver build/fallback attempted")
    campaign = a.study / "campaign-start.json"
    if not campaign.exists():
        write(campaign, {"unix_s": time.time(), "maximum_wall_s": 8 * 3600})
    deadline = json.loads(campaign.read_bytes())["unix_s"] + 8 * 3600
    out.mkdir()
    write(out / "protocol.json", {"arguments": vars(a) | {"study": str(a.study), "out": str(out), "recording": str(a.recording) if a.recording else None, "app": str(demo.ref.APP)},
        "app_sha256": demo.ref.sha256_file(demo.ref.APP), "shader_identity": demo.ref.shader_identity(),
        "extension_sha256": demo.ref.sha256_file(demo.EXTENSION), "worker_sha256": demo.ref.sha256_file(terrain.HERE / "external_synxflow/session_worker.py"),
        "created_unix_s": time.time(), "profile_scope": "private X11, real Vulkan; sampled frames; CPU download and wall progress, not pure CUDA kernels",
        "display": "owned Xvfb 3840x2160; actual viewport verified in profiles; no physical scanout"})
    rows = []
    display = Display(out)
    try:
        for repeat in range(a.repeats):
            for name in a.cases:
                case = (a.study / "cases" / name).resolve(strict=True)
                if not case.is_relative_to((a.study / "cases").resolve()):
                    raise ValueError("case escaped prepared study")
                for mode in a.modes:
                    if time.time() >= deadline:
                        write(out / "results.json", {"trials": rows, "campaign_budget_exhausted": True})
                        return 0
                    label = f"{name}-{mode}-{repeat}"
                    result = run_trial(case, out / label, mode, display, horizon=a.horizon,
                        cap=a.cap, wall=a.wall, viewport=(a.width, a.height), recording=a.recording,
                        capture=a.capture and mode == "live", rain=a.rain, control_probe=a.control_probe)
                    rows.append({"directory": label, **{k: result.get(k) for k in
                        ("case", "mode", "status", "measurement_wall_s", "physical_wall_ratio", "errors")}})
                    write(out / "results.json", {"trials": rows})
    finally:
        write(out / "display-cleanup.json", display.close())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
