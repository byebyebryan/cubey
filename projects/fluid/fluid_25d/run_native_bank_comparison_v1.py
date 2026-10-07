#!/usr/bin/env python3
"""Recorded three-mode demo handoff. Does not launch a solver or bake masks."""
from __future__ import annotations

import argparse
import csv
import html
import json
from pathlib import Path
import re
import shutil

import run_native_presentation_v1 as ref
import run_native_bank_render_v1 as bank
from review_native_bank_refinement_v1 import ffmpeg, stats

INPUT = ref.OUTPUT_ROOT / "bank-refinement-v1-20261006-ZmPvk9"
MODIFIED = {
    "projects/fluid/fluid_25d/CMakeLists.txt",
    "projects/fluid/fluid_25d/README.md",
    "projects/fluid/fluid_25d/fluid_25d_project_config.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_recording_app.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_display_coverage.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_display_coverage.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_tests.cpp",
}
MODES = ("reference", "bspline-2x", "marching-squares")
LABELS = {"reference": "Triangular reference", "bspline-2x": "B-spline 2x (experimental)",
          "marching-squares": "Marching-squares coverage (recorded)"}
START = {"rain-on": 4800, "rain-off": 7260}
STAMP = {"rain-on": 5340, "rain-off": 7800}
CAMERA = {"rain-on": "runoff", "rain-off": "collection"}


def mode_args(case, mode):
    return ["--fluid25d-native-surface-highlights", "off",
            "--fluid25d-native-bank-view", mode, "--fluid25d-bank-comparison",
            "--fluid25d-native-display-coverage",
            str(INPUT / "bake-motion-moderate" / f"{case}-moderate" / "coverage.json")]


def capture(out):
    phase = out / "capture"
    ref.reserve_directory(phase)
    before = bank.identity()
    rows = []
    for mode in MODES:
        leaf = phase / mode
        ref.reserve_directory(leaf)
        for case in ("rain-on", "rain-off"):
            for angle, pitch in (("default", -.92), ("above", -1.4), ("grazing", -.4)):
                asset = ref.still_asset(case, CAMERA[case], STAMP[case])
                asset.update(width=1920, height=1080)
                asset["id"] += "-" + angle
                row = bank.capture(asset, leaf, False, mode_args(case, mode) +
                                   ["--fluid25d-native-camera-pitch", str(pitch)])
                rows.append({**row, "mode": mode, "angle": angle,
                             "path": str((leaf / row["path"]).relative_to(out))})
            asset = ref.video_asset("growth" if case == "rain-on" else "recession",
                                    case, CAMERA[case], START[case], 120, 10, 5)
            row = bank.capture(asset, leaf, True, mode_args(case, mode))
            row["path"] = str((leaf / row["path"]).relative_to(out))
            row["review_video"]["path"] = str((leaf / row["review_video"]["path"]).relative_to(out))
            rows.append({**row, "mode": mode})
            print(f"Captured {mode}/{case}", flush=True)
    parity = []
    leaf = phase / "ordinary-default"
    ref.reserve_directory(leaf)
    prior = json.loads((out / "prior-captures/manifest.json").read_text())
    for old in prior["assets"]:
        asset = {k: old[k] for k in ("id", "kind", "case", "camera", "requested_time_s",
                                    "saved_field_time_s", "width", "height")}
        row = bank.capture(asset, leaf, False, ["--fluid25d-native-surface-highlights", "off"])
        if row["sha256"] != old["sha256"]:
            raise ValueError("ordinary default changed from pre-edit capture")
        parity.append({"id": row["id"], "path": str((leaf / row["path"]).relative_to(out)),
                       "sha256": row["sha256"], "pass": True})
    ref.assert_same_runtime(before, bank.identity(), "comparison captures")
    ref.write_json_exclusive(phase / "manifest.json", {"runtime_identity": before,
        "assets": rows, "default_parity": parity, "human_visual_acceptance": "deferred"})


