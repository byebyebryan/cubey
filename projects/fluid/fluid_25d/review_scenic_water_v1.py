#!/usr/bin/env python3
"""Frozen-field Scenic audition; fresh evidence only, no hydraulic solver launch."""
from __future__ import annotations

import argparse
import csv
import html
import json
import math
import re
import statistics
from pathlib import Path

import run_native_presentation_v1 as ref


def source_files() -> dict:
    paths = list((ref.ROOT/"projects/fluid/sim/fluid_25d").rglob("*"))
    paths += list(Path(__file__).parent.glob("*.py"))
    paths += [Path(__file__).parent/"CMakeLists.txt",Path(__file__).parent/"fluid_25d_project_config.h"]
    paths += [ref.ROOT/p for p in ("include/cubey/input/input.h", "src/cubey/input/input.cpp", "src/cubey/host/glfw_window.cpp")]
    paths += list((ref.ROOT/"shaders/cubey").rglob("*.glsl"))
    paths += [ref.ROOT/p for p in ("src/cubey/render/generated_ibl.cpp", "src/cubey/render/generated_texture.cpp")]
    return {str(p.relative_to(ref.ROOT)):ref.sha256_file(p) for p in sorted(paths)
            if p.is_file() and p.suffix in (".cpp",".h",".glsl",".vert",".frag",".comp",".py",".txt")}


def capture(out: Path, phase: str, label: str | None = None) -> None:
    leaf = out / (label or phase)
    ref.reserve_directory(leaf)
    runtime = ref.runtime_identity()
    inputs = ref.frozen_input_identity()
    ref.write_json_exclusive(leaf / "protocol.json", {
        "runtime": runtime, "inputs": inputs, "phase": phase, "source_files": source_files(),
        "human_visual_acceptance": "deferred", "simulation": "immutable saved states",
    })
    assets = []
    for case, camera, t in (("rain-on", "runoff", 6000), ("rain-off", "collection", 14400),
                            ("rain-on", "overview", 0), ("rain-on", "runoff", 1800)):
        asset = ref.still_asset(case, camera, t)
        asset.update(width=1280, height=720)
        styles = ("readable",) if phase == "baseline" else ("readable", "scenic")
        for style in styles:
            path = leaf / f"{asset['id']}-{style}.png"
            cmd = ref.app_command(asset, path, style, False)
            cmd += ["--fluid25d-native-surface-highlights", "off"]
            result, receipt = ref.run_logged(cmd, leaf / "logs", path.stem, expected_asset=asset,
                                            expected_presentation=style)
            if result.returncode:
                raise RuntimeError(result.stderr)
            assets.append({**asset, "style": style, "path": path.name,
                           "sha256": ref.sha256_file(path), "receipt": receipt})
    for case, t in (("rain-on", 6000), ("rain-off", 14400)):
        for view in ("depth", "flow", "wet-dry"):
            asset = ref.diagnostic_asset(case, view, t)
            asset.update(width=1280, height=720)
            styles = ("readable",) if phase == "baseline" else ("readable", "scenic")
            for style in styles:
                path = leaf / f"{asset['id']}-{style}.png"
                result, receipt = ref.run_logged(ref.app_command(asset, path, style, False),
                                                leaf / "logs", path.stem, expected_asset=asset,
                                                expected_presentation=style)
                if result.returncode:
                    raise RuntimeError(result.stderr)
                assets.append({**asset, "style": style, "path": path.name,
                               "sha256": ref.sha256_file(path), "receipt": receipt})
    ref.assert_same_runtime(runtime, ref.runtime_identity(), "capture completion")
    ref.write_json_exclusive(leaf / "manifest.json", {"assets": assets})
    if phase != "baseline":
        old_shaders = json.loads((out/"baseline/protocol.json").read_text())["runtime"]["compiled_shaders"]["files"]
        for name,digest in old_shaders.items():
            if runtime["compiled_shaders"]["files"].get(name)!=digest:
                raise RuntimeError("Legacy/numerical compiled shader changed: "+name)
        baseline = json.loads((out / "baseline/manifest.json").read_text())["assets"]
        by_id = {(a["id"], a["style"]): a for a in assets}
        for old in baseline:
            if old["sha256"] != by_id[old["id"], "readable"]["sha256"]:
                raise RuntimeError(f"Readable pixel parity failed: {old['id']}")
            if old["kind"] == "diagnostic" and old["sha256"] != by_id[old["id"], "scenic"]["sha256"]:
                raise RuntimeError(f"Scenic raw diagnostic parity failed: {old['id']}")
        ref.write_json_exclusive(leaf / "parity.json", {"readable": "PASS", "raw_diagnostics": "PASS",
            "legacy_and_numerical_spirv": "PASS", "unchanged_shader_count":len(old_shaders)})


