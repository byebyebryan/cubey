#!/usr/bin/env python3
"""Bounded rain-response evidence using the existing managed headless demo.

MIT client-side observer only. No native imports, installs, input edits or GUI.
Published fields are retained exactly; recording rain knots describe the observed
switch (a one-microsecond display taper), not a native conservation certificate.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import time

import run_terrain_rain_demo as demo

SCHEMA = "cubey.fluid25d.rain-response.v1"


def write(path, value):
    with Path(path).open("x") as file:
        json.dump(value, file, indent=2, sort_keys=True, allow_nan=False)
        file.write("\n")


def metrics(planes, shape, spacing):
    import numpy as np
    from run_live_perf_terrain_v1 import field_stats
    result = field_stats(planes, spacing)
    h, qx, qz = np.frombuffer(planes, dtype="<f4").reshape(3, *shape)
    speed = np.hypot(qx, qz) / np.maximum(h, 1e-30)
    wet = h >= .1
    # Match the existing mountain observation cameras, not new sources/sinks.
    for label, center in (("runoff", (311 / 511, 392 / 511)),
                          ("collection", (105 / 511, 474 / 511))):
        y, x = (int(center[0] * (shape[0] - 1)), int(center[1] * (shape[1] - 1)))
        radius = max(1, round(1000 / spacing))
        region = h[max(0, y-radius):min(shape[0], y+radius+1),
                   max(0, x-radius):min(shape[1], x+radius+1)]
        result[label + "_volume_m3"] = float(region.sum(dtype=np.float64) * spacing**2)
    result.update(wet_km2=float(wet.sum() * spacing**2 / 1e6),
                  flowing_km2=float(np.count_nonzero(wet & (speed >= .5)) * spacing**2 / 1e6),
                  wet_speed_p95_m_per_s=float(np.percentile(speed[wet], 95)) if wet.any() else 0)
    return result


def actions(rain_off_s, end_s, rain_on_control=False):
    if not (math.isfinite(rain_off_s) and math.isfinite(end_s)
            and 0 < rain_off_s < end_s <= 72000):
        raise ValueError("rain-off/end must be ordered within the prepared horizon")
    sequence = [{"kind": "resume", "after_s": 1}]
    if not rain_on_control:
        sequence.append({"kind": "set-rain", "at_physical_s": rain_off_s, "value": 0})
    return sequence + [{"kind": "pause", "at_physical_s": end_s}]


def seal_recording(case, session, recording, frames, captures, off_s):
    spec = json.loads((case / "case-spec.json").read_bytes())
    for name, source in (("bed.f32", session / "bed.f32"),
                         ("source-bed.f32", case / "source-bed.f32")):
        shutil.copy2(source, recording / name)
    duration = frames[-1]["time_s"]
    rain = captures[0]["rain_m_per_s"]
    history = ([[0, rain], [off_s - 1e-6, rain], [off_s, 0], [duration, 0]]
               if off_s is not None else [[0, rain], [duration, rain]])
    document = {"schema": "cubey.fluid25d.recording.v1", "encoding": "float32-little-endian",
        "field_layout": "planar", "fields": ["h_m", "qx_m2_per_s", "qz_m2_per_s"],
        "grid": {"width": spec["cols"], "height": spec["rows"], "cell_size_m": spec["dx_m"],
            "storage_order": "row-major", "row_direction": "world-z-positive", "sample_location": "cell-center"},
        "bed": {"path": "bed.f32", "sha256": demo.runtime.sha256_file(recording / "bed.f32")},
        "source_bed": {"path": "source-bed.f32", "sha256": demo.runtime.sha256_file(recording / "source-bed.f32")},
        "frames": frames, "protocol": dict(spec, duration_s=duration, output_interval_s=60,
            rainfall_history=history),
        "provenance": {"capture_schema": "cubey.fluid25d.external-binary-capture.v1",
            "field_source": "validated external service binary snapshots",
            "purpose": "changing hydraulic fields; held between irregular observed snapshots; no physical interpolation",
            "rain_history_policy": "observed acknowledgement time; one-microsecond display taper only; not an exact native source ledger",
            "captures": captures, "case_protocol_sha256": demo.runtime.sha256_file(case / "case-protocol.json"),
            "extension_sha256": demo.runtime.EXTENSION_SHA,
            "source_sha256": {"case_spec": demo.runtime.sha256_file(case / "case-spec.json"),
                "case_protocol": demo.runtime.sha256_file(case / "case-protocol.json"),
                "metadata": demo.runtime.sha256_file(session / "metadata.json"),
                "native_numerical_bed": demo.runtime.sha256_file(session / "bed.f32")}}}
    write(recording / "recording.json", document)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--study", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--app", type=Path, default=demo.RELEASE_APP)
    p.add_argument("--reseal", type=Path, help="verify and reseal an existing study into a fresh leaf; no solver")
    p.add_argument("--rain-on-control", action="store_true", help="same dry start/rain, but no shutoff")
    p.add_argument("--rain-off-s", type=float, default=7200)
    p.add_argument("--end-s", type=float, default=21600)
    p.add_argument("--wall-seconds", type=float, default=480)
    a = p.parse_args()
    script = actions(a.rain_off_s, a.end_s, a.rain_on_control)
    if not math.isfinite(a.wall_seconds) or not 30 <= a.wall_seconds <= 1800:
        p.error("wall duration must be 30..1800 seconds")
    case = (a.study / "cases/mountain-patch-1024-16m").resolve(strict=True)
    identity = demo.case_identity(case)
    if (identity["spec"]["cols"], identity["spec"]["rows"], identity["spec"]["dx_m"],
        identity["spec"]["rain_rate_mm_per_h"]) != (1024, 1024, 16, 512):
        raise ValueError("study requires the agreed Mountain 1k/16m/rain512 case")
    out = demo.runtime.fresh_output(a.out)
    out.mkdir()
    recording = out / "recording"
    recording.mkdir()
    if a.reseal:
        original = a.reseal.resolve(strict=True)
        result = json.loads((original / "result.json").read_bytes())
        protocol = json.loads((original / "protocol.json").read_bytes())
        prior = json.loads((original / "recording/recording.json").read_bytes())
        if not result["passed"] or protocol["identity"] != identity or len(result["frames"]) > 512:
            raise ValueError("cannot reseal failed, changed or oversized study")
        for frame in result["frames"]:
            source = original / "recording" / frame["path"]
            if source.parent != original / "recording" or source.is_symlink():
                raise ValueError("unsafe frame path")
            if demo.runtime.sha256_file(source) != frame["sha256"]:
                raise ValueError("captured frame changed")
            os.link(source, recording / source.name)
        session = original / "trial/sessions/generation-00000000000000000000"
        if demo.runtime.sha256_file(session / "bed.f32") != identity["spec"]["terrain_source_identity"]["solver_bed_sha256"]:
            raise ValueError("published solver bed changed")
        seal_recording(case, session, recording, result["frames"], prior["provenance"]["captures"],
                       result["rain_off_observed_s"])
        write(out / "result.json", dict(result, resealed_from=str(original), numerical_rerun=False))
        write(out / "protocol.json", protocol)
        print(json.dumps({"resealed": str(out), "frames": len(result["frames"]), "numerical_rerun": False}))
        return 0
    from run_live_perf_terrain_v1 import snapshot
    write(out / "script.json", {"schema": demo.managed.SCHEMA, "actions": script})
    command = ["python3", str(demo.HERE / "run_terrain_rain_demo.py"), "--study", str(a.study.resolve()),
        "--app", str(a.app.resolve(strict=True)), "--out", str(out / "trial"), "--headless",
        "--run-seconds", str(a.wall_seconds), "--switch-script", str(out / "script.json")]
    write(out / "protocol.json", {"schema": SCHEMA, "identity": identity, "command": command,
        "actions": script, "snapshot_cadence_physical_s": 60, "maximum_frames": 512,
        "rain_on_control": a.rain_on_control,
        "observer_sha256": demo.runtime.sha256_file(Path(__file__)),
        "scope": "hydraulic/presentation study with offscreen Vulkan; observer I/O excluded from performance claims"})
    shutil.copy2(__file__, out / "observer.py")
    started = time.monotonic()
    frames, captures, off_s, session_id = [], [], None, None
    next_s, reached = 0, False
    session = out / "trial/sessions/generation-00000000000000000000"
    with (out / "launcher.log").open("xb") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            while process.poll() is None:
                if time.monotonic() - started > a.wall_seconds + 180:
                    raise TimeoutError("study exceeded bounded startup/shutdown allowance")
                if not (session / "state.json").exists():
                    time.sleep(.1)
                    continue
                state = json.loads((session / "state.json").read_bytes())
                t = state["frame"]["physical_time_s"]
                is_off = state["rain_m_per_s"] == 0
                terminal = state["frame"]["lifecycle"] in ("failed", "stopped", "completed")
                if terminal:
                    raise RuntimeError("unexpected worker terminal state: " + state["frame"]["lifecycle"])
                endpoint = t >= a.end_s and state["frame"]["lifecycle"] == "paused"
                if t >= next_s or (is_off and off_s is None) or endpoint:
                    state, planes, capture = snapshot(session)
                    header = state["frame"]
                    t = header["physical_time_s"]
                    if session_id is None:
                        session_id = header["session_id"]
                    if header["session_id"] != session_id or header["reset_generation"] != 1:
                        raise ValueError("hydraulic identity changed inside study")
                    if state["rain_m_per_s"] == 0 and off_s is None:
                        ack = state["ack"]
                        history = json.loads((session / "controls.json").read_bytes())["retained"]
                        applied = [item for item in history if ack and
                            item["command"]["command_id"] == ack["command_id"] and
                            item["command"]["kind"] == "set-rain" and item["command"]["value"] == 0]
                        if not ack or ack["state"] != "applied" or len(applied) != 1:
                            raise ValueError("rain-off has no applied acknowledgement")
                        off_s = ack["application_time_s"]
                    if not frames or t > frames[-1]["time_s"]:
                        if len(frames) >= 512:
                            raise ValueError("bounded snapshot archive exhausted")
                        path = recording / f"frame-{len(frames):04d}.f32"
                        with path.open("xb") as file:
                            file.write(planes)
                        row = {"time_s": t, "path": path.name, "sha256": hashlib.sha256(planes).hexdigest(),
                               **metrics(planes, (1024, 1024), 16)}
                        frames.append(row)
                        captures.append({**{k: v for k, v in capture.items() if k != "_state_raw"},
                            "frame": header, "ack": state["ack"], "rain_m_per_s": state["rain_m_per_s"],
                            "wall_s": time.monotonic()-started})
                        print(json.dumps({"physical_s": t, "rain_mm_per_h": state["rain_m_per_s"]*3.6e6,
                            **{k: v for k, v in row.items() if k not in ("path", "sha256")}}), flush=True)
                        next_s = t + 60
                    if endpoint:
                        reached = True
                        time.sleep(.5)  # Allow the viewer to log the final applied acknowledgement.
                        break
                time.sleep(.05)
        finally:
            cleanup = demo.runtime.stop_owned(process, grace_s=60)
    receipt = json.loads((out / "trial/launch.json").read_bytes())
    errors = []
    if not reached or (off_s is None and not a.rain_on_control):
        errors.append("incomplete physical rain-response sequence")
    if a.rain_on_control and off_s is not None:
        errors.append("rain-on control unexpectedly lost supply")
    if demo.case_identity(case) != identity:
        errors.append("immutable case changed")
    if cleanup.get("active_group_remaining") or any(item.get("active_group_remaining") for item in receipt["cleanup"]):
        errors.append("owned process cleanup failed")
    viewer = (out / "trial/viewer.log").read_text(errors="replace")
    if "Validation Error" in viewer or "VUID-" in viewer:
        errors.append("Vulkan validation error")
    if not errors:
        seal_recording(case, session, recording, frames, captures, off_s)
    result = {"schema": SCHEMA, "passed": not errors, "errors": errors, "frames": frames,
        "rain_off_observed_s": off_s, "session_id": session_id, "cleanup": cleanup,
        "launcher_cleanup": receipt["cleanup"], "app_sha256": receipt["app_sha256"],
        "immutable_case_unchanged": demo.case_identity(case) == identity, "wall_s": time.monotonic()-started,
        "controls": json.loads((session / "controls.json").read_bytes()),
        "rain_on_control": a.rain_on_control}
    write(out / "result.json", result)
    print(json.dumps({k: v for k, v in result.items() if k != "frames"}), flush=True)
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
