#!/usr/bin/env python3
"""Additional source-query lighting gates and serial matched GPU timings."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
from acceleration_loop import gpu_state, save
from benchmark_tracing import percentile
from investigate_guardian import ROOT, digest, invoke, metadata
from qualify_shader_selection import Study, route_flags, ROUTES


def edges(args):
    study = Study(args)
    common = ["--width", "320", "--height", "180", "--authored-scene",
              "projects/sdf/scenes/material-study/sphere-plain.json", *route_flags(ROUTES[-1])]
    for elevation, azimuth in ((0, 0), (0, 90), (.00001, 180), (-.00001, 270), (85, 0)):
        for shadows in (0, 1, 2):
            study.pair(f"sun-{elevation}-{azimuth}-shadows-{shadows}", common +
                       ["--sun-elevation", str(elevation), "--sun-azimuth", str(azimuth),
                        "--shadows", str(shadows)], 2)
    for label, flags in (("ao-ground-soft", ["--ao", "--ground", "--shadows", "2"]),
                         ("clay-ao-ground-soft", ["--clay", "--ao", "--ground", "--shadows", "2"]),
                         ("bump-disabled", ["--no-bump"]),
                         ("procedural-disabled", ["--no-procedural"])):
        study.pair(label, common + flags, 2)
    # Keep the admitted >=0.6 orbit distance. A clipped enclosing sphere lets
    # the actual camera start just outside/inside a domain face at that distance.
    enclosing = json.loads((ROOT / "projects/sdf/scenes/material-study/sphere-plain.json").read_text())
    enclosing["root"]["radius"] = .8
    enclosing.pop("geometry_hash", None)
    enclosing.pop("appearance_hash", None)
    path = args.output / "clipped-enclosing-sphere.json"
    save(path, enclosing)
    for label, z in (("near-surface", .4991), ("inside", .4989)):
        study.pair(label, common + ["--authored-scene", str(path), "--distance", ".6",
                    "--pitch", "0", "--yaw", str(math.acos(z / .6)), "--target-y", "0"], 2)
    study.receipt["complete"] = True
    save(args.output / "qualification.json", study.receipt)


def timing(args):
    q = json.loads((args.qualification / "qualification.json").read_text())
    if not q.get("complete") or q["binary_sha256"] != digest(args.binary):
        raise RuntimeError("timing requires this binary's completed qualification")
    receipt = dict(metadata(args.binary), runner_sha256=digest(Path(__file__)),
                   frames=180, warmup=30, modes=args.modes, runs=[])
    cases = [(scene, preset, view, offset, mode) for scene, preset in
             (("guardian", "sculpture-guardian-limestone"), ("robot", "survey-robot-fieldkit"))
             for view, offset in (("preset", 0), ("side", math.pi / 2), ("rear", math.pi))
             for mode in args.modes]
    for repeat in range(1, 4):
        for scene, preset, view, offset, mode in cases if repeat % 2 else reversed(cases):
            prefix = args.output / f"{scene}-{view}-{mode}-r{repeat}"
            config = json.loads((ROOT / f"projects/sdf/presets/{preset}.json").read_text())
            command = [str(args.binary), "--headless", "--headless-render-only", "--require-validation",
                       "--supersample", "1", "--config", f"projects/sdf/presets/{preset}.json",
                       "--width", "1920", "--height", "1080", "--surface-trace", "analytic",
                       "--csg-evaluation", "pruned", "--region-axis", "64",
                       "--acceleration-variant", "root-far", "--source-optimization", mode,
                       "--yaw", str(config["yaw"] + offset), "--frames", "180",
                       "--profile-warmup-frames", "30", "--profile-output", str(prefix)]
            before = gpu_state()
            wall = invoke(command, prefix, timeout=180)
            after = gpu_state()
            groups = {}
            for row in csv.DictReader(prefix.with_suffix(".passes.csv").open()):
                if row["kind"] == "gpu":
                    groups.setdefault(row["label"], []).append(float(row["duration_ms"]))
            if len(groups.get("sdf.whole_scene", [])) != 150:
                raise RuntimeError("incomplete GPU timestamp tail")
            metrics = list(csv.DictReader(prefix.with_suffix(".metrics.csv").open()))
            selection = {r["name"]: float(r["value"]) for r in metrics if r["category"] == "sdf.shader_selection"}
            receipt["runs"].append(dict(scene=scene, view=view, mode=mode, repeat=repeat, command=command,
                gpu_before=before, gpu_after=after, wall_seconds=wall, selection=selection,
                passes={k: dict(samples=len(v), median_ms=statistics.median(v), p95_ms=percentile(v, .95))
                        for k, v in groups.items()}))
            save(args.output / "timing.json", receipt)
            print(prefix.name, round(receipt["runs"][-1]["passes"]["sdf.whole_scene"]["p95_ms"], 4), flush=True)
    receipt["complete"] = True
    save(args.output / "timing.json", receipt)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("phase", choices=["edges", "timing"])
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--qualification", type=Path)
    p.add_argument("--modes", nargs="+", default=["reference", "shadow"])
    p.add_argument("--source-optimization", default="shadow")
    p.set_defaults(resume=False)
    p.add_argument("--binary", type=Path, default=ROOT / "build/release/projects/sdf/sdf")
    args = p.parse_args()
    args.binary = args.binary.resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    (edges if args.phase == "edges" else timing)(args)


if __name__ == "__main__":
    main()