def profile(out):
    phase = out / "profile"
    ref.reserve_directory(phase)
    before = bank.identity()
    rows = []
    for repeat in (1, 2):
        for mode in (MODES if repeat == 1 else tuple(reversed(MODES))):
            asset = ref.video_asset(f"profile-{mode}-{repeat}", "rain-off", "collection", 7260, 120, 30, 5)
            prefix = phase / asset["id"]
            command = ref.app_command(asset, prefix.with_suffix(".mp4"), "readable", True, prefix)
            command += mode_args("rain-off", mode)
            _, receipt = ref.run_logged(command, phase / "logs", asset["id"], timeout=300,
                                       expected_asset=asset, expected_presentation="readable")
            with prefix.with_suffix(".passes.csv").open() as f:
                uploads = [float(v["duration_ms"]) for v in csv.DictReader(f)
                           if v["kind"] == "gpu" and v["label"] == "fluid_25d display coverage upload"]
            text = (phase / "logs" / (asset["id"] + ".stdout.txt")).read_text()
            ready = re.search(r"bank_cache: READY frames=(\d+) bytes=(\d+) preparation_ms=(\S+)", text)
            if not ready or "display_coverage_load:" in text:
                raise ValueError("comparison was not fully preloaded before playback")
            count, size, preparation = int(ready[1]), int(ready[2]), float(ready[3])
            if count != 10 or size != 167281000 or len(uploads) != (10 if mode == "marching-squares" else 0):
                raise ValueError("cache/upload count mismatch")
            row = {"mode": mode, "repeat": repeat, "command": receipt,
                   "summary": ref.profile_summary(prefix, 12, 108), "upload_gpu": stats(uploads),
                   "cache_frames": count, "cache_bytes": size, "preparation_cpu_ms": preparation,
                   "payload_budget_bytes": 256 * 1024**2, "playback_mask_file_loads": 0}
            rows.append(row)
            print(f"Standalone {mode}/{repeat}: p95 {row['summary']['gpu_p95_ms']:.3f} ms; "
                  f"cache prep {preparation:.1f} ms", flush=True)
    ref.assert_same_runtime(before, bank.identity(), "comparison profiles")
    ref.write_json_exclusive(phase / "manifest.json", {"runtime_identity": before, "rows": rows,
        "scope": "960x540, dots on, 120 replay frames/12 warmup/108 GPU spans; reversed second round; standalone, no concurrent test/bake/CUDA",
        "note": "GPU total includes mask uploads on publication frames. Cache preparation is CPU file/digest/validation time. Retained payload budget is not total process RSS. Frame delta is playback cadence, not performance."})


