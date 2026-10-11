"""CPU-only managed-demo ownership and failure tests; no native imports."""
import json
from pathlib import Path
import tempfile
import time
import unittest

import terrain_demo_session as managed


class RequestTests(unittest.TestCase):
    def test_validation_and_stale_requests(self):
        entries = {"mountain-patch": {"available": True}, "missing": {"available": False}}
        valid = {"schema": managed.SCHEMA, "launch_id": "owned", "request_id": 1,
                 "case_id": "mountain-patch", "rain_mm_per_hour": 512, "pacing": 60}
        self.assertEqual(managed.switch_request(valid, "owned", entries, 0), valid)
        self.assertIsNone(managed.switch_request(valid, "owned", entries, 1))
        for change in ({"launch_id": "stale"}, {"request_id": True}, {"request_id": -1},
                       {"case_id": "../other"}, {"case_id": "missing"}, {"pacing": 0},
                       {"rain_mm_per_hour": float("nan")}, {"rain_mm_per_hour": 36001},
                       {"extra": 1}):
            with self.assertRaises(ValueError):
                managed.switch_request({**valid, **change}, "owned", entries, 0)

    def test_documents_are_bounded_and_non_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            managed.atomic_json(root / "value.json", {"v": 1})
            self.assertEqual(managed.read_json(root / "value.json"), {"v": 1})
            managed.atomic_json(root / "value.json", {"v": 2})
            self.assertEqual(managed.read_json(root / "value.json"), {"v": 2})
            (root / "link.json").symlink_to(root / "value.json")
            for raw in ('{"v":1,"v":2}', '{"v":NaN}', '[]', 'x' * (managed.MAX_JSON + 1)):
                (root / "bad.json").write_text(raw)
                with self.assertRaises(ValueError):
                    managed.read_json(root / "bad.json")
            with self.assertRaises(ValueError):
                managed.read_json(root / "link.json")


class FakeWorker:
    def __init__(self, command, **_):
        self.case, self.session, self.pacing = command
        self.pid = 123
        self.returncode = None
        self.session.mkdir()
        entry = ENTRIES[self.case.name]
        self.identity = self.session.name
        managed.atomic_json(self.session / "metadata.json", {"grid": {
            "width": entry["width"], "height": entry["height"], "spacing_m": entry["spacing_m"],
            "input_sha256": entry["input_sha256"], "solver_bed_sha256": entry["solver_bed_sha256"]},
            "session": {"session_id": self.identity}})
        self.state = {"frame": {"session_id": self.identity, "physical_time_s": 0,
                               "reset_generation": 1, "lifecycle": "paused"},
                      "rain_m_per_s": 512 / 3.6e6, "pacing": self.pacing, "ack": None}
        managed.atomic_json(self.session / "state.json", self.state)

    def poll(self):
        if self.returncode is None and (self.session / "command.json").exists():
            command = managed.read_json(self.session / "command.json")
            self.state["rain_m_per_s"] = command["value"]
            self.state["ack"] = {"command_id": 1, "state": "applied"}
            managed.atomic_json(self.session / "state.json", self.state)
        return self.returncode


ENTRIES = {name: {"id": name, "available": True, "case_dir": "/" + name,
    "width": 2, "height": 2, "spacing_m": 16, "input_sha256": "a" * 64,
    "solver_bed_sha256": "b" * 64, "rain_mm_per_hour": 512} for name in ("mountain", "rolling")}


class SupervisorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.workers, self.stopped = [], []
        def popen(command, **kwargs):
            self.assertTrue(all(worker.poll() is not None for worker in self.workers))
            worker = FakeWorker(command, **kwargs)
            self.workers.append(worker)
            return worker
        def stop(worker):
            self.stopped.append(worker)
            worker.returncode = 0
            return {"pid": worker.pid, "returncode": 0}
        self.manager = managed.ManagedTerrainDemo(Path(self.temporary.name), list(ENTRIES.values()),
            "mountain", lambda _: None, lambda case, session, speed: [case, session, speed], stop, popen)
        self.manager.start_initial()
        self.manager.poll()
        self.assertEqual(self.manager.state["phase"], "active")

    def tearDown(self):
        self.manager.close()
        self.temporary.cleanup()

    def request(self, identity, name="rolling", rain=256):
        managed.atomic_json(self.manager.control / "request.json", {"schema": managed.SCHEMA,
            "launch_id": self.manager.launch_id, "request_id": identity, "case_id": name,
            "rain_mm_per_hour": rain, "pacing": 30})

    def until(self, phase):
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            self.manager.poll()
            if self.manager.state["phase"] == phase:
                return
            time.sleep(.001)
        self.fail("did not reach " + phase + ": " + str(self.manager.state))

    def installed(self, success=True):
        state = self.manager.state
        managed.atomic_json(self.manager.control / "installed.json", {"schema": managed.SCHEMA,
            "launch_id": self.manager.launch_id, "request_id": state["request_id"],
            "session_id": state["session_id"], "success": success, "error": "" if success else "upload failed"})

    def test_serial_switch_acknowledgement_and_bounded_normal_storage(self):
        original = self.manager.state["session_id"]
        for identity in range(1, 5):
            self.request(identity)
            self.until("ready")
            self.assertNotEqual(self.manager.state["session_id"], original)
            self.assertAlmostEqual(self.manager.worker.state["rain_m_per_s"] * 3.6e6, 256)
            self.assertEqual(self.manager.worker.pacing, 30)
            self.assertEqual(self.manager.worker.state["frame"]["physical_time_s"], 0)
            self.installed()
            self.until("active")
            self.assertLessEqual(len(list(self.manager.sessions.iterdir())), 2)
        self.assertEqual(len(self.stopped), 4)
        self.manager.poll()  # Same mailbox request does not start another worker.
        self.assertEqual(len(self.workers), 5)

    def test_preflight_failure_keeps_current_worker_and_allows_retry(self):
        worker = self.manager.worker
        self.manager.validate_case = lambda _: (_ for _ in ()).throw(ValueError("input changed"))
        self.request(1)
        self.until("error")
        self.assertIs(self.manager.worker, worker)
        self.assertTrue(self.manager.state["worker_available"])
        self.assertIn("input changed", self.manager.state["error"])
        self.manager.validate_case = lambda _: None
        self.request(2)
        self.until("ready")

    def test_failed_install_stops_new_worker_and_can_restart_previous_dry(self):
        self.request(1)
        self.until("ready")
        self.installed(False)
        self.until("error")
        self.assertIsNone(self.manager.worker)
        self.assertFalse(self.manager.state["worker_available"])
        self.request(2, "mountain")
        self.until("ready")
        self.assertEqual(self.manager.worker.state["frame"]["physical_time_s"], 0)

    def test_close_during_load_stops_only_owned_worker(self):
        self.request(1)
        self.manager.poll()
        self.manager.close()
        self.assertEqual(self.manager.state["phase"], "closed")
        self.assertTrue(all(worker.poll() is not None for worker in self.workers))

    def test_terminal_worker_is_not_an_unexpected_failure_and_can_restart(self):
        for lifecycle in ("completed", "stopped"):
            worker = self.manager.worker
            worker.state["frame"]["lifecycle"] = lifecycle
            managed.atomic_json(worker.session / "state.json", worker.state)
            worker.returncode = 0
            self.until(lifecycle)
            self.assertEqual(self.manager.state["error"], "")
            self.request(1 if lifecycle == "completed" else 2, "mountain")
            self.until("ready")
            self.installed()
            self.until("active")

    def test_stale_confirmation_and_startup_timeout_fail_closed(self):
        self.request(1)
        self.until("ready")
        self.installed()
        document = managed.read_json(self.manager.control / "installed.json")
        managed.atomic_json(self.manager.control / "installed.json", {**document, "session_id": "old"})
        self.manager.poll()
        self.assertEqual(self.manager.state["phase"], "ready")
        self.manager.deadline = -1
        self.until("error")
        self.assertIsNone(self.manager.worker)


if __name__ == "__main__":
    unittest.main()
