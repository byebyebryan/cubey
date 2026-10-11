"""MIT managed-demo supervisor. No native imports, CUDA API, or numerical code.

The separate launch-control mailbox requests a *new* immutable worker session.
Only this supervisor launches/stops its workers. The viewer confirms installation
before retired session files are collected; failed switches retain a labelled
held view, not a claim that the previous solver is still running.
"""
from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import json
import math
from pathlib import Path
import shutil
import subprocess
import time
import uuid

SCHEMA = "cubey.fluid25d.terrain-demo.v1"
MAX_JSON = 64 * 1024
PHASES = ("active", "validating", "stopping", "starting", "ready", "completed", "stopped", "error", "closed")


def read_json(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_JSON:
        raise ValueError("unsafe/oversized demo document: " + str(path))
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate demo JSON key")
            result[key] = value
        return result
    def constant(value):
        raise ValueError("nonfinite JSON: " + value)
    value = json.loads(path.read_bytes(), object_pairs_hook=pairs, parse_constant=constant)
    if not isinstance(value, dict):
        raise ValueError("demo document must be an object")
    return value


def atomic_json(path, value):
    path = Path(path)
    raw = json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()
    if len(raw) > MAX_JSON or path.is_symlink() or path.with_suffix(".tmp").is_symlink():
        raise ValueError("unsafe/oversized demo publication")
    temporary = path.with_suffix(".tmp")
    with temporary.open("xb") as file:
        file.write(raw)
    temporary.replace(path)


def number(value, lo, hi):
    if isinstance(value, bool) or not isinstance(value, (float, int)):
        raise ValueError("demo setting must be numeric")
    value = float(value)
    if not math.isfinite(value) or not lo <= value <= hi:
        raise ValueError("demo setting outside supported range")
    return value


def switch_request(document, launch_id, catalogue, previous_id):
    expected = {"schema", "launch_id", "request_id", "case_id", "rain_mm_per_hour", "pacing"}
    if set(document) != expected or document["schema"] != SCHEMA or document["launch_id"] != launch_id:
        raise ValueError("invalid demo switch envelope")
    request_id = document["request_id"]
    if type(request_id) is not int or not 0 < request_id < 2**64:
        raise ValueError("invalid demo request ID")
    if request_id <= previous_id:
        return None  # Duplicate/stale requests never restart a worker.
    case_id = document["case_id"]
    if not isinstance(case_id, str) or case_id not in catalogue or not catalogue[case_id]["available"]:
        raise ValueError("selected prepared case is unavailable")
    return {**document, "rain_mm_per_hour": number(document["rain_mm_per_hour"], 0, 36000),
            "pacing": number(document["pacing"], .125, 300)}


class ManagedTerrainDemo:
    """One switch in flight, one owned CUDA worker; bounded session storage."""
    def __init__(self, out, entries, initial_case, validate_case, worker_command, stop_owned,
                 popen=subprocess.Popen, timeout=120):
        self.out = Path(out).resolve(strict=True)
        self.control = self.out / "control"
        self.control.mkdir()
        self.sessions = self.out / "sessions"
        self.sessions.mkdir()
        self.launch_id = uuid.uuid4().hex
        self.entries = {entry["id"]: entry for entry in entries}
        self.validate_case = validate_case
        self.worker_command, self.stop_owned, self.popen = worker_command, stop_owned, popen
        self.timeout = timeout
        self.executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="terrain-preflight")
        self.preflight = None
        self.worker = None
        self.log = None
        self.worker_session = None
        self.retired = []
        self.cleanup = []
        self.request = None
        self.last_request_id = 0
        self.rain_command_sent = False
        self.deadline = 0
        self.ready_identity = None
        self.last_bad_request = None
        self.state = {"schema": SCHEMA, "launch_id": self.launch_id, "request_id": 0,
                      "phase": "starting", "case_id": initial_case, "session_name": "",
                      "session_id": "", "error": "", "worker_available": False,
                      "published_unix_s": time.time()}
        atomic_json(self.control / "catalog.json", {"schema": SCHEMA, "launch_id": self.launch_id,
                    "initial_case": initial_case, "cases": entries})
        self._publish()

    def _publish(self, phase=None, error=""):
        if phase is not None:
            self.state.update(phase=phase, error=str(error)[:512])
        self.state["published_unix_s"] = time.time()
        self.state["worker_available"] = self.worker is not None and self.worker.poll() is None
        atomic_json(self.control / "status.json", self.state)

    def _stop(self):
        if self.worker is not None:
            self.cleanup.append(self.stop_owned(self.worker))
            self.worker = None
        if self.log is not None:
            self.log.close()
            self.log = None
        if self.worker_session is not None:
            self.retired.append(self.worker_session)
            self.worker_session = None

    def _start(self, request_id, case_id, pacing):
        self._stop()
        name = f"generation-{request_id:020d}"
        self.worker_session = self.sessions / name
        self.log = (self.out / "worker.log").open("ab")
        command = self.worker_command(Path(self.entries[case_id]["case_dir"]), self.worker_session, pacing)
        self.worker = self.popen(command, stdout=self.log, stderr=subprocess.STDOUT, start_new_session=True)
        self.state.update(request_id=request_id, case_id=case_id, session_name=name, session_id="")
        self.deadline = time.monotonic() + self.timeout
        self.rain_command_sent = False
        self.ready_identity = None
        self._publish("starting")

    def start_initial(self):
        entry = self.entries[self.state["case_id"]]
        self.validate_case(Path(entry["case_dir"]))
        self._start(0, entry["id"], 60)

    def _check_ready(self):
        session = self.worker_session
        if not (session / "state.json").exists():
            return False
        metadata, state = read_json(session / "metadata.json"), read_json(session / "state.json")
        entry = self.entries[self.state["case_id"]]
        grid = metadata["grid"]
        frame = state["frame"]
        if (grid["width"] != entry["width"] or grid["height"] != entry["height"] or
                grid["spacing_m"] != entry["spacing_m"] or
                grid["solver_bed_sha256"] != entry["solver_bed_sha256"] or
                grid["input_sha256"] != entry["input_sha256"] or
                frame["session_id"] != metadata["session"]["session_id"] or
                frame["physical_time_s"] != 0 or frame["reset_generation"] != 1 or
                frame["lifecycle"] not in ("ready", "paused")):
            raise ValueError("new worker does not match the selected dry/paused case")
        rain = self.request["rain_mm_per_hour"] if self.request else entry["rain_mm_per_hour"]
        if self.request and not self.rain_command_sent:
            # The new session is not yet advertised to a viewer. This one startup
            # command is acknowledged before sole control passes to that viewer.
            atomic_json(session / "command.json", {"schema": "cubey.fluid25d.backend-session.v1",
                "type": "command", "command_id": 1, "session_id": frame["session_id"],
                "reset_generation": 1, "domain": "solver", "kind": "set-rain", "value": rain / 3.6e6})
            self.rain_command_sent = True
            return False
        if self.request:
            ack = state.get("ack")
            if not ack or ack["command_id"] != 1 or ack["state"] == "accepted":
                return False
            if ack["state"] != "applied":
                raise ValueError("new-worker rainfall setting rejected")
        if not math.isclose(state["rain_m_per_s"] * 3.6e6, rain, rel_tol=1e-6, abs_tol=1e-5):
            raise ValueError("new worker rainfall acknowledgement mismatch")
        pacing = self.request["pacing"] if self.request else 60
        if not math.isclose(state["pacing"], pacing, rel_tol=1e-6):
            raise ValueError("new worker pacing mismatch")
        self.state["session_id"] = frame["session_id"]
        self.ready_identity = metadata
        self.deadline = time.monotonic() + self.timeout
        self._publish("ready" if self.request else "active")
        return True

    def _installed(self):
        path = self.control / "installed.json"
        if not path.exists():
            return
        document = read_json(path)
        if set(document) != {"schema", "launch_id", "request_id", "session_id", "success", "error"}:
            raise ValueError("invalid demo installation envelope")
        if (document["schema"] != SCHEMA or document["launch_id"] != self.launch_id or
                document["request_id"] != self.state["request_id"] or
                document["session_id"] != self.state["session_id"]):
            return  # A previous scene's confirmation is never accepted.
        if type(document["success"]) is not bool or not isinstance(document["error"], str):
            raise ValueError("invalid installation result")
        if not document["success"]:
            self._stop()
            self._publish("error", document["error"])
        else:
            self._publish("active")
            # The renderer has drained old jobs and GPU submissions. Keep one
            # prior session for failure inspection; no unbounded field archive.
            while len(self.retired) > 1:
                leaf = self.retired.pop(0)
                if leaf.parent != self.sessions or leaf.is_symlink() or not leaf.name.startswith("generation-"):
                    raise ValueError("unsafe retired session target")
                if leaf.exists():
                    shutil.rmtree(leaf)
        self.request = None

    def poll(self):
        phase = self.state["phase"]
        try:
            if phase == "validating" and self.preflight.done():
                self.preflight.result()
                self.preflight = None
                self._publish("stopping")
                self._start(self.request["request_id"], self.request["case_id"], self.request["pacing"])
            elif phase == "starting":
                if self.worker.poll() is not None:
                    raise RuntimeError("new worker exited before readiness; see worker.log")
                if not self._check_ready() and time.monotonic() > self.deadline:
                    raise TimeoutError("new worker startup timed out")
            elif phase == "ready":
                self._installed()
                if self.state["phase"] == "ready" and time.monotonic() > self.deadline:
                    raise TimeoutError("renderer installation timed out")
            elif phase == "active" and self.worker is not None and self.worker.poll() is not None:
                native = read_json(self.worker_session / "state.json")
                if native["frame"]["lifecycle"] in ("completed", "stopped"):
                    self._publish(native["frame"]["lifecycle"])
                else:
                    raise RuntimeError("owned worker exited; see worker.log")
            if self.state["phase"] in ("active", "completed", "stopped", "error"):
                path = self.control / "request.json"
                if path.exists():
                    if path.is_symlink() or path.stat().st_size > MAX_JSON:
                        raise ValueError("unsafe/oversized switch request")
                    raw = path.read_bytes()
                    if raw != self.last_bad_request:
                        try:
                            request = switch_request(read_json(path), self.launch_id, self.entries, self.last_request_id)
                        except Exception:
                            self.last_bad_request = raw
                            raise
                        if request:
                            self.last_request_id = request["request_id"]
                            self.request = request
                            # Preflight failure keeps the old worker alive. A new
                            # request may retry; no silent fallback or case edits.
                            self.state["request_id"] = request["request_id"]
                            self._publish("validating")
                            self.preflight = self.executor.submit(self.validate_case,
                                Path(self.entries[request["case_id"]]["case_dir"]))
        except Exception as error:
            if self.state["phase"] not in ("active", "validating", "error"):
                self._stop()
            self.request = None
            self._publish("error", error)
        if time.time() - self.state["published_unix_s"] >= 1:
            self._publish()

    def close(self):
        self.executor.shutdown(wait=True, cancel_futures=True)
        self._stop()
        self._publish("closed")
