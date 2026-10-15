#!/usr/bin/env python3
"""Publishable, bounded glTF water audition export; no renderer/solver launch."""
import argparse
import gzip
import html
import json
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

import review_water_landscape_v1 as study


def checked_evidence(out, qualification, tests):
    study.QUALIFICATION_NAME = qualification
    protocol = study.verify_final(out)
    final = study.qualified_dir(out)
    confirmation = json.loads((final / "confirmation.json").read_bytes())
    performance = json.loads((final / "performance.json").read_bytes())
    films = json.loads((final / "films.json").read_bytes())
    audition = json.loads((out / "audition.json").read_bytes())
    expected_views = {(id, hours, pitch, water) for id in protocol["candidates"]
        for hours, pitch in ((10, -6.8755), (12, -6.8755), (16, -6.8755), (10, -14.8969))
        for water in (True, False)}
    rows = confirmation["rows"]
    if len(rows) != 16 or {(r["id"], r["hours"], r["pitch_deg"], r["water"]) for r in rows} != expected_views:
        raise ValueError("incomplete actual confirmation matrix")
    for r in rows:
        if study.digest(final / r["image"]) != r["sha256"]:
            raise ValueError("confirmed image changed")
        log = (final / Path(r["image"]).with_suffix(".log")).read_text()
        if "GPU bed/depth/decoded-velocity exact" not in log or "VUID-" in log or "validation error" in log.lower():
            raise ValueError("actual immutable/validation evidence missing")
    for name in ("baseline_parity", "terrain_parity"):
        if confirmation[name]["difference"]["max_abs_8bit"]:
            raise ValueError("existing viewer/shared Terrain changed")
    timings = performance["rows"]
    expected_times = {(id, water, repeat) for id in protocol["candidates"]
        for water in (True, False) for repeat in range(2)} | {("legacy", True, r) for r in range(2)}
    if len(timings) != 10 or {(r["id"], r["water"], r["repeat"]) for r in timings} != expected_times:
        raise ValueError("incomplete/duplicate profiling matrix")
    if any(r["gpu_samples"] < 100 or r["cpu_samples"] < 100 for r in timings):
        raise ValueError("insufficient complete timing samples")
    # A is the recommendation; B is retained as a comparison with its measured
    # margin-gate failure, not silently promoted or omitted from the table.
    if any(not r["gate_pass"] for r in timings if r["id"] == "mountain-local"):
        raise ValueError("recommended A misses the declared performance margin")
    if {r["id"] for r in films["rows"]} != set(protocol["candidates"]) or len(films["rows"]) != 2:
        raise ValueError("two proposal films required")
    for r in films["rows"]:
        log = (final / (r["id"] + "-film.log")).read_text()
        if "GPU bed/depth/decoded-velocity exact" not in log or "VUID-" in log or "validation error" in log.lower():
            raise ValueError("multi-frame validation/immutable evidence missing")
        for key in ("master", "gallery"):
            path = final / r[key]
            if study.digest(path) != r[key + "_sha256"]:
                raise ValueError("proposal film changed")
            probe = json.loads(subprocess.check_output(["rtk", "proxy", "ffprobe", "-v", "error",
                "-show_streams", "-show_format", "-of", "json", str(path)], text=True))
            video = [s for s in probe["streams"] if s["codec_type"] == "video"]
            if len(video) != 1 or any(s["codec_type"] == "audio" for s in probe["streams"]):
                raise ValueError("unexpected video/audio streams")
            expected_size = (1920, 1080) if key == "master" else (1280, 720)
            v = video[0]
            if ((v["width"], v["height"]) != expected_size or v["nb_frames"] != "480" or
                v["r_frame_rate"] != "60/1" or abs(float(probe["format"]["duration"]) - 8) > .05):
                raise ValueError("proposal film duration/frames/size mismatch")
    cases = ET.parse(tests).getroot().findall(".//testcase")
    if len(cases) < 26 or any(any(t.find(tag) is not None for tag in ("failure", "error", "skipped")) for t in cases):
        raise ValueError("full scoped regression results missing/failing/skipped")
    if "gltf_viewer_headless_profiles_scene" not in {t.get("name") for t in cases}:
        raise ValueError("whole-scene profiling regression not included")
    if len(audition["rows"]) != 24:
        raise ValueError("24 provisional low-angle views required")
    for r in audition["rows"]:
        if study.digest(out / r["image"]) != r["sha256"]:
            raise ValueError("initial audition changed")
    return protocol, confirmation, performance, films, audition, len(cases)


