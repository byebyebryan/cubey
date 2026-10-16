#!/usr/bin/env python3
"""Finite diagnostic: recorded viewing under the same unchanged CUDA workload.

This does not erase the frozen idle-GPU performance failure or substitute for
the full publication/parity acceptance. It stops only its own importer after
the normal native call returns; its partial stream can therefore be cancelled.
"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

import run_live_view_acceptance_v1 as acceptance


def review_existing(out: Path, reference: Path) -> int:
    """Review frozen measurements; never rerun CUDA or erase original failure."""
    protocol = acceptance.read(out / "protocol.json")
    assert protocol["reference"] == str(reference)
    assert acceptance.digest(out / "run_live_view_gpu_control_v1.py") == protocol["control_sha256"]
    assert not (out / "results.json").exists(), "refusing to overwrite an existing review"
    status = acceptance.read(out / "session/case/native-call-status.json")
    assert status["native_call_returned_normally"] and not status["native_running"]
    capture_finished = (out / "concurrent-recorded.png").stat().st_mtime
    assert status["native_call_started_unix_s"] <= capture_finished < status["native_call_finished_unix_s"]
    baseline = acceptance.BASELINE / "cases/mountain-rain-on-14400s"
    for field in ("h", "hUx", "hUy"):
        for timestamp in range(0, 14401, 60):
            name = f"{field}_{timestamp}.asc"
            assert acceptance.digest(out / "session/case/native/output" / name) == acceptance.digest(baseline / "native/output" / name), name
    measured = acceptance.profile(out / "concurrent-recorded")
    live = acceptance.read(reference / "rain-on-paced/results.json")["concurrent_window_profile"]
    assert measured["observed_wall_s"] >= 9
    assert [list(extent) for extent in measured["extents"]] == live["extents"]
    ratio = live["p95_delta_ms"] / measured["p95_delta_ms"]
    acceptance.write(out / "results.json", {
        "status": "frozen_measurement_review_complete", "recorded_with_cuda": measured,
        "live_with_cuda": live, "p95_live_recorded_ratio_same_cuda_load": ratio,
        "diagnostic_threshold_passed": ratio <= 1.2,
        "original_frozen_gate": acceptance.read(reference / "acceptance-failure.json"),
        "original_control_failure": acceptance.read(out / "failure.json"),
        "review_source_sha256": acceptance.digest(Path(__file__)),
        "review_correction": "Normalize in-memory tuple extents to the JSON list representation; measurements and thresholds unchanged",
        "native_call": status, "all723_native_fields_match_baseline": True,
        "capture_completed_file_mtime_unix_s": capture_finished,
        "capture_timing_boundary": "file modification timestamp; original control also enforced its observed capture span within native call before the tuple/list assertion",
        "importer_terminal_state": acceptance.read(out / "session/session-result.json")["state"],
        "not_a_complete_stream_acceptance": True, "human_gui_acceptance": "pending"})
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--review-existing", action="store_true")
    args = parser.parse_args()
    out = args.out.resolve()
    reference = args.reference.resolve()
    if args.review_existing:
        return review_existing(out, reference)
    out.mkdir(exist_ok=False)
    live = acceptance.read(reference / "rain-on-paced/results.json")
    baseline = acceptance.BASELINE / "cases/mountain-rain-on-14400s"
    title = "Cubey recorded concurrent CUDA control"
    viewer_command = acceptance.viewer_command(
        acceptance.TARGET, reference / "rain-on/independent-recording", title,
        out / "concurrent-recorded", False, False)
    publisher_command = ["rtk", "proxy", sys.executable,
                         str(acceptance.HERE / "live_synxflow_session_v1.py"),
                         "--baseline-case", str(baseline), "--out", str(out / "session"),
                         "--python", str(acceptance.PYTHON)]
    for name in ("run_live_view_gpu_control_v1.py", "run_live_view_acceptance_v1.py", "live_synxflow_session_v1.py"):
        shutil.copyfile(acceptance.HERE / name, out / name)
    acceptance.write(out / "protocol.json", {
        "schema": "cubey.fluid25d.concurrent-recorded-control.v1", "created_unix_s": time.time(),
        "reference": str(reference), "target_sha256": acceptance.digest(acceptance.TARGET),
        "publisher_sha256": acceptance.digest(acceptance.HERE / "live_synxflow_session_v1.py"),
        "control_sha256": acceptance.digest(Path(__file__)),
        "viewer_command": viewer_command, "publisher_command": publisher_command,
        "view_wall_s": 10, "minimum_profile_wall_s": 9,
        "comparison": "paced live versus paced recorded, both with the identical native solver and publisher workload",
        "diagnostic_threshold": "live/recorded p95 <=1.2; original concurrent/idle failure remains retained",
        "stop_condition": "cleanly close this window at10s; cancel only this importer after normal stock flood.run return",
        "not_a_complete_stream_acceptance": True})
    publisher = viewer = None
    capture_started = capture_finished = None
    started = time.monotonic()
    try:
        with (out / "publisher.log").open("w") as publisher_log, (out / "viewer.log").open("w") as viewer_log:
            publisher = subprocess.Popen(publisher_command, stdout=publisher_log, stderr=subprocess.STDOUT)
            while not (out / "session/stream.json").is_file():
                assert publisher.poll() is None, "control publisher failed before first snapshot"
                assert time.monotonic() - started < 120
                time.sleep(.1)
            viewer = subprocess.Popen(viewer_command, stdout=viewer_log, stderr=subprocess.STDOUT)
            view_started = time.monotonic()
            while time.monotonic() - view_started < 10:
                assert viewer.poll() is None, "control window exited early"
                if capture_started is None and time.monotonic() - view_started >= 2:
                    capture_started = time.time()
                    assert acceptance.scoped_capture(title, out / "concurrent-recorded.png")
                    capture_finished = time.time()
                time.sleep(.1)
            acceptance.close_owned_window(title)
            assert viewer.wait(timeout=15) == 0
            while True:
                status = acceptance.read(out / "session/case/native-call-status.json")
                if status.get("native_call_returned_normally") is True:
                    break
                assert publisher.poll() is None, "control native call failed"
                assert time.monotonic() - started < 180
                time.sleep(.1)
            assert capture_started is not None and capture_finished is not None
            assert status["native_call_started_unix_s"] <= capture_started <= capture_finished < status["native_call_finished_unix_s"]
            # The normal native call is already over, so cancelling this owned
            # importer cannot alter the solver's completed numerical output.
            acceptance.stop_owned(publisher)
        for field in ("h", "hUx", "hUy"):
            for timestamp in range(0, 14401, 60):
                name = f"{field}_{timestamp}.asc"
                assert acceptance.digest(out / "session/case/native/output" / name) == acceptance.digest(baseline / "native/output" / name), name
        measured = acceptance.profile(out / "concurrent-recorded")
        assert measured["observed_wall_s"] >= 9
        assert [list(extent) for extent in measured["extents"]] == live["concurrent_window_profile"]["extents"]
        terminal = acceptance.read(out / "session/session-result.json")
        stream = acceptance.read(out / "session/stream.json")
        assert terminal["state"] == stream["producer"]["state"] == "cancelled"
        assert not stream["producer"]["native_running"]
        assert terminal["frames_published"] == len(stream["frames"])
        ratio = live["concurrent_window_profile"]["p95_delta_ms"] / measured["p95_delta_ms"]
        result = {"status": "diagnostic_complete", "recorded_with_cuda": measured,
                  "live_with_cuda": live["concurrent_window_profile"], "p95_live_recorded_ratio_same_cuda_load": ratio,
                  "diagnostic_threshold_passed": ratio <= 1.2,
                  "original_frozen_gate": acceptance.read(reference / "acceptance-failure.json"),
                  "native_call": status, "all723_native_fields_match_baseline": True,
                  "importer_exit_code": publisher.returncode,
                  "importer_terminal_state": terminal["state"],
                  "retained_frame_count": terminal["frames_published"],
                  "human_gui_acceptance": "pending"}
        acceptance.write(out / "results.json", result)
        print(json.dumps(result), flush=True)
    except BaseException as error:
        acceptance.write(out / "failure.json", {"type": type(error).__name__, "error": str(error)})
        raise
    finally:
        if viewer is not None:
            acceptance.stop_owned(viewer)
        if publisher is not None:
            acceptance.stop_owned(publisher)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
