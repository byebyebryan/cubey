#!/usr/bin/env python3
"""Decode ordinary MP4 output and verify frame identity, count, timing and duration."""
import argparse
from fractions import Fraction
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

COLORS = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255),
          (0, 255, 255), (255, 255, 0), (0, 0, 0)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--mode", choices=["direct", "inline", "threaded"], required=True)
    args = parser.parse_args()
    unavailable = 1 if os.environ.get("CUBEY_REQUIRE_CAPTURE_MEDIA_TESTS") == "1" else 77
    if not all(shutil.which(tool) for tool in ["ffmpeg", "ffprobe"]):
        print("capture media verification requires ffmpeg and ffprobe")
        return unavailable
    with tempfile.TemporaryDirectory(prefix="cubey-capture-media-") as temporary:
        result = subprocess.run([str(args.app), args.mode, temporary], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        print(result.stdout)
        if result.returncode == 77:
            return unavailable
        if result.returncode or "vulkan validation error" in result.stdout.lower():
            return 1
        for fps in [24, 60]:
            for count in range(1, 8):
                path = Path(temporary) / f"{count}-{fps}.mp4"
                metadata = json.loads(subprocess.check_output(
                    ["ffprobe", "-v", "error", "-select_streams", "v:0", "-count_frames",
                     "-show_streams", "-show_frames", "-of", "json", str(path)]))
                stream = metadata["streams"][0]
                raw = subprocess.check_output(
                    ["ffmpeg", "-v", "error", "-i", str(path), "-fps_mode", "passthrough",
                     "-f", "rawvideo", "-pix_fmt", "rgb24", "-"])
                size = 32 * 24 * 3
                assert len(raw) == count * size, (args.mode, count, fps, "decoded frame count")
                assert int(stream["nb_read_frames"]) == count
                assert Fraction(stream["avg_frame_rate"]) == fps
                assert abs(float(stream["duration"]) - count / fps) < 0.0002
                assert len(metadata["frames"]) == count
                for index, frame in enumerate(metadata["frames"]):
                    assert abs(float(frame["best_effort_timestamp_time"]) - index / fps) < 0.00002
                    pixels = raw[index * size:(index + 1) * size]
                    mean = [sum(pixels[c::3]) / (32 * 24) for c in range(3)]
                    identity = min(range(len(COLORS)), key=lambda color: sum(
                        (mean[c] - COLORS[color][c]) ** 2 for c in range(3)))
                    assert identity == index, (args.mode, count, fps, index, identity)
                print(f"PASS {args.mode}: {count} frames @ {fps}, decoded order/count/PTS/duration")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
