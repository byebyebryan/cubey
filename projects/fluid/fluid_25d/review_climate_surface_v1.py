#!/usr/bin/env python3
"""Bounded climate/landform surface comparison on immutable native recordings."""
from __future__ import annotations

import argparse
from array import array
import json
from pathlib import Path
import re
import statistics
import xml.etree.ElementTree as ET

import run_native_presentation_v1 as ref

SOURCE = ref.ROOT / "cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json"
SCENES = (("dry-overview", "rain-on", "overview", 0, -0.52),
          ("dry-reverse", "rain-on", "overview", 0, 2.62),
          ("dry-runoff", "rain-on", "runoff", 0, -0.52),
          ("wet-runoff", "rain-on", "runoff", 6000, -0.52),
          ("collection", "rain-off", "collection", 14400, -0.52))


def inputs():
    climate = SOURCE.with_name("surface-fields.json")
    manifest = json.loads(climate.read_text())
    return {"recordings": ref.frozen_input_identity(),
            "terrain_manifest": ref.sha256_file(SOURCE),
            "climate_manifest": ref.sha256_file(climate),
            "climate_payload": ref.sha256_file(climate.parent / manifest["files"]["climate"]["path"])}


def capture(out, baseline=False):
    leaf = out / ("baseline" if baseline else "candidate")
    ref.reserve_directory(leaf)
    runtime, frozen = ref.runtime_identity(), inputs()
    rows = []
    modes = ("legacy",) if baseline else ("legacy", "landform", "climate")
    for mode in modes:
        plan = [(s, "shaded") for s in SCENES]
        plan += [(SCENES[0], view) for view in ("albedo", "weights", "roughness")]
        for (name, case, camera, time_s, yaw), view in plan:
            a = ref.still_asset(case, camera, time_s)
            a.update(width=1280, height=720)
            label = f"{mode}-{name}-{view}"
            path = leaf / (label + ".png")
            command = ref.app_command(a, path, "scenic", False)
            command += ["--fluid25d-scenic-material", "macro",
                        "--fluid25d-native-camera-yaw", str(yaw),
                        "--fluid25d-scenic-terrain-view", view]
            if not baseline:
                command += ["--fluid25d-scenic-surface-source", str(SOURCE),
                            "--fluid25d-scenic-surface-mode", mode]
            result, receipt = ref.run_logged(command, leaf / "logs", label,
                expected_asset=a, expected_presentation="scenic")
            surface = re.search(r"fluid_25d_terrain_surface: (\{[^\n]+\})", result.stdout)
            if not baseline and not surface:
                raise ValueError("missing source-bound surface receipt")
            rows.append({"id": label, "mode": mode, "scene": name, "view": view,
                         "path": path.name, "sha256": ref.sha256_file(path),
                         "surface": json.loads(surface[1]) if surface else None,
                         "receipt": receipt})
            print("captured " + label, flush=True)
    if frozen != inputs():
        raise ValueError("numerical terrain/recording/climate input changed")
    ref.assert_same_runtime(runtime, ref.runtime_identity(), "surface capture")
    ref.write_json_exclusive(leaf / "manifest.json", {
        "runtime_identity": runtime, "inputs": frozen, "assets": rows,
        "simulation": "immutable recorded fields; no solver launch",
        "human_visual_acceptance": "deferred"})


