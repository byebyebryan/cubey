#!/usr/bin/env python3
"""Exclusive, runtime-bound terrain auditions using immutable native fields."""
from __future__ import annotations

import argparse
import json
import re
import statistics
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

import run_native_presentation_v1 as ref
from review_scenic_material_v2 import effective_material

SIM = Path("projects/fluid/sim/fluid_25d")
LOCAL = Path("projects/fluid/fluid_25d")
SOURCE_PATHS = [*(SIM / name for name in (
    "fluid_25d_recording_app.cpp", "fluid_25d_scenic.cpp", "fluid_25d_scenic.h",
    "fluid_25d_scenic_material.h", "fluid_25d_scenic_material.cpp", "fluid_25d_tests.cpp",
    "fluid_25d_surface_gradient_gpu_tests.cpp", "shaders/fluid_25d_scenic_terrain.frag",
    "shaders/fluid_25d_scenic_terrain_legacy.frag", "shaders/fluid_25d_surface_gradient.glsl",
    "shaders/fluid_25d_surface_gradient_test.comp")),
    *(LOCAL / name for name in ("CMakeLists.txt", "fluid_25d_project_config.h",
    "run_mountain_rain_demo.py", "probe_mountain_rain_demo.py", "test_scenic_gpu_v1.py",
    "review_terrain_material_v3.py", "test_review_terrain_material_v3.py")),
    Path("third_party/surface_gradient/README.md")]


def source_files():
    return {str(path): ref.sha256_file(ref.ROOT / path) for path in SOURCE_PATHS}


def checked_label(label):
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,63}", label):
        raise ValueError("phase label must be a plain lowercase leaf")
    return label


def camera_args(asset, yaw=None, sweep=None):
    args = ["--fluid25d-native-camera-yaw", str(yaw)] if yaw is not None else []
    if sweep is not None and asset["kind"] == "video":
        args += ["--fluid25d-native-camera-sweep", str(sweep)]
    return args


def check_phase_sources(out, pins):
    # Orchestration changes need not invalidate renderer-identical captures.
    # Only these two tools may match an exact retained pre-fix source snapshot;
    # renderer, shader, launcher and fixture code must still match live source.
    archives = {str(LOCAL/"review_terrain_material_v3.py"): "review-helper-before-sweep-fix.py",
                str(LOCAL/"test_review_terrain_material_v3.py"): "test-helper-before-sweep-fix.py"}
    current = source_files()
    if pins.keys() != current.keys():
        raise ValueError("phase source inventory changed")
    used = []
    for path, digest in pins.items():
        if current[path] == digest:
            continue
        archive = out/archives[path] if path in archives else None
        if archive is None or not archive.is_file() or ref.sha256_file(archive) != digest:
            raise ValueError("phase renderer/source mismatch: "+path)
        used.append(archive.name)
    return used


SCENES = (("overview", 0), ("runoff", 1800), ("runoff", 6000), ("collection", 14400))
VIEWS = ("terrain-only", "albedo", "weights", "base-normal", "detail-normal",
         "roughness", "direct", "ambient", "constant-albedo", "no-detail", "face-normal")


