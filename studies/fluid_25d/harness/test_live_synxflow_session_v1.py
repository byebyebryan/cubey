"""CPU-only synthetic tests for the live SynxFlow publisher contract."""

from __future__ import annotations

from array import array
import hashlib
import json
import math
from pathlib import Path
import signal
import tempfile
import time
import unittest
from unittest.mock import patch

import convert_synxflow_recording_v1 as converter
from live_synxflow_session_v1 import (
    PINNED_PYTHON,
    LiveSession,
    LiveSessionError,
    _now_pair,
    _safe_tree_hashes,
)
from test_convert_synxflow_recording_v1 import _make_case, _write_asc


_FRAME_VALUES = {
    0: {
        "h": [0.0, 0.0, 0.0, 0.0],
        "hUx": [0.0, 0.0, 0.0, 0.0],
        "hUy": [0.0, 0.0, 0.0, 0.0],
    },
    1: {
        "h": [0.1, 0.0, 0.2, 0.4],
        "hUx": [0.02, 0.0, 0.4, 0.6],
        "hUy": [-0.03, 0.0, 0.2, 0.1],
    },
    2: {
        "h": [0.0, 0.3, 0.2, 0.1],
        "hUx": [0.0, -0.03, 0.02, 0.0],
        "hUy": [0.0, 0.02, -0.01, 0.0],
    },
}


