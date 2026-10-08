#!/usr/bin/env python3
"""Scenic material study on frozen native fields; no solver or desktop launch."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

import review_scenic_water_v1 as previous
import run_native_presentation_v1 as ref

SCENES = (("rain-on", "overview", 0), ("rain-on", "runoff", 1800),
          ("rain-on", "runoff", 6000), ("rain-off", "collection", 14400))


def material_args(profile, tuning=None):
    if profile not in ("v1", "refined"):
        raise ValueError("unknown material")
    args = ["--fluid25d-scenic-material", profile]
    if tuning is not None:
        args += ["--fluid25d-scenic-tuning", str(tuning.resolve())]
    return args


def effective_material(text):
    match = re.search(r"fluid_25d_scenic_material: profile=(\S+) settings=(\{[^\n]+\})", text)
    if not match:
        raise ValueError("missing effective material receipt")
    return {"profile": match[1], "settings": json.loads(match[2])}


def legacy_parity(out, runtime):
    old = json.loads((out/"baseline/protocol.json").read_text())
    if old["inputs"] != ref.frozen_input_identity():
        raise ValueError("frozen inputs changed")
    legacy = {n: h for n, h in old["runtime"]["compiled_shaders"]["files"].items()
              if not n.startswith("fluid_25d_scenic_")}
    current = runtime["compiled_shaders"]["files"]
    if any(current.get(n) != h for n, h in legacy.items()):
        raise ValueError("legacy/numerical shader changed")
    return len(legacy)


def capture(out, label, profile, tuning, quick=False):
    leaf = out/label
    ref.reserve_directory(leaf)
    runtime = ref.runtime_identity()
    count = legacy_parity(out, runtime)
    tuning_pin = ({"path": str(tuning.resolve()), "sha256": ref.sha256_file(tuning)}
                  if tuning else None)
    ref.write_json_exclusive(leaf/"protocol.json", {"runtime": runtime,
        "source_files": previous.source_files(), "inputs": ref.frozen_input_identity(),
        "material": profile, "tuning": tuning_pin, "quick": quick,
        "simulation": "immutable saved states", "human_visual_acceptance": "deferred"})
    assets, settings = [], None
    plan = [ref.still_asset(*scene) for scene in SCENES]
    if not quick:
        plan += [ref.diagnostic_asset(case, view, t) for case, t in
                 (("rain-on", 6000), ("rain-off", 14400)) for view in ("depth", "flow", "wet-dry")]
    for asset in plan:
        asset.update(width=1280, height=720)
        for style in (("scenic",) if quick else ("readable", "scenic")):
            path = leaf/f"{asset['id']}-{style}.png"
            cmd = ref.app_command(asset, path, style, False)
            if style == "scenic":
                cmd += material_args(profile, tuning)
            result, receipt = ref.run_logged(cmd, leaf/"logs", path.stem,
                                            expected_asset=asset, expected_presentation=style)
            if style == "scenic":
                effective = effective_material(result.stdout)
                if settings is not None and settings != effective:
                    raise ValueError("material changed between captures")
                settings = effective
            assets.append({**asset, "style": style, "path": path.name,
                           "sha256": ref.sha256_file(path), "receipt": receipt})
    ref.assert_same_runtime(runtime, ref.runtime_identity(), "material capture completion")
    if tuning and ref.sha256_file(tuning) != tuning_pin["sha256"]:
        raise ValueError("tuning file changed during capture")
    ref.write_json_exclusive(leaf/"manifest.json", {"assets": assets, "effective_material": settings})
    if not quick:
        by_id = {(a["id"], a["style"]): a for a in assets}
        baseline = json.loads((out/"baseline/manifest.json").read_text())["assets"]
        for old in baseline:
            if old["sha256"] != by_id[old["id"], "readable"]["sha256"]:
                raise ValueError("Readable pixel parity failed")
            if old["kind"] == "diagnostic" and old["sha256"] != by_id[old["id"], "scenic"]["sha256"]:
                raise ValueError("raw diagnostic pixel parity failed")
        ref.write_json_exclusive(leaf/"parity.json", {"readable": "PASS", "raw_diagnostics": "PASS",
            "legacy_and_numerical_spirv": "PASS", "unchanged_shader_count": count})


def media(out, label):
    leaf = out/label
    ref.reserve_directory(leaf)
    runtime = ref.runtime_identity()
    legacy_parity(out, runtime)
    assets = []
    for name, case, camera, start, interval in (
            ("refined-flow", "rain-on", "runoff", 4800, 5),
            ("refined-collection", "rain-off", "collection", 9600, 20),
            ("refined-rain-on", "rain-on", "collection", 9600, 20)):
        asset = ref.video_asset(name, case, camera, start, 240, 30, interval)
        asset.update(width=1280, height=720)
        path = leaf/(name+".mp4")
        cmd = ref.app_command(asset, path, "scenic", True)+material_args("refined")
        result, receipt = ref.run_logged(cmd, leaf/"logs", name,
                                        expected_asset=asset, expected_presentation="scenic")
        ref.validate_probe(ref.ffprobe_video(path), 240, 30, 1280, 720)
        assets.append({"name": name, "path": path.name, "asset": asset, "receipt": receipt,
                       "sha256": ref.sha256_file(path), "effective_material": effective_material(result.stdout)})
    font = ref.find_font()
    for name, old, new in (("01-runoff-v1-v2", "scenic-flow", "refined-flow"),
                           ("02-collection-v1-v2", "scenic-rain-off", "refined-collection")):
        output = leaf/(name+".mp4")
        graph = (f"[0:v]scale=640:360,drawtext=fontfile='{font}':text='Scenic V1':x=12:y=12:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.65[l];"
                 f"[1:v]scale=640:360,drawtext=fontfile='{font}':text='Scenic V2':x=12:y=12:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.65[r];[l][r]hstack=inputs=2[v]")
        result = ref.rtk_output("ffmpeg", "-v", "error", "-n", "-ignore_editlist", "1", "-i", str(out/"v1-media"/(old+".mp4")),
            "-ignore_editlist", "1", "-i", str(leaf/(new+".mp4")), "-filter_complex", graph, "-map", "[v]",
            "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p", str(output))
        if result.returncode:
            raise RuntimeError(result.stderr)
        ref.validate_probe(ref.ffprobe_video(output), 240, 30, 1280, 360)
        for suffix, select, frames in (("poster", "eq(n,0)", 1), ("contact", "not(mod(n,60))", 1)):
            png = leaf/(name+"-"+suffix+".png")
            vf = "select='"+select+"'"+(",tile=1x4" if suffix == "contact" else "")
            result = ref.rtk_output("ffmpeg", "-v", "error", "-n", "-ignore_editlist", "1", "-i", str(output),
                "-vf", vf, "-frames:v", str(frames), str(png))
            if result.returncode:
                raise RuntimeError(result.stderr)
        assets.append({"name": name, "path": output.name, "sha256": ref.sha256_file(output),
                       "sources": ["v1-media/"+old+".mp4", label+"/"+new+".mp4"]})
    ref.assert_same_runtime(runtime, ref.runtime_identity(), "material video completion")
    ref.write_json_exclusive(leaf/"manifest.json", {"runtime": runtime,
        "source_files": previous.source_files(), "assets": assets, "human_visual_acceptance": "deferred"})


def still_review(out):
    commands, filters, views = [], [], []
    font = ref.find_font()
    for row, (case, camera, t) in enumerate(SCENES[1:]):
        for col, (leaf, style, title) in enumerate((("baseline", "readable", "Readable"),
                ("v1-baseline", "scenic", "Scenic V1"), ("candidate-final", "scenic", "Scenic V2"))):
            index = row*3+col
            commands += ["-i", str(out/leaf/f"{case}-{camera}-{t}s-{style}.png")]
            filters.append(f"[{index}:v]scale=640:360,drawtext=fontfile='{font}':text='{title} - {t}s':x=12:y=12:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.65[v{index}]")
            views.append(f"[v{index}]")
    layout = "|".join(f"{(i%3)*640}_{(i//3)*360}" for i in range(9))
    filters.append("".join(views)+f"xstack=inputs=9:layout={layout}[out]")
    result = ref.rtk_output("ffmpeg", "-v", "error", "-n", *commands, "-filter_complex", ";".join(filters),
                            "-map", "[out]", "-frames:v", "1", str(out/"still-review.png"))
    if result.returncode:
        raise RuntimeError(result.stderr)


def gallery(out):
    page = """<!doctype html><meta charset='utf-8'><title>Mountain Rain — Scenic V2</title>
