#!/usr/bin/env python3
"""Exercise only an owned Xvfb raytracing window."""
import argparse
import ctypes as C
from ctypes.util import find_library
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/dev/projects/raytracing/raytracing"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--inspect", action="store_true")
    parser.add_argument("--config",type=Path)
    parser.add_argument("--optics",action="store_true")
    args = parser.parse_args()
    initial=json.loads(args.config.read_text()) if args.config else {}
    solo = initial.get("scene") == "reflection-dragon-solo"
    home_yaw=initial.get("yaw",.6); home_distance=initial.get("distance",2.6)
    home_sun=initial.get("sun-azimuth",-.8)
    if os.environ.get("CUBEY_RT_PRIVATE_XVFB") != "1":
        raise SystemExit("Requires CUBEY_RT_PRIVATE_XVFB=1 in an owned xvfb-run display")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    D, W, U, I = C.c_void_p, C.c_ulong, C.c_uint, C.c_int
    x, t = C.CDLL(find_library("X11")), C.CDLL(find_library("Xtst"))

    def bind(lib, name, parameters, result=I):
        f = getattr(lib, name)
        f.argtypes, f.restype = parameters, result

    bind(x, "XOpenDisplay", [C.c_char_p], D)
    bind(x, "XCloseDisplay", [D])
    bind(x, "XDefaultRootWindow", [D], W)
    bind(x, "XQueryTree", [D, W, C.POINTER(W), C.POINTER(W), C.POINTER(C.POINTER(W)), C.POINTER(U)])
    bind(x, "XFetchName", [D, W, C.POINTER(C.c_char_p)])
    bind(x, "XFree", [D])
    bind(x, "XMoveWindow", [D, W, I, I])
    bind(x, "XResizeWindow", [D, W, U, U])
    bind(x, "XSetInputFocus", [D, W, I, W])
    bind(x, "XSync", [D, I])
    bind(x, "XStringToKeysym", [C.c_char_p], W)
    bind(x, "XKeysymToKeycode", [D, W], C.c_ubyte)
    bind(t, "XTestFakeKeyEvent", [D, U, I, W])
    bind(t, "XTestFakeButtonEvent", [D, U, I, W])
    bind(t, "XTestFakeMotionEvent", [D, I, I, I, W])
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError("Owned Xvfb display unavailable")
    root, window = x.XDefaultRootWindow(display), None
    log = out / "native.log"
    env = dict(os.environ, XDG_SESSION_TYPE="x11", VK_LAYER_VALIDATE_SYNC="1")
    env.pop("WAYLAND_DISPLAY", None)
    command = ["stdbuf", "-oL", str(args.binary.resolve()), "--width", "1000", "--height", "760",
               "--require-validation", "--save-config", str(out / "saved.json"), "--metrics", str(out / "metrics.json")]

    if args.config: command[3:3] = ["--config",str(args.config.resolve())]

    def wait_for(predicate, description):
        deadline = time.monotonic() + 45
        while not predicate():
            if process.poll() is not None:
                raise RuntimeError("Native exit during " + description + ": " + log.read_text())
            if time.monotonic() > deadline:
                raise TimeoutError(description)
            time.sleep(.05)

    def find_window():
        nonlocal window
        a, b, children, count = W(), W(), C.POINTER(W)(), U()
        x.XQueryTree(display, root, C.byref(a), C.byref(b), C.byref(children), C.byref(count))
        for i in range(count.value):
            name = C.c_char_p()
            if x.XFetchName(display, children[i], C.byref(name)) and name.value:
                title = name.value.decode(errors="replace")
                x.XFree(C.cast(name, D))
                if "cubey ray tracing" in title.lower():
                    window = children[i]
        if children:
            x.XFree(C.cast(children, D))
        return window is not None

    def capture(name):
        subprocess.run(["import", "-window", hex(window), str(out / (name + ".png"))], check=True, timeout=10)

    def key(name):
        code = x.XKeysymToKeycode(display, x.XStringToKeysym(name.encode()))
        t.XTestFakeKeyEvent(display, code, 1, 0)
        x.XSync(display, 0)
        time.sleep(.08)
        t.XTestFakeKeyEvent(display, code, 0, 0)
        x.XSync(display, 0)
        time.sleep(.2)

    def click(px, py, button=1):
        t.XTestFakeMotionEvent(display, -1, px + 20, py + 20, 0)
        t.XTestFakeButtonEvent(display, button, 1, 0)
        x.XSync(display, 0)
        time.sleep(.08)
        t.XTestFakeButtonEvent(display, button, 0, 0)
        x.XSync(display, 0)
        time.sleep(.2)

    with log.open("w") as stream:
        process = subprocess.Popen(command, env=env, stdout=stream, stderr=subprocess.STDOUT)
        try:
            wait_for(find_window, "window creation")
            wait_for(lambda: "windowed_render_ready:" in log.read_text(), "render readiness")
            x.XMoveWindow(display, window, 20, 20)
            x.XSetInputFocus(display, window, 1, 0)
            x.XSync(display, 0)
            time.sleep(.3)
            capture("initial")
            if not args.inspect and solo:
                click(140, 428); click(130, 458); click(60, 398)
                wait_for(lambda: json.loads((out / "saved.json").read_text())["subject-material"] == "clay", "clay surface save")
                capture("clay")
                click(180, 279); click(60, 398)
                wait_for(lambda: abs(json.loads((out / "saved.json").read_text())["roughness"] - initial["roughness"]) > .05, "clay roughness edit")
                click(140, 428); click(130, 488); click(60, 398)
                wait_for(lambda: json.loads((out / "saved.json").read_text())["subject-material"] == "mirror", "mirror surface save")
                capture("mirror")
                click(140, 458); click(130, 488); click(60, 398)
                wait_for(lambda: json.loads((out / "saved.json").read_text())["reflection-source"] == "environment", "environment comparison save")
                capture("environment")
                click(140, 458); click(130, 518); click(60, 398)
                wait_for(lambda: json.loads((out / "saved.json").read_text())["reflection-source"] == "scene", "scene reflection save")
                x.XResizeWindow(display, window, 1100, 800)
                x.XSync(display, 0); time.sleep(.4)
                capture("resized")
                x.XResizeWindow(display, window, 1000, 760)
                x.XSync(display, 0); time.sleep(.4)
                capture("restored")
            elif not args.inspect:
                t.XTestFakeMotionEvent(display, -1, 720, 440, 0)
                t.XTestFakeButtonEvent(display, 1, 1, 0)
                x.XSync(display, 0)
                time.sleep(.1)
                for px, py in [(740, 450), (780, 470), (820, 500)]:
                    t.XTestFakeMotionEvent(display, -1, px, py, 0)
                    x.XSync(display, 0)
                    time.sleep(.07)
                t.XTestFakeButtonEvent(display, 1, 0, 0)
                click(800, 500, 4)
                capture("orbit-zoom")
                click(60, 398)
                wait_for(lambda: (out / "saved.json").exists(), "orbit configuration save")
                orbit_config=json.loads((out / "saved.json").read_text())
                assert abs(orbit_config["yaw"]-home_yaw)>.01 and orbit_config["distance"]<home_distance
                click(800, 500)
                key("r")
                capture("reset")
                click(38, 128)
                click(180, 279)
                click(180, 158)
                click(140, 98)
                click(130,149)
                capture("normal-view")
                click(60, 398)
                wait_for(lambda: json.loads((out / "saved.json").read_text())["view"]!="final", "diagnostic view save")
                edited=json.loads((out / "saved.json").read_text())
                assert not edited["shadows"] and abs(edited["roughness"]-.4)>.05
                assert abs(edited["sun-azimuth"]-home_sun)>.05
                assert abs(edited["yaw"]-home_yaw)<1e-5 and abs(edited["distance"]-home_distance)<1e-5
                click(140,98)
                click(130,129)
                click(60,398)
                wait_for(lambda: json.loads((out / "saved.json").read_text())["view"]=="final", "final view restore")
                capture("edited-final")
                x.XResizeWindow(display, window, 1100, 800)
                x.XSync(display, 0)
                time.sleep(.4)
                capture("resized")
                x.XResizeWindow(display, window, 1000, 760)
                x.XSync(display, 0)
                time.sleep(.4)
                capture("restored")
                if args.optics:
                    # Controls follow Save view in the optical panel.
                    click(140,428);click(130,458);click(60,398)
                    wait_for(lambda:json.loads((out/"saved.json").read_text())["reflection-source"]=="environment","reflection route edit")
                    click(140,458);click(130,488);click(60,398)
                    wait_for(lambda:json.loads((out/"saved.json").read_text())["transmission-source"]=="environment","transmission route edit")
                    click(180,488);click(60,398)
                    wait_for(lambda:abs(json.loads((out/"saved.json").read_text())["ior"]-1.5)>.05,"IOR edit")
                    capture("optical-edits")
            key("Escape")
            code = process.wait(timeout=15)
            text = log.read_text()
            if code or "VUID-" in text or "hazard detected" in text.lower() or "vulkan validation error" in text.lower():
                raise RuntimeError(text)
            metrics = json.loads((out / "metrics.json").read_text())
            if not args.inspect:
                assert metrics["as_builds"] == (3 if solo or not args.config else 7)
                assert metrics["target_generations"] >= 3
            (out / "result.json").write_text(json.dumps({"passed": True, "command": command,
                "as_builds": metrics["as_builds"], "target_generations": metrics["target_generations"],
                "scope": "owned Xvfb; physical desktop acceptance separate"}, indent=2) + "\n")
            print((out / "result.json").read_text())
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            x.XCloseDisplay(display)


if __name__ == "__main__":
    main()
