#!/usr/bin/env python3
"""Balanced legacy/candidate trials; existing offscreen runner, never a GUI."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import review_terrain_presentation_v1 as review
import run_offscreen_perf_v2 as perf


def plan():
    rows = []
    for site in ("mountain", "steep-ridges"):
        for repeat in range(2):
            order = ("legacy", "candidate") if repeat == 0 else ("candidate", "legacy")
            for variant in order:
                for mode in ("renderer-only", "live"):
                    rows.append(dict(site=site, repeat=repeat, variant=variant, mode=mode,
                        name=f"{site}-{repeat}-{variant}-{mode}"))
    return rows


def main():
    import run_live_perf_terrain_v1 as v1  # Existing optional NumPy environment; no installs.
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", type=Path, required=True)
    args = p.parse_args()
    out = args.out.resolve()
    if out.parent != (review.ROOT/"outputs/fluid").resolve() or not out.name.startswith("terrain-presentation-v1-"):
        p.error("use the owned terrain-presentation-v1-* review leaf")
    destination = out / "paired-performance"
    destination.mkdir()
    if review.digest(perf.demo.EXTENSION) != perf.demo.EXTENSION_SHA:
        raise ValueError("audited native extension changed")
    perf.v1 = v1
    v1.PROBE = review.APP.parent / "cubey_project_fluid_25d_external_probe"
    identity = review.runtime()
    review.write(destination/"protocol.json", {"runtime": identity, "plan": plan(),
        "width": 1920, "height": 1080, "render_fps": 60, "native_pace": 60,
        "live_measurement_s": 45, "held_measurement_s": 15,
        "warmup_s": 5, "live_prewarm_physical_s": 1800,
        "boundary": "headless Vulkan; no readback/encode; balanced order; not native GUI/scanout"})
    rows = []
    for task in plan():
        case, recording, _ = review.scene(task["site"])
        live = task["mode"] == "live"
        trial = argparse.Namespace(app=review.APP, host="offscreen", width=1920, height=1080,
            render_fps=60, camera="overview", wall=45 if live else 15,
            warmup=5, prewarm=1800 if live else 0, exercise_controls=False,
            surface="legacy" if task["variant"] == "legacy" else "correlated",
            shadow_projection="legacy" if task["variant"] == "legacy" else "scene",
            shadow_extent=2048, recording=recording)
        print("start " + task["name"], flush=True)
        result = perf.run_trial(case, destination/task["name"], trial, task["mode"], task["repeat"])
        rows.append({**task, "result": result})
        v1.write(destination/"results.json", {"runtime": identity, "trials": rows})
        if result["status"] != "completed" or review.runtime() != identity:
            raise RuntimeError("trial failed or frozen renderer changed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
