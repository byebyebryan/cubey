#!/usr/bin/env python3
"""Bounded macro-terrain review; immutable inputs, fresh output, pinned receipts."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import statistics

import run_native_presentation_v1 as ref
import review_terrain_material_v3 as previous

EXTRA_SOURCES = (
    "include/cubey/render/generated_ibl.h",
    "src/cubey/render/generated_ibl.cpp",
    "tests/cubey/render_generated_ibl_tests.cpp",
    "projects/fluid/sim/fluid_25d/shaders/fluid_25d_scenic.glsl",
    "projects/fluid/fluid_25d/review_macro_terrain_v4.py",
    "projects/fluid/fluid_25d/probe_macro_rain_v4.py",
    "projects/fluid/fluid_25d/test_macro_terrain_v4.py",
)


def sources():
    paths = [*previous.SOURCE_PATHS, *(Path(p) for p in EXTRA_SOURCES)]
    return {str(p): ref.sha256_file(ref.ROOT / p) for p in paths if (ref.ROOT / p).is_file()}


previous.source_files = sources


def snapshot(out, label):
    phase = out / label
    ref.reserve_directory(phase)
    pins = sources()
    for name in pins:
        target = phase / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ref.ROOT / name, target)
    ref.write_json_exclusive(phase / "identity.json", {"source_files": pins, "runtime_identity": ref.runtime_identity()})


def freeze(out):
    ref.reserve_directory(out)
    snapshot(out, "before-source")
    previous.capture(out, "baseline", profile="terrain", parity=True, video=True)
    previous.capture(out, "baseline-components", profile="terrain", views=True)
    previous.capture(out, "baseline-alternate", profile="terrain", yaw=.35)


def controls(out, label, material, tuning):
    phase = out / previous.checked_label(label)
    ref.reserve_directory(phase)
    runtime, pins = ref.runtime_identity(), sources()
    rows = []
    for view in ("terrain-only", "constant-albedo", "no-specular", "no-detail", "no-shadows", "specular-only", "ambient", "direct", "albedo"):
        asset = ref.still_asset("rain-off", "runoff", 6000)
        asset.update(id=view, width=1280, height=720)
        path = phase / (view + ".png")
        command = ref.app_command(asset, path, "scenic", False)
        command += ["--fluid25d-scenic-material", material, "--fluid25d-scenic-terrain-view", view]
        if tuning:
            command += ["--fluid25d-scenic-tuning", str(tuning)]
        result, receipt = ref.run_logged(command, phase / "logs", view, timeout=600,
                                        expected_asset=asset, expected_presentation="scenic")
        if f"view={view} water_draw=0 dots_draw=0" not in result.stdout:
            raise ValueError("component draw suppression missing")
        rows.append({**asset, "view": view, "path": path.name, "sha256": ref.sha256_file(path),
                     "effective_material": previous.effective_material(result.stdout), "receipt": receipt})
        print("captured " + label + "/" + view, flush=True)
    ref.assert_same_runtime(runtime, ref.runtime_identity(), "during controls")
    if pins != sources():
        raise ValueError("control sources changed")
    ref.write_json_exclusive(phase / "manifest.json", {"runtime_identity": runtime, "source_files": pins,
                                                      "assets": rows, "human_visual_acceptance": "deferred"})


def profile(out):
    phase = out / "profiles"
    ref.reserve_directory(phase)
    runtime, pins = ref.runtime_identity(), sources()
    rows = []
    for width, height in ((1280, 720), (1920, 1080)):
        for batch in range(3):
            order = ("terrain", "macro") if batch % 2 == 0 else ("macro", "terrain")
            for material in order:
                label = f"{height}p-{batch}-{material}"
                asset = ref.video_asset(label, "rain-off", "runoff", 6000, 120, 30, 5)
                asset.update(width=width, height=height, profile_warmup_frames=12)
                prefix = phase / label
                command = ref.app_command(asset, prefix.with_suffix(".mp4"), "scenic", True, prefix)
                command += ["--fluid25d-scenic-material", material]
                _, receipt = ref.run_logged(command, phase / "logs", label, timeout=600,
                                           expected_asset=asset, expected_presentation="scenic")
                rows.append({"height": height, "batch": batch, "material": material,
                             "summary": ref.profile_summary(prefix, 12, 108), "receipt": receipt})
                print("profiled " + label, flush=True)
    ref.assert_same_runtime(runtime, ref.runtime_identity(), "during profiles")
    if pins != sources():
        raise ValueError("profile sources changed")
    summaries = []
    for height in (720, 1080):
        stats = {}
        for material in ("terrain", "macro"):
            batch = [r["summary"] for r in rows if r["height"] == height and r["material"] == material]
            stats[material] = {"median_ms": statistics.median(r["gpu_median_ms"] for r in batch),
                               "p95_ms": statistics.median(r["gpu_p95_ms"] for r in batch),
                               "worst_batch_p95_ms": max(r["gpu_p95_ms"] for r in batch)}
        summaries.append({"height": height, "stats": stats})
    ref.write_json_exclusive(phase / "result.json", {"runtime_identity": runtime, "source_files": pins,
                                                    "rows": rows, "summaries": summaries})


def gallery(out):
    phase = out / "review"
    ref.reserve_directory(phase)
    font = ref.find_font()
    graph = (f"[0:v]scale=640:360,drawtext=fontfile='{font}':text='Current terrain V3':x=12:y=12:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.65[l];"
             f"[1:v]scale=640:360,drawtext=fontfile='{font}':text='Macro terrain candidate':x=12:y=12:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.65[r];[l][r]hstack=inputs=2[v]")
    for camera, time in previous.SCENES:
        name = f"rain-off-{camera}-{time}s.png"
        previous.checked_ffmpeg("-i", out/"baseline"/name, "-i", out/"candidate-final"/name,
                                 "-filter_complex", graph, "-map", "[v]", "-frames:v", 1, phase/name)
    for name in ("flow-rain-off", "collection-rain-off"):
        previous.checked_ffmpeg("-ignore_editlist", 1, "-i", out/"baseline"/(name+".mp4"),
                                 "-ignore_editlist", 1, "-i", out/"candidate-final"/(name+".mp4"),
                                 "-filter_complex", graph, "-map", "[v]", "-c:v", "libx264", "-crf", 18,
                                 "-pix_fmt", "yuv420p", "-frames:v", 240, phase/(name+".mp4"))
        previous.checked_ffmpeg("-i", phase/(name+".mp4"), "-frames:v", 1, phase/(name+"-poster.png"))
        ref.validate_probe(ref.ffprobe_video(phase/(name+".mp4")), 240, 30, 1280, 360)
    figures = "".join(f"<figure><img src='review/rain-off-{c}-{t}s.png'><figcaption>{c}, saved {t}s. Same fields and camera.</figcaption></figure>" for c,t in previous.SCENES)
    ref.write_text_exclusive(out/"index.html", f"""<!doctype html><meta charset='utf-8'><title>Macro terrain and rainfall V4</title>