def capture(out, label, *, profile="refined", tuning=None, views=False, video=False,
            parity=False, yaw=None, sweep=None):
    phase = out / checked_label(label)
    ref.reserve_directory(phase)
    runtime = ref.runtime_identity()
    pins = source_files()
    tuning_pin = ref.sha256_file(tuning) if tuning else None
    settings = None
    assets = []
    plan = []
    if views:
        for view in VIEWS:
            a = ref.still_asset("rain-off", "runoff", 6000)
            a["id"] = view
            plan.append((a, view))
        a = ref.still_asset("rain-off", "collection", 14400)
        a["id"] = "collection-terrain-only"
        plan.append((a, "terrain-only"))
    else:
        plan = [(ref.still_asset("rain-off", camera, t), None) for camera, t in SCENES]
    if parity:
        for camera, t in SCENES:
            a = ref.still_asset("rain-off", camera, t)
            a["id"] += "-readable"
            a["style"] = "readable"
            plan.append((a, None))
        for view in ref.DIAGNOSTICS:
            a = ref.diagnostic_asset("rain-off", view, 14400)
            plan.append((a, None))
    if video:
        for name, camera, start, interval in (("flow", "runoff", 4800, 5),
                                               ("collection", "collection", 9600, 20)):
            a = ref.video_asset(name, "rain-off", camera, start, 240, 30, interval)
            plan.append((a, None))
    for a, view in plan:
        a.update(width=1280, height=720)
        style = a.get("style", "scenic")
        output = phase / (a["id"] + (".mp4" if a["kind"] == "video" else ".png"))
        markers = view is None and style == "scenic" and a["kind"] == "video"
        command = ref.app_command(a, output, style, markers)
        if style == "scenic":
            command += ["--fluid25d-scenic-material", profile]
            if tuning:
                command += ["--fluid25d-scenic-tuning", str(tuning)]
            if view:
                command += ["--fluid25d-scenic-terrain-view", view]
        command += camera_args(a, yaw, sweep)
        if a["kind"] == "video":
            command.append("--fluid25d-recording-gpu-validation")
        result, receipt = ref.run_logged(command, phase / "logs", a["id"], timeout=600,
                                   expected_asset=a, expected_presentation=style)
        if style == "scenic":
            current = effective_material(result.stdout)
            if settings is not None and current != settings:
                raise ValueError("effective settings changed during phase")
            settings = current
            if view and f"view={view} water_draw=0 dots_draw=0" not in result.stdout:
                raise ValueError("terrain-only draw suppression receipt missing")
        record = {**a, "view": view, "presentation": style,
                  "path": output.relative_to(phase).as_posix(),
                  "sha256": ref.sha256_file(output), "receipt": receipt}
        if a["kind"] == "video":
            record["probe"] = ref.validate_probe(ref.ffprobe_video(output), 240, 30, 1280, 720)
        assets.append(record)
        print(f"captured {label}/{a['id']}", flush=True)
    ref.assert_same_runtime(runtime, ref.runtime_identity(), "during terrain audition")
    if pins != source_files() or (tuning and tuning_pin != ref.sha256_file(tuning)):
        raise ValueError("terrain source or tuning changed during phase")
    ref.write_json_exclusive(phase / "manifest.json", {
        "schema": "cubey.fluid25d.terrain-audition.v3", "runtime_identity": runtime,
        "profile": profile, "source_files": pins, "effective_material": settings,
        "tuning": {"path": str(tuning), "sha256": tuning_pin} if tuning else None,
        "camera_yaw": yaw, "camera_sweep": sweep, "assets": assets,
        "human_visual_acceptance": "deferred"})


def freeze(out):
    phase = out / "frozen-v2"
    ref.reserve_directory(phase)
    runtime = ref.runtime_identity()
    shutil.copy2(ref.APP, phase / "fluid_25d")
    shutil.copytree(ref.SHADER_ROOT, phase / "shaders")
    # These are archived identities, not a relocated runnable application:
    # the executable's compiled shader path still points at the dev build.
    ref.write_json_exclusive(phase / "identity.json", runtime)
    capture(out, "baseline", parity=True, video=True)


def profile(out):
    phase = out / "profiles"
    ref.reserve_directory(phase)
    runtime = ref.runtime_identity()
    pins = source_files()
    rows = []
    for width, height in ((1280, 720), (1920, 1080)):
        for batch in range(3):
            for material in ("refined", "terrain") if batch % 2 == 0 else ("terrain", "refined"):
                label = f"{height}p-{batch}-{material}"
                a = ref.video_asset(label, "rain-off", "runoff", 6000, 120, 30, 5)
                a.update(width=width, height=height, profile_warmup_frames=12)
                prefix = phase / label
                command = ref.app_command(a, prefix.with_suffix(".mp4"), "scenic", True, prefix)
                command += ["--fluid25d-scenic-material", material]
                _, receipt = ref.run_logged(command, phase / "logs", label, timeout=600,
                                           expected_asset=a, expected_presentation="scenic")
                rows.append({"height": height, "batch": batch, "material": material,
                             "summary": ref.profile_summary(prefix, 12, 108), "receipt": receipt})
                print(f"profiled {label}", flush=True)
    ref.assert_same_runtime(runtime, ref.runtime_identity(), "during matched profiling")
    if pins != source_files():
        raise ValueError("profile source changed")
    ref.write_json_exclusive(phase / "result.json", {"runtime_identity": runtime, "source_files": pins, "rows": rows})


