"""Repeatable native orbit/stop/settle on an explicitly owned Xvfb display.

Timing runs omit video; optional recording is separate, with its overhead labeled.
"""

import argparse
import ctypes as C
import hashlib
import json
import os
import subprocess
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--binary", type=Path, default=Path("build/dev/projects/raytracing/raytracing")
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--video", action="store_true")
    parser.add_argument("--single-orbit", action="store_true")
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--settle-seconds", type=float, default=30)
    args = parser.parse_args()
    if os.environ.get("CUBEY_RT_PRIVATE_XVFB") != "1":
        parser.error("Use CUBEY_RT_PRIVATE_XVFB=1 and an owned xvfb-run")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    x, xt = C.CDLL("libX11.so.6"), C.CDLL("libXtst.so.6")
    D, W, U, I = C.c_void_p, C.c_ulong, C.c_uint, C.c_int
    for lib, name, parameters, result in [
        (x, "XOpenDisplay", [C.c_char_p], D),
        (x, "XDefaultRootWindow", [D], W),
        (
            x,
            "XQueryTree",
            [D, W, C.POINTER(W), C.POINTER(W), C.POINTER(C.POINTER(W)), C.POINTER(U)],
            I,
        ),
        (x, "XFetchName", [D, W, C.POINTER(C.c_char_p)], I),
        (x, "XFree", [D], I),
        (x, "XSetInputFocus", [D, W, I, W], I),
        (x, "XSync", [D, I], I),
        (x, "XStringToKeysym", [C.c_char_p], W),
        (x, "XKeysymToKeycode", [D, W], C.c_ubyte),
        (xt, "XTestFakeKeyEvent", [D, U, I, W], I),
        (xt, "XTestFakeButtonEvent", [D, U, I, W], I),
        (xt, "XTestFakeMotionEvent", [D, I, I, I, W], I),
        (x, "XCloseDisplay", [D], I),
        (
            x,
            "XTranslateCoordinates",
            [D, W, W, I, I, C.POINTER(I), C.POINTER(I), C.POINTER(W)],
            I,
        ),
    ]:
        f = getattr(lib, name)
        f.argtypes, f.restype = parameters, result
    display = x.XOpenDisplay(None)
    assert display, "Owned display unavailable"
    root, window = x.XDefaultRootWindow(display), None
    env = dict(os.environ, XDG_SESSION_TYPE="x11")
    env.pop("WAYLAND_DISPLAY", None)
    if args.validate:
        env["VK_LAYER_VALIDATE_SYNC"] = "1"
    command = [
        str(args.binary.resolve()),
        "--config",
        "projects/raytracing/presets/optics/dragon-glass.json",
        "--width",
        str(args.width),
        "--height",
        str(args.height),
        "--samples",
        "512",
        "--metrics",
        str(out / "metrics.json"),
        "--profile-output",
        str(out / "host"),
        "--profile-max-frames",
        "20000",
    ]
    command += ["--require-validation"] if args.validate else ["--no-validation"]
    events, phases = [], []
    process, video = None, None
    start_ns = time.monotonic_ns()
    with (out / "native.log").open("w") as log:
        try:
            process = subprocess.Popen(
                command, env=env, stdout=log, stderr=subprocess.STDOUT
            )
            deadline = time.monotonic() + 120
            while (
                window is None
                or "windowed_render_ready:" not in (out / "native.log").read_text()
            ):
                assert process.poll() is None, (out / "native.log").read_text()
                a, b, children, count = W(), W(), C.POINTER(W)(), U()
                x.XQueryTree(
                    display,
                    root,
                    C.byref(a),
                    C.byref(b),
                    C.byref(children),
                    C.byref(count),
                )
                for i in range(count.value):
                    title = C.c_char_p()
                    if (
                        x.XFetchName(display, children[i], C.byref(title))
                        and title.value
                    ):
                        name = title.value.decode(errors="replace")
                        x.XFree(C.cast(title, D))
                        if "raytrac" in name.lower().replace(" ", ""):
                            window = children[i]
                if children:
                    x.XFree(C.cast(children, D))
                assert time.monotonic() < deadline, "native readiness timeout"
                time.sleep(0.05)
            rx, ry, child = I(), I(), W()
            x.XTranslateCoordinates(
                display, window, root, 0, 0, C.byref(rx), C.byref(ry), C.byref(child)
            )
            x.XSetInputFocus(display, window, 1, 0)

            def inject(kind, operation, **details):
                begin = time.monotonic_ns()
                operation()
                x.XSync(display, 0)
                events.append(
                    dict(
                        kind=kind, begin_ns=begin, end_ns=time.monotonic_ns(), **details
                    )
                )

            def move(px, py):
                inject(
                    "motion",
                    lambda: xt.XTestFakeMotionEvent(
                        display, -1, rx.value + px, ry.value + py, 0
                    ),
                    x=px,
                    y=py,
                )

            def button(pressed):
                inject(
                    "left-button",
                    lambda: xt.XTestFakeButtonEvent(display, 1, int(pressed), 0),
                    pressed=pressed,
                )

            def wait_phase(name, seconds):
                begin = time.monotonic_ns()
                time.sleep(seconds)
                phases.append(
                    {"name": name, "begin_ns": begin, "end_ns": time.monotonic_ns()}
                )

            # Focus the editor before warmup without modifying any scene setting.
            time.sleep(0.5)
            move(20, 20)
            button(True)
            time.sleep(0.08)
            button(False)
            wait_phase("warmup", 2)
            if args.video:
                stream = (out / "ffmpeg.log").open("w")
                video = subprocess.Popen(
                    [
                        "ffmpeg",
                        "-y",
                        "-f",
                        "x11grab",
                        "-draw_mouse",
                        "0",
                        "-framerate",
                        "30",
                        "-video_size",
                        f"{args.width}x{args.height}",
                        "-i",
                        f"{os.environ['DISPLAY']}+{rx.value},{ry.value}",
                        "-c:v",
                        "libx264",
                        "-preset",
                        "ultrafast",
                        "-crf",
                        "18",
                        "-pix_fmt",
                        "yuv420p",
                        str(out / "orbit-settle.mp4"),
                    ],
                    stdin=subprocess.PIPE,
                    stdout=stream,
                    stderr=subprocess.STDOUT,
                )
            # Fixed pixel displacement gives the same angular trajectory at each extent.
            origin_x, origin_y = int(args.width * 0.65), int(args.height * 0.55)
            move(origin_x, origin_y)
            trajectory = (
                [("outward", False)]
                if args.single_orbit
                else [("outward", False), ("return", True)]
            )
            for name, reverse in trajectory:
                begin = time.monotonic_ns()
                button(True)
                time.sleep(0.15)
                motion_begin = time.monotonic_ns()
                for step in range(1, 121):
                    deadline_step = motion_begin + int(step / 60 * 1e9)
                    time.sleep(max(0, (deadline_step - time.monotonic_ns()) / 1e9))
                    px = origin_x + round(
                        200 * ((120 - step) if reverse else step) / 120
                    )
                    move(px, origin_y)
                # Let the endpoint reach an app poll while the button is held.
                time.sleep(0.2)
                button(False)
                phases.append(
                    {
                        "name": "orbit-" + name,
                        "begin_ns": begin,
                        "end_ns": time.monotonic_ns(),
                    }
                )
                wait_phase("settle-" + name, args.settle_seconds)
                if args.video:
                    subprocess.run(
                        [
                            "import",
                            "-window",
                            hex(window),
                            str(out / (name + "-settled.png")),
                        ],
                        check=True,
                        timeout=15,
                    )
            if video:
                video.communicate(b"q\n", timeout=30)
                assert video.returncode == 0, (out / "ffmpeg.log").read_text()
            code = x.XKeysymToKeycode(display, x.XStringToKeysym(b"Escape"))
            inject("escape", lambda: xt.XTestFakeKeyEvent(display, code, 1, 0))
            xt.XTestFakeKeyEvent(display, code, 0, 0)
            x.XSync(display, 0)
            assert process.wait(timeout=30) == 0, (out / "native.log").read_text()
            text = (out / "native.log").read_text().lower()
            assert not any(
                t in text
                for t in ["vuid-", "hazard detected", "vulkan validation error"]
            ), text
            metrics = json.loads((out / "metrics.json").read_text())
            (out / "events.json").write_text(
                json.dumps(
                    {
                        "command": command,
                        "phases": phases,
                        "events": events,
                        "width": args.width,
                        "height": args.height,
                        "recording": args.video,
                        "validation": args.validate,
                        "binary_sha256": hashlib.sha256(
                            args.binary.read_bytes()
                        ).hexdigest(),
                        "scope": "Owned Xvfb native Vulkan window; injection and app receipt timestamps; no physical-display latency claim",
                    },
                    indent=2,
                )
                + "\n"
            )
            assert metrics["as_builds"] == 3 and metrics["target_generations"] == 1
            assert (
                metrics["history_samples"] == 512
                and metrics["dropped_native_frames"] == 0
            )
            assert metrics["optical_camera_rejected_updates"] == 0
            assert metrics["native_frames"], "instrumented binary required"
            assert all(
                start_ns < f["record_begin_steady_ns"] < time.monotonic_ns()
                for f in metrics["native_frames"]
            )
            if not args.single_orbit:
                assert abs(metrics["native_frames"][-1]["yaw"] - 0.45) < 1e-5, (
                    "trajectory did not return to the canonical view"
                )
            (out / "receipt.json").write_text(
                json.dumps(
                    {
                        "passed": True,
                        "as_builds": metrics["as_builds"],
                        "samples": metrics["history_samples"],
                        "frames": len(metrics["native_frames"]),
                        "validation": args.validate,
                        "recording": args.video,
                    },
                    indent=2,
                )
                + "\n"
            )
            print("Native orbit/settle complete:", out, flush=True)
        finally:
            if video and video.poll() is None:
                video.terminate()
                video.wait(timeout=15)
            if process and process.poll() is None:
                process.terminate()
                process.wait(timeout=15)
            x.XCloseDisplay(display)


if __name__ == "__main__":
    main()
