#!/usr/bin/env python3
"""Matched wet-ground reconstruction evidence; frozen replay, no solver launch."""
from __future__ import annotations

import argparse
import html
import json
import shutil
import statistics
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image

import review_bank_edge_v1 as bank

ref, review = bank.ref, bank.review
CASES = (
    ("streams", "bspline-2x", "shaded", "macro"),
    ("streams", "bspline-2x", "terrain-only", "macro"),
    ("streams", "bspline-2x", "albedo", "macro"),
    ("streams", "bspline-2x", "roughness", "macro"),
    ("lake", "bspline-2x", "shaded", "macro"),
    ("wide", "bspline-2x", "shaded", "macro"),
    ("streams", "reference", "shaded", "macro"),
    ("lake", "reference", "shaded", "macro"),
    ("wide", "reference", "shaded", "macro"),
    ("streams", "reference", "shaded", "v1"),
    ("streams", "bspline-2x", "shaded", "v1"),
)


def capture(out, phase):
    dest = out / phase
    ref.reserve_directory(dest)
    runtime, inputs, sources = review.runtime_identity(ref.APP), review.inputs(), bank.sources()
    sources[str(Path(__file__).resolve().relative_to(ref.ROOT))] = ref.sha256_file(Path(__file__))
    for name in sources:
        target = dest / "source" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ref.ROOT / name, target)
    tuning = dest / "off.json"
    ref.write_json_exclusive(tuning, {"schema": "cubey.fluid25d.scenic-material.v2",
                                    "water_bank_irregularity_m": 0,
                                    "water_bank_motion_m": 0})
    assets, profiles = [], []
    for scene_name, mesh, view, material in CASES:
        scene = next(s for s in review.SCENES if s[0] == scene_name)
        label = "-".join((scene_name, mesh, view, material))
        path = dest / (label + ".png")
        cmd = bank.command(scene, path, tuning, bank=mesh)
        cmd += ["--fluid25d-scenic-terrain-view", view,
                "--fluid25d-scenic-material", material]
        completed, receipt = ref.run_logged(cmd, dest / "logs", label,
                                            expected_presentation="scenic")
        if "VUID-" in completed.stdout + completed.stderr:
            raise ValueError("Vulkan validation error")
        assets.append({"scene": scene_name, "mesh": mesh, "view": view,
                       "material": material, "path": path.name,
                       "sha256": ref.sha256_file(path), "receipt": receipt})
        print("Captured " + phase + "/" + label, flush=True)
    for batch in range(3):
        prefix = dest / ("profile-" + str(batch))
        cmd = bank.command(review.SCENES[0], prefix.with_suffix(".mp4"), tuning,
                           frames=120)
        cmd += ["--profile-output", str(prefix), "--profile-warmup-frames", "12"]
        _, receipt = ref.run_logged(cmd, dest / "logs", prefix.name,
                                   expected_presentation="scenic")
        profiles.append({"batch": batch, "summary": ref.profile_summary(prefix, 12, 108),
                         "receipt": receipt})
        print("Profiled " + phase + "/" + str(batch), flush=True)
    if runtime != review.runtime_identity(ref.APP) or inputs != review.inputs():
        raise ValueError("runtime or native inputs changed during capture")
    if any(ref.sha256_file(ref.ROOT / name) != digest for name, digest in sources.items()):
        raise ValueError("source changed during capture")
    ref.write_json_exclusive(dest / "manifest.json", {
        "runtime": runtime, "inputs": inputs, "sources": sources, "assets": assets,
        "profiles": profiles, "scope": "Held saved fields, bank noise off; zero hydraulics"})