def review(out):
    phase = out / "review"
    ref.reserve_directory(phase)
    captures = json.loads((out / "capture/manifest.json").read_text())
    profiles = json.loads((out / "profile/manifest.json").read_text())
    gui = json.loads((out / "gui/manifest.json").read_text())
    for document in (captures, profiles, gui):
        ref.assert_same_runtime(document["runtime_identity"], bank.identity(), "review")
    font = ref.find_font()
    images, videos = [], []
    def image(mode, case, angle):
        return next(v for v in captures["assets"] if v["kind"] == "still" and
                    v["mode"] == mode and v["case"] == case and v["angle"] == angle)
    for name, case, angle, crop in (
        ("overview", "rain-off", "default", None),
        ("junction-detail", "rain-off", "default", (590, 440, 600, 310)),
        ("downstream-detail", "rain-off", "default", (760, 640, 450, 410)),
        ("grazing", "rain-off", "grazing", None),
        ("growth-overview", "rain-on", "default", None)):
        args, filters = [], []
        for i, mode in enumerate(MODES):
            args += ["-i", str(out / image(mode, case, angle)["path"])]
            transform = f"crop={crop[2]}:{crop[3]}:{crop[0]}:{crop[1]}" if crop else "scale=640:360"
            filters.append(f"[{i}:v]{transform},pad=iw:ih+42:0:42:color=0x16202a,"
                           f"drawtext=fontfile={font}:text='{LABELS[mode]}':x=10:y=10:fontsize=18:fontcolor=white[c{i}]")
        filters.append("[c0][c1][c2]hstack=inputs=3[v]")
        path = phase / (name + ".png")
        ffmpeg(phase, name, [*args, "-filter_complex", ";".join(filters), "-map", "[v]", "-frames:v", "1", str(path)])
        images.append({"path": path.name, "sha256": ref.sha256_file(path), "crop_xywh": crop})
    for case in ("rain-on", "rain-off"):
        args, filters, sources = [], [], []
        for i, mode in enumerate(MODES):
            row = next(v for v in captures["assets"] if v["kind"] == "video" and v["mode"] == mode and v["case"] == case)
            source = out / row["review_video"]["path"]
            if ref.sha256_file(source) != row["review_video"]["sha256"]:
                raise ValueError("clip hash changed")
            args += ["-i", str(source)]
            filters.append(f"[{i}:v]pad=iw:ih+38:0:38:color=0x16202a,"
                           f"drawtext=fontfile={font}:text='{LABELS[mode]}':x=10:y=10:fontsize=18:fontcolor=white[c{i}]")
            sources.append(row)
        if any(r["timeline"] != sources[0]["timeline"] for r in sources):
            raise ValueError("comparison timelines differ")
        filters.append("[c0][c1][c2]hstack=inputs=3[v]")
        path = phase / (case + "-comparison.mp4")
        ffmpeg(phase, case, [*args, "-filter_complex", ";".join(filters), "-map", "[v]", "-an",
                            "-c:v", "libx264", "-crf", "20", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(path)])
        probe = ref.validate_probe(ref.ffprobe_video(path, ignore_editlist=False), 120, 10, 2880, 578)
        for frame in (0, 60, 119):
            ffmpeg(phase, case + f"-frame-{frame}", ["-i", str(path), "-vf", f"select=eq(n\\,{frame})",
                    "-frames:v", "1", str(phase / f"{case}-decoded-{frame:03d}.png")])
        # Inspect every published native state in a local bank crop, not just a promotional still.
        x, y, w, h = (570, 250, 300, 190) if case == "rain-on" else (295, 258, 300, 155)
        filters = ["[0:v]select=eq(mod(n\\,12)\\,0),split=3[s0][s1][s2]"]
        filters += [f"[s{i}]crop={w}:{h}:{x+i*960}:{y}[c{i}]" for i in range(3)]
        filters.append("[c0][c1][c2]hstack=inputs=3,tile=2x5[v]")
        ffmpeg(phase, case + "-ten-states", ["-i", str(path), "-filter_complex", ";".join(filters),
                    "-map", "[v]", "-frames:v", "1", str(phase / (case + "-ten-states.png"))])
        videos.append({"path": path.name, "sha256": ref.sha256_file(path), "probe": probe,
                       "timeline": sources[0]["timeline"]})
    page = """<!doctype html><meta charset='utf-8'><title>Three bank views</title>
<style>body{background:#16202a;color:#eee;max-width:1800px;margin:auto;padding:24px;font:17px system-ui;line-height:1.5}a{color:#8cf}img,video{width:100%;height:auto}td,th{padding:8px;text-align:left}summary{cursor:pointer}</style>
<h1>Three bank views, one water simulation</h1>
<p>Left: triangular reference. Middle: experimental B-spline 2x. Right: recorded marching-squares coverage.
Blue means depth, not speed. White dots/trails are approximate velocity cues, not conserved particles.</p>
<table><tr><th>Reference</th><th>B-spline 2x</th><th>Marching-squares coverage</th></tr>
<tr><td>Most direct baseline; angular cell-scale banks.</td><td>Rounder banks; changes displayed terrain/water, may widen or bead streams and visually connect gaps.</td><td>Softens local sawteeth without changing the reference bed mesh; still faceted. Bounded, prerecorded masks only.</td></tr></table>
<h2>Start here: the fork and downstream banks</h2><a href='overview.png'><img src='overview.png'></a>
<p>Original render-pixel crops, identical camera and saved field:</p><img src='junction-detail.png'><img src='downstream-detail.png'>
<h2>Motion</h2><p>Two 12-second clips, 50x playback. Native depth/velocity update at saved 60-second intervals; dots move between those states. The viewer holds at the comparison window end unless you deliberately enable replay looping. No solver rerun or temporal field interpolation.</p>"""
    for case in ("rain-on", "rain-off"):
        page += f"<h3>{case}</h3><video controls preload='metadata' poster='{case}-decoded-060.png' src='{case}-comparison.mp4'></video>"
    page += "<h2>Controls in the actual viewer</h2><p>Bank view [S] switches presentation without resetting camera, playback or cues. Dots [W] are independent. Space plays/pauses; at the window end it replays from the start. R rewinds. Raw 2D maps bypass all bank reconstruction.</p>"
    for row in gui["captures"]:
        page += f"<details><summary>{html.escape(row['label'])}</summary><img src='../gui/{row['path']}'></details>"
    page += "<h2>Standalone costs</h2><p>960x540 with dots; GPU p95 for two reversed-order repeats. CPU preload and GPU upload are reported separately. The ~160 MiB retained cache is not total process memory. This is not a full GUI or concurrent solver frame-budget claim.</p><table><tr><th>Mode</th><th>GPU p95 ms</th><th>CPU preload ms</th></tr>"
    for mode in MODES:
        rows = [r for r in profiles["rows"] if r["mode"] == mode]
        page += f"<tr><td>{LABELS[mode]}</td><td>" + " / ".join(f"{r['summary']['gpu_p95_ms']:.3f}" for r in rows) + "</td><td>" + " / ".join(f"{r['preparation_cpu_ms']:.1f}" for r in rows) + "</td></tr>"
    page += "</table><details><summary>Grazing contact, growth context and all ten saved states</summary><img src='grazing.png'><img src='growth-overview.png'><img src='rain-on-ten-states.png'><img src='rain-off-ten-states.png'></details><p><a href='../RESULTS.md'>Verdict and limits</a> | <a href='../capture/manifest.json'>Capture provenance</a> | <a href='../profile/manifest.json'>Separate costs</a> | <a href='../final-integrity.json'>Integrity receipt</a></p><p>Defaults and numerical inputs unchanged. Owner visual acceptance deferred. No commits or pushes.</p>"
    ref.write_text_exclusive(phase / "index.html", page)
    ref.write_json_exclusive(phase / "manifest.json", {"runtime_identity": captures["runtime_identity"],
        "images": images, "videos": videos, "human_visual_acceptance": "deferred"})


