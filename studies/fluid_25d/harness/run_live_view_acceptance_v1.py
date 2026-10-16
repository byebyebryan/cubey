#!/usr/bin/env python3
"""Opt-in, finite native/live-view acceptance; never modifies the CUDA solver."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
BASELINE = ROOT / "outputs/fluid/native-rain-recession-v1-20261003-1ZMqTJ"
PYTHON = ROOT / "outputs/fluid/native-runoff-reuse-v1-20261002-trm3Ve/synxflow-setup/env/.venv/bin/python"
TARGET = ROOT / "build/dev/projects/fluid/fluid_25d/fluid_25d"
FOLLOW_VIEW_WALL_S = 20.0
PACED_VIEW_WALL_S = 10.0
MINIMUM_PROFILE_WALL_S = 9.0


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            value.update(block)
    return value.hexdigest()


def write(path: Path, data) -> None:
    path.write_text(json.dumps(data, indent=2, sort_keys=True, allow_nan=False) + "\n")


def read(path: Path):
    return json.loads(path.read_text())


def command_output(command: list[str]) -> str:
    return subprocess.check_output(["rtk", "proxy", *command], cwd=ROOT, text=True, timeout=15).strip()


def percentile(values: list[float], fraction: float = .95) -> float:
    assert values
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * fraction) - 1)]


def profile(path: Path) -> dict:
    with Path(str(path) + ".frames.csv").open() as source:
        rows = list(csv.DictReader(source))
    return {"count": len(rows), "p95_delta_ms": percentile([float(row["delta_ms"]) for row in rows]),
            "observed_wall_s": sum(float(row["delta_ms"]) for row in rows) / 1000.0,
            "extents": [list(extent) for extent in sorted({(int(row["width"]), int(row["height"])) for row in rows})]}


def owned_window_id(title: str) -> str | None:
    result = subprocess.run(["rtk", "proxy", "niri", "msg", "--json", "windows"],
                            capture_output=True, text=True, timeout=5)
    if result.returncode:
        return None
    matches = [window for window in json.loads(result.stdout) if window.get("title") == title]
    if len(matches) != 1:
        return None
    return str(matches[0]["id"])


def close_owned_window(title: str) -> None:
    window_id = owned_window_id(title)
    if window_id is None:
        raise RuntimeError(f"could not resolve this acceptance window for clean close: {title}")
    subprocess.run(["rtk", "proxy", "niri", "msg", "action", "close-window", "--id", window_id],
                   check=True, capture_output=True, text=True, timeout=5)


def scoped_capture(title: str, path: Path) -> bool:
    window_id = owned_window_id(title)
    if window_id is None:
        return False
    result = subprocess.run(["rtk", "proxy", "niri", "msg", "action", "screenshot-window",
                             "--id", window_id, "--path", str(path.resolve())],
                            capture_output=True, text=True, timeout=5)
    return result.returncode == 0


def stop_owned(process: subprocess.Popen) -> None:
    if process.poll() is None:
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=15)


def viewer_command(target: Path, source: Path, title: str, prefix: Path, live: bool,
                   follow: bool) -> list[str]:
    # Mailbox presentation is not a 60Hz timer. The harness closes only its
    # uniquely identified window after a predeclared wall-clock interval.
    command = ["rtk", "proxy", str(target), "--width", "960", "--height", "640", "--frames", "0",
               "--title", title, "--profile-output", str(prefix), "--profile-warmup-frames", "60",
               "--fluid25d-stream" if live else "--fluid25d-recording", str(source),
               "--fluid25d-recording-camera", "overview", "--fluid25d-catchment-view", "composite"]
    if follow:
        command.append("--fluid25d-stream-follow-latest")
    return command


def native_case(out: Path, label: str, target: Path, *, follow: bool = True) -> dict:
    baseline = BASELINE / "cases" / f"mountain-rain-{label}-14400s"
    run = out / (f"rain-{label}" if follow else f"rain-{label}-paced")
    run.mkdir()
    publisher_command = ["rtk", "proxy", sys.executable, str(HERE / "live_synxflow_session_v1.py"),
                         "--baseline-case", str(baseline), "--out", str(run / "session"), "--python", str(PYTHON)]
    title = f"Cubey live bridge acceptance {run.name}"
    viewer = None
    captures = []
    start = time.monotonic()
    with (run / "publisher.log").open("w") as publisher_log, (run / "viewer.log").open("w") as viewer_log:
        publisher = subprocess.Popen(publisher_command, stdout=publisher_log, stderr=subprocess.STDOUT)
        try:
            while not (run / "session/stream.json").is_file():
                if publisher.poll() is not None:
                    raise RuntimeError(f"publisher exited before frame0: {publisher.returncode}")
                if time.monotonic() - start > 120:
                    raise RuntimeError("first ready snapshot deadline exceeded")
                time.sleep(.1)
            command = viewer_command(target, run / "session", title, run / "concurrent", True, follow)
            viewer = subprocess.Popen(command, stdout=viewer_log, stderr=subprocess.STDOUT)
            viewer_start = time.monotonic()
            view_duration = FOLLOW_VIEW_WALL_S if follow else PACED_VIEW_WALL_S
            close_requested = False
            next_capture = 2.0
            while publisher.poll() is None or viewer.poll() is None:
                elapsed = time.monotonic() - viewer_start
                if viewer.poll() is None and elapsed >= view_duration and not close_requested:
                    close_owned_window(title)
                    close_requested = True
                if viewer.poll() is None and elapsed > view_duration + 15:
                    raise RuntimeError("owned viewer did not exit after clean window close")
                if viewer.poll() is None and elapsed >= next_capture:
                    destination = run / f"live-{int(next_capture):03d}s.png"
                    capture_started = time.time()
                    if scoped_capture(title, destination):
                        captures.append({"path": str(destination.relative_to(out)),
                                         "started_unix_s": capture_started, "unix_s": time.time(),
                                         "producer": read(run / "session/stream.json")["producer"]})
                    next_capture += 10.0
                if time.monotonic() - start > 600:
                    raise RuntimeError("finite native/viewer acceptance deadline exceeded")
                time.sleep(.1)
            assert publisher.returncode == viewer.returncode == 0, (publisher.returncode, viewer.returncode)
        finally:
            if viewer is not None:
                stop_owned(viewer)
            stop_owned(publisher)

    from convert_synxflow_recording_v1 import convert_case
    completed = convert_case(run / "session/case", run / "independent-recording")
    expected = read(completed)
    live = read(run / "session/stream.json")
    assert live["producer"]["state"] == "completed" and not live["producer"]["native_running"]
    assert len(live["frames"]) == len(expected["frames"]) == 241
    for native_field in ("h", "hUx", "hUy"):
        for timestamp in range(0, 14401, 60):
            name = f"{native_field}_{timestamp}.asc"
            assert digest(run / "session/case/native/output" / name) == digest(baseline / "native/output" / name), name
    for imported, converted in zip(live["frames"], expected["frames"], strict=True):
        assert imported["time_s"] == converted["time_s"]
        assert imported["sha256"] == converted["sha256"]
        assert digest(run / "session" / imported["path"]) == converted["sha256"]

    result = read(run / "session/case/case-result.json")
    started = result["native_call_started_unix_s"]
    finished = result["native_call_finished_unix_s"]
    events = []
    for line in (run / "viewer.log").read_text().splitlines():
        if line.startswith("fluid_25d_stream_display:"):
            fields = dict(re.findall(r"([a-z_]+)=([^ ]+)", line))
            events.append({key: float(fields[key]) for key in ("saved_s", "displayed_unix_s", "ready_to_display_s", "native_running")})
    during = [event for event in events if event["saved_s"] > 0 and started <= event["displayed_unix_s"] < finished]
    assert len(during) >= 3, "viewer did not receive several actual frames during native flood.run"
    assert all(event["native_running"] == 1 for event in during), "native computing status disagreed with actual call interval"
    captures_during = [capture for capture in captures
                       if started <= capture["started_unix_s"] <= capture["unix_s"] < finished]
    assert captures_during, "no scoped GUI capture completed while native flood.run was computing"
    delays = [event["ready_to_display_s"] for event in events if event["saved_s"] > 0]
    assert delays and min(delays) >= 0
    delay_p95 = percentile(delays)
    if follow:
        assert delay_p95 < 1.0, f"follow-latest ready-to-command-record p95 {delay_p95:.3f}s exceeded1s"
    assert "VIEWER ERROR" not in (run / "viewer.log").read_text()
    assert "vulkan validation error" not in (run / "viewer.log").read_text().lower()
    concurrent_profile = profile(run / "concurrent")
    assert concurrent_profile["observed_wall_s"] >= MINIMUM_PROFILE_WALL_S, "concurrent profile observation too short"
    data = {"baseline": str(baseline), "native_command": publisher_command, "viewer_command": command,
            "native_call_started_unix_s": started, "native_call_finished_unix_s": finished,
            "displayed_during_native_count": len(during), "displayed_saved_times_during_native_s": [event["saved_s"] for event in during],
            "ready_to_command_record_p95_s": delay_p95, "ready_to_command_record_max_s": max(delays),
            "follow_latest": follow,
            "all241_payloads_match_converter": True, "all723_native_fields_match_frozen_baseline": True,
            "captures": captures, "captures_during_native_count": len(captures_during),
            "concurrent_window_profile": concurrent_profile, "requested_view_wall_s": view_duration,
            "timing_boundary": "Vulkan command recording, not proof of physical monitor scanout; scoped GUI captures are separate"}
    write(run / "results.json", data)
    print(json.dumps({"case": label, **data}), flush=True)
    return data


def matched_viewer_benchmark(out: Path, target: Path) -> dict:
    directory = out / "matched-viewer"
    directory.mkdir()
    rows = []
    for repeat in range(2):
        for live in (False, True):
            label = f"{'live' if live else 'recorded'}-{repeat}"
            source = out / ("rain-on/session" if live else "rain-on/independent-recording")
            prefix = directory / label
            title = f"Cubey matched bridge {label}"
            command = viewer_command(target, source, title, prefix, live, False)
            with (directory / f"{label}.log").open("w") as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
                try:
                    started = time.monotonic()
                    while time.monotonic() - started < PACED_VIEW_WALL_S:
                        assert process.poll() is None, "matched viewer exited before its observation interval"
                        time.sleep(.1)
                    close_owned_window(title)
                    assert process.wait(timeout=15) == 0
                finally:
                    stop_owned(process)
            measured = profile(prefix)
            assert measured["observed_wall_s"] >= MINIMUM_PROFILE_WALL_S, "matched profile observation too short"
            rows.append({"live": live, "repeat": repeat, "command": command, **measured})
    assert len({tuple(tuple(size) for size in row["extents"]) for row in rows}) == 1, "benchmark actual viewport mismatch"
    recorded = sorted(row["p95_delta_ms"] for row in rows if not row["live"])
    live = sorted(row["p95_delta_ms"] for row in rows if row["live"])
    ratio = (sum(live) / len(live)) / (sum(recorded) / len(recorded))
    assert ratio <= 1.2, f"matched completed-prefix live/recorded p95 frame-time ratio {ratio:.3f} >1.2"
    result = {"runs": rows, "p95_frame_time_ratio": ratio,
              "boundary": "Matched completed-prefix rendering without concurrent CUDA; concurrent profiles reported separately"}
    write(directory / "results.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--target", type=Path, default=TARGET)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=False, exist_ok=False)
    target = args.target.resolve()
    for name in ("run_live_view_acceptance_v1.py", "live_synxflow_session_v1.py", "convert_synxflow_recording_v1.py"):
        shutil.copyfile(HERE / name, out / name)
    write(out / "protocol.json", {"schema": "cubey.fluid25d.live-view-acceptance.v1", "created_unix_s": time.time(),
          "runner_sha256": digest(Path(__file__)), "publisher_sha256": digest(HERE / "live_synxflow_session_v1.py"),
          "converter_sha256": digest(HERE / "convert_synxflow_recording_v1.py"),
          "target_sha256": digest(target), "pinned_python": str(PYTHON), "baseline_root": str(BASELINE),
          "host": platform.node(), "git_head": command_output(["git", "rev-parse", "HEAD"]),
          "git_worktree_status": command_output(["git", "status", "--short"]),
          "gpu_identity": command_output(["nvidia-smi", "--query-gpu=name,uuid,driver_version,memory.total", "--format=csv,noheader"]),
          "cases": ["rain-on-follow-latest", "rain-off-follow-latest", "rain-on-paced-render-repeat"], "frame_count_each": 241, "native_output_cadence_s": 60,
          "latency_gate": "p95 nonzero-time follow-latest ready-to-command-record<1s; report maximum; frame0 initialization separate",
          "follow_view_wall_s": FOLLOW_VIEW_WALL_S, "paced_view_wall_s": PACED_VIEW_WALL_S,
          "minimum_profile_wall_s": MINIMUM_PROFILE_WALL_S,
          "render_gate": "same viewport, two sequential paired10s completed-prefix runs AND10s paced view while native computes; >=9s measured each; both p95 live/recorded<=1.2",
          "live_gate": ">=3 nonzero-time uploads and >=1 scoped GUI capture inside actual flood.run interval per case; computing status must agree",
          "preserved": "solver binary, input serialization, terrain, physics, output cadence; no CUDA interoperability",
          "human_gui_acceptance": "not implied by instrumentation or automated capture"})
    try:
        cases = {label: native_case(out, label, target) for label in ("on", "off")}
        concurrent_paced = native_case(out, "on", target, follow=False)
        performance = matched_viewer_benchmark(out, target)
        recorded = [row["p95_delta_ms"] for row in performance["runs"] if not row["live"]]
        assert concurrent_paced["concurrent_window_profile"]["extents"] == performance["runs"][0]["extents"]
        concurrent_ratio = concurrent_paced["concurrent_window_profile"]["p95_delta_ms"] / (sum(recorded) / len(recorded))
        assert concurrent_ratio <= 1.2, f"concurrent paced live/recorded p95 frame-time ratio {concurrent_ratio:.3f}>1.2"
        performance["concurrent_paced_frame_time_ratio"] = concurrent_ratio
        performance["concurrent_paced"] = concurrent_paced
        write(out / "completion.json", {"status": "automated_live_bridge_gates_passed", "cases": cases,
                                       "performance": performance, "human_gui_acceptance": "pending"})
    except BaseException as error:
        write(out / "acceptance-failure.json", {"status": "failed", "type": type(error).__name__, "error": str(error)})
        raise
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
