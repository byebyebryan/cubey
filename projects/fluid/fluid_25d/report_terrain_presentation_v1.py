#!/usr/bin/env python3
"""Publish the bounded terrain review from existing receipts; never run a renderer."""
from __future__ import annotations

import argparse
import html
import json
import math
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

import review_terrain_presentation_v1 as review
import run_terrain_presentation_perf_v1 as perf

FINAL_PHASES = ("final-candidate", "final-dry", "final-cues", "raw-legacy-checked",
                "raw-candidate", "sweep-legacy-checked", "sweep-candidate")
LABELS = {"mountain": "Mountain", "upland-basins": "Upland basins",
          "joining-valleys": "Joining valleys", "steep-ridges": "Steep ridges"}
GPU = "gpu:fluid_25d native presentation total"
CPU = "cpu:headless.frame_service"
AGE = "fluid_25d.external.advancing_state_age_s"


def read(path):
    return json.loads(path.read_bytes())


def stat(profile, group, name):
    value = profile[group][name]
    if value["samples"] < 1 or any(not math.isfinite(value[k]) for k in ("p50", "p95", "max")):
        raise ValueError("missing/nonfinite timing samples: " + name)
    return value


def performance(results):
    """Keep gates per trial: never pool percentiles or equate capped loops with FPS."""
    expected = perf.plan()
    rows = results["trials"]
    if [{k: r[k] for k in expected[0]} for r in rows] != expected:
        raise ValueError("paired trials are incomplete, duplicated or out of order")
    summary = []
    for row in rows:
        result = row["result"]
        profile = result["profile"]
        if (result["status"] != "completed" or result["errors"] or result["host"] != "offscreen"
                or profile["viewport"] != [[1920, 1080]]
                or result["app_sha256"] != results["runtime"]["app_sha256"]
                or result["input_identity"] != result["input_identity_after"]
                or any(c["active_group_remaining"] for c in result["cleanup"])):
            raise ValueError("invalid completed trial: " + row["name"])
        gpu, cpu = stat(profile, "passes_ms", GPU), stat(profile, "passes_ms", CPU)
        if gpu["p95"] <= 0 or cpu["p95"] < 0:
            raise ValueError("invalid timing range")
        live = row["mode"] == "live"
        age = stat(profile, "metrics", AGE)["p95"] if live else None
        pace = result["physical_wall_ratio"] if live else None
        dispatch = "fluid_25d.external.hydraulic_dispatch_count" if live else "fluid_25d.recording.hydraulic_dispatch_count"
        if stat(profile, "metrics", dispatch)["max"] != 0:
            raise ValueError("presentation unexpectedly ran hydraulic dispatches")
        if live:
            completion = result["completion"]
            if (not completion["inputs_unchanged"] or not completion["reference_inputs_unchanged"]
                    or completion["failure_message"] or not math.isfinite(pace) or age < 0):
                raise ValueError("invalid native completion")
        summary.append({k: row[k] for k in expected[0]} | dict(
            gpu_p50_ms=gpu["p50"], gpu_p95_ms=gpu["p95"], cpu_service_p95_ms=cpu["p95"],
            capped_loop_p95_ms=profile["frame_ms"]["p95"],
            field_age_p95_s=age, physical_wall_ratio=pace,
            gate_pass=gpu["p95"] <= 8 and (not live or (pace >= 54 and age <= .5))))
    return summary


def paired_hashes(left, right):
    def keys(value):
        return {(r["site"], r["camera"], r["view"]): r["sha256"] for r in value["assets"]}
    a, b = keys(left), keys(right)
    if not a or a.keys() != b.keys() or len(a) != len(left["assets"]) or len(b) != len(right["assets"]):
        raise ValueError("parity views do not match")
    return {"total": len(a), "identical": sum(a[k] == b[k] for k in a)}


