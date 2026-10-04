"""Synthetic recorded-state GPU upload/immutability smoke, not a solver test."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from convert_synxflow_recording_v1 import convert_case
from test_convert_synxflow_recording_v1 import _make_case


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--video", action="store_true", help="also check multi-slot video updates when libav is built")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cubey-recording-gpu-") as temporary:
        root = Path(temporary)
        manifest = convert_case(_make_case(root), root / "recording")
        for time, view, extra in (
            (0, "composite", []),
            (1, "flow-inspection", ["--fluid25d-motion-markers"]),
            (2, "water-isolation", []),
        ):
            command = ["rtk", "proxy", str(args.target.resolve()), "--headless", "--width", "96", "--height", "64",
                       "--fluid25d-recording", str(manifest), "--fluid25d-recording-time-seconds", str(time),
                       "--fluid25d-recording-gpu-validation", "--fluid25d-catchment-view", view,
                       "--output", str(root / f"state-{time}.png"), *extra]
            result = subprocess.run(command, text=True, capture_output=True, timeout=30)
            print(json.dumps({"command": command, "exit_code": result.returncode}))
            print(result.stdout, end="")
            print(result.stderr, end="")
            if any(message in result.stdout + result.stderr for message in (
                "no Vulkan physical devices found", "vkEnumeratePhysicalDevices",
                "no Vulkan device with required queues and dynamic rendering found",
            )):
                return 77
            if result.returncode or "fluid_25d_recording_upload: PASS" not in result.stdout or "vulkan validation error" in (result.stdout + result.stderr).lower():
                return 1
            if not (root / f"state-{time}.png").is_file():
                return 1
        if args.video:
            command = ["rtk", "proxy", str(args.target.resolve()), "--headless", "--capture", "video",
                       "--frames", "4", "--fps", "4", "--width", "96", "--height", "64",
                       "--fluid25d-recording", str(manifest), "--fluid25d-recording-frame-interval-seconds", "1",
                       "--fluid25d-recording-gpu-validation", "--fluid25d-catchment-view", "flow-inspection",
                       "--fluid25d-motion-markers", "--output", str(root / "states.mp4")]
            result = subprocess.run(command, text=True, capture_output=True, timeout=30)
            print(json.dumps({"command": command, "exit_code": result.returncode}))
            print(result.stdout, end="")
            print(result.stderr, end="")
            if result.returncode or "PASS bit-exact bed/h/velocity at saved t=2 s" not in result.stdout:
                return 1
            if "vulkan validation error" in (result.stdout + result.stderr).lower():
                return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
