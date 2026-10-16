#!/usr/bin/env python3
"""Small patch-only review: held-field motion clips and checked static export."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET

import review_patch_only_v1 as review

REQUIRED_TESTS = {
    "terrain_water_capture_tests", "terrain_backdrop_surface_tests", "terrain_surface_field_tests",
    "terrain_landscape_catalog_cpu", "terrain_site_cpu", "terrain_surface_patch_lod_cpu",
    "terrain_world_water_lod_cpu", "terrain_site_water_cpu", "terrain_surface_context_cpu",
    "terrain_surface_context_edge_gpu", "terrain_surface_patch_lod_gap_gpu", "terrain_surface_patch_gap_gpu",
    "terrain_water_capture_lifecycle_gpu", "fluid_25d_terrain_alignment", "fluid_25d_terrain_demo_contract",
    "fluid_25d_terrain_surface", "fluid_25d_surface_gradient_gpu", "fluid_25d_external_session",
    "fluid_25d_terrain_presentation_review_helpers", "fluid_25d_offscreen_performance_helpers",
    "fluid_25d_terrain_demo_launcher", "fluid_25d_terrain_site_catalogue", "fluid_25d_world_water_study_helpers",
    "fluid_25d_external_worker_host", "fluid_25d_external_worker_build_helpers", "fluid_25d_terrain_demo_supervisor",
    "fluid_25d_terrain_demo_evidence_helpers", "fluid_25d_terrain_review_helpers", "fluid_25d_whitewater_gpu",
    "fluid_25d_scenic_gpu", "fluid_25d_tests", "terrain_landscape_prepare_helpers",
    "terrain_prepared_landscape_review_helpers", "terrain_config_tests", "terrain_source_catalog_tests",
    "terrain_site_package_helpers", "terrain_surface_model_tests", "terrain_capture_preparation_cpu",
    "terrain_landscape_hardening_review_helpers", "terrain_landscape_presentation_cpu",
    "terrain_landscape_first_use_review_helpers", "terrain_wide_view_study_helpers",
    "fluid_25d_patch_only_review_helpers", "fluid_25d_recording_contract", "fluid_25d_backend_adapters",
}


def checked_regressions(path):
    tests = ET.parse(path).getroot().findall(".//testcase")
    if not REQUIRED_TESTS <= {t.get("name") for t in tests} or any(
            any(t.find(k) is not None for k in ("failure", "error", "skipped")) for t in tests):
        raise ValueError("missing/failed/skipped regression evidence")
    return tests


def checked_installs(life):
    installs=[e for e in life["events"] if e["event"] in ("initial", "installed")]
    if (len(installs)!=9 or len({e["session_id"] for e in installs})!=9 or
            any(e["physical_s"]!=0 or e["hydraulic_dispatches"]!=0 for e in installs) or
            any(r["active_group_remaining"] or r["escalated"] for r in life["cleanup"])):
        raise ValueError("initial scene and replacements must start dry with clean ownership")
    return installs


def clips(out):
    review.verify(out,"final")
    recording=review.CASES["world1k"][1]
    meta=json.loads(recording.read_bytes())
    pairs=[(a,b) for a,b in zip(meta["frames"],meta["frames"][1:])
        if b["time_s"]-a["time_s"]>143]
    if not pairs:
        raise ValueError("clip must stay within one held native state")
    start=pairs[-1][0]["time_s"]
    rows=[]
    for camera in ("overview","runoff","collection"):
        args=review.command("world1k",camera)
        args[args.index("--fluid25d-recording-time-seconds")+1]=str(start)
        name="motion-"+camera
        receipt=review.checked(args+["--capture","video","--frames","144","--fps","24",
            "--fluid25d-recording-frame-interval-seconds","1","--output",str(out/(name+".mp4"))],out/(name+".log"))
        raw=(out/(name+".log")).read_text()
        times=re.findall(r"fluid_25d_recording_capture:.*saved_s=(\S+)",raw)
        if len(times)!=144 or len(set(times))!=1 or "fluid_25d_recording_upload: PASS" not in raw:
            raise ValueError("motion clip changed native fields or lacks audit")
        probe=subprocess.run(["rtk","proxy","ffprobe","-v","error","-count_frames","-select_streams","v:0",
            "-show_entries","stream=nb_read_frames,width,height,r_frame_rate","-of","json",str(out/(name+".mp4"))],
            check=True,capture_output=True,text=True,timeout=30)
        stream=json.loads(probe.stdout)["streams"][0]
        if (stream["width"],stream["height"],stream["nb_read_frames"],stream["r_frame_rate"])!=(1280,720,"144","24/1"):
            raise ValueError("invalid clip dimensions or frame count")
        review.write(out/(name+"-probe.json"),stream)
        rows.append({"camera":camera,"path":name+".mp4","saved_time_s":float(times[0]),
            "frame_count":144,"fps":24,"native_fields":"held; decorative flow/rain cues only",**receipt})
        print("captured motion",camera,flush=True)
    review.write(out/"clips.json",{"rows":rows})
    review.verify(out,"final")


def controls(out):
    review.verify(out,"final")
    args=review.command("ordinary1k","runoff")
    args[args.index("--fluid25d-native-terrain-context")+1]="on"
    receipt=review.render.run(args,out/"retired-on.log",1)
    raw=(out/"retired-on.log").read_text()
    if "Use the Terrain application" not in raw or "NVIDIA GeForce" in raw:
        raise ValueError("retired context-on must reject clearly before GPU startup")
    legacy=review.command("ordinary1k","runoff")
    image=out/"legacy-off.png"
    alias=review.checked(legacy+["--fluid25d-native-terrain-context","off","--output",str(image)],out/"legacy-off.log")
    diff=review.render.pixel_difference(image,out/"final-ordinary1k-runoff.png")
    if diff["max_abs_8bit"]: raise ValueError("explicit legacy off changed patch image")
    ordinary=review.demo.catalogue(review.ORDINARY)
    world=review.demo.catalogue(review.WORLD,review.demo.load_sites(review.demo.HERE/"terrain_water_world_sites.json"))
    if len(ordinary)!=30 or len(world)!=15 or not all(r["available"] for r in ordinary+world):
        raise ValueError("prepared selections were lost")
    terrain_runtime=review.render.runtime()
    old=review.ROOT/"outputs/terrain/wide-view-trial-20261013-RCCBO6/protocol.json"
    if terrain_runtime != json.loads(old.read_bytes())["runtime"]:
        raise ValueError("shared Terrain application runtime changed")
    review.write(out/"controls.json",{"retired_on":receipt,"explicit_off":alias,"off_difference":diff,
        "ordinary_available":30,"world_available":15,"terrain_runtime_unchanged":True,
        "terrain_runtime":terrain_runtime})
    review.verify(out,"final")


def report(out):
    review.verify(out,"final")
    captured=json.loads((out/"final-captures.json").read_bytes())["rows"]
    if len(captured)!=8 or {(r["case"],r["camera"]) for r in captured}!=set(review.VIEWS):
        raise ValueError("incomplete frame matrix")
    if any(r["difference"]["max_abs_8bit"] for r in captured if r["case"].startswith("ordinary")):
        raise ValueError("ordinary patch rendering regressed")
    motion=json.loads((out/"clips.json").read_bytes())["rows"]
    perf=json.loads((out/"performance.json").read_bytes())["rows"]
    controls=json.loads((out/"controls.json").read_bytes())
    life=json.loads((out/"lifecycle/validation.json").read_bytes())
    live=[json.loads((out/f"live-perf-{i}/validation.json").read_bytes()) for i in range(2)]
    if len(motion)!=3 or len(perf)!=6 or life["errors"] or life["returncode"]:
        raise ValueError("missing/failed qualification")
    for row in live:
        if (row["errors"] or row["returncode"] or not row["headless_only"] or
            row["app_sha256"] != review.runtime()["app"] or row["viewport"] != [[1920,1080]] or
            any(r["active_group_remaining"] or r["escalated"] for r in row["cleanup"]) or
            row["physical_wall_ratio"] is None or row["physical_wall_ratio"]<54):
            raise ValueError("live performance/lifecycle failed")
        for key in ("fluid_25d.external.publication_freshness_s","fluid_25d.external.advancing_state_age_s"):
            if row["metrics"][key]["p95"]>1: raise ValueError("live field age failed")
        if row["passes_ms"]["gpu:fluid_25d native presentation total"]["p95"]>8 or row["passes_ms"]["cpu:headless.frame_service"]["p95"]>4:
            raise ValueError("live scene/service budget failed")
    tests=checked_regressions(out/"regression-tests.xml")
    installs=checked_installs(life)
    gpu=[r["gpu_p95_ms"] for r in perf if r["case"]!="ordinary2k"]
    if max(gpu)>8 or any(r["cpu_p95_ms"]>4 for r in perf): raise ValueError("1k renderer/service budget failed")
    exports=out/"export";exports.mkdir()
    for row in captured: shutil.copy2(out/row["image"],exports/row["image"])
    for row in motion: shutil.copy2(out/row["path"],exports/row["path"])
    for name in ("final-protocol.json","final-captures.json","performance.json","clips.json","controls.json","regression-tests.xml"):
        shutil.copy2(out/name,exports/name)
    review.write(exports/"live-summary.json",{"lifecycle":{"installs":installs,"errors":life["errors"],
        "cleanup":life["cleanup"],"scope":"switching trial; mixed metrics are not steady-state timings"},
        "repeats":[{k:r[k] for k in ("physical_wall_ratio","publication_age_s","metrics","passes_ms","frame_ms","cleanup","warmup_seconds")} for r in live]})
    summary={"status":"qualified headlessly; native GUI deferred","ordinary_exact_frames":4,"world_projection_differences_recorded":4,
        "zero_exterior_mesh":True,"terrain_foundation_unchanged":controls["terrain_runtime_unchanged"],
        "regression_groups":len(tests),"dry_installs":9,"frozen_inputs":len(json.loads((out/"final-protocol.json").read_bytes())["inputs"]),
        "no_shader_or_solver_changes":True,"verdict":"patch-only contract complete; no further water-render change justified by this review"}
    review.write(exports/"summary.json",summary)
    table=''.join(f'<tr><td>{r["case"]} / {r["repeat"]+1}</td><td>{r["gpu_p95_ms"]:.3f}</td><td>{r["cpu_p95_ms"]:.3f}</td></tr>' for r in perf)
    live_table=''.join(f'<tr><td>{i+1}</td><td>{r["physical_wall_ratio"]:.2f}×</td><td>{r["passes_ms"]["gpu:fluid_25d native presentation total"]["p95"]:.3f}</td><td>{r["passes_ms"]["cpu:headless.frame_service"]["p95"]:.3f}</td><td>{r["metrics"]["fluid_25d.external.advancing_state_age_s"]["p95"]:.3f}</td></tr>' for i,r in enumerate(live))
    document='''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Fluid 2.5D: simulation patch only</title><style>
body{background:#161d24;color:#e6edf3;font:16px/1.55 system-ui;margin:0}main{max-width:1280px;margin:auto;padding:24px}h1{font-size:clamp(1.6rem,4vw,2.3rem);line-height:1.2}h2{font-size:1.3rem;margin-top:32px}a{color:#99d5e3}img,video{width:100%;height:auto;background:#222}select{font:inherit;color:inherit;background:#263747;padding:8px;max-width:100%}figure{margin:16px 0}details{margin:24px 0}summary{cursor:pointer}.controls{display:flex;gap:16px;flex-wrap:wrap;align-items:center}.muted{color:#b4c1cc}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}.scroll{overflow:auto}table{width:100%;border-collapse:collapse;font-size:.9rem}th,td{padding:8px;text-align:left;border-bottom:1px solid #465667}code{overflow-wrap:anywhere}@media(max-width:750px){.pair{grid-template-columns:1fr}main{padding:16px}}
</style></head><body><main><h1>Fluid 2.5D: the simulation patch only</h1>
<p>Fluid renders the complete selected simulation grid. No surrounding terrain, outer LOD mesh or exterior shadow casters. The shared Terrain foundation keeps its wider backdrop and LOD support.</p><p><strong>This is a scope/UX cleanup, not a new water material.</strong> The ordinary collection was already patch-only; all four ordinary replay controls remain pixel-exact. Prepared world sites now skip exterior mesh construction/upload, not merely hide it.</p>
<h2>Read the actual Fluid renderer</h2><p>Start with local runoff. Blue water follows channels; whitewater and dots/trails provide motion cues. Full patch is a layout overview, not a distant landscape-quality test. The rectangular boundary is deliberately the simulation boundary.</p>
<div class="controls"><label>View <select id="view" disabled><option value="runoff">Local runoff</option></select></label><label><input id="motion" type="checkbox" disabled> Play motion cues</label></div>
<figure><a id="image-link" href="final-world1k-runoff.png"><img id="scene" src="final-world1k-runoff.png" width="1280" height="720" alt="Full-resolution local mountain runoff inside the simulation patch"></a><video id="clip" controls loop muted playsinline preload="metadata" hidden src="motion-runoff.mp4"></video><figcaption id="caption">Prepared-world mountain · 1024² / 16m · native fields saved after roughly twelve hours at 512mm/h rain. No solver runs for these review captures.</figcaption></figure>
<p class="muted">The six-second clips hold one native bed/depth/velocity state while rain, foam and dots/trails animate. They demonstrate surface motion cues, not advancing hydrology. Separate live trials below verify a running solver. Click a still for its original PNG.</p><noscript><p>JavaScript is off; the local still and evidence links remain available.</p></noscript>
<h2>What changed</h2><ul><li>Fluid no longer constructs or uploads exterior terrain, including for world-site inputs.</li><li>The wider-terrain toggle is gone. <code>--fluid25d-native-terrain-context off</code> remains compatible; <code>on</code> rejects before GPU startup and directs users to Terrain.</li><li>“Fit simulation patch” and “Full patch” describe the camera correctly. World/site identities and shared climate/material organization remain checked.</li><li>Camera clipping and shadows use the patch only. Grid/site selection, live/replay, rain, pace, reset, diagnostic views and solver alternatives are unchanged.</li></ul>
<h2>Visual verdict</h2><p>Local runoff and collection are readable enough to retain the current water treatment. No shader/material or solver changes were made. We are not chasing the former wide-map artifacts as Fluid quality gates.</p><p>The most obvious remaining presentation issue is framing: the full-patch overview leaves substantial empty background and makes streams small. A tighter fit or a better initial local camera is worth a separate UX pass. No additional render fix is forced into this loop.</p>
<details><summary>Ordinary/default collection and 2k controls</summary><div class="pair"><figure><img src="final-ordinary1k-runoff.png" alt="Ordinary default collection local runoff" loading="lazy"><figcaption>Ordinary mountain · 1k/16m · unchanged pixels</figcaption></figure><figure><img src="final-ordinary2k-runoff.png" alt="Ordinary 2k full-resolution runoff control" loading="lazy"><figcaption>Ordinary mountain · 2k/8m · unchanged pixels; different saved state</figcaption></figure></div><p>These are different captured inputs from the main prepared-world gallery, not an A/B comparison of simulation accuracy or a new generated site.</p></details>
<details><summary>Qualification and performance</summary><p>@@TESTS@@ regression groups pass without skips. Eight final replay views audit exact native fields. Four ordinary controls are pixel-exact; four world-site views record small raster differences after camera clip-range removal (mean absolute channel difference 0.010–0.031/255). They are not claimed pixel-exact.</p><p>Managed live switching installs nine dry/paused scenes with exact field audits, including all five grid/area presets, rain/pace/reset and a different world site. Owned processes shut down cleanly. Switching metrics are not steady-state throughput evidence.</p><p>Renderer-only: RTX 5070 Ti, 1080p headless Vulkan, 120 warmup frames excluded, two reversed-order repeats, no solver/readback/encoding. The 1k budget remains GPU ≤8ms / CPU service ≤4ms. 2k is an advanced measured cost, not a promise of default-grid performance.</p><div class="scroll"><table><tr><th>Case / repeat</th><th>GPU p95 ms</th><th>CPU service p95 ms</th></tr>@@TABLE@@</table></div><p>Live default: two separate 60s dry-start runs with a CUDA worker, 20s running warmup excluded, 60Hz cap and 60× requested pace. Not a long wet-state soak or native-display FPS qualification.</p><div class="scroll"><table><tr><th>Repeat</th><th>Measured pace</th><th>GPU p95 ms</th><th>CPU p95 ms</th><th>Advancing field age p95 s</th></tr>@@LIVE@@</table></div><p>No native Cubey window was opened. Panel wording is implemented and headless behavior is checked; native panel/layout acceptance remains deferred.</p></details>
<details><summary>Evidence and related studies</summary><p><a href="summary.json">Summary</a> · <a href="final-protocol.json">Frozen runtime/inputs</a> · <a href="final-captures.json">Replay comparisons</a> · <a href="clips.json">Held-field clip receipts</a> · <a href="controls.json">Compatibility/foundation checks</a> · <a href="performance.json">Renderer timings</a> · <a href="live-summary.json">Live and lifecycle evidence</a> · <a href="regression-tests.xml">Regression results</a></p><p><a href="http://starship:8787/reports/55f06e5da5dce2a094a9b5004154524074f6915b96ac0b34929cc317e77c9b85/">Previous wide-landscape study</a> is a separate backdrop/stress experiment, not the Fluid demo’s target. <a href="http://starship:8787/reports/#project=cubey">Cubey report index</a></p></details>
</main><script>const names={overview:'Full simulation patch',runoff:'Local runoff',collection:'Local collection / pool'};const id=x=>document.getElementById(x);const view=id('view'),motion=id('motion');view.innerHTML=Object.entries(names).map(([v,n])=>`<option value="${v}">${n}</option>`).join('');view.value='runoff';
function update(){const v=view.value;id('scene').src='final-world1k-'+v+'.png';id('scene').alt=names[v]+' · full-resolution simulation patch only';id('image-link').href=id('scene').src;id('scene').hidden=motion.checked;id('image-link').hidden=motion.checked;id('clip').hidden=!motion.checked;id('clip').pause();id('clip').src='motion-'+v+'.mp4';if(motion.checked)id('clip').play().catch(()=>{});id('caption').textContent=names[v]+' · prepared-world mountain, 1024² / 16m. '+(motion.checked?'Held native fields; animated motion cues only, not a running solver.':'Saved rain-on state; unchanged water rendering.');}view.onchange=update;motion.onchange=update;view.disabled=motion.disabled=false;update();</script></body></html>'''
    document=document.replace('@@TESTS@@',str(len(tests))).replace('@@TABLE@@',table).replace('@@LIVE@@',live_table)
    with (exports/"index.html").open("x") as stream:stream.write(document)
    review.write(out/"report-receipt.json",{**summary,"export_assets":{p.name:review.render.digest(p) for p in sorted(exports.iterdir()) if p.is_file()}})
    review.verify(out,"final")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out",type=Path)
    parser.add_argument("--phase",choices=("clips","controls","report"),required=True)
    args=parser.parse_args();globals()[args.phase](args.out.resolve())


if __name__=="__main__":main()
