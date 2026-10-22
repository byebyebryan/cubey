#!/usr/bin/env python3
"""Package the guardian chip review from two qualified native snapshots."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--before',type=Path,required=True)
parser.add_argument('--after',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args = parser.parse_args()
before,after,out = (p.resolve() for p in (args.before,args.after,args.output))
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
old = json.loads((before/'manifest.json').read_text())
new = json.loads((after/'manifest.json').read_text())
history = json.loads((after/'history/manifest.json').read_text())
assert old['binary_sha256'] == new['binary_sha256'] == history['binary_sha256']
assert old['shader_sha256'] == new['shader_sha256'] == history['shader_sha256']
assert old['device'] == new['device']
assert old['edits'] == 108 and new['edits'] == 106
assert not new['source_dirty']
assert new['reloaded_scene_and_image_identical'] and new['atlas_80_image_identical']
assert history['final_matches_static_fingerprints'] and history['video_builds'] == 107
assert json.loads((after/'intent-audit.json').read_text())['passed']
assert json.loads((after/'history/video-check.json').read_text())['passed']
assert sha(after/'history/presentation.mp4') == history['presentation_sha256']
assert sha(after/'scenes/source.json') == new['source_scene_sha256'] == history['source_scene_sha256']
assert sha(before/'scenes/source.json') == old['source_scene_sha256']
old_scene = json.loads((before/'scenes/source.json').read_text())
new_scene = json.loads((after/'scenes/source.json').read_text())
removed = {'base-chip','jaw-chip'}
def leaves(node):
    if 'children' not in node: return {node['name']:node}
    return {name:leaf for child in node['children'] for name,leaf in leaves(child).items()}
old_leaves,new_leaves = leaves(old_scene['root']),leaves(new_scene['root'])
assert set(old_leaves)-set(new_leaves) == removed
assert {k:v for k,v in old_leaves.items() if k not in removed} == new_leaves
assert [h for h in old_scene['history'] if h['node'] not in removed] == new_scene['history']
assert old_scene['materials'] == new_scene['materials']
for entries,base in ((new['runs'],after),(history['runs'],after/'history')):
    for row in entries.values():
        if 'capture' in row: assert sha(base/row['capture']) == row['capture_sha256']
views = new['metadata']['views'] + [dict(name='clay',label='Clay / hero')]
out.mkdir(exist_ok=False)
shutil.copytree(after,out/'after')
(out/'before/captures').mkdir(parents=True)
for view in views:
    name = view['name']
    a = json.loads((before/f'configs/{name}.json').read_text())
    b = json.loads((after/f'configs/{name}.json').read_text())
    a.pop('output'); b.pop('output')
    assert a == b, (name,a,b)
    assert sha(before/f'captures/{name}.png') == old['runs'][name]['capture_sha256']
    shutil.copy2(before/f'captures/{name}.png',out/f'before/captures/{name}.png')
shutil.copy2(before/'manifest.json',out/'before/manifest.json')
shutil.copy2(before/'scenes/source.json',out/'before/scene.json')
shutil.copy2(__file__,out/'build_report.py')
data = dict(views=views,steps=history['steps'],events=history['video_events'],
            frames=history['frames'],edits=new['edits'],source=new['source_commit'])
html = '''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cubey SDF — guardian review: chips removed</title><link rel="icon" href="data:,">
<style>
:root{color-scheme:dark;--bg:#121b1e;--panel:#1c2a2e;--ink:#edf1e9;--muted:#b6c6c6;--line:#384c50;--accent:#d2dba5}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.6 system-ui,sans-serif}main{max-width:1280px;margin:auto;padding:40px 24px}h1,h2{font:clamp(2rem,4vw,3.5rem)/1.12 Georgia,serif;letter-spacing:-.03em}h1{font-size:clamp(2.8rem,6vw,4.8rem)}h1,h2,p{margin-top:0}.eyebrow{font-size:.78rem;letter-spacing:.15em;text-transform:uppercase;color:var(--accent)}.lead{max-width:75ch;font-size:1.12rem;color:var(--muted)}a{color:var(--accent);text-underline-offset:3px}.pair,.columns{display:grid;grid-template-columns:1fr 1fr;gap:20px}.pair>*,.columns>*{min-width:0}figure{margin:0;border-radius:12px;overflow:hidden;background:var(--panel)}img{display:block;width:100%;aspect-ratio:1;object-fit:contain}figcaption{padding:12px 16px;color:var(--muted);font-size:.9rem}section{border-top:1px solid var(--line);padding-top:30px;margin-top:36px}button,select,input{font:inherit}button,select{color:var(--ink);background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:10px 14px}button{cursor:pointer}button:disabled{opacity:.4}select{width:100%;max-width:100%;margin:8px 0 16px}label,.note{color:var(--muted);font-size:.9rem}.controls{display:flex;align-items:center;flex-wrap:wrap;gap:10px;margin:12px 0}:focus-visible{outline:3px solid #eba963;outline-offset:3px}video{display:block;width:100%;aspect-ratio:4/3;border-radius:12px;background:var(--panel)}input[type=range]{width:100%;accent-color:var(--accent)}details{padding:18px;background:var(--panel);border-radius:9px;margin:20px 0}summary{cursor:pointer}#film-caption{min-height:3.3em}footer{margin-top:32px;color:var(--muted);font-size:.9rem}@media(max-width:700px){main{padding:26px 16px}.pair,.columns{grid-template-columns:1fr}.lead{font-size:1rem}}
</style><main>
<p class="eyebrow">Cubey / SDF sculpture / user review</p><h1>A cleaner guardian.</h1>
<p class="lead">The jaw and base chips were deliberate attempts at stone wear. They read as arbitrary damage, so both cuts are removed. The facial carving, inner ears, horn flutes and neck structure remain.</p>
<p class="note">Matched native renders · plain material · hard self-shadows · shared atmosphere · no ground plane</p>
<label for="view">Inspection view</label><select id="view"></select>
<div class="pair"><figure><a id="before-link"><img id="before" alt=""></a><figcaption>Previous refinement · 108 leaves · jaw/base chips</figcaption></figure><figure><a id="after-link"><img id="after" alt=""></a><figcaption>After review · 106 leaves · intact jaw and base</figcaption></figure></div>
<section><h2>The revised construction.</h2><p>106 edits, with both chip operations removed from the actual expression and replay. The film builds for 40 seconds, then orbits the completed sculpture for ten seconds.</p>
<div class="columns"><div><video id="film" controls muted playsinline preload="metadata" poster="after/captures/hero.png" src="after/history/presentation.mp4"></video>
<div class="controls"><button id="build">Build from the start</button><button id="orbit">Finished orbit</button><a href="after/history/presentation.mp4">Download film</a></div><p id="film-caption" class="note" aria-live="polite"></p>
<p class="note">Red guides preview real subtractive tools. This is leaf-reveal playback of scripted CSG, rather than a recording of a freehand sculpting session.</p></div>
<div><label for="step">Committed edit <output id="count"></output></label><input id="step" type="range" min="0" step="1">
<div class="controls"><button id="previous">Previous edit</button><button id="next">Next edit</button><button id="watch">Watch this edit</button></div>
<label for="named-step">Named edit</label><select id="named-step"></select>
<figure><a id="state-link"><img id="state" alt=""></a><figcaption id="state-caption"></figcaption></figure></div></div></section>
<section><h2>Driftwood stops here.</h2><p class="lead">The extra modeling still did not produce a convincing enough result. Driftwood is removed from the active refinement presentation; further detailing is deferred. Its source and evidence remain as an unsuccessful content experiment.</p>
<p>The <a href="http://starship:8787/reports/d66a4ee7ce92dd2f255e170b9dc67676551a1f5efec099ec99ccda304a4d7973/">previous two-subject gallery</a> remains the historical review record. The guardian is the selected sculpture for continuation.</p>
<details><summary>Native evidence and remaining limits</summary><p>The corrected scene has 106 leaves / 211 instructions. Saved-volume probes verify the restored jaw and base, existing connections and hollow sockets. The native matrix covers all 107 committed states, cutter previews, CPU/GPU field agreement, static/replay equivalence, exact export/reload, atlas relocation and decoded video checks.</p>
<p>All remaining primitive definitions, their history entries and materials are unchanged. Camera, lighting and rendering configuration match between each before/after pair; only output paths differ. Both snapshots use the same binary, shaders and GPU. Qualification was captured from a clean worktree at <code id="source"></code>.</p>
<p>These checks establish correctness, not artistic acceptance. Some sampled horn transitions remain visible under grazing light; an exact spline sweep remains deferred.</p>
<div class="controls"><a href="after/manifest.json">Qualification receipt</a><a href="after/history/manifest.json">History and preview checks</a><a href="after/intent-audit.json">Design probes</a><a href="after/history/video-check.json">Film screening</a><a href="after/scenes/source.json">Current scene</a><a href="after/scenes/author_refinements.py">Generator</a><a href="before/manifest.json">Previous snapshot receipt</a><a href="before/scene.json">Previous scene</a><a href="build_report.py">Report builder</a></div></details></section>
<footer><a href="http://starship:8787/reports/#project=cubey">Cubey report catalog</a></footer>
</main><script>
const data=__DATA__,$=id=>document.getElementById(id),film=$('film');let step=data.edits;
$('view').replaceChildren(...data.views.map(v=>new Option(v.label,v.name)));
function view(){const name=$('view').value,label=data.views.find(v=>v.name===name).label;for(const id of ['before','after']){$(id).src=`${id}/captures/${name}.png`;$(id).alt=`Guardian ${id==='before'?'before':'after'} chip removal, ${label}`;$(id+'-link').href=$(id).src;}}
function show(n){step=Math.max(0,Math.min(data.edits,Number(n)));const s=data.steps[step];$('step').value=step;$('named-step').value=step;$('count').textContent=`${step} / ${data.edits}`;$('state').src=`after/history/${s.capture}`;$('state').alt=`Edit ${step}: ${s.label}`;$('state-link').href=$('state').src;$('state-caption').textContent=s.label;$('previous').disabled=step===0;$('next').disabled=step===data.edits;}
function seek(time){film.pause();if(film.readyState>=1)film.currentTime=time;else film.addEventListener('loadedmetadata',()=>{film.currentTime=time;},{once:true});}
$('step').max=data.edits;$('named-step').replaceChildren(...data.steps.map(s=>new Option(`${s.index} · ${s.label}`,s.index)));
$('view').addEventListener('change',view);$('step').addEventListener('input',e=>show(e.target.value));$('named-step').addEventListener('change',e=>show(e.target.value));$('previous').addEventListener('click',()=>show(step-1));$('next').addEventListener('click',()=>show(step+1));$('watch').addEventListener('click',()=>seek(data.steps[step].time*(data.frames-1)/data.frames));$('build').addEventListener('click',()=>seek(0));$('orbit').addEventListener('click',()=>seek(40.2));
function caption(){const event=data.events.findLast(e=>e.time<=film.currentTime);$('film-caption').textContent=event?event.text:'';}film.addEventListener('timeupdate',caption);film.addEventListener('seeked',caption);$('source').textContent=data.source;view();show(step);
</script></html>'''
(out/'index.html').write_text(html.replace('__DATA__',json.dumps(data).replace('</','<\\/')))
files = {str(p.relative_to(out)):dict(bytes=p.stat().st_size,sha256=sha(p))
         for p in sorted(out.rglob('*')) if p.is_file()}
(out/'assets.json').write_text(json.dumps(files,indent=2)+'\n')
size=sum(p.stat().st_size for p in out.rglob('*') if p.is_file())
assert size < 100*1024*1024
print(json.dumps(dict(output=str(out),bytes=size,files=len(files)+1)))
