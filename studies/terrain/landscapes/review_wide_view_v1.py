#!/usr/bin/env python3
"""Qualify one explicit lighting-only trial against frozen wide-view ablations."""
import argparse
import json
from pathlib import Path

import study_wide_view_v1 as study

base = study.review


def verify(out):
    protocol = json.loads((out / "protocol.json").read_bytes())
    study.verify(Path(protocol["baseline"]), runtime=False)
    if (protocol["runtime"] != base.render.runtime() or
        protocol["harness"] != base.render.digest(Path(__file__)) or
        protocol["inputs"] != base.identities()):
        raise ValueError("trial runtime/harness/inputs changed; retain evidence")
    for name, digest in protocol["baseline_images"].items():
        if base.render.digest(Path(protocol["baseline"]) / name) != digest:
            raise ValueError("baseline image changed")
    return protocol


def command(case, variant, stats, filtered=False):
    args = study.command(case, variant, stats)
    if filtered:
        args += ["--terrain-readability", "filtered-study"]
    return args


def crop_difference(a, b, crop, width=1280):
    left, top, right, bottom = crop
    x, y = base.render.pixels(a), base.render.pixels(b)
    if len(x) != len(y) or len(x) != width * 720 * 3:
        raise ValueError("unexpected capture dimensions")
    values = [abs(x[i]-y[i]) for row in range(top, bottom)
              for i in range((row*width+left)*3, (row*width+right)*3)]
    return {"crop_xyxy": crop, "mean_abs_8bit": sum(values)/len(values),
            "max_abs_8bit": max(values), "different_channels": sum(v != 0 for v in values)}


def capture(out):
    protocol = verify(out)
    baseline = Path(protocol["baseline"])
    stats = json.loads((baseline / "field-statistics.json").read_bytes())
    rows, parity = [], []
    cases = [(case, "default") for case in study.CASES]
    cases += [("wide64", "clay-no-shadow"), ("wide64", "hidden-no-shadow")]
    for case, variant in cases:
        for filtered in (False, True):
            name = case + "-" + variant + ("-filtered" if filtered else "-control")
            image = out / (name + ".png")
            receipt = base.checked(command(case, variant, stats, filtered) + ["--frames", "1",
                "--width", "1280", "--height", "720", "--terrain-water-audit", "--output", str(image)],
                out / (name + ".log"))
            if "terrain_water_capture_immutable: GPU bed/depth/decoded-velocity exact" not in (out / (name + ".log")).read_text():
                raise ValueError("immutable field audit missing")
            rows.append({"case": case, "variant": variant, "filtered": filtered,
                         "image": image.name, **receipt})
            if not filtered:
                diff = base.render.pixel_difference(baseline / (case + "-" + variant + ".png"), image)
                if diff["max_abs_8bit"]:
                    raise ValueError("default branch changed frozen picture: " + name)
                parity.append({"case": case, "variant": variant, "difference": diff})
            print("trial capture", name, flush=True)
    crops = []
    for case, variant in cases:
        a = out / (case + "-" + variant + "-control.png")
        b = out / (case + "-" + variant + "-filtered.png")
        crops.append({"case": case, "variant": variant,
            "whole": base.render.pixel_difference(a,b),
            "near": crop_difference(a,b,[320,540,960,720]),
            "far": crop_difference(a,b,[320,210,960,330])})
    base.write(out / "captures.json", {"rows": rows, "default_parity": parity, "crops": crops,
        "scope": "Lighting approximation only; crops are image coordinates, not perceptual quality scores"})
    verify(out)


def performance(out):
    protocol = verify(out)
    stats = json.loads((Path(protocol["baseline"]) / "field-statistics.json").read_bytes())
    rows = []
    for repeat in range(2):
        controls = [("local32", "default", False), ("local32", "default", True),
                    ("wide64", "default", False), ("wide64", "default", True),
                    ("wide64", "uniform", False)]
        if repeat:
            controls.reverse()
        for case, variant, filtered in controls:
            name = f"perf-{case}-{variant}-{'filtered' if filtered else 'control'}-{repeat}"
            prefix = out / name
            receipt = base.checked(command(case, variant, stats, filtered) + ["--headless-render-only",
                "--run-seconds", "12", "--render-fps", "60", "--width", "1920", "--height", "1080",
                "--no-validation", "--profile-output", str(prefix), "--profile-warmup-frames", "120",
                "--profile-sample-interval", "1"], prefix.with_suffix(".log"))
            timing = base.render.timings(prefix)
            rows.append({"case": case, "variant": variant, "filtered": filtered, "repeat": repeat,
                **receipt, **timing,
                "budget_pass": timing["whole_gpu_p95_ms"] <= 8 and timing["cpu_service_p95_ms"] <= 4})
            print("trial timing", name, timing["whole_gpu_p95_ms"], flush=True)
    base.write(out / "performance.json", {"rows": rows,
        "scope": "Two reversed-order 1080p hardware headless repeats; 120 warmup frames; no solver/readback/encode"})
    verify(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--phase", choices=("freeze", "capture", "performance"), required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if args.phase == "freeze":
        if not args.baseline:
            parser.error("freeze requires --baseline")
        baseline = args.baseline.resolve()
        study.verify(baseline, runtime=False)
        base.write(out / "protocol.json", {"baseline": str(baseline), "inputs": base.identities(),
            "runtime": base.render.runtime(), "harness": base.render.digest(Path(__file__)),
            "baseline_images": {p.name: base.render.digest(p) for p in sorted(baseline.glob("*.png"))},
            "scope": "One explicit filtered-study trial; defaults and immutable geometry/fields unchanged"})
    else:
        globals()[args.phase](out)


if __name__ == "__main__":
    main()
