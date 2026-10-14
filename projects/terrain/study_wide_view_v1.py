#!/usr/bin/env python3
"""Matched immutable-field wide-view ablations; no GUI, solver or producer."""
import argparse
from array import array
import bisect
import json
import math
from pathlib import Path
import sys

import review_prepared_landscapes_v1 as review

ROOT = review.ROOT
CASES = {"local32": ("mountain-local", 11457.6), "wide32": ("mountain-local", 45000),
         "wide64": ("mountain-wide", 45000), "valley64": ("valley-interior-wide", 45000),
         "boundary32": ("mountain-local", 20000)}
VARIANTS = {
    "default": [], "hidden": ["--no-terrain-water-visible"],
    "clay": ["--terrain-water-ground-view", "clay-clean"],
    "clay-no-shadow": ["--terrain-water-ground-view", "clay-clean", "--terrain-water-shadow-strength", "0"],
    "no-shadow": ["--terrain-water-shadow-strength", "0"],
    "hidden-no-shadow": ["--no-terrain-water-visible", "--terrain-water-shadow-strength", "0"],
    "uniform": ["--terrain-water-detail", "uniform-control"],
    "uniform-clay": ["--terrain-water-detail", "uniform-control", "--terrain-water-ground-view", "clay-clean"],
    "normal": ["--terrain-water-ground-view", "normal"],
}
MATRIX = {
    "local32": ("default", "hidden", "clay", "no-shadow"),
    "wide32": ("default", "hidden", "clay", "clay-no-shadow", "no-shadow", "uniform", "uniform-clay"),
    "wide64": tuple(VARIANTS),
    "valley64": ("default", "hidden", "clay", "clay-no-shadow", "no-shadow"),
    "boundary32": ("default", "hidden", "clay", "no-shadow"),
}


def source(id):
    p = next(p for p in json.loads(review.CATALOG.read_bytes())["presets"] if p["id"] == id)
    site_path = ROOT / "cache/terrain" / p["site"]["path"]
    binding_path = ROOT / "cache/terrain" / p["water"]["path"]
    binding = json.loads(binding_path.read_bytes())
    capture_path = binding_path.parent / binding["capture"]["path"]
    return json.loads(site_path.read_bytes()), capture_path, json.loads(capture_path.read_bytes())


def verify(out, runtime=True):
    protocol = json.loads((out / "protocol.json").read_bytes())
    if protocol["inputs"] != review.identities() or protocol["harness"] != review.render.digest(Path(__file__)):
        raise ValueError("frozen inputs/harness changed; retain old evidence")
    if runtime and protocol["runtime"] != review.render.runtime():
        raise ValueError("baseline runtime changed; use a separate final protocol")
    return protocol


def statistics(out):
    verify(out)
    rows = []
    thresholds = [0, .0001, .002, .01, .05, .1, 1, 10]
    for id in sorted({v[0] for v in CASES.values()}):
        site, path, capture = source(id)
        g = capture["grid"]
        field = capture["fields"]["depth_m"]
        h = array('f')
        with (path.parent / field["path"]).open('rb') as stream:
            h.fromfile(stream, g["width"] * g["height"])
        if sys.byteorder != 'little':
            h.byteswap()
        if any(not math.isfinite(v) or v < 0 for v in h):
            raise ValueError("invalid immutable depth")
        counts = [0] * (len(thresholds) + 1)
        for value in h:
            counts[bisect.bisect_right(thresholds, value)] += 1
        width = g["width"]
        last = h[-width:]
        x = max(range(width), key=lambda x: last[x])
        origin = g["sample_origin_xz_m"]
        spacing = g["cell_size_m"]
        ordered = sorted(h)
        rows.append({"id": id, "grid": g, "time_s": capture["state"]["physical_time_s"],
            "rain_mm_per_hour": capture["state"]["rain_mm_per_hour"],
            "thresholds_m": thresholds, "bin_counts": counts,
            "minimum_m": ordered[0], "maximum_m": ordered[-1],
            "percentiles_m": {str(q): ordered[round(q * (len(h)-1))] for q in (.5, .9, .99)},
            "mean_depth_m": math.fsum(h) / len(h), "volume_m3": math.fsum(h) * spacing * spacing,
            "boundary_max_depth_m": last[x],
            "boundary_focus_xz_m": [origin[0] + min(max(x * spacing, 1024), (width-1)*spacing-1024),
                                      origin[1] + (g["height"]-1)*spacing - 512],
            "site_center_xz_m": site["center_xz_m"]})
        print("read depth", id, rows[-1]["percentiles_m"], "max", ordered[-1], flush=True)
    review.write(out / "field-statistics.json", {"rows": rows,
        "scope": "Native depth cells, not rendered area or a permanent-lake classification"})
    verify(out)


def command(case, variant, field_stats):
    id, span = CASES[case]
    stats = next(s for s in field_stats["rows"] if s["id"] == id)
    center = stats["site_center_xz_m"]
    focus = stats["boundary_focus_xz_m"] if case == "boundary32" else center
    return review.command(id) + ["--no-terrain-water-animate", "--terrain-water-span", str(span),
        "--terrain-water-focus-x", str(focus[0]), "--terrain-water-focus-z", str(focus[1]),
        "--terrain-water-detail-focus-x", str(center[0]), "--terrain-water-detail-focus-z", str(center[1]),
        "--terrain-water-yaw", "0.25", "--terrain-water-pitch", "-0.4",
        *VARIANTS[variant]]


def capture(out):
    verify(out)
    field_stats = json.loads((out / "field-statistics.json").read_bytes())
    rows = []
    for case, variants in MATRIX.items():
        for variant in variants:
            name = case + "-" + variant
            path = out / (name + ".png")
            receipt = review.checked(command(case, variant, field_stats) + ["--frames", "1",
                "--width", "1280", "--height", "720", "--terrain-water-audit", "--output", str(path)],
                out / (name + ".log"))
            text = (out / (name + ".log")).read_text()
            if "terrain_water_capture_immutable: GPU bed/depth/decoded-velocity exact" not in text:
                raise ValueError("missing immutable field audit")
            rows.append({"case": case, "variant": variant, "image": path.name, **receipt})
            print("captured", name, flush=True)
    # New optional controls must leave the approved no-tuning frame unchanged.
    old = ROOT / "outputs/terrain/landscape-first-use-qualified-20261013-1SCoIc/export/mountain-local-local.png"
    difference = review.render.pixel_difference(old, out / "local32-default.png")
    if difference["max_abs_8bit"]:
        raise ValueError("study plumbing changed accepted local frame")
    review.write(out / "captures.json", {"rows": rows, "local_reference_difference": difference})
    verify(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--phase", choices=("freeze", "statistics", "capture"), required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if args.phase == "freeze":
        review.prepare.prepare(review.CATALOG, ROOT / "cache/terrain")
        review.write(out / "protocol.json", {"runtime": review.render.runtime(),
            "inputs": review.identities(), "harness": review.render.digest(Path(__file__)),
            "matrix": MATRIX, "cases": CASES, "variants": VARIANTS,
            "scope": "Same-state headless ablations; no solver, producer, asset edits or default promotion"})
    else:
        globals()[args.phase](out)


if __name__ == "__main__":
    main()
