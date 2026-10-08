#!/usr/bin/env python3
"""Bounded private-Xvfb control/readability smoke, never owner acceptance."""
from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import csv
import json
import math
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import sys
import tempfile
import time

import run_mountain_rain_demo as demo
import run_native_presentation_v1 as ref

PROBE = ref.APP.parent / "cubey_project_fluid_25d_external_probe"


class PrivateKeys:
    def __init__(self):
        if os.environ.get("CUBEY_PRIVATE_XVFB") != "1" or not os.environ.get("DISPLAY"):
            raise ValueError("requires CUBEY_PRIVATE_XVFB=1 inside a newly owned xvfb-run")
        self.x = ctypes.CDLL(ctypes.util.find_library("X11"))
        self.xt = ctypes.CDLL(ctypes.util.find_library("Xtst"))
        self.x.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x.XOpenDisplay.restype = ctypes.c_void_p
        self.display = self.x.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError("private display unavailable")
        self.x.XStringToKeysym.argtypes = [ctypes.c_char_p]
        self.x.XStringToKeysym.restype = ctypes.c_ulong
        self.x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x.XKeysymToKeycode.restype = ctypes.c_ubyte
        self.x.XFlush.argtypes = [ctypes.c_void_p]
        self.x.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.x.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        self.x.XDefaultRootWindow.restype = ctypes.c_ulong
        self.x.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                                     ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                                     ctypes.POINTER(ctypes.c_uint)]
        self.x.XFetchName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_void_p)]
        self.x.XFree.argtypes = [ctypes.c_void_p]
        self.x.XResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_uint, ctypes.c_uint]
        self.x.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        self.x.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.window_id = None
        self.xt.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
        self.xt.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
        self.xt.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]

    def focus(self):
        if self.window_id is None:
            raise RuntimeError("private viewer window was not resolved for input focus")
        # Xvfb has no window manager to provide click-to-focus after resize or
        # reattachment. Target only the resolved window on our private display.
        self.x.XSetInputFocus(self.display, self.window_id, 2, 0)
        self.x.XSync(self.display, 0)
        self.xt.XTestFakeMotionEvent(self.display, -1, 1800, 980, 0)
        for down in (1, 0):
            self.xt.XTestFakeButtonEvent(self.display, 1, down, 0)
        self.x.XFlush(self.display)
        time.sleep(.1)

    def has_window(self, title):
        root, parent = ctypes.c_ulong(), ctypes.c_ulong()
        children, count = ctypes.POINTER(ctypes.c_ulong)(), ctypes.c_uint()
        if not self.x.XQueryTree(self.display, self.x.XDefaultRootWindow(self.display),
                                ctypes.byref(root), ctypes.byref(parent), ctypes.byref(children), ctypes.byref(count)):
            return False
        try:
            for index in range(count.value):
                name = ctypes.c_void_p()
                if self.x.XFetchName(self.display, children[index], ctypes.byref(name)) and name.value:
                    try:
                        if ctypes.string_at(name).decode(errors="replace") == title:
                            self.window_id = children[index]
                            return True
                    finally:
                        self.x.XFree(name)
        finally:
            if children:
                self.x.XFree(children)
        return False

    def key(self, name):
        code = self.x.XKeysymToKeycode(self.display, self.x.XStringToKeysym(name.encode()))
        if not code:
            raise ValueError("unmapped key")
        self.xt.XTestFakeKeyEvent(self.display, code, 1, 0)
        self.x.XFlush(self.display)
        time.sleep(.06)
        self.xt.XTestFakeKeyEvent(self.display, code, 0, 0)
        self.x.XFlush(self.display)
        time.sleep(.2)

    def resize(self, width, height):
        if self.window_id is None:
            raise RuntimeError("private viewer window was not resolved")
        self.x.XResizeWindow(self.display,self.window_id,width,height)
        self.x.XFlush(self.display)
        time.sleep(1)

    def close(self):
        self.x.XCloseDisplay(self.display)


