#!/usr/bin/env python3
"""Explicit real-worker/MIT-client control study; no GUI or default test launch."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent


def write(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def percentile(values):
    values = sorted(values)
    return values[max(0, int(0.95 * len(values) + 0.999999) - 1)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--extension", type=Path, required=True)
    parser.add_argument("--case", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--protocol", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    protocol = json.loads(args.protocol.read_text())
    if protocol.get("schema") != "cubey.fluid25d.external-controls-protocol.v1" or not protocol.get("frozen_before_candidate_execution"):
        raise ValueError("missing frozen native control protocol")
    extension, case, probe = [path.resolve(strict=True) for path in (args.extension, args.case, args.probe)]
    out = args.out.absolute()
    if out.exists() or out.is_symlink() or not out.parent.is_dir():
        raise ValueError("fresh study output leaf required")
    out.mkdir()
    records = []
    command = ["rtk", "proxy", sys.executable, str(HERE / "external_synxflow/session_worker.py"),
               "--extension", str(extension), "--case", str(case), "--out", str(out / "session"),
               "--paused", "--speed", "300"]
    with (out / "worker.log").open("wb") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        def wait_ready():
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError("worker exited before publishing initialized state")
                if (out / "session/state.json").is_file():
                    return
                time.sleep(0.01)
            raise RuntimeError("worker initialization timed out")
        def observe(kind=None, value=None):
            argv = ["rtk", "proxy", str(probe), str(out / "session")]
            if kind is not None:
                argv.append(kind)
            if value is not None:
                argv.append(str(value))
            result = subprocess.run(argv, text=True, capture_output=True, timeout=8)
            if result.returncode:
                raise RuntimeError("MIT client probe failed: " + result.stderr + result.stdout)
            item = json.loads(result.stdout)
            item["command"] = kind
            records.append(item)
            if kind is not None:
                assert item["ack"]["state"] == "applied", item
            return item
        def planes():
            state = json.loads((out / "session/state.json").read_bytes())
            raw = (out / "session" / f"slot-{state['slot']}.bin").read_bytes()
            assert hashlib.sha256(raw).hexdigest() == state["payload_sha256"]
            return state, raw[32:]
        try:
            wait_ready()
            initial = observe()
            assert initial["frame"]["lifecycle"] == "paused" and initial["water_volume_m3"] == 0
            initial_state, initial_planes = planes()
            step = observe("step")
            assert step["frame"]["physical_time_s"] > 0 and step["water_volume_m3"] > 0
            observe("resume")
            time.sleep(0.8)
            paused = observe("pause")
            assert paused["water_volume_m3"] > step["water_volume_m3"]
            pause_state, held = planes()
            time.sleep(0.65) # Includes a paused liveness publication, not numerical advancement.
            still = observe()
            _, still_planes = planes()
            assert still["frame"]["physical_time_s"] == paused["frame"]["physical_time_s"]
            assert still_planes == held and still["state_age_s"] < 1
            rain = observe("set_rain", 240 / 3.6e6)
            _, rain_planes = planes()
            assert rain_planes == held and rain["frame"]["physical_time_s"] == paused["frame"]["physical_time_s"]
            assert abs(rain["rain_m_per_s"] - 240 / 3.6e6) < 1e-10
            advanced = observe("step")
            assert advanced["frame"]["physical_time_s"] > rain["frame"]["physical_time_s"]
            assert advanced["water_volume_m3"] > 0
            off = observe("set_rain", 0)
            assert off["rain_m_per_s"] == 0 and off["water_volume_m3"] == advanced["water_volume_m3"]
            paced = observe("set_time_scale", 60)
            assert paced["pacing"] == 60 and paced["frame"]["physical_time_s"] == off["frame"]["physical_time_s"]
            observe("resume")
            time.sleep(0.8)
            recession = observe("pause")
            assert recession["frame"]["physical_time_s"] > paced["frame"]["physical_time_s"]
            assert recession["water_volume_m3"] > 0
            reset = observe("reset")
            state, reset_planes = planes()
            assert reset["frame"]["reset_generation"] == initial["frame"]["reset_generation"] + 1
            assert reset["frame"]["physical_time_s"] == 0 and reset["frame"]["lifecycle"] == "ready"
            assert reset_planes == initial_planes and reset["frame"]["sequence"] > recession["frame"]["sequence"]
            # Warmed acknowledgements using the real C++ client, not raw file writes.
            warm = []
            for _ in range(10):
                warm.append(observe("resume")["acknowledgement_ms"])
                warm.append(observe("pause")["acknowledgement_ms"])
            assert percentile(warm) <= protocol["warmed_ack_p95_ms"]
            final = observe("stop")
            assert final["frame"]["lifecycle"] == "stopped"
            assert process.wait(timeout=5) == 0
            completion = json.loads((out / "session/completion.json").read_bytes())
            assert completion["inputs_unchanged"] and completion["reference_inputs_unchanged"]
            assert len(list((out / "session").glob("slot-*.bin"))) == 3
            assert not list((out / "session/native/output").iterdir())
            write(out / "result.json", {"schema": "cubey.fluid25d.external-controls-result.v1", "passed": True,
                  "protocol": str(args.protocol), "worker_command": command,
                  "extension_sha256": hashlib.sha256(extension.read_bytes()).hexdigest(),
                  "probe_sha256": hashlib.sha256(probe.read_bytes()).hexdigest(),
                  "initial_planes_sha256": hashlib.sha256(initial_planes).hexdigest(),
                  "reset_planes_byte_exact": True, "paused_planes_byte_exact": True,
                  "warm_ack_samples": len(warm), "warm_ack_p95_ms": percentile(warm),
                  "warm_ack_max_ms": max(warm), "completion": completion, "observations": records,
                  "scope": "Real native controls through MIT client; no GUI, 10min soak or rendering acceptance"})
            print(json.dumps({"passed": True, "warm_ack_p95_ms": percentile(warm), "out": str(out)}))
            return 0
        except Exception as error:
            write(out / "failure.json", {"passed": False, "error": repr(error), "observations": records})
            raise
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGINT) # Only this explicitly owned process group.
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGTERM)
                    process.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
