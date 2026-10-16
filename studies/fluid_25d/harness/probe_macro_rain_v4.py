#!/usr/bin/env python3
"""Actual mouse/keyboard rainfall edits on a newly owned private Xvfb display."""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

import run_mountain_rain_demo as demo
import run_native_presentation_v1 as ref
from probe_mountain_rain_demo import PrivateKeys, until, wait_viewer_frame, issue_gui_control


class RainKeys(PrivateKeys):
    def click(self, x, y):
        self.xt.XTestFakeMotionEvent(self.display, -1, x, y, 0)
        for down in (1, 0):
            self.xt.XTestFakeButtonEvent(self.display, 1, down, 0)
            self.x.XFlush(self.display)
            time.sleep(.06)

    def replace(self, point, value):
        self.click(*point)
        control = self.x.XKeysymToKeycode(self.display, self.x.XStringToKeysym(b"Control_L"))
        self.xt.XTestFakeKeyEvent(self.display, control, 1, 0)
        self.key("a")
        self.xt.XTestFakeKeyEvent(self.display, control, 0, 0)
        self.x.XFlush(self.display)
        for char in str(value):
            self.key({".": "period", "-": "minus"}.get(char, char))


def rain_points(path):
    if ref.png_dimensions(path) != (1920,1080):
        raise ValueError("fixed private input layout requires 1920x1080")
    # Fixed unscaled ImGui layout, checked against the retained initial screen.
    # Wrong focus/layout cannot pass: actual command value and native ack must
    # match every requested rain rate. No OCR package/model dependency.
    return {"input": (65,200), "apply": (74,230),
            "source": "Initial screenshot-reviewed 1920x1080 unscaled private layout"}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", required=True, type=Path)
    a = p.parse_args()
    keys = RainKeys()
    out = demo.fresh_output(a.out)
    ref.reserve_directory(out)
    runtime = ref.runtime_identity()
    env = dict(os.environ, XDG_SESSION_TYPE="x11")
    env.pop("WAYLAND_DISPLAY", None)
    launch = out/"launch"
    session = launch/"session"
    command = ["rtk", "proxy", sys.executable, str(demo.HERE/"run_mountain_rain_demo.py"), "live",
               "--out", str(launch), "--width", "1920", "--height", "1080", "--speed", "60",
               "--style", "scenic", "--material", "macro", "--profile"]
    observations, captures = [], []
    ref.write_json_exclusive(out/"protocol.json", {"mode":"live", "private_gui_only":True,
        "rain_mm_per_hour":[0,120,240,0], "physical_seconds_per_stage":180,
        "commands":"Only real GUI mouse/keyboard; no external probe writes",
        "human_visual_acceptance":"deferred"})
    with (out/"launcher.log").open("xb") as stream:
        process = subprocess.Popen(command,cwd=ref.ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            wait_viewer_frame(keys,process,"Cubey Mountain Rain - live")
            keys.focus()
            def state():
                if process.poll() is not None:
                    raise RuntimeError("private live launcher exited")
                return json.loads((session/"state.json").read_bytes())
            def wait_state(predicate):
                return until(lambda: (s if predicate(s) else None) if (s:=state()) else None, 30)
            def capture(name):
                path = out/(name+".png")
                subprocess.run(["rtk","proxy","import","-window","root",str(path)],env=env,check=True,timeout=20)
                if ref.png_dimensions(path)!=(1920,1080):
                    raise ValueError("private screen size mismatch")
                captures.append({"path":path.name,"sha256":ref.sha256_file(path),"publication":state()})
                return path
            initial = wait_state(lambda s:s["frame"]["lifecycle"]=="paused")
            if initial["frame"]["physical_time_s"]!=0:
                raise ValueError("live must begin dry at native time zero")
            points = rain_points(capture("01-initial"))
            for index, rate in enumerate((0,120,240,0)):
                before = (session/"command.json").read_bytes() if (session/"command.json").is_file() else None
                keys.replace(points["input"],rate)
                last_click = -math.inf
                def applied():
                    nonlocal last_click
                    current = (session/"command.json").read_bytes() if (session/"command.json").is_file() else None
                    if current is not None and current != before:
                        value = json.loads(current)
                        if value["kind"]!="set-rain" or value["domain"]!="solver" or abs(value["value"]*3.6e6-rate)>.001:
                            raise ValueError("unexpected actual GUI rain command: "+str(value))
                        return value
                    if time.monotonic()-last_click>1.3:
                        keys.click(*points["apply"])
                        last_click=time.monotonic()
                    return None
                issued = until(applied,30)
                acknowledged = wait_state(lambda s:s.get("ack") and s["ack"].get("command_id")==issued["command_id"] and
                                           s["ack"]["state"]=="applied")
                if abs(acknowledged["rain_m_per_s"]*3.6e6-rate)>.001:
                    raise ValueError("native acknowledged rain differs from GUI request")
                capture(f"{index+2:02d}-rain-{rate}-applied")
                keys.focus()
                resume = issue_gui_control(keys,session,"space","resume")
                origin = acknowledged["frame"]["physical_time_s"]
                running = wait_state(lambda s:s["frame"]["physical_time_s"]>=origin+180)
                pause = issue_gui_control(keys,session,"space","pause")
                paused = wait_state(lambda s:s["frame"]["lifecycle"]=="paused")
                observations.append({"requested_mm_per_hour":rate,"issued":issued,"acknowledged":acknowledged,
                                     "resume":resume,"running":running,"pause":pause,"paused":paused})
            capture("06-final-rain-off-draining")
            keys.focus()
            keys.key("v")
            capture("07-readable")
            keys.key("v")
            keys.resize(1280,720)
            keys.resize(1920,1080)
            keys.focus()
            capture("08-resize-return")
            keys.key("Escape")
            until(lambda:(launch/"detached.json").is_file(),30)
            os.killpg(process.pid,signal.SIGINT)
            until(lambda:(launch/"result.json").is_file(),30)
            process.wait(timeout=10)
        finally:
            if process.poll() is None:
                os.killpg(process.pid,signal.SIGINT)
                until(lambda:(launch/"result.json").is_file(),30)
            demo.stop_owned(process)
            keys.close()
    result = json.loads((launch/"result.json").read_bytes())
    if result["failure"] or any(row["escalated"] for row in result["owned_cleanup"]):
        raise ValueError("owned native cleanup failed")
    text = (launch/"viewer.log").read_text()
    ref.verify_capture_log(text,False)
    if "VIEWER ERROR" in text:
        raise ValueError("live viewer reported error")
    for event in ("mode=readable","mode=scenic","extent=1280x720","extent=1920x1080"):
        if event not in text:
            raise ValueError("style/resize event missing: "+event)
    ref.assert_same_runtime(runtime,ref.runtime_identity(),"actual private rain controls")
    ref.write_json_exclusive(out/"manifest.json", {"status":"pass","runtime_identity":runtime,
        "captures":captures,"observations":observations,"control_points":points,"launcher_result":result,
        "scope":"Private Xvfb actual GUI edits and bounded native computation, not desktop/human acceptance",
        "human_visual_acceptance":"deferred"})
    print("actual private GUI rainfall controls: PASS",flush=True)


if __name__=="__main__":
    main()