def until(predicate, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(.05)
    raise TimeoutError("private smoke observation deadline")


def wait_viewer_frame(keys, process, title):
    """A mapped black window is not ready for key edges during cold HDR setup."""
    def window():
        if process.poll() is not None:
            raise RuntimeError("private viewer exited before first presented frame")
        return keys.has_window(title)
    until(window, 40)
    with tempfile.TemporaryDirectory(prefix="cubey-private-frame-readiness-") as temp:
        frame = Path(temp)/"frame.png"
        def presented():
            if not window():
                return False
            captured = subprocess.run(["rtk", "proxy", "import", "-window", hex(keys.window_id), str(frame)],
                                      text=True, capture_output=True, timeout=5)
            if captured.returncode:
                return False
            measured = subprocess.run(["rtk", "proxy", "convert", str(frame), "-format", "%[fx:mean]", "info:"],
                                      text=True, capture_output=True, timeout=5)
            if measured.returncode:
                return False
            mean = float(measured.stdout)
            return math.isfinite(mean) and mean > .001
        until(presented, 40)


def issue_gui_control(keys, session, key, kind):
    """Retry only ignored keys, never an already emitted solver command."""
    path = session/"command.json"
    before = path.read_bytes() if path.is_file() else None
    attempts, last_key = 0, -math.inf
    def issued():
        nonlocal attempts, last_key
        current = path.read_bytes() if path.is_file() else None
        if current is not None and current != before:
            command = json.loads(current)
            if command["domain"] != "solver" or command["kind"] != kind:
                raise ValueError("unexpected private GUI command: " + str(command))
            return command
        now = time.monotonic()
        # Detune from the paused producer's 0.5s heartbeat; 1s probes can
        # repeatedly land in the same briefly disabled-control phase.
        if now-last_key >= 1.3:
            keys.key(key)
            last_key = now
            attempts += 1
        return None
    command = until(issued, 30)
    return {"gui_key": key, "kind": kind, "command_id": command["command_id"],
            "key_attempts": attempts, "stopped_retry_on_command_emission": True}


def live_profile_summary(prefix):
    metrics = {}
    with prefix.with_suffix(".metrics.csv").open(newline="") as stream:
        for row in csv.DictReader(stream):
            if row["category"] == "fluid_25d.external":
                metrics.setdefault(int(row["frame_index"]),{})[row["name"]] = float(row["value"])
    running = {frame:row for frame,row in metrics.items() if frame>=12 and
               row.get("lifecycle_code")==1 and row.get("physical_time_s",0)>=300}
    if not running: raise ValueError("missing running native profile")
    freshness = [row["publication_freshness_s"] for row in running.values()]
    assert all(row["hydraulic_dispatch_count"]==0 for row in metrics.values())
    gpu = []
    with prefix.with_suffix(".passes.csv").open(newline="") as stream:
        for row in csv.DictReader(stream):
            if row["kind"]=="gpu" and row["label"]=="fluid_25d native presentation total" and int(row["frame_index"]) in running:
                gpu.append(float(row["duration_ms"]))
    if not gpu or any(not math.isfinite(v) or v<0 for v in (*gpu,*freshness)):
        raise ValueError("missing/non-finite native live profile")
    def summarize(values):
        return {"samples":len(values),"median":statistics.median(values),
                "p95":sorted(values)[math.ceil(len(values)*.95)-1],"max":max(values)}
    age = summarize(freshness)
    assert age["p95"]<=1 and age["max"]<=2, age
    return {"presentation_gpu_ms":summarize(gpu),"publication_freshness_s":age,
            "freshness_gate":"PASS: p95 <=1s, max <=2s while running",
            "hydraulic_dispatches":0,"cuda_concurrency":True,
            "scope":"running after 300 physical seconds; native total includes upload/cues/dots/Scenic, excludes ImGui/present/encode"}


def verify_scenic_controls(text):
    for event in ("fluid_25d_native_style: mode=readable", "fluid_25d_native_style: mode=scenic",
                  "fluid_25d_scenic_resources: extent=1280x720", "fluid_25d_scenic_resources: extent=1920x1080"):
        if event not in text: raise ValueError("private Scenic control not observed: "+event)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--mode", required=True, choices=("replay", "banks", "live"))
    p.add_argument("--style", choices=("readable", "scenic"), default="readable")
    p.add_argument("--material", choices=("v1", "refined", "terrain", "macro"), default="refined")
    p.add_argument("--live-wall-seconds", type=float, default=0,
                   help="Extra bounded rain-on live observation, 0..300 wall seconds")
    a = p.parse_args()
    if not 0 <= a.live_wall_seconds <= 300 or (a.mode != "live" and a.live_wall_seconds):
        p.error("live observation requires live mode and 0..300 seconds")
    keys = PrivateKeys()
    out = demo.fresh_output(a.out)
    ref.reserve_directory(out)
    before = ref.runtime_identity()
    ref.write_json_exclusive(out / "protocol.json", {"frozen_before_execution": True, "mode": a.mode,
        "private_gui_only": True, "live_target_physical_s": 300, "native_speed": 60,
        "controls": "GUI Space pause/resume and R reset; MIT probe rain edit only while detached; launcher Ctrl-C cleanup",
        "human_visual_acceptance": "deferred", "style": a.style, "material": a.material,
        "viewer_readiness": "owned window title and nonblank first presented frame; bounded 40s",
        "gui_command_readiness": "retry ignored keys at <=1Hz for <=30s; stop at first command emission; no service writes",
        "additional_rain_on_wall_seconds": a.live_wall_seconds})
    env = dict(os.environ, XDG_SESSION_TYPE="x11")
    env.pop("WAYLAND_DISPLAY", None)
    launch_out = out / "launch"
    extra = (["--start", "6000", "--camera", "collection"] if a.mode == "replay" else
             ["--start", "7800"] if a.mode == "banks" else [])
    command = ["rtk", "proxy", sys.executable, str(demo.HERE / "run_mountain_rain_demo.py"), a.mode,
               "--out", str(launch_out), "--width", "1920", "--height", "1080", "--speed", "60",
               "--style", a.style, "--material", a.material, *(["--profile"] if a.mode == "live" else []), *extra]
    captures, observations, reattached = [], [], None
    with (out / "launcher.log").open("xb") as stream:
        process = subprocess.Popen(command, cwd=out, env=env, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            def window_ready():
                if process.poll() is not None:
                    raise RuntimeError("launcher exited before window creation; inspect launcher.log")
                return keys.has_window("Cubey Mountain Rain - " + a.mode)
            until(window_ready, 40)
            # rtk proxy publishes child logs on exit; inspect our private mapped
            # window instead. Cold IBL work can outlast a fixed startup sleep.
            wait_viewer_frame(keys, process, "Cubey Mountain Rain - " + a.mode)
            if process.poll() is not None:
                raise RuntimeError("launcher exited before GUI observation: " + (out / "launcher.log").read_text()[-3000:])
            keys.focus()
            def capture(name, label):
                path = out / (name + ".png")
                subprocess.run(["rtk", "proxy", "import", "-window", "root", str(path)], env=env, check=True, timeout=20)
                if ref.png_dimensions(path) != (1920, 1080):
                    raise ValueError("private viewport dimensions mismatch")
                row = {"path": path.name, "label": label, "sha256": ref.sha256_file(path)}
                if a.mode == "live":
                    row["publication"] = json.loads((launch_out / "session/state.json").read_bytes())
                captures.append(row)

            capture("01-initial", "Actual " + a.mode + " controls and labels")
            if a.style == "scenic":
                keys.key("v")
                keys.key("v")
                keys.resize(1280,720)
                keys.resize(1920,1080)
                keys.focus()
                observations.append({"scenic_style_roundtrip": True, "resize_extents": [[1280,720],[1920,1080]]})
                capture("01b-resize-return", "Scenic style roundtrip and two private-window resizes")
            if a.mode != "live":
                keys.key("s")
                capture("02-bspline", "Experimental B-spline, held state")
                keys.key("s")
                capture("03-next-bank", "Recorded MS" if a.mode == "banks" else "Reference; live MS unavailable")
                keys.key("w")
                keys.key("space")
                time.sleep(1.6)
                capture("04-playing-or-window-end", "Recorded comparison end is not solver completion" if a.mode == "banks" else "Replay advances without running a solver")
                keys.key("r")
                keys.key("Escape")
                if process.wait(timeout=30) != 0:
                    raise RuntimeError("replay/banks launcher failed")
                text = (launch_out / "viewer.log").read_text()
                ref.verify_capture_log(text, False)
                if a.style == "scenic": verify_scenic_controls(text)
                events = re.findall(r"^fluid_25d_bank_view: .+$", text, re.M)
                if not any("mode=bspline-2x" in e for e in events):
                    raise ValueError("actual GUI bank switching not observed")
                if a.mode == "banks":
                    if not any("mode=marching-squares" in e for e in events) or "END OF BAKED WINDOW" not in text:
                        raise ValueError("recorded MS/end control not observed")
                elif any("mode=marching-squares" in e for e in events):
                    raise ValueError("unavailable MS selected")
                observations.append({"bank_events": events})
            else:
                session = launch_out / "session"
                def state():
                    if process.poll() is not None:
                        raise RuntimeError("live launcher exited unexpectedly")
                    return json.loads((session / "state.json").read_bytes())
                def wait_state(predicate, timeout=30):
                    return until(lambda: (s if predicate(s) else None) if (s := state()) else None, timeout)
                initial = state()
                assert initial["frame"]["lifecycle"] == "paused" and initial["frame"]["physical_time_s"] == 0
                initial_planes_sha = ref.sha256_file(session / f"slot-{initial['slot']}.bin")
                initial_planes = (session / f"slot-{initial['slot']}.bin").read_bytes()[32:]
                observations.append(issue_gui_control(keys, session, "space", "resume"))
                running = wait_state(lambda s: s["frame"]["physical_time_s"] >= 300)
                if a.live_wall_seconds:
                    deadline = time.monotonic()+a.live_wall_seconds
                    while time.monotonic()<deadline:
                        current = state()
                        assert current["frame"]["lifecycle"] == "running", current
                        time.sleep(min(1, max(0,deadline-time.monotonic())))
                    running = state()
                capture("02-running", "Actual native rain-on computation, 60x")
                observations.append(issue_gui_control(keys, session, "space", "pause"))
                paused = wait_state(lambda s: s["frame"]["lifecycle"] == "paused")
                keys.key("Escape")
                until(lambda: (launch_out / "detached.json").is_file())
                detached = json.loads((launch_out / "detached.json").read_bytes())
                assert demo.active_owned_group(detached["worker_pid"]), "viewer close stopped worker"
                # No GUI command writer remains during the probe rain edit.
                result = subprocess.run(["rtk", "proxy", str(PROBE), str(session), "set_rain", "0"],
                                        capture_output=True, text=True, timeout=10, check=True)
                rain_off = json.loads(result.stdout)
                assert rain_off["ack"]["state"] == "applied" and rain_off["rain_m_per_s"] == 0
                argv = json.loads((launch_out / "launch.json").read_bytes())["commands"]["viewer"]
                if "--profile-output" in argv:
                    argv[argv.index("--profile-output")+1] = str(out/"reattach-profile")
                with (out / "reattach.log").open("xb") as view_log:
                    reattached = subprocess.Popen(argv, cwd=out, env=env, stdout=view_log, stderr=subprocess.STDOUT, start_new_session=True)
                    wait_viewer_frame(keys, reattached, "Cubey Mountain Rain - " + a.mode)
                    keys.focus()
                    capture("03-rain-off", "Reattached viewer, native rain override is zero")
                    observations.append(issue_gui_control(keys, session, "space", "resume"))
                    after_off = wait_state(lambda s: s["frame"]["physical_time_s"] >= paused["frame"]["physical_time_s"] + 300)
                    observations.append(issue_gui_control(keys, session, "space", "pause"))
                    wait_state(lambda s: s["frame"]["lifecycle"] == "paused")
                    observations.append(issue_gui_control(keys, session, "r", "reset"))
                    reset = wait_state(lambda s: s["frame"]["reset_generation"] == 2 and s["frame"]["physical_time_s"] == 0)
                    assert (session / f"slot-{reset['slot']}.bin").read_bytes()[32:] == initial_planes
                    capture("04-reset", "GUI full reset, new generation, dry fields byte-exact")
                    keys.key("Escape")
                    assert reattached.wait(timeout=30) == 0
                observations += [initial, running, paused, rain_off, after_off, reset,
                                 {"initial_slot_sha256": initial_planes_sha, "reset_planes_byte_exact": True}]
                os.killpg(process.pid, signal.SIGINT)
                until(lambda: (launch_out / "result.json").is_file(), 30)
                process.wait(timeout=10)
                result = json.loads((launch_out / "result.json").read_bytes())
                assert result["exit_code"] == 130 and not result["failure"] and result["viewer_detached"]
                assert not any(c["escalated"] for c in result["owned_cleanup"])
                assert result["completion"]["lifecycle"] == "stopped"
                assert len(list(session.glob("slot-*.bin"))) == 3
                assert not list((session / "native/output").iterdir())
                for log in (launch_out / "viewer.log", out / "reattach.log"):
                    text = log.read_text()
                    ref.verify_capture_log(text, False)
                    assert "VIEWER ERROR" not in text
                if a.style == "scenic": verify_scenic_controls((launch_out/"viewer.log").read_text())
        finally:
            if reattached is not None:
                demo.stop_owned(reattached)
            # Give the foreground owner a chance to gracefully clean its worker.
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGINT)
                until(lambda: (launch_out / "result.json").is_file(), 30)
            demo.stop_owned(process)
            keys.close()
    ref.assert_same_runtime(before, ref.runtime_identity(), "private GUI/native smoke")
    live_profile = live_profile_summary(launch_out/"viewer-profile") if a.mode=="live" else None
    ref.write_json_exclusive(out / "manifest.json", {"status": "pass", "mode": a.mode, "command": command,
        "runtime_identity": before, "style": a.style, "captures": captures, "observations": observations,
        "live_profile":live_profile,
        "launcher_result": json.loads((launch_out / "result.json").read_bytes()),
        "scope": "private Xvfb real UI/control events; bounded native smoke only in live mode; no desktop window touched",
        "human_visual_acceptance": "deferred"})
    print(a.mode + " private GUI/ownership smoke: PASS", flush=True)


if __name__ == "__main__":
    main()