def review(out):
    old = json.loads((out / "baseline/manifest.json").read_text())
    new = json.loads((out / "candidate/manifest.json").read_text())
    if old["inputs"] != new["inputs"]:
        raise ValueError("baseline/candidate inputs differ")
    assets = {a["id"]: a for a in new["assets"]}
    for a in old["assets"]:
        if a["sha256"] != assets[a["id"]]["sha256"]:
            raise ValueError("disabled surface mode changed legacy pixels: " + a["id"])
    before = old["runtime_identity"]["compiled_shaders"]["files"]
    after = new["runtime_identity"]["compiled_shaders"]["files"]
    changed = [n for n, h in before.items() if h != after.get(n)]
    if changed != ["fluid_25d_scenic_terrain.frag.spv"]:
        raise ValueError("unrelated/numerical shader changed: " + str(changed))
    for filename, scenes, views in (("overview.png", SCENES, ("shaded",)),
            ("components.png", (SCENES[0],), ("albedo", "weights", "roughness"))):
        commands, filters, slots = [], [], []
        font = ref.find_font()
        index = 0
        for scene in scenes:
            for view in views:
                for col, mode in enumerate(("legacy", "landform", "climate")):
                    a = assets[f"{mode}-{scene[0]}-{view}"]
                    commands += ["-i", str(out / "candidate" / a["path"])]
                    title = f"{mode} / {scene[0]} / {view}"
                    filters.append(f"[{index}:v]scale=640:360:flags=lanczos,pad=640:386:0:26:color=0x18212a,drawtext=fontfile='{font}':text='{title}':x=12:y=5:fontsize=16:fontcolor=white[v{index}]")
                    slots.append(f"[v{index}]")
                    index += 1
        layout = "|".join(f"{(i%3)*640}_{(i//3)*386}" for i in range(index))
        filters.append("".join(slots)+f"xstack=inputs={index}:layout={layout}[out]")
        result = ref.rtk_output("ffmpeg", "-v", "error", "-n", *commands,
            "-filter_complex_threads", "1", "-filter_complex", ";".join(filters),
            "-map", "[out]", "-frames:v", "1", str(out / filename))
        if result.returncode:
            raise RuntimeError(result.stderr)
    ref.write_json_exclusive(out / "parity.json", {
        "disabled_mode_pixel_matches": len(old["assets"]), "only_changed_shader": changed,
        "immutable_inputs": "PASS", "human_visual_acceptance": "deferred"})
    ref.write_text_exclusive(out / "index.html", """<!doctype html><meta charset='utf-8'>
<title>Climate + landform surface study</title><style>body{background:#18212a;color:#e0e7ed;font:16px system-ui;margin:24px}img{max-width:100%}a{color:#8acbff}</style>
<h1>Procedural terrain organization</h1><p>Columns: existing macro material, landform masks, climate + landform.
Same elevation, lighting, cameras and saved water fields. No imported textures, foliage or erosion.
Annual precipitation is NOT current rainfall or wetness. Weights are artistic proxies.</p>
<img src='overview.png'><h2>Components</h2><p>Weights: red=rock, green=cover potential, blue=snow potential.
Cover changes bare substrate, not vegetation geometry. Component colors retain Scenic tonemapping.</p>
<img src='components.png'><p><a href='candidate/manifest.json'>Source/field/mask receipts</a> ·
<a href='parity.json'>Parity</a> · <a href='RESULTS.md'>Verdict</a></p>""")


def profile(out):
    leaf = out / "profiles"
    ref.reserve_directory(leaf)
    runtime, frozen = ref.runtime_identity(), inputs()
    rows = []
    # Frame-budget gate declared before measuring; excludes first-use setup and CUDA.
    for batch in range(3):
        for mode in (("legacy", "landform", "climate") if batch % 2 == 0 else
                     ("climate", "landform", "legacy")):
            label = f"{mode}-{batch}"
            a = ref.video_asset(label, "rain-on", "runoff", 6000, 120, 30, 5)
            a.update(width=1280, height=720, profile_warmup_frames=12)
            prefix = leaf / label
            command = ref.app_command(a, prefix.with_suffix(".mp4"), "scenic", False, prefix)
            command += ["--fluid25d-scenic-material", "macro",
                        "--fluid25d-scenic-surface-source", str(SOURCE),
                        "--fluid25d-scenic-surface-mode", mode]
            _, receipt = ref.run_logged(command, leaf / "logs", label,
                expected_asset=a, expected_presentation="scenic")
            rows.append({"mode": mode, "batch": batch,
                         "summary": ref.profile_summary(prefix, 12, 108), "receipt": receipt})
            print("profiled " + label, flush=True)
    summaries = {mode: {"median_ms": statistics.median(r["summary"]["gpu_median_ms"]
                     for r in rows if r["mode"] == mode),
                     "p95_ms": statistics.median(r["summary"]["gpu_p95_ms"]
                     for r in rows if r["mode"] == mode)}
                 for mode in ("legacy", "landform", "climate")}
    delta = summaries["climate"]["p95_ms"] - summaries["legacy"]["p95_ms"]
    if frozen != inputs():
        raise ValueError("inputs changed during profile")
    ref.assert_same_runtime(runtime, ref.runtime_identity(), "surface profile")
    ref.write_json_exclusive(leaf / "result.json", {
        "runtime_identity": runtime, "inputs": frozen, "rows": rows, "summaries": summaries,
        "gate": {"resolution": [1280,720], "p95_ceiling_ms": 4.0,
                 "incremental_p95_ceiling_ms": 0.15},
        "incremental_p95_ms": delta,
        "status": "PASS" if summaries["climate"]["p95_ms"] <= 4.0 and delta <= 0.15 else "FAIL",
        "scope": "headless replay; first-use CPU/GPU setup and simultaneous CUDA excluded"})