def lifecycle_checked(result, script):
    events = result["events"]
    actions = [r["event"][7:] for r in events
               if r["event"].startswith("action-") and r["event"] != "action-applied"]
    applied = [r for r in events if r["event"] == "action-applied"]
    if (actions != [r["kind"] for r in script["actions"]] or len(applied) != len(actions)
            or any(r["hydraulic_dispatches"] != 0 for r in events)
            or any(not r["solver_bed_sha256"] for r in applied)):
        raise ValueError("managed actions did not all complete")
    expected = {r["case_id"] for r in script["actions"] if r["kind"] == "switch"}
    if not expected.issubset({r["case_id"] for r in applied}):
        raise ValueError("managed site/grid changes were not installed")
    return dict(actions_applied=len(applied), installed_cases=sorted({r["case_id"] for r in applied}))


def video_checked(path):
    probe = json.loads(subprocess.check_output(["rtk", "proxy", "ffprobe", "-v", "error",
        "-select_streams", "v:0", "-show_entries",
        "stream=codec_name,width,height,r_frame_rate,nb_frames,duration", "-of", "json", str(path)]))
    stream, = probe["streams"]
    if (stream["codec_name"] != "h264" or (stream["width"], stream["height"]) != (1280, 720)
            or stream["r_frame_rate"] != "24/1" or int(stream["nb_frames"]) != 120
            or not 4.9 <= float(stream["duration"]) <= 5.1):
        raise ValueError("unexpected sweep encoding")
    decoded = subprocess.check_output(["rtk", "proxy", "ffmpeg", "-v", "error", "-i", str(path),
        "-vf", "fps=1", "-frames:v", "3", "-f", "framemd5", "-"], text=True)
    hashes = [line.split(",")[-1].strip() for line in decoded.splitlines() if line and not line.startswith("#")]
    if len(hashes) != 3 or len(set(hashes)) != 3:
        raise ValueError("camera sweep did not visibly change decoded frames")
    return dict(stream=stream, decoded_frame_hashes=hashes)


