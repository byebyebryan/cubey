#!/usr/bin/env python3
"""Bounded native PBR/optical captures, legacy oracle and virtual-window smoke."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--binary", type=Path, default=Path("build/release/projects/sdf/sdf"))
    p.add_argument("--only-glass", action="store_true")
    args = p.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    receipt = []
    base = [str(args.binary), "--headless", "--require-validation", "--frames", "1", "--supersample", "1"]
    for subject, preset in [("robot", "survey-robot-fieldkit"), ("guardian", "sculpture-guardian-limestone"),
                            ("glass", "material-study/sphere-glass")]:
        if args.only_glass and subject != "glass":
            continue
        for mode, csg in [("sampled", "full"), ("analytic", "pruned"), ("hybrid", "pruned")]:
            name = f"{subject}-{mode}"; image = args.output/f"{name}.png"
            extent = ["320", "180"] if subject == "guardian" else ["640", "360"]
            command = base+["--config", f"projects/sdf/presets/{preset}.json", "--width", extent[0], "--height", extent[1],
                            "--surface-trace", mode, "--csg-evaluation", csg, "--region-axis", "64", "--output", str(image)]
            run = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
            (args.output/f"{name}.log").write_text(run.stdout)
            if run.returncode:
                raise RuntimeError(f"{name}: {run.stdout[-1000:]}")
            receipt.append(dict(name=name, command=command, returncode=run.returncode,
                                image_sha256=hashlib.sha256(image.read_bytes()).hexdigest()))
            if subject == "glass":
                diagnostic = args.output/f"{name}-transport.png"
                command += ["--debug", "9", "--output", str(diagnostic)]
                run = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
                (args.output/f"{name}-transport.log").write_text(run.stdout)
                if run.returncode:
                    raise RuntimeError(f"{name} transport diagnostic failed")
                receipt.append(dict(name=name+"-transport", command=command, returncode=run.returncode,
                                    image_sha256=hashlib.sha256(diagnostic.read_bytes()).hexdigest()))
    for name, command in [
        ("legacy-field-batches",base+["--headless-render-only", "--scene", "box", "--width", "32", "--height", "32", "--validate-field", "--validate-batches"]),
        ("legacy-glass-oracle",base+["--headless-render-only", "--config", "projects/sdf/presets/material-study/sphere-glass.json", "--width", "32", "--height", "32", "--validate-field"]),
        ("native-window",["xvfb-run", "-a", "-s", "-screen 0 800x600x24", str(args.binary), "--require-validation",
                           "--run-seconds", "1", "--width", "640", "--height", "360", "--supersample", "1",
                           "--config", "projects/sdf/presets/survey-robot-fieldkit.json", "--surface-trace", "analytic",
                           "--csg-evaluation", "pruned", "--region-axis", "64"])
    ]:
        if args.only_glass:
            continue
        run = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        (args.output/f"{name}.log").write_text(run.stdout)
        receipt.append(dict(name=name, command=command, returncode=run.returncode))
        if run.returncode:
            raise RuntimeError(f"{name}: {run.stdout[-2000:]}")
    (args.output/"native.json").write_text(json.dumps(receipt, indent=2)+"\n")
    print(json.dumps(dict(checks=len(receipt), output=str(args.output))))


if __name__ == "__main__":
    main()
