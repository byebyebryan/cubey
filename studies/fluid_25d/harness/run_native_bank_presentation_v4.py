#!/usr/bin/env python3
"""Quiet water review and shoreline gates using pinned, immutable recordings."""
from __future__ import annotations

import argparse
import html
import json
from pathlib import Path

import run_native_bank_render_v1 as bank
import run_native_bank_reconstruction_v2 as reconstruction
import run_native_presentation_v1 as reference
import run_native_shoreline_raster_v1 as raster

MODES = (("triangular", 1), ("bilinear", 1), ("bspline", 2), ("bspline", 4))
V3 = reference.OUTPUT_ROOT / "bank-validation-v3-20261005-WSLDl6"


def identity() -> dict:
    result = bank.identity()
    result["study_tools_sha256"] = {p.name: reference.sha256_file(p) for p in
                                    (Path(__file__), Path(raster.__file__))}
    return result


def still_plan() -> list[dict]:
    assets = [reference.still_asset("rain-on", "runoff", 6000),
              reference.still_asset("rain-off", "collection", 14400),
              reference.still_asset("rain-off", "collection", 9600)]
    for asset in assets:
        asset.update(width=1920, height=1080)
    return assets


def video_plan() -> list[dict]:
    return [reference.video_asset("runoff", "rain-on", "runoff", 4800, 91, 15, 5),
            reference.video_asset("recession", "rain-off", "collection", 7260, 61, 8, 60)]


def extra(sampling: str, subdivision: int, highlights: str) -> list[str]:
    if (sampling, subdivision) not in MODES or highlights not in ("auto", "on", "off"):
        raise ValueError("unsupported presentation study option")
    return reconstruction.mode_args(sampling, subdivision) + [
        "--fluid25d-native-surface-highlights", highlights]


