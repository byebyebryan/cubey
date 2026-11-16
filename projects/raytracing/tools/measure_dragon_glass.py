# /// script
# dependencies = ["numpy==2.4.6", "pillow==12.0.0"]
# ///
"""Opt-in raw convergence captures of the certified dragon; no renderer tuning."""

import argparse
import hashlib
import json
import os
import subprocess
import time
from pathlib import Path

import numpy as np
from dragon_glass_acceptance import require_requested_memory, require_transport_status

PRESET = Path("projects/raytracing/presets/optics/dragon-glass.json")
VIEWS = {
    "canonical": [],
    "elevated": ["--pitch", ".65"],
    "grazing": ["--yaw", "1.35", "--pitch", ".1"],
}
STATUS_BITS = (1, 2, 4, 8, 16, 32, 64)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def capture(
    binary,
    directory,
    name,
    width,
    height,
    samples,
    seed,
    extra=(),
    mode="hdr",
    diagnostic_continue=False,
):
    directory.mkdir(parents=True, exist_ok=True)
    prefix = directory / name
    command = [
        str(binary.resolve()),
        "--config",
        str(PRESET),
        "--headless",
        "--frames",
        "1",
        "--require-validation",
        "--width",
        str(width),
        "--height",
        str(height),
        "--samples",
        str(samples),
        "--seed",
        str(seed),
        "--output",
        str(prefix) + ".png",
        "--metrics",
        str(prefix) + ".json",
        "--" + mode + "-output",
        str(prefix) + ".bin",
        *extra,
    ]
    receipt_path = Path(str(prefix) + ".receipt.json")
    identity = {
        "command": command,
        "binary_sha256": digest(binary),
        "preset_sha256": digest(PRESET),
        "model_sha256": digest(json.loads(PRESET.read_text())["model"]),
        "recipe_sha256": digest("assets/materials/glass.json"),
    }
    if receipt_path.exists():
        old = json.loads(receipt_path.read_text())
        assert old["identity"] == identity, f"{name}: stale capture identity"
        for suffix, expected in old["artifacts"].items():
            assert digest(str(prefix) + suffix) == expected, (
                f"{name}: changed artifact {suffix}"
            )
        if not diagnostic_continue:
            assert not old.get("transport_failure"), old.get("transport_failure")
        print("retained", name, flush=True)
        return old
    assert not any(
        Path(str(prefix) + s).exists() for s in [".png", ".json", ".bin", ".log"]
    ), name
    start = time.monotonic()
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        timeout=1800,
        env=dict(os.environ, VK_LAYER_VALIDATE_SYNC="1"),
    )
    wall = time.monotonic() - start
    log = result.stdout + result.stderr
    Path(str(prefix) + ".log").write_text(log)
    assert result.returncode == 0, (name, log)
    assert not any(
        t in log.lower()
        for t in ["vuid-", "hazard detected", "vulkan validation error"]
    ), log
    metrics = json.loads(Path(str(prefix) + ".json").read_text())
    assert metrics["history_samples"] == samples and metrics["history_epochs"] == 1
    assert metrics["as_builds"] == 3 and metrics["target_generations"] == 1
    assert metrics["active_triangles"] == 870558 and metrics["active_route"] == "dragon"
    assert metrics["config"]["throughput-cutoff"] == 0
    require_requested_memory(metrics)
    raw = np.fromfile(str(prefix) + ".bin", dtype="<f4")
    raw = (
        raw.reshape(height, width, {"optics": 16, "query": 4}[mode], 4)
        if mode != "hdr"
        else raw.reshape(height, width, 4)
    )
    assert np.isfinite(raw).all(), name
    counts = None
    transport_failure = None
    if mode != "query":
        color = raw[:, :, 10, :3] if mode == "optics" else raw[:, :, :3]
        assert (color >= 0).all(), name
        flags = (raw[:, :, 11, 3] if mode == "optics" else raw[:, :, 3]).astype(
            np.uint32
        )
        counts = {str(b): int(np.count_nonzero(flags & b)) for b in STATUS_BITS}
        try:
            require_transport_status(name, counts)
        except AssertionError as error:
            transport_failure = str(error)
    receipt = {
        "identity": identity,
        "width": width,
        "height": height,
        "samples": samples,
        "seed": seed,
        "mode": mode,
        "wall_seconds_including_setup": wall,
        "finite_output": True,
        "nonnegative_hdr": mode != "query",
        "validation_clean": True,
        "transport_status_checked": mode != "query",
        "transport_accepted": mode != "query" and transport_failure is None,
        "transport_failure": transport_failure,
        "transport_policy": "diagnostic only"
        if diagnostic_continue
        else "strict accepted capture",
        "status_or_pixels": counts,
        "budget": {
            k: metrics["config"][k]
            for k in ["max-events", "max-path-rays", "max-total-rays"]
        },
        "resident_requested_bytes": metrics["resident_project_requested_bytes"],
        "peak_requested_bytes": metrics["peak_simultaneous_project_requested_bytes"],
        "artifacts": {
            s: digest(str(prefix) + s) for s in [".png", ".json", ".bin", ".log"]
        },
    }
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
    print(name, f"{wall:.2f}s", counts, flush=True)
    if transport_failure and not diagnostic_continue:
        raise AssertionError(transport_failure)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--binary", type=Path, default=Path("build/dev/projects/raytracing/raytracing")
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--suite",
        choices=["pilot", "fullhd", "ablations", "deep", "extended"],
        required=True,
    )
    parser.add_argument("--views", nargs="+", choices=VIEWS, default=list(VIEWS))
    args = parser.parse_args()
    width, height = (1920, 1080) if args.suite == "fullhd" else (960, 540)
    calls = []
    for view in args.views:
        extra = VIEWS[view]
        if args.suite in ["pilot", "fullhd"]:
            pairs = [(s, 7) for s in [32, 128, 512, 2048]] + [(4096, 19), (4096, 43)]
            for samples, seed in pairs:
                calls.append(
                    capture(
                        args.binary,
                        args.output,
                        f"{view}-{samples}-s{seed}",
                        width,
                        height,
                        samples,
                        seed,
                        extra,
                    )
                )
            # Last-sample primary IDs are labels, not accumulated coverage.
            # Four-vector primary labels fit the existing 32 MiB query cap at
            # 960x540. Full-HD analysis uses this explicitly labeled proxy mask.
            calls.append(
                capture(
                    args.binary,
                    args.output,
                    f"{view}-primary-labels",
                    960,
                    540,
                    1,
                    7,
                    extra,
                    mode="query",
                )
            )
        elif args.suite == "ablations":
            for lighting in ["combined", "sky", "sun"]:
                for samples, seed in [(128, 7), (2048, 19), (2048, 43)]:
                    calls.append(
                        capture(
                            args.binary,
                            args.output,
                            f"{view}-{lighting}-{samples}-s{seed}",
                            width,
                            height,
                            samples,
                            seed,
                            extra + ["--workshop-lighting", lighting],
                        )
                    )
        elif args.suite == "extended":
            for seed in [19, 43]:
                calls.append(
                    capture(
                        args.binary,
                        args.output,
                        f"{view}-8192-s{seed}",
                        width,
                        height,
                        8192,
                        seed,
                        extra,
                    )
                )
        else:
            calls.append(
                capture(
                    args.binary,
                    args.output,
                    f"{view}-budget-default",
                    width,
                    height,
                    128,
                    7,
                    extra,
                )
            )
            calls.append(
                capture(
                    args.binary,
                    args.output,
                    f"{view}-budget-deep",
                    width,
                    height,
                    128,
                    7,
                    extra
                    + [
                        "--max-events",
                        "256",
                        "--max-path-rays",
                        "2048",
                        "--max-total-rays",
                        "8192",
                    ],
                )
            )
    (args.output / "suite.json").write_text(
        json.dumps({"suite": args.suite, "views": args.views, "calls": calls}, indent=2)
        + "\n"
    )


if __name__ == "__main__":
    main()
