#!/usr/bin/env python3
"""Compact actual-Cubey review, standalone profiles and integrity receipt."""
from __future__ import annotations

import argparse
import csv
import html
import json
from pathlib import Path
import re
import shutil
import statistics
import subprocess

import run_native_bank_refinement_v1 as candidate
import run_native_presentation_v1 as ref
import run_native_bank_render_v1 as bank

MODES = ("reference", "moderate", "bspline-2x")
LABELS = {"reference": "Current - triangular", "moderate": "Contour coverage - moderate",
          "bspline-2x": "B-spline 2x - rounder / wider"}
OWN_FILES = (
    "projects/fluid/fluid_25d/run_native_bank_refinement_v1.py",
    "projects/fluid/fluid_25d/review_native_bank_refinement_v1.py",
    "projects/fluid/fluid_25d/test_native_bank_refinement_v1.py",
    "projects/fluid/fluid_25d/test_display_coverage_gpu_v1.py",
    "projects/fluid/sim/fluid_25d/fluid_25d_display_coverage.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_display_coverage.cpp",
)


def stats(values):
    if not values:
        return {"count": 0, "median_ms": None, "p95_ms": None}
    ordered = sorted(values)
    return {"count": len(values), "median_ms": statistics.median(values),
            "p95_ms": ordered[min(len(ordered)-1, __import__("math").ceil(len(ordered)*.95)-1)]}


def profile(out):
    phase = out / "profile"
    ref.reserve_directory(phase)
    identity = bank.identity()
    rows = []
    for repeat in (1,2):
        for mode in (MODES if repeat == 1 else tuple(reversed(MODES))):
            asset = ref.video_asset(f"profile-{mode}-{repeat}", "rain-off", "collection", 7260, 120, 30, 5)
            prefix = phase / asset["id"]
            command = ref.app_command(asset, prefix.with_suffix(".mp4"), "readable", True, prefix)
            command += candidate.mode_args(out, "rain-off", mode, "bake-motion-moderate")
            _, receipt = ref.run_logged(command, phase / "logs", asset["id"], timeout=300,
                                       expected_asset=asset, expected_presentation="readable")
            row = {"mode": mode, "repeat": repeat, "command": receipt,
                   "summary": ref.profile_summary(prefix,12,108)}
            with prefix.with_suffix(".passes.csv").open() as f:
                uploads = [float(v["duration_ms"]) for v in csv.DictReader(f)
                           if v["kind"] == "gpu" and v["label"] == "fluid_25d display coverage upload"]
            text = (phase / "logs" / (asset["id"] + ".stdout.txt")).read_text()
            loads = [float(v) for v in re.findall(r"display_coverage_load: saved_s=\S+ cpu_ms=(\S+)", text)]
            row.update(upload_gpu=stats(uploads), mask_load_cpu=stats(loads))
            rows.append(row)
            print(f"Standalone profile {mode}/{repeat}: p95 {row['summary']['gpu_p95_ms']:.3f} ms; "
                  f"upload {row['upload_gpu']}; load {row['mask_load_cpu']}", flush=True)
    ref.assert_same_runtime(identity, bank.identity(), "standalone profiles")
    ref.write_json_exclusive(phase / "manifest.json", {"runtime_identity": identity, "rows": rows,
        "scope": "960x540, dots on, 120 replay frames/12 warmup/108 measured, same saved fields; reversed second round; no concurrent bake/test/CUDA requested",
        "note": "GPU total includes optional upload on publication frames. CPU load includes file/digest/validation, not bake. Scheduler frame delta is not performance."})


def ffmpeg(out, label, args):
    return ref.run_logged(["rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-v", "error", *args],
                          out / "logs", label, timeout=180)


def find_image(stills, mode, case, angle="default"):
    return next(v for v in stills["assets"] if v["mode"] == mode and v["case"] == case and v["angle"] == angle)


