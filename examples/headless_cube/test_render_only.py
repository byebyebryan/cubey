#!/usr/bin/env python3
"""Actual Vulkan bounded-loop smoke, without a display or capture output."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    env = os.environ.copy()
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_SESSION_TYPE", "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS"):
        env.pop(name, None)
    with tempfile.TemporaryDirectory(prefix="cubey-render-only-") as temporary:
        output = Path(temporary) / "must-not-exist.png"
        for bounded in (["--frames", "8"], ["--frames", "0", "--run-seconds", ".15", "--render-fps", "60"]):
            result = subprocess.run([sys.argv[1], "--headless", "--headless-render-only",
                "--width", "64", "--height", "64", "--output", str(output), *bounded],
                env=env, capture_output=True, text=True, timeout=30)
            text = result.stdout + result.stderr
            print(text)
            if result.returncode:
                if re.search(r"no Vulkan physical devices found|vkEnumeratePhysicalDevices|no Vulkan device with required queues and dynamic rendering found", text):
                    return 77
                return 1
            if "vulkan validation error" in text or output.exists():
                return 1
            completion = re.search(r"headless_render_only_complete: frames=(\d+) wall_s=([\d.]+) readback=0 encode=0", text)
            if not completion or "headless_render_only_ready:" not in text:
                return 1
            frames, wall = int(completion[1]), float(completion[2])
            if bounded == ["--frames", "8"] and frames != 8:
                return 1
            if "--run-seconds" in bounded and not (frames > 1 and .15 <= wall < 10):
                return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
