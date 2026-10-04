#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Explicit separate-process SynxFlow service; never imported by Cubey.

Requires the optional hooked extension and its pinned Python/NumPy environment.
Input files are copied byte-exact into a fresh leaf; a three-slot snapshot ring,
one command, and the most recent 64 control records bound session storage.
"""
from __future__ import annotations

import argparse
from collections import deque
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import shutil
import signal
import struct
import time
import uuid

SCHEMA = "cubey.fluid25d.backend-session.v1"
STATE_SCHEMA = "cubey.fluid25d.external-state.v1"
MAX_JSON = 64 * 1024
MAX_CELLS = 4_194_304
CAPABILITIES = ("pause", "resume", "reset", "stop", "step", "seek", "set_rain", "set_time_scale")
WIRE_COMMANDS = {name.replace("_", "-") for name in CAPABILITIES}
MAGIC = b"CBWTRV1\0"


def digest(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def input_hashes(directory: Path) -> dict[str, str]:
    result = {}
    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            raise ValueError("input symlinks are not supported")
        if path.is_file():
            result[str(path.relative_to(directory))] = digest(path.read_bytes())
    return result


def atomic_bytes(path: Path, raw: bytes) -> None:
    temporary = path.with_name(path.name + ".tmp")
    if temporary.is_symlink() or path.is_symlink():
        raise ValueError("unsafe publication path")
    temporary.write_bytes(raw)
    temporary.replace(path)


def atomic_json(path: Path, value: dict) -> None:
    raw = json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()
    if len(raw) > MAX_JSON:
        raise ValueError("JSON publication exceeds limit")
    atomic_bytes(path, raw)


def strict_json(raw: bytes) -> dict:
    if len(raw) > MAX_JSON:
        raise ValueError("command exceeds JSON limit")
    def object_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate command key")
            result[key] = value
        return result
    def bad_constant(value):
        raise ValueError("nonfinite JSON constant: " + value)
    value = json.loads(raw, object_pairs_hook=object_pairs, parse_constant=bad_constant)
    if not isinstance(value, dict):
        raise ValueError("command must be an object")
    return value


def finite(value, minimum: float, maximum: float) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError("numeric command value required")
    result = float(value)
    if not math.isfinite(result) or not minimum <= result <= maximum:
        raise ValueError("numeric command value outside supported range")
    return result


def raster_fields(snapshot, width: int, height: int):
    # Native Cartesian IDs enumerate bottom-up rows; ASC/Cubey preserves top-down
    # raster row order. Native Y points opposite Cubey's increasing row/world Z.
    import numpy as np
    h_raw, hu_raw, z_raw = snapshot()
    count = width * height
    if (len(h_raw), len(hu_raw), len(z_raw)) != (count * 4, count * 8, count * 4):
        raise ValueError("snapshot is not a dense float32/Vector2 Cartesian grid")
    h = np.frombuffer(h_raw, dtype="<f4").reshape(height, width)[::-1].copy()
    hu = np.frombuffer(hu_raw, dtype="<f4").reshape(height, width, 2)[::-1]
    z = np.frombuffer(z_raw, dtype="<f4").reshape(height, width)[::-1].copy()
    qx = hu[:, :, 0].copy()
    qz = (-hu[:, :, 1]).copy()
    if not all(np.isfinite(field).all() for field in (h, qx, qz, z)) or (h < 0).any():
        raise ValueError("nonfinite native snapshot or negative native depth")
    return h.tobytes() + qx.tobytes() + qz.tobytes(), z.tobytes()


class SessionWorker:
    def __init__(self, case: Path, out: Path, speed: float = 60.0,
                 publish_interval: float = 0.1, paused: bool = False):
        self.case = case.resolve(strict=True)
        self.out = out.absolute()
        if self.out.exists() or self.out.is_symlink() or not self.out.parent.is_dir():
            raise ValueError("service output must be a fresh leaf below an existing directory")
        self.speed = finite(speed, 0.125, 300.0)
        self.publish_interval = finite(publish_interval, 0.02, 1.0)
        self.original_hashes = input_hashes(self.case / "native/input")
        header = (self.case / "native/input/mesh/DEM.txt").read_text().splitlines()[:6]
        geometry = {line.split()[0].lower(): float(line.split()[1]) for line in header}
        self.width, self.height = int(geometry["ncols"]), int(geometry["nrows"])
        self.spacing = geometry["cellsize"]
        if (self.width != geometry["ncols"] or self.height != geometry["nrows"] or
                not 1 <= self.width <= 16384 or not 1 <= self.height <= 16384 or
                self.width * self.height > MAX_CELLS or not math.isfinite(self.spacing) or self.spacing <= 0):
            raise ValueError("invalid service grid")
        times = (self.case / "native/input/times_setup.dat").read_text().split()
        if len(times) != 4 or float(times[0]) != 0 or not float(times[2]) > 0:
            raise ValueError("service requires a zero-origin case with positive output cadence")
        self.rain_knots = self._uniform_rain()
        self.expected_bed = self._input_bed()
        self.out.mkdir()
        self.native = self.out / "native"
        shutil.copytree(self.case / "native/input", self.native / "input")
        (self.native / "output").mkdir()
        for name in ("case-spec.json", "case-protocol.json"):
            if (self.case / name).is_file():
                shutil.copy2(self.case / name, self.out / name)
        if input_hashes(self.native / "input") != self.original_hashes:
            raise ValueError("case copy was not byte-exact")
        self.session_id = uuid.uuid4().hex
        self.generation = 1
        self.sequence = 0
        self.lifecycle = "ready"
        self.paused = paused
        self.rain_override = None
        self.last_command_id = 0
        self.last_ack = None
        self.reset_command = None
        self.step_command = None
        self.step_origin_time = 0.0
        self.history = deque(maxlen=64)
        self.stop_requested = False
        self.failure_message = ""
        self.bed = None
        self.planes = None
        self.initial_planes = None
        self.time_s = 0.0
        self.fields_time_s = 0.0
        self.next_dt_s = 0.005
        self.last_publish_wall = -math.inf
        self.pace_wall = time.monotonic()
        self.pace_physical = 0.0
        self.started = False

    def _uniform_rain(self):
        fields = self.case / "native/input/field"
        source = (fields / "precipitation_source_all.dat").read_text().splitlines()
        mask = (fields / "precipitation_mask.dat").read_text().splitlines()
        count = self.width * self.height
        if (not source or source[0].strip() != "1" or len(mask) != count + 3 or
                mask[:3] != ["$Element Number", str(count), "$Element_id  Value"] or
                any(row.split() != [str(index), "0"] for index, row in enumerate(mask[3:]))):
            raise ValueError("service SetRain requires one uniform full-map rainfall region")
        knots = [(float(row.split()[0]), float(row.split()[1])) for row in source[1:] if row.strip()]
        if (len(knots) < 2 or knots[0][0] != 0 or
                any(not math.isfinite(t) or not math.isfinite(r) or r < 0 for t, r in knots) or
                any(a[0] >= b[0] for a, b in zip(knots, knots[1:]))):
            raise ValueError("rain schedule is invalid")
        return knots

    def _input_bed(self):
        lines = (self.case / "native/input/field/z.dat").read_text().splitlines()
        count = self.width * self.height
        if lines[:3] != ["$Element Number", str(count), "$Element_id  Value"] or len(lines) < count + 3:
            raise ValueError("numerical bed input does not cover the dense grid")
        result = bytearray(count * 4)
        for identity, row in enumerate(lines[3:count + 3]):
            values = row.split()
            if len(values) != 2 or values[0] != str(identity):
                raise ValueError("numerical bed native IDs are not sequential")
            value = float(values[1])
            if not math.isfinite(value):
                raise ValueError("nonfinite numerical bed input")
            native_row, column = divmod(identity, self.width)
            raster_index = (self.height - 1 - native_row) * self.width + column
            struct.pack_into("<f", result, raster_index * 4, value)
        return bytes(result)

    def rain_at(self, physical_time):
        if self.rain_override is not None:
            return self.rain_override
        for (ta, ra), (tb, rb) in zip(self.rain_knots, self.rain_knots[1:]):
            if physical_time <= tb:
                return ra + (rb - ra) * (physical_time - ta) / (tb - ta)
        return self.rain_knots[-1][1]

    def _handshake(self, bed):
        if bed != self.expected_bed:
            raise ValueError("native snapshot bed differs from immutable float32 z.dat input")
        self.bed = bed
        atomic_bytes(self.out / "bed.f32", bed)
        identity = dict(self.original_hashes)
        for name in ("case-spec.json", "case-protocol.json"):
            if (self.out / name).is_file():
                identity["../" + name] = digest((self.out / name).read_bytes())
        capabilities = {name: name != "seek" for name in CAPABILITIES}
        metadata = {
            "schema": SCHEMA, "type": "metadata",
            "backend": {"kind": "external", "profile": "external-service",
                        "capability_source": "service-handshake", "id": "synxflow-service-v1"},
            "grid": {"width": self.width, "height": self.height, "spacing_m": self.spacing,
                     "orientation": "row-major-x-fastest-z-rows",
                     "input_sha256": digest(json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()),
                     "solver_bed_sha256": digest(bed)},
            "fields": {"depth_m": True, "horizontal_velocity_x_m_per_s": True,
                       "horizontal_velocity_z_m_per_s": True, "momentum_x_m2_per_s": True,
                       "momentum_z_m2_per_s": True, "tracer_depth_equivalent_m": False,
                       "face_discharge_m3_per_s": False, "water_ledger": False, "tracer_ledger": False},
            "capabilities": {"solver": capabilities, "playback": {name: False for name in CAPABILITIES}},
            "session": {"session_id": self.session_id, "reset_generation": 1, "frame_sequence": 0,
                        "physical_time_s": 0.0, "lifecycle": "ready", "failure_message": ""}}
        atomic_json(self.out / "metadata.json", metadata)

    def publish(self, snapshot=None):
        if snapshot is not None:
            self.planes, bed = raster_fields(snapshot, self.width, self.height)
            self.fields_time_s = self.time_s
            if self.bed is None:
                self._handshake(bed)
            elif bed != self.bed:
                raise ValueError("native solver bed changed during immutable session")
        if self.planes is None:
            raise ValueError("no initialized fields are available to publish")
        self.sequence += 1
        if self.sequence >= 2**64:
            raise ValueError("frame identity exhausted")
        raw = struct.pack("<8sQQd", MAGIC, self.generation, self.sequence, self.time_s) + self.planes
        slot = self.sequence % 3
        atomic_bytes(self.out / f"slot-{slot}.bin", raw)
        state = {"schema": STATE_SCHEMA,
                 "frame": {"schema": SCHEMA, "type": "frame", "session_id": self.session_id,
                           "reset_generation": self.generation, "sequence": self.sequence,
                           "physical_time_s": self.time_s, "lifecycle": self.lifecycle,
                           "failure_message": self.failure_message},
                 "slot": slot, "payload_sha256": digest(raw), "published_unix_s": time.time(),
                 "next_step_s": self.next_dt_s, "rain_m_per_s": self.rain_at(self.time_s),
                 "pacing": self.speed, "ack": self.last_ack}
        atomic_json(self.out / "state.json", state)
        self.last_publish_wall = time.monotonic()

    def acknowledge(self, command, state="applied", message="Applied at synchronized solver boundary"):
        self.last_ack = {"schema": SCHEMA, "type": "ack", "command_id": command["command_id"],
                         "session_id": self.session_id, "reset_generation": self.generation,
                         "state": state, "application_time_s": self.time_s if state == "applied" else None,
                         "lifecycle": self.lifecycle, "message": message[:512]}
        self.history.append({"command": command, "ack": self.last_ack})
        atomic_json(self.out / "controls.json", {"schema": "cubey.fluid25d.control-history.v1",
                                                "total_commands": self.last_command_id,
                                                "retained": list(self.history)})

    def read_command(self):
        path = self.out / "command.json"
        if not path.exists():
            return None
        if path.is_symlink() or path.stat().st_size > MAX_JSON:
            raise ValueError("unsafe command file")
        command = strict_json(path.read_bytes())
        if set(command) != {"schema", "type", "command_id", "session_id", "reset_generation", "domain", "kind", "value"}:
            raise ValueError("unknown command fields")
        identity = command["command_id"]
        if type(identity) is not int or not 1 <= identity < 2**64:
            raise ValueError("invalid command identity")
        if (type(command["reset_generation"]) is not int or
                not 1 <= command["reset_generation"] < 2**64 or
                any(not isinstance(command[key], str) or len(command[key]) > 128
                    for key in ("schema", "type", "session_id", "domain", "kind")) or
                (command["value"] is not None and
                 (isinstance(command["value"], bool) or not isinstance(command["value"], (int, float)) or
                  not math.isfinite(float(command["value"]))))):
            raise ValueError("malformed bounded command fields")
        if identity <= self.last_command_id:
            return None
        self.last_command_id = identity
        if (command["schema"] != SCHEMA or command["type"] != "command" or
                command["session_id"] != self.session_id or command["reset_generation"] != self.generation or
                command["domain"] != "solver" or command["kind"] not in WIRE_COMMANDS or
                command["kind"] == "seek" or self.step_command is not None):
            self.acknowledge(command, "rejected", "Stale session/generation or unsupported command")
            return {"rejected": True}
        try:
            value_kind = command["kind"] in ("set-rain", "set-time-scale")
            if value_kind != (command["value"] is not None):
                raise ValueError("command value presence is invalid")
            if command["kind"] == "set-rain":
                command["value"] = finite(command["value"], 0, 0.01)
            if command["kind"] == "set-time-scale":
                command["value"] = finite(command["value"], 0.125, 300)
            if command["kind"] == "step" and not self.paused:
                raise ValueError("Step requires a paused solver")
        except ValueError as error:
            self.acknowledge(command, "rejected", str(error))
            return {"rejected": True}
        return command

    def boundary(self, initial, physical_time, next_dt, output_due, snapshot):
        native_snapshot = snapshot
        cached_snapshot = []
        def snapshot():
            # Several commands can apply while paused at the same boundary.
            # Native fields have not changed; perform at most one D2H copy here.
            if not cached_snapshot:
                cached_snapshot.append(native_snapshot())
            return cached_snapshot[0]
        if (not math.isfinite(physical_time) or physical_time < 0 or
                not math.isfinite(next_dt) or next_dt < 0 or (next_dt == 0 and not output_due)):
            raise ValueError("invalid native clock or timestep")
        # Stock output clipping deliberately schedules one zero-duration native
        # pass after an exact output boundary. Preserve it; only unexpected
        # positive-dt stalls or regressions are a clock failure.
        if not initial and (physical_time < self.time_s or
                            (physical_time == self.time_s and self.next_dt_s != 0)):
            raise ValueError("native float clock stalled or regressed")
        self.time_s, self.next_dt_s = float(physical_time), float(next_dt)
        if initial:
            # Validate reinitialization before announcing its generation or
            # Applied acknowledgement. A failed reset retains the old fields.
            initial_planes, initial_bed = raster_fields(snapshot, self.width, self.height)
            if initial_bed != self.expected_bed:
                raise ValueError("initialized native bed differs from immutable input")
            if self.initial_planes is not None and initial_planes != self.initial_planes:
                raise ValueError("full native reset did not restore initial depth/momentum bytes")
            if self.started:
                if self.reset_command is None:
                    raise ValueError("unexpected native reinitialization")
                self.generation += 1
                self.rain_override = None
                self.paused = True
                self.lifecycle = "ready"
                self.acknowledge(self.reset_command)
                self.reset_command = None
            else:
                self.lifecycle = "paused" if self.paused else "running"
                self.started = True
            self.pace_wall, self.pace_physical = time.monotonic(), self.time_s
            self.publish(snapshot)
            if self.initial_planes is None:
                self.initial_planes = self.planes
        elif self.step_command is not None and self.time_s > self.step_origin_time:
            self.paused = True
            self.lifecycle = "paused"
            self.acknowledge(self.step_command)
            self.step_command = None
            self.publish(snapshot)
        elif output_due or time.monotonic() - self.last_publish_wall >= self.publish_interval:
            self.publish(snapshot)

        while True:
            if self.stop_requested:
                self.lifecycle = "stopped"
                self.publish(snapshot)
                return {"action": "stop", "rain_override_m_per_s": self.rain_override}
            command = self.read_command()
            if command is not None:
                if command.get("rejected"):
                    self.publish(snapshot)
                else:
                    kind = command["kind"].replace("-", "_")
                    if kind == "reset":
                        self.reset_command = command
                        self.acknowledge(command, "accepted", "Full native reinitialization pending")
                        self.publish(snapshot)
                        return {"action": "reset", "rain_override_m_per_s": None}
                    if kind == "stop":
                        self.lifecycle = "stopped"
                        self.acknowledge(command)
                        self.publish(snapshot)
                        return {"action": "stop", "rain_override_m_per_s": self.rain_override}
                    if kind == "pause":
                        self.paused, self.lifecycle = True, "paused"
                    elif kind == "resume":
                        self.paused, self.lifecycle = False, "running"
                    elif kind == "step":
                        self.step_command = command
                        self.step_origin_time = self.time_s
                        self.paused = False
                        self.acknowledge(command, "accepted", "One native CFL-selected step pending")
                        self.publish(snapshot)
                        return {"action": "continue", "rain_override_m_per_s": self.rain_override}
                    elif kind == "set_rain":
                        # Report the actual float32 value supplied to the native kernel.
                        self.rain_override = struct.unpack("<f", struct.pack("<f", command["value"]))[0]
                    elif kind == "set_time_scale":
                        self.speed = command["value"]
                    self.pace_wall, self.pace_physical = time.monotonic(), self.time_s
                    self.acknowledge(command)
                    self.publish(snapshot)
            if not self.paused:
                target_wall = self.pace_wall + (self.time_s - self.pace_physical) / self.speed
                if time.monotonic() >= target_wall:
                    return {"action": "continue", "rain_override_m_per_s": self.rain_override}
            if time.monotonic() - self.last_publish_wall >= 0.5:
                self.publish(snapshot) # Liveness while paused/paced; physical fields remain held.
            time.sleep(0.01) # Interruptible boundary wait; commands remain responsive.

    def finish(self, lifecycle, message=""):
        local_unchanged = input_hashes(self.native / "input") == self.original_hashes
        reference_unchanged = input_hashes(self.case / "native/input") == self.original_hashes
        if not local_unchanged or not reference_unchanged:
            lifecycle, message = "failed", "Immutable session/reference inputs changed"
        self.lifecycle, self.failure_message = lifecycle, message[:512]
        if lifecycle == "failed":
            self.last_ack = None
        if self.planes is not None and lifecycle != "stopped":
            self.time_s = self.fields_time_s
            self.publish() # Reuse the last synchronized fields; never invent a failed snapshot.
        completion = {
            "schema": "cubey.fluid25d.external-completion.v1", "session_id": self.session_id,
            "lifecycle": lifecycle, "failure_message": self.failure_message,
            "reset_generation": self.generation, "frame_sequence": self.sequence,
            "inputs_unchanged": local_unchanged,
            "reference_inputs_unchanged": reference_unchanged,
            "storage_policy": "three binary slots; latest state/command/ack; last64controls; no native output in service mode"}
        atomic_json(self.out / "completion.json", completion)
        return completion


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--extension", type=Path, required=True)
    parser.add_argument("--case", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--speed", type=float, default=60)
    parser.add_argument("--publish-interval", type=float, default=0.1)
    parser.add_argument("--paused", action="store_true")
    parser.add_argument("--finite", action="store_true", help="explicit finite input horizon, not default continuous service")
    args = parser.parse_args()
    extension = args.extension.resolve(strict=True)
    worker = SessionWorker(args.case, args.out, args.speed, args.publish_interval, args.paused)
    atomic_json(worker.out / "worker-provenance.json", {"schema": "cubey.fluid25d.external-worker.v1",
                "extension_sha256": digest(extension.read_bytes()), "source": str(extension),
                "continuous": not args.finite, "native_outputs": False, "cuda_owner": "separate worker process"})
    for signum in (signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, lambda *_: setattr(worker, "stop_requested", True))
    try:
        spec = importlib.util.spec_from_file_location("flood", extension)
        if spec is None or spec.loader is None:
            raise ValueError("invalid optional extension")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        while True:
            code = module.run_service(str(worker.native), worker.boundary,
                                      continuous=not args.finite, keep_outputs=False)
            if code == 1:
                continue # All native dynamic fields/controllers/cursors rebuilt by run_service.
            if code not in (0, 2):
                raise RuntimeError("unexpected native service return code")
            completion = worker.finish("stopped" if code == 2 else "completed")
            return 1 if completion["lifecycle"] == "failed" else 0
    except Exception as error:
        worker.finish("failed", str(error))
        raise


if __name__ == "__main__":
    raise SystemExit(main())