def contact(out):
    phase = out / "review"
    ref.reserve_directory(phase)
    stills = json.loads((out / "stills-final/manifest.json").read_text())
    clips = json.loads((out / "clips/manifest.json").read_text())
    profiles = json.loads((out / "profile/manifest.json").read_text())
    for manifest in (stills, clips, profiles):
        ref.assert_same_runtime(manifest["runtime_identity"], bank.identity(), "review rendering identities")
    font = ref.find_font()
    images = []
    for name, case, angle, crop in (
        ("overview", "rain-off", "default", None),
        ("junction-detail", "rain-off", "default", (590,440,600,310)),
        ("downstream-bank-detail", "rain-off", "default", (760,640,450,410)),
        ("grazing-contact", "rain-off", "grazing", None),
        ("runoff-overview", "rain-on", "default", None),
    ):
        inputs, filters = [], []
        for i, mode in enumerate(MODES):
            row = find_image(stills, mode, case, angle)
            inputs += ["-i", str(out / row["path"])]
            transform = (f"crop={crop[2]}:{crop[3]}:{crop[0]}:{crop[1]}" if crop else "scale=640:360")
            filters.append(f"[{i}:v]{transform},pad=iw:ih+42:0:42:color=0x16202a,"
                           f"drawtext=fontfile={font}:text='{LABELS[mode]}':x=10:y=10:fontsize=19:fontcolor=white[c{i}]")
        filters.append("[c0][c1][c2]hstack=inputs=3[v]")
        output = phase / (name + ".png")
        ffmpeg(phase,name,[*inputs,"-filter_complex",";".join(filters),"-map","[v]","-frames:v","1",str(output)])
        images.append({"path": output.name, "sha256": ref.sha256_file(output), "crop_xywh": crop,
                       "sources": [find_image(stills,mode,case,angle)["path"] for mode in MODES]})
    videos = []
    for case in ("rain-on", "rain-off"):
        inputs, filters, sources = [], [], []
        for i, mode in enumerate(MODES):
            row = next(v for v in clips["assets"] if v["mode"] == mode and v["case"] == case)
            source = out / row["review_video"]["path"]
            if ref.sha256_file(source) != row["review_video"]["sha256"]:
                raise ValueError("source clip hash mismatch")
            inputs += ["-i", str(source)]
            # Keep native 960x540 tiles in the comparison; do not upscale them.
            filters.append(f"[{i}:v]pad=iw:ih+38:0:38:color=0x16202a,"
                           f"drawtext=fontfile={font}:text='{LABELS[mode]}':x=10:y=10:fontsize=19:fontcolor=white[c{i}]")
            sources.append(row)
        if any(v["timeline"] != sources[0]["timeline"] for v in sources):
            raise ValueError("unmatched physical timelines")
        filters.append("[c0][c1][c2]hstack=inputs=3[v]")
        output = phase / (case + "-comparison.mp4")
        ffmpeg(phase,case,[*inputs,"-filter_complex",";".join(filters),"-map","[v]","-an","-c:v","libx264",
                          "-crf","20","-pix_fmt","yuv420p","-movflags","+faststart",str(output)])
        probe = ref.validate_probe(ref.ffprobe_video(output,ignore_editlist=False),120,10,2880,578)
        poster = output.with_suffix(".png")
        ffmpeg(phase,case+"-poster",["-i",str(output),"-vf","select=eq(n\\,60)","-frames:v","1",str(poster)])
        # Decode first/middle/last for primary inspection of the actual clip,
        # not the PNG still renderer. This is temporal sampling, not full review.
        for frame in (0,60,119):
            sample = phase / f"{case}-decoded-{frame:03d}.png"
            ffmpeg(phase,case+f"-decoded-{frame}",["-i",str(output),"-vf",f"select=eq(n\\,{frame})","-frames:v","1",str(sample)])
        videos.append({"path":output.name,"sha256":ref.sha256_file(output),"probe":probe,
                       "poster":poster.name,"poster_sha256":ref.sha256_file(poster),
                       "timeline":sources[0]["timeline"], "source_clips":[v["review_video"] for v in sources]})
    measured = []
    for mode in MODES:
        rows = [r for r in profiles["rows"] if r["mode"] == mode]
        measured.append(f"<tr><td>{LABELS[mode]}</td><td>" + " / ".join(f"{r['summary']['gpu_p95_ms']:.3f}" for r in rows) + " ms</td></tr>")
    page = """<!doctype html><meta charset='utf-8'><title>Visual-first bank refinement</title>
<style>body{background:#16202a;color:#eee;max-width:1800px;margin:auto;padding:24px;font:17px system-ui;line-height:1.5}a{color:#8cf}img,video{width:100%;height:auto}th,td{text-align:left;padding:8px}summary{cursor:pointer}figure{margin:16px 0}</style>
<h1>Bank refinement: actual filled water in Cubey</h1>
<p>Read left to right: <b>current angular banks</b>, <b>new contour-coverage mask</b>, <b>existing B-spline 2x</b>.
Same saved simulation, lighting and camera. Blue is depth, not speed. Dots/trails are approximate velocity cues.</p>
<p>This is a visual demo: modest width/footprint changes are acceptable. The question is whether it looks smoother and stays believable in motion—not whether every shoreline is numerically exact.</p>
<h2>Start here: the late collection scene</h2><p>Look at the diagonal banks around the fork and the lower branch. The mask trims some sawteeth but retains faceting. B-spline rounds more, while widening/beading streams and smoothing displayed terrain too.</p>
<a href='overview.png'><img src='overview.png'></a>
<h2>Two bank details, original render pixels</h2>
<p>The same crop is used for all three. This separates cell-scale steps from overview/pixel aliasing.</p>
<a href='junction-detail.png'><img src='junction-detail.png'></a>
<a href='downstream-bank-detail.png'><img src='downstream-bank-detail.png'></a>
<h2>Motion: growth and recession</h2><p>Both clips are 12 seconds at 50x physical time. Native h/u change only every saved 60 physical seconds; dots move between those states. No field interpolation or hydraulic rerun.</p>"""
    for v in videos:
        page += f"<h3>{html.escape(v['path'].removesuffix('-comparison.mp4'))}</h3><video controls loop preload='metadata' poster='{v['poster']}' src='{v['path']}'></video>"
    page += "<h2>Measured rendering cost</h2><p>Standalone reversed-order repeats, 960x540, dots on; GPU p95 below. CPU mask bake/load and GPU upload are separate in the profile manifest. No concurrent CUDA solver is included.</p><table><tr><th>Mode</th><th>p95 repeat 1 / 2</th></tr>"+"".join(measured)+"</table>"
    page += "<details><summary>Terrain contact and runoff context</summary><img src='grazing-contact.png'><img src='runoff-overview.png'></details>"
    page += "<details><summary>Unsmoothed mask control and stronger static trial</summary><p>Raw-mask/light/moderate/strong are in the full still manifest. Strong is static-only: its contour linework crosses at rain-on 5100s and rain-off 7440s, so that motion bake was rejected. No stale masks or silent fallback.</p>"
    for mode in ("raw-mask","light","strong"):
        row = find_image(stills,mode,"rain-off")
        page += f"<h3>{mode}</h3><a href='../{row['path']}'><img src='../{row['path']}'></a>"
    page += "</details><p><a href='../RESULTS.md'>Verdict and limits</a> | <a href='../profile/manifest.json'>Separate costs</a> | <a href='../stills-final/manifest.json'>All stills and pre-edit parity</a> | <a href='../final-integrity.json'>Integrity receipt</a></p><p>Numerical fields/defaults unchanged. Prebaked coverage is not live support. Owner visual acceptance remains deferred; no commits or pushes.</p>"
    ref.write_text_exclusive(phase / "index.html",page)
    ref.write_json_exclusive(phase / "manifest.json", {"images":images,"videos":videos,
        "runtime_identity":stills["runtime_identity"],"human_visual_acceptance":"deferred"})


