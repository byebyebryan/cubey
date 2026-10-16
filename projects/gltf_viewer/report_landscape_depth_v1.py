#!/usr/bin/env python3
"""Export the bounded depth-range experiment and its retained controls."""
import argparse
import html
import json
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

import review_landscape_depth_v1 as review


def export(out):
    review.verify(out)
    captures = json.loads((out / "captures.json").read_bytes())
    if len(captures["rows"]) != 5 or {r["case"] for r in captures["rows"]} != set(review.CASES):
        raise ValueError("incomplete control matrix")
    for row in captures["rows"]:
        if review.base.digest(out / (row["case"] + "-1080p.mp4")) != row["master_sha256"]:
            raise ValueError("master film changed")
    tests = ET.parse(out / "regression-tests.xml").getroot().findall(".//testcase")
    if len(tests) != 21 or any(t.find("failure") is not None or t.find("error") is not None or t.find("skipped") is not None for t in tests):
        raise ValueError("regression matrix failed or incomplete")
    parity = json.loads((out / "parity.json").read_bytes())
    if any(r["max_abs_8bit"] for r in parity.values()):
        raise ValueError("old glTF/legacy controls changed")
    motion = json.loads((out / "motion-statistics.json").read_bytes())
    performance = json.loads((out / "performance.json").read_bytes())
    dest = out / "export"
    dest.mkdir()
    files = ["protocol.json", "captures.json", "regression-tests.xml", "parity.json", "motion-statistics.json", "performance.json", "source-diff.patch"]
    for name in review.CASES:
        files += [name + "-720p.mp4", name + ".png", name + ".log"]
    for name in ("legacy", "fitted"):
        files.append(name + "-1080p.mp4")
    files += [p.name for p in out.glob("perf-*.log")]
    files += [p.name for p in out.glob("perf-*.passes.csv")]
    for name in files:
        shutil.copy2(out / name, dest / name)
    get = lambda case, region: next(r["mean_adjacent_luma_difference"] for r in motion["rows"] if r["case"] == case and r["region"] == region)
    reduction = 100 * (1 - get("fitted", "distant_shallows") / get("legacy", "distant_shallows"))
    stats = "".join(f'<tr><td>{html.escape(name)}</td><td>{get(name,"distant_shallows"):.3f}</td><td>{get(name,"near_bare_slope"):.3f}</td><td>{get(name,"large_pool"):.3f}</td></tr>' for name in review.CASES)
    perf = "".join(f'<tr><td>{r["case"]}</td><td>{r["repeat"]+1}</td><td>{r["gpu_p95_ms"]:.3f}</td><td>{r["cpu_service_p95_ms"]:.3f}</td></tr>' for r in performance["rows"])
    diagnostics = "".join(f'<figure><video controls loop muted playsinline preload="metadata" poster="{name}.png" src="{name}-720p.mp4"></video><figcaption>{label} · <a href="{name}-720p.mp4">download</a></figcaption></figure>' for name, label in (("split-8m", "Tight foreground, but background near still 8m"), ("legacy-no-shadow", "Legacy depth, sun shadows disabled"), ("fitted-no-shadow", "Fitted depth, sun shadows disabled")))
    ranges = "\n".join(r["depth_range"] for r in captures["rows"])
    document = '''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>glTF saved water: depth precision comparison</title><style>
body{margin:0;background:#141a21;color:#e5eaf0;font:16px/1.55 system-ui}main{max-width:1500px;margin:auto;padding:24px}a{color:#a4dce8}h1{font-size:clamp(1.6rem,3vw,2.3rem);line-height:1.2}h2{font-size:1.3rem;margin-top:2em}p{max-width:1100px}.notice{padding:14px;background:#24313a;border-left:4px solid #8dc7d7}.pair,.diagnostics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}figure{margin:0}video,img{display:block;width:100%;height:auto;background:#26313b}figcaption,.muted{color:#becbd5}button{font:inherit;background:#2c3e4d;color:inherit;padding:10px;border:1px solid #849dab;border-radius:5px}button:focus-visible{outline:3px solid #c5efff}.controls{display:flex;gap:10px;flex-wrap:wrap;margin:16px 0}.scroll{overflow:auto}table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:8px;border-bottom:1px solid #43545f}pre{white-space:pre-wrap;overflow-wrap:anywhere}details{margin:24px 0}nav{display:flex;gap:16px;flex-wrap:wrap}@media(max-width:750px){main{padding:14px}.pair,.diagnostics{grid-template-columns:1fr}}
</style></head><body><main><nav><a href="/reports/#project=cubey">Cubey report index</a><a href="http://starship:8787/reports/33eee7eb4e01f7080efad68e70d4c300bfe8015faae9c47a9310a545ebf4a1a0/">Previous site audition</a></nav>
<h1>Same A orbit, better-separated depth ranges</h1><p class="notice">The fitted background range reduces the distant shallow-water instability. Disabling sun shadows does not produce the same improvement. This supports depth precision as a major contributor, not a claim that every subpixel stream is now perfectly antialiased.</p>
<p>Original mountain A, same held 12h storm state, 10:00 solar time, model scale, 60° vertical FOV and 20° eased orbit. Only depth ranges differ below. No water simulation, terrain generation, smoothing, geometry displacement or optics tuning. Genuine headless Vulkan, 1080p60 source; 720p browser derivatives.</p>
<h2>Watch the shallow blue channels—not the helmet</h2><p>Start with the left-hand distant floodplain and the thin hillside streams. The large lake should stay similar. Load the clips for reliable local seeking, then play together. Open native 1080p masters to judge fine shimmer; these side-by-side previews are downscaled.</p>
<div class="controls"><button id="load" disabled>Load clips for local seeking</button><button id="play" disabled>Play both from start</button><button id="pause" disabled>Pause all</button><button id="middle" disabled>Both at 4 seconds</button></div><p id="status" aria-live="polite" class="muted">Native controls and download links work without JavaScript.</p>
<div class="pair"><figure><video id="before" controls loop muted playsinline preload="metadata" poster="legacy.png" src="legacy-720p.mp4"></video><figcaption>Before · foreground 0.1m–81.9km; background 8m–81.9km.<br><a href="legacy-1080p.mp4">Native 1080p master</a> · <a href="legacy.png">Native midpoint frame</a></figcaption></figure><figure><video id="after" controls loop muted playsinline preload="metadata" poster="fitted.png" src="fitted-720p.mp4"></video><figcaption>Fitted · foreground 0.01m–19m; background 531.5m–98.9km.<br><a href="fitted-1080p.mp4">Native 1080p master</a> · <a href="fitted.png">Native midpoint frame</a></figcaption></figure></div>
<h2>Why this is not just a larger arbitrary near plane</h2><p>The camera fit conservatively encloses the actual retained paired ground/water triangles and exterior terrain. Triangle-cluster distance bounds are prepared once, inflated for CPU/GPU evaluation roundoff, then combined with the entire allowed camera orbit, the frustum corner angle, and a 2× near-plane safety guard. Yaw, pitch and zoom within that orbit do not pump the range. Resizing accounts for the new aspect ratio. A requested near-plane override above the conservative bound fails rather than silently clipping terrain.</p><p>The foreground keeps its own asset-scale projection. Existing depth remapping preserves model occlusion; shared lighting and the single HDR display transform remain. The original glTF startup and the explicit legacy landscape control remain pixel-exact. This is still the bounded opaque/masked, model-above-landscape showcase—not general ground contact or free-flight compositing.</p>
<h2>Controls: shadows versus depth</h2><div class="diagnostics">DIAGNOSTICS</div>
<p>“Split 8m” tightens the foreground range and fits the background far distance, but retains an 8m background near plane. It stays close to legacy in the temporal diagnostic. Shadows-off also stays close to its matching depth configuration.</p>
<details><summary>Measured motion differences and limits</summary><p>Over source frames 180–300, the distant-shallows crop's mean adjacent-frame luma difference falls by <strong>REDUCTION%</strong>. This includes expected camera motion and encoding; it is <strong>not a reprojected shimmer score</strong>, a perceptual quality percentage or proof that every flicker was caused by Z fighting. Visibility comparison and depth-based optical reconstruction both benefit from better precision. Residual tiny stream coverage and terrain-normal aliasing remain possible.</p><div class="scroll"><table><thead><tr><th>Case</th><th>Distant shallows</th><th>Near bare slope</th><th>Large pool</th></tr></thead><tbody>STATS</tbody></table></div><p><a href="motion-statistics.json">Crop coordinates and measurements</a>. Native 1080p clips should decide visual acceptance, not this scalar alone.</p></details>
<details><summary>Regression checks, preparation and bounded performance</summary><p>All 21 scoped groups pass, including conservative triangle-distance bounds, frustum-corner/orbit fitting, unsafe override rejection, numerical float32 depth precision at 20km, foreground occlusion, range remapping, retired frame slots and resize lifecycle. Both complete 480-frame movies and all diagnostic movies pass Vulkan validation and exact immutable GPU bed/depth/velocity audits. No hydraulic dispatches.</p><p>Bounds preparation is CPU-only once (about 136ms in preflight); no new draw pass, GPU geometry upload or per-frame triangle scan. Timings below are two reversed-order eight-second 1080p offscreen repeats, 120 warmup frames omitted, 60Hz pacing. No image readback, encoding, validation overhead or solver inside measured windows. Shared GPU; foreign jobs were not controlled. These are not isolated incremental-cost or native-GUI FPS claims.</p><div class="scroll"><table><thead><tr><th>Case</th><th>Repeat</th><th>Whole GPU p95 ms</th><th>CPU service p95 ms</th></tr></thead><tbody>PERF</tbody></table></div><p><a href="performance.json">Timing and GPU context</a> · <a href="regression-tests.xml">Tests</a> · <a href="parity.json">Pixel-exact controls</a> · <a href="captures.json">Commands and film hashes</a> · <a href="protocol.json">Frozen inputs/runtime</a> · <a href="source-diff.patch">Tracked implementation diff</a></p></details>
<details><summary>Exact depth ranges</summary><pre>RANGES</pre></details><p class="muted">Fitted ranges are the working setting for the opt-in saved-landscape path. Normal glTF startup, Fluid 2.5D, Terrain defaults and published gallery media are unchanged. Native Cubey GUI acceptance and final showcase promotion remain deferred. No commits or push in this trial.</p>
</main><script>
const videos=[...document.querySelectorAll('video')],left=document.getElementById('before'),right=document.getElementById('after'),el=id=>document.getElementById(id);let urls=[];
for(const id of ['load','play','pause','middle'])el(id).disabled=false;
el('load').onclick=async()=>{el('load').disabled=true;el('status').textContent='Loading five small browser clips for local seeking…';try{await Promise.all(videos.map(async v=>{const response=await fetch(v.getAttribute('src'));if(!response.ok)throw Error('HTTP '+response.status);const url=URL.createObjectURL(await response.blob());urls.push(url);v.pause();v.src=url;v.load();await new Promise((resolve,reject)=>{v.addEventListener('loadedmetadata',resolve,{once:true});v.addEventListener('error',reject,{once:true});});}));el('status').textContent='All clips loaded. Native playback and seeking are ready.';}catch(e){el('status').textContent='Local loading failed; use download links. '+e;el('load').disabled=false;}};
el('play').onclick=async()=>{left.pause();right.pause();left.currentTime=right.currentTime=0;try{await Promise.all([left.play(),right.play()]);el('status').textContent='Playing the matched orbit. Fine differences are clearest in native 1080p.';}catch(e){el('status').textContent='Use Load clips or the native play controls. '+e.message;}};
el('pause').onclick=()=>{videos.forEach(v=>v.pause());el('status').textContent='All clips paused.';};el('middle').onclick=()=>{videos.forEach(v=>v.pause());left.currentTime=right.currentTime=4;el('status').textContent='Both comparison clips paused at 4 seconds.';};
left.addEventListener('timeupdate',()=>{if(!left.paused&&!right.paused&&Math.abs(left.currentTime-right.currentTime)>.06)right.currentTime=left.currentTime;});
</script></body></html>'''
    for token, value in {"DIAGNOSTICS": diagnostics, "REDUCTION": f"{reduction:.1f}", "STATS": stats, "PERF": perf, "RANGES": html.escape(ranges)}.items():
        document = document.replace(token, value)
    (dest / "index.html").write_text(document)
    review.base.write(dest / "bundle.json", {"files": {p.name: review.base.digest(p) for p in dest.iterdir() if p.is_file()},
        "scope": "Final depth-range study only; earlier preflight retained outside this export."})
    print(dest)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    export(parser.parse_args().out.resolve())
