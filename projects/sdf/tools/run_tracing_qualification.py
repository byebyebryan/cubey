#!/usr/bin/env python3
"""Run one owned GPU query process at a time and preserve source/artifact receipts."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
from tracing_comparison import check


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/release/projects/sdf/sdf"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = Path("projects/sdf/comparison/fixtures.json")
    source = json.loads(manifest.read_text())
    receipt = dict(input_sha256=digest(manifest), binary_sha256=digest(args.binary),
                   source_head=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                   source_dirty=subprocess.check_output(["git", "status", "--porcelain"], text=True).splitlines(),
                   shader_sha256={p.name: digest(p) for p in sorted((args.binary.parent/"shaders").glob("*.spv"))}, complete=False, runs=[])
    matrix = [("analytic", "full", 16), ("sampled", "full", 16), ("hybrid", "full", 16)]
    matrix += [(mode, "pruned", axis) for axis in (4, 16, 64) for mode in ("analytic", "hybrid")]
    for mode, csg, axis in matrix:
        name = f"{mode}-{csg}-{axis}"
        result = args.output/f"{name}.json"
        command = [str(args.binary), "--headless", "--headless-render-only", "--require-validation",
                   "--frames", "1", "--width", "32", "--height", "32", "--surface-trace", mode,
                   "--csg-evaluation", csg, "--region-axis", str(axis), "--comparison-input", str(manifest),
                   "--comparison-output", str(result)]
        start = time.monotonic()
        run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
        (args.output/f"{name}.log").write_text(run.stdout)
        if run.returncode:
            raise RuntimeError(f"{name} failed; see its log")
        data = json.loads(result.read_text())
        score = check(source, data) if mode != "sampled" else dict(passed=True, scope="diagnostic baseline")
        row = dict(name=name, command=command, wall_seconds=time.monotonic()-start,
                   result_sha256=digest(result), score=score,
                   derived_probes=sum(c.get("derived_probe_count", 0) for c in data["cases"]),
                   derived_max_error=max(c.get("derived_probe_max_error", 0) for c in data["cases"]))
        receipt["runs"].append(row)
        (args.output/"qualification.json").write_text(json.dumps(receipt, indent=2)+"\n")
        print(json.dumps(dict(name=name, score=score, derived_probes=row["derived_probes"])), flush=True)
        if not score["passed"]:
            return 1
    receipt["complete"] = True
    (args.output/"qualification.json").write_text(json.dumps(receipt, indent=2)+"\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