def profile(out: Path, label: str | None) -> None:
    leaf = out/(label or "profile")
    ref.reserve_directory(leaf)
    runtime = ref.runtime_identity()
    rows = []
    for style,width,height in (("readable",1280,720),("scenic",1280,720),("scenic",1920,1080)):
        name = f"{style}-{width}x{height}"
        asset = ref.video_asset(name,"rain-on","runoff",6000,120,30,5)
        asset.update(width=width,height=height,profile_warmup_frames=12)
        prefix = leaf/name
        cmd = ref.app_command(asset,leaf/(name+".mp4"),style,True,prefix)
        cmd += ["--fluid25d-native-surface-highlights","off"]
        _,receipt = ref.run_logged(cmd,leaf/"logs",name,expected_asset=asset,expected_presentation=style)
        summary = ref.profile_summary(prefix,12,108)
        passes = {}
        with prefix.with_suffix(".passes.csv").open(newline="") as stream:
            for r in csv.DictReader(stream):
                if r["kind"]=="gpu" and int(r["frame_index"])>=12:
                    v = float(r["duration_ms"])
                    if not math.isfinite(v) or v<=0: raise ValueError("non-finite/non-positive GPU duration")
                    passes.setdefault(r["label"],[]).append(v)
        pass_summary = {}
        for key,values in passes.items():
            if len(values)!=108: raise ValueError("missing GPU pass samples: "+key)
            pass_summary[key] = {"samples":108,"median_ms":statistics.median(values),
                                 "p95_ms":sorted(values)[math.ceil(len(values)*.95)-1]}
        total = pass_summary["fluid_25d native presentation total"]["p95_ms"]
        if style=="scenic" and width==1280 and total>4:
            raise ValueError(f"Scenic exceeded 4 ms p95 budget: {total}")
        rows.append({"style":style,"width":width,"height":height,"summary":summary,
            "passes":pass_summary,"receipt":receipt,"budget_4ms_p95": "PASS" if total<=4 else "not met",
            "measurement":"standalone GPU, includes upload/cues/dots/presentation; excludes native CUDA, capture and encode"})
    ref.assert_same_runtime(runtime,ref.runtime_identity(),"profile completion")
    ref.write_json_exclusive(leaf/"result.json",{"runtime":runtime,"source_files":source_files(),"rows":rows,
        "warmup_frames":12,"cold_environment_generation":"outside GPU presentation spans; capture receipts include total process wall time"})


def media(out: Path, label: str | None) -> None:
    leaf = out/(label or "media")
    ref.reserve_directory(leaf)
    runtime = ref.runtime_identity()
    rows = []
    for name,case,camera,start,frames,interval,style in (
        ("readable-flow","rain-on","runoff",4800,240,5,"readable"),
        ("scenic-flow","rain-on","runoff",4800,240,5,"scenic"),
        ("scenic-rain-on","rain-on","collection",9600,240,20,"scenic"),
        ("scenic-rain-off","rain-off","collection",9600,240,20,"scenic")):
        asset = ref.video_asset(name,case,camera,start,frames,30,interval)
        asset.update(width=1280,height=720)
        path = leaf/(name+".mp4")
        cmd = ref.app_command(asset,path,style,True)+["--fluid25d-native-surface-highlights","off"]
        _,receipt = ref.run_logged(cmd,leaf/"logs",name,expected_asset=asset,expected_presentation=style)
        ref.validate_probe(ref.ffprobe_video(path),frames,30,1280,720)
        rows.append({"name":name,"asset":asset,"style":style,"path":path.name,
                     "sha256":ref.sha256_file(path),"receipt":receipt})
    # Fixed-size, clearly labelled side-by-side pairs. Native h/q still change
    # only at their saved 60 s boundaries; normal detail and dots are decorative.
    font = str(ref.find_font())
    for name,left,right,l_label,r_label in (
        ("01-readable-vs-scenic","readable-flow","scenic-flow","Readable","Scenic (opt-in)"),
        ("02-rain-on-vs-off","scenic-rain-on","scenic-rain-off","Rain continues: 120 mm/h","Rain off since 7260s")):
        output = leaf/(name+".mp4")
        graph = (f"[0:v]scale=640:360,drawtext=fontfile='{font}':text='{l_label}':x=12:y=12:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.65[l];"
                 f"[1:v]scale=640:360,drawtext=fontfile='{font}':text='{r_label}':x=12:y=12:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.65[r];[l][r]hstack=inputs=2[v]")
        result = ref.rtk_output("ffmpeg","-v","error","-n","-ignore_editlist","1","-i",str(leaf/(left+".mp4")),"-ignore_editlist","1","-i",str(leaf/(right+".mp4")),
            "-filter_complex",graph,"-map","[v]","-c:v","libx264","-crf","18","-pix_fmt","yuv420p",str(output))
        if result.returncode: raise RuntimeError(result.stderr)
        ref.validate_probe(ref.ffprobe_video(output),240,30,1280,360)
        rows.append({"name":name,"path":output.name,"sha256":ref.sha256_file(output),"sources":[left,right]})
    ref.assert_same_runtime(runtime,ref.runtime_identity(),"media completion")
    ref.write_json_exclusive(leaf/"manifest.json",{"runtime":runtime,"source_files":source_files(),"assets":rows,
        "human_visual_acceptance":"deferred"})