def build(out, qualification, tests, export_name=None):
    protocol, confirmation, performance, films, audition, count = checked_evidence(out, qualification, tests)
    final = study.qualified_dir(out)
    name = export_name or ("report-" + qualification)
    if Path(name).name != name or name in (".", ".."):
        raise ValueError("export name must be a single directory component")
    export = out / name
    export.mkdir()
    def copy(path, name=None):
        shutil.copyfile(path, export / (name or path.name))
    for r in confirmation["rows"]:
        copy(final / r["image"])
        copy(final / Path(r["image"]).with_suffix(".log"))
    for r in audition["rows"]:
        copy(out / r["image"])
    for name in ("protocol.json", "confirmation.json", "performance.json", "films.json"):
        copy(final / name)
    copy(final / "baseline.png")
    copy(out / "light-mountain-local-10.png", "depth-before.png")
    copy(out / "baseline-freeze.json")
    copy(out / "audition.json")
    copy(tests, "regression-tests.xml")
    study.write(export / "gpu-context.json", {"isolation": protocol["performance_gate"]["gpu_isolation"],
        "observations": [{"id": r["id"], "water": r["water"], "repeat": r["repeat"],
            "before": r["gpu_context_before"], "after": r["gpu_context_after"]} for r in performance["rows"]],
        "interpretation": "Shared-GPU boundary snapshots, not continuous monitoring or isolated shader-cost attribution. No foreign process was changed."})
    # Retain the invalid first timing attempt as an explicit instrument failure,
    # never mix its result with the qualified timing window.
    copy(out / "final/perf-mountain-local-hidden-0.log", "early-profiler-failure.log")
    copy(out / "final-v2/mountain-local-film.log", "early-multiframe-validation.log")
    for r in films["rows"]:
        copy(final / r["master"])
        copy(final / r["gallery"])
        copy(final / (r["id"] + "-film.log"))
    for r in performance["rows"]:
        prefix = f'perf-{r["id"]}-{"wet" if r["water"] else "hidden"}-{r["repeat"]}'
        original = (final / (prefix + ".passes.csv")).read_bytes()
        packed = gzip.compress(original, compresslevel=9, mtime=0)
        if gzip.decompress(packed) != original:
            raise ValueError("lossless measurement packaging failed")
        (export / (prefix + ".passes.csv.gz")).write_bytes(packed)
        copy(final / (prefix + ".log"))
    table = ''.join('<tr><td>' + html.escape(r["id"]) + (' / water shown' if r["water"] else ' / water hidden') +
        f'</td><td>{r["repeat"] + 1}</td><td>{r["gpu_p95_ms"]:.3f}</td><td>{r["cpu_service_p95_ms"]:.3f}</td>' +
        '<td>' + ('reference only' if r["id"] == "legacy" else 'pass' if r["gate_pass"] else 'MISSED margin') + '</td></tr>' for r in performance["rows"])
    raw = ''.join(f'<li><a href="perf-{r["id"]}-{"wet" if r["water"] else "hidden"}-{r["repeat"]}.passes.csv.gz">'
        f'{html.escape(r["id"])} / {"shown" if r["water"] else "hidden"} / repeat {r["repeat"] + 1} raw samples (gzip)</a></li>'
        for r in performance["rows"])
    summary = {"status": "headless correctness validated; A meets margin; B comparison retains measured miss; not promoted", "regression_groups": count,
        "provisional_views": 24, "actual_views": 16, "films": 2, "native_gui": "deferred",
        "recommended": "mountain-local / morning / clear sky / 20 degree small orbit",
        "published_media_unchanged": True, "default_promoted": False, "solver_running": False,
        "source_runtime_sha256": protocol["runtime"]["binary"],
        "new_wet_gpu_p95_ms_max": max(r["gpu_p95_ms"] for r in performance["rows"] if r["id"] != "legacy" and r["water"]),
        "new_cpu_service_p95_ms_max": max(r["cpu_service_p95_ms"] for r in performance["rows"] if r["id"] != "legacy")}
    summary["margin_gate_misses"] = [{k: r[k] for k in ("id", "water", "repeat", "gpu_p95_ms", "cpu_service_p95_ms")}
        for r in performance["rows"] if r["id"] != "legacy" and not r["gate_pass"]]
    summary["isolated_gpu_cost_qualified"] = False
    misses = summary["margin_gate_misses"]
    margin_note = ('Both candidates meet the declared ≤8ms GPU / ≤4ms CPU service margin in both repetitions.'
        if not misses else f'A meets the declared ≤8ms GPU / ≤4ms CPU service margin in both repetitions. '
        f'B misses it in {len(misses)} wet run(s), reaching {max(r["gpu_p95_ms"] for r in misses):.2f}ms; '
        'it remains a comparison, not a fully qualified promotion choice.')
    study.write(export / "qualification.json", summary)
    page = '''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>glTF + saved water: low-angle shortlist</title>
<style>body{margin:0;background:#141a21;color:#e5eaf0;font:16px/1.55 system-ui}main{max-width:1400px;margin:auto;padding:24px}h1{font-size:clamp(1.6rem,3vw,2.25rem);line-height:1.2}h2{font-size:1.3rem;margin-top:2em}p{max-width:1050px}a{color:#9fd8e5}nav{display:flex;flex-wrap:wrap;gap:14px}.notice{background:#24313a;padding:14px;border-left:4px solid #8dc7d7}.muted,figcaption{color:#bac7d1}.controls{display:flex;flex-wrap:wrap;gap:12px;align-items:center;margin:16px 0}button,select{font:inherit;background:#283640;color:#edf3f7;padding:9px;border:1px solid #748e9e;border-radius:5px;max-width:100%}button{cursor:pointer}button[aria-pressed=true]{background:#356674}button:focus-visible,select:focus-visible{outline:3px solid #b1e6ff}.pair{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}figure{margin:0}img,video{display:block;width:100%;height:auto;background:#26313b}figcaption{font-size:.9rem;margin-top:6px}details{margin:20px 0}summary{cursor:pointer}code{overflow-wrap:anywhere}pre{white-space:pre-wrap;background:#222e37;padding:12px;overflow-wrap:anywhere}.scroll{overflow:auto}table{width:100%;border-collapse:collapse;font-size:.92rem}th,td{text-align:left;padding:8px;border-bottom:1px solid #455866}section{scroll-margin-top:16px}@media(max-width:750px){main{padding:14px}.pair{grid-template-columns:1fr}h1{font-size:1.65rem}}</style></head><body><main>
<nav><a href="http://starship:8787/reports/#project=cubey">Cubey report index</a><a href="#proposal">Films</a><a href="#comparison">Water comparison</a><a href="#audition">All four landscapes</a><a href="#cost">Measurements</a></nav>
<h1>glTF + saved water: a stronger backdrop, not a new simulation</h1>
<p class="notice"><strong>Recommended: A — original mountain, sharper 32m state, morning light.</strong> Its tributaries and right-hand lake remain readable around the real helmet. B has a larger foreground pool but more coarse flooding. These are review proposals: the existing highlight, gallery, startup defaults and GitHub attachment remain unchanged.</p>
<section id="proposal"><h2>Start with the eight-second proposals</h2><p>Watch the water beside and below the helmet, not behind its solid silhouette. The foreground stays at its established screen scale. Camera and surface cues move; the saved 12h hydraulic state does not grow or evolve.</p>
<div class="controls"><button id="film-a" aria-pressed="true" disabled>A · original mountain</button><button id="film-b" aria-pressed="false" disabled>B · interior mountain</button><button id="seekable" disabled>Load clip for reliable seeking</button></div>
<figure><video id="film" controls loop muted playsinline preload="metadata" poster="mountain-local-10-low-wet.png" src="mountain-local-720p.mp4"></video><figcaption id="film-caption">A · original mountain / 32m, 10:00 solar time, exposure 0, clear sky. 8s / 60fps / 20° eased arc.</figcaption></figure><p id="film-status" class="muted" aria-live="polite">Native video controls work without JavaScript. If seeking stalls on this server, load the clip for local seeking.</p>
<p><a id="master" href="mountain-local-1080p.mp4">Download native 1080p master</a> · <a id="gallery" href="mountain-local-720p.mp4">720p gallery derivative</a> · <a href="mountain-interior-wide-720p.mp4">B clip without JavaScript</a></p>
<p class="muted">No rain streaks: this is a saved terrain backdrop, not live rainy weather. The water was captured after 12 simulated hours at an exaggerated 512mm/h supply; these are not calibrated permanent-lake predictions.</p></section>
<section id="comparison"><h2>Does water actually improve this viewpoint?</h2><p>These are actual glTF captures with matched camera, lighting, model and stored fields. Right hides only the water draw: wet-ground shading stays. Between sites, the 32m and 64m states are independent simulations, not water LOD or a pure resolution A/B.</p>
<div class="controls"><label>Site <select id="actual-site" disabled><option value="mountain-local">A · original mountain / 32m</option><option value="mountain-interior-wide">B · interior mountain / 64m</option></select></label><label>Pitch <select id="actual-pitch" disabled><option value="low">Low · 6.9°</option><option value="oblique">Oblique control · 14.9°</option></select></label><label>Solar time <select id="actual-time" disabled><option value="10">10:00</option><option value="12">12:00</option><option value="16">16:00</option></select></label></div>
<div class="pair"><figure><a id="wet-link" href="mountain-local-10-low-wet.png"><img id="wet" src="mountain-local-10-low-wet.png" width="1280" height="720" alt="Original mountain with saved water and real glTF foreground"></a><figcaption>Water shown</figcaption></figure><figure><a id="hidden-link" href="mountain-local-10-low-hidden.png"><img id="hidden" src="mountain-local-10-low-hidden.png" width="1280" height="720" alt="Matched original mountain with water hidden and wet terrain retained"></a><figcaption>Water hidden · same wet terrain</figcaption></figure></div><p id="actual-caption" class="muted">A · original mountain. Morning avoids the strong sun-facing lake glare seen at 16:00. Click images for full PNGs.</p>
<p>My verdict: water makes the landforms much richer. The restrained morning version is a useful showcase candidate. Afternoon glare can wash the lake towards white; the snow/pale-rock macro terrain is still simple and uncluttered. This pass does not claim a finished terrain-material solution.</p></section>
<details><summary>Integration issue caught: close model depth is not landscape depth</summary><p>The first real glTF render used its 0.1m near plane for distant water too. That lost depth precision and produced pronounced broken horizontal banks. The fixed path keeps the normal model projection, uses Terrain's 8m landscape near range, and remaps opaque foreground depth before water/refraction. The view pose and FOV are shared; there is still one HDR display transform. This is not a terrain-contact/underwater camera solution.</p><div class="pair"><figure><img src="depth-before.png" alt="Initial integration with broken water edges from the close model depth range" loading="lazy"><figcaption>Before · one close-range projection</figcaption></figure><figure><img src="mountain-local-10-low-wet.png" alt="Matched view after separate landscape range and foreground depth remapping" loading="lazy"><figcaption>After · foreground range remapped into landscape depth</figcaption></figure></div><p>Portable depth-mapping checks and GPU empty/near/far/remapped-layer checks pass. A linear-red reference verifies one ACES+sRGB transform, not two. Two retired frame slots and 128/192/128 target replacement retain immutable simulation fields.</p></details>
<section id="audition"><h2>Why these sites? The bounded 24-view screen</h2><p>Four existing landscapes × three headings × two low pitches. This first stage is terrain-only with a tiny 16m orbit around a foreground proxy, 8m near clip, 60° FOV and fixed reference daylight. It is provisional background evidence, not glTF foreground composition or a lighting comparison with the films.</p>
<div class="controls"><label>Landscape <select id="screen-site" disabled><option value="mountain-local">Original mountain / 32m / 65km</option><option value="mountain-wide">Original mountain / 64m / 131km</option><option value="mountain-interior-wide">Interior mountain / 64m / 131km</option><option value="valley-interior-wide">Interior valley / 64m / 131km</option></select></label><label>Heading <select id="screen-heading" disabled><option value="0">Published starting heading · −66.5°</option><option value="1">Control · 14.3°</option><option value="2" selected>Shortlist heading · 94.5°</option></select></label><label>Pitch <select id="screen-pitch" disabled><option value="0">Low · 6.9°</option><option value="1">Oblique · 14.9°</option></select></label></div>
<figure><a id="screen-link" href="mountain-local-h2-p0.png"><img id="screen" src="mountain-local-h2-p0.png" width="1280" height="720" alt="Background-only low-angle original mountain audition" loading="lazy"></a><figcaption id="screen-caption">Original mountain / sharper 32m: preferred foreground-scale detail. Wider mountain is an independent, coarser 64m storm; the valley's greater water coverage reads more like flooded sheets from this angle.</figcaption></figure></section>
<section id="cost"><h2>Bounded rendering cost · shared-GPU limitation retained</h2><p>RTX 5070 Ti, 1920×1080 genuine offscreen Vulkan, 60Hz pacing, two reversed-order repetitions. Each run is eight seconds with 120 warmup frames omitted. No Cubey solver, image readback, video encoding or validation-layer overhead inside timing windows. Whole-scene GPU p95 includes the model, shared terrain/context, water, surface cues, lighting work and final display—not just the bridge.</p><p class="notice">MARGIN_NOTE All measured GPU p95 values remain below the 16.67ms 60Hz GPU budget, which does not prove native FPS or end-to-end pacing.</p><p>Wet views top out at <strong>GPU_MAX ms GPU p95</strong>; CPU service across the new cases tops out at <strong>CPU_MAX ms</strong>. Other Python/CUDA work was observed at timing-case boundaries. These snapshots are not continuous monitoring, and the work was not interrupted. Variation is consistent with shared-GPU contention, not proven attribution; <strong>isolated renderer cost remains unqualified</strong>. <a href="gpu-context.json">Context and limitation receipt</a>.</p><div class="scroll"><table><thead><tr><th>Case</th><th>Repeat</th><th>GPU p95 ms</th><th>CPU service p95 ms</th><th>Margin gate</th></tr></thead><tbody>TABLE</tbody></table></div>
<p>The legacy reference retains its old terrain/cloud recipe at held 14:00 opening light. It is not a matched site/cloud A/B, so do not read that row as a water speedup. Shown/hidden pairs retain the same wet ground and source fields, but their p95 differences still include run/scheduling variation—not just isolated water-shader cost.</p>
<details><summary>Qualification and raw evidence</summary><p>TEST_COUNT regression groups pass without skips, including the new render-only profiling smoke and shared GPU lifecycle checks. The old glTF opening and unchanged shared Terrain control remain pixel-exact. All 16 actual captures and both full films audit GPU bed/depth/decoded velocity unchanged. Native GUI/layout acceptance is deferred; no Cubey window was launched.</p><p>The first timing attempt found a non-nestable GPU-profiler span error, not a rendering-speed result. Whole-scene timing now uses an independent query pool; the failed run is retained <a href="early-profiler-failure.log">here</a>. The earlier movie then caught an in-use lighting-descriptor update; <a href="early-multiframe-validation.log">that failed revision</a> and its timings are excluded. Final captured-sky descriptors are per-slot; two/three live-recording reuse and both complete 480-frame films pass validation.</p><p><a href="qualification.json">Qualification summary</a> · <a href="protocol.json">Final binary/shader/source/input identities and gates</a> · <a href="confirmation.json">Actual captures and exact legacy/shared parity</a> · <a href="performance.json">Measured rows and commands</a> · <a href="films.json">Film receipts</a> · <a href="regression-tests.xml">Regression results</a> · <a href="baseline-freeze.json">Original baseline freeze</a> · <a href="audition.json">Initial 24 views</a></p><ul>RAW_LINKS</ul></details></section>
<details><summary>Existing published opening and V1 boundaries</summary><figure><img src="baseline.png" width="1280" height="720" alt="Unchanged opening frame of the existing glTF terrain highlight" loading="lazy"><figcaption>Existing published recipe · unchanged. Different terrain, camera heading, exposure and clouds; not a matched rendering A/B.</figcaption></figure><p>The new path is opt-in: saved water, opaque/masked glTF assets, clear-sky daylight and a fixed/small-orbit macro backdrop. It rejects clouds, alpha/transmission, ocean, legacy terrain controls, debug output and non-ACES display in this mode. Ordinary viewer paths remain available. The foreground uses shared sky lighting, not rendered terrain/water reflections. The finite water boundary and unqualified far shadow receivers remain known limits.</p><pre>rtk proxy build/release/projects/gltf_viewer/gltf_viewer --terrain-landscape mountain-local --no-clouds --input outputs/showcase/audition-2/assets/DamagedHelmet.glb --time-of-day-mode solar --time-hours 10 --time-speed-hours-per-second 0 --capture-camera-distance-scale .70 --capture-camera-yaw 94.538 --capture-camera-pitch -6.8755 --exposure 0</pre><p>No SynxFlow, Python or CUDA runtime is needed to view a prepared capture. The native application does not prepare missing terrain or water automatically.</p></details>
<h2>Review decision</h2><p>My recommendation is to keep A as the leading replacement candidate and retain B for comparison. Review the short orbit and whether the pale macro terrain is acceptable before promoting anything. Do not expand this into another hydrology, terrain-generation or water-material study just to publish a clip.</p>
<p><a href="http://starship:8787/reports/55f06e5da5dce2a094a9b5004154524074f6915b96ac0b34929cc317e77c9b85/">Earlier wider-terrain/flooding study</a> · <a href="http://starship:8787/reports/4d2c9b639957b210136f1a88b49838f84fe81e5edf4a2ad762ac4a94f77b0e19/">Current Fluid presentation package</a> · <a href="http://starship:8787/reports/#project=cubey">All Cubey reports</a></p>
<p class="muted">Media-only attribution: Damaged Helmet by ctxwing (2018 conversion/rebuild) and theblueturtle_ (earlier model), with the retained CC-BY-NC-4.0 media boundary. Cubey source remains MIT. <a href="https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet">Upstream asset</a>. No GLB, physics payload or replacement GitHub attachment is published here.</p>
<noscript><p>JavaScript is off: the default A clip, static shown/hidden pair, both film download links and initial audition image remain usable.</p></noscript>
</main><script>
const el=id=>document.getElementById(id);let currentFilm='mountain-local',blobUrl=null;
const filmInfo={'mountain-local':{caption:'A · original mountain / 32m, 10:00 solar time, exposure 0, clear sky. 8s / 60fps / 20° eased arc.',poster:'mountain-local-10-low-wet.png'},'mountain-interior-wide':{caption:'B · interior mountain / 64m, 12:00 solar time, exposure 0, clear sky. Independent saved storm; more foreground pooling.',poster:'mountain-interior-wide-12-low-wet.png'}};
function selectFilm(id){const v=el('film');v.pause();if(blobUrl){URL.revokeObjectURL(blobUrl);blobUrl=null;}currentFilm=id;v.src=id+'-720p.mp4';v.poster=filmInfo[id].poster;v.load();el('film-caption').textContent=filmInfo[id].caption;el('master').href=id+'-1080p.mp4';el('gallery').href=id+'-720p.mp4';el('film-a').setAttribute('aria-pressed',String(id==='mountain-local'));el('film-b').setAttribute('aria-pressed',String(id!=='mountain-local'));el('film-status').textContent='Native controls ready. Use local seeking if the server cannot seek.';el('seekable').disabled=false;}
el('film-a').onclick=()=>selectFilm('mountain-local');el('film-b').onclick=()=>selectFilm('mountain-interior-wide');
el('seekable').onclick=async()=>{const button=el('seekable'),id=currentFilm;button.disabled=true;el('film-status').textContent='Loading the clip for local seeking…';try{const response=await fetch(id+'-720p.mp4');if(!response.ok)throw Error('HTTP '+response.status);const blob=await response.blob();if(id!==currentFilm)return;if(blobUrl)URL.revokeObjectURL(blobUrl);blobUrl=URL.createObjectURL(blob);const v=el('film');v.pause();v.src=blobUrl;v.load();el('film-status').textContent='Clip loaded for reliable local playback and seeking.';}catch(error){el('film-status').textContent='Local loading failed; use native controls or the download link. '+error.message;button.disabled=false;}};
const as=el('actual-site'),ap=el('actual-pitch'),at=el('actual-time');function actual(){if(ap.value==='oblique')at.value='10';for(const o of at.options)o.disabled=ap.value==='oblique'&&o.value!=='10';const base=as.value+'-'+at.value+'-'+ap.value;for(const kind of ['wet','hidden']){el(kind).src=base+'-'+kind+'.png';el(kind+'-link').href=el(kind).src;el(kind).alt=as.options[as.selectedIndex].text+' / '+at.value+':00 / '+ap.value+' / '+(kind==='wet'?'water shown':'water hidden, wet terrain retained');}el('actual-caption').textContent=as.options[as.selectedIndex].text+' · '+at.value+':00 solar time; '+ap.options[ap.selectedIndex].text+'. '+(at.value==='16'?'Sun-facing glare can wash out the lake. ':'Held saved state; model/camera/lighting match across the pair. ')+'Click images for full PNGs.';}
as.onchange=ap.onchange=at.onchange=actual;
const ss=el('screen-site'),sh=el('screen-heading'),sp=el('screen-pitch');function screen(){const file=ss.value+'-h'+sh.value+'-p'+sp.value+'.png';el('screen').src=file;el('screen-link').href=file;el('screen').alt=ss.options[ss.selectedIndex].text+' / background-only heading '+sh.value+' pitch '+sp.value;el('screen-caption').textContent=ss.options[ss.selectedIndex].text+' · background-only provisional screen; fixed reference daylight, not the real glTF composition.';}
ss.onchange=sh.onchange=sp.onchange=screen;for(const id of ['film-a','film-b','seekable','actual-site','actual-pitch','actual-time','screen-site','screen-heading','screen-pitch'])el(id).disabled=false;actual();
</script></body></html>'''
    page = page.replace("TABLE", table).replace("RAW_LINKS", raw).replace("TEST_COUNT", str(count)).replace("MARGIN_NOTE", margin_note)
    page = page.replace("GPU_MAX", f'{summary["new_wet_gpu_p95_ms_max"]:.3f}').replace("CPU_MAX", f'{summary["new_cpu_service_p95_ms_max"]:.3f}')
    (export / "index.html").write_text(page)
    study.write(export / "artifact-manifest.json", {p.name: study.digest(p) for p in sorted(export.iterdir())})
    study.verify_final(out)
    print(json.dumps({"export": str(export), "summary": summary}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--qualification-name", default="final-v3")
    parser.add_argument("--tests", type=Path, required=True)
    parser.add_argument("--export-name")
    args = parser.parse_args()
    build(args.out.resolve(), args.qualification_name, args.tests.resolve(), args.export_name)


if __name__ == "__main__":
    main()