def validate(out):
    checkpoint = json.loads((out / "checkpoint/identity.json").read_text())
    identity = bank.identity()
    if checkpoint["runtime"]["inputs"] != identity["inputs"]:
        raise ValueError("native recording inputs changed")
    if checkpoint["runtime"]["source"]["head"] != identity["source"]["head"]:
        raise ValueError("HEAD changed without authorization")
    protected = {}
    own_initial = "projects/fluid/fluid_25d/run_native_bank_refinement_v1.py"
    for name,digest in checkpoint["prior_fluid_files"].items():
        if name not in candidate.MODIFIED and name != own_initial:
            if ref.sha256_file(ref.ROOT/name) != digest:
                raise ValueError("prior file changed outside scope: "+name)
            protected[name] = digest
    manifests = {}
    for phase in ("stills-final","clips","profile","review"):
        document = json.loads((out/phase/"manifest.json").read_text())
        manifests[phase] = document
        ref.assert_same_runtime(document["runtime_identity"], identity, phase)
        for row in document.get("assets",[]):
            path = out/row["path"]
            if ref.sha256_file(path) != row["sha256"]:
                raise ValueError("capture hash mismatch")
            if "review_video" in row and ref.sha256_file(out/row["review_video"]["path"]) != row["review_video"]["sha256"]:
                raise ValueError("review source hash mismatch")
    prior = json.loads((out/"prior-captures/manifest.json").read_text())
    parity = []
    if len(prior["assets"]) != 2 or len(manifests["stills-final"]["default_parity"]) != 2:
        raise ValueError("two pre-edit default controls required")
    for old in prior["assets"]:
        current = next(row for row in manifests["stills-final"]["assets"]
                       if row["mode"] == "reference" and row["id"] == old["id"]+"-default")
        old_digest = ref.sha256_file(out/"prior-captures"/old["path"])
        if old_digest != old["sha256"] or old_digest != current["sha256"]:
            raise ValueError("pre-edit control image changed")
        parity.append({"id":old["id"],"sha256":old_digest,"pass":True})
    for row in manifests["profile"]["rows"]:
        for kind in ("frames","passes"):
            if ref.sha256_file(out/"profile"/row["summary"][kind+"_csv"]) != row["summary"][kind+"_csv_sha256"]:
                raise ValueError("standalone profile CSV changed")
        if row["summary"]["measured_gpu_span_count"] != 108:
            raise ValueError("incomplete standalone profile")
        if row["mode"] == "moderate" and (row["upload_gpu"]["count"] != 10 or row["mask_load_cpu"]["count"] != 9):
            raise ValueError("missing mask load/upload measurements")
    bake_receipts = []
    for group, expected in (("bake-final",8),("bake-motion-moderate",2),("bake-repro-check",2)):
        paths = sorted((out/group).glob("*/coverage.json"))
        if len(paths) != expected:
            raise ValueError("incomplete canonical bake group: "+group)
        for path in paths:
            document = json.loads(path.read_text())
            case = path.parent.name.removesuffix("-raw-mask").removesuffix("-light").removesuffix("-moderate").removesuffix("-strong")
            source = ref.INPUT_ROOT/"recordings"/case/"recording.json"
            if ref.sha256_file(source) != document["source_manifest_sha256"]:
                raise ValueError("mask source manifest changed")
            source_rows = {r["time_s"]:r for r in json.loads(source.read_text())["frames"]}
            for row in document["frames"]:
                payload = path.parent/row["path"]
                if ref.sha256_file(payload) != row["sha256"] or payload.stat().st_size != row["bytes"] or row["source_frame_sha256"] != source_rows[row["time_s"]]["sha256"]:
                    raise ValueError("mask/source payload mismatch")
            bake_receipts.append({"path":str(path.relative_to(out)),"sha256":ref.sha256_file(path),"frames":len(document["frames"])})
    replay_checks = []
    for path in sorted((out/"bake-repro-check").glob("*/coverage.json")):
        document = json.loads(path.read_text())
        original = json.loads((out/"bake-final"/path.parent.name/"coverage.json").read_text())
        original_rows = {r["time_s"]:r for r in original["frames"]}
        for row in document["frames"]:
            if row["sha256"] != original_rows[row["time_s"]]["sha256"]:
                raise ValueError("final bake tools do not reproduce canonical mask")
            replay_checks.append({"case_variant":path.parent.name,"time_s":row["time_s"],"sha256":row["sha256"],"pass":True})
    if len(replay_checks) != 4:
        raise ValueError("four deterministic mask anchor checks required")
    rejections = json.loads((out/"rejection-check/manifest.json").read_text())
    if [(r["case"],r["time_s"]) for r in rejections["cases"]] != [("rain-on",5100),("rain-off",7440)]:
        raise ValueError("strong motion rejection controls missing")
    for row in rejections["cases"]:
        log = out/"rejection-check"/row["stderr_path"]
        if ref.sha256_file(log) != row["stderr_sha256"] or row["expected_error"] not in log.read_text():
            raise ValueError("strong rejection receipt mismatch")
    review = json.loads((out/"review/manifest.json").read_text())
    for row in review["images"] + review["videos"]:
        if ref.sha256_file(out/"review"/row["path"]) != row["sha256"]:
            raise ValueError("derived review media mismatch")
        if "probe" in row:
            ref.validate_probe(ref.ffprobe_video(out/"review"/row["path"],ignore_editlist=False),120,10,2880,578)
    links = re.findall(r"(?:href|src)='([^']+)'",(out/"review/index.html").read_text())
    for link in links:
        if not (out/"review"/link).resolve().is_file() and link != "../final-integrity.json":
            raise ValueError("broken local gallery link: "+link)
    tests = (out/"gates/ctest-dev.log").read_text()
    if len(re.findall(r"^Test Passed\.", tests, re.MULTILINE)) != 161 or "Test Failed." in tests:
        raise ValueError("full development gate missing")
    optional = (out/"gates/bake-controls.log").read_text()
    if "Ran 6 tests" not in optional or "OK" not in optional or "skipped" in optional:
        raise ValueError("optional raster controls did not execute")
    source_paths = sorted(candidate.MODIFIED | set(OWN_FILES))
    source_hashes = {name:ref.sha256_file(ref.ROOT/name) for name in source_paths}
    artifact_hashes = {str(p.relative_to(out)):ref.sha256_file(p) for p in sorted(out.rglob("*"))
                       if p.is_file() and (p.suffix in (".png",".mp4") or
                          p.name in ("manifest.json","RESULTS.md","protocol.json","preflight-notes.md","ctest-dev.log","bake-controls.log"))}
    ref.write_json_exclusive(out/"final-integrity.json", {"status":"pass","runtime_identity":identity,
        "native_inputs_unchanged":True,"head_unchanged":True,"default_pre_edit_pixel_parity":True,
        "default_parity_controls":parity,"deterministic_bake_replay_checks":replay_checks,
        "protected_prior_files":protected,"allowed_overlap":sorted(candidate.MODIFIED),
        "source_sha256":source_hashes,"artifact_sha256":artifact_hashes,
        "bake_sidecars":bake_receipts,"local_gallery_links":len(links),
        "strong_motion_rejection_controls":rejections["cases"],
        "comparison_clips_decoded":2,"development_tests":161,"optional_bake_tests_executed":6,
        "scope":"recording-only filled-water prototype; no live mask generation or default promotion",
        "human_visual_acceptance":"deferred","commits_pushes":"none"})
    print("Bank refinement handoff: PASS",flush=True)


