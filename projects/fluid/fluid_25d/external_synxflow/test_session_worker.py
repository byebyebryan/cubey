# SPDX-License-Identifier: GPL-3.0-only
"""CPU/fake-boundary tests only; no CUDA import or solver execution."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from session_worker import MAGIC, SCHEMA, SessionWorker, atomic_json, digest, input_hashes, raster_fields, strict_json


def make_case(root):
    case = root / "case"
    native = case / "native/input"
    (native / "mesh").mkdir(parents=True)
    (native / "field").mkdir()
    (native / "mesh/DEM.txt").write_text("ncols 3\nnrows 2\nxllcorner 0\nyllcorner 0\ncellsize 2\nNODATA_value -9999\n1 2 3\n10 20 30\n")
    (native / "times_setup.dat").write_text("0 100 10 1000\n")
    (native / "field/precipitation_source_all.dat").write_text("1\n0 0.0001\n100 0.0001\n")
    (native / "field/precipitation_mask.dat").write_text("$Element Number\n6\n$Element_id  Value\n" + "".join(f"{index} 0\n" for index in range(6)))
    (native / "field/z.dat").write_text("$Element Number\n6\n$Element_id  Value\n" +
                                       "".join(f"{index} {value}\n" for index, value in enumerate((10,20,30,1,2,3))))
    return case


def fake_fields(snapshot, *_):
    snapshot()
    return struct.pack("<18f", *([1.0] * 6 + [2.0] * 6 + [-3.0] * 6)), struct.pack("<6f", 1, 2, 3, 10, 20, 30)


def command(worker, identity, kind, value=None, generation=None):
    atomic_json(worker.out / "command.json", {"schema": SCHEMA, "type": "command", "command_id": identity,
                "session_id": worker.session_id, "reset_generation": generation or worker.generation,
                "domain": "solver", "kind": kind, "value": value})


def wait_state(worker, predicate):
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        if (worker.out / "state.json").exists():
            state = json.loads((worker.out / "state.json").read_bytes())
            if predicate(state):
                return state
        time.sleep(0.002)
    raise AssertionError("fake boundary did not publish expected state")


class BoundaryThread:
    def __init__(self, worker, initial, physical_time):
        self.result = None
        self.error = None
        def run():
            try:
                self.result = worker.boundary(initial, physical_time, 0.1, False, lambda: None)
            except Exception as error:
                self.error = error
        self.thread = threading.Thread(target=run, daemon=True)
        self.thread.start()

    def join(self):
        self.thread.join(timeout=2)
        if self.error:
            raise self.error
        if self.thread.is_alive():
            raise AssertionError("boundary thread did not terminate")
        return self.result


class WorkerTests(unittest.TestCase):
    @patch("session_worker.raster_fields", side_effect=fake_fields)
    def test_pause_rain_step_reset_resume_stop(self, _mock):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root)
            worker = SessionWorker(case, root / "service", paused=True)
            original = input_hashes(case / "native/input")
            thread = BoundaryThread(worker, True, 0)
            wait_state(worker, lambda state: state["frame"]["lifecycle"] == "paused")
            command(worker, 1, "set-rain", 0.001)
            rain = wait_state(worker, lambda state: state["ack"] and state["ack"]["command_id"] == 1)
            self.assertEqual(rain["ack"]["application_time_s"], 0)
            self.assertAlmostEqual(rain["rain_m_per_s"], 0.001)
            command(worker, 2, "step")
            self.assertEqual(thread.join()["action"], "continue")
            thread = BoundaryThread(worker, False, 0.005)
            step = wait_state(worker, lambda state: state["ack"]["command_id"] == 2 and state["ack"]["state"] == "applied")
            self.assertEqual(step["ack"]["application_time_s"], 0.005)
            self.assertEqual(step["frame"]["lifecycle"], "paused")
            command(worker, 3, "reset")
            self.assertEqual(thread.join()["action"], "reset")
            thread = BoundaryThread(worker, True, 0)
            reset = wait_state(worker, lambda state: state["frame"]["reset_generation"] == 2)
            self.assertGreater(reset["frame"]["sequence"], step["frame"]["sequence"])
            self.assertEqual(reset["ack"]["state"], "applied")
            self.assertEqual(reset["frame"]["lifecycle"], "ready")
            self.assertEqual(reset["rain_m_per_s"], 0.0001)
            command(worker, 4, "resume")
            self.assertEqual(thread.join()["action"], "continue")
            command(worker, 5, "stop")
            self.assertEqual(worker.boundary(False, 0.005, 0.1, False, lambda: None)["action"], "stop")
            worker.finish("stopped")
            completion = json.loads((worker.out / "completion.json").read_bytes())
            self.assertTrue(completion["inputs_unchanged"] and completion["reference_inputs_unchanged"])
            self.assertEqual(input_hashes(case / "native/input"), original)
            self.assertEqual(len(list(worker.out.glob("slot-*.bin"))), 3)
            state = json.loads((worker.out / "state.json").read_bytes())
            raw = (worker.out / f"slot-{state['slot']}.bin").read_bytes()
            self.assertEqual(struct.unpack("<8sQQd", raw[:32]), (MAGIC, 2, state["frame"]["sequence"], 0.005))
            self.assertEqual(digest(raw), state["payload_sha256"])

    @patch("session_worker.raster_fields", side_effect=fake_fields)
    def test_rejection_duplicate_and_bounded_storage(self, _mock):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            worker = SessionWorker(make_case(root), root / "service")
            worker.boundary(True, 0, 0.1, False, lambda: None)
            command(worker, 1, "seek", 10)
            self.assertTrue(worker.read_command()["rejected"])
            self.assertEqual(worker.last_ack["state"], "rejected")
            self.assertIsNone(worker.read_command())
            command(worker, 2, "set-rain", 0.001, generation=3)
            self.assertTrue(worker.read_command()["rejected"])
            command(worker, 3, "set-time-scale", -1)
            self.assertTrue(worker.read_command()["rejected"])
            for index in range(4, 104):
                command(worker, index, "set-rain", 0.001)
                cmd = worker.read_command()
                worker.acknowledge(cmd)
                worker.publish(lambda: None)
            self.assertEqual(len(worker.history), 64)
            self.assertEqual(len(list(worker.out.glob("slot-*.bin"))), 3)
            self.assertFalse(list(worker.out.glob("*.tmp")))
            self.assertLess((worker.out / "controls.json").stat().st_size, 64 * 1024)
            self.assertFalse(list((worker.native / "output").iterdir()))
            worker.time_s = 100 # An error must label the last synchronized fields, not this clock.
            worker.finish("failed", "injected native error " + "\u00e9" * 512)
            state = json.loads((worker.out / "state.json").read_bytes())
            self.assertEqual(state["frame"]["physical_time_s"], 0)
            self.assertEqual(state["frame"]["lifecycle"], "failed")
            self.assertLessEqual(len(state["frame"]["failure_message"].encode("utf-8")), 512)

    def test_canonical_wire_commands_are_not_capability_keys(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            worker = SessionWorker(make_case(root), root / "service", paused=True)
            for identity, kind, value in ((1, "set-rain", 0.001),
                                          (2, "set-time-scale", 60)):
                command(worker, identity, kind, value)
                parsed = worker.read_command()
                self.assertEqual(parsed["kind"], kind)
                self.assertFalse(parsed.get("rejected", False))
            for identity, kind in ((3, "set_rain"), (4, "set_time_scale")):
                command(worker, identity, kind, 1)
                self.assertTrue(worker.read_command()["rejected"])
                self.assertEqual(worker.last_ack["state"], "rejected")

    def test_malformed_json_and_fresh_output(self):
        for raw in (b'{"x":1,"x":2}', b'{"x":NaN}', b'[]', b' ' * 65537):
            with self.assertRaises(ValueError):
                strict_json(raw)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = make_case(root)
            existing = root / "existing"
            existing.mkdir()
            with self.assertRaises(ValueError):
                SessionWorker(case, existing)

    @patch("session_worker.raster_fields", side_effect=fake_fields)
    def test_stock_zero_duration_pass_and_unexpected_stall(self, _mock):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            worker = SessionWorker(make_case(root), root / "service", speed=300)
            worker.boundary(True, 0, 0.005, True, lambda: None)
            worker.boundary(False, 0.01, 0, True, lambda: None)
            worker.boundary(False, 0.01, 0.1, False, lambda: None)
            with self.assertRaisesRegex(ValueError, "stalled or regressed"):
                worker.boundary(False, 0.01, 0.1, False, lambda: None)
            with self.assertRaisesRegex(ValueError, "invalid native clock"):
                worker.boundary(False, 0.02, 0, False, lambda: None)

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "numerical byte-map check uses optional study NumPy")
    def test_asymmetric_native_rows_and_momentum_sign(self):
        h = struct.pack("<6f", 10, 20, 30, 1, 2, 3)
        hu = struct.pack("<12f", 100, -1000, 200, -2000, 300, -3000, 4, 40, 5, 50, 6, 60)
        z = struct.pack("<6f", 40, 50, 60, 7, 8, 9)
        planes, bed = raster_fields(lambda: (h, hu, z), 3, 2)
        self.assertEqual(struct.unpack("<18f", planes), (1,2,3,10,20,30,4,5,6,100,200,300,-40,-50,-60,1000,2000,3000))
        self.assertEqual(struct.unpack("<6f", bed), (7,8,9,40,50,60))


if __name__ == "__main__":
    unittest.main()