<style>body{{background:#151b22;color:#e4e8ed;font:16px system-ui;max-width:1280px;margin:24px auto;padding:0 16px}}img,video{{max-width:100%}}a{{color:#94ccff}}p{{line-height:1.5}}</style>
<h1>Mountain Rain: bare macro terrain</h1><p>Left: actual pre-pass terrain V3. Right: opt-in macro candidate. No vegetation, clutter or close-up realism target. Solver fields, terrain geometry, water optics and bank coverage are unchanged; Readable remains first-class.</p>
<video controls loop preload='metadata' src='review/flow-rain-off.mp4' poster='review/flow-rain-off-poster.png'></video><p>Eight seconds of runoff playback. Dots are approximate flow cues, not conserved water parcels.</p>
<video controls loop preload='metadata' src='review/collection-rain-off.mp4' poster='review/collection-rain-off-poster.png'></video><p>Eight seconds of rain-off collection/recession.</p>{figures}
<details><summary>Controls and numerical truth</summary><p><a href='controls-v3/ambient.png'>V3 ambient</a> · <a href='controls-macro/ambient.png'>Candidate ambient</a> · <a href='controls-v3/no-specular.png'>No terrain specular</a> · <a href='controls-v3/no-shadows.png'>No terrain shadows</a> · <a href='candidate-final/rain-off-runoff-6000s-readable.png'>Readable reference</a> · <a href='candidate-alternate/rain-off-runoff-6000s.png'>Alternate heading</a></p></details>
<p><a href='RESULTS.md'>Verdict and limits</a> · <a href='acceptance.json'>Validation</a> · <a href='review-seal.json'>Provenance</a></p><p>Macro presentation study only. Human visual acceptance remains deferred; no defaults promoted.</p>""")


def parity(out):
    baseline = json.loads((out/"baseline/manifest.json").read_text())
    legacy = json.loads((out/"legacy-final/manifest.json").read_text())
    candidate = json.loads((out/"candidate-final/manifest.json").read_text())
    by_name = {r["path"]: r for r in legacy["assets"]}
    cand = {r["path"]: r for r in candidate["assets"]}
    controls = 0
    for row in baseline["assets"]:
        if row["kind"] != "video":
            if row["sha256"] != by_name[row["path"]]["sha256"]:
                raise ValueError("legacy pixel changed: " + row["path"])
            if row["presentation"] == "readable" or row["kind"] == "diagnostic":
                if row["sha256"] != cand[row["path"]]["sha256"]:
                    raise ValueError("Readable/raw pixel changed: " + row["path"])
                controls += 1
    before = baseline["runtime_identity"]
    current = ref.runtime_identity()
    excluded = {"fluid_25d_scenic_terrain.frag.spv"}
    old = before["compiled_shaders"]["files"]
    if any(current["compiled_shaders"]["files"].get(n) != h for n,h in old.items() if n not in excluded):
        raise ValueError("water/shared/numerical/geometry shader changed")
    if before["inputs"] != current["inputs"]:
        raise ValueError("native inputs changed")
    return {"legacy_pixel_controls": 11, "readable_raw_pixel_controls": controls,
            "unchanged_compiled_shaders": len(old)-len(excluded), "native_inputs": "byte-identical"}


def seal(out):
    import xml.etree.ElementTree as ET
    checks = ET.parse(out/"gates.xml").getroot().findall(".//testcase")
    if not checks or any(any(t.find(k) is not None for k in ("failure", "error", "skipped")) for t in checks):
        raise ValueError("test suite must pass without skips")
    runtime, pins = ref.runtime_identity(), sources()
    for label in ("candidate-final", "legacy-final", "candidate-alternate", "controls-v3", "controls-macro"):
        manifest = json.loads((out/label/"manifest.json").read_text())
        ref.assert_same_runtime(manifest["runtime_identity"], runtime, label)
        if manifest["source_files"] != pins:
            raise ValueError("final source mismatch: " + label)
    profiles = json.loads((out/"profiles/result.json").read_text())
    ref.assert_same_runtime(profiles["runtime_identity"], runtime, "profiles")
    if profiles["source_files"] != pins:
        raise ValueError("profile sources differ")
    if profiles["summaries"][0]["stats"]["macro"]["worst_batch_p95_ms"] > 4:
        raise ValueError("720p p95 hard budget failed")
    rain = json.loads((out/"gui-rain/manifest.json").read_text())
    if rain["status"] != "pass":
        raise ValueError("private actual rain GUI test failed")
    ref.assert_same_runtime(rain["runtime_identity"],runtime,"rain GUI")
    acceptance = {"parity": parity(out), "full_dev_tests": len(checks), "profiles": profiles["summaries"],
                  "private_rain_gui": "actual GUI rain edits; bounded live only", "human_visual_acceptance": "deferred"}
    ref.write_json_exclusive(out/"acceptance.json", acceptance)
    ref.write_json_exclusive(out/"review-seal.json", {"schema":"cubey.fluid25d.macro-review.v4", "runtime_identity": runtime,
                                                   "source_files": pins, "artifacts": previous.inventory(out),
                                                   "human_visual_acceptance": "deferred"})
    print(json.dumps(acceptance, indent=2))


def verify(out):
    import re
    seal_value = json.loads((out/"review-seal.json").read_text())
    if seal_value["schema"] != "cubey.fluid25d.macro-review.v4" or seal_value["artifacts"] != previous.inventory(out):
        raise ValueError("review artifacts changed")
    ref.assert_same_runtime(seal_value["runtime_identity"],ref.runtime_identity(),"verification")
    if seal_value["source_files"] != sources():
        raise ValueError("review sources changed")
    links = re.findall(r"(?:src|href|poster)='([^']+)'", (out/"index.html").read_text())
    if any(not (out/link).is_file() for link in links):
        raise ValueError("missing gallery artifact")
    return {"status":"PASS", "artifact_count":len(seal_value["artifacts"]), "human_visual_acceptance":"deferred"}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=("freeze", "capture", "controls", "profile", "gallery", "seal", "verify"))
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--label", default="candidate-final")
    p.add_argument("--material", choices=("v1", "refined", "terrain", "macro"), default="macro")
    p.add_argument("--tuning", type=Path)
    p.add_argument("--video", action="store_true")
    p.add_argument("--parity", action="store_true")
    p.add_argument("--yaw", type=float)
    p.add_argument("--sweep", type=float)
    a = p.parse_args()
    out = a.out.resolve()
    if out.parent != ref.OUTPUT_ROOT.resolve() or not out.name.startswith("macro-terrain-v4-"):
        raise ValueError("fresh macro-terrain-v4-* leaf under outputs/fluid required")
    if a.action == "capture":
        previous.capture(out,a.label,profile=a.material,tuning=a.tuning,video=a.video,parity=a.parity,yaw=a.yaw,sweep=a.sweep)
    elif a.action == "controls":
        controls(out,a.label,a.material,a.tuning)
    elif a.action == "verify":
        print(json.dumps(verify(out),indent=2))
    else:
        globals()[a.action](out)


if __name__ == "__main__":
    main()
