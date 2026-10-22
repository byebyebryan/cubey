#!/usr/bin/env python3
"""Assemble the native authored-history video and all committed edit views."""
import argparse
import html
import json
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--controls', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, out = args.input.resolve(), args.output.resolve()
    data = json.loads((source / 'manifest.json').read_text())
    controls = json.loads((args.controls / 'windowed-interaction.json').read_text())
    assert controls['passed'] and controls['binary_sha256'] == data['binary_sha256']
    assert data['final_matches_static_fingerprints'] and data['video_builds'] == 44
    assert json.loads((source / 'video-check.json').read_text())['passed']
    regression = json.loads((source / 'regressions/manifest.json').read_text())
    assert regression['replay_scene_bytes_identical']
    assert regression['binary_sha256'] == data['binary_sha256']
    if out.exists():
        raise RuntimeError('Use a fresh report export directory')
    shutil.copytree(source, out / 'data')
    shutil.copytree(args.controls, out / 'controls')
    template = Path(__file__).with_name('negative_report_template.html').read_text()
    css = template.split('<style>')[1].split('</style>')[0]
    video_js = template.split('<script>')[1].split('const stages=')[0]
    options = ''.join(f'<option value="{s["index"]}" {"selected" if s["index"]==4 else ""}>{s["index"]:02} · {html.escape(s["label"])}</option>' for s in data['steps'])
    milestones = ''.join(f'<button data-step="{index}" aria-pressed="{str(index==4).lower()}">{label}</button>'
        for index, label in ((0, 'Empty'), (4, 'Head shell'), (5, 'Socket cut'), (9, 'Optic barrel'),
                             (10, 'Optic bore'), (18, 'Panel cut'), (43, 'Finished')))
    preview = data['previews'][0]
    cut_images = ''.join(f'<figure><a href="data/{file}"><img loading="lazy" src="data/{file}" alt="{label}"></a><figcaption>{label}</figcaption></figure>'
        for file, label in (('captures/step-04.png', 'Before: the head is solid'),
                           (preview['on'], 'Preview: the cutter is visible'),
                           ('captures/step-05.png', 'After: the socket has been removed')))
    frame_ms = regression['runs']['profile-hard']['gpu_frame']
    final_geometry = next(s for s in data['runs']['static-final']['stats'] if s.startswith('sdf: authored geometry='))
    page = f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cubey SDF — building the robot one edit at a time</title><link rel="icon" href="data:,"><style>{css}
