#!/usr/bin/env python3
"""Build a compact, self-contained film review; does not publish or install."""
import argparse
import html
import json
from pathlib import Path
import shutil
import subprocess

import review_showcase_solar_v1 as review
from package_landscape_showcase_v1 import check_video, probe


def build(out):
    review.verify(out)
    export = out / "report"
    export.mkdir()
    package = out / "package"
    recipe = json.loads((package / "gltf-terrain-recipe.json").read_bytes())
    for name, descriptor in recipe["media"].items():
        path = package / name
        if path.stat().st_size != descriptor["bytes"] or review.base.digest(path) != descriptor["sha256"]:
            raise ValueError("packaged candidate changed: " + name)
    repeat = json.loads((out / "repeat.json").read_bytes())
    if not repeat["all_480_decoded_frames_exact"] or repeat["source_decoded_sha256"] != repeat["repeat_decoded_sha256"]:
        raise ValueError("repeat parity is not qualified")
    for name in ("gltf-terrain.mp4", "gltf-terrain.png", "gltf-terrain-recipe.json"):
        shutil.copy2(package / name, export / name)
    for name, source in {
        "current.mp4": review.base.ROOT / "docs/media/showcase/gltf-terrain.mp4",
        "current.png": review.base.ROOT / "docs/media/showcase/gltf-terrain.png",
        "current-recipe.json": review.base.ROOT / "docs/media/showcase/gltf-terrain-recipe.json",
        "current-publication.json": review.base.ROOT / "docs/media/showcase/gltf-terrain-publication.json",
        "night-blocks.png": out / "front-45-solar-360.png",
        "night-control.png": out / "dusk-auto.png",
        "night-controls.json": out / "night-controls.json",
        "exposure-controls.json": out / "exposure-controls.json",
        "protocol.json": out / "protocol.json",
        "captures.json": out / "refined-captures.json",
        "repeat.json": out / "repeat.json",
    }.items():
        shutil.copy2(source, export / name)
    row = json.loads((out / "refined-captures.json").read_bytes())["rows"][0]
    for frame in row["samples"]:
        shutil.copy2(out / frame["image"], export / frame["image"])
    for orbit in review.ORBITS:
        subprocess.run(["rtk", "proxy", "ffmpeg", "-v", "error", "-n", "-i",
            str(out / f"front-{orbit}-solar-1080p.mp4"), "-an", "-vf",
            "scale=1280:720:flags=lanczos,setpts=N/(60*TB)", "-r", "60", "-fps_mode", "cfr",
            "-frames:v", "480", "-c:v", "libx264", "-preset", "medium", "-crf", "24",
            "-pix_fmt", "yuv420p", "-movflags", "+faststart",
            str(export / f"initial-{orbit}.mp4")], check=True)
    clips = []
    for name in ("gltf-terrain.mp4", "current.mp4", "initial-30.mp4", "initial-45.mp4"):
        info = probe(export / name)
        check_video(info, 1280, 720)
        clips.append({"path": name, "bytes": (export / name).stat().st_size,
                      "sha256": review.base.digest(export / name), "probe": info})
    review.base.write(export / "media-audit.json", {"clips": clips, "frozen_fields_verified": True,
        "boundary": "Headless Vulkan film acceptance and package audit, not owner/native GUI acceptance or live solver performance."})

    def clock(hours):
        minutes = int(hours * 60 + .5)
        return f"{minutes // 60:02}:{minutes % 60:02}"

    strip = ''.join(f'<figure><img src="{html.escape(f["image"])}" alt="Frame {f["frame"]}: '
        f'solar {clock(f["solar_hours"])}"><figcaption>{f["seconds"]:.2f}s · '
        f'{clock(f["solar_hours"])} · yaw {f["yaw_degrees"]:.1f}°</figcaption></figure>'
        for f in row["samples"])
    body = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>glTF showcase — front view and day-to-night</title><style>
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#111820;color:#e3ebf2;font:16px/1.55 system-ui}main{max-width:1450px;margin:auto;padding:26px}a{color:#80d9ea}h1,h2{line-height:1.2}.muted,figcaption{color:#acbdcb;font-size:14px}.lead{max-width:1000px}.badge{display:inline-block;background:#264b42;padding:4px 12px;border-radius:20px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:18px}.card,details{background:#1b2631;border:1px solid #344553;border-radius:10px;padding:16px;margin:18px 0}video,img{max-width:100%;height:auto;display:block}video{width:100%;aspect-ratio:16/9;background:#000}figure{margin:0}figcaption{margin:6px 0}.strip{display:grid;grid-template-columns:repeat(5,1fr);gap:10px}button{padding:8px 15px;background:#244251;color:#edfaff;border:1px solid #6197ab;border-radius:5px;cursor:pointer}input{max-width:100%;width:480px}summary{cursor:pointer}code{overflow-wrap:anywhere}pre{overflow:auto;padding:12px;background:#0d131a;font-size:13px}@media(max-width:850px){.grid{grid-template-columns:1fr}.strip{grid-template-columns:repeat(2,1fr)}}
</style><main><nav><a href="http://starship:8787/reports/#project=cubey">Cubey reports</a> · <a href="http://starship:8787/reports/372c661105753f4f4a6b92c6845619a64be574571767435b268a685ca29e6571/">Previous depth fix</a></nav>
<h1>Front-facing opening. Afternoon → night.</h1><p class="badge">Recommended candidate · published gallery unchanged</p>
<p class="lead">The helmet starts face-on, then eases through a 45° arc to reveal its side and the lake. The existing shared solar clock advances from 14:00 to 19:30. Automatic exposure keeps the closing helmet readable. Same original-mountain A, held storm state, water rendering and fitted depth correction. No rendering-code changes.</p>
<section class="grid"><article class="card"><h2>Recommended revision</h2><video id="new" controls muted playsinline preload="metadata" poster="gltf-terrain.png" src="gltf-terrain.mp4"></video><p>Yaw 0° → 45° · solar 14:00 → 19:30 · shared automatic exposure · point stars off for this recipe only.</p><a href="gltf-terrain.mp4">Download candidate</a> · <a href="gltf-terrain-recipe.json">Exact candidate recipe</a></article>
<article class="card"><h2>Currently published</h2><video id="old" controls muted playsinline preload="metadata" poster="current.png" src="current.mp4"></video><p>Yaw 84.5° → 104.5° · fixed 10:00 morning · fixed exposure. Different headings and light: a presentation comparison, not a matched shader A/B.</p><a href="current-recipe.json">Current recipe</a> · <a href="current-publication.json">Current publication receipt</a></article></section>
<div class="card"><button id="play">Play both from start</button> <button id="pause">Pause both</button> <button id="reset">Reset</button><p><label>Compare at <input id="seek" aria-label="Comparison frame" type="range" min="0" max="479" step="1" value="0"> <output id="stamp">0.00s</output></label></p><p class="muted">Native video controls and direct MP4 downloads work independently. The comparison slider seeks both films together.</p></div>
<h2>Read the eight-second story</h2><p>Daylight establishes the face and terrain; grazing sun reveals the slopes; dusk and the helmet's emissive markings close the shot. Night remains intentionally darker. The lake is less prominent in the opening than in the old side-facing shot; it opens up as the orbit progresses.</p><section class="strip">__STRIP__</section>
<details><summary>What the first attempts exposed</summary><p>Starting at 0° fixes the side/back-biased opening. 30° and 45° arcs were compared; 45° gives the lake and helmet more change without leaving the face behind. A literal restoration of the old 14:00→21:00 clock spent too long dark. Explicit exposure 0 also bypassed existing automatic exposure.</p><p>The shared landscape sky is sampled from a 64px environment cubemap. In this view, point stars become conspicuous blurry blocks. The bounded remedy is <code>--star-intensity 0</code> for the film, not a global sky change or a claim that night-sky rendering is fixed.</p><div class="grid"><figure><img src="night-blocks.png" alt="Rejected night with enlarged point stars"><figcaption>Rejected: point-star blocks and fixed exposure.</figcaption></figure><figure><img src="night-control.png" alt="Night control with automatic exposure and no stars"><figcaption>Existing controls: automatic exposure and point stars off, at 19:00.</figcaption></figure></div><div class="grid"><figure><video controls muted preload="none" src="initial-30.mp4"></video><figcaption>Initial 30° arc · 14:00→21:00 · fixed exposure.</figcaption></figure><figure><video controls muted preload="none" src="initial-45.mp4"></video><figcaption>Initial 45° arc · otherwise matched to 30°.</figcaption></figure></div></details>
<details><summary>Validation, provenance and reproduction</summary><p>All three native films contain 480 rendered 1080p frames. A second render of the refined film matches all 480 decoded frames exactly. Capture uses hardware Vulkan without a display server, required validation, GPU-exact bed/depth/decoded-velocity checks and zero hydraulic dispatches. The master and gallery normalize timestamps to exactly 8s/60 FPS. No solver, CUDA integration, new terrain, hydraulic updates, GUI acceptance or performance claim.</p><p><a href="protocol.json">Frozen runtime and inputs</a> · <a href="captures.json">Selected capture receipt</a> · <a href="repeat.json">All-frame repeat parity</a> · <a href="media-audit.json">Exact MP4 hashes and probes</a> · <a href="night-controls.json">Night controls</a> · <a href="exposure-controls.json">Exposure controls</a></p><p>The candidate is not yet owner-approved or published as the repository highlight. The existing gallery and its attachment receipt are unchanged.</p><pre>__COMMAND__</pre><p>Media-only CC-BY-NC-4.0: Damaged Helmet by ctxwing (2018 rebuild/conversion) and theblueturtle_ (2016 earlier model). Cubey source remains MIT. <a href="https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet">Upstream asset and attribution</a>.</p></details>
<script>const videos=[document.querySelector('#new'),document.querySelector('#old')],seek=document.querySelector('#seek'),stamp=document.querySelector('#stamp');const ready=Promise.all(videos.map(async v=>{const response=await fetch(v.getAttribute('src'));if(!response.ok)throw new Error('Video download failed');const url=URL.createObjectURL(await response.blob());await new Promise((resolve,reject)=>{v.addEventListener('loadedmetadata',resolve,{once:true});v.addEventListener('error',reject,{once:true});v.src=url;v.load()})}));ready.catch(()=>stamp.textContent='Use direct MP4 download if seeking is unavailable');async function move(t){await ready;for(const v of videos)v.currentTime=t;seek.value=Math.round(t*60);stamp.textContent=Number(t).toFixed(2)+'s'}document.querySelector('#play').onclick=async()=>{await move(0);const r=await Promise.allSettled(videos.map(v=>v.play()));if(r.some(x=>x.status==='rejected'))stamp.textContent='Use native Play controls'};document.querySelector('#pause').onclick=()=>videos.forEach(v=>v.pause());document.querySelector('#reset').onclick=()=>{videos.forEach(v=>v.pause());move(0)};seek.oninput=()=>{const t=Number(seek.value)/60;videos.forEach(v=>v.pause());move(t)};videos[0].ontimeupdate=()=>{if(!videos[0].paused){seek.value=Math.round(videos[0].currentTime*60);stamp.textContent=videos[0].currentTime.toFixed(2)+'s'}};</script></main></html>'''
    import shlex
    (export / "index.html").write_text(body.replace('__STRIP__', strip).replace(
        '__COMMAND__', html.escape(shlex.join(row["command"]))))
    review.verify(out)
    print(export)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    build(parser.parse_args().out.resolve(strict=True))