def validate(out):
    checkpoint = json.loads((out / "checkpoint/identity.json").read_text())
    identity = bank.identity()
    if checkpoint["runtime"]["inputs"] != identity["inputs"] or checkpoint["runtime"]["source"]["head"] != identity["source"]["head"]:
        raise ValueError("native inputs or HEAD changed")
    protected = {}
    for name, digest in checkpoint["prior_fluid_files"].items():
        if name not in MODIFIED and not name.endswith("run_native_bank_comparison_v1.py"):
            if ref.sha256_file(ref.ROOT / name) != digest:
                raise ValueError("prior file changed outside scope: " + name)
            protected[name] = digest
    for name, digest in checkpoint["mask_input_sha256"].items():
        if ref.sha256_file(INPUT / name) != digest:
            raise ValueError("reused mask input changed: " + name)
    manifests = {}
    for phase in ("capture", "profile", "gui", "review"):
        document = json.loads((out / phase / "manifest.json").read_text())
        manifests[phase] = document
        ref.assert_same_runtime(document["runtime_identity"], identity, phase)
    prior = json.loads((out / "prior-captures/manifest.json").read_text())
    if len(manifests["capture"]["default_parity"]) != 2 or len(prior["assets"]) != 2:
        raise ValueError("two pre-edit default controls required")
    for old in prior["assets"]:
        new = next(r for r in manifests["capture"]["default_parity"] if r["id"] == old["id"])
        if ref.sha256_file(out / "prior-captures" / old["path"]) != old["sha256"] or ref.sha256_file(out / new["path"]) != old["sha256"]:
            raise ValueError("default image parity failed")
    if len(manifests["capture"]["assets"]) != 24:
        raise ValueError("18 stills and six matched clips required")
    for row in manifests["capture"]["assets"]:
        if ref.sha256_file(out / row["path"]) != row["sha256"]:
            raise ValueError("capture media changed")
        if row["kind"] == "video":
            video = row["review_video"]
            if ref.sha256_file(out / video["path"]) != video["sha256"]:
                raise ValueError("captioned clip changed")
            ref.validate_probe(ref.ffprobe_video(out / video["path"], ignore_editlist=False), 120, 10)
    for row in manifests["profile"]["rows"]:
        for kind in ("frames", "passes"):
            if ref.sha256_file(out / "profile" / row["summary"][kind + "_csv"]) != row["summary"][kind + "_csv_sha256"]:
                raise ValueError("profile CSV changed")
        if row["summary"]["measured_gpu_span_count"] != 108 or row["playback_mask_file_loads"] != 0:
            raise ValueError("incomplete profile/preload control")
    for row in manifests["gui"]["captures"]:
        if ref.sha256_file(out / "gui" / row["path"]) != row["sha256"]:
            raise ValueError("GUI capture changed")
    for row in manifests["review"]["images"] + manifests["review"]["videos"]:
        if ref.sha256_file(out / "review" / row["path"]) != row["sha256"]:
            raise ValueError("derived review media changed")
        if "probe" in row:
            ref.validate_probe(ref.ffprobe_video(out / "review" / row["path"], ignore_editlist=False), 120, 10, 2880, 578)
    for link in re.findall(r"(?:href|src)='([^']+)'", (out / "review/index.html").read_text()):
        if link != "../final-integrity.json" and not (out / "review" / link).resolve().is_file():
            raise ValueError("broken gallery link: " + link)
    tests = (out / "gates/ctest-dev.log").read_text()
    passed = re.findall(r"^\s*(\d+)/162 Test\s+#\d+:.+Passed\s", tests, re.MULTILINE)
    if "100% tests passed" not in tests or sorted(map(int, passed)) != list(range(1, 163)) or "***Failed" in tests:
        raise ValueError("full development gate missing")
    own = MODIFIED | {"projects/fluid/fluid_25d/" + p for p in
                     ("run_native_bank_comparison_v1.py", "test_bank_comparison_gpu_v1.py", "probe_bank_comparison_gui_v1.py")} | {
                     "projects/fluid/sim/fluid_25d/fluid_25d_bank_comparison.h", "projects/fluid/sim/fluid_25d/fluid_25d_bank_comparison.cpp"}
    artifacts = {str(p.relative_to(out)): ref.sha256_file(p) for p in sorted(out.rglob("*"))
                 if p.is_file() and (p.suffix in (".png", ".mp4", ".csv") or
                     p.name in ("manifest.json", "RESULTS.md", "ctest-dev.log", "preflight-notes.md"))}
    ref.write_json_exclusive(out / "final-integrity.json", {"status": "pass", "runtime_identity": identity,
        "native_inputs_unchanged": True, "head_unchanged": True, "default_pre_edit_pixel_parity": True,
        "protected_prior_files": protected, "reused_mask_inputs_unchanged": True,
        "source_sha256": {p: ref.sha256_file(ref.ROOT / p) for p in sorted(own)},
        "artifact_sha256": artifacts, "development_tests": 162,
        "scope": "recorded comparison windows, no live reconstruction or solver/default changes",
        "human_visual_acceptance": "deferred", "commits_pushes": "none"})
    print("Bank comparison handoff: PASS", flush=True)