def climate_statistics(out):
    identity = json.loads((ref.INPUT_ROOT / "recordings/rain-on/recording.json").read_text())["provenance"]["terrain_source_identity"]
    height = json.loads(SOURCE.read_text())
    manifest_path = SOURCE.with_name("surface-fields.json")
    c = json.loads(manifest_path.read_text())
    grid, payload = c["grid"], c["files"]["climate"]
    data = array("f")
    data.frombytes((manifest_path.parent / payload["path"]).read_bytes())
    x, z, w, h = identity["crop_xzwh"]
    hg = height["grid"]
    sx = [max(0, min(grid["width"] - 1, (hg["sample_origin_x_m"] + (x + i) * hg["sample_spacing_m"] - grid["sample_origin_x_m"]) / grid["sample_spacing_m"])) for i in range(w)]
    sz = [max(0, min(grid["height"] - 1, (hg["sample_origin_z_m"] + (z + i) * hg["sample_spacing_m"] - grid["sample_origin_z_m"]) / grid["sample_spacing_m"])) for i in range(h)]
    rows = {}
    plane = grid["width"] * grid["height"]
    for index, channel in enumerate(payload["channels"]):
        field = data[index*plane:(index+1)*plane]
        sample = []
        for zz in sz:
            z0, z1, tz = int(zz), min(int(zz)+1, grid["height"]-1), zz-int(zz)
            for xx in sx:
                x0, x1, tx = int(xx), min(int(xx)+1, grid["width"]-1), xx-int(xx)
                top = field[z0*grid["width"]+x0]*(1-tx)+field[z0*grid["width"]+x1]*tx
                bottom = field[z1*grid["width"]+x0]*(1-tx)+field[z1*grid["width"]+x1]*tx
                sample.append(top*(1-tz)+bottom*tz)
        rows[channel["name"]] = {"unit": channel["unit"], "minimum": min(sample),
                                 "mean": statistics.fmean(sample), "maximum": max(sample)}
    ref.write_json_exclusive(out / "climate-statistics.json", {
        "inputs": inputs(), "crop": identity["crop_xzwh"], "statistics": rows,
        "scope": "bilinear companion samples; 240m raster does not imply 240m independent climate detail"})