def performance(rows):
    result = []
    for height, budget in ((720, .25), (1080, .75)):
        summary = {}
        for material in ("refined", "terrain"):
            batch = [row["summary"] for row in rows if row["height"] == height and row["material"] == material]
            if len(batch) != 3 or any(row["measured_gpu_span_count"] != 108 for row in batch):
                raise ValueError("matched profiling requires three 108-sample batches per material/size")
            summary[material] = {"median_ms": statistics.median(row["gpu_median_ms"] for row in batch),
                                 "p95_ms": statistics.median(row["gpu_p95_ms"] for row in batch),
                                 "worst_batch_p95_ms": max(row["gpu_p95_ms"] for row in batch)}
        increments = {key: summary["terrain"][key]-summary["refined"][key] for key in ("median_ms", "p95_ms")}
        if max(increments.values()) > budget or (height == 720 and summary["terrain"]["worst_batch_p95_ms"] > 4):
            raise ValueError("terrain incremental rendering budget failed")
        result.append({"height": height, "summary": summary, "increments_ms": increments,
                       "incremental_budget_ms": budget, "status": "PASS"})
    return result


def parity(out, runtime):
    baseline = json.loads((out/"baseline/manifest.json").read_text())
    candidate = json.loads((out/"candidate-final/manifest.json").read_text())
    controls = json.loads((out/"legacy-final/manifest.json").read_text())
    by_name = {row["path"]: row for row in candidate["assets"]}
    legacy = {row["path"]: row for row in controls["assets"]}
    unchanged = []
    for row in baseline["assets"]:
        if row["kind"] == "video":
            continue
        if legacy[row["path"]]["sha256"] != row["sha256"]:
            raise ValueError("V2 reference pixel parity failed: "+row["path"])
        if row["presentation"] == "readable" or row["kind"] == "diagnostic":
            if by_name[row["path"]]["sha256"] != row["sha256"]:
                raise ValueError("Readable/raw pixel parity failed: "+row["path"])
            unchanged.append(row["path"])
    frozen = json.loads((out/"frozen-v2/identity.json").read_text())
    current = runtime["compiled_shaders"]["files"]
    shaders = frozen["compiled_shaders"]["files"]
    if any(current.get(name) != pin for name, pin in shaders.items() if name != "fluid_25d_scenic_terrain.frag.spv"):
        raise ValueError("water/shared/geometry/numerical SPIR-V changed")
    if current["fluid_25d_scenic_terrain_legacy.frag.spv"] != shaders["fluid_25d_scenic_terrain.frag.spv"]:
        raise ValueError("legacy terrain shader not byte-identical")
    if frozen["inputs"] != runtime["inputs"]:
        raise ValueError("immutable native fields changed")
    old_water = effective_material((out/"baseline/logs/rain-off-runoff-6000s.stdout.txt").read_text())["settings"]
    settings = candidate["effective_material"]["settings"]
    if any(settings[name] != value for name, value in old_water.items()):
        raise ValueError("V2 water or retained material values changed")
    return {"v2_pixel_controls": 11, "readable_raw_pixel_controls": len(unchanged),
            "unchanged_compiled_shader_count": len(shaders)-1, "legacy_terrain_spirv": "byte-identical",
            "native_inputs": "byte-identical", "status": "PASS"}


def checked_ffmpeg(*args):
    result = ref.rtk_output("ffmpeg", "-v", "error", "-n", *map(str, args))
    if result.returncode:
        raise RuntimeError(result.stderr)


