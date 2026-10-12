#!/usr/bin/env python3
"""Matched headless material/geometry controls and frozen 1080p repeats."""
import argparse
import html
import json
import math
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import review_captured_water_v1 as base
import review_terrain_world_v1 as previous
import review_wider_water_v1 as wide
import study_terrain_atlas_v1 as atlas
import study_terrain_world_v1 as study

OLD = study.ROOT / "outputs/terrain/world-sites-20261012-a1"
SITES = ("valley-interior", "mountain-interior", "mountain-original")


def command(name, span=32768, mode="current", view="shaded", cap=1024, shift=(0, 0)):
    if name == "water":
        pose = (*wide.CAMERAS["landscape"][:3], span, .25, -.4)
        cmd = wide.command(study.CAPTURE, pose=pose)+["--terrain-water-context",
            "--terrain-water-context-source", str(atlas.WORLD / "sites/mountain-original/heightfield.json")]
    else:
        cmd = previous.site_command(atlas.WORLD / "sites" / name / "site.json", span=span, shift=shift)
    return cmd+["--terrain-readability", mode, "--terrain-water-ground-view", view,
                "--terrain-context-max-spacing", str(cap)]


def still(out, label, cmd):
    folder = out / "images"; folder.mkdir(exist_ok=True)
    path = folder / (label+".png")
    receipt = base.run(cmd+["--frames", "1", "--width", "1280", "--height", "720",
        "--terrain-water-audit", "--output", str(path)], path.with_suffix(".log"))
    log = path.with_suffix(".log").read_text()
    if "hydraulic_dispatches=0" not in log or "NVIDIA GeForce RTX 5070 Ti" not in log:
        raise ValueError("missing immutable GPU/device evidence")
    return dict(label=label, path=str(path.relative_to(out)), sha256=study.digest(path), **receipt)


def controls(out):
    atlas.check(out)
    evidence = {"runtime": base.runtime(), "assets": [], "legacy_parity": []}
    for name in SITES:
        evidence["assets"].append(still(out, name+"-current-wide", command(name)))
        diff = base.pixel_difference(out/"images"/(name+"-current-wide.png"), OLD/"final"/(name+"-wide.png"))
        if diff["max_abs_8bit"]: raise ValueError("legacy terrain pixels changed")
        evidence["legacy_parity"].append(dict(case=name, **diff))
    for view in ("clay-clean", "albedo", "normal"):
        evidence["assets"].append(still(out, "valley-"+view, command("valley-interior", view=view)))
    evidence["assets"].append(still(out, "valley-detail-off", command("valley-interior", mode="detail-off")))
    evidence["assets"].append(still(out, "valley-clay-512", command("valley-interior", view="clay-clean", cap=512)))
    for camera in ("overview", "stream", "lake"):
        evidence["assets"].append(still(out, "legacy-"+camera, base.command(camera)))
        diff = base.pixel_difference(out/"images"/("legacy-"+camera+".png"), OLD/"final"/("legacy-"+camera+".png"))
        if diff["max_abs_8bit"]: raise ValueError("legacy water pixels changed")
        evidence["legacy_parity"].append(dict(case=camera, **diff))
    study.write(out / "controls.json", evidence)
    atlas.check(out)


def refined(out):
    atlas.check(out); before = json.loads((out/"controls.json").read_text())
    evidence = {"runtime": base.runtime(), "assets": [], "legacy_parity": []}
    for name in (*SITES, "water"):
        for span, label in ((32768, "wide"), (11457.6, "normal")):
            evidence["assets"].append(still(out, name+"-refined-"+label, command(name, span, "refined")))
        if name != "water":
            evidence["assets"].append(still(out, name+"-current-repeat", command(name)))
            diff = base.pixel_difference(out/"images"/(name+"-current-repeat.png"), out/"images"/(name+"-current-wide.png"))
            if diff["max_abs_8bit"]: raise ValueError("disabled candidate changed legacy output")
            evidence["legacy_parity"].append(dict(case=name, **diff))
    evidence["assets"].append(still(out, "valley-refined-albedo", command("valley-interior", mode="refined", view="albedo")))
    evidence["assets"].append(still(out, "water-current-normal", command("water", span=11457.6)))
    for i, shift in enumerate(((-1000, 0), (1000, 0))):
        evidence["assets"].append(still(out, "valley-refined-shift-"+str(i), command("valley-interior", mode="refined", shift=shift)))
    # Exercise target recreation, backdrop/patch transitions and water toggles
    # with the opted-in shared recipe. This is not native GUI acceptance.
    evidence["lifecycle"] = base.run(command("water", span=11457.6, mode="refined")+[
        "--headless-render-only", "--frames", "8", "--render-fps", "0", "--width", "640", "--height", "360",
        "--terrain-water-transition-smoke", "--terrain-water-audit"], out/"lifecycle.log")
    study.write(out/"refined.json", evidence)
    atlas.check(out)


def performance(out):
    evidence = json.loads((out/"refined.json").read_text())
    if evidence["runtime"] != base.runtime(): raise ValueError("frozen runtime required")
    folder = out/"performance"; folder.mkdir(exist_ok=False); rows=[]
    for name in ("valley-interior", "mountain-interior", "water"):
        for repeat in range(2):
            for mode in ("current", "refined"):
                prefix = folder/f"{name}-{repeat}-{mode}"
                receipt = base.run(command(name, mode=mode)+["--headless-render-only", "--run-seconds", "10",
                    "--render-fps", "60", "--width", "1920", "--height", "1080", "--no-validation",
                    "--profile-output", str(prefix), "--profile-warmup-frames", "120",
                    "--profile-sample-interval", "1"], prefix.with_suffix(".log"))
                rows.append(dict(case=name, repeat=repeat, mode=mode, **receipt, **base.timings(prefix)))
                study.write(out/"performance.json", {"rows": rows})
                print(f"measured {name} {repeat} {mode}", flush=True)
    deltas=[]
    for i in range(0, len(rows), 2):
        a,b=rows[i:i+2]
        deltas.append({"case":a["case"], "repeat":a["repeat"],
                       "added_gpu_p95_ms":b["whole_gpu_p95_ms"]-a["whole_gpu_p95_ms"]})
    study.write(out/"performance.json", {"rows":rows, "deltas":deltas,
        "budget_pass":all(r["whole_gpu_p95_ms"] <= 8 and r["cpu_service_p95_ms"] <= 4 for r in rows)
            and all(d["added_gpu_p95_ms"] <= .3 for d in deltas)})


def main():
    p=argparse.ArgumentParser(__doc__); p.add_argument("output", type=Path)
    p.add_argument("--phase", choices=("controls", "refined", "performance"), required=True)
    args=p.parse_args(); globals()[args.phase](atlas.owned(args.output))


if __name__ == "__main__": main()
