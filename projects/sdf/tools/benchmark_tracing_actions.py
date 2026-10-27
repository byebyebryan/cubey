#!/usr/bin/env python3
"""Selected existing construction states, with backward/redo requests separated.

The action timer includes request compilation, cache creation, waiting and
publication. Expected-state compilation happens before that timer. Completion
is a serialized queue-idle observation of the first rendered frame.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from benchmark_tracing import percentile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, default=Path("build/release/projects/sdf/sdf"))
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--repeats", type=int, default=3)
    args = p.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    provenance = dict(binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                      source_head=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                      runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    scenes = {"robot": "survey-robot-fieldkit", "guardian": "sculpture-guardian-limestone"}
    inputs = {}
    for name, filename in scenes.items():
        source = json.loads((Path("projects/sdf/scenes")/(filename+".json")).read_text())
        total = len(source["history"])
        selected = [1, total//4, total//2, 3*total//4, total-1, total, total-1, total, total//2, 1]
        actions = [dict(label=f"{'backward' if i and n<selected[i-1] else 'forward'} prefix {n}/{total}",
                        scene=source, visible_scene=source, history_prefix=n, visible_history_prefix=n)
                   for i, n in enumerate(selected)]
        file = args.output/f"{name}-input.json"
        file.write_text(json.dumps(dict(format="cubey.sdf.action-study", version=1, actions=actions))+"\n")
        inputs[name] = file
    routes = [("sampled", "full", 16), ("analytic", "pruned", 16), ("hybrid", "pruned", 16),
              ("analytic", "pruned", 64), ("hybrid", "pruned", 64)]
    matrix = [(s, *r) for s in scenes for r in routes]
    summary = []
    for repeat in range(1, args.repeats+1):
        for scene, mode, csg, axis in (matrix if repeat % 2 else list(reversed(matrix))):
            name = f"{scene}-{mode}-{axis}-r{repeat}"
            receipt = args.output/f"{name}.json"
            command = [str(args.binary), "--config", f"projects/sdf/presets/{scenes[scene]}.json",
                       "--headless", "--headless-render-only", "--require-validation", "--frames", "10",
                       "--width", "320", "--height", "180", "--supersample", "1", "--surface-trace", mode,
                       "--csg-evaluation", csg, "--region-axis", str(axis), "--comparison-actions", str(inputs[scene]),
                       "--comparison-output", str(receipt)]
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
            (args.output/f"{name}.log").write_text(result.stdout)
            if result.returncode:
                raise RuntimeError(f"{name} failed: {result.stdout[-1500:]}")
            data = json.loads(receipt.read_text())
            if len(data["actions"]) != 10:
                raise RuntimeError("incomplete action receipt")
            values = [a["request_to_waited_gpu_completion_ms"] for a in data["actions"]]
            row = dict(name=name, scene=scene, mode=mode, csg=csg, region_axis=axis, repeat=repeat,
                       p95_ms=percentile(values, .95), max_ms=max(values), actions=data["actions"], command=command)
            row.update(provenance)
            summary.append(row)
            (args.output/"action-benchmarks.json").write_text(json.dumps(summary, indent=2)+"\n")
            print(json.dumps({k: v for k, v in row.items() if k not in ("actions", "command")}), flush=True)


if __name__ == "__main__":
    main()
