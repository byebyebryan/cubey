#!/usr/bin/env python3
"""Serial fixed-view timing/capture packet for the first hardware RT pass."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def gpu_state():
    p = subprocess.run(["nvidia-smi", "--query-gpu=name,driver_version,utilization.gpu,utilization.encoder,memory.used,temperature.gpu",
                        "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=10)
    return {"exit": p.returncode, "csv": p.stdout.strip(), "error": p.stderr.strip()}


def quantile(values, fraction):
    values = sorted(values)
    position = (len(values) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(values) - 1)
    return values[lower] + (values[upper] - values[lower]) * (position - lower)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/dev/projects/raytracing/raytracing"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--model", type=Path, default=Path("cache/raytracing/stanford-dragon/dragon.glb"))
    parser.add_argument("--captures-only", action="store_true")
    args = parser.parse_args()
    args.binary, args.model, args.output = args.binary.resolve(), args.model.resolve(), args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    shaders = args.binary.parent / "shaders"
    source = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    diff = subprocess.check_output(["git", "diff", "HEAD", "--binary"])
    (args.output / "source.patch").write_bytes(diff)
    identity = {"source": source, "source_patch_sha256": hashlib.sha256(diff).hexdigest(),
                "binary_sha256": sha(args.binary), "model_sha256": sha(args.model),
                "shader_sha256": {p.name: sha(p) for p in sorted(shaders.glob("*.spv"))},
                "gpu": gpu_state(), "host_scope": "shared host; boundary observations do not prove continuous exclusivity"}
    (args.output / "identity.json").write_text(json.dumps(identity, indent=2) + "\n")
    rows = []
    for repetition in range(3 if not args.captures_only else 0):
        for view in ["hero", "opposite", "detail"]:
            for shadows in [True, False]:
                name = f"{view}-{'shadow' if shadows else 'unshadowed'}-{repetition}"
                metrics = args.output / (name + ".json")
                command = [str(args.binary), "--config", f"projects/raytracing/presets/{view}.json",
                           "--model", str(args.model), "--headless", "--headless-render-only", "--frames", "180",
                           "--no-validation", "--metrics", str(metrics)]
                if not shadows:
                    command += ["--no-shadows"]
                before, start = gpu_state(), time.monotonic()
                process = subprocess.run(command, capture_output=True, text=True, timeout=90)
                wall, after = time.monotonic() - start, gpu_state()
                (args.output / (name + ".log")).write_text(process.stdout + process.stderr)
                if process.returncode:
                    raise RuntimeError(name + ": " + process.stdout + process.stderr)
                data = json.loads(metrics.read_text())
                if data["dropped_gpu_samples"]:
                    raise RuntimeError("Incomplete GPU receipt")
                summaries = {}
                for label in ["raytracing.trace", "raytracing.whole_scene"]:
                    frames = [p for p in data["gpu_samples"] if p["pass"] == label and p["frame"] >= 30]
                    if len(frames) != 150 or len({p["frame"] for p in frames}) != 150:
                        raise RuntimeError(f"{name}: expected 150 unique completed samples for {label}")
                    values = [p["milliseconds"] for p in frames]
                    summaries[label] = {"p50_ms": quantile(values, .5), "p95_ms": quantile(values, .95), "samples": 150}
                rows.append({"name": name, "view": view, "shadows": shadows, "repetition": repetition,
                             "command": command, "process_wall_seconds": wall, "before": before, "after": after,
                             "gpu": summaries})
                (args.output / "timing.json").write_text(json.dumps(rows, indent=2) + "\n")
                print(name, summaries, flush=True)
    captures = []
    for view in ["hero", "opposite", "detail"]:
        for diagnostic in ["final", "geometric-normal", "shading-normal"]:
            name = view + "-" + diagnostic
            image = args.output / (name + ".png")
            command = [str(args.binary), "--config", f"projects/raytracing/presets/{view}.json", "--model", str(args.model),
                       "--headless", "--frames", "1", "--require-validation", "--view", diagnostic,
                       "--output", str(image), "--metrics", str(args.output / (name + "-capture.json"))]
            start = time.monotonic()
            process = subprocess.run(command, capture_output=True, text=True, timeout=60)
            (args.output / (name + "-capture.log")).write_text(process.stdout + process.stderr)
            log = (process.stdout + process.stderr).lower()
            if process.returncode or any(token in log for token in ["vulkan validation error", "hazard detected", "vuid-"]):
                raise RuntimeError(name + ": " + process.stdout + process.stderr)
            captures.append({"name": name, "command": command, "sha256": sha(image),
                             "process_to_completed_png_seconds": time.monotonic() - start})
    (args.output / "captures.json").write_text(json.dumps(captures, indent=2) + "\n")


if __name__ == "__main__":
    main()
