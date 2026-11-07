"""Check that exported views reproduce the final headless orbit frame."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys


def capture(binary, output, arguments):
    environment = os.environ.copy()
    for key in (
        "DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_SESSION_TYPE",
        "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS",
    ):
        environment.pop(key, None)
    environment["VK_LAYER_VALIDATE_SYNC"] = "1"
    command = [
        str(binary), "--headless", "--width", "128", "--height", "96",
        "--require-validation", *arguments, "--output", str(output),
    ]
    result = subprocess.run(
        command, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=30,
    )
    output.with_suffix(".log").write_text(result.stdout)
    if any(marker in result.stdout for marker in (
        "vulkan validation error", "Validation Error", "VUID-", "SYNC-HAZARD",
    )):
        raise RuntimeError(f"Vulkan validation failed: {output.with_suffix('.log')}")
    if result.returncode:
        if any(marker in result.stdout for marker in (
            "no Vulkan physical devices found", "vkEnumeratePhysicalDevices",
            "no Vulkan device with required queues and dynamic rendering found",
        )):
            print("view replay: skipped because Vulkan is unavailable")
            sys.exit(77)
        raise RuntimeError(f"Capture failed: {output.with_suffix('.log')}")
    if not output.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
        raise RuntimeError(f"Missing PNG capture: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cases = []
    for source in ("screen", "environment"):
        for presentation in (False, True):
            name = f"{source}-{'presentation' if presentation else 'workshop'}"
            folder = output / name
            folder.mkdir(exist_ok=True)
            saved = folder / "view.json"
            original, replay = folder / "original.png", folder / "replay.png"
            options = [
                "--frames", "2", "--lighting", "clear", "--yaw", "0.17",
                "--orbit-degrees", "30", "--transmission-source", source,
                "--view-output", str(saved),
            ]
            if presentation:
                options += [
                    "--presentation", "library", "--lighting-cycle", "frozen",
                    "--presentation-time", "3",
                ]
            else:
                options += ["--material", "glass"]
            capture(args.binary.resolve(), original, options)
            view = json.loads(saved.read_text())
            expected_yaw = 0.17 + math.radians(30)
            if not math.isclose(view["yaw"], expected_yaw, abs_tol=1e-6):
                raise RuntimeError(f"{name}: exported yaw lost the final orbit offset")
            if view["orbit_degrees"] != 0 or view["transmission_source"] != source:
                raise RuntimeError(f"{name}: exported view changed its source or repeats motion")
            if presentation and view["presentation_time"] != 3:
                raise RuntimeError(f"{name}: exported view lost its presentation time")
            capture(args.binary.resolve(), replay, ["--frames", "1", "--config", str(saved)])
            original_bytes, replay_bytes = original.read_bytes(), replay.read_bytes()
            if original_bytes != replay_bytes:
                raise RuntimeError(f"{name}: replay differs from the final captured frame")
            cases.append({
                "name": name, "passed": True, "yaw": view["yaw"],
                "sha256": hashlib.sha256(original_bytes).hexdigest(),
            })
    receipt = {"passed": True, "cases": cases}
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