def gallery(out):
    phase = out/"review"
    ref.reserve_directory(phase)
    font = ref.find_font()
    def pair_graph(kind):
        return (f"[0:v]scale=640:360,drawtext=fontfile='{font}':text='V2 before':x=12:y=12:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.65[l];"
                f"[1:v]scale=640:360,drawtext=fontfile='{font}':text='Terrain study - after':x=12:y=12:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.65[r];"
                f"[l][r]hstack=inputs=2{',setpts=N/(30*TB)' if kind == 'video' else ''}[v]")
    for camera, t in SCENES:
        name = f"rain-off-{camera}-{t}s.png"
        checked_ffmpeg("-i", out/"baseline"/name, "-i", out/"candidate-final"/name,
                       "-filter_complex", pair_graph("still"), "-map", "[v]", "-frames:v", 1, phase/name)
    for name in ("flow-rain-off", "collection-rain-off"):
        checked_ffmpeg("-ignore_editlist", 1, "-i", out/"baseline"/(name+".mp4"),
                       "-ignore_editlist", 1, "-i", out/"candidate-final"/(name+".mp4"),
                       "-filter_complex", pair_graph("video"), "-map", "[v]", "-c:v", "libx264",
                       "-crf", 18, "-pix_fmt", "yuv420p", "-frames:v", 240, phase/(name+".mp4"))
        ref.validate_probe(ref.ffprobe_video(phase/(name+".mp4")), 240, 30, 1280, 360)
        ref.validate_probe(ref.ffprobe_video(phase/(name+".mp4"), ignore_editlist=False), 240, 30, 1280, 360)
        checked_ffmpeg("-i", phase/(name+".mp4"), "-frames:v", 1, phase/(name+"-poster.png"))
    figures = "".join(f"<figure><img src='review/rain-off-{camera}-{t}s.png'><figcaption>{camera}, saved {t}s — left V2, right terrain study.</figcaption></figure>" for camera,t in SCENES)
    ref.write_text_exclusive(out/"index.html", f"""<!doctype html><meta charset='utf-8'>
<title>Fluid 2.5D — terrain material study</title>
<style>body{{background:#151b22;color:#e4e8ed;font:16px system-ui;max-width:1280px;margin:24px auto;padding:0 16px}}img,video{{max-width:100%}}a{{color:#94ccff}}p{{line-height:1.5}}figure{{margin:20px 0}}</style>
<h1>Mountain Rain: terrain study</h1>
<p>Left: retained Scenic V2. Right: opt-in neutral soil/rock, corrected terrain normals and softened terrain ambient light. Same native water, geometry, banks, exposure and water shader. Readable and refined V2 remain defaults. Human visual acceptance is deferred.</p>
<h2>Start with these two 8-second comparisons</h2>
<video controls loop preload='metadata' src='review/flow-rain-off.mp4' poster='review/flow-rain-off-poster.png'></video>
<p>Runoff: requested 4800–5995s, saved native states every 60s. Dots are retained approximate diagnostic cues; their motion is not a particle solver.</p>
<video controls loop preload='metadata' src='review/collection-rain-off.mp4' poster='review/collection-rain-off-poster.png'></video>
<p>Collection after rain: requested 9600–14380s; rain was off by 7260s. Water can change apparent color because the underlying ground changed, not because depth or water optics changed.</p>
<h2>Four matched stills, without dots</h2>{figures}
<details><summary>Diagnostics, alternate heading and moving-camera checks</summary>
<p>The sharp pale cliff bands were traced to the narrow key retained in Cubey's generated irradiance. Ambient softening is a five-direction terrain-only approximation, not physical hemispherical integration.</p>
<p><a href='diagnosis/ambient.png'>V2 ambient component</a> · <a href='diagnosis/terrain-only.png'>V2 terrain-only</a> · <a href='diagnosis/no-detail.png'>No-detail control</a> · <a href='candidate-diagnostics/terrain-only.png'>Candidate terrain-only</a> · <a href='alternate/rain-off-runoff-6000s.png'>Alternate heading</a></p>
<p><a href='sweep-v2/flow-rain-off.mp4'>Moving camera: V2</a> · <a href='sweep-terrain/flow-rain-off.mp4'>Moving camera: terrain study</a>. Sweep and mild zoom are render-only; requested/saved water times remain logged.</p>
</details>
<p>Limits: macro material identity improved; this is not photorealistic close-up rock. Sixty-metre normal support and 30m geometric cell/bank steps remain. No terrain, shoreline or simulation fix is claimed. No new texture assets, ambient occlusion or shared environment changes.</p>
<p><a href='RESULTS.md'>Verdict and measured costs</a> · <a href='acceptance.json'>Acceptance receipts</a> · <a href='review-seal.json'>Artifact provenance</a></p>""")


def inventory(out):
    files = {}
    for path in sorted(out.rglob("*")):
        if path.is_symlink():
            raise ValueError("review contains symlink")
        if path.is_file() and path.name not in ("review-seal.json", "verification.json"):
            files[path.relative_to(out).as_posix()] = ref.sha256_file(path)
    return files