<style>body{background:#141b23;color:#dae4ee;font:16px system-ui;max-width:1320px;margin:24px auto;padding:0 16px}video,img{max-width:100%;height:auto}a{color:#83c5fb}p{line-height:1.5}summary{cursor:pointer}</style>
<h1>Mountain Rain: Scenic material refinement</h1>
<p>Same mountain, rain, saved fields, camera and bank coverage. V1 left; V2 right. Readable remains the launcher default. Scenic colors are not depth measurements.</p>
<h2>Runoff — 8 seconds</h2><video controls loop preload='metadata' poster='media-final/01-runoff-v1-v2-poster.png' src='media-final/01-runoff-v1-v2.mp4'></video>
<p>4800–5995 simulated seconds. Compare wet-ground gloss and stream contrast. Dots/trails are approximate motion cues; normal detail is decorative. Native fields remain held at saved 60-second intervals.</p>
<h2>Collection — 8 seconds</h2><video controls loop preload='metadata' poster='media-final/02-collection-v1-v2-poster.png' src='media-final/02-collection-v1-v2.mp4'></video>
<p>9600–14380 seconds, rain off since saved 7260 s. Compare pool appearance and shaded banks, not different hydraulics. Local collection is not proof of a permanent calm lake.</p>
<h2>Still review — no dots</h2><img src='still-review.png'>
<p>Rows: early runoff 1800 s, established runoff 6000 s, late collection 14400 s. Columns: Readable, Scenic V1, Scenic V2. Actual bank geometry remains stepped.</p>
<details><summary>Full-resolution media and diagnostic evidence</summary>
<p><a href='media-final/refined-flow.mp4'>V2 flow</a> · <a href='media-final/refined-collection.mp4'>V2 collection</a> · <a href='media-final/refined-rain-on.mp4'>V2 sustained-rain collection</a></p>
<p><a href='candidate-final/rain-on-diagnostic-depth-6000s-scenic.png'>Raw depth</a> · <a href='candidate-final/rain-on-diagnostic-flow-6000s-scenic.png'>Raw speed</a> · <a href='candidate-final/rain-on-diagnostic-wet-dry-6000s-scenic.png'>Raw wet/dry</a></p>
<p><a href='candidate-final/protocol.json'>Runtime/input/source pins</a> · <a href='candidate-final/manifest.json'>Effective materials</a> · <a href='candidate-final/parity.json'>Readable/raw/shader parity</a> · <a href='profile-final/result.json'>GPU timings</a></p>
<p>Dry terrain, same overview camera:</p><img src='candidate-final/rain-on-overview-0s-scenic.png'>
<p>V1 material remains selectable. Experimental B-spline and prerecorded marching-squares retain their existing limitations. No solver changes, enlarged water coverage, or new shoreline reconstruction.</p></details>
<p><a href='RESULTS.md'>Verdict and measured limits</a>. Private-window checks are automated evidence; human visual acceptance remains deferred.</p>"""
    ref.write_text_exclusive(out/"index.html", page)


def seal(out):
    runtime = ref.runtime_identity()
    for leaf, name in (("candidate-final", "protocol.json"), ("media-final", "manifest.json"), ("profile-final", "result.json")):
        value = json.loads((out/leaf/name).read_text())
        ref.assert_same_runtime(runtime, value["runtime"], leaf)
        if value["source_files"] != previous.source_files():
            raise ValueError("source pins mismatch: "+leaf)
    parity = json.loads((out/"candidate-final/parity.json").read_text())
    if any(parity[k] != "PASS" for k in ("readable", "raw_diagnostics", "legacy_and_numerical_spirv")):
        raise ValueError("parity gate failed")
    protocol = json.loads((out/"candidate-final/protocol.json").read_text())
    material = json.loads((out/"candidate-final/manifest.json").read_text())["effective_material"]
    if protocol["material"] != "refined" or protocol["tuning"] is not None or material["profile"] != "refined":
        raise ValueError("final capture is not the unmodified refined preset")
    videos = json.loads((out/"media-final/manifest.json").read_text())["assets"]
    if any(row["effective_material"] != material for row in videos if "effective_material" in row):
        raise ValueError("video materials differ from stills")
    for row in json.loads((out/"profile-final/result.json").read_text())["rows"]:
        if row["style"] == "scenic":
            receipt = row["receipt"]
            text = (out/"profile-final"/receipt["stdout_path"]).read_text()
            if effective_material(text) != material:
                raise ValueError("profile did not measure the accepted material")
    for leaf in ("gui-replay-final", "gui-banks-final", "gui-live-final", "gui-live-detuned-validation"):
        value = json.loads((out/leaf/"manifest.json").read_text())
        if value["status"] != "pass" or value["style"] != "scenic":
            raise ValueError("GUI gate failed")
        ref.assert_same_runtime(runtime, value["runtime_identity"], leaf)
    if not any(row["style"] == "scenic" and row["width"] == 1280 and row["budget_4ms_p95"] == "PASS"
               for row in json.loads((out/"profile-final/result.json").read_text())["rows"]):
        raise ValueError("720p GPU budget missing")
    suite = ET.parse(out/"gates-final.xml").getroot()
    if int(suite.get("tests", "0")) < 167 or any(int(suite.get(k, "0")) for k in ("failures", "errors", "skipped")):
        raise ValueError("complete dev test gate missing/failed")
    names = {case.get("name") for case in suite.findall("testcase")}
    if not {"fluid_25d_scenic_gpu", "fluid_25d_scenic_material_review_helpers", "fluid_25d_tests"} <= names:
        raise ValueError("required material checks missing")
    leaves = ["baseline", "v1-baseline", "v1-media", "candidate-final", "media-final", "profile-final",
              "gui-replay-final", "gui-banks-final", "gui-live-final", "gui-live-detuned-validation"]
    files = ["index.html", "RESULTS.md", "still-review.png", "gates-final.log", "gates-final.xml"]
    ref.write_json_exclusive(out/"review-seal.json", {"schema": "cubey.fluid25d.scenic-material-review.v2",
        "runtime": runtime, "source_files": previous.source_files(), "inputs": ref.frozen_input_identity(),
        "leaves": leaves, "files": files, "artifacts": previous.artifact_inventory(out, leaves, files),
        "scope": "V1 frozen baseline and final V2 handoff; developmental ablations excluded",
        "human_visual_acceptance": "deferred"})
    print(json.dumps(verify(out)))


def verify(out):
    value = json.loads(previous.safe_artifact(out, "review-seal.json").read_text())
    if value["schema"] != "cubey.fluid25d.scenic-material-review.v2":
        raise ValueError("wrong review schema")
    if previous.artifact_inventory(out, value["leaves"], value["files"]) != value["artifacts"]:
        raise ValueError("review artifact inventory/hash mismatch")
    ref.assert_same_runtime(value["runtime"], ref.runtime_identity(), "sealed material review")
    if value["source_files"] != previous.source_files() or value["inputs"] != ref.frozen_input_identity():
        raise ValueError("review source/input pins changed")
    for relative in re.findall(r"(?:src|href|poster)=['\"]([^'\"]+)['\"]", (out/"index.html").read_text()):
        previous.safe_artifact(out, relative)
        if relative not in value["artifacts"]:
            raise ValueError("unsealed gallery link")
    return {"status": "PASS", "artifacts": len(value["artifacts"]), "human_visual_acceptance": "deferred"}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("phase", choices=("capture", "profile", "media", "sheet", "gallery", "seal", "verify"))
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--label", default="candidate-final")
    p.add_argument("--material", choices=("v1", "refined"), default="refined")
    p.add_argument("--tuning", type=Path)
    p.add_argument("--quick", action="store_true")
    a = p.parse_args()
    out = a.out.resolve()
    if out.parent != ref.OUTPUT_ROOT.resolve() or not out.name.startswith("scenic-material-v2-"):
        p.error("fresh scenic-material-v2-* evidence under outputs/fluid required")
    if Path(a.label).name != a.label or a.label in (".", "..", "baseline", "v1-baseline", "v1-media"):
        p.error("simple fresh candidate label required")
    if a.phase == "capture":
        capture(out, a.label, a.material, a.tuning, a.quick)
    elif a.phase == "profile":
        previous.profile(out, "profile-final")
    elif a.phase == "media":
        media(out, "media-final")
    elif a.phase == "gallery":
        gallery(out)
    elif a.phase == "sheet":
        still_review(out)
    elif a.phase == "seal":
        seal(out)
    else:
        print(json.dumps(verify(out)))