def validate(out):
    results = read(out/"paired-performance/results.json")
    timings = performance(results)
    protocol = read(out/"paired-performance/protocol.json")
    if protocol["runtime"] != results["runtime"] or protocol["plan"] != perf.plan():
        raise ValueError("performance protocol mismatch")
    manifests = {phase: read(out/phase/"manifest.json") for phase in
                 ("baseline-main", "legacy-parity", *FINAL_PHASES)}
    videos = {}
    for phase, manifest in manifests.items():
        sites = ("mountain", "joining-valleys") if phase.startswith("sweep-") else LABELS
        cameras = ("slope",) if phase.startswith("sweep-") else (
            ("overview",) if phase.startswith("raw-") else
            ("overview", "slope") if phase == "final-dry" else
            ("overview", "basin") if phase == "final-cues" else ("overview", "slope", "basin"))
        views = ("raw-depth", "raw-speed") if phase.startswith("raw-") else ("shaded",)
        expected = {(s, c, v) for s in sites for c in cameras for v in views}
        actual = {(r["site"], r["camera"], r["view"]) for r in manifest["assets"]}
        if actual != expected or len(actual) != len(manifest["assets"]):
            raise ValueError("incomplete/duplicate capture coverage: " + phase)
        if phase in FINAL_PHASES and manifest["runtime"] != results["runtime"]:
            raise ValueError("final review runtime differs from measured runtime")
        for site, identity in manifest["inputs"].items():
            if identity != review.input_identity(site):
                raise ValueError("sealed review input changed: " + site)
        for row in manifest["assets"]:
            path = out/phase/row["path"]
            if not path.is_file() or review.digest(path) != row["sha256"] or row["cleanup"]["active_group_remaining"]:
                raise ValueError("missing/changed media or incomplete cleanup")
            log = (path.parent/(path.stem+".log")).read_text(errors="replace")
            if "PASS bit-exact" not in log or "VUID-" in log or "validation error" in log.lower():
                raise ValueError("capture validation failed")
            if phase.startswith("sweep-"):
                samples = re.findall(r"fluid_25d_recording_capture: output_frame=(\d+) requested_s=([\d.]+) "
                    r"saved_s=([\d.]+).*hydraulic_dispatches=0 visual_delta_s=([\d.]+)", log)
                if (len(samples) != 120 or {int(r[0]) for r in samples} != set(range(120))
                        or len({(r[1], r[2]) for r in samples}) != 1
                        or any(float(r[3]) != 0 for r in samples)):
                    raise ValueError("camera sweep did not hold the native playhead/fields")
                videos[str(path.relative_to(out))] = video_checked(path)
    parity = paired_hashes(manifests["baseline-main"], manifests["legacy-parity"])
    raw = paired_hashes(manifests["raw-legacy-checked"], manifests["raw-candidate"])
    if parity != dict(total=12, identical=12) or raw != dict(total=8, identical=8):
        raise ValueError("legacy or raw numerical-image parity failed")
    shader_before = {p.name: review.digest(p) for p in (out/"baseline-shaders").glob("*.spv")}
    shader_after = results["runtime"]["shaders"]
    changed = sorted(k for k in shader_before if shader_before[k] != shader_after[k])
    allowed = sorted("fluid_25d_"+k+".spv" for k in
                     ("scenic_terrain.frag", "scenic_terrain_legacy.frag", "scenic_water.frag", "whitewater.frag"))
    if shader_before.keys() != shader_after.keys() or changed != allowed:
        raise ValueError("unexpected numerical/geometry shader change")
    tests = ET.parse(out/"tests.xml").getroot().attrib
    if int(tests["tests"]) != 55 or any(int(tests[k]) for k in ("failures", "skipped", "disabled")):
        raise ValueError("selected CTest suite did not pass")
    helpers = ET.parse(out/"helper-tests-complete.xml").getroot().attrib
    if int(helpers["tests"]) != 2 or any(int(helpers[k]) for k in ("failures", "skipped", "disabled")):
        raise ValueError("final review/report helper checks did not pass")
    lifecycle = read(out/"managed-lifecycle/validation.json")
    if (lifecycle["returncode"] or lifecycle["errors"] or not lifecycle["headless_only"]
            or lifecycle["app_sha256"] != results["runtime"]["app_sha256"]
            or any(c["active_group_remaining"] for c in lifecycle["cleanup"])):
        raise ValueError("managed lifecycle trial failed")
    lifecycle_summary = lifecycle_checked(lifecycle, read(out/"lifecycle-script.json"))
    managed_log = (out/"managed-lifecycle/trial/viewer.log").read_text(errors="replace")
    if "fluid_25d_external_upload: PASS bit-exact" not in managed_log or "mode=scene" not in managed_log:
        raise ValueError("managed default projection/upload receipt missing")
    candidate = [r for r in timings if r["variant"] == "candidate"]
    return dict(schema="cubey.fluid25d.terrain-presentation.v1", runtime=results["runtime"],
        parity=parity, raw_diagnostic_parity=raw, changed_shaders=changed,
        unchanged_shaders=len(shader_before)-len(changed), ctest_passed=55,
        final_helper_ctest_passed=2,
        timings=timings, candidate_performance_pass=all(r["gate_pass"] for r in candidate),
        lifecycle_script_sha256=review.digest(out/"lifecycle-script.json"),
        lifecycle_receipt_sha256=review.digest(out/"managed-lifecycle/validation.json"),
        lifecycle=lifecycle_summary,
        videos=videos,
        prepared_source_identity={s: review.terrain.case_identity(review.scene(s)[0]) for s in LABELS},
        tool_sha256={p: review.digest(review.ROOT/"projects/fluid/fluid_25d"/p) for p in
                     ("review_terrain_presentation_v1.py", "run_terrain_presentation_perf_v1.py",
                      "report_terrain_presentation_v1.py", "test_review_terrain_presentation_v1.py")},
        excluded_setup_attempts={"baseline-overview": "wrong protocol filename; no media",
                                 "raw-legacy": "wrong diagnostic CLI flag; no media; fixed in harness",
                                 "sweep-legacy": "zero recording interval rejected; no media; positive interval clamps final playhead"},
        media_manifests={p: review.digest(out/p/"manifest.json") for p in manifests},
        human_visual_acceptance="deferred", push="not authorized; not performed",
        boundary="Genuine Vulkan offscreen; timings exclude capture/encode; native GUI/scanout not tested")


