#!/usr/bin/env python3
"""Bounded native timing ladder. One owned GPU job at a time; no captures/encoding."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
from sdf_paths import resolve_sdf_path
import statistics
import subprocess
import time


def percentile(values, q):
    a = sorted(values)
    index = (len(a)-1)*q
    lo = int(index)
    return a[lo] + (a[min(lo+1, len(a)-1)]-a[lo])*(index-lo)


def run(binary, output, scene, mode, csg, axis, extent, frames, warmup, repeat):
    name = f"{scene}-{mode}-{csg}-{axis}-{extent[0]}-r{repeat}"
    prefix = output/name
    preset = {"robot": "survey-robot-fieldkit", "guardian": "sculpture-guardian-limestone"}[scene]
    command = [str(binary), "--config", str(resolve_sdf_path(f"projects/sdf/presets/{preset}.json")), "--headless",
               "--headless-render-only", "--require-validation", "--frames", str(frames),
               "--width", str(extent[0]), "--height", str(extent[1]), "--supersample", "1",
               "--shadows", "1", "--surface-trace", mode, "--csg-evaluation", csg,
               "--region-axis", str(axis), "--profile-warmup-frames", str(warmup), "--profile-output", str(prefix)]
    start = time.monotonic()
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    prefix.with_suffix(".log").write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f"{name} failed: {result.stdout[-3000:]}")
    rows = list(csv.DictReader(prefix.with_suffix(".passes.csv").open()))
    gpu = [float(r["duration_ms"]) for r in rows if r["kind"] == "gpu" and r["label"] == "sdf.whole_scene"]
    # The profiler applies the configured warmup. Tail slots must be present.
    if len(gpu) != frames-warmup:
        raise RuntimeError(f"{name}: incomplete GPU samples {len(gpu)} != {frames-warmup}")
    metrics = list(csv.DictReader(prefix.with_suffix(".metrics.csv").open()))
    resources = {r["name"]: float(r["value"]) for r in metrics if r["category"] == "sdf.resources"}
    return dict(name=name, scene=scene, mode=mode, csg=csg, region_axis=axis, width=extent[0], height=extent[1],
                repeat=repeat, command=command, wall_seconds=time.monotonic()-start, gpu_samples=len(gpu),
                gpu_median_ms=statistics.median(gpu), gpu_p95_ms=percentile(gpu, .95), resources=resources)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, default=Path("build/release/projects/sdf/sdf"))
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--full", action="store_true")
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = dict(binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                    source_head=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                    runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), runs=[], skips=[])
    destination = args.output/"benchmarks.json"

    def save():
        destination.write_text(json.dumps(manifest, indent=2)+"\n")

    routes = [("sampled", "full", 16), ("analytic", "pruned", 16), ("hybrid", "pruned", 16),
              ("analytic", "pruned", 64), ("hybrid", "pruned", 64)]
    practical = []
    for scene in ("robot", "guardian"):
        for mode, csg, axis in routes:
            r = run(args.binary, args.output, scene, mode, csg, axis, (320, 180), 12, 2, 0)
            manifest["runs"].append(r); save(); print(json.dumps(r), flush=True)
            if r["gpu_p95_ms"] > 12:
                manifest["skips"].append(dict(scene=scene, mode=mode, csg=csg, axis=axis, reason="320x180 p95 exceeds 12 ms cost guard")); save(); continue
            if not args.full:
                continue
            r = run(args.binary, args.output, scene, mode, csg, axis, (960, 540), 18, 3, 0)
            manifest["runs"].append(r); save(); print(json.dumps(r), flush=True)
            if r["gpu_p95_ms"] > 50:
                manifest["skips"].append(dict(scene=scene, mode=mode, csg=csg, axis=axis, reason="960x540 p95 exceeds 50 ms cost guard")); save(); continue
            practical.append((scene, mode, csg, axis))
    # Repetition order changes each round, with no concurrent GPU benchmarks.
    for repeat in range(1, 4):
        order = practical if repeat % 2 else list(reversed(practical))
        for scene, mode, csg, axis in order:
            r = run(args.binary, args.output, scene, mode, csg, axis, (1920, 1080), 180, 30, repeat)
            manifest["runs"].append(r); save(); print(json.dumps(r), flush=True)


if __name__ == "__main__":
    main()