def checkpoint(out):
    phase = out / "checkpoint"
    ref.reserve_directory(phase)
    hashes = {}
    for name in ref.git_output("ls-files", "--cached", "--others", "--exclude-standard", "projects/fluid").splitlines():
        source = ref.ROOT / name
        if source.is_file():
            hashes[name] = ref.sha256_file(source)
            if name in MODIFIED:
                dest = phase / "prior-source" / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, dest)
    patch = ref.rtk_output("git", "diff", "--binary", "HEAD")
    if patch.returncode:
        raise RuntimeError("checkpoint diff failed")
    ref.write_text_exclusive(phase / "prior.patch", patch.stdout)
    masks = {str(p.relative_to(INPUT)): ref.sha256_file(p)
             for p in (INPUT / "bake-motion-moderate").rglob("*") if p.is_file()}
    ref.write_json_exclusive(phase / "identity.json", {"runtime": bank.identity(),
        "prior_fluid_files": hashes, "allowed_overlap": sorted(MODIFIED), "mask_input_sha256": masks,
        "commits_pushes": "not authorized", "human_visual_acceptance": "deferred"})
    captures = out / "prior-captures"
    ref.reserve_directory(captures)
    rows = []
    for case, camera, stamp in (("rain-on", "runoff", 6000),("rain-off", "collection",14400)):
        asset = ref.still_asset(case,camera,stamp)
        asset.update(width=1920,height=1080)
        rows.append(bank.capture(asset,captures,False,["--fluid25d-native-surface-highlights","off"]))
    ref.write_json_exclusive(captures / "manifest.json", {"assets": rows})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("checkpoint", "capture", "profile", "review", "validate"))
    parser.add_argument("--out",required=True,type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    if out.parent != ref.OUTPUT_ROOT or not out.name.startswith("bank-comparison-v1-") or args.out.is_symlink():
        raise ValueError("use a fresh bank-comparison-v1-* leaf under outputs/fluid")
    {"checkpoint": checkpoint, "capture": capture, "profile": profile,
     "review": review, "validate": validate}[args.phase](out)


if __name__ == "__main__":
    main()