def capture(root: Path) -> None:
    phase = root / "capture"
    reference.reserve_directory(phase)
    before = identity()
    reference.write_json_exclusive(phase/"runtime-before.json",before)
    rows = []
    for sampling, subdivision in MODES:
        name = reconstruction.mode_name(sampling, subdivision)
        for policy in (("on", "auto", "off") if sampling == "triangular" else ("off",)):
            leaf = phase / f"{name}-{policy}"
            reference.reserve_directory(leaf)
            for asset in still_plan():
                row = bank.capture(dict(asset), leaf, False, extra(sampling, subdivision, policy))
                rows.append({**row, "mode": name, "highlights": policy,
                             "path": str((leaf / row["path"]).relative_to(root))})
            if (sampling == "triangular" and policy in ("on", "off")) or (sampling == "bspline" and subdivision == 2):
                for asset in video_plan():
                    row = bank.capture(dict(asset), leaf, True, extra(sampling, subdivision, policy))
                    row["review_video"]["path"] = str((leaf / row["review_video"]["path"]).relative_to(root))
                    rows.append({**row, "mode": name, "highlights": policy,
                                 "path": str((leaf / row["path"]).relative_to(root))})
    # Reproduce exact V3 stills, not merely similar scenes, before accepting
    # the independent toggle. On must retain legacy; Off must match ablation.
    parity = []
    for sampling, subdivision in (("triangular",1), ("bilinear",1), ("bspline",4)):
        name = reconstruction.mode_name(sampling, subdivision)
        for policy, variant in (("on","baseline"), ("off","cues-off")):
            asset = reference.still_asset("rain-off", "collection", 14400)
            asset.update(width=1920,height=1080)
            leaf = phase / f"parity-{name}-{policy}"
            reference.reserve_directory(leaf)
            row = bank.capture(asset, leaf, False, extra(sampling, subdivision, policy))
            old = V3 / (f"confirmation-{sampling}-{variant}" if sampling != "bspline" else variant) / "media" / (asset["id"] + ".png")
            expected = reference.sha256_file(old)
            if row["sha256"] != expected:
                raise ValueError(f"V3 appearance parity failed: {name}/{policy}")
            parity.append({"mode":name,"highlights":policy,"pass":True,"sha256":expected,
                           "prior_path":str(old.relative_to(reference.ROOT)),
                           "current_path":str((leaf/row["path"]).relative_to(root))})
    for asset in still_plan():
        matches = [r for r in rows if r["id"] == asset["id"] and r["mode"] == "triangular"
                   and r["highlights"] in ("auto","off")]
        if len(matches) != 2 or matches[0]["sha256"] != matches[1]["sha256"]:
            raise ValueError("Readable Auto did not resolve to Off")
    truth = []
    leaf = phase / "truth"
    reference.reserve_directory(leaf)
    for diagnostic in ("depth", "flow", "wet-dry"):
        asset = reference.diagnostic_asset("rain-off", diagnostic, 14400)
        image = leaf / (asset["id"] + ".png")
        command = reference.app_command(asset,image,"readable",False)
        command += ["--fluid25d-native-surface-highlights","off"]
        _,receipt = reference.run_logged(command,leaf/"logs",asset["id"],expected_asset=asset,expected_presentation="readable")
        truth.append({**asset,"path":str(image.relative_to(root)),"sha256":reference.sha256_file(image),"command":receipt})
    for sampling, subdivision in MODES:
        asset = reference.still_asset("rain-off","collection",14400)
        asset["id"] += "-" + reconstruction.mode_name(sampling,subdivision) + "-contours"
        row = bank.capture(asset,leaf,False,extra(sampling,subdivision,"off") + ["--fluid25d-native-water-debug","contours"])
        truth.append({**row,"path":str((leaf/row["path"]).relative_to(root))})
    after = identity()
    reference.write_json_exclusive(phase/"runtime-after.json",after)
    reference.assert_same_runtime(before,after,"quiet bank captures")
    if before["fluid_source_sha256"] != after["fluid_source_sha256"]:
        raise ValueError("source changed during capture")
    reference.write_json_exclusive(phase/"manifest.json",{
        "schema":"cubey.fluid25d.bank_presentation.v4","runtime_identity":before,
        "runtime_after":after,"assets":rows,"truth":truth,"v3_parity":parity,
        "hydraulics":"immutable recordings, zero hydraulic dispatches", "human_visual_acceptance":"deferred"})


def profile(root: Path) -> None:
    phase = root / "profile"
    reference.reserve_directory(phase)
    before = identity()
    reference.write_json_exclusive(phase/"runtime-before.json",before)
    rows = []
    for sampling, subdivision in MODES:
        name = reconstruction.mode_name(sampling,subdivision)
        asset = reference.video_asset("profile-"+name,"rain-off","runoff",6000,120,30,5)
        asset["profile_warmup_frames"] = 12
        prefix = phase / name
        command = reference.app_command(asset,prefix.with_suffix(".mp4"),"readable",True,prefix)
        command += extra(sampling,subdivision,"off")
        _,receipt = reference.run_logged(command,phase/"logs",name,timeout=600,expected_asset=asset,expected_presentation="readable")
        measured = reference.profile_summary(prefix,12,108)
        rows.append({"mode":name,"highlights":"off","motion_markers":True,"command":receipt,
                     **measured,"passes_1ms_gpu_gate":measured["gpu_p95_ms"] <= 1.0})
    after = identity()
    reference.write_json_exclusive(phase/"runtime-after.json",after)
    reference.assert_same_runtime(before,after,"quiet bank profiles")
    reference.write_json_exclusive(phase/"manifest.json",{
        "runtime_identity":before,"profiles":rows,
        "scope":"native presentation total, 960x540, 120 frames/12 warmup/108 measured; markers on; excludes concurrent CUDA cost"})
    for row in rows:
        if row["mode"] in ("triangular","bspline-2x") and not row["passes_1ms_gpu_gate"]:
            raise ValueError(f"practical presentation budget missed: {row['mode']}")