def gallery(out: Path, stills: str, videos: str, timings: str) -> None:
    for leaf in (stills,videos,timings):
        if Path(leaf).name!=leaf or leaf in (".",".."): raise ValueError("invalid evidence leaf")
    page = f"""<!doctype html><meta charset='utf-8'><title>Mountain Rain — Scenic V1</title>
<style>body{{background:#141b23;color:#dae4ee;font:16px system-ui;max-width:1320px;margin:24px auto;padding:0 16px}}video,img{{max-width:100%;height:auto}}.pair{{display:grid;grid-template-columns:1fr 1fr;gap:12px}}a{{color:#83c5fb}}summary{{cursor:pointer}}p{{line-height:1.5}}</style>
<h1>Mountain Rain: Readable vs Scenic</h1>
<p>Same native mountain, rain and saved h/q fields. Scenic is an opt-in rendering alternative, not a new solver or bank reconstruction. Readable remains the launcher default.</p>
<h2>Start here — 8 seconds</h2><video controls loop preload='metadata' src='{videos}/01-readable-vs-scenic.mp4'></video>
<p>4800–5995 simulated seconds. Look for terrain relief, bed visibility and water reflection. Dots/trails are retained visual flow cues; moving normal detail is decorative, not measured waves. Hydraulic fields are held between 60-second saves.</p>
<h2>Terrain and deeper collection</h2><div class='pair'><figure><img src='{stills}/rain-off-collection-14400s-readable.png'><figcaption>Readable: easier depth/footprint interpretation.</figcaption></figure><figure><img src='{stills}/rain-off-collection-14400s-scenic.png'><figcaption>Scenic: lit materials, reflection and depth-dependent absorption.</figcaption></figure></div>
<h2>Rain continues vs rain stops — 8 seconds</h2><video controls loop preload='metadata' src='{videos}/02-rain-on-vs-off.mp4'></video>
<p>9600–14380 simulated seconds. The right-hand basin is collecting residual runoff while overall storage falls; this is not evidence of a permanent calm lake.</p>
<details><summary>Shallow streams, dry terrain and evidence</summary><div class='pair'><img src='{stills}/rain-on-runoff-1800s-readable.png'><img src='{stills}/rain-on-runoff-1800s-scenic.png'></div>
<p>Scenic preserves the film/coverage policy; shallow water can be harder to spot against the material than in Readable. The reference cell-scale bank steps remain. B-spline is still experimental; recorded marching-squares is bounded coverage, not new 3D geometry.</p>
<div class='pair'><img src='{stills}/rain-on-overview-0s-readable.png'><img src='{stills}/rain-on-overview-0s-scenic.png'></div>
<p><a href='{stills}/parity.json'>Pixel/shader parity</a> · <a href='{stills}/protocol.json'>Input/runtime/source pins</a> · <a href='{timings}/result.json'>GPU timings</a> · <a href='{videos}/manifest.json'>Video provenance</a></p>
</details><p>Automated private-window and GPU checks are not owner visual acceptance. No desktop GUI was opened. See <a href='RESULTS.md'>results and limits</a>.</p>"""
    ref.write_text_exclusive(out/"index.html",page)


def safe_artifact(out: Path, relative: str) -> Path:
    path = Path(relative)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ValueError("unsafe review artifact: "+relative)
    target = out/path
    if any(p.is_symlink() for p in (target,*target.parents)) or not target.is_file():
        raise ValueError("missing/symlink review artifact: "+relative)
    return target


def artifact_inventory(out: Path, leaves: list[str], files: list[str]) -> dict:
    result = {}
    for leaf in leaves:
        if Path(leaf).name != leaf or leaf in (".",".."):
            raise ValueError("unsafe evidence leaf")
        directory = out/leaf
        if not directory.is_dir() or directory.is_symlink():
            raise ValueError("missing evidence leaf: "+leaf)
        for path in sorted(directory.rglob("*")):
            if path.is_symlink(): raise ValueError("symlink in evidence")
            if path.is_file():
                relative = path.relative_to(out).as_posix()
                result[relative] = ref.sha256_file(safe_artifact(out,relative))
    for relative in files:
        result[relative] = ref.sha256_file(safe_artifact(out,relative))
    return result


