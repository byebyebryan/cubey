#!/usr/bin/env python3
"""Bounded content equivalence against the already-qualified full GPU source.

This is a differential content check, not an independent Guardian ray oracle.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
from tracing_comparison import H, EPS, dot, unit, add, mul


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--binary", type=Path, default=Path("build/release/projects/sdf/sdf"))
    args = p.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    cases = []
    for name, file in [("robot", "survey-robot-fieldkit.json"), ("guardian", "sculpture-guardian-limestone.json")]:
        scene = json.loads((Path("projects/sdf/scenes")/file).read_text())
        queries = []
        for yaw in (0, .8, 2.5):
            eye = [math.sin(yaw)*1.4, .42, math.cos(yaw)*1.4]
            for y in range(-2, 3):
                for x in range(-2, 3):
                    target = [x*.1, y*.15, 0]
                    queries.append(dict(kind="ray", origin=eye, direction=unit(add(target, mul(eye, -1)))))
        cases.append(dict(name=name, scene=scene, queries=queries))
    input_path = args.output/"input.json"
    input_path.write_text(json.dumps(dict(format="cubey.sdf.tracing-study", version=1, cases=cases), indent=2)+"\n")
    baseline = None; reports = []
    for mode, csg, axis in [("analytic", "full", 16), ("analytic", "pruned", 16), ("hybrid", "pruned", 16),
                             ("analytic", "pruned", 64), ("hybrid", "pruned", 64)]:
        name = f"{mode}-{csg}-{axis}"
        output = args.output/f"{name}.json"
        command = [str(args.binary), "--headless", "--headless-render-only", "--require-validation", "--frames", "1",
                   "--width", "32", "--height", "32", "--surface-trace", mode, "--csg-evaluation", csg,
                   "--region-axis", str(axis), "--comparison-input", str(input_path), "--comparison-output", str(output)]
        run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
        (args.output/f"{name}.log").write_text(run.stdout)
        if run.returncode:
            raise RuntimeError(f"{name} failed: {run.stdout[-1000:]}")
        result = json.loads(output.read_text())
        if baseline is None:
            baseline = result
        failures = []; regular = 0; grazing = 0; max_depth = 0; hits = 0
        for fixture, refcase, case in zip(cases, baseline["cases"], result["cases"]):
            for i, (q, ref, actual) in enumerate(zip(fixture["queries"], refcase["results"], case["results"])):
                if ref["status"] == 2 or actual["status"] == 2:
                    failures.append([case["name"], i, "exhausted"]); continue
                if ref["status"] != actual["status"]:
                    failures.append([case["name"], i, "classification"]); continue
                if ref["status"] != 1:
                    continue
                hits += 1
                incidence = abs(dot(ref["normal"], q["direction"]))
                err = abs(actual["t"]-ref["t"]); max_depth = max(max_depth, err)
                if incidence >= .25:
                    regular += 1
                    if err > 4*EPS:
                        failures.append([case["name"], i, "depth", err])
                else:
                    grazing += 1
                    # Declared incidence-scaled differential check; keep separate.
                    if err > 4*EPS/max(.01, incidence):
                        failures.append([case["name"], i, "grazing depth", err])
                if abs(actual["residual"]) > EPS+2e-6:
                    failures.append([case["name"], i, "residual", actual["residual"]])
        report = dict(name=name, regular_hits=regular, grazing_hits=grazing, hits=hits,
                      max_depth_difference=max_depth, failures=failures,
                      cases=[{k: v for k, v in c.items() if k != "results"} for c in result["cases"]])
        reports.append(report); print(json.dumps(report), flush=True)
        (args.output/"content-qualification.json").write_text(json.dumps(reports, indent=2)+"\n")
        if failures:
            raise RuntimeError(f"{name} content equivalence failed")


if __name__ == "__main__":
    main()
