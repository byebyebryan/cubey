#!/usr/bin/env python3
"""Explicit continuous native/GUI soak; never part of ordinary configure/tests."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import time

from run_external_service_controls_v1 import percentile, write
from run_live_view_acceptance_v1 import close_owned_window, profile, scoped_capture, stop_owned

HERE = Path(__file__).resolve().parent


def process_rss(pid):
    # rtk proxy may own a child rather than exec it. Include only descendants
    # of this explicitly owned process, not an unrelated machine-wide match.
    children = Path(f"/proc/{pid}/task/{pid}/children").read_text().split()
    own = None
    for line in Path(f"/proc/{pid}/status").read_text().splitlines():
        if line.startswith("VmRSS:"):
            own = int(line.split()[1]) / 1024
            break
    if own is None:
        raise RuntimeError("owned process RSS is unavailable")
    return own + sum(process_rss(int(child)) for child in children)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("extension", "case", "probe", "viewer", "protocol", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    protocol = json.loads(args.protocol.read_bytes())
    if protocol.get("schema") != "cubey.fluid25d.external-lifetime-protocol.v1" or not protocol.get("frozen_before_candidate_execution"):
        raise ValueError("frozen lifetime protocol required")
    extension, case, probe, viewer = [getattr(args, name).resolve(strict=True) for name in ("extension", "case", "probe", "viewer")]
    if hashlib.sha256(extension.read_bytes()).hexdigest() != protocol["extension_sha256"]:
        raise ValueError("extension does not match frozen protocol")
    out = args.out.absolute()
    if out.exists() or out.is_symlink() or not out.parent.is_dir():
        raise ValueError("fresh study output leaf required")
    out.mkdir()
    original_viewer = viewer
    viewer = out / "fluid_25d-tested"
    shutil.copy2(original_viewer, viewer)
    session = out / "session"
    native_command = ["rtk", "proxy", sys.executable, str(HERE / "external_synxflow/session_worker.py"),
                      "--extension", str(extension), "--case", str(case), "--out", str(session), "--speed", "60"]
    title = "Cubey external continuous soak " + out.name
    viewer_command = ["rtk", "proxy", str(viewer), "--fluid25d-external-session", str(session),
                      "--width", "960", "--height", "640", "--frames", "0", "--title", title,
                      "--profile-output", str(out / "concurrent"), "--profile-warmup-frames", "60",
                      "--fluid25d-recording-camera", "overview", "--fluid25d-catchment-view", "composite"]
    observations, rss_samples, captures, controls = [], [], [], []
    window = None
    with (out / "worker.log").open("wb") as native_log, (out / "viewer.log").open("wb") as view_log:
        worker = subprocess.Popen(native_command, stdout=native_log, stderr=subprocess.STDOUT, start_new_session=True)
        def observe(kind=None, value=None):
            argv = ["rtk", "proxy", str(probe), str(session)]
            if kind:
                argv.append(kind)
            if value is not None:
                argv.append(str(value))
            result = subprocess.run(argv, text=True, capture_output=True, timeout=8)
            if result.returncode:
                raise RuntimeError("MIT probe failed: " + result.stdout + result.stderr)
            item = json.loads(result.stdout)
            item["command"] = kind
            if kind:
                assert item["ack"]["state"] == "applied", item
                controls.append(item)
            return item
        try:
            deadline = time.monotonic() + 30
            while not (session / "state.json").is_file():
                assert worker.poll() is None, "worker failed before initialization"
                assert time.monotonic() < deadline, "native initialization deadline"
                time.sleep(.05)
            initial = observe()
            assert initial["metadata"]["grid"]["width"] == 512
            assert initial["metadata"]["grid"]["height"] == 512
            window = subprocess.Popen(viewer_command, stdout=view_log, stderr=subprocess.STDOUT)
            started = time.monotonic()
            next_sample, next_capture = 0, 60
            rain_off = rain_on = False
            while time.monotonic() - started < protocol["soak_wall_s"]:
                elapsed = time.monotonic() - started
                assert worker.poll() is None, "continuous worker exited unexpectedly"
                assert window.poll() is None, "viewer exited unexpectedly"
                if elapsed >= next_sample:
                    state = json.loads((session / "state.json").read_bytes())
                    assert state["frame"]["lifecycle"] == "running"
                    assert state["frame"]["reset_generation"] == 1
                    observations.append({"elapsed_wall_s": elapsed, "state": state})
                    rss_samples.append({"elapsed_wall_s": elapsed, "worker_rss_mib": process_rss(worker.pid),
                                        "viewer_rss_mib": process_rss(window.pid)})
                    next_sample += 1
                if elapsed >= next_capture:
                    path = out / f"live-{next_capture:03d}s.png"
                    if scoped_capture(title, path):
                        captures.append({"elapsed_wall_s": elapsed, "path": str(path),
                                         "state": json.loads((session / "state.json").read_bytes())})
                    next_capture += 120
                if elapsed >= 120 and not rain_off:
                    observe("set_rain", 0)
                    rain_off = True
                if elapsed >= 180 and not rain_on:
                    observe("set_rain", 120 / 3.6e6)
                    rain_on = True
                time.sleep(.05)
            final_running = observe()
            assert final_running["frame"]["physical_time_s"] >= protocol["minimum_final_physical_s"]
            assert rain_off and rain_on and captures
            warm = []
            for _ in range(protocol["warm_ack_samples"] // 2):
                warm.append(observe("pause")["acknowledgement_ms"])
                warm.append(observe("resume")["acknowledgement_ms"])
            assert percentile(warm) <= protocol["warm_ack_p95_target_ms"]
            paused = observe("pause")
            # Matched same state/viewport, CUDA idle; presentation cues are disabled
            # in this composite view, so pause does not remove a quiver workload.
            time.sleep(2)
            idle_title = title + " idle"
            idle_command = viewer_command.copy()
            idle_command[idle_command.index(title)] = idle_title
            idle_command[idle_command.index(str(out / "concurrent"))] = str(out / "idle")
            close_owned_window(title)
            assert window.wait(timeout=120) == 0 # Large explicit profile export follows GUI close.
            window = subprocess.Popen(idle_command, stdout=view_log, stderr=subprocess.STDOUT)
            idle_started = time.monotonic()
            while time.monotonic() - idle_started < 12:
                assert window.poll() is None and worker.poll() is None
                time.sleep(.1)
            scoped_capture(idle_title, out / "paused-water.png")
            close_owned_window(idle_title)
            assert window.wait(timeout=120) == 0
            stop = observe("stop")
            assert stop["frame"]["lifecycle"] == "stopped"
            assert worker.wait(timeout=5) == 0
            completion = json.loads((session / "completion.json").read_bytes())
            assert completion["inputs_unchanged"] and completion["reference_inputs_unchanged"]
            assert len(list(session.glob("slot-*.bin"))) == protocol["service_slots"]
            assert len(json.loads((session / "controls.json").read_bytes())["retained"]) <= protocol["control_history_limit"]
            assert len(list((session / "native/output").iterdir())) == protocol["native_output_file_count"]
            assert not list(session.glob("*.tmp"))
            concurrent = profile(out / "concurrent")
            idle = profile(out / "idle")
            assert concurrent["observed_wall_s"] >= protocol["soak_wall_s"] - 2
            assert concurrent["p95_delta_ms"] <= protocol["frame_p95_target_ms"]
            assert idle["p95_delta_ms"] <= protocol["frame_p95_target_ms"]
            assert concurrent["extents"] == idle["extents"]
            freshness = []
            dispatches = []
            with (out / "concurrent.metrics.csv").open() as stream:
                for row in csv.DictReader(stream):
                    if row["category"] == "fluid_25d.external":
                        if row["name"] == "publication_freshness_s":
                            freshness.append(float(row["value"]))
                        elif row["name"] == "hydraulic_dispatch_count":
                            dispatches.append(float(row["value"]))
            assert dispatches and all(value == 0 for value in dispatches)
            assert percentile(freshness) <= protocol["publication_to_command_record_p95_target_s"]
            warm_rss = [row["worker_rss_mib"] for row in rss_samples if row["elapsed_wall_s"] >= 60]
            assert max(warm_rss) - min(warm_rss) <= protocol["worker_rss_after_warmup_growth_limit_mib"]
            log = (out / "viewer.log").read_text()
            assert "VIEWER ERROR" not in log and "vulkan validation error" not in log.lower()
            result = {"schema": "cubey.fluid25d.external-lifetime-result.v1", "passed": True,
                      "protocol": str(args.protocol), "host": platform.node(),
                      "native_command": native_command, "viewer_command": viewer_command,
                      "viewer_sha256": hashlib.sha256(viewer.read_bytes()).hexdigest(),
                      "original_viewer": str(original_viewer),
                      "soak_wall_s": protocol["soak_wall_s"], "final_running": final_running,
                      "paused": paused, "concurrent_profile": concurrent, "idle_profile": idle,
                      "shared_gpu_p95_ratio": concurrent["p95_delta_ms"] / idle["p95_delta_ms"],
                      "publication_freshness_p95_s": percentile(freshness),
                      "publication_freshness_max_s": max(freshness), "warm_ack_p95_ms": percentile(warm),
                      "rss_samples": rss_samples, "observations": observations, "captures": captures,
                      "controls": controls, "completion": completion,
                      "human_gui_acceptance": False, "scanout_timing": False}
            write(out / "result.json", result)
            print(json.dumps({"passed": True, "out": str(out), "p95_frame_ms": concurrent["p95_delta_ms"],
                              "freshness_p95_s": percentile(freshness)}), flush=True)
            return 0
        except Exception as error:
            write(out / "failure.json", {"passed": False, "error": repr(error), "observations": observations,
                  "rss_samples": rss_samples, "controls": controls, "captures": captures})
            raise
        finally:
            if window is not None:
                stop_owned(window)
            if worker.poll() is None:
                os.killpg(worker.pid, signal.SIGINT)
                try:
                    worker.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    os.killpg(worker.pid, signal.SIGTERM)
                    worker.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
