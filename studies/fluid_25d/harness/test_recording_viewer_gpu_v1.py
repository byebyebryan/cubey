"""Synthetic recorded-state GPU upload/immutability smoke, not a solver test."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path
import subprocess
import tempfile

from convert_synxflow_recording_v1 import convert_case
from test_convert_synxflow_recording_v1 import _make_case, _write_asc


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
            (1, "composite", ["--fluid25d-native-presentation", "motion"]),
            (2, "composite", ["--fluid25d-native-presentation", "readable"]),
            (1, "composite", ["--fluid25d-native-presentation", "readable",
                              "--fluid25d-native-water-sampling", "bilinear", "--fluid25d-motion-markers"]),
            *((1, "composite", ["--fluid25d-native-presentation", "readable",
                                  "--fluid25d-native-water-sampling", "bspline",
                                  "--fluid25d-native-surface-subdivision", str(level), "--fluid25d-motion-markers"])
              for level in (1,2,4)),
            (1, "flow-inspection", ["--fluid25d-native-water-sampling", "bspline"]),
            *((2, "composite", ["--fluid25d-native-water-sampling", "bilinear",
                                  "--fluid25d-native-water-debug", diagnostic])
              for diagnostic in ("solid", "unlit", "normals", "wireframe", "wet-mask", "no-occlusion", "contours")),
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
            if any(s in extra for s in ("bilinear", "bspline")) and "fluid_25d_bank_controls: PASS" not in result.stdout:
                return 1
            if not (root / f"state-{time}.png").is_file():
                return 1
        # Effective policy is independent of marker enablement. These exact
        # image equalities also exercise the water push-constant bit at runtime.
        for style, resolved in (("original", "on"), ("motion", "on"), ("readable", "off")):
            images = []
            for policy in ("auto", resolved):
                image = root / f"policy-{style}-{policy}.png"
                command = ["rtk", "proxy", str(args.target.resolve()), "--headless",
                           "--width", "96", "--height", "64", "--fluid25d-recording", str(manifest),
                           "--fluid25d-recording-time-seconds", "1", "--fluid25d-native-presentation", style,
                           "--fluid25d-native-surface-highlights", policy, "--fluid25d-motion-markers",
                           "--fluid25d-recording-gpu-validation", "--output", str(image)]
                result = subprocess.run(command, text=True, capture_output=True, timeout=30)
                print(json.dumps({"command": command, "exit_code": result.returncode}))
                print(result.stdout, end="")
                print(result.stderr, end="")
                if result.returncode or "fluid_25d_recording_upload: PASS" not in result.stdout:
                    return 1
                if "vulkan validation error" in (result.stdout + result.stderr).lower():
                    return 1
                images.append(image.read_bytes())
            if images[0] != images[1]:
                print(f"native highlights Auto is not {resolved} in {style}")
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
            for style, sampling in (("motion", "triangular"), ("readable", "triangular"),
                                    ("readable", "bilinear"), ("readable", "bspline")):
                prefix = root / f"{style}-{sampling}-profile"
                smooth_command = [*command, "--fluid25d-native-presentation", style,
                                  "--fluid25d-native-water-sampling", sampling,
                                  "--profile-output", str(prefix), "--profile-warmup-frames", "1"]
                smooth_command[smooth_command.index("--frames") + 1] = "13"
                smooth_command[smooth_command.index("--fluid25d-recording-frame-interval-seconds") + 1] = "0.25"
                smooth_command[smooth_command.index("--output") + 1] = str(root / f"{style}.mp4")
                smooth = subprocess.run(smooth_command, text=True, capture_output=True, timeout=30)
                print(json.dumps({"command": smooth_command, "exit_code": smooth.returncode}))
                print(smooth.stdout, end="")
                print(smooth.stderr, end="")
                if smooth.returncode or "PASS bit-exact bed/h/velocity at saved t=2 s" not in smooth.stdout:
                    return 1
                deltas = [float(value) for value in re.findall(r"visual_delta_s=([0-9.]+)", smooth.stdout)]
                if deltas != [0.0, *([0.25] * 8), *([0.0] * 4)]:
                    print(f"unexpected {style} visual clock: {deltas}")
                    return 1
                if "fluid_25d_native_cue_controls: PASS" not in smooth.stdout:
                    return 1
                if "vulkan validation error" in (smooth.stdout + smooth.stderr).lower():
                    return 1
                with prefix.with_suffix(".passes.csv").open(newline="") as source:
                    timings = [row for row in csv.DictReader(source)
                               if row["kind"] == "gpu" and
                               row["label"] == "fluid_25d native presentation total"]
                if sorted(int(row["frame_index"]) for row in timings) != list(range(1, 13)):
                    print(f"missing or duplicated {style} GPU timing samples: {timings}")
                    return 1
                if any(float(row["duration_ms"]) <= 0 for row in timings):
                    return 1
        # Zero-velocity saved fields: same image at two physical times even
        # with cues/markers enabled. This is renderer determinism, not a native
        # lake-at-rest solver test (the recording is synthetic).
        rest_case = _make_case(root / "stationary")
        for saved in (0, 1, 2):
            _write_asc(rest_case / f"native/output/h_{saved}.asc", [3.999, 2.998, 1.997, 0.996])
            for field in ("hUx", "hUy"):
                _write_asc(rest_case / f"native/output/{field}_{saved}.asc", [0.0] * 4)
        rest_manifest = convert_case(rest_case, root / "stationary-recording")
        for sampling, highlights in ((sampling, policy) for sampling in ("triangular", "bilinear", "bspline")
                                    for policy in ("auto", "on", "off")):
            images = []
            for saved in (0, 2):
                image = root / f"stationary-{sampling}-{highlights}-{saved}.png"
                command = ["rtk", "proxy", str(args.target.resolve()), "--headless", "--width", "96", "--height", "64",
                           "--fluid25d-recording", str(rest_manifest), "--fluid25d-recording-time-seconds", str(saved),
                           "--fluid25d-native-presentation", "readable", "--fluid25d-native-water-sampling", sampling,
                           "--fluid25d-native-surface-highlights", highlights,
                           "--fluid25d-motion-markers", "--fluid25d-recording-gpu-validation", "--output", str(image)]
                if sampling == "bspline":
                    command += ["--fluid25d-native-surface-subdivision", "4"]
                result = subprocess.run(command, text=True, capture_output=True, timeout=30)
                print(json.dumps({"command": command, "exit_code": result.returncode}))
                print(result.stdout, end="")
                print(result.stderr, end="")
                if result.returncode or "fluid_25d_recording_upload: PASS" not in result.stdout:
                    return 1
                if "vulkan validation error" in (result.stdout + result.stderr).lower():
                    return 1
                images.append(image.read_bytes())
            if images[0] != images[1]:
                print(f"stationary native fields must not animate {sampling} water material")
                return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