def verify_rejections(out, bake_python):
    phase = out/"rejection-check"
    ref.reserve_directory(phase)
    rows = []
    for case,stamp in (("rain-on",5100),("rain-off",7440)):
        label = f"{case}-{stamp}-strong"
        command = ["rtk","proxy",str(bake_python),str(Path(candidate.__file__)),"bake",
                   "--out",str(out),"--variants","strong","--cases",case,"--times",str(stamp),
                   "--bake-leaf","bake-rejected-strong-check"]
        expected = f"self-intersecting display contour: {case}/{stamp}/strong"
        try:
            ref.run_logged(command,phase/"logs",label,timeout=180)
        except RuntimeError:
            log = phase/"logs"/(label+".stderr.txt")
            if not log.is_file() or expected not in log.read_text():
                raise
        else:
            raise ValueError("strong candidate unexpectedly passed; revisit the verdict")
        if (out/"bake-rejected-strong-check"/(case+"-strong")/"coverage.json").exists():
            raise ValueError("rejected bake published a completed sidecar")
        rows.append({"case":case,"time_s":stamp,"expected_error":expected,"command":command,
                     "stderr_path":str(log.relative_to(phase)),"stderr_sha256":ref.sha256_file(log),
                     "completed_sidecar_published":False})
        print("Reproduced explicit rejection: "+expected,flush=True)
    ref.write_json_exclusive(phase/"manifest.json",{"cases":rows,"scope":"strong contour candidate is not motion viable"})


