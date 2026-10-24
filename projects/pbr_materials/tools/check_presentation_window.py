#!/usr/bin/env python3
"""Exercise both native presentation presets in an isolated Xvfb display."""
import argparse
import ctypes as C
from ctypes.util import find_library
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "outputs/material-presentation/windowed")
    args = parser.parse_args()
    if os.environ.get("CUBEY_PRESENTATION_PRIVATE_XVFB") != "1":
        raise SystemExit("Use CUBEY_PRESENTATION_PRIVATE_XVFB=1 xvfb-run -a to isolate the checks")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    x, test = C.CDLL(find_library("X11")), C.CDLL(find_library("Xtst"))
    D, W, U, I = C.c_void_p, C.c_ulong, C.c_uint, C.c_int

    def bind(lib, name, args, result=I):
        f = getattr(lib, name)
        f.argtypes, f.restype = args, result
        return f

    bind(x, "XOpenDisplay", [C.c_char_p], D)
    bind(x, "XDefaultRootWindow", [D], W)
    bind(x, "XQueryTree", [D, W, C.POINTER(W), C.POINTER(W), C.POINTER(C.POINTER(W)), C.POINTER(U)])
    bind(x, "XFetchName", [D, W, C.POINTER(C.c_char_p)])
    bind(x, "XFree", [D])
    bind(x, "XCloseDisplay", [D])
    bind(x, "XMoveWindow", [D, W, I, I])
    bind(x, "XResizeWindow", [D, W, U, U])
    bind(x, "XSetInputFocus", [D, W, I, W])
    bind(x, "XSync", [D, I])
    bind(x, "XStringToKeysym", [C.c_char_p], W)
    bind(x, "XKeysymToKeycode", [D, W], C.c_ubyte)
    bind(test, "XTestFakeKeyEvent", [D, U, I, W])
    bind(test, "XTestFakeButtonEvent", [D, U, I, W])
    bind(test, "XTestFakeMotionEvent", [D, I, I, I, W])
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError("private display unavailable")
    root = x.XDefaultRootWindow(display)
    receipts = {}

    def key(name):
        code = x.XKeysymToKeycode(display, x.XStringToKeysym(name.encode()))
        test.XTestFakeKeyEvent(display, code, 1, 0)
        x.XSync(display, 0)
        time.sleep(.06)
        test.XTestFakeKeyEvent(display, code, 0, 0)
        x.XSync(display, 0)

    def capture(name):
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "x11grab", "-video_size", "1200x900",
                        "-i", os.environ["DISPLAY"] + "+0,0", "-frames:v", "1", str(out / name)], check=True)

    try:
        for project, preset in (("pbr_materials", "contrast"), ("sdf", "wood-cut")):
            log = out / f"{project}.log"
            cmd = ["stdbuf", "-oL", str(ROOT / f"build/release/projects/{project}/{project}"),
                   "--config", str(ROOT / f"projects/{project}/presets/presentation/{preset}.json"),
                   "--require-validation", "--width", "1200", "--height", "900"]
            if project == "sdf":
                cmd += ["--authored-scene", str(ROOT / "projects/sdf/scenes/material-presentation/wood-cut.json")]
            with tempfile.TemporaryDirectory(prefix="cubey-material-window-") as work, log.open("w") as stream:
                proc = subprocess.Popen(cmd, cwd=work, stdout=stream, stderr=subprocess.STDOUT)
                try:
                    window = None
                    deadline = time.monotonic() + 45
                    while not window or "windowed_render_ready:" not in log.read_text():
                        if proc.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError("native readiness failed: " + log.read_text()[-2000:])
                        r, parent, children, count = W(), W(), C.POINTER(W)(), U()
                        x.XQueryTree(display, root, C.byref(r), C.byref(parent), C.byref(children), C.byref(count))
                        for i in range(count.value):
                            name = C.c_char_p()
                            if x.XFetchName(display, children[i], C.byref(name)) and name.value:
                                if "cubey" in name.value.decode().lower():
                                    window = children[i]
                                x.XFree(C.cast(name, D))
                        if children:
                            x.XFree(C.cast(children, D))
                        time.sleep(.1)
                    x.XMoveWindow(display, window, 0, 0)
                    x.XSetInputFocus(display, window, 1, 0)
                    test.XTestFakeMotionEvent(display, -1, 1100, 780, 0)
                    x.XSync(display, 0)
                    time.sleep(.4)
                    key("space")
                    capture(f"{project}-paused.png")
                    x.XResizeWindow(display, window, 1050, 760)
                    x.XSync(display, 0)
                    time.sleep(.3)
                    x.XResizeWindow(display, window, 1200, 900)
                    x.XSync(display, 0)
                    time.sleep(.3)
                    test.XTestFakeButtonEvent(display, 1, 1, 0)
                    for step in range(10):
                        test.XTestFakeMotionEvent(display, -1, 950 + step * 6, 720 + step * 2, 0)
                        x.XSync(display, 0)
                        time.sleep(.025)
                    test.XTestFakeButtonEvent(display, 1, 0, 0)
                    test.XTestFakeButtonEvent(display, 4, 1, 0)
                    test.XTestFakeButtonEvent(display, 4, 0, 0)
                    key("r")
                    key("space")
                    time.sleep(.3)
                    capture(f"{project}-inspection.png")
                    key("Escape")
                    proc.wait(timeout=15)
                    text = log.read_text()
                    if proc.returncode or "vulkan validation error" in text.lower():
                        raise RuntimeError(text[-2500:])
                    receipts[project] = dict(passed=True, command=cmd,
                        actions=["native preset", "Space pause", "resize twice", "pointer orbit", "scroll", "R", "Space", "Escape"],
                        log=log.name)
                finally:
                    if proc.poll() is None:
                        proc.terminate()
                        proc.wait(timeout=10)
    finally:
        x.XCloseDisplay(display)
    (out / "windowed.json").write_text(json.dumps(receipts, indent=2) + "\n")
    print("Both native presets passed isolated input/resize/validation checks.")


if __name__ == "__main__":
    main()