def verify_review(out: Path) -> dict:
    seal = json.loads(safe_artifact(out,"review-seal.json").read_text())
    if seal["schema"] != "cubey.fluid25d.scenic-review.v1": raise ValueError("wrong review schema")
    if artifact_inventory(out,seal["leaves"],seal["files"]) != seal["artifacts"]:
        raise ValueError("review artifact inventory/hash mismatch")
    ref.assert_same_runtime(seal["runtime"],ref.runtime_identity(),"sealed Scenic review")
    if seal["source_files"] != source_files(): raise ValueError("review source hashes changed")
    if seal["inputs"] != ref.frozen_input_identity(): raise ValueError("frozen native input hashes changed")
    page = (out/"index.html").read_text()
    for relative in re.findall(r"(?:src|href)=['\"]([^'\"]+)['\"]",page):
        safe_artifact(out,relative)
        if relative not in seal["artifacts"]: raise ValueError("unsealed gallery link: "+relative)
    return {"status":"PASS","artifacts":len(seal["artifacts"]),"human_visual_acceptance":"deferred"}


def seal_review(out: Path, stills: str, videos: str, timings: str) -> None:
    runtime = ref.runtime_identity()
    for leaf,name in ((stills,"protocol.json"),(videos,"manifest.json"),(timings,"result.json")):
        value = json.loads(safe_artifact(out,leaf+"/"+name).read_text())
        ref.assert_same_runtime(runtime,value["runtime"],"final review leaf "+leaf)
        if value["source_files"] != source_files(): raise ValueError("final capture source mismatch: "+leaf)
    parity = json.loads(safe_artifact(out,stills+"/parity.json").read_text())
    if any(parity[k]!="PASS" for k in ("readable","raw_diagnostics","legacy_and_numerical_spirv")):
        raise ValueError("parity gate did not pass")
    measured = json.loads((out/timings/"result.json").read_text())["rows"]
    if not any(r["style"]=="scenic" and r["width"]==1280 and r["budget_4ms_p95"]=="PASS" for r in measured):
        raise ValueError("Scenic presentation budget gate missing")
    for leaf in ("gui-replay-final","gui-banks-final","gui-live-final"):
        value = json.loads(safe_artifact(out,leaf+"/manifest.json").read_text())
        if value["status"]!="pass" or value["style"]!="scenic": raise ValueError("private GUI gate did not pass")
        ref.assert_same_runtime(runtime,value["runtime_identity"],"private GUI review "+leaf)
    leaves = [stills,videos,timings,"gui-replay-final","gui-banks-final","gui-live-final"]
    files = ["index.html","RESULTS.md","gates-final.log","gates-final.xml"]
    ref.write_json_exclusive(out/"review-seal.json",{"schema":"cubey.fluid25d.scenic-review.v1",
        "runtime":runtime,"source_files":source_files(),"inputs":ref.frozen_input_identity(),
        "leaves":leaves,"files":files,"artifacts":artifact_inventory(out,leaves,files),
        "scope":"curated final review only; baseline, failed/prototype captures and harness investigations are excluded",
        "human_visual_acceptance":"deferred"})
    print(json.dumps(verify_review(out)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("baseline", "candidate", "profile", "media", "gallery", "seal", "verify"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--label", help="Fresh candidate evidence leaf; never overwrites an earlier run")
    parser.add_argument("--stills",default="candidate-final")
    parser.add_argument("--videos",default="media-final")
    parser.add_argument("--timings",default="profile-final")
    args = parser.parse_args()
    root = args.out.resolve()
    if root.parent != ref.OUTPUT_ROOT.resolve() or not root.name.startswith("scenic-terrain-water-v1-"):
        parser.error("--out must be a scenic-terrain-water-v1-* leaf under outputs/fluid")
    if args.label and (Path(args.label).name != args.label or args.label in (".", "..", "baseline")):
        parser.error("--label must be a simple new candidate leaf")
    if args.phase=="gallery": gallery(root,args.stills,args.videos,args.timings)
    elif args.phase=="seal": seal_review(root,args.stills,args.videos,args.timings)
    elif args.phase=="verify": print(json.dumps(verify_review(root)))
    elif args.phase=="media": media(root,args.label)
    elif args.phase=="profile": profile(root,args.label)
    else: capture(root,args.phase,args.label)