def validate_media(root: Path, assets: list[dict]) -> None:
    for row in assets:
        path = root / row["path"]
        if path.is_symlink() or reference.sha256_file(path) != row["sha256"]:
            raise ValueError("media provenance mismatch")
        if "review_video" in row:
            review = row["review_video"]
            path = root / review["path"]
            if reference.sha256_file(path) != review["sha256"]:
                raise ValueError("review clip provenance mismatch")
            reference.validate_probe(reference.ffprobe_video(path,ignore_editlist=False),row["frame_count"],row["fps"])


def review(root: Path) -> None:
    phase = root / "review"
    reference.reserve_directory(phase)
    manifest = json.loads((root/"capture/manifest.json").read_text())
    profile_manifest = json.loads((root/"profile/manifest.json").read_text())
    reference.assert_same_runtime(manifest["runtime_identity"],profile_manifest["runtime_identity"],"review inputs")
    shoreline = json.loads((root/"shoreline/report.json").read_text())
    validate_media(root,manifest["assets"]+manifest["truth"])
    rows = manifest["assets"]
    sections = []
    for planned in still_plan():
        pair = [next(r for r in rows if r["id"]==planned["id"] and r["mode"]=="triangular" and r["highlights"]==policy)
                for policy in ("on","off")]
        output = phase / (planned["id"]+"-comparison.png")
        font = reference.find_font()
        comparison_filter = (
            f"[0:v]drawtext=fontfile={font}:text='Legacy procedural highlights - ON':x=12:y=12:fontsize=24:fontcolor=white:box=1:boxcolor=black@0.8[a];"
            f"[1:v]drawtext=fontfile={font}:text='Quiet water - OFF / Readable AUTO':x=12:y=12:fontsize=24:fontcolor=white:box=1:boxcolor=black@0.8[b];"
            "[a][b]hstack=inputs=2[v]")
        reference.run_logged(["rtk","proxy","ffmpeg","-nostdin","-n","-v","error",
            "-i",str(root/pair[0]["path"]),"-i",str(root/pair[1]["path"]),
            "-filter_complex",comparison_filter,"-map","[v]","-frames:v","1",str(output)],
            phase/"logs",planned["id"],timeout=60)
        sections.append(f"<h2>{html.escape(planned['id'])}</h2><p>Left: legacy highlights On. Right: quiet Off / Readable Auto. Triangular banks unchanged.</p><a href='{output.name}'><img src='{output.name}'></a>")
    clips = []
    for planned in video_plan():
        pair = [next(r for r in rows if r["id"]==planned["id"] and r["mode"]=="triangular" and r["highlights"]==policy)
                for policy in ("on","off")]
        output = phase / (planned["id"]+"-comparison.mp4")
        font = reference.find_font()
        comparison_filter = (
            f"[0:v]drawtext=fontfile={font}:text='Highlights ON':x=12:y=62:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.8[a];"
            f"[1:v]drawtext=fontfile={font}:text='Highlights OFF - dots retained':x=12:y=62:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.8[b];"
            "[a][b]hstack=inputs=2[v]")
        reference.run_logged(["rtk","proxy","ffmpeg","-nostdin","-n","-v","error",
            "-i",str(root/pair[0]["review_video"]["path"]),"-i",str(root/pair[1]["review_video"]["path"]),
            "-filter_complex",comparison_filter,"-map","[v]","-an",
            "-c:v","libx264","-crf","20","-pix_fmt","yuv420p",str(output)],phase/"logs",planned["id"],timeout=120)
        probe = reference.validate_probe(reference.ffprobe_video(output,ignore_editlist=False),planned["frame_count"],planned["fps"],1920,540)
        # Decode only normalized review clips: raw capture MP4 edit lists are
        # not used as presentation-time selection oracles.
        poster = output.with_suffix(".png")
        reference.run_logged(["rtk","proxy","ffmpeg","-nostdin","-n","-v","error","-i",str(output),
            "-vf",f"select=eq(n\\,{planned['frame_count']//2})","-frames:v","1",str(poster)],phase/"logs",planned["id"]+"-poster",timeout=60)
        clips.append({"path":str(output.relative_to(root)),"sha256":reference.sha256_file(output),"probe":probe,
                      "poster":str(poster.relative_to(root)),"poster_sha256":reference.sha256_file(poster)})
        sections.append(f"<h2>{planned['id']} with dots/trails</h2><p>Left On, right Off. Hydraulic fields held at saved 60-second cadence; dots are approximate velocity cues, not tracked water parcels.</p><video controls loop preload='metadata' poster='{poster.name}' src='{output.name}'></video>")
    matrix = ""
    for sampling,subdivision in MODES:
        name = reconstruction.mode_name(sampling,subdivision)
        collection = next(r for r in rows if r["id"]=="rain-off-collection-14400s" and r["mode"]==name and r["highlights"]=="off")
        matrix += f"<figure><figcaption>{name} (highlights Off)</figcaption><a href='../{collection['path']}'><img src='../{collection['path']}'></a></figure>"
    failures = ""
    for name in ("triangular","bspline-4x"):
        row = next(r for r in shoreline["assets"] if r["case"]=="positive-film-gap" and r["mode"]==name)
        failures += f"<figure><figcaption>{name}: >=26 mm depth band; thin film remains numerically wet</figcaption><img src='../shoreline/{row['path']}'></figure>"
    reference.write_text_exclusive(phase/"index.html","<!doctype html><meta charset='utf-8'><title>Quiet water review</title>"
        "<style>body{background:#16202a;color:#eee;font:16px system-ui;margin:24px}img,video{width:100%;max-width:1920px}figure{margin:18px 0}a{color:#8cf}</style>"
        "<h1>Quiet water: reversible readability change, not a new shoreline solver</h1>"
        "<p>Procedural highlights are independently switchable. Readable Auto now disables them; Original/Motion Auto preserve them. Dots/trails remain available. Same terrain, hydraulic fields, depth palette and 2–50 mm film opacity.</p>"
        "<p>The coarse 30 m triangular banks remain. B-spline smooths BOTH displayed terrain and water and can widen/connect streams; it is opt-in and NOT promoted. Human visual acceptance deferred.</p>"
        + "".join(sections) + "<details><summary>Quiet bank matrix (experimental)</summary>"+matrix+"</details>"
        + "<details><summary>Known topology counterexample and diagnostic truth</summary>"+failures
        + "<p>Harness passes by detecting the known failure. Candidate promotion remains rejected. Lake-stage precision is checked separately by GPU/CPU probes, not inferred from these images.</p>"
        + "".join(f"<a href='../{a['path']}'>{html.escape(a['id'])}</a><br>" for a in manifest["truth"])
        + "</details><p>See ../RESULTS.md, ../capture/manifest.json, ../profile/manifest.json and ../shoreline/report.json for provenance and gates.</p>")
    reference.write_json_exclusive(phase/"manifest.json",{"clips":clips,"human_visual_acceptance":"deferred",
        "parity":manifest["v3_parity"],"candidate_promotion":"rejected","source_manifest":"../capture/manifest.json"})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase",choices=("capture","profile","shoreline","review"))
    parser.add_argument("--out",type=Path,required=True)
    args = parser.parse_args()
    root = args.out.resolve()
    if args.out.is_symlink() or root.parent != reference.OUTPUT_ROOT.resolve() or not root.name.startswith("bank-presentation-v4-"):
        raise ValueError("output must be a fresh bank-presentation-v4-* leaf under outputs/fluid")
    if args.phase == "shoreline":
        raster.run(reference.APP,root/"shoreline")
    else:
        {"capture":capture,"profile":profile,"review":review}[args.phase](root)
    print(root / args.phase)


if __name__ == "__main__":
    main()