def seal(out):
    runtime = ref.runtime_identity()
    for label in ("candidate-final", "legacy-final", "candidate-diagnostics", "alternate", "sweep-v2", "sweep-terrain"):
        manifest = json.loads((out/label/"manifest.json").read_text())
        ref.assert_same_runtime(manifest["runtime_identity"], runtime, label)
        check_phase_sources(out,manifest["source_files"])
    profiles = json.loads((out/"profiles/result.json").read_text())
    ref.assert_same_runtime(profiles["runtime_identity"], runtime, "profiles")
    if profiles["source_files"] != source_files():
        raise ValueError("profile source mismatch")
    tests = ET.parse(out/"gates.xml").getroot().findall(".//testcase")
    if not tests or any(t.find("failure") is not None or t.find("error") is not None or t.find("skipped") is not None for t in tests):
        raise ValueError("full dev tests must pass without skips")
    names = {t.get("name") for t in tests}
    if not {"fluid_25d_surface_gradient_gpu", "fluid_25d_scenic_gpu", "fluid_25d_tests", "fluid_25d_terrain_review_helpers"}.issubset(names):
        raise ValueError("required terrain tests missing")
    gui = json.loads((out/"gui-terrain/manifest.json").read_text())
    if gui["status"] != "pass" or gui["style"] != "scenic":
        raise ValueError("private style/resize gate failed")
    ref.assert_same_runtime(gui["runtime_identity"],runtime,"private GUI")
    if effective_material((out/"gui-terrain/launch/viewer.log").read_text())["profile"] != "terrain":
        raise ValueError("private GUI did not render terrain preset")
    helper_tests = ET.parse(out/"helper-gates.xml").getroot().findall(".//testcase")
    if not helper_tests or any(t.find("failure") is not None or t.find("error") is not None or t.find("skipped") is not None for t in helper_tests):
        raise ValueError("post-orchestration-fix helper gates must pass")
    acceptance = {"parity": parity(out,runtime), "performance": performance(profiles["rows"]),
                  "full_dev_tests": len(tests), "private_gui": "automated style/resize only",
                  "human_visual_acceptance": "deferred"}
    ref.write_json_exclusive(out/"acceptance.json", acceptance)
    ref.write_json_exclusive(out/"review-seal.json", {"schema":"cubey.fluid25d.terrain-review.v3",
        "runtime_identity": runtime, "source_files": source_files(), "artifacts": inventory(out),
        "human_visual_acceptance": "deferred"})
    print(json.dumps(acceptance,indent=2))


def verify(out):
    value = json.loads((out/"review-seal.json").read_text())
    if value["schema"] != "cubey.fluid25d.terrain-review.v3" or value["artifacts"] != inventory(out):
        raise ValueError("review artifact inventory changed")
    ref.assert_same_runtime(value["runtime_identity"],ref.runtime_identity(),"review verification")
    if value["source_files"] != source_files():
        raise ValueError("review source changed")
    page = (out/"index.html").read_text()
    links = re.findall(r"(?:src|href|poster)='([^']+)'",page)
    if len([link for link in links if link.endswith(".mp4")]) != 4 or page.count("<video ") != 2:
        raise ValueError("compact gallery video count changed")
    if any(not (out/link).is_file() for link in links):
        raise ValueError("broken review link")
    return {"status":"PASS","artifact_count":len(value["artifacts"]),"review_links":len(links),
            "human_visual_acceptance":"deferred"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("freeze", "capture", "profile", "gallery", "seal", "verify"))
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--label", default="candidate")
    parser.add_argument("--material", choices=("v1", "refined", "terrain"), default="refined")
    parser.add_argument("--tuning", type=Path)
    parser.add_argument("--views", action="store_true")
    parser.add_argument("--video", action="store_true")
    parser.add_argument("--parity", action="store_true")
    parser.add_argument("--yaw", type=float)
    parser.add_argument("--sweep", type=float)
    args = parser.parse_args()
    out = args.out.resolve()
    if out.parent != ref.OUTPUT_ROOT.resolve() or not out.name.startswith("terrain-material-v3-"):
        raise ValueError("use a fresh terrain-material-v3-* leaf under outputs/fluid")
    if args.action == "freeze":
        freeze(out)
    elif args.action == "profile":
        profile(out)
    elif args.action == "gallery":
        gallery(out)
    elif args.action == "seal":
        seal(out)
    elif args.action == "verify":
        print(json.dumps(verify(out),indent=2))
    else:
        capture(out, args.label, profile=args.material, tuning=args.tuning, views=args.views,
                video=args.video, parity=args.parity, yaw=args.yaw, sweep=args.sweep)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