def figure(path, caption, video=False, poster=None):
    escaped = html.escape(path, quote=True)
    if video:
        media = f'<video controls muted loop playsinline preload="metadata" poster="{html.escape(poster, quote=True)}"><source src="{escaped}" type="video/mp4"></video>'
    else:
        media = f'<a href="{escaped}"><img loading="lazy" src="{escaped}" alt="{html.escape(caption, quote=True)}"></a>'
    return f'<figure>{media}<figcaption>{html.escape(caption)}</figcaption></figure>'


def pair(phase_a, phase_b, site, camera, label_a="Before", label_b="Recommended"):
    name = f"{site}-{camera}-shaded.png"
    return '<div class="pair">'+figure(phase_a+"/"+name, label_a)+figure(phase_b+"/"+name, label_b)+"</div>"


def table(rows):
    body = []
    for r in rows:
        pace = "—" if r["physical_wall_ratio"] is None else f'{r["physical_wall_ratio"]:.2f}×'
        age = "—" if r["field_age_p95_s"] is None else f'{r["field_age_p95_s"]:.3f}'
        body.append(f'<tr><td>{LABELS[r["site"]]}</td><td>{r["variant"]} / {r["mode"]} / {r["repeat"]+1}</td><td>{r["gpu_p95_ms"]:.2f}</td><td>{r["cpu_service_p95_ms"]:.2f}</td><td>{pace}</td><td>{age}</td><td>{"pass" if r["gate_pass"] else "MISS"}</td></tr>')
    return '<div class="scroll"><table><thead><tr><th>Site</th><th>Variant / mode / repeat</th><th>GPU p95 ms</th><th>CPU service p95 ms</th><th>Actual pace</th><th>Field age p95 s</th><th>Gate</th></tr></thead><tbody>'+"".join(body)+"</tbody></table></div>"