button:disabled{{opacity:.45;cursor:default}}.hero-video,.gallery{{max-width:1000px;margin-inline:auto}}video{{height:auto;aspect-ratio:4/3;object-fit:contain}}
.step-range{{display:flex;gap:14px;align-items:center;margin:18px 0}}.step-range input{{width:100%;min-width:0}}.step-range output{{white-space:nowrap;color:#e4b66b;font-weight:600}}select{{width:100%;max-width:650px}}
</style></head><body><main><div class="eyebrow">Cubey · Authored SDF construction · Native rendering</div>
<h1>Building the robot, one edit at a time.</h1>
<p class="lead">43 named additions and cuts turn an empty workspace into the survey robot. Watch the shell being cut, the optic being bored and the limbs assembled, then inspect the finished piece in orbit.</p>
<div class="hero-video"><video controls preload="metadata" width="1280" height="960" data-video-src="data/presentation.mp4" poster="data/captures/step-43.png" aria-label="Robot construction followed by its finished orbit"></video></div>
<p class="note" id="video-caption" aria-live="polite">Ready to build the robot.</p>
<div class="chapters"><button disabled data-time="0">Start</button><button disabled data-time="4.73">Socket preview</button><button disabled data-time="10.4">Bore the optic</button><button disabled data-time="16.45">Chest notch</button><button disabled data-time="32">Finished</button><button disabled data-time="37">Orbit</button></div>
<p class="note" id="video-status" role="status">Preparing the video…</p>
<p class="note">42 seconds · 30 FPS · 1280×960 · 2× internal sampling · hard self-shadows and shared atmosphere. Construction holds the camera for 32 seconds; the final ten seconds orbit the completed robot. Edit labels were added after native capture. <a href="data/presentation.mp4">Labeled video</a> · <a href="data/native.mp4">Original native capture</a>.</p>
<h2>Inspect every edit</h2><p>Use Previous/Next, the slider or the named list to inspect any committed state. Start with the head shell and advance once to see the socket cut.</p>
<div class="controls">{milestones}</div>
<div class="step-range"><output id="step-count" for="step">Edit 04 / 43</output><input id="step" type="range" min="0" max="43" step="1" value="4" aria-label="Committed authoring edit"></div>
<div class="controls"><button id="previous">Previous edit</button><button id="next">Next edit</button><button id="watch-step" disabled data-time="4.103">Watch this edit</button></div>
<label for="step-select">Named edit</label><select id="step-select">{options}</select>
<p id="step-label" aria-live="polite">Place the tilted head shell</p>
<div class="viewer gallery"><a id="step-link" href="data/captures/step-04.png"><img id="step-image" src="data/captures/step-04.png" alt="Edit 4: Place the tilted head shell"></a></div>
<p class="note">These are native captures of the intermediate grouped scenes. The two backpack edits are on the rear; they become clear in the final orbit. In the desktop app you can orbit freely while inspecting a paused edit.</p>
<h2>See a cut before it commits</h2><div class="grid">{cut_images}</div>
<p>The translucent cutter preview identifies the next cutter while the body remains intact. At commitment, the scene performs the subtraction and rebuilds its distance field. The six removals cut the socket, three face vents, the optic bore and the chest notch.</p>
<h2>A saved sequence, with real intermediate geometry</h2>
<p>The scene stores the final named CSG tree and an ordered history of its primitive leaves. Missing operands are reduced into valid partial expressions, preserving local transforms and material inheritance. A subtraction needs its body first. Each step can be revisited, and the last step retains the exact final scene fingerprints.</p>
<div class="facts"><div><strong>44 scene states</strong><small>Empty workspace and 43 committed edits</small></div><div><strong>6 cuts</strong><small>Local removals with depth-aware previews</small></div><div><strong>44 field builds</strong><small>Across 1,260 native video frames</small></div></div>
<p>New geometry builds once when the committed edit changes. Camera motion and the fading preview reuse that field. Field construction remains synchronous; the captured playback does not establish smooth continuous sculpting performance. The matched final-object 1080p profile measures {frame_ms['median_ms']:.3f} ms median GPU frame time with hard shadows, {frame_ms['p95_ms']:.3f} ms p95 ({frame_ms['samples']} completed samples).</p>
<h2>Checks and desktop controls</h2><p>All 44 native history states pass required Vulkan validation, sampled-field ray checks and direct CPU/GPU distance, material and part comparisons. The same field also passes backward, empty/reset and final-state revisits. Unchanged states reuse the cache. The final history view matches the static robot's geometry and appearance fingerprints, with image differences bounded separately from the ray oracle.</p>
<pre>{html.escape(final_geometry)}</pre>
<p>All 1,260 native video frames were decoded and screened for visible magenta traversal exhaustion; none was found. Native validation retains its existing finite-grid tolerance and reports sampling ambiguities separately. The original primitive/material fixtures, scene replay, batch/reset checks and legacy procedural scenes also pass.</p>
<p>The private desktop check passes previous/next edits, the edit scrubber at final and midpoint, pause/resume, restart to empty, resize and camera input. Holding a paused edit does not rebuild its field. <a href="controls/captures/window-step-final.png">Native desktop screenshot</a>.</p>
<details><summary>Run it and inspect the evidence</summary><pre>build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot-build.json

# Inspect every intermediate state and render the presentation:
python3 projects/sdf/tools/qualify_history.py --video --regressions --output outputs/sdf/history-review-new</pre>
<p>Use Play/Pause, Previous edit, Next edit and the Edit slider. Timeline spans construction and orbit; R restarts and Space toggles playback. Choose Construction for a fixed camera or Presentation for the final orbit. Cut preview can be disabled. The existing static robot preset remains available.</p>
<p>Runtime source <code>{data['source_commit']}</code> · worktree {'dirty' if data['source_dirty'] else 'clean'} at capture start · binary/shader hashes recorded. Desktop and regression receipts identify the same native binary.</p>
<p><a href="data/manifest.json">Full commands and fingerprints</a> · <a href="data/logs/history-validation.log">Every-state native checks</a> · <a href="data/logs/video.log">Video builds and step visits</a> · <a href="data/scenes/robot.json">Saved scene and history</a> · <a href="data/video-check.json">Video screening</a> · <a href="controls/windowed-interaction.json">Desktop interaction receipt</a> · <a href="data/regressions/manifest.json">Legacy and material regressions</a></p></details>
<footer><a href="http://starship:8787/reports/eafb4462b9072db9f445b21e5d9402466d78e11fd70e23fdcba7d2bf263c8540/">Previous robot turntable and blockout audition</a> · <a href="http://starship:8787/reports/#project=cubey">Cubey report catalog</a></footer>
</main><script>{video_js}
const steps={json.dumps(data['steps'])};const events={json.dumps(data['video_events'])};
let selected=4;
function showStep(value){{selected=Math.max(0,Math.min(43,Number(value)));const state=steps[selected];
document.querySelector('#step').value=selected;document.querySelector('#step-select').value=selected;
document.querySelector('#step-count').textContent=`Edit ${{String(selected).padStart(2,'0')}} / 43`;
document.querySelector('#step-label').textContent=state.label;const path=`data/${{state.capture}}`;
document.querySelector('#step-image').src=path;document.querySelector('#step-image').alt=`Edit ${{selected}}: ${{state.label}}`;
document.querySelector('#step-link').href=path;document.querySelector('#previous').disabled=selected===0;document.querySelector('#next').disabled=selected===43;
document.querySelector('#watch-step').dataset.time=String(state.time*1259/1260);
document.querySelectorAll('[data-step]').forEach(button=>button.setAttribute('aria-pressed',String(Number(button.dataset.step)===selected)));}}
document.querySelector('#step').addEventListener('input',event=>showStep(event.target.value));
document.querySelector('#step-select').addEventListener('change',event=>showStep(event.target.value));
document.querySelector('#previous').addEventListener('click',()=>showStep(selected-1));
document.querySelector('#next').addEventListener('click',()=>showStep(selected+1));
document.querySelectorAll('[data-step]').forEach(button=>button.addEventListener('click',()=>showStep(button.dataset.step)));
function caption(){{const event=events.findLast(event=>event.time<=video.currentTime);if(event)document.querySelector('#video-caption').textContent=event.text;}}
video.addEventListener('timeupdate',caption);video.addEventListener('seeked',caption);showStep(4);
</script></body></html>'''
    (out / 'index.html').write_text(page)
    print(out)


if __name__ == '__main__':
    main()
