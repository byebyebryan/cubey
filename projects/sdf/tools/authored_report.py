#!/usr/bin/env python3
"""Assemble the native authored robot review from qualified captures."""
import argparse
import html
import json
from pathlib import Path
import re
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--blockouts', type=Path, required=True)
    parser.add_argument('--controls', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, export = args.input.resolve(), args.output.resolve()
    if export.exists():
        raise RuntimeError('Use a fresh report output directory')
    data = json.loads((source / 'manifest.json').read_text())
    assert data['replay_scene_bytes_identical']
    assert all(row['exit_code'] == 0 or row['expected_error'] for row in data['runs'].values())
    assert json.loads((source / 'video-check.json').read_text())['passed']
    assert json.loads((args.controls / 'windowed-interaction.json').read_text())['passed']
    shutil.copytree(source, export / 'data')
    shutil.copytree(args.controls, export / 'controls')
    (export / 'blockouts').mkdir()
    for variant in ('survey', 'tall', 'broad'):
        shutil.copy2(args.blockouts / f'captures/{variant}-hero.png', export / f'blockouts/{variant}.png')
    shutil.copy2(args.blockouts / 'manifest.json', export / 'blockouts/manifest.json')
    template = (Path(__file__).with_name('negative_report_template.html')).read_text()
    css = template.split('<style>')[1].split('</style>')[0]
    video_js = template.split('<script>')[1].split('const stages=')[0]
    hero = data['runs']['hero']
    oracle = next(line for line in hero['stats'] if line.startswith('sdf: oracle rays='))
    direct = next(line for line in hero['stats'] if line.startswith('sdf: authored CPU/GPU points='))
    build = next(line for line in hero['stats'] if line.startswith('sdf: authored instructions='))
    bricks = int(re.search(r'bricks=(\d+)', build)[1])
    allocated = int(re.search(r'allocated=(\d+)', build)[1])
    build_ms = float(re.search(r' build_ms=([\d.]+)', build)[1])
    rows = ''
    for key, label in (('primary', 'Authored materials · shadows off'), ('hard', 'Authored materials · hard shadows'), ('clay', 'Clay override · hard shadows')):
        timing = data['runs'][f'profile-{key}']['gpu_frame']
        rows += f'<tr><td>{label}</td><td>{timing["median_ms"]:.3f} ms</td><td>{timing["p95_ms"]:.3f} ms</td><td>{timing["samples"]}</td></tr>'
    views = {'hero': 'Three-quarter hero', 'front': 'Front', 'side': 'Side', 'rear': 'Rear', 'detail': 'Head and chest detail',
             'clay': 'Neutral clay', 'parts': 'Part identity', 'base-color': 'Base color', 'roughness': 'Roughness', 'normals': 'Surface normals'}
    buttons = ''.join(f'<button data-view="{key}" aria-pressed="{str(key=="hero").lower()}">{label}</button>' for key, label in views.items())
    blockouts = ''.join(f'<figure><a href="blockouts/{name}.png"><img loading="lazy" src="blockouts/{name}.png" alt="{name} gray robot blockout"></a><figcaption>{label}</figcaption></figure>'
        for name, label in (('survey', 'Selected survey proportions'), ('tall', 'Taller body'), ('broad', 'Wider head')))
    page = f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cubey SDF — the first authored survey robot</title><link rel="icon" href="data:,"><style>{css}
button:disabled{{opacity:.5;cursor:wait}}.hero-video{{max-width:1000px;margin-inline:auto}}.gallery{{max-width:960px;margin-inline:auto}}
</style></head><body><main><div class="eyebrow">Cubey · Authored SDF content · Native rendering</div>
<h1>A survey robot, built from 43 shapes.</h1>
<p class="lead">One original mechanical toy, authored in code and saved as grouped SDF geometry. Four plain materials give it ivory housing, teal accents, dark joints and an amber sensor. No image textures or UV mapping.</p>
<div class="hero-video"><video controls preload="metadata" data-video-src="data/turntable.mp4" poster="data/captures/hero.png" aria-label="Native sixteen-second survey robot turntable"></video></div>
<div class="chapters"><button disabled data-time="0">Hero · 0s</button><button disabled data-time="4">Quarter turn · 4s</button><button disabled data-time="8">Opposite · 8s</button><button disabled data-time="12">Three-quarter turn · 12s</button><button disabled data-time="15.9">Return · 15.9s</button></div>
<p class="note" id="video-status" role="status">Preparing the video…</p>
<p class="note">Native 1280×960 · 16 seconds · 30 FPS · 2× internal sampling · hard self-shadows and Cubey's shared atmosphere. <a href="data/turntable.mp4">Download video</a>.</p>
<h2>Inspect the authored result</h2><div class="controls" aria-label="Native robot views">{buttons}</div>
<div class="viewer gallery"><a id="view-link" href="data/captures/hero.png"><img id="view-image" src="data/captures/hero.png" alt="Three-quarter hero view of the survey robot"></a></div>
<p class="note" id="view-caption" aria-live="polite">Three-quarter hero</p>
<p>The eye socket, face vents and chest notch are local subtractions. Capsules form the limbs and antenna; capped cylinders form the optic and shins. The torso/hip transition uses a smooth union. The head tilt and raised arm supply the pose; detached animation and a sculpting editor are outside this piece.</p>
<p>Base color, roughness and a small amount of emission define all four materials. The robot uses sharp material boundaries; the separate blend fixtures exercise smooth parameter mixing. Procedural surface noise was left off so the result can be judged on form and plain materials.</p>
<h2>From gray blockout to a named assembly</h2><div class="grid">{blockouts}</div>
<p>The early blockout audition used the existing rounded-box/sphere path. The medium survey proportions were selected for refinement. These captures preserve that design step; the finished robot has its own qualification below. <a href="blockouts/manifest.json">Blockout commands and checks</a>.</p>
<h2>What changed in the foundation</h2>
<p>The explicit scene retains names, grouped operations, transforms and material assignments. A bounded expression program builds the same distance-brick representation used by the procedural demos. Materials are evaluated at surface hits, so recoloring does not require a color volume or rebuild the distance cache.</p>
<p>Subtraction can inherit the body's material or assign a material to newly exposed surfaces. The group fixtures verify that cutting one component leaves neighboring components intact. Smooth geometry and material blending have independent settings. Existing version-1 recipes and their generator hashes remain supported.</p>
<div class="facts"><div><strong>43 shapes / 85 instructions</strong><small>64-shape and 128-instruction limits</small></div><div><strong>{bricks:,} bricks</strong><small>262,144-brick axis-64 capacity</small></div><div><strong>{allocated / 1048576:.1f} MiB</strong><small>Allocated field resources; excludes frame targets and driver</small></div></div>
<h2>Measured rendering cost</h2><p>RTX 5070 Ti · 1920×1080 · 1× sampling · static robot · 180 submitted frames with 30 warmup frames. Completed GPU timestamps cover surface rendering and shared post. Field construction, readback and video encoding are excluded.</p>
<div class="table-wrap"><table><thead><tr><th>Treatment</th><th>Median GPU frame</th><th>p95</th><th>Samples</th></tr></thead><tbody>{rows}</tbody></table></div>
<p class="note">The clay comparison changes the material treatment on the same geometry and lighting; its difference from the colored treatment is not an isolated shader-stage timer. The hero field build took {build_ms:.1f} ms wall time. Geometry changes currently rebuild the authored field synchronously. This turntable demonstrates rendering of a completed object.</p>
<h2>Native checks</h2><p>Eleven expression fixtures cover primitives, transformed groups, inherited and explicit cut materials, nested smooth blends, thin gaps, workspace clipping and the 64-leaf boundary. They pass required Vulkan validation, the sampled-field ray oracle and direct CPU/GPU expression/material comparisons. The robot's explicit exported scene reloads byte for byte, with matching fingerprints. Legacy additive/subtractive fixtures and batch/reset checks pass.</p>
<pre>{html.escape(oracle)}\n{html.escape(direct)}</pre>
<p class="note">The ray oracle retains its finite-grid tolerance and reports sampling ambiguities separately. A nominal first-hit difference can be larger where a grazing ray changes which surface it meets; the surface residual and inner/outer sampling bracket remain checked.</p>
<p>The private Xvfb check passes resize, orbit, zoom, pause/reset and a live material roughness edit. The appearance fingerprint changes while geometry and the one field build remain unchanged. This automated interaction check is separate from your visual judgment of the piece.</p>
<p class="note">Every one of the 480 encoded video frames was decoded and screened for the visible magenta traversal-exhaustion color; none was found. <a href="data/video-check.json">Video screening receipt</a>. <a href="controls/captures/window-material-edited.png">Native material-edit screenshot</a>.</p>
<details><summary>Run it and inspect the receipts</summary><pre>build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot.json

# Regenerate the explicit authored scene:
python3 projects/sdf/tools/author_robot.py

# Reproduce captures, native checks, GPU profiles and the turntable:
python3 projects/sdf/tools/qualify_authored.py --video --output outputs/sdf/robot-review-new</pre>
<p>Camera drag/zoom, pause/reset, reload, clay inspection and per-material color/roughness/emission are available in the desktop panel. New authored scenes use static or orbit playback.</p>
<p>Native source <code>{data['source_commit']}</code> · worktree {'dirty' if data['source_dirty'] else 'clean'} at capture start · {len(data['shader_sha256'])} compiled shader hashes recorded.</p>
<p><a href="data/manifest.json">Commands and runtime hashes</a> · <a href="data/scenes/robot.json">Explicit authored scene</a> · <a href="data/scenes/author_robot.py">Robot authoring script</a> · <a href="data/scenes/sdf_author.py">Authoring helpers</a> · <a href="data/logs/lifecycle.log">Batch, rollback and transition checks</a> · <a href="controls/windowed-interaction.json">Desktop interaction receipt</a> · <a href="data/profiles/hard.passes.csv">Hard-shadow timing samples</a></p></details>
<footer><a href="http://starship:8787/reports/1db7103651a39de3b013aabf8424bbcb06e0a236627b8af3c32dcb9306dc6aa1/">Previous procedural carving prototype</a> · <a href="http://starship:8787/reports/#project=cubey">Cubey report catalog</a></footer>
</main><script>{video_js}
const viewLabels={json.dumps(views)};
document.querySelectorAll('[data-view]').forEach(button=>button.addEventListener('click',()=>{{const view=button.dataset.view;const file=`data/captures/${{view}}.png`;document.querySelector('#view-image').src=file;document.querySelector('#view-image').alt=viewLabels[view];document.querySelector('#view-link').href=file;document.querySelector('#view-caption').textContent=viewLabels[view];document.querySelectorAll('[data-view]').forEach(other=>other.setAttribute('aria-pressed',String(other===button)));}}));
</script></body></html>'''
    (export / 'index.html').write_text(page)
    print(export)


if __name__ == '__main__':
    main()
