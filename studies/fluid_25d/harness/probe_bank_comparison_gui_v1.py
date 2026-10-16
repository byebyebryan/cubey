#!/usr/bin/env python3
"""Automated private-Xvfb GUI probe; not owner or desktop visual acceptance."""
from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import json
import os
from pathlib import Path
import re
import subprocess
import time

import run_native_bank_comparison_v1 as study
import run_native_presentation_v1 as ref
import run_native_bank_render_v1 as bank


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    if out.parent != ref.OUTPUT_ROOT or not out.name.startswith("bank-comparison-v1-") or args.out.is_symlink():
        raise ValueError("invalid comparison output root")
    if not os.environ.get("DISPLAY") or not os.environ.get("CUBEY_PRIVATE_XVFB"):
        raise ValueError("launch inside private xvfb-run with CUBEY_PRIVATE_XVFB=1")
    phase = out / "gui"
    ref.reserve_directory(phase)
    before = bank.identity()
    x = ctypes.CDLL(ctypes.util.find_library("X11"))
    xt = ctypes.CDLL(ctypes.util.find_library("Xtst"))
    x.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x.XOpenDisplay.restype = ctypes.c_void_p
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError("private display could not be opened")
    x.XStringToKeysym.argtypes = [ctypes.c_char_p]
    x.XStringToKeysym.restype = ctypes.c_ulong
    x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    x.XKeysymToKeycode.restype = ctypes.c_ubyte
    x.XFlush.argtypes = [ctypes.c_void_p]
    x.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xt.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    xt.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
    xt.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]

    def key(name):
        code = x.XKeysymToKeycode(display, x.XStringToKeysym(name.encode()))
        if not code:
            raise RuntimeError("unmapped X key: " + name)
        xt.XTestFakeKeyEvent(display, code, 1, 0)
        x.XFlush(display)
        time.sleep(.06)
        xt.XTestFakeKeyEvent(display, code, 0, 0)
        x.XFlush(display)
        time.sleep(.2)

    # The X server was created by xvfb-run for this process. Never target a user's display.
    env = dict(os.environ, XDG_SESSION_TYPE="x11")
    env.pop("WAYLAND_DISPLAY", None)
    command = ["rtk", "proxy", str(ref.APP), "--width", "1920", "--height", "1080",
               "--fluid25d-recording", str(ref.INPUT_ROOT / "recordings/rain-off/recording.json"),
               "--fluid25d-recording-time-seconds", "7800", "--fluid25d-recording-speed", "60",
               "--fluid25d-recording-camera", "collection", "--fluid25d-native-presentation", "readable",
               *study.mode_args("rain-off", "reference")]
    stdout, stderr = phase / "viewer.stdout.txt", phase / "viewer.stderr.txt"
    rows = []
    with stdout.open("x") as f, stderr.open("x") as e:
        process = subprocess.Popen(command, cwd=phase, env=env, stdout=f, stderr=e)
        try:
            time.sleep(3)
            if process.poll() is not None:
                raise RuntimeError("viewer exited before private GUI probe: " + stderr.read_text()[-2000:])
            xt.XTestFakeMotionEvent(display, -1, 1800, 980, 0)
            xt.XTestFakeButtonEvent(display, 1, 1, 0)
            xt.XTestFakeButtonEvent(display, 1, 0, 0)
            x.XFlush(display)
            time.sleep(.2)

            def capture(name, label):
                path = phase / (name + ".png")
                subprocess.run(["rtk", "proxy", "import", "-window", "root", str(path)],
                               env=env, check=True, timeout=20)
                if ref.png_dimensions(path) != (1920, 1080):
                    raise ValueError("private GUI dimensions mismatch")
                rows.append({"path": path.name, "label": label, "sha256": ref.sha256_file(path)})

            capture("01-reference", "Reference, paused at saved 7800s")
            key("s")
            capture("02-bspline", "B-spline 2x, same paused native state")
            key("s")
            capture("03-marching-squares", "Marching-squares coverage, same paused native state")
            key("w")
            capture("04-dots", "Dots/trails enabled independently")
            key("s")
            capture("05-reference-return", "Return to reference without seeking")
            key("space")
            time.sleep(1.6)
            capture("06-window-end", "End of comparison window, held state, replay explicit")
            key("space")
            key("space")
            key("r")
            capture("07-restart", "Explicit restart to window beginning")
            key("Escape")
            process.wait(timeout=20)
            if process.returncode:
                raise RuntimeError(f"viewer exit {process.returncode}")
        finally:
            if process.poll() is None:
                process.terminate()  # Only the process this probe created.
                process.wait(timeout=10)
            x.XCloseDisplay(display)
    text = stdout.read_text() + stderr.read_text()
    # Numerical upload/readback is a separate headless test contract.
    ref.verify_capture_log(text, False)
    if "END OF BAKED WINDOW" not in text or "bank_cache: READY frames=10" not in text:
        raise ValueError("GUI cache/end controls were not observed")
    events = re.findall(r"^fluid_25d_bank_view: .+$", text, re.MULTILINE)
    if not any("marching-squares" in v for v in events) or not any("bspline-2x" in v for v in events):
        raise ValueError("actual GUI mode keys were not observed")
    held = [re.search(r"mode=(\S+) requested_s=(\S+) saved_s=(\S+) camera=(\S+) "
                      r"paused=(\S+) rate=(\S+) cue_reset=(\S+) quiver_reset=(\S+) "
                      r"marker_reset=(\S+) generation=(\S+);", v).groups() for v in events]
    if [r[0] for r in held] != ["reference", "bspline-2x", "marching-squares", "reference"]:
        raise ValueError("unexpected GUI switching sequence")
    if any(r[1:6] != ("7800.000000000", "7800.000000000", "collection", "1", "60.000") for r in held):
        raise ValueError("GUI bank switching changed camera/time/pause/rate")
    if len(set(r[6:] for r in held[1:])) != 1:
        raise ValueError("GUI switching reset visual histories/generation")
    ref.assert_same_runtime(before, bank.identity(), "private GUI probe")
    ref.write_json_exclusive(phase / "manifest.json", {"runtime_identity": before, "command": command,
        "captures": rows, "mode_events": events, "viewer_stdout_sha256": ref.sha256_file(stdout),
        "viewer_stderr_sha256": ref.sha256_file(stderr), "exit_code": process.returncode,
        "scope": "private Xvfb window, real keyboard events, no desktop/user window touched",
        "human_visual_acceptance": "deferred"})
    print("Private GUI bank controls: PASS", flush=True)


if __name__ == "__main__":
    main()