def inspect_motion(out):
    """All ten published states, decoded from actual comparison videos."""
    phase = out / "motion-inspection"
    ref.reserve_directory(phase)
    rows = []
    for case, roi in (("rain-on",(570,250,300,190)),("rain-off",(295,258,300,155))):
        source = out/"review"/(case+"-comparison.mp4")
        x,y,w,h = roi
        filters = ["[0:v]select=eq(mod(n\\,12)\\,0),split=3[s0][s1][s2]"]
        for i in range(3):
            filters.append(f"[s{i}]crop={w}:{h}:{x+i*960}:{y}[c{i}]")
        filters.append("[c0][c1][c2]hstack=inputs=3,tile=2x5[v]")
        path = phase/(case+"-ten-states.png")
        ffmpeg(phase,case,["-i",str(source),"-filter_complex",";".join(filters),"-map","[v]","-frames:v","1",str(path)])
        rows.append({"case":case,"path":path.name,"sha256":ref.sha256_file(path),
                     "source_sha256":ref.sha256_file(source),"roi_xywh_each_tile":roi,
                     "frame_indices":list(range(0,120,12)),
                     "reading_order":"row-major; within each state: reference, moderate, B-spline 2x"})
    ref.write_json_exclusive(phase/"manifest.json",{"assets":rows,
        "scope":"decoded publication-state contact sheets, not full continuous video review"})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase",choices=("profile","review","inspect-motion","verify-rejections","validate"))
    parser.add_argument("--out",required=True,type=Path)
    parser.add_argument("--bake-python",type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    if out.parent != ref.OUTPUT_ROOT or not out.name.startswith("bank-refinement-v1-") or args.out.is_symlink():
        raise ValueError("invalid refinement output root")
    if args.phase == "verify-rejections":
        if args.bake_python is None:
            parser.error("verify-rejections requires --bake-python for the isolated environment")
        # Keep a virtualenv's interpreter path: resolving its symlink to the
        # system binary discards the environment's site-packages.
        verify_rejections(out,args.bake_python.absolute())
    else:
        {"profile":profile,"review":contact,"inspect-motion":inspect_motion,"validate":validate}[args.phase](out)


if __name__ == "__main__":
    main()
