#!/usr/bin/env python3
"""Checked, compact export for the optional shared environment integration."""
import argparse
import csv
import html
import json
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET

import review_environment_v1 as review
from check_environment_regressions import TESTS
from report_patch_only_v1 import checked_installs


def read(path):
    return json.loads(path.read_bytes())


def burst_summary(prefix, eligible=None):
    metrics={}
    with prefix.with_suffix(".metrics.csv").open() as stream:
        for row in csv.DictReader(stream):
            if row["category"]=="fluid_25d.environment":
                metrics.setdefault(int(row["frame_index"]),{})[row["name"]]=float(row["value"])
    capture_frames=set()
    previous=None
    for index, values in sorted(metrics.items()):
        key=(values["sky_generation"],values["cloud_generation"])
        if previous is not None and key!=previous:
            capture_frames.add(index)
        previous=key
    capture,ordinary=[],[]
    with prefix.with_suffix(".passes.csv").open() as stream:
        for row in csv.DictReader(stream):
            if row["kind"]=="gpu" and row["label"]=="fluid_25d native presentation total":
                index=int(row["frame_index"])
                if eligible is None or index in eligible:
                    (capture if index in capture_frames else ordinary).append(float(row["duration_ms"]))
    return {"capture_frames":len(capture),"capture_gpu_p95_ms":review.render.percentile(capture) if capture else None,
        "ordinary_gpu_p95_ms":review.render.percentile(ordinary) if ordinary else None,
        "scope":"generation-change frames; original submitted frame indices; not per-pass CUDA accounting"}


def live_row(out, name):
    result=read(out/name/"validation.json")
    if result["errors"] or result["returncode"] or not result["headless_only"]:
        raise ValueError("failed live trial: "+name)
    if result["app_sha256"]!=review.patch.runtime()["app"] or result["viewport"]!=[[1920,1080]]:
        raise ValueError("live artifact/viewport mismatch")
    if any(r["active_group_remaining"] or r["escalated"] for r in result["cleanup"]):
        raise ValueError("owned cleanup failed")
    gpu=result["passes_ms"]["gpu:fluid_25d native presentation total"]
    cpu=result["passes_ms"]["cpu:headless.frame_service"]
    age=result["metrics"]["fluid_25d.external.advancing_state_age_s"]
    running=[r for r in result["samples"] if r.get("phase")=="active" and r.get("lifecycle")=="running"]
    start=running[0]["unix_s"]+result["warmup_seconds"]
    metrics={}
    with (out/name/"trial/profile.metrics.csv").open() as stream:
        for row in csv.DictReader(stream):
            metrics.setdefault(int(row["frame_index"]),{})[row["category"]+"."+row["name"]]=float(row["value"])
    eligible={i for i,v in metrics.items() if v.get("fluid_25d.external.lifecycle_code")==1
        and v.get("fluid_25d.external.physical_time_s",0)>0
        and v.get("fluid_25d.study.command_recorded_unix_s",0)>=start}
    times=[metrics[i]["fluid_25d.study.command_recorded_unix_s"] for i in eligible]
    measured=[]
    with (out/name/"trial/profile.passes.csv").open() as stream:
        for row in csv.DictReader(stream):
            if int(row["frame_index"]) in eligible and row["kind"]=="gpu" and row["label"]=="fluid_25d native presentation total":
                measured.append(float(row["duration_ms"]))
    if len(measured)!=gpu["count"]:
        raise ValueError("live timing window mismatch")
    gpu["p99"]=review.render.percentile(measured,.99)
    return {"name":name,"gpu":gpu,"cpu":cpu,"age":age,
        "pace":result["physical_wall_ratio"],"frame_ms":result["frame_ms"],
        "within_proposed_budget":gpu["p95"]<=8 and cpu["p95"]<=4 and age["p95"]<=1 and result["physical_wall_ratio"]>=54,
        "bursts":burst_summary(out/name/"trial/profile",eligible),"cleanup":result["cleanup"],
        "timing_span_s":max(times)-min(times),
        "whole_trial_wall_s":result["samples"][-1]["wall_s"],
        "warmup_seconds":result["warmup_seconds"]}


