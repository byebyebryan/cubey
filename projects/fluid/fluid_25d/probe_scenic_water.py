#!/usr/bin/env python3
"""Private replay GUI smoke for consolidated shading and retained diagnostics."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

import review_scenic_water as review
import run_mountain_rain_demo as demo
from probe_macro_rain_v4 import RainKeys
from probe_mountain_rain_demo import wait_viewer_frame


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", required=True, type=Path)
    a = p.parse_args()
    keys = RainKeys()  # Refuses the user's desktop; only owned private Xvfb.
    out = a.out.resolve()
    review.ref.reserve_directory(out)
    pins, runtime = review.sources(), review.ref.runtime_identity()
    env = dict(os.environ, XDG_SESSION_TYPE="x11")
    env.pop("WAYLAND_DISPLAY", None)
    launch = out / "launch"
    command = ["rtk", "proxy", sys.executable, str(demo.HERE / "run_mountain_rain_demo.py"), "replay",
               "--out", str(launch), "--width", "1920", "--height", "1080", "--speed", "60",
               "--style", "scenic", "--material", "macro", "--start", "6000", "--camera", "collection"]
    captures = []
    with (out / "launcher.log").open("xb") as log:
        process = subprocess.Popen(command, cwd=review.ref.ROOT, env=env, stdout=log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            wait_viewer_frame(keys, process, "Cubey Mountain Rain - replay")
            keys.focus()
            def capture(name):
                path = out / (name + ".png")
                subprocess.run(["rtk", "proxy", "import", "-window", "root", str(path)],
                               env=env, check=True, timeout=20)
                captures.append({"path": path.name, "sha256": review.ref.sha256_file(path)})
            def select(index):
                name = ("shaded", "environment-only", "direct-only", "transmission-only",
                        "no-environment", "no-direct", "no-clarity", "no-detail",
                        "depth-bands", "coverage", "film-weight")[index]
                for attempt in range(3):
                    keys.click(100, 771)
                    time.sleep(.6)
                    capture(f"popup-{len(captures)}")
                    keys.click(100, 802 + index * 20)
                    time.sleep(.6)
                    events = re.findall(r"^fluid_25d_scenic_water: .+$", (launch / "viewer.log").read_text(), re.M)
                    if events and f"view={name} " in events[-1]:
                        return
                raise ValueError("actual GUI selection did not apply: " + name)
            capture("01-initial")
            keys.click(180, 712)
            capture("02-materials")
            select(1)
            capture("03-environment")
            select(5)  # Within ImGui's default eight-row popup; no blind offscreen click.
            capture("04-no-direct")
            select(0)
            capture("05-shaded")
            keys.click(180, 712)
            keys.key("v")
            capture("06-readable")
            keys.key("v")
            keys.resize(1280, 720)
            keys.resize(1920, 1080)
            keys.focus()
            keys.key("space")
            time.sleep(1)
            keys.key("space")
            capture("07-resize-playback")
            keys.key("Escape")
            if process.wait(timeout=30) != 0:
                raise RuntimeError("private launcher failed")
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGINT)
                process.wait(timeout=30)
            keys.close()
    text = (launch / "viewer.log").read_text()
    review.ref.verify_capture_log(text, False)
    events = re.findall(r"^fluid_25d_scenic_water: .+$", text, re.M)
    for view in ("shaded", "environment-only", "no-direct"):
        if not any(f"view={view}" in e and "shading=wet-ground" in e and "pipeline=normal" in e for e in events):
            raise ValueError("actual GUI component selection missing: " + view)
    if not all("dots_draw=0" in e for e in events if "view=shaded" not in e):
        raise ValueError("diagnostic dots not suppressed")
    terrain_events = re.findall(r"^fluid_25d_scenic_terrain: .+$", text, re.M)
    if len(terrain_events) != len(events) or any(
            re.search(r"dots_draw=\d", t).group() != re.search(r"dots_draw=\d", w).group()
            for t, w in zip(terrain_events, events)):
        raise ValueError("terrain/water diagnostic dot receipts disagree")
    styles = re.findall(r"^fluid_25d_native_style: mode=(\S+)$", text, re.M)
    if "readable" not in styles or "scenic" not in styles:
        raise ValueError("actual style roundtrip missing")
    review.ref.assert_same_runtime(runtime, review.ref.runtime_identity(), "private GUI")
    if pins != review.sources():
        raise ValueError("GUI sources changed")
    review.ref.write_json_exclusive(out / "result.json", {
        "status": "PASS", "source_files": pins, "runtime_identity": runtime,
        "events": events, "styles": styles, "captures": captures,
        "scope": "Actual private replay diagnostics/style/resize/playback; no live solver or human approval"})
    print("PASS: private consolidated Scenic water GUI")


if __name__ == "__main__":
    main()
