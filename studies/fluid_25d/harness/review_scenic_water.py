#!/usr/bin/env python3
"""Matched consolidation evidence for the single Scenic water renderer."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import statistics
import xml.etree.ElementTree as ET

import run_native_presentation_v1 as ref
import review_macro_terrain_v4 as macro
import review_terrain_material_v3 as terrain

LOCAL = Path("projects/fluid/fluid_25d")
SIM = Path("projects/fluid/sim/fluid_25d")
SCENES = (("rain-off", "overview", 0), ("rain-on", "runoff", 900),
          ("rain-off", "runoff", 1800), ("rain-off", "runoff", 6000),
          ("rain-off", "collection", 14400), ("rain-on", "collection", 14400))
EXTRA_SOURCES = (
    LOCAL / "review_scenic_water.py", LOCAL / "test_scenic_water.py",
    LOCAL / "test_scenic_water_gpu.py", LOCAL / "probe_scenic_water.py",
    SIM / "shaders/fluid_25d_scenic_water.frag",
    SIM / "shaders/fluid_25d_water_film.glsl",
    Path("shaders/cubey/pbr.glsl"), Path("include/cubey/render/material.h"),
    Path("docs/notes/fluid25d-water-film-v5.md"),
)


def sources():
    paths = [*macro.sources(), *(str(p) for p in EXTRA_SOURCES)]
    return {p: ref.sha256_file(ref.ROOT / p) for p in paths if (ref.ROOT / p).is_file()}


def assert_render_runtime(expected, actual, where):
    """Synthetic controls share the renderer, not the frozen rain inputs."""
    keys = ("executable", "executable_sha256", "compiled_shaders")
    if {k: expected[k] for k in keys} != {k: actual[k] for k in keys}:
        raise ValueError("executable or SPIR-V changed " + where)


def snapshot(out):
    phase = out / "final-source"
    ref.reserve_directory(phase)
    pins = sources()
    for name in pins:
        target = phase / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ref.ROOT / name, target)
    ref.write_json_exclusive(phase / "identity.json", {
        "source_files": pins, "runtime_identity": ref.runtime_identity()})


def extras(out, label, legacy=False):
    """Migration-only comparison with the old candidate, not a runtime mode."""
    phase = out / terrain.checked_label(label)
    ref.reserve_directory(phase)
    pins, runtime, rows = sources(), ref.runtime_identity(), []
    for material, view in (("v1", "shaded"), ("refined", "shaded"),
                           *(("macro", v) for v in ("environment-only", "direct-only",
                            "transmission-only", "no-direct", "no-environment", "no-clarity",
                            "no-detail", "depth-bands", "coverage", "film-weight"))):
        a = ref.still_asset("rain-off", "runoff", 6000)
        a.update(id=material + "-" + view, width=1280, height=720)
        path = phase / (a["id"] + ".png")
        command = ref.app_command(a, path, "scenic", False)
        command += ["--fluid25d-scenic-material", material,
                    "--fluid25d-scenic-water-view", view]
        if legacy:
            command += ["--fluid25d-scenic-water-film", "wet-ground"]
        _, receipt = ref.run_logged(command, phase / "logs", a["id"], timeout=180,
                                    expected_asset=a, expected_presentation="scenic")
        rows.append({**a, "path": path.name, "sha256": ref.sha256_file(path), "receipt": receipt})
        print("captured " + label + "/" + a["id"], flush=True)
    ref.assert_same_runtime(runtime, ref.runtime_identity(), "extras")
    if pins != sources():
        raise ValueError("sources changed during extras")
    ref.write_json_exclusive(phase / "manifest.json", {
        "source_files": pins, "runtime_identity": runtime, "assets": rows})


def capture(out):
    phase = out / "consolidated"
    ref.reserve_directory(phase)
    pins, runtime, rows = sources(), ref.runtime_identity(), []
    plan = [ref.still_asset(*s) for s in SCENES]
    for scene in SCENES:
        a = ref.still_asset(*scene)
        a.update(id=a["id"] + "-readable", style="readable")
        plan.append(a)
    plan += [ref.diagnostic_asset("rain-off", v, 14400) for v in ref.DIAGNOSTICS]
    for a in plan:
        a.update(width=1280, height=720)
        style = a.get("style", "scenic")
        path = phase / (a["id"] + ".png")
        command = ref.app_command(a, path, style, False)
        if style == "scenic":
            command += ["--fluid25d-scenic-material", "macro"]
        _, receipt = ref.run_logged(command, phase / "logs", a["id"], timeout=180,
                                    expected_asset=a, expected_presentation=style)
        rows.append({**a, "style": style, "path": path.name,
                     "sha256": ref.sha256_file(path), "receipt": receipt})
        print("captured consolidated/" + a["id"], flush=True)
    ref.assert_same_runtime(runtime, ref.runtime_identity(), "capture")
    if pins != sources():
        raise ValueError("sources changed during capture")
    ref.write_json_exclusive(phase / "manifest.json", {
        "source_files": pins, "runtime_identity": runtime, "assets": rows})


def parity(out):
    counts = []
    for before, after in (("candidate-before", "consolidated"), ("extras-before", "extras-after")):
        old = json.loads((out / before / "manifest.json").read_text())
        new = json.loads((out / after / "manifest.json").read_text())
        old_rows = {a["path"]: a["sha256"] for a in old["assets"]}
        new_rows = {a["path"]: a["sha256"] for a in new["assets"]}
        if old_rows != new_rows:
            different = [p for p in old_rows if old_rows[p] != new_rows.get(p)]
            raise ValueError("candidate parity changed: " + str(different))
        counts.append(len(old_rows))
    old = json.loads((out / "before-source/identity.json").read_text())["runtime_identity"]
    runtime = ref.runtime_identity()
    if old["inputs"] != runtime["inputs"]:
        raise ValueError("immutable native inputs changed")
    unchanged = 0
    for name, digest in old["compiled_shaders"]["files"].items():
        if name in ("fluid_25d_scenic_water.frag.spv", "fluid_25d_scenic_water_study.frag.spv"):
            continue
        if runtime["compiled_shaders"]["files"].get(name) != digest:
            raise ValueError("unrelated shader changed: " + name)
        unchanged += 1
    if "fluid_25d_scenic_water_study.frag.spv" in runtime["compiled_shaders"]["files"]:
        raise ValueError("retired study SPIR-V still in live shader directory")
    archive = ref.OUTPUT_ROOT / "water-film-v5-20261007-study"
    synthetic = 0
    for case in ("dry", "film", "transition", "deep", "thin-stream", "sloped-film",
                 "transition-bspline", "transition-marching-squares"):
        views = ("coverage",) if case.startswith("transition-") else ("shaded", "coverage")
        for view in views:
            old_path = archive / "gpu-controls" / f"{case}-wet-ground-{view}-0.png"
            new_path = out / "gpu-controls" / f"{case}-default-{view}-0.png"
            if ref.sha256_file(old_path) != ref.sha256_file(new_path):
                raise ValueError("archived synthetic candidate changed: " + case + "/" + view)
            synthetic += 1
    seal = json.loads((archive / "review-seal.json").read_text())
    if seal["artifacts"] != terrain.inventory(archive):
        raise ValueError("historical V5 review artifacts changed")
    return {"matched_main_images": counts[0], "matched_preset_component_images": counts[1],
            "matched_archived_synthetic_images": synthetic,
            "historical_v5_artifact_inventory": "unchanged",
            "unchanged_other_shaders": unchanged, "native_inputs": "byte-identical"}


def profile(out):
    phase = out / "profiles"
    ref.reserve_directory(phase)
    pins, runtime, rows = sources(), ref.runtime_identity(), []
    for width, height in ((1280, 720), (1920, 1080)):
        for batch in range(3):
            label = f"{height}p-{batch}"
            a = ref.video_asset(label, "rain-off", "runoff", 6000, 120, 30, 5)
            a.update(width=width, height=height, profile_warmup_frames=12)
            prefix = phase / label
            command = ref.app_command(a, prefix.with_suffix(".mp4"), "scenic", False, prefix)
            command += ["--fluid25d-scenic-material", "macro"]
            _, receipt = ref.run_logged(command, phase / "logs", label, timeout=300,
                                        expected_asset=a, expected_presentation="scenic")
            rows.append({"height": height, "batch": batch,
                         "summary": ref.profile_summary(prefix, 12, 108), "receipt": receipt})
            print("profiled " + label, flush=True)
    ref.assert_same_runtime(runtime, ref.runtime_identity(), "profiles")
    if pins != sources():
        raise ValueError("profile sources changed")
    summaries = []
    for height in (720, 1080):
        group = [r["summary"] for r in rows if r["height"] == height]
        summaries.append({"height": height,
                          "median_ms": statistics.median(r["gpu_median_ms"] for r in group),
                          "p95_ms": statistics.median(r["gpu_p95_ms"] for r in group),
                          "worst_batch_p95_ms": max(r["gpu_p95_ms"] for r in group)})
    ref.write_json_exclusive(phase / "result.json", {
        "source_files": pins, "runtime_identity": runtime, "rows": rows, "summaries": summaries,
        "scope": "Headless replay GPU spans; no simultaneous CUDA; excludes first-use setup."})


def seal(out):
    tests = ET.parse(out / "gates.xml").getroot().findall(".//testcase")
    if not tests or any(t.find(k) is not None for t in tests for k in ("failure", "error", "skipped")):
        raise ValueError("development tests must pass without skips")
    pins, runtime = sources(), ref.runtime_identity()
    for p in ("consolidated/manifest.json", "extras-after/manifest.json",
              "profiles/result.json", "gui/result.json", "gpu-controls/result.json"):
        value = json.loads((out / p).read_text())
        if p == "gpu-controls/result.json":
            assert_render_runtime(value["runtime_identity"], runtime, p)
        else:
            ref.assert_same_runtime(value["runtime_identity"], runtime, p)
        if value["source_files"] != pins or value.get("status", "PASS") != "PASS":
            raise ValueError("evidence failed or source identity changed: " + p)
    profiles = json.loads((out / "profiles/result.json").read_text())["summaries"]
    if profiles[0]["height"] != 720 or profiles[0]["worst_batch_p95_ms"] > 4:
        raise ValueError("720p review budget failed")
    value = {"status": "PASS", "parity": parity(out), "development_tests": len(tests),
             "profiles": profiles, "human_visual_acceptance": "not repeated; subtle polish accepted for consolidation"}
    ref.write_json_exclusive(out / "acceptance.json", value)
    ref.write_json_exclusive(out / "review-seal.json", {
        "schema": "cubey.fluid25d.scenic-water-consolidation.v1", "source_files": pins,
        "runtime_identity": runtime, "artifacts": terrain.inventory(out)})
    print(json.dumps(value, indent=2))


def verify(out):
    value = json.loads((out / "review-seal.json").read_text())
    if value["artifacts"] != terrain.inventory(out) or value["source_files"] != sources():
        raise ValueError("sealed artifacts or sources changed")
    ref.assert_same_runtime(value["runtime_identity"], ref.runtime_identity(), "verification")
    return {"status": "PASS", "artifact_count": len(value["artifacts"])}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=("snapshot", "capture", "extras", "profile", "seal", "verify"))
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--before", action="store_true", help="extras only: old executable's selected wet-ground candidate")
    a = p.parse_args()
    out = a.out.resolve()
    if a.out.is_symlink() or out.parent != ref.OUTPUT_ROOT.resolve() or not out.name.startswith("scenic-water-"):
        raise ValueError("fresh scenic-water-* leaf directly under outputs/fluid required")
    if a.before and a.action != "extras":
        p.error("--before applies only to migration extras")
    if a.action == "extras":
        extras(out, "extras-before" if a.before else "extras-after", a.before)
    elif a.action == "verify":
        print(json.dumps(verify(out), indent=2))
    else:
        globals()[a.action](out)


if __name__ == "__main__":
    main()