def report(out):
    frozen=review.verify(out)
    review.patch.verify(out,"final")
    fixed=read(out/"final-captures.json")["rows"]
    if len(fixed)!=8 or any(r["difference"]["max_abs_8bit"] for r in fixed):
        raise ValueError("fixed controls must remain pixel exact")
    frames=read(out/"environment-frames.json")
    terrain=read(out/"environment-terrain.json")
    motion=read(out/"environment-motion.json")
    perf=read(out/"environment-performance.json")
    if len(frames["rows"])!=20 or len(frames["low_oblique"])!=2 or len(terrain["rows"])!=4 or len(motion["rows"])!=2 or len(perf["rows"])!=12:
        raise ValueError("incomplete matched evidence")
    for row in frames["rows"]+frames["low_oblique"]+terrain["rows"]+motion["rows"]:
        if row["exit_code"] or "VUID-" in (out/Path(row["command"][-1]).with_suffix(".log").name).read_text():
            raise ValueError("invalid GPU evidence")
    tests=ET.parse(out/"regression-tests.xml").getroot().findall(".//testcase")
    if {t.get("name") for t in tests}!=TESTS or any(t.find(tag) is not None for t in tests for tag in ("failure","error","skipped")):
        raise ValueError("named regressions missing/failed/skipped")
    life=read(out/"lifecycle/validation.json")
    if life["errors"] or life["returncode"]:
        raise ValueError("managed lifecycle failed")
    installs=checked_installs(life)
    raw=(out/"lifecycle/trial/viewer.log").read_text()
    if raw.count("fluid_25d_external_upload: PASS")!=9:
        raise ValueError("missing exact native GPU audit after a managed scene")
    clocks=[float(v) for v in re.findall(r"terrain_surface_environment: wall_s=(\S+)",raw)]
    if len(clocks)<100 or any(b<a for a,b in zip(clocks,clocks[1:])):
        raise ValueError("weather clock reset across managed scene replacements")
    live=[live_row(out,name) for name in ("live-fixed-0","live-weather-0","live-weather-1","live-fixed-1")]
    soak=live_row(out,"soak-weather")
    for row in perf["rows"]:
        name=f"perf-{row['case']}-{row['treatment']}-{row['quality']}-{row['repeat']}"
        row["bursts"]=burst_summary(out/name)
    export=out/"export";export.mkdir()
    assets=[r["image"] for r in frames["rows"]+frames["low_oblique"]+terrain["rows"]]
    assets += [r["path"] for r in motion["rows"]]+["cycle-contact.png"]
    for name in assets:
        shutil.copy2(out/name,export/name)
    evidence={"fixed_pixel_exact_views":8,"native_fields":"exact GPU imports; bed/depth/velocity unchanged",
        "frozen_input_files":len(frozen["inputs"]),"regression_groups":len(tests),
        "dry_installs":len(installs),"environment_clock_monotonic_across_installs":True,
        "performance":perf,"live":live,"soak":soak,"decoded_cycle":read(out/"cycle-decoded-review.json"),
        "limits":["surface cloud V1, not aerial/cloud-top qualification","receiver-plane cloud shadows, not relief-aware cascades",
            "clear-sky diffuse irradiance plus approximate cloud direct attenuation, not complete overcast GI",
            "dark night and visible primary-light handover in accelerated cycles","headless only; native GUI and default promotion deferred"],
        "solver_and_geometry_changes":False,"default":"fixed daylight/clear unchanged","push":False}
    review.patch.write(export/"evidence.json",evidence)
    for name in ("environment-protocol.json","environment-frames.json","environment-terrain.json",
                 "environment-motion.json","final-captures.json","regression-tests.xml"):
        shutil.copy2(out/name,export/name)
    fmt=lambda value:f"{value:.2f}"
    perf_rows="".join(f"<tr><td>{html.escape(r['case'])}</td><td>{r['treatment']} / {r['quality']}</td><td>{r['repeat']+1}</td><td>{fmt(r['gpu_p95_ms'])}</td><td>{fmt(r['gpu_p99_ms'])}</td><td>{fmt(r['gpu_max_ms'])}</td><td>{fmt(r['cpu_p95_ms'])}</td></tr>" for r in perf["rows"])
    live_rows="".join(f"<tr><td>{r['name']}</td><td>{fmt(r['gpu']['p95'])}</td><td>{fmt(r['cpu']['p95'])}</td><td>{fmt(r['age']['p95'])}</td><td>{fmt(r['pace'])}x</td><td>{'within' if r['within_proposed_budget'] else 'over budget'}</td></tr>" for r in live+[soak])
    page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Fluid 2.5D · shared environment</title><style>
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#13171c;color:#e2e7ed;font:16px/1.55 system-ui,sans-serif}main{max-width:1100px;margin:auto;padding:24px}h1{font-size:2rem;line-height:1.2}h2{font-size:1.3rem;margin-top:36px}p{max-width:85ch}a{color:#8dd5f5}select,button{font:inherit;padding:8px;background:#263440;color:inherit;border:1px solid #657687;border-radius:5px}label{display:inline-block;margin:0 12px 10px 0}img,video{display:block;width:100%;height:auto;border-radius:6px;background:#000}figure{margin:16px 0}figcaption,.muted{font-size:.92rem;color:#aab8c4}table{width:100%;border-collapse:collapse;font-size:.9rem}th,td{padding:8px;text-align:left;border-bottom:1px solid #36404b}.scroll{overflow:auto}.pair{display:grid;grid-template-columns:1fr 1fr;gap:12px}.callout{border-left:3px solid #83b7cf;padding:12px 18px;background:#1b2630}summary{cursor:pointer}code{overflow-wrap:anywhere}.status{color:#a4dfb6}@media(max-width:650px){main{padding:14px}.pair{grid-template-columns:1fr}h1{font-size:1.65rem}}
</style><main>
<nav><a href="http://starship:8787/reports/#project=cubey">Cubey reports</a> · <a href="http://starship:8787/reports/3b1edf62bd0def56df30a7d9d49af487ff291fc9527eab38d4961150b58e89e1/">Previous patch-only review</a></nav>
<h1>One shared environment, unchanged water</h1>
<p class="status">Optional day/night, sky, cached cloudy reflections and restrained cloud shadows are integrated in Fluid and saved-water Terrain. Fixed daylight/clear remains the startup default.</p>
<p>This is a presentation pass: no solver math, water supply, terrain geometry, simulation grid or LOD changes. The environment uses wall time; pausing/resetting water does not reset the day or cloud wind.</p>
<h2>Read the matched views</h2>
<p>Switch time first. Noon changes the light direction, sunset reddens and dims the scene, night removes daytime water/rain glow. These are identical imported fields. “Reference daylight” is the accepted manual sun angle, not solar noon.</p>
<label>View <select id="view" disabled><option value="collection">Collection / lake</option><option value="runoff">Downhill runoff</option></select></label>
<label>Time <select id="time" disabled><option value="daylight">Reference daylight</option><option value="noon">Solar noon</option><option value="sunset">Sunset</option><option value="night">Night</option></select></label>
<label>Weather <select id="weather" disabled><option value="clear">Clear</option><option value="weather">Cloud background + reflections + 0.18 shadow</option></select></label>
<figure><a id="image-link" href="collection-daylight-clear.png"><img id="scene" src="collection-daylight-clear.png" alt="Collection view with shared reference daylight"></a><figcaption id="caption">Shared reference daylight, clear · 1k / 16 m · frozen 12-hour rain-on recording, not a running solver.</figcaption></figure>
<h2>Environment motion, not hydraulic evolution</h2>
<p>32 seconds = one 24-hour cycle (0.75 hours per wall second). Water fields are held exactly; the clip demonstrates sky/light/reflection coherence, not a new simulation. The night is dark and the accelerated sun/moon handover is still visible.</p>
<video id="cycle" controls muted loop playsinline preload="metadata" poster="collection-noon-weather.png" src="day-night-cycle.mp4"></video>
<details><summary>Six-second wind/weather and flow-cue clip; same held fields</summary><video id="wind" controls muted loop playsinline preload="metadata" src="weather-motion.mp4"></video><img src="cycle-contact.png" alt="Eight decoded times through the full cycle"><p class="muted">Contact sheet: noon, 15:00, 18:00, 21:00, midnight, 03:00, 06:00, 09:00. Existing rain/whitewater animation remains decorative and render-only.</p></details>
<h2>What do the cloud pieces contribute?</h2>
<p>The elevated collection view often contains little visible cloud: background-only clouds preserve terrain instead of painting haze over it. Reflection and shadow differences can be subtle here. Select an isolated component rather than treating every change as an improvement.</p>
<label>Component <select id="component" disabled><option value="reference">Fixed startup reference</option><option value="clear">Shared clear sky</option><option value="background">Cloud background only</option><option value="reflection">Cached reflections only</option><option value="shadow">Projected shadow only (0.35 study strength)</option><option value="weather">All three (0.18 shadow)</option></select></label>
<img id="ablation" src="collection-daylight-reference.png" alt="Fixed startup reference">
<details><summary>Lower-oblique cloud review and important limit</summary><div class="pair"><figure><img src="low-oblique-clear.png" alt="Lower-oblique clear view"><figcaption>Clear</figcaption></figure><figure><img src="low-oblique-weather.png" alt="Lower-oblique cloud view"><figcaption>Existing surface cloud V1</figcaption></figure></div><p>This lower-oblique pose makes the clouds legible, but does not qualify aerial/cloud-top views. Projected shadows use one receiver plane and soft footprint fade; they are not relief-aware cloud cascades. Diffuse light is still clear-sky irradiance, not a physically complete overcast model. Keep these couplings optional.</p></details>
<h2>Also wired into shared Terrain</h2><p>The saved-water consumer borrows the same controller and shading contracts; its capture fields and backdrop geometry/LOD remain unchanged. Normal Terrain's existing environment path still works.</p>
<div class="pair"><figure><img src="terrain-day.png" alt="Terrain captured water at noon"><figcaption>Saved Terrain water · solar noon</figcaption></figure><figure><img src="terrain-night.png" alt="Terrain captured water at night"><figcaption>Same saved fields · night</figcaption></figure></div>
<h2>Measured cost on Starship · RTX 5070 Ti · 1080p</h2>
<p>Renderer-only: two reversed-order repeats, 36 s each, 120 warmup frames, every frame sampled. Shared runs cross a full cycle, including 4 Hz capture bursts. No encoding, readback or solver inside these timing windows.</p>
<div class="scroll"><table><thead><tr><th>Grid</th><th>Mode / cloud quality</th><th>Repeat</th><th>GPU p95 ms</th><th>p99</th><th>max</th><th>CPU p95 ms</th></tr></thead><tbody>PERF_ROWS</tbody></table></div>
<p>1k is the recommended simulation grid. The 2k rows measure incremental presentation cost, not a realtime 2k solver qualification. Full cloud quality is an option, not a promoted default.</p>
<h2>With the CUDA solver actually running</h2><p>Independent 90-second fixed/shared trials plus a five-minute shared-weather soak. 20 seconds of running warmup excluded, every frame sampled in the retained timing window, 60 Hz target, 60x requested solver pace. The existing 12,000-frame profile cap limits soak timing to roughly its first 200 seconds; process/freshness supervision continues through all five minutes. Shared uses half-resolution clouds. These are offscreen Vulkan + live CUDA, not replay clips or scanout measurements.</p>
<div class="scroll"><table><thead><tr><th>Trial</th><th>GPU p95 ms</th><th>CPU p95 ms</th><th>Advancing field age p95 s</th><th>Achieved pace</th><th>8 ms / 4 ms / 1 s / 54x</th></tr></thead><tbody>LIVE_ROWS</tbody></table></div>
<p>Generation-change frame costs and full p99/max values are retained in <a href="evidence.json">evidence JSON</a>; the average alone does not describe cloud/probe bursts. Shared-GPU timings are whole-scene costs, not isolated per-process CUDA attribution.</p>
<h2>Checks and retained boundaries</h2><p>Eight fixed-default views are pixel-exact. Twenty matched views, two lower-oblique controls, four saved-Terrain views and both clips retain exact native GPU bed/depth/velocity imports. Nine initial/replacement scenes start dry; weather clock survives the managed replacements. REGRESSION_COUNT named regression groups pass without skips. All GPU evidence is hardware headless Vulkan, without validation errors.</p>
<p class="callout">Verdict: day/night is useful and integrated. Clouds are usable but camera-dependent; combined live weather narrowly exceeds the proposed 8 ms p95 GPU gate, while CPU service, solver pace and field freshness pass. Keep weather optional, especially projected shadows. No hydraulic/terrain changes, no default promotion, no push. Native GUI and human visual acceptance remain deferred.</p>
<p><a href="environment-frames.json">Capture commands</a> · <a href="environment-motion.json">Motion receipts</a> · <a href="environment-terrain.json">Terrain receipts</a> · <a href="environment-protocol.json">Frozen binary/shader/input identity</a> · <a href="final-captures.json">Pixel comparisons</a> · <a href="regression-tests.xml">Regression results</a></p>
<noscript><p>The still and video controls work without scripts; use the capture receipts to open additional stills.</p></noscript>
<script>
const $=id=>document.getElementById(id);for(const id of ['view','time','weather','component'])$(id).disabled=false;
function update(){const file=`${$('view').value}-${$('time').value}-${$('weather').value}.png`;$('scene').src=file;$('image-link').href=file;$('caption').textContent=`${$('view').selectedOptions[0].text} · ${$('time').selectedOptions[0].text} · ${$('weather').selectedOptions[0].text}. Frozen imported water fields, not a running solver.`;}
for(const id of ['view','time','weather'])$(id).addEventListener('change',update);
$('component').addEventListener('change',()=>{$('ablation').src=`collection-daylight-${$('component').value}.png`;});
</script></main></html>'''
    page=page.replace("PERF_ROWS",perf_rows).replace("LIVE_ROWS",live_rows).replace("REGRESSION_COUNT",str(len(tests)))
    (export/"index.html").write_text(page)
    print(export)


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out",type=Path)
    report(parser.parse_args().out.resolve())
