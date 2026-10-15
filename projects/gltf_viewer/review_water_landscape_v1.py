#!/usr/bin/env python3
"""Gated, saved-water glTF-view audition. Never generates terrain or runs a solver."""
import argparse
import array
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "projects/terrain"))
import review_captured_water_v1 as capture

OUT_IDS = ("mountain-local", "mountain-wide", "mountain-interior-wide", "valley-interior-wide")
CATALOG = ROOT / "projects/terrain/terrain_landscape_presets.json"
GLTF = ROOT / "build/release/projects/gltf_viewer/gltf_viewer"
ASSET = ROOT / "outputs/showcase/audition-2/assets/DamagedHelmet.glb"
HEADINGS = (-1.16, .25, 1.65)
PITCHES = (-.12, -.26)


def write(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def presets():
    rows = {}
    cache = ROOT / "cache/terrain"
    for p in json.loads(CATALOG.read_bytes())["presets"]:
        if p["id"] not in OUT_IDS:
            continue
        site_path = cache / p["site"]["path"]
        site = json.loads(site_path.read_bytes())
        binding = cache / p["water"]["path"]
        manifest = binding.parent / "water-capture.json"
        bed = array.array("f")
        bed.frombytes((manifest.parent / "bed.f32").read_bytes())
        if sys.byteorder != "little":
            bed.byteswap()
        rows[p["id"]] = {"site": str(site_path), "binding": str(binding),
            "capture": str(manifest), "focus_xz": site["center_xz_m"],
            "maximum_bed_m": max(bed), "focus_height_m": max(bed) + 200.0}
    if set(rows) != set(OUT_IDS):
        raise ValueError("four prepared wet landscapes are required")
    return rows


def identities():
    paths = {CATALOG, ASSET, ROOT / "docs/media/showcase/manifest.json",
        ROOT / "docs/media/showcase/gltf-terrain.mp4", ROOT / "docs/media/showcase/gltf-terrain.png"}
    for p in presets().values():
        for folder in (Path(p["site"]).parent, Path(p["capture"]).parent):
            paths.update(x for x in folder.rglob("*") if x.is_file())
    return {str(p.relative_to(ROOT)): digest(p) for p in sorted(paths)}


def runtime(app):
    return {"binary": digest(app), "shaders": {p.name: digest(p) for p in
            sorted((app.parent / "shaders").glob("*.spv"))}}


def run(args, log):
    receipt = capture.run(args, log)
    text = log.read_text()
    if "VUID-" in text or "Validation Error" in text or "NVIDIA GeForce RTX 5070 Ti" not in text:
        raise ValueError("hardware Vulkan/validation evidence missing: " + str(log))
    return receipt


def baseline_command():
    return ["rtk", "proxy", str(GLTF), "--headless", "--input", str(ASSET),
        "--pbr-environment-source", "atmosphere", "--time-of-day-mode", "solar",
        "--time-hours", "14", "--time-speed-hours-per-second", ".875", "--clouds",
        "--cloud-weather-preset", "broken-cumulus", "--cloud-quality", "full",
        "--terrain-heightfield", str(ROOT / "cache/terrain/sources/v1/default"),
        "--terrain-surface-detail", "filtered-detail", "--terrain-shadows",
        "--terrain-foreground-height", "200", "--capture-camera-distance-scale", ".70",
        "--exposure", "1", "--ibl-intensity", "1"]


def view_command(id, yaw, pitch, info, water=True):
    # Tiny orbit radius makes the pivot a foreground-proxy camera, not a
    # landscape overview. Terrain's 16m minimum and 8m near clip are disclosed;
    # actual glTF confirmation later uses the unmodified asset camera.
    x, z = info["focus_xz"]
    args = ["rtk", "proxy", str(capture.APP), "--headless", "--terrain-landscape", id,
        "--terrain-water-span", "16", "--terrain-water-focus-x", str(x),
        "--terrain-water-focus-z", str(z), "--terrain-water-focus-height", str(info["focus_height_m"]),
        "--terrain-water-yaw", str(yaw), "--terrain-water-pitch", str(pitch)]
    if not water:
        args.append("--no-terrain-water-visible")
    return args


def freeze(out):
    write(out / "baseline-freeze.json", {"git_head": subprocess.check_output(
        ["rtk", "proxy", "git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "terrain_runtime": runtime(capture.APP), "gltf_runtime": runtime(GLTF),
        "inputs": identities(), "sites": presets(), "camera": {"yaw_rad": list(HEADINGS),
        "pitch_rad": list(PITCHES), "vertical_fov_deg": 60, "proxy_orbit_distance_m": 16,
        "proxy_near_clip_m": 8, "foreground_clearance_over_maximum_bed_m": 200},
        "scope": "Four independent saved storm states. No terrain generation, solver, GUI or media promotion."})
    receipt = run(baseline_command() + ["--frames", "1", "--width", "1280", "--height", "720",
        "--output", str(out / "baseline.png")], out / "baseline.log")
    write(out / "baseline-run.json", receipt)


def audition(out):
    frozen = json.loads((out / "baseline-freeze.json").read_bytes())
    if frozen["inputs"] != identities() or frozen["terrain_runtime"] != runtime(capture.APP):
        raise ValueError("audition inputs/runtime changed")
    rows = []
    for id, info in frozen["sites"].items():
        for yi, yaw in enumerate(HEADINGS):
            for pi, pitch in enumerate(PITCHES):
                name = f"{id}-h{yi}-p{pi}"
                image = out / (name + ".png")
                receipt = run(view_command(id, yaw, pitch, info) + ["--frames", "1",
                    "--width", "1280", "--height", "720", "--terrain-water-audit",
                    "--output", str(image)], out / (name + ".log"))
                if "GPU bed/depth/decoded-velocity exact" not in (out / (name + ".log")).read_text():
                    raise ValueError("immutable field audit missing")
                rows.append({"id": id, "heading": yi, "pitch": pi, "image": image.name,
                    "yaw_rad": yaw, "pitch_rad": pitch, "sha256": digest(image), **receipt})
                print("audition", name, flush=True)
    if frozen["inputs"] != identities() or frozen["terrain_runtime"] != runtime(capture.APP):
        raise ValueError("inputs/runtime changed during audition")
    write(out / "audition.json", {"rows": rows,
        "scope": "Provisional background-only, glTF-like camera screen. Not actual model/depth integration."})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--phase", required=True, choices=("freeze", "audition"))
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_dir():
        raise ValueError("output directory must already exist")
    globals()[args.phase](out)


if __name__ == "__main__":
    main()