def seal(out):
    """Check the final runtime and all retained evidence before hashing the handoff."""
    runtime, frozen = ref.runtime_identity(), inputs()
    phases = {name: json.loads((out / name / "manifest.json").read_text())
              for name in ("baseline", "candidate")}
    for name, phase in phases.items():
        if phase["inputs"] != frozen:
            raise ValueError("changed immutable inputs: " + name)
        if name == "candidate":
            ref.assert_same_runtime(phase["runtime_identity"], runtime, "final capture seal")
        for asset in phase["assets"]:
            if ref.sha256_file(out / name / asset["path"]) != asset["sha256"]:
                raise ValueError("changed capture: " + asset["id"])
    old = {a["id"]: a["sha256"] for a in phases["baseline"]["assets"]}
    new = {a["id"]: a["sha256"] for a in phases["candidate"]["assets"]}
    if not old or any(new.get(k) != v for k, v in old.items()):
        raise ValueError("legacy pixel parity failed")
    before = phases["baseline"]["runtime_identity"]["compiled_shaders"]["files"]
    after = runtime["compiled_shaders"]["files"]
    changed = [n for n, h in before.items() if h != after.get(n)]
    if changed != ["fluid_25d_scenic_terrain.frag.spv"] or before.keys() != after.keys():
        raise ValueError("unexpected shader change")
    profile_receipt = json.loads((out / "profiles/result.json").read_text())
    ref.assert_same_runtime(profile_receipt["runtime_identity"], runtime, "final profile seal")
    if profile_receipt["inputs"] != frozen or profile_receipt["status"] != "PASS":
        raise ValueError("profile gate failed")
    tests = ET.parse(out / "gates.xml").getroot()
    cases = tests.findall(".//testcase")
    if not cases or any(c.find("failure") is not None or c.find("error") is not None or
                        c.find("skipped") is not None for c in cases):
        raise ValueError("full development gate failed or skipped")
    for leaf in ("private-gui-final", "existing-water-gui"):
        gui = json.loads((out / leaf / "result.json").read_text())
        if gui["status"] != "PASS":
            raise ValueError("private GUI gate failed")
        ref.assert_same_runtime(gui["runtime_identity"], runtime, "final private GUI seal")
        for capture in gui["captures"]:
            if ref.sha256_file(out / leaf / capture["path"]) != capture["sha256"]:
                raise ValueError("changed private GUI capture")
    roi = {}
    for mode, filename in (("legacy", "04-legacy.png"), ("landform", "05-landform.png")):
        paths = [out / "private-gui-final" / p for p in (filename, "06-climate.png")]
        if any(ref.png_dimensions(p) != (1920, 1080) for p in paths):
            raise ValueError("private GUI scene ROI requires the measured layout")
        result = ref.rtk_output("compare", "-metric", "AE", "(", str(paths[0]),
            "-crop", "1370x1080+550+0", "+repage", ")", "(", str(paths[1]),
            "-crop", "1370x1080+550+0", "+repage", ")", "null:")
        if result.returncode != 1 or float(result.stderr.split()[0]) <= 0:
            raise ValueError("GUI changed labels but not scene pixels: " + mode)
        roi[mode + "_versus_climate"] = result.stderr.strip()
    sim = Path("projects/fluid/sim/fluid_25d")
    local = Path("projects/fluid/fluid_25d")
    paths = [local / n for n in ("CMakeLists.txt", "README.md", "fluid_25d_project_config.h",
             "review_climate_surface_v1.py", "probe_scenic_water.py")]
    paths += [sim / (n + suffix) for n, suffix in (
        ("fluid_25d_recording_app", ".cpp"), ("fluid_25d_scenic", ".cpp"),
        ("fluid_25d_scenic", ".h"), ("fluid_25d_tests", ".cpp"),
        ("fluid_25d_terrain_surface", ".cpp"), ("fluid_25d_terrain_surface", ".h"),
        ("fluid_25d_terrain_surface_tests", ".cpp"))]
    paths += [sim / "shaders" / n for n in ("fluid_25d_scenic.glsl", "fluid_25d_scenic_terrain.frag")]
    paths += [Path("projects/terrain") / n for n in ("terrain_surface_model.cpp", "terrain_raster_climate_source.cpp")]
    paths += [Path("docs/notes/fluid25d-climate-surface-v1.md")]
    ref.write_json_exclusive(out / "acceptance.json", {
        "status": "PASS", "runtime_identity": runtime, "inputs": frozen,
        "source_files": {str(p): ref.sha256_file(ref.ROOT / p) for p in paths},
        "artifacts": {str(p.relative_to(out)): ref.sha256_file(p)
                      for p in sorted(out.rglob("*")) if p.is_file()},
        "full_dev_tests": len(cases), "skips": 0,
        "legacy_exact_images": len(old), "changed_shaders": changed,
        "private_gui_scene_roi_comparisons": roi,
        "scope": "completed-recording rendering study; no live climate integration or solver launch",
        "human_visual_acceptance": "deferred", "default_promotion": False})
    print("PASS: sealed runtime, immutable inputs, pixel parity, GUI and full tests", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("baseline", "candidate", "review", "profile", "climate-statistics", "seal"))
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    if out.parent != ref.OUTPUT_ROOT.resolve() or not out.name.startswith("climate-surface-"):
        raise ValueError("output must be a climate-surface-* leaf under outputs/fluid")
    if args.phase == "review":
        review(out)
    elif args.phase == "profile":
        profile(out)
    elif args.phase == "climate-statistics":
        climate_statistics(out)
    elif args.phase == "seal":
        seal(out)
    else:
        capture(out, args.phase == "baseline")


if __name__ == "__main__":
    main()
