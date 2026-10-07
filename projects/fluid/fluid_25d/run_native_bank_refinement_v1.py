#!/usr/bin/env python3
"""Visual-first bank prototype. Immutable recordings; no hydraulic dispatches.

Heavy contour libraries belong only to the isolated bake environment. Cubey
loads hash-checked, recording-specific display masks, not these libraries.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

import run_native_presentation_v1 as ref
import run_native_bank_render_v1 as bank

ROOT = ref.ROOT
MODIFIED = {
    "projects/fluid/fluid_25d/CMakeLists.txt",
    "projects/fluid/fluid_25d/README.md",
    "projects/fluid/fluid_25d/fluid_25d_project_config.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_gpu_resources.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_gpu_resources.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_commands.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_presentation.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_recording_app.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_tests.cpp",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_water.frag",
}
VARIANTS = {"raw-mask": (0, 0.0), "light": (10, 0.1), "moderate": (20, 0.25),
            "strong": (20, 0.5)}


def checkpoint(out: Path):
    phase = out / "checkpoint"
    ref.reserve_directory(phase)
    paths = ref.git_output("ls-files", "--cached", "--others", "--exclude-standard",
                           "projects/fluid").splitlines()
    hashes = {}
    for name in paths:
        path = ROOT / name
        if path.is_file():
            hashes[name] = ref.sha256_file(path)
            if name in MODIFIED:
                target = phase / "prior-source" / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
    diff = ref.rtk_output("git", "diff", "--binary", "HEAD")
    if diff.returncode:
        raise RuntimeError("checkpoint diff failed")
    ref.write_text_exclusive(phase / "prior.patch", diff.stdout)
    ref.write_json_exclusive(phase / "identity.json", {
        "runtime": bank.identity(), "prior_fluid_files": hashes,
        "allowed_overlap": sorted(MODIFIED), "goal": "visual demo, not exact shoreline fidelity",
        "commits_pushes": "not authorized", "human_visual_acceptance": "deferred"})
    phase = out / "prior-captures"
    ref.reserve_directory(phase)
    assets = []
    for case, camera, stamp in (("rain-on", "runoff", 6000), ("rain-off", "collection", 14400)):
        asset = ref.still_asset(case, camera, stamp)
        asset.update(width=1920, height=1080)
        row = bank.capture(asset, phase, False, ["--fluid25d-native-surface-highlights", "off"])
        assets.append(row)
    ref.write_json_exclusive(phase / "manifest.json", {"assets": assets})


def display_levels(count=12):
    """Equal-opacity midpoint quadrature of the existing 2..50 mm fade."""
    import numpy as np
    low, high = np.zeros(count), np.ones(count)
    target = (np.arange(count) + 0.5) / count
    for _ in range(50):
        mid = (low + high) * 0.5
        value = mid * mid * (3 - 2 * mid)
        low = np.where(value < target, mid, low)
        high = np.where(value < target, high, mid)
    return 0.002 + 0.048 * (low + high) * 0.5


def raster_shape(shape, width, height, subdivision=4):
    """Rasterize continuously positioned polygons at 8x, filter to 4x nodes.

    The half-cell-pixel spatial filter is identical in the unsmoothed control
    and candidates. No smoothing is inferred merely from upsampling the mask.
    """
    import numpy as np
    from PIL import Image, ImageDraw
    from scipy.ndimage import uniform_filter
    factor = subdivision * 2
    image = Image.new("L", ((width - 1) * factor + 1, (height - 1) * factor + 1))
    draw = ImageDraw.Draw(image)
    polygons = [shape] if shape.geom_type == "Polygon" else list(shape.geoms)
    polygons = [p for p in polygons if not p.is_empty]
    # A wet island inside another polygon's hole must be drawn after the
    # enclosing ring, regardless of the union library's component ordering.
    polygons.sort(key=lambda p: p.exterior.envelope.area, reverse=True)
    for polygon in polygons:
        draw.polygon([(round(x*factor), round(y*factor)) for x,y in polygon.exterior.coords], fill=255)
        for ring in polygon.interiors:
            draw.polygon([(round(x*factor), round(y*factor)) for x,y in ring.coords], fill=0)
    values = np.asarray(image, dtype=np.float32) / 255.0
    # Node-aligned reduction: preserve domain coordinates instead of resize's
    # half-pixel shifts. The spatial support is 1/8 of a native cell each way.
    return uniform_filter(values, size=3, mode="nearest")[::2, ::2].copy()


def bake(out: Path, variants: list[str], cases: list[str], times: list[int], bake_leaf="bake"):
    import numpy as np
    import run_shoreline_boundary_study_v1 as study
    import review_shoreline_boundary_study_v1 as recorded
    start_all = time.perf_counter()
    if not bake_leaf.replace("-", "").isalnum():
        raise ValueError("invalid bake leaf")
    phase = out / bake_leaf
    phase.mkdir(exist_ok=True)
    levels = display_levels()
    tool_paths = tuple(Path(p) for p in (__file__, study.__file__, recorded.__file__))
    tool_hashes = {p.name: ref.sha256_file(p) for p in tool_paths}
    for case in cases:
        source = ref.INPUT_ROOT / "recordings" / case
        source_document = json.loads((source / "recording.json").read_text())
        leaves, docs = {}, {}
        for variant in variants:
            leaf = phase / f"{case}-{variant}"
            ref.reserve_directory(leaf)
            leaves[variant] = leaf
            docs[variant] = {"schema": "cubey.fluid25d.display_coverage.v1",
                "source_manifest_sha256": ref.sha256_file(source / "recording.json"),
                "grid": source_document["grid"], "subdivision": 4,
                "encoding": "float32-little-endian", "variant": variant,
                "levels_m": levels.tolist(), "weights": [1/len(levels)] * len(levels),
                "raster_support_cells": 0.125, "temporal_interpolation": False,
                "purpose": "display opacity only; not depth, volume or wetness", "frames": []}
            docs[variant]["bake_tools_sha256"] = tool_hashes
        for timestamp in times:
            h, identity = recorded.recorded(case, timestamp)
            masks = {name: np.zeros(((h.shape[0]-1)*4+1, (h.shape[1]-1)*4+1), np.float32)
                     for name in variants}
            timings = {name: {"extraction_ms": 0., "smoothing_ms": 0., "fill_raster_ms": 0.}
                       for name in variants}
            shapes_report = {name: [] for name in variants}
            for level in levels:
                start = time.perf_counter()
                raw = study.extract(h, float(level), study.METHODS[0])
                extraction_ms = (time.perf_counter()-start)*1000
                original = None
                for name in variants:
                    iterations, cap = VARIANTS[name]
                    start = time.perf_counter()
                    curves = study.constrained(raw, {"iterations": iterations, "distance_cells": cap}, .25)
                    timings[name]["extraction_ms"] += extraction_ms
                    timings[name]["smoothing_ms"] += (time.perf_counter()-start)*1000
                    start = time.perf_counter()
                    if not study.linework_simple(curves):
                        raise ValueError(f"self-intersecting display contour: {case}/{timestamp}/{name}")
                    shape = study.filled(curves, h, float(level), raw)
                    if not shape.is_valid:
                        raise ValueError("invalid filled display polygon")
                    if name == "raw-mask":
                        original = shape
                    masks[name] += raster_shape(shape, h.shape[1], h.shape[0]) / len(levels)
                    timings[name]["fill_raster_ms"] += (time.perf_counter()-start)*1000
                    shapes_report[name].append({"level_m": float(level), "area_cells2": shape.area,
                                                "components_holes": study.topology(shape),
                                                "raw_area_cells2": original.area if original is not None else None})
            for name, mask in masks.items():
                np.clip(mask, 0, 1, out=mask)
                leaf = leaves[name]
                path = leaf / f"coverage-{timestamp:05d}.f32"
                with path.open("xb") as f:
                    f.write(mask.astype("<f4").tobytes())
                docs[name]["frames"].append({"time_s": timestamp, "path": path.name,
                    "sha256": ref.sha256_file(path), "source_frame_sha256": identity["frame_sha256"],
                    "bytes": path.stat().st_size, "timings_ms": timings[name],
                    "band_measurements": shapes_report[name]})
            print(f"Baked {case} {timestamp}s: " + ", ".join(
                f"{name} {sum(timings[name].values()):.0f}ms" for name in variants), flush=True)
        if tool_hashes != {p.name: ref.sha256_file(p) for p in tool_paths}:
            raise RuntimeError("bake tools changed during execution; do not accept this bake")
        for name, document in docs.items():
            ref.write_json_exclusive(leaves[name] / "coverage.json", document)
    print(f"Bake total wall: {time.perf_counter()-start_all:.2f}s", flush=True)


def mode_args(out: Path, case: str, mode: str, bake_group="bake"):
    args = ["--fluid25d-native-surface-highlights", "off"]
    if mode == "bspline-2x":
        return args + ["--fluid25d-native-water-sampling", "bspline",
                       "--fluid25d-native-surface-subdivision", "2"]
    if mode != "reference":
        return args + ["--fluid25d-native-display-coverage",
                       str(out / bake_group / f"{case}-{mode}" / "coverage.json")]
    return args


def stills(out: Path, modes: list[str], capture_leaf="stills", bake_leaf="bake"):
    if not capture_leaf.replace("-", "").isalnum():
        raise ValueError("invalid capture leaf")
    phase = out / capture_leaf
    ref.reserve_directory(phase)
    initial = bank.identity()
    rows = []
    for mode in modes:
        leaf = phase / mode
        ref.reserve_directory(leaf)
        for case, camera, stamp in (("rain-on", "runoff", 6000), ("rain-off", "collection", 14400)):
            for angle, pitch in (("default", -0.92), ("above", -1.40), ("grazing", -0.40)):
                asset = ref.still_asset(case, camera, stamp)
                asset.update(width=1920, height=1080)
                asset["id"] += "-" + angle
                row = bank.capture(asset, leaf, False, mode_args(out, case, mode, bake_leaf) +
                                   ["--fluid25d-native-camera-pitch", str(pitch)])
                rows.append({**row, "mode": mode, "angle": angle,
                             "path": str((leaf / row["path"]).relative_to(out))})
        print(f"Captured 3D stills: {mode}", flush=True)
    after = bank.identity()
    ref.assert_same_runtime(initial, after, "refinement stills")
    parity = []
    prior = json.loads((out / "prior-captures/manifest.json").read_text())
    for old in prior["assets"] if "reference" in modes else []:
        current = next(row for row in rows if row["mode"] == "reference" and
                       row["id"] == old["id"] + "-default")
        if current["sha256"] != old["sha256"]:
            raise ValueError("unchanged default failed exact pre-edit image parity")
        parity.append({"id": old["id"], "sha256": old["sha256"], "pass": True})
    ref.write_json_exclusive(phase / "manifest.json", {
        "runtime_identity": initial, "assets": rows, "default_parity": parity,
        "scope": "actual Cubey filled water, prerecorded h/u, no hydraulic dispatches",
        "human_visual_acceptance": "deferred"})


def clips(out: Path, modes: list[str], bake_leaf="bake-motion"):
    phase = out / "clips"
    ref.reserve_directory(phase)
    before = bank.identity()
    rows = []
    for mode in modes:
        leaf = phase / mode
        ref.reserve_directory(leaf)
        for case, camera, start in (("rain-on", "runoff", 4800), ("rain-off", "collection", 7260)):
            asset = ref.video_asset("runoff" if case == "rain-on" else "recession", case, camera, start, 120, 10, 5)
            prefix = leaf / (case + "-profile")
            row = bank.capture(asset, leaf, True, mode_args(out, case, mode, bake_leaf) +
                ["--profile-output", str(prefix), "--profile-warmup-frames", "12"])
            row["profile"] = ref.profile_summary(prefix, 12, 108)
            row["path"] = str((leaf / row["path"]).relative_to(out))
            row["review_video"]["path"] = str((leaf / row["review_video"]["path"]).relative_to(out))
            row["mode"] = mode
            rows.append(row)
            print(f"Captured clip and matched GPU profile: {mode}/{case}", flush=True)
    ref.assert_same_runtime(before, bank.identity(), "refinement clips")
    ref.write_json_exclusive(phase / "manifest.json", {
        "runtime_identity": before, "assets": rows,
        "scope": "actual Cubey prerecorded h/u, 60s native save cadence, visual dots, no concurrent CUDA",
        "human_visual_acceptance": "deferred"})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("checkpoint", "bake", "stills", "clips"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--variants", nargs="+", choices=tuple(VARIANTS), default=["raw-mask", "light", "moderate"])
    parser.add_argument("--cases", nargs="+", choices=ref.CASES, default=list(ref.CASES))
    parser.add_argument("--times", type=int, nargs="+", default=[6000, 9600, 14400])
    parser.add_argument("--modes", nargs="+", choices=("reference", "bspline-2x", *VARIANTS),
                        default=["reference", "bspline-2x", "raw-mask", "light", "moderate"])
    parser.add_argument("--capture-leaf", default="stills")
    parser.add_argument("--bake-leaf", default="bake")
    args = parser.parse_args()
    out = args.out.resolve()
    if out.parent != ref.OUTPUT_ROOT or not out.name.startswith("bank-refinement-v1-") or args.out.is_symlink():
        raise ValueError("use a fresh bank-refinement-v1-* leaf under outputs/fluid")
    if args.phase == "checkpoint":
        checkpoint(out)
    elif args.phase == "bake":
        bake(out, args.variants, args.cases, sorted(set(args.times)), args.bake_leaf)
    elif args.phase == "stills":
        stills(out, args.modes, args.capture_leaf, args.bake_leaf)
    else:
        clips(out, args.modes, args.bake_leaf)


if __name__ == "__main__":
    main()