def page(receipt):
    rows = receipt["timings"]
    candidate = [r for r in rows if r["variant"] == "candidate"]
    live = [r for r in candidate if r["mode"] == "live"]
    verdict = "Performance targets pass in every candidate trial." if receipt["candidate_performance_pass"] else "Some candidate performance targets were missed; see timings below."
    content = f'''<header><p class="eyebrow">Cubey · Fluid 2.5D · 11 October 2026</p><h1>Terrain presentation: fewer false grid lines</h1>
<p class="lead">Keep the 2× B-spline mesh. Use existing source-aligned terrain materials and the corrected, whole-scene 2048² shadow fit.</p>
<p>{verdict} Four live trials: {min(r["physical_wall_ratio"] for r in live):.2f}–{max(r["physical_wall_ratio"] for r in live):.2f}× actual pace, GPU p95 up to {max(r["gpu_p95_ms"] for r in live):.2f} ms, field-age p95 up to {max(r["field_age_p95_s"] for r in live):.3f} s. No GUI was opened.</p></header>
<nav><a href="#close">Close views</a> · <a href="#sites">Four sites</a> · <a href="#motion">Camera sweeps</a> · <a href="#performance">Performance</a> · <a href="#proof">Validation</a></nav>
<section id="close"><h2>What changed</h2><p>Look for repeated dark striping on the lower-left hillsides. Much of the apparent cell structure was false self-shadowing, not a coarse water solver or terrain mesh. The recommendation changes shadow bias/projection and material organization; it does not move terrain or water.</p>
<h3>Mountain · 900 m close-view framing</h3>{pair("baseline-main", "final-candidate", "mountain", "slope")}
<h3>Joining valleys · 900 m close-view framing</h3>{pair("baseline-main", "final-candidate", "joining-valleys", "slope")}
<p>Improved, not perfect: fine shadow patterning and smooth source-limited terrain remain. This is a bare-ground macro demo, not walking-level realism.</p>
<details><summary>Separate the shadow change from the material change</summary>{pair("baseline-main", "shadow-world-2048", "joining-valleys", "slope", "Legacy shadow / same legacy material", "Corrected shadow / same legacy material")}
<div class="pair">{figure("baseline-components/joining-valleys-slope-no-shadows.png", "No shadows: the repeated striping largely disappears")}{figure("baseline-components/joining-valleys-slope-base-normal.png", "Analytic base normals: no matching cell staircase")}</div>
<p>Tighter projection alone was rejected: clipping receiver-plane gradients to ±1 made steep receivers worse. The corrected path retains their real gradient and scales residual bias by shadow texel size in world metres. All terrain bounds remain shadow casters, even off camera.</p></details></section>
<section id="sites"><h2>Existing materials, now source-aligned by default</h2><p>These are shared Terrain foundation masks, derived from full-source landform and climate data. They are not hand-picked colors per site, imported assets, current vegetation, or a new biome simulation. Expect restrained substrate differences, not dramatically different forests/deserts.</p>'''
    for site in LABELS:
        content += f'<h3>{LABELS[site]}</h3>'+pair("baseline-main", "final-candidate", site, "overview", "Legacy organization", "Source-aligned organization + corrected shadows")
    content += '<details><summary>Dry controls and normal demo cues</summary><div class="pair">'
    for site in LABELS:
        content += figure(f"final-dry/{site}-overview-shaded.png", LABELS[site]+" · original dry field")
        content += figure(f"final-cues/{site}-overview-shaded.png", LABELS[site]+" · developed field with rain/trails")
    content += '</div><p>Mountain is held at about 10.6 physical hours; the other sites at about six hours. Comparisons are matched within each site, not equal-age hydrology across sites.</p></details></section>'
    content += '<section id="motion"><h2>Does the terrain hold together when the camera moves?</h2><p>Five-second matched sweeps. Native water fields and their clock are held; these clips test camera-dependent artifacts, not new solver flow.</p>'
    for site in ("mountain", "joining-valleys"):
        name = f"{site}-slope-shaded"
        content += '<h3>'+LABELS[site]+'</h3><div class="pair">'
        content += figure(f"sweep-legacy-checked/{name}.mp4", "Legacy", True, f"baseline-main/{name}.png")
        content += figure(f"sweep-candidate/{name}.mp4", "Recommended", True, f"final-candidate/{name}.png")+"</div>"
    content += f'''</section><section id="performance"><h2>Measured while the actual solver runs</h2><p>1920×1080, 60 Hz render cap, requested 60× solver pace; sequential trials with reversed order on repeat two. Native prewarm is 1,800 physical seconds before renderer startup. Live measurements: 45 wall seconds after five-second warmup. Held measurements: 15 seconds after warmup, no solver. No image readback or video encoding.</p>
<p>Per-trial gates: scene GPU p95 ≤8 ms; live actual pace ≥54×; live advancing-field age p95 ≤0.5 s. CPU service excludes intentional frame pacing. GPU timestamps measure the scene, not CUDA kernels; sharing the GPU can affect them.</p>{table(candidate)}
<details><summary>All 16 paired trials, including legacy</summary>{table(rows)}</details>
<p>Do not infer uncapped FPS from the 60 Hz loop, or extrapolate to 2k grids, all cameras, late-time states, or native GUI scanout. Existing desktop GPU background work was left running.</p>
<details><summary>Why not denser geometry or a 4096² shadow default?</summary><p>The 4× mesh gave little additional visual benefit over 2×, but its Mountain held-state GPU p95 was 8.69 ms, above the target. 4096² remains an explicit study control: its limited visible gain did not justify promotion. Short probes are not balanced proof that 4096² is faster. Tight fit without the steep-gradient fix was rejected.</p></details></section>
<section id="proof"><h2>What is verified, and what is not</h2><ul><li>55 selected CTest checks passed, none skipped: shared terrain, renderer/solver boundaries, headless lifecycle, GPU gradient and lighting controls.</li><li>12/12 legacy images remain byte-identical to the pre-pass reference.</li><li>8/8 raw depth/speed images are byte-identical across the appearance change.</li><li>42/46 compiled shaders are unchanged. Only four shared lighting consumers changed; numerical solvers and B-spline geometry did not.</li><li>Every final capture has a bit-exact terrain/depth/velocity GPU upload receipt. Managed start/pause/reset/rain/site/grid controls were exercised headlessly.</li><li>Owner visual acceptance and native GUI layout/scanout are deferred. No push was performed.</li></ul>
<p><a href="acceptance.json">Machine-readable validation receipt</a> · <a href="paired-performance/results.json">Full timing receipts</a> · <a href="managed-lifecycle/validation.json">Managed lifecycle receipt</a> · <a href="tests.xml">CTest results</a></p>
<p>Research supports diagnosing coverage/bias before adding a new shadow architecture: <a href="https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps">Microsoft shadow-map guidance</a>. The existing comparison filter follows <a href="https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-11-shadow-map-antialiasing">GPU Gems shadow filtering</a>.</p></section>'''
    style = '''body{margin:0;background:#101719;color:#e1e8e6;font:16px/1.6 system-ui,sans-serif}main{max-width:1280px;margin:auto;padding:36px 22px}h1{font-size:clamp(28px,4vw,44px);line-height:1.2}h2{font-size:26px}h3{font-size:19px}.eyebrow,figcaption{color:#acbbb6}.lead{font-size:21px;max-width:900px}a{color:#89c9bd}nav{padding:16px 0}section{padding:18px 0;border-top:1px solid #344440}.pair{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}figure{margin:8px 0 18px}img,video{display:block;width:100%;height:auto;background:#080e10}figcaption{padding-top:6px;font-size:14px}details{margin:18px 0;padding:14px;background:#172225}summary{cursor:pointer}.scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:14px}th,td{padding:8px;border-bottom:1px solid #344440;text-align:left;white-space:nowrap}th{color:#89c9bd}@media(max-width:760px){.pair{grid-template-columns:1fr}main{padding:22px 14px}}'''
    return '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Fluid 2.5D · Terrain presentation V1</title><style>'+style+'</style></head><body><main>'+content+'</main></body></html>\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve(strict=True)
    if out.parent != (review.ROOT/"outputs/fluid").resolve() or not out.name.startswith("terrain-presentation-v1-"):
        parser.error("use the owned terrain review leaf")
    if (out/"index.html").exists() or (out/"acceptance.json").exists():
        parser.error("report already exists; do not overwrite sealed review artifacts")
    receipt = validate(out)
    document = page(receipt)
    # Reject missing local links, media or posters before publishing either artifact.
    for target in re.findall(r'(?:href|src|poster)="([^"]+)"', document):
        if not target.startswith(("#", "https://")) and target != "acceptance.json":
            path = (out/html.unescape(target)).resolve(strict=True)
            if not path.is_relative_to(out) or not path.is_file():
                raise ValueError("invalid report link: " + target)
    review.write(out/"acceptance.json", receipt)
    with (out/"index.html").open("x") as stream:
        stream.write(document)
    print(json.dumps(dict(report=str(out/"index.html"), performance_pass=receipt["candidate_performance_pass"])))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
