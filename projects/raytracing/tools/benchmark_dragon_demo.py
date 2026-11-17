"""Measure matched dragon GPU work on a deterministic camera sweep.

Rendering is unpaced and headless, without video encoding or validation. The
same 151 moving poses are measured after 30 warmup frames in each serial run.
Results are project GPU timestamps, not native presentation or desktop FPS.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

from dragon_demo import MODES, PRESETS, ROOT, VIEWS


def percentile(values, fraction):
    ordered = sorted(values)
    index = (len(ordered) - 1) * fraction
    low = int(index)
    high = min(low + 1, len(ordered) - 1)
    return ordered[low] + (ordered[high] - ordered[low]) * (index - low)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path,
                        default=ROOT / "build/dev/projects/raytracing/raytracing")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve()
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    results = {}
    for name, mode, samples in (("forward", "forward", None), ("rt4", "rt", 4),
                                 ("rt2", "rt", 2), ("sampled", "sampled", None)):
        preset, renderer, transport = MODES[mode]
        config = json.loads((PRESETS / preset).read_text())
        config.update(VIEWS["whole"])
        config.update({"renderer": renderer, "transport-mode": transport, "shadows": False,
                       "fov": 45, "yaw": .85, "orbit-speed": -.4, "controls": False,
                       "width": 1920, "height": 1080, "fps": 30, "frames": 181})
        if mode == "forward":
            config["forward-transmission-source"] = "hybrid"
        if samples:
            config["demo-samples"] = samples
        config_path = (args.output / (name + "-config.json")).resolve()
        metrics_path = (args.output / (name + ".json")).resolve()
        config_path.write_text(json.dumps(config, indent=2) + "\n")
        command = [str(binary), "--config", str(config_path), "--headless",
                   "--headless-render-only", "--no-validation", "--metrics", str(metrics_path)]
        process = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True)
        (args.output / (name + ".log")).write_text(process.stdout)
        assert process.returncode == 0, process.stdout
        metrics = json.loads(metrics_path.read_text())
        label = "forward.total_frame" if mode == "forward" else "raytracing.total_frame"
        times = [r["milliseconds"] for r in metrics["gpu_samples"]
                 if r["pass"] == label and 30 <= r["frame"] <= 180]
        assert len(times) == 151 and min(times) > 0, (name, len(times))
        if mode == "forward":
            assert not metrics["ray_tracing_enabled"] and metrics["as_bytes"] == 0
        else:
            frames = metrics["sampling_frames"]
            assert len(frames) == 181
            expected = samples if mode == "rt" else 1
            assert all(f["new_samples"] == expected for f in frames), (name, frames[:2])
            assert all(f["samples_before"] == 0 for f in frames), name
        results[name] = {"command": command, "gpu_pass": label, "measured_frames": len(times),
                         "device": metrics.get("device"),
                         "samples_per_moving_frame": samples if mode == "rt" else 1,
                         "sample_scope": "per-frame" if mode != "sampled" else "progressive; moving view resets history",
                         "median_gpu_ms": statistics.median(times), "p95_gpu_ms": percentile(times, .95),
                         "max_gpu_ms": max(times), "renderer": renderer,
                         "as_bytes": metrics["as_bytes"]}
        print(name, json.dumps({k: results[name][k] for k in
                               ("median_gpu_ms", "p95_gpu_ms", "samples_per_moving_frame")} ), flush=True)
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == binary_hash
    receipt = {"passed": True, "binary_sha256": binary_hash,
               "extent": [1920, 1080], "validation": False, "video_recording": False,
               "measurement": "Unpaced headless project GPU timestamps; no desktop FPS claim",
               "camera": {"measured_frames": [30, 180], "yaw": [.45, -1.55],
                          "pitch": .35, "distance": 1.75, "target_y": .32}, "routes": results}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