def _prepare_baseline(root: Path) -> Path:
    case = _make_case(root)
    input_hashes, _, _ = _safe_tree_hashes(case / "native/input")
    result_path = case / "case-result.json"
    result = json.loads(result_path.read_text(encoding="utf-8"))
    result.update({
        "schema": "synthetic.completed.native.case-result",
        "status": "healthy",
        "process_exit_code": 0,
        "process_timeout": False,
        "native_gpu_success_marker": True,
    })
    result["input_provenance"]["native_inputs_unchanged_during_run"] = True
    result["input_provenance"]["native_input_audit_before_solver"]["native_input_sha256"] = input_hashes
    result_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (case / "native-preflight.json").write_text(
        json.dumps({"native_input_sha256": input_hashes}, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (case / "native-postrun.json").write_text(
        json.dumps({"input_sha256_after_run": input_hashes, "unchanged_inputs": True}, indent=2) + "\n",
        encoding="utf-8",
    )
    return case


def _make_session(root: Path) -> LiveSession:
    baseline = _prepare_baseline(root)
    return LiveSession(
        baseline,
        root / "session",
        PINNED_PYTHON,
        verify_backend=False,
        minimum_free_bytes=0,
    )


def _write_field(session: LiveSession, field: str, timestamp: int, values: list[float]) -> Path:
    path = session.output_dir / f"{field}_{timestamp}.asc"
    _write_asc(path, values)
    return path


def _write_frame(session: LiveSession, timestamp: int, fields: tuple[str, ...] = ("h", "hUx", "hUy")) -> None:
    for field in fields:
        _write_field(session, field, timestamp, _FRAME_VALUES[timestamp][field])


def _write_call_status(session: LiveSession, *, running: bool, started: float = 10.0, finished: float = 20.0) -> None:
    status = {
        "schema": "synthetic.native-call-status",
        "native_running": running,
        "native_call_started": True,
        "native_call_started_unix_s": started,
        "native_call_started_monotonic_s": started,
        "synxflow_version": "1.0.1",
        "python": str(PINNED_PYTHON.resolve()),
    }
    if not running:
        status.update({
            "native_call_returned_unix_s": finished,
            "native_call_returned_monotonic_s": finished,
            "native_call_finished_unix_s": finished,
            "native_call_finished_monotonic_s": finished,
            "native_call_returned_normally": True,
        })
    session.case_dir.joinpath("native-call-status.json").write_text(
        json.dumps(status, sort_keys=True) + "\n", encoding="utf-8"
    )


class LiveSynxFlowSessionTests(unittest.TestCase):
    def test_chunked_triples_atomic_prefix_converter_parity_and_final_withholding(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = _make_session(root)

            # h_0 and its vector pair are complete but lack the required next
            # depth export, so no frame or stream is visible yet.
            _write_frame(session, 0)
            self.assertEqual(session.publish_ready_frames(), 0)
            self.assertFalse((session.out / "stream.json").exists())

            # The source begins h_1; this conservative barrier makes t=0
            # eligible. Only h_1 is written at this point, so t=1 is pending.
            _write_field(session, "h", 1, _FRAME_VALUES[1]["h"])
            self.assertEqual(session.publish_ready_frames(), 1)
            first = json.loads((session.out / "stream.json").read_text(encoding="utf-8"))
            self.assertEqual(first["revision"], 0)
            self.assertEqual(first["schema"], "cubey.fluid25d.stream.v1")
            self.assertEqual([frame["time_s"] for frame in first["frames"]], [0.0])
            self.assertEqual(first["producer"]["state"], "running")
            self.assertFalse(first["producer"]["native_running"])

            # Complete the t=1 triple in a deliberately mixed order. Since h_2
            # has not started, frame 1 remains outside the published prefix.
            _write_field(session, "hUx", 1, _FRAME_VALUES[1]["hUx"])
            _write_field(session, "hUy", 1, _FRAME_VALUES[1]["hUy"])
            self.assertEqual(session.publish_ready_frames(), 0)
            self.assertEqual(len(json.loads((session.out / "stream.json").read_text())["frames"]), 1)

            # Starting h_2 releases t=1. Write its vector pair afterward; t=2
            # still cannot publish until successful child exit.
            _write_field(session, "h", 2, _FRAME_VALUES[2]["h"])
            self.assertEqual(session.publish_ready_frames(), 1)
            _write_field(session, "hUy", 2, _FRAME_VALUES[2]["hUy"])
            _write_field(session, "hUx", 2, _FRAME_VALUES[2]["hUx"])
            self.assertEqual(session.publish_ready_frames(), 0)
            before_exit = json.loads((session.out / "stream.json").read_text(encoding="utf-8"))
            self.assertEqual([frame["time_s"] for frame in before_exit["frames"]], [0.0, 1.0])

            _write_call_status(session, running=False)
            session._native_call_timing = session._load_child_timing()
            session._child_exit_observed = _now_pair()
            self.assertEqual(session.publish_ready_frames(successful_exit=True), 1)
            (session.case_dir / "native.stdout").write_text(
                "Simulation successfully finished!\n", encoding="utf-8"
            )
            session._child_started_unix_s, session._child_started_monotonic_s = _now_pair()
            session._finalize_success(0, "synthetic child success")

            stream = json.loads((session.out / "stream.json").read_text(encoding="utf-8"))
            self.assertEqual(stream["producer"]["state"], "completed")
            self.assertFalse(stream["producer"]["native_running"])
            self.assertEqual([frame["time_s"] for frame in stream["frames"]], [0.0, 1.0, 2.0])
            self.assertEqual(stream["revision"], 3)
            self.assertEqual(stream["session_id"], session.session_id)
            self.assertEqual(len(stream["session_id"]), 32)
            self.assertIn("pre_solver_audit", stream["provenance"]["source_sha256"])
            self.assertIn("case_result", stream["provenance"]["source_sha256"])
            self.assertEqual(stream["provenance"]["raw_asc_sha256"], session.raw_hashes)

            # The unchanged completed-case converter accepts the same fresh
            # child output and makes byte-identical planar payloads/statistics.
            recording = converter.convert_case(session.case_dir, root / "converted")
            converted = json.loads(recording.read_text(encoding="utf-8"))
            self.assertEqual(len(converted["frames"]), len(stream["frames"]))
            for live, saved in zip(stream["frames"], converted["frames"], strict=True):
                self.assertEqual({key: live[key] for key in saved}, saved)
                self.assertEqual(
                    (session.out / live["path"]).read_bytes(),
                    (recording.parent / saved["path"]).read_bytes(),
                )
                self.assertTrue(math.isfinite(live["published_unix_s"]))
                self.assertGreater(live["published_unix_s"], 0.0)

            sidecar = json.loads((session.out / "session-result.json").read_text(encoding="utf-8"))
            self.assertEqual(sidecar["state"], "completed")
            self.assertIn("native_call_started_unix_s", sidecar)
            self.assertIn("native_call_finished_unix_s", sidecar)

    def test_publisher_refreshes_solver_state_and_uses_successor_time_for_queue_lag(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = _make_session(root)
            for timestamp in (0, 1, 2):
                _write_frame(session, timestamp)

            now_unix, now_mono = _now_pair()
            session._observed_h[1] = (now_unix - 4.0, now_mono - 4.0)
            session._observed_h[2] = (now_unix - 3.0, now_mono - 3.0)
            native_states = iter((True, True, True, False, False, False, False, False))
            published_states: list[bool] = []

            def read_state() -> bool:
                try:
                    session.native_running = next(native_states)
                except StopIteration:
                    session.native_running = False
                return session.native_running

            original_publish = session._publish_manifest

            def record_publish(*, force: bool = False, message: str = "") -> None:
                published_states.append(session.native_running)
                original_publish(force=force, message=message)

            session._read_native_running = read_state  # type: ignore[method-assign]
            session._publish_manifest = record_publish  # type: ignore[method-assign]
            self.assertEqual(session.publish_ready_frames(), 2)
            self.assertEqual(published_states, [True, False])
            self.assertGreaterEqual(session.frame_timings[0]["queue_lag_after_barrier_s"], 4.0)
            self.assertGreaterEqual(session.frame_timings[1]["queue_lag_after_barrier_s"], 3.0)
            manifest = json.loads((session.out / "stream.json").read_text(encoding="utf-8"))
            self.assertFalse(manifest["producer"]["native_running"])

    def test_running_message_tracks_native_call_vs_import_and_audit_catchup(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = _make_session(root)
            _write_frame(session, 0)
            _write_field(session, "h", 1, _FRAME_VALUES[1]["h"])
            self.assertEqual(session.publish_ready_frames(), 1)
            initializing = json.loads((session.out / "stream.json").read_text(encoding="utf-8"))
            self.assertFalse(initializing["producer"]["native_running"])
            self.assertIn("importing/initializing", initializing["producer"]["message"])

            _write_call_status(session, running=True)
            self.assertTrue(session._read_native_running())
            session._publish_manifest(force=True)
            computing = json.loads((session.out / "stream.json").read_text(encoding="utf-8"))
            self.assertTrue(computing["producer"]["native_running"])
            self.assertIn("is computing", computing["producer"]["message"])

            _write_call_status(session, running=False)
            self.assertFalse(session._read_native_running())
            session._publish_manifest(force=True)
            catching_up = json.loads((session.out / "stream.json").read_text(encoding="utf-8"))
            self.assertFalse(catching_up["producer"]["native_running"])
            self.assertIn("draining exports and auditing", catching_up["producer"]["message"])

    def test_incomplete_current_fields_and_truncated_exports_never_publish(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = _make_session(root)
            _write_field(session, "h", 0, _FRAME_VALUES[0]["h"])
            _write_field(session, "hUx", 0, _FRAME_VALUES[0]["hUx"])
            _write_field(session, "h", 1, _FRAME_VALUES[1]["h"])
            with self.assertRaisesRegex(LiveSessionError, "without hUy"):
                session.publish_ready_frames()
            self.assertFalse((session.out / "stream.json").exists())

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = _make_session(root)
            _write_frame(session, 0)
            _write_field(session, "h", 1, _FRAME_VALUES[1]["h"])
            self.assertEqual(session.publish_ready_frames(), 1)
            _write_field(session, "h", 2, _FRAME_VALUES[2]["h"])
            _write_field(session, "hUx", 1, _FRAME_VALUES[1]["hUx"])
            (session.output_dir / "hUy_1.asc").write_text(
                "ncols 2\nnrows 2\nxllcorner 0\nyllcorner 0\ncellsize 10\nNODATA_value -9999\n0\n",
                encoding="ascii",
            )
            with self.assertRaisesRegex(converter.RecordingConversionError, "cells"):
                session.publish_ready_frames()
            manifest = json.loads((session.out / "stream.json").read_text(encoding="utf-8"))
            self.assertEqual([frame["time_s"] for frame in manifest["frames"]], [0.0])

    def test_malformed_order_symlinks_and_extra_timestamps_reject(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = _make_session(root)
            _write_frame(session, 0)
            (session.output_dir / "h_01.asc").write_text("invalid\n", encoding="ascii")
            with self.assertRaisesRegex(LiveSessionError, "malformed timestamp"):
                session.publish_ready_frames()
            self.assertFalse((session.out / "stream.json").exists())

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = _make_session(root)
            _write_field(session, "h", 0, _FRAME_VALUES[0]["h"])
            _write_field(session, "h", 2, _FRAME_VALUES[2]["h"])
            with self.assertRaisesRegex(LiveSessionError, "skipped 1s"):
                session.publish_ready_frames()

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = _make_session(root)
            outside = root / "outside.asc"
            _write_asc(outside, _FRAME_VALUES[0]["h"])
            (session.output_dir / "h_0.asc").symlink_to(outside)
            with self.assertRaisesRegex(LiveSessionError, "symlink"):
                session.publish_ready_frames()

    def test_failed_cancelled_input_drift_and_zero_exit_missing_marker(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = _make_session(root)
            _write_call_status(session, running=True)
            z_path = session.case_dir / "native/input/field/z.dat"
            z_path.write_text(z_path.read_text(encoding="ascii") + "\n", encoding="ascii")
            session.finish_failure("cancelled", "synthetic cancellation", -15)
            sidecar = json.loads((session.out / "session-result.json").read_text(encoding="utf-8"))
            result = json.loads((session.case_dir / "case-result.json").read_text(encoding="utf-8"))
            status = json.loads((session.case_dir / "native-call-status.json").read_text(encoding="utf-8"))
            self.assertEqual(sidecar["state"], "cancelled")
            self.assertIsNone(sidecar["stream"])
            self.assertFalse(status["native_running"])
            self.assertFalse(result["input_provenance"]["native_inputs_unchanged_during_run"])
            self.assertFalse((session.out / "stream.json").exists())

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = _make_session(root)
            (session.case_dir / "native.stdout").write_text("child returned without marker\n", encoding="utf-8")

            class FakeProcess:
                returncode = 0

            self.assertEqual(session.finish_child(FakeProcess()), 1)  # type: ignore[arg-type]
            sidecar = json.loads((session.out / "session-result.json").read_text(encoding="utf-8"))
            self.assertEqual(sidecar["state"], "failed")
            self.assertIn("success marker", sidecar["message"])

    def test_no_overwrite_and_output_budget(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = _prepare_baseline(root)
            out = root / "existing"
            out.mkdir()
            marker = out / "keep.txt"
            marker.write_text("preserve", encoding="utf-8")
            with self.assertRaisesRegex(LiveSessionError, "refusing to overwrite"):
                LiveSession(baseline, out, PINNED_PYTHON, verify_backend=False, minimum_free_bytes=0)
            self.assertEqual(marker.read_text(encoding="utf-8"), "preserve")

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = _prepare_baseline(root)
            with self.assertRaisesRegex(LiveSessionError, "insufficient free space"):
                LiveSession(
                    baseline,
                    root / "session",
                    PINNED_PYTHON,
                    verify_backend=False,
                    minimum_free_bytes=10**20,
                    disk_usage=lambda _path: type("Usage", (), {"free": 0})(),
                )
            self.assertFalse((root / "session").exists())

    def test_failure_retains_published_validated_prefix(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = _make_session(root)
            _write_frame(session, 0)
            _write_field(session, "h", 1, _FRAME_VALUES[1]["h"])
            self.assertEqual(session.publish_ready_frames(), 1)
            before = json.loads((session.out / "stream.json").read_text(encoding="utf-8"))
            descriptor = before["frames"][0]
            payload = (session.out / descriptor["path"]).read_bytes()

            _write_call_status(session, running=True)
            session.finish_failure("failed", "synthetic child error", 7)

            terminal = json.loads((session.out / "stream.json").read_text(encoding="utf-8"))
            sidecar = json.loads((session.out / "session-result.json").read_text(encoding="utf-8"))
            self.assertEqual(terminal["producer"]["state"], "failed")
            self.assertFalse(terminal["producer"]["native_running"])
            self.assertEqual(terminal["session_id"], before["session_id"])
            self.assertGreater(terminal["revision"], before["revision"])
            self.assertEqual(terminal["frames"], before["frames"])
            self.assertEqual(terminal["provenance"]["raw_asc_sha256"], before["provenance"]["raw_asc_sha256"])
            self.assertEqual((session.out / descriptor["path"]).read_bytes(), payload)
            self.assertEqual(sidecar["state"], "failed")

    def test_cancellation_stops_and_waits_only_for_owned_child(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = _make_session(root)
            _write_frame(session, 0)
            _write_field(session, "h", 1, _FRAME_VALUES[1]["h"])
            self.assertEqual(session.publish_ready_frames(), 1)
            prefix = json.loads((session.out / "stream.json").read_text(encoding="utf-8"))
            prefix_payload = (session.out / prefix["frames"][0]["path"]).read_bytes()

            class FakeChild:
                def __init__(self) -> None:
                    self.returncode: int | None = None
                    self.terminate_calls = 0
                    self.wait_calls = 0
                    self.kill_calls = 0

                def poll(self) -> int | None:
                    return self.returncode

                def terminate(self) -> None:
                    self.terminate_calls += 1
                    self.returncode = -15

                def wait(self, timeout: float | None = None) -> int:
                    self.wait_calls += 1
                    return self.returncode or 0

                def kill(self) -> None:
                    self.kill_calls += 1
                    self.returncode = -9

            child = FakeChild()
            finish_failure = session.finish_failure

            def finish_after_repeated_sigterm(*args, **kwargs):
                signal.raise_signal(signal.SIGTERM)
                return finish_failure(*args, **kwargs)

            with patch("live_synxflow_session_v1.subprocess.Popen", return_value=child) as popen:
                with patch.object(session, "publish_ready_frames", side_effect=KeyboardInterrupt) as publish:
                    with patch.object(session, "finish_failure", side_effect=finish_after_repeated_sigterm) as finish:
                        self.assertEqual(session.run(poll_interval_s=0.01), 130)
                    publish.assert_called_once_with()
                    finish.assert_called_once()

            popen.assert_called_once()
            self.assertEqual(child.terminate_calls, 1)
            self.assertEqual(child.wait_calls, 1)
            self.assertEqual(child.kill_calls, 0)
            result = json.loads((session.out / "session-result.json").read_text(encoding="utf-8"))
            self.assertEqual(result["state"], "cancelled")
            self.assertEqual(result["process_exit_code"], -15)
            terminal = json.loads((session.out / "stream.json").read_text(encoding="utf-8"))
            self.assertEqual(terminal["producer"]["state"], "cancelled")
            self.assertFalse(terminal["producer"]["native_running"])
            self.assertEqual(terminal["frames"], prefix["frames"])
            self.assertEqual((session.out / prefix["frames"][0]["path"]).read_bytes(), prefix_payload)


if __name__ == "__main__":
    unittest.main()
