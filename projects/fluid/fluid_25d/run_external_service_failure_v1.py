#!/usr/bin/env python3
"""Opt-in scoped GUI controls, disconnect and native-error recovery study."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

from run_external_service_controls_v1 import write
from run_live_view_acceptance_v1 import close_owned_window, owned_window_id, scoped_capture, stop_owned

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("extension", "case", "viewer", "probe", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--keyboard-driver", type=Path, required=True,
                        help="explicit Wayland driver emitting evdev Space=57/R=19; stock wtype uses sequential codes")
    args = parser.parse_args()
    out = args.out.absolute()
    if out.exists() or out.is_symlink() or not out.parent.is_dir():
        raise ValueError("fresh study output leaf required")
    out.mkdir()
    session = out / "session"
    native_command = ["rtk", "proxy", sys.executable, str(HERE / "external_synxflow/session_worker.py"),
                      "--extension", str(args.extension.resolve(strict=True)), "--case", str(args.case.resolve(strict=True)),
                      "--out", str(session), "--paused", "--speed", "60"]
    title = "Cubey service fault study " + out.name
    view_command = ["rtk", "proxy", str(args.viewer.resolve(strict=True)), "--fluid25d-external-session", str(session),
                    "--width", "960", "--height", "640", "--frames", "0", "--title", title]
    window = None
    stopped = False
    observed = []
    with (out / "worker.log").open("wb") as native_log:
        worker = subprocess.Popen(native_command, stdout=native_log, stderr=subprocess.STDOUT, start_new_session=True)
        def state():
            return json.loads((session / "state.json").read_bytes())
        def wait_for(predicate, deadline_s=8):
            deadline = time.monotonic() + deadline_s
            while time.monotonic() < deadline:
                if (session / "state.json").exists():
                    current = state()
                    if predicate(current):
                        observed.append(current)
                        return current
                time.sleep(.02)
            raise RuntimeError("expected service boundary was not observed")
        def send_key(key):
            window_id = owned_window_id(title)
            if window_id is None:
                raise RuntimeError("owned GUI window is unavailable")
            subprocess.run(["rtk", "proxy", "niri", "msg", "action", "focus-window", "--id", window_id], check=True, capture_output=True)
            focused = subprocess.check_output(["rtk", "proxy", "niri", "msg", "--json", "focused-window"], text=True)
            assert str(json.loads(focused)["id"]) == window_id, "refuse to send keys to an unrelated window"
            # Send just after a fresh heartbeat has reached the UI; this tests
            # the application's actual control route, not a raw command write.
            wait_for(lambda item: time.time() - item["published_unix_s"] < .10)
            time.sleep(.12)
            focused = subprocess.check_output(["rtk", "proxy", "niri", "msg", "--json", "focused-window"], text=True)
            assert str(json.loads(focused)["id"]) == window_id, "owned GUI lost focus before key injection"
            subprocess.run(["rtk", "proxy", str(args.keyboard_driver.resolve(strict=True)),
                            "-P", key, "-s", "120", "-p", key],
                           check=True, capture_output=True)
        def close(code):
            close_owned_window(title)
            assert window.wait(timeout=15) == code
        try:
            initial = wait_for(lambda item: item["frame"]["lifecycle"] == "paused", 30)
            with (out / "controls-view.log").open("wb") as log:
                window = subprocess.Popen(view_command, stdout=log, stderr=subprocess.STDOUT)
                deadline = time.monotonic() + 10
                while owned_window_id(title) is None:
                    assert window.poll() is None and time.monotonic() < deadline
                    time.sleep(.05)
                time.sleep(.6)
                send_key("space")
                resumed = wait_for(lambda item: item["ack"] and item["ack"]["command_id"] == 1 and item["ack"]["state"] == "applied")
                assert resumed["frame"]["lifecycle"] == "running"
                wait_for(lambda item: item["frame"]["physical_time_s"] >= 120)
                send_key("space")
                paused = wait_for(lambda item: item["ack"] and item["ack"]["command_id"] == 2 and item["ack"]["state"] == "applied")
                assert paused["frame"]["lifecycle"] == "paused"
                time.sleep(.2)
                assert scoped_capture(title, out / "paused-gui.png")
                send_key("r")
                reset = wait_for(lambda item: item["frame"]["reset_generation"] == 2 and item["ack"]["state"] == "applied")
                assert reset["frame"]["physical_time_s"] == 0 and reset["frame"]["lifecycle"] == "ready"
                time.sleep(.2)
                assert scoped_capture(title, out / "reset-gui.png")
                close(0)
                assert worker.poll() is None and not (session / "completion.json").exists(), "viewer detach must not stop the producer"

            with (out / "disconnected-view.log").open("wb") as log:
                window = subprocess.Popen(view_command, stdout=log, stderr=subprocess.STDOUT)
                deadline = time.monotonic() + 10
                while owned_window_id(title) is None:
                    assert window.poll() is None and time.monotonic() < deadline
                    time.sleep(.05)
                time.sleep(.6)
                os.killpg(worker.pid, signal.SIGSTOP) # Only this explicitly owned tree.
                stopped = True
                time.sleep(3.8)
                assert scoped_capture(title, out / "disconnected-gui.png")
                close(1)
                disconnected = (out / "disconnected-view.log").read_text()
                assert "no changed valid service publication for 3 seconds" in disconnected
                os.killpg(worker.pid, signal.SIGCONT)
                stopped = False

            # Intentionally malformed command at a paused native boundary:
            # Python hook raises, native RAII unwinds and producer reports Failed.
            temporary = session / "command.json.tmp"
            temporary.write_bytes(b'{"schema":1,"schema":2}')
            temporary.replace(session / "command.json")
            assert worker.wait(timeout=10) == 1
            failed = wait_for(lambda item: item["frame"]["lifecycle"] == "failed")
            assert failed["frame"]["reset_generation"] == 2 and failed["frame"]["physical_time_s"] == 0
            completion = json.loads((session / "completion.json").read_bytes())
            assert completion["inputs_unchanged"] and completion["reference_inputs_unchanged"]
            with (out / "failed-view.log").open("wb") as log:
                window = subprocess.Popen(view_command, stdout=log, stderr=subprocess.STDOUT)
                deadline = time.monotonic() + 10
                while owned_window_id(title) is None:
                    assert window.poll() is None and time.monotonic() < deadline
                    time.sleep(.05)
                time.sleep(.3)
                assert scoped_capture(title, out / "failed-gui.png")
                close(1)
            assert "FAILED" in (out / "failed-view.log").read_text()
            write(out / "result.json", {"schema": "cubey.fluid25d.external-failure-result.v1", "passed": True,
                  "native_command": native_command, "viewer_command": view_command, "observations": observed,
                  "keyboard_driver": str(args.keyboard_driver.resolve(strict=True)),
                  "real_gui_resume_pause_reset": True, "detach_preserves_producer": True,
                  "disconnected_after_3s_fail_closed": True, "native_callback_error_unwinds_and_reaps": True,
                  "failed_viewer_exit_nonzero": True, "completion": completion,
                  "human_visual_acceptance": False})
            print(json.dumps({"passed": True, "out": str(out)}), flush=True)
            return 0
        except Exception as error:
            write(out / "failure.json", {"passed": False, "error": repr(error), "observations": observed})
            raise
        finally:
            if window is not None:
                stop_owned(window)
            if stopped and worker.poll() is None:
                os.killpg(worker.pid, signal.SIGCONT)
            if worker.poll() is None:
                os.killpg(worker.pid, signal.SIGINT)
                try:
                    worker.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    os.killpg(worker.pid, signal.SIGTERM)
                    worker.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
