#!/usr/bin/env python3
"""Bounded native qualification of shared mesh/SDF material workshops.

Run from any directory. Build the two applications first. Captures, profiles,
source identities and command/log receipts are kept outside tracked source.
GPU work is deliberately sequential.
"""
import argparse
import csv
import statistics
from collections import defaultdict
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
IDS = ("plain", "limestone", "marble", "clay", "ceramic", "bronze", "glass")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_timing_summary(output):
    summary = {}
    for path in sorted(output.glob("profile-*.passes.csv")):
        frames = defaultdict(dict)
        for row in csv.DictReader(path.open()):
            if row["kind"] == "gpu":
                frames[int(row["frame_index"])][row["label"]] = float(row["duration_ms"])
        samples = list(frames.values())
        keys = sorted({key for frame in samples for key in frame})
        passes = {}
        for key in keys:
            values = sorted(frame[key] for frame in samples if key in frame)
            passes[key] = dict(count=len(values), median_ms=statistics.median(values), p95_ms=values[int((len(values)-1)*.95)])
        totals = sorted(frame.get("material workshop whole scene", frame.get("sdf.whole_scene", sum(frame.values()))) for frame in samples)
        summary[path.name.removesuffix(".passes.csv")] = dict(passes=passes,
            median_ms=statistics.median(totals), p95_ms=totals[int((len(totals)-1)*.95)], samples=len(totals),
            whole_method="whole scene GPU scope" if any("whole" in key for key in keys) else "sum of trace and display GPU scopes")
    (output / "timings.json").write_text(json.dumps(summary, indent=2)+"\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "outputs/material-workshops/qualification")
    parser.add_argument("--stage", choices=("captures", "profiles", "films", "all"), default="all")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binaries = {r: ROOT / "build/release/projects" / ("pbr_materials" if r == "mesh" else "sdf") / ("pbr_materials" if r == "mesh" else "sdf") for r in ("mesh", "sdf")}
    manifest_path = output / "manifest.json"
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {"runs": {}}
    manifest.update(source_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        binary_sha256={r: sha(b) for r, b in binaries.items()},
        shader_sha256={r: {p.name: sha(p) for p in sorted((b.parent / "shaders").glob("*.spv"))} for r, b in binaries.items()},
        recipe_sha256={p.name: sha(p) for p in sorted((ROOT / "assets/materials").glob("*.json"))})

    def run(name, renderer, material, lighting, specimen="sphere", yaw=0, extra=(), suffix="png", dimensions=(960,720)):
        command = [str(binaries[renderer]), "--headless", "--require-validation", "--width", str(dimensions[0]), "--height", str(dimensions[1])]
        if renderer == "mesh":
            command += ["--material", material, "--lighting", lighting, "--specimen", specimen, "--yaw", str(yaw), "--pitch", ".15", "--distance", "1.05"]
        else:
            command += ["--workshop", specimen, "--material", material, "--lighting", "material-study" if lighting == "studio" else "material-atmosphere", "--yaw", str(yaw), "--pitch", ".15", "--distance", "1.05", "--supersample", "1"]
            if lighting == "clouds":
                command += ["--clouds"]
        artifact = output / f"{name}.{suffix}"
        command += ["--output", str(artifact), *extra]
        completed = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / f"{name}.log").write_text(completed.stdout)
        if completed.returncode or "vulkan validation error" in completed.stdout.lower():
            raise RuntimeError(f"{name} failed; inspect its native log")
        manifest["runs"][name] = dict(renderer=renderer, material=material, lighting=lighting,
            specimen=specimen, yaw=yaw, command=command, log=f"{name}.log",
            binary_sha256=manifest["binary_sha256"][renderer], shader_sha256=manifest["shader_sha256"][renderer],
            artifact=artifact.name if artifact.exists() else None,
            sha256=sha(artifact) if artifact.exists() else None)
        manifest_path.write_text(json.dumps(manifest, indent=2)+"\n")
        print(name, flush=True)

    if args.stage in ("captures", "all"):
        for lighting in ("studio", "clear", "clouds"):
            for material in IDS:
                for view, yaw in (("front",0),("oblique",.7)):
                    run(f"mesh-{material}-{lighting}-{view}","mesh",material,lighting,yaw=yaw)
                run(f"sdf-{material}-{lighting}-front","sdf",material,lighting)
        for renderer in ("mesh", "sdf"):
            for lighting in ("studio", "clouds"):
                for material in ("marble", "clay", "glass"):
                    run(f"{renderer}-block-{material}-{lighting}",renderer,material,lighting,"block",.55)
        for material in ("marble", "clay", "glass"):
            for lighting in ("studio", "clouds"):
                for view,yaw in (("front",0),("oblique",.7)):
                    run(f"sdf-csg-{material}-{lighting}-{view}","sdf",material,lighting,"csg",yaw)
        run("mesh-gallery","mesh","gallery","studio",dimensions=(1920,1080))
        for label,extra in (("cut",("--workshop-cut-material",)),("unblended",("--no-workshop-blend-materials",)),("no-bump",("--no-bump",)),("no-pattern",("--no-procedural",)),("geometric-normal",("--debug","1")),("shading-normal",("--debug","8"))):
            run(f"sdf-csg-{label}","sdf","marble","studio","csg",.4,extra)
        for name in ("geometric-normal", "normal", "roughness", "base-color"):
            run(f"mesh-clay-{name}","mesh","clay","studio",extra=("--debug-view",name))
    if args.stage in ("profiles", "all"):
        for renderer,material,specimen in (("mesh","gallery","sphere"),("mesh","glass","sphere"),("sdf","marble","csg"),("sdf","glass","csg")):
            for lighting in ("studio","clear","clouds"):
                name=f"profile-{renderer}-{material}-{lighting}"
                run(name,renderer,material,lighting,specimen,extra=("--headless-render-only","--frames","112","--profile-warmup-frames","16","--profile-output",str(output/name)),dimensions=(1920,1080))
        run("stress","mesh","clay","studio",extra=("--headless-render-only","--frames","32","--stress-edits","1000"))
        write_timing_summary(output)
    if args.stage in ("films", "all"):
        for material in ("bronze","glass"):
            for lighting in ("studio","clouds"):
                run(f"mesh-{material}-{lighting}-orbit","mesh",material,lighting,
                    extra=("--capture","video","--frames","240","--fps","30","--orbit-degrees","360"),suffix="mp4")
        run("sdf-csg-marble-clouds-orbit","sdf","marble","clouds","csg",extra=("--capture","video","--frames","240","--fps","30","--motion","orbit","--duration","8"),suffix="mp4")


if __name__ == "__main__":
    main()
