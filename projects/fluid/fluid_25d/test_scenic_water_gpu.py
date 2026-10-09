#!/usr/bin/env python3
"""GPU film and depth-limited absorption controls for the Scenic water shader."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile
from unittest.mock import patch

import run_native_shoreline_raster_v1 as fixtures
import review_scenic_water as review
from test_display_coverage_gpu_v1 import make_sidecar


def runtime_identity(target):
    """Pin the requested build without depending on archived rain recordings."""
    if not target.is_file() or target.is_symlink():
        raise ValueError("GPU control executable is missing or a symlink: " + str(target))
    with patch.object(review.ref, "SHADER_ROOT", target.parent / "shaders"):
        shaders = review.ref.shader_identity()
    return {"executable": str(target.relative_to(review.ref.ROOT))
            if target.is_relative_to(review.ref.ROOT) else str(target),
            "executable_sha256": review.ref.sha256_file(target),
            "compiled_shaders": shaders}


def run(out, target):
    review.ref.reserve_directory(out)
    pins, runtime = review.sources(), runtime_identity(target)
    binary_pin, shader_pin = runtime["executable_sha256"], runtime["compiled_shaders"]
    rows = []
    # Set base roughness to .2 for both controls: film_roughness=.2 then never
    # raises the no-film control, including normal-variance roughness.
    common = out / "controlled-base.json"
    review.ref.write_json_exclusive(common, {
        "schema": "cubey.fluid25d.scenic-material.v2", "water_roughness": .2})
    disabled = out / "disabled-with-base.json"
    review.ref.write_json_exclusive(disabled, {
        "schema": "cubey.fluid25d.scenic-material.v2", "water_roughness": .2,
        "film_roughness": .2, "film_ground_mix": 0})
    optics = {}
    for mode, boost in (("optics-base", 0), ("optics-boost", 3)):
        optics[mode] = out / (mode + ".json")
        review.ref.write_json_exclusive(optics[mode], {
            "schema": "cubey.fluid25d.scenic-material.v2",
            "water_extinction_scale": 1, "water_shallow_extinction_boost": boost,
            "water_shallow_extinction_end_m": 16, "water_clarity": 0,
            "water_ripple_strength": 0})

    def render(case, manifest, control="default", view="shaded", extra=(), t=0):
        if runtime != runtime_identity(target):
            raise ValueError("runtime changed during GPU controls")
        label = f"{case}-{control}-{view}-{t}"
        image = out / (label + ".png")
        command = ["rtk", "proxy", str(target.resolve()), "--headless", "--width", "384", "--height", "288",
                   "--fluid25d-recording", str(manifest), "--fluid25d-recording-time-seconds", str(t),
                   "--fluid25d-recording-gpu-validation", "--fluid25d-native-presentation", "scenic",
                   "--fluid25d-scenic-material", "macro", "--fluid25d-scenic-water-view", view,
                   "--output", str(image), *extra]
        if control == "disabled":
            command += ["--fluid25d-scenic-tuning", str(disabled)]
        elif control == "controlled":
            command += ["--fluid25d-scenic-tuning", str(common)]
        elif control in optics:
            command += ["--fluid25d-scenic-tuning", str(optics[control])]
        result = subprocess.run(command, text=True, capture_output=True, timeout=90)
        review.ref.write_text_exclusive(out / (label + ".log"), result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        review.ref.verify_capture_log(result.stdout + result.stderr, True)
        if runtime != runtime_identity(target):
            raise ValueError("runtime changed during GPU capture")
        rows.append({"case": case, "control": control, "view": view, "time_s": t,
                     "path": image.name, "sha256": review.ref.sha256_file(image),
                     "upload_validation": "PASS", "hydraulic_dispatches": 0})
        return image

    exact_depth, exact_coverage, exact_calm, changed = 0, 0, 0, 0
    for case, depth in (("dry", 0), ("film", .006), ("transition", .03), ("deep", .25),
                        ("thin-stream", .02), ("sloped-film", .02)):
        bed = [0.0] * (fixtures.COLS * fixtures.ROWS)
        h = [depth] * len(bed)
        if case == "thin-stream":
            h = [depth if i % fixtures.COLS == 10 and 3 <= i // fixtures.COLS <= 16 else 0 for i in range(len(h))]
        if case == "sloped-film":
            bed = [.3 * (i % fixtures.COLS) + .15 * (i // fixtures.COLS) for i in range(len(bed))]
        with patch.object(fixtures, "fields", return_value=(bed, h)):
            manifest = fixtures.fixture(out / "fixtures" / case, "fully-wet-lake")
        before = {str(p): review.ref.sha256_file(p) for p in manifest.parent.rglob("*") if p.is_file()}
        normal = render(case, manifest)
        calm = render(case, manifest, t=2)
        if review.ref.sha256_file(normal) != review.ref.sha256_file(calm):
            raise ValueError("identical calm fields animate: " + case)
        exact_calm += 1
        images = [render(case, manifest, c) for c in ("controlled", "disabled")]
        same = review.ref.sha256_file(images[0]) == review.ref.sha256_file(images[1])
        if case in ("dry", "deep"):
            if not same:
                raise ValueError("dry/deep film control changed pixels: " + case)
            exact_depth += 1
        else:
            if same:
                raise ValueError("shallow treatment has no effect: " + case)
            changed += 1
        coverage = [render(case, manifest, c, "coverage") for c in ("default", "disabled")]
        if review.ref.sha256_file(coverage[0]) != review.ref.sha256_file(coverage[1]):
            raise ValueError("material changed coverage: " + case)
        exact_coverage += 1
        if case == "thin-stream":
            terrain = render(case, manifest, extra=("--fluid25d-scenic-terrain-view", "terrain-only"), t=1)
            if review.ref.sha256_file(normal) == review.ref.sha256_file(terrain):
                raise ValueError("thin stream disappeared")
        if case == "transition":
            sidecar, _ = make_sidecar(out / "masks", manifest)
            for bank, extra in (("bspline", ("--fluid25d-native-bank-view", "bspline-2x")),
                                ("marching-squares", ("--fluid25d-native-bank-view", "marching-squares",
                                                      "--fluid25d-native-display-coverage", str(sidecar)))):
                masks = [render(case + "-" + bank, manifest, c, "coverage", extra) for c in ("default", "disabled")]
                if review.ref.sha256_file(masks[0]) != review.ref.sha256_file(masks[1]):
                    raise ValueError("alternate bank coverage changed: " + bank)
                exact_coverage += 1
        after = {str(p): review.ref.sha256_file(p) for p in manifest.parent.rglob("*") if p.is_file()}
        if before != after:
            raise ValueError("native fixture mutated")
        print("GPU controls passed: " + case, flush=True)
    optics_summary = {"dry_or_deep_exact_cases": 0, "effective_shallow_cases": 0,
                      "coverage_exact_cases": 0, "reflection_exact_cases": 0,
                      "direct_exact_cases": 0, "calm_exact_cases": 0}
    for case, depth in (("optics-dry", 0), ("optics-shallow", .36),
                        ("optics-at-end", 16), ("optics-deep", 30)):
        bed = [0.] * (fixtures.COLS * fixtures.ROWS)
        h = [depth] * len(bed)
        with patch.object(fixtures, "fields", return_value=(bed, h)):
            manifest = fixtures.fixture(out / "fixtures" / case, "fully-wet-lake")
        before = {str(p): review.ref.sha256_file(p) for p in manifest.parent.rglob("*") if p.is_file()}
        images = [render(case, manifest, mode) for mode in optics]
        same = review.ref.sha256_file(images[0]) == review.ref.sha256_file(images[1])
        if depth == 0 or depth >= 16:
            if not same:
                raise ValueError("absorption boost changed dry/deep water: " + case)
            optics_summary["dry_or_deep_exact_cases"] += 1
        else:
            if same:
                raise ValueError("absorption boost has no shallow-water effect")
            optics_summary["effective_shallow_cases"] += 1
            for view, counter in (("coverage", "coverage_exact_cases"),
                                  ("environment-only", "reflection_exact_cases"),
                                  ("direct-only", "direct_exact_cases")):
                controls = [render(case, manifest, mode, view) for mode in optics]
                if review.ref.sha256_file(controls[0]) != review.ref.sha256_file(controls[1]):
                    raise ValueError("absorption boost changed " + view)
                optics_summary[counter] += 1
            calm = render(case, manifest, "optics-boost", t=2)
            if review.ref.sha256_file(calm) != review.ref.sha256_file(images[1]):
                raise ValueError("absorption boost animates calm/no-wind fields")
            optics_summary["calm_exact_cases"] += 1
        after = {str(p): review.ref.sha256_file(p) for p in manifest.parent.rglob("*") if p.is_file()}
        if before != after:
            raise ValueError("absorption fixture mutated")
        print("GPU absorption controls passed: " + case, flush=True)
    if pins != review.sources():
        raise ValueError("GPU control sources changed")
    if runtime != runtime_identity(target):
        raise ValueError("runtime changed during GPU controls")
    review.ref.write_json_exclusive(out / "result.json", {
        "status": "PASS", "source_files": pins, "runtime_identity": runtime,
        "executable_sha256": binary_pin, "compiled_shaders": shader_pin,
        "rows": rows, "summary": {"render_count": len(rows), "unchanged_coverage_cases": exact_coverage,
        "dry_and_deep_exact_cases": exact_depth, "calm_exact_controls": exact_calm,
        "effective_shallow_cases": changed, "fixture_fields": "byte-identical"},
        "depth_limited_absorption": optics_summary})


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--target", required=True, type=Path)
    p.add_argument("--out", type=Path)
    a = p.parse_args()
    if a.out:
        run(a.out.resolve(), a.target.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="cubey-scenic-water-gpu-") as temp:
            run(Path(temp) / "controls", a.target.resolve())


if __name__ == "__main__":
    main()