def report(out):
    phases = {p: json.loads((out / p / "manifest.json").read_text()) for p in ("before", "after")}
    if phases["before"]["inputs"] != phases["after"]["inputs"]:
        raise ValueError("native input pins differ")
    if phases["after"]["inputs"] != review.inputs() or \
            phases["after"]["runtime"] != review.runtime_identity(ref.APP):
        raise ValueError("current runtime/native inputs differ from reviewed captures")
    for phase, data in phases.items():
        for name, digest in data["sources"].items():
            if ref.sha256_file(out / phase / "source" / name) != digest:
                raise ValueError("archived capture source changed")
        for row in data["profiles"]:
            for key in ("frames_csv", "passes_csv"):
                if ref.sha256_file(out / phase / row["summary"][key]) != row["summary"][key + "_sha256"]:
                    raise ValueError("profile timing CSV changed")
    before_runtime, after_runtime = (phases[p]["runtime"] for p in ("before", "after"))
    old_shaders, new_shaders = (r["compiled_shaders"]["files"] for r in (before_runtime, after_runtime))
    changed_shaders = sorted(k for k in old_shaders if old_shaders[k] != new_shaders.get(k))
    if before_runtime["executable_sha256"] != after_runtime["executable_sha256"] or \
            old_shaders.keys() != new_shaders.keys() or changed_shaders != [
                "fluid_25d_scenic_terrain.frag.spv", "fluid_25d_scenic_terrain_legacy.frag.spv"]:
        raise ValueError("change must be confined to the two terrain fragment shaders")
    tests = ET.parse(out / "tests.xml").getroot()
    if any(int(tests.get(k, "0")) for k in ("failures", "errors", "skipped", "disabled")):
        raise ValueError("regression failures or skipped GPU validation")
    if not int(tests.get("tests", "0")):
        raise ValueError("regression results must contain executed tests")
    rows, cards = [], []
    for before, after in zip(phases["before"]["assets"], phases["after"]["assets"], strict=True):
        keys = ("scene", "mesh", "view", "material", "path")
        if any(before[k] != after[k] for k in keys):
            raise ValueError("capture configuration mismatch")
        for phase, row in (("before", before), ("after", after)):
            if ref.sha256_file(out / phase / row["path"]) != row["sha256"]:
                raise ValueError("capture hash mismatch")
        same = before["sha256"] == after["sha256"]
        if before["mesh"] == "reference" and not same:
            raise ValueError("reference pixels changed")
        rows.append({**{k: before[k] for k in keys}, "pixel_exact": same})
        if before["mesh"] == "reference":
            continue
        images = []
        for phase in phases:
            source = out / phase / before["path"]
            with Image.open(source) as image:
                box = (400, 420, 820, 720) if before["scene"] == "streams" else \
                      (380, 405, 830, 670) if before["scene"] == "lake" else (0, 0, 1280, 720)
                crop = image.crop(box)
                crop.save(source.with_name(source.stem + "-crop.png"))
            images.append(f'<figure><figcaption>{phase.title()}</figcaption>'
                          f'<a href="{phase}/{source.name}"><img src="{phase}/{source.stem}-crop.png" '
                          f'alt="{phase} {html.escape(before["scene"])}"></a></figure>')
        title = " · ".join((before["scene"].title(), before["view"], before["material"]))
        cards.append(f'<section><h2>{html.escape(title)}</h2><div class="pair">'
                     + "".join(images) + '</div></section>')
    timing = {phase: {"median_ms": statistics.median(r["summary"]["gpu_median_ms"]
                                                    for r in data["profiles"]),
                      "runs": [r["summary"] for r in data["profiles"]]}
              for phase, data in phases.items()}
    change = 100 * (timing["after"]["median_ms"] / timing["before"]["median_ms"] - 1)
    ranges = {phase: (min(r["gpu_median_ms"] for r in data["runs"]),
                      max(r["gpu_median_ms"] for r in data["runs"]))
              for phase, data in timing.items()}
    timing_limitation = (
        "Sequential, non-isolated batches: a negative delta is not evidence "
        "of a speedup or an overhead guarantee")
    result = {"cases": rows, "reference_pixel_exact_cases": sum(r["mesh"] == "reference" for r in rows),
              "inputs_unchanged": True, "profiles": timing, "median_change_percent": change,
              "changed_compiled_shaders": changed_shaders, "regression_tests": int(tests.get("tests")),
              "report_generator_sha256": ref.sha256_file(Path(__file__)),
              "timing_limitation": timing_limitation,
              "scope": "Presentation-only B-spline terrain wetness; no solver or default changes"}
    ref.write_json_exclusive(out / "comparison.json", result)
    page = '''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Fluid 2.5D · B-spline wet-ground fix</title>
<style>body{max-width:1180px;margin:auto;padding:24px;background:#111820;color:#e0e7ed;font:16px/1.5 system-ui}
a{color:#9ddaff}.pair{display:grid;grid-template-columns:1fr 1fr;gap:12px}figure{margin:0}img{display:block;width:100%}
section{margin:28px 0}h1,h2{line-height:1.2}figcaption{padding:8px;background:#1b2631}
@media(max-width:650px){body{padding:12px}.pair{grid-template-columns:1fr}}</style>
<nav><a href="/">All reports</a></nav><h1>B-spline wet-ground fix</h1>
<p>The marked grey stair steps were a terrain-material mask, not the smoothed water bank.
Terrain wetness now reads the continuous cubic depth in B-spline mode; reference mode retains native triangle depth.
Colour and roughness use the same wetness weight. No new pass, texture, knob, simulation step or terrain modification.</p>
<p><strong>Read the first pair:</strong> look around the little pools on the left, not just at the main turquoise channel.
The angular grey patches should become continuous transitions. The colour-only and roughness views isolate that change.</p>
<p><strong>Verdict:</strong> keep this correction within B-spline mode. The marked wet-ground staircase is visibly reduced
without stronger noise, different geometry or a finer simulation. Small-pool mesh faceting and other bank tradeoffs remain;
this is not a universal shoreline fix. B-spline itself stays optional, as does bank noise.</p>
<p>Bank noise is off in every comparison. Crops show the same pixel rectangles, scaled by the browser;
click an image for the full capture. The existing 2× display mesh and its small-pool faceting remain unchanged.</p>'''
    page += "".join(cards)
    page += (f'<h2>Checks and cost</h2><p>{result["reference_pixel_exact_cases"]} reference captures are pixel-exact. '
             f'{result["regression_tests"]} focused CTests pass, including the analytic GPU suite '
             'and the synthetic optional-bank renderer controls. Only the two terrain fragment binaries changed; '
             'water shaders and every hydraulic shader remain byte-identical. '
             'Native recordings are unchanged; all captures validate GPU uploads and dispatch zero hydraulics.</p>'
             f'<p>1280×720 held-replay GPU median: {timing["before"]["median_ms"]:.3f} → '
             f'{timing["after"]["median_ms"]:.3f} ms ({change:+.1f}%). Three warmed runs per build, '
             '108 samples each. Sequential before/after batches, not alternating builds; desktop GPU activity '
             'and clocks are not isolated. Before run medians ranged '
             f'{ranges["before"][0]:.2f}–{ranges["before"][1]:.2f} ms; after '
             f'{ranges["after"][0]:.2f}–{ranges["after"][1]:.2f} ms. '
             '<strong>The negative delta is not evidence of a speedup or a reliable overhead guarantee.</strong> '
             'This is render cost, not live solver throughput.</p>'
             '<p><a href="comparison.json">Comparison and timing receipts</a> · '
             '<a href="before/manifest.json">Before input/source/runtime pins</a> · '
             '<a href="after/manifest.json">After input/source/runtime pins</a> · '
             '<a href="tests.xml">Regression checks</a></p>'
             '<p>An initial run found an outdated source-name assertion from the existing bank ablation alias. '
             'The guard now checks that alias, plus shared terrain wetness selection; no water renderer change was needed. '
             '<a href="tests-initial.xml">Retained initial test result</a>.</p>'
             '<p>These are diagnostic replay renders, not manual GUI acceptance or calibrated hydrology.</p></html>')
    ref.write_text_exclusive(out / "index.html", page)
    print(json.dumps({k: result[k] for k in ("reference_pixel_exact_cases", "regression_tests",
                                           "changed_compiled_shaders", "timing_limitation")}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("capture", "report"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--phase", choices=("before", "after"), default="after")
    args = parser.parse_args()
    out = args.out.resolve()
    if out.parent != ref.OUTPUT_ROOT or not out.name.startswith("bspline-wet-ground-"):
        parser.error("fresh bspline-wet-ground-* leaf under outputs/fluid required")
    if args.action == "capture":
        capture(out, args.phase)
    else:
        report(out)
