#!/usr/bin/env python3
"""Separate visual receipts, deliberately outside the performance windows."""
from __future__ import annotations

import argparse
import html
import json
from pathlib import Path
import subprocess
import time

import run_mountain_rain_demo as demo
import run_terrain_rain_demo as terrain
from run_offscreen_perf_v2 import environment, size_owned_window, wait_viewer


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--study", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--app", type=Path, default=Path("build/release/projects/fluid/fluid_25d/fluid_25d"))
    p.add_argument("--baseline-app", type=Path, help="Optional frozen app for exact legacy PNG regression")
    p.add_argument("--headless-only", action="store_true", help="Never open native GUI windows")
    a = p.parse_args()
    root = a.out.resolve(strict=True)
    out = root / "captures"
    if out.exists() or not root.is_relative_to(demo.ref.OUTPUT_ROOT.resolve()):
        p.error("use an approved V2 root with no existing captures")
    out.mkdir()
    recipes = [
        ("mountain-1k-held", "mountain-patch-1024-16m", a.study / "coverage/mountain-patch-1024-16m-live-0/static-recording/recording.json", True),
        ("mountain-2k-held", "mountain-patch-2048-8m", a.study / "release-mountain-patch-2048-8m/mountain-patch-2048-8m-live-0/static-recording/recording.json", True),
        ("mountain-1k-soak-final", "mountain-patch-1024-16m", root / "soak-600s-offscreen/live-0/static-recording/recording.json", False),
        ("rolling-1k-final", "rolling-patch-1024-16m", root / "rolling-confirmation/live-0/static-recording/recording.json", False),
    ]
    receipts, cards, regressions = [], [], []
    for name, case_name, recording, held in recipes:
        native = held and not a.headless_only
        recording = recording.resolve(strict=True)
        case = (a.study / "cases" / case_name).resolve(strict=True)
        document = json.loads(recording.read_text())
        physical = document["protocol"]["duration_s"]
        source_sha = demo.ref.sha256_file(recording)
        common = terrain.viewer_command(case, recording, "Cubey V2 visual " + name,
            live=False, start=physical, app=a.app.resolve(strict=True))
        png = out / (name + "-offscreen.png")
        command = [*common, "--headless", "--output", str(png)]
        with (out / (name + "-offscreen.log")).open("wb") as log:
            subprocess.run(command, env=environment("offscreen"), stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=120)
        raw = (out / (name + "-offscreen.log")).read_text(errors="replace")
        if not png.is_file() or "headless_png: NVIDIA GeForce RTX 5070 Ti" not in raw or "vulkan validation error" in raw:
            raise RuntimeError("invalid offscreen capture receipt")
        receipts.append({"name": name, "host": "offscreen PNG", "physical_s": physical,
                         "recording_sha256": source_sha, "command": command,
                         "image_sha256": demo.ref.sha256_file(png)})
        if held and a.baseline_app:
            baseline_png = out / (name + "-baseline.png")
            baseline_command = [*terrain.viewer_command(case, recording,
                "Cubey V2 baseline " + name, live=False, start=physical,
                app=a.baseline_app.resolve(strict=True)), "--headless", "--output", str(baseline_png)]
            baseline_log = out / (name + "-baseline.log")
            with baseline_log.open("wb") as log:
                subprocess.run(baseline_command, env=environment("offscreen"), stdout=log,
                               stderr=subprocess.STDOUT, check=True, timeout=120)
            baseline_raw = baseline_log.read_text(errors="replace")
            if not baseline_png.is_file() or "headless_png: NVIDIA GeForce RTX 5070 Ti" not in baseline_raw or "vulkan validation error" in baseline_raw:
                raise RuntimeError("invalid archived-app capture receipt")
            same = demo.ref.sha256_file(png) == demo.ref.sha256_file(baseline_png)
            regressions.append({"name": name, "baseline_command": baseline_command,
                "baseline_app_sha256": demo.ref.sha256_file(a.baseline_app),
                "baseline_image_sha256": demo.ref.sha256_file(baseline_png),
                "current_image_sha256": demo.ref.sha256_file(png), "png_byte_identical": same})
            if not same:
                raise RuntimeError("legacy deterministic PNG changed from archived app")
        native_png = None
        if native:
            log_path = out / (name + "-wayland.log")
            title = "Cubey V2 visual " + name
            command = [*common, "--run-seconds", "10", "--render-fps", "60"]
            with log_path.open("wb") as log:
                viewer = subprocess.Popen(command, env=environment("wayland"), stdout=log,
                    stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    wait_viewer(viewer, log_path, "wayland")
                    owned = size_owned_window(title, 1920, 1080)
                    if not owned.get("available"):
                        raise RuntimeError("owned-window capture needs niri")
                    time.sleep(2)
                    native_png = out / (name + "-wayland.png")
                    subprocess.run(["rtk", "proxy", "niri", "msg", "action", "screenshot-window",
                        "--id", str(owned["window_id"]), "--path", str(native_png)], check=True,
                        capture_output=True, timeout=15)
                    viewer.wait(timeout=20)
                    if viewer.returncode or not native_png.is_file() or "vulkan validation error" in log_path.read_text(errors="replace"):
                        raise RuntimeError("native capture failed")
                finally:
                    cleanup = demo.stop_owned(viewer)
            receipts.append({"name": name, "host": "actual native Wayland window", "physical_s": physical,
                             "recording_sha256": source_sha, "command": command, "owned_window": owned,
                             "cleanup": cleanup, "image_sha256": demo.ref.sha256_file(native_png)})
        if demo.ref.sha256_file(recording) != source_sha:
            raise RuntimeError("recording changed during visual capture")
        cards.append(f'<article><h3>{html.escape(name)} · {physical:.1f} physical s</h3>' +
                     f'<p>Exact saved native fields, separate from timings. <a href="captures/{png.name}">Offscreen PNG</a>' +
                     (f' · <a href="captures/{native_png.name}">Native Wayland window (UI retained)</a>' if native_png else '') +
                     f'</p><a href="captures/{png.name}"><img loading="lazy" src="captures/{png.name}"></a></article>')
    (out / "receipt.json").write_text(json.dumps({"app_sha256": demo.ref.sha256_file(a.app),
        "headless_only": a.headless_only,
        "scope": "Actual Vulkan images of exact recorded endpoints, not a live-motion video or a timing sample",
        "captures": receipts, "legacy_png_regressions": regressions}, indent=2) + "\n")
    hosts = "offscreen only" if a.headless_only else "offscreen and native Wayland"
    (out / "gallery.html").write_text('<details><summary>Visual receipts · exact saved states, ' + hosts + '</summary>' + ''.join(cards) + '</details>')
    print(json.dumps({"captures": len(receipts), "out": str(out)}))


if __name__ == "__main__":
    main()
