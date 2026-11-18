#!/usr/bin/env python3
"""Check the preceding cloud sample recorded by the native two-slot host."""

import csv
from pathlib import Path
import subprocess
import sys
import tempfile


def record(binary, directory, submitted):
    prefix = directory / ("submitted" if submitted else "legacy")
    command = [
        binary, "--headless", "--headless-render-only", "--require-validation",
        "--frames", "12", "--width", "128", "--height", "72", "--clouds",
        "--cloud-temporal", "--cloud-view-samples", "2", "--cloud-view-sample-mode",
        "temporal-phased", "--cloud-wind-speed-mps", "0", "--pause-time",
        "--profile-output", str(prefix),
    ]
    if submitted:
        command.append("--cloud-submitted-history")
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, check=False, timeout=60)
    if result.returncode or any(f"vulkan validation {level}:" in result.stdout
                                for level in ("error", "warning")):
        raise RuntimeError(result.stdout)
    rows = {}
    with prefix.with_suffix(".metrics.csv").open() as metrics:
        for row in csv.DictReader(metrics):
            if row["category"] == "cloud.temporal":
                rows.setdefault(int(row["frame_index"]), {})[row["name"]] = int(float(row["value"]))
    if set(rows) != set(range(12)):
        raise RuntimeError(f"Expected all 12 frames in {prefix.name}: {rows}")
    gap = 1 if submitted else 2
    for frame, row in rows.items():
        expected = {"slot": frame % 2, "sample_frame": frame,
                    "read_sample_frame": frame - gap if frame >= gap else -1}
        if row != expected:
            raise RuntimeError(f"{prefix.name}, frame {frame}: got {row}, expected {expected}")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="cubey-cloud-history-") as temporary:
        for submitted_history in (False, True):
            record(sys.argv[1], Path(temporary), submitted_history)
    print("Cloud history follows accepted views and mixes both phases with two GPU slots")
