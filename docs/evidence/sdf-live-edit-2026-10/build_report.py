#!/usr/bin/env python3
"""Assemble the completed native study; no GPU work. numpy/scipy/Pillow required."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil
import statistics
import sys

import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'projects/sdf/tools'))
from investigate_guardian import digest


def save(path,data):path.write_text(json.dumps(data,indent=2,allow_nan=False)+'\n')


def build(raw,post,export,evidence):
    export.mkdir(parents=True,exist_ok=False);(export/'images').mkdir()
    receipts={k:json.loads((raw/path).read_text()) for k,path in dict(
        geometry='geometry/geometry.json',admission='prepare/admission.json',
        qualification='qualify/qualification.json',captures='captures/captures.json',timing='timing/timing.json').items()}
    for name,r in receipts.items():
        if not r.get('complete'):raise RuntimeError(f'incomplete {name}')
        if r['source_head']!=receipts['timing']['source_head']:raise RuntimeError('source identity differs')
        if r['binary_sha256']!=receipts['timing']['binary_sha256']:raise RuntimeError('binary identity differs')
        if r['private_shader_sha256']!=receipts['timing']['private_shader_sha256']:raise RuntimeError('shader identity differs')
    q=json.loads((post/'postqualification.json').read_text())
    if not q['complete']:raise RuntimeError('incomplete postqualification')
    if q['tool_sha256']!=digest(Path(__file__).with_name('postqualify.py')):raise RuntimeError('postqualification tool identity differs')
    private=ROOT/'build/release/projects/sdf/cubey_project_sdf_live_edit_study'
    if digest(private)!=receipts['timing']['binary_sha256']:raise RuntimeError('current private binary differs')
    for name,expected in receipts['timing']['private_shader_sha256'].items():
        if digest(private.parent/'shaders'/name)!=expected:raise RuntimeError('current private shader differs')
    groups={}
    for r in receipts['timing']['runs']:
        groups.setdefault((r['subject'],r['count'],r['policy']),[]).append(r)
    summary=[]
    for (subject,count,policy),runs in groups.items():
        if sorted(r['repeat'] for r in runs)!=[1,2,3]:raise RuntimeError('missing timing repeats')
        if any(len(r['passes']['sdf.whole_scene']['values_ms'])!=150 for r in runs):raise RuntimeError('missing samples')
        summary.append(dict(subject=subject,count=count,policy=policy,
            p50_ms=statistics.median(r['stats']['p50_ms'] for r in runs),
            p95_ms=statistics.median(r['stats']['p95_ms'] for r in runs),
            p95_range=[min(r['stats']['p95_ms'] for r in runs),max(r['stats']['p95_ms'] for r in runs)],
            p99_ms=max(r['stats']['p99_ms'] for r in runs),max_ms=max(r['stats']['max_ms'] for r in runs),
            over_11_11=sum(r['stats']['over_11_11'] for r in runs),over_16_67=sum(r['stats']['over_16_67'] for r in runs),samples=450))
    if len(summary)!=36:raise RuntimeError(f'incomplete matrix: {len(summary)} groups')
    baseline=json.loads((raw.parent/'baseline.json').read_text())
    shader_dir=ROOT/'build/release/projects/sdf/shaders'
    changed_shaders=[f for f,h in baseline['shader_hashes'].items() if digest(shader_dir/f)!=h]
    changed_protected=[f for f,h in baseline['protected'].items() if digest(ROOT/f)!=h]
    if changed_shaders or changed_protected:raise RuntimeError('public/protected identity changed')
    images={};motion={};image_stats=[]
    for r in receipts['captures']['runs']:
        if r['mode'].startswith('motion'):
            paths=[]
            for pose in r['native_receipt']['poses']:
                source=Path(pose['frame_png']);name=f"{r['name']}-{r['mode']}-{pose['frame']}.webp"
                im=Image.open(source);im.save(export/'images'/name,'WEBP',lossless=True)
                paths.append('images/'+name)
            motion.setdefault(r['name'],{})[r['mode'][7:]]=paths
            continue
        source=raw/'captures'/r['image'];name=source.stem+'.webp'
        im=Image.open(source).convert('RGB');a=np.asarray(im)
        magenta=int(((a[:,:,0]>250)&(a[:,:,1]<5)&(a[:,:,2]>250)).sum())
        image_stats.append(dict(name=r['name'],mode=r['mode'],exhaustion_color_pixels=magenta))
        im.save(export/'images'/name,'WEBP',lossless=True)
        images.setdefault(r['name'],{})[r['mode']]='images/'+name
    identity={k:receipts['timing'][k] for k in ('source_head','binary_sha256','public_binary_sha256','private_shader_sha256','device')}
    guards=dict(original_shaders_unchanged=len(baseline['shader_hashes']),protected_files_unchanged=len(baseline['protected']),
                source_gate=receipts['geometry']['source_gate'],automatic_sampled_handoff_gate=receipts['geometry']['automatic_sampled_handoff_gate'])
    source=receipts['geometry']['checks']
    construction=[]
    for subject in ('robot','guardian'):
        runs=[r['native_receipt'] for r in receipts['timing']['runs'] if r['subject']==subject and r['native_receipt']]
        construction.append(dict(subject=subject,field_build_median_ms=statistics.median(r['field_build_ms'] for r in runs),
            field_build_range_ms=[min(r['field_build_ms'] for r in runs),max(r['field_build_ms'] for r in runs)],
            region_build_median_ms=statistics.median(r['region_ms'] for r in runs),field_bytes=runs[0]['field_bytes'],region_bytes=runs[0]['region_bytes']))
    data=dict(identity=identity,guards=guards,timing=summary,geometry=source,construction=construction,
        qualification=receipts['qualification']['runs'],native_validation=q['native_validation'],handoff=q['comparisons'],
        admission=receipts['admission'],images=images,motion=motion,image_stats=image_stats,max_tail_replay=q['max_tail_replay'],source_diagnostic_relation=q['source_diagnostic_relation'])
    save(evidence/'summary.json',data);save(export/'summary.json',data)
    for name,value in receipts.items():
        save(evidence/(name+'.json'),value)
    save(evidence/'postqualification.json',q)
    for folder,names in [('geometry',['zero-input.json','pending-input.json','zero-source.json','zero-cached.json','pending-source.json','pending-cached.json','fresh-analytic.json','fresh-sampled.json'])]:
        for name in names:shutil.copy2(raw/folder/name,evidence/name)
    raw_files={str(p.relative_to(raw)):dict(bytes=p.stat().st_size,sha256=digest(p)) for p in sorted(raw.rglob('*')) if p.is_file()}
    save(evidence/'raw-manifest.json',dict(root=str(raw),files=raw_files,post_root=str(post),post_files={p.name:dict(bytes=p.stat().st_size,sha256=digest(p)) for p in sorted(post.iterdir()) if p.is_file()}))
    for name in ('summary.json','geometry.json','admission.json','qualification.json','timing.json','postqualification.json','pending-input.json','pending-source.json','fresh-sampled.json','raw-manifest.json'):
        shutil.copy2(evidence/name,export/name)
    page=PAGE.replace('@@SOURCE',identity['source_head'][:8]).replace('@@DEVICE',html.escape(identity['device']))
    page=page.replace('@@DATA',json.dumps(data,separators=(',',':')).replace('</','<\\/'))
    (export/'index.html').write_text(page)
    save(evidence/'report-manifest.json',dict(export=str(export),assets={str(p.relative_to(export)):dict(bytes=p.stat().st_size,sha256=digest(p)) for p in sorted(export.rglob('*')) if p.is_file()}))
    print(json.dumps(dict(groups=len(summary),runs=len(receipts['timing']['runs']),frames=108*150,assets=len(list(export.rglob('*'))),guards=guards),indent=2))


PAGE=r'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cubey SDF — Frozen base, live edits</title>
<style>
:root{font-family:system-ui,sans-serif;color:#182d38;background:#f1f5f4;font-size:16px;line-height:1.6}*{box-sizing:border-box}body{margin:0}main{max-width:1200px;margin:auto;padding:44px 24px 80px}header{padding:35px 0}h1{font-size:clamp(2.2rem,5vw,4rem);line-height:1.08;letter-spacing:-.04em;max-width:930px}h2{font-size:1.8rem;line-height:1.2;margin:0 0 20px}h3{line-height:1.3}p{max-width:900px}.eyebrow{font-size:.8rem;text-transform:uppercase;letter-spacing:.14em;color:#53717b}.lead{font-size:1.25rem;max-width:930px}.card{background:#fff;border:1px solid #d4dfdf;border-radius:16px;padding:26px;margin:24px 0}.note{background:#e8f1ed;border-left:4px solid #237663;padding:16px 20px;border-radius:6px}.warning{background:#fff2db;border-color:#a46812}.grid{display:grid;grid-template-columns:1fr 1fr;gap:24px}.stat{font-size:2.1rem;line-height:1.1;font-weight:700}.muted,small{color:#526a75}.controls{display:flex;gap:18px;flex-wrap:wrap;align-items:center;margin:16px 0}label{font-size:.9rem}select,button{font:inherit;border:1px solid #a9b9bc;padding:7px 11px;background:#fff;border-radius:6px;color:#182d38}button{cursor:pointer}input[type=range]{width:180px;vertical-align:middle}table{width:100%;border-collapse:collapse;font-size:.87rem;font-variant-numeric:tabular-nums}th,td{padding:9px 12px;border-bottom:1px solid #dbe3e2;text-align:right}th:first-child,td:first-child{text-align:left}thead{background:#f1f5f4}.scroll{overflow-x:auto}a{color:#096b83}img{max-width:100%;display:block}.image-pair{display:grid;grid-template-columns:1fr 1fr;gap:12px}.image-pair figure{margin:0}.image-pair img{width:100%;background:#dce9ec;border-radius:9px}.image-pair figcaption{font-size:.86rem;color:#526a75;margin:6px 0}.chart{width:100%;height:auto;display:block}code{font-size:.88em;background:#eef2f2;padding:2px 4px}.compact{line-height:1.5}footer{border-top:1px solid #cbd9da;margin-top:36px;padding-top:24px;font-size:.85rem}nav a{margin-right:20px}li{margin:7px 0}.pill{display:inline-block;background:#e8eeef;border-radius:24px;padding:4px 12px;font-size:.8rem}summary{cursor:pointer;font-weight:600}@media(max-width:720px){.grid,.image-pair{grid-template-columns:1fr}.card{padding:18px}main{padding:20px 14px 60px}h1{font-size:2.5rem}th,td{padding:8px}}
</style></head><body><main>
<header><div class="eyebrow">Cubey · native SDF study · 30 October 2026 · stages 1–2</div>
<h1>A frozen base and live CSG work.<br>The handoff still matters.</h1>
<p class="lead">We implemented the projectR-inspired composition in Cubey, tested additions and real cuts, then measured the complete shaded path. The source-defined candidate preserves the tested geometry. A completed sampled bake can still erase a pending feature.</p>
<nav><a href="#mechanism">Mechanism</a><a href="#performance">Performance</a><a href="#images">Native images</a><a href="#handoff">Handoff</a><a href="#next">Next decision</a><a href="#evidence">Evidence</a></nav></header>
<div class="note warning"><strong>Decision: keep this as a private study.</strong> Retain source/history and the ordered pending expression. Do not promote automatic preview retirement or begin a background updater on the assumption that cache completion preserves the live surface. Public modes and the Balanced default are unchanged.</div>
<section class="card" id="mechanism"><h2>What was actually built</h2>
<div class="grid"><div><span class="pill">Candidate A · sampled base</span><h3>Reconstructed cache + pending CSG</h3><p>Use the frozen R16 trilinear base where it is a valid scalar, then apply the bounded canonical postfix suffix. Uniform empty/filled cells supply intervals, with source fallback when a value is needed. This is an approximate base with precise pending primitives.</p></div><div><span class="pill">Candidate B · source defined</span><h3>Cache certificates + pending CSG</h3><p>Compose conservative cached base intervals with the suffix. Step only when the interval certifies clearance; otherwise evaluate the frozen source prefix. The visible surface remains source defined. This is distinct from Cubey’s existing Hybrid rendering mode.</p></div></div>
<p>Both paths use the same edited expression for traversal, finite-difference normals and hard-shadow rays. Materials and cut ownership use the complete canonical edited program. The base field and region references remain frozen; new additions are included in the edited traversal bounds. Workspace clipping is applied once, after composition.</p>
<p class="muted">A is a new experimental walk at a tight hit tolerance, not the current Cached renderer with a tail attached. Its cost cannot be attributed to “cached sampling” in general. Both candidates add interval/postfix machinery; the public controls use the existing optimized renderer. Domain warps, glass, general source replacement and palette changes are outside admission.</p>
<div class="grid"><div><div class="stat">213</div><p>Source candidate checks passed: 155 existing controls + 58 pending controls, with the original depth/normal/tangent rules.</p></div><div><div class="stat">58 / 58</div><p>Pending controls pass for A too: additions outside old bounds, inside cavities, through-holes, smooth operations, groups, clipping and empty bases. Existing sampled base losses remain.</p></div></div>
<p id="qualification-text"></p><p>Native compute-to-fragment validation flags remain visible below. A flagged pixel is excluded from the geometry comparisons; exclusion is not a full-image pass.</p><div class="scroll"><table><thead><tr><th>Subject / route</th><th>Native pixels</th><th>Flagged</th><th>Exhaustion color</th></tr></thead><tbody id="validation-table"></tbody></table></div>
</section>
<section class="card" id="performance"><h2>How tail cost grows</h2>
<p>1080p, SS1, native shaded output with hard shadows. Three serial interleaved repeats; each has 30 warmup + 150 measured frames. Lines show the median of the three run p95 values. The table retains repeat range and counts every frame over the deadline. These are finite desktop GPU trials, not input latency or headset qualification.</p>
<div class="controls"><label>Subject <select id="subject"><option value="robot">Survey Robot</option><option value="guardian">Guardian</option></select></label><label>Rows <select id="rows"><option value="all">All routes</option><option value="live">Live candidates</option><option value="controls">Fresh baked controls</option></select></label><button id="reset">Reset</button></div>
<svg id="chart" class="chart" viewBox="0 0 1000 390" role="img" aria-label="GPU p95 by pending leaf count"></svg>
<p id="timing-caption" class="muted"></p><div class="scroll"><table><thead><tr><th>Route / pending leaves</th><th>p50 ms</th><th>p95 ms [repeat range]</th><th>worst p99</th><th>max</th><th>&gt;11.11 ms</th><th>&gt;16.67 ms</th></tr></thead><tbody id="timing-table"></tbody></table></div>
<p class="muted">Tail sweep uses visible hard-union stamps. Cuts and smooth operations receive geometry/native-image controls, but their long-tail performance is not established by this matrix. Source limits are enforced: Robot reaches 146 total leaves at +64; full Guardian reaches 1,024 at +13. Guardian +14 and any +65 pending tail reject before GPU allocation.</p>
<details><summary>Why the first timing attempt was discarded</summary><p>The first private walk traced the whole workspace and repeatedly lost useful empty-cell bounds. Four completed Robot pilot runs measured A at 110–114 ms and B at 93–97 ms p95. The sweep was stopped, retained, and replaced with conservative combined-scene roots and signed frozen-region certificates. Geometry checks were rerun under a new source identity. Final results do not mix those shader versions.</p></details>
</section>
<section class="card" id="images"><h2>Compare the native output</h2><p>The same edited source, lighting and camera, at 960×540 SS1. These images include normals, full material evaluation and hard shadows. The blue wall of the through-hole is the cut material.</p>
<div class="controls"><label>Case <select id="case"></select></label><label>Left <select id="left-mode"></select></label><label>Right <select id="right-mode"></select></label></div>
<div class="image-pair"><figure><img id="left-image" alt="Left native SDF comparison"><figcaption id="left-caption"></figcaption></figure><figure><img id="right-image" alt="Right native SDF comparison"><figcaption id="right-caption"></figcaption></figure></div>
<h3>Change pending geometry while moving the camera</h3><p>Sixteen native frames per subject/policy, with one frozen base field and one region build. The tail grows from one to eight leaves. Upload, frame completion and readback are serialized; this is composition and motion evidence, not a continuous-input latency trial.</p>
<div class="controls"><label>Motion subject <select id="motion-subject"><option value="robot">Survey Robot</option><option value="guardian">Guardian</option></select></label><label>Frame <input id="frame" type="range" min="0" max="15" value="0"></label><span id="frame-label"></span><button id="play">Play</button></div>
<div class="image-pair"><figure><img id="motion-a" alt="Sampled base with changing pending geometry"><figcaption>A · sampled base</figcaption></figure><figure><img id="motion-b" alt="Source defined changing pending geometry"><figcaption>B · source defined</figcaption></figure></div></section>
<section class="card" id="handoff"><h2>Finishing a bake is not enough</h2><p>One native ray starts at the center of a radius 0.35h pending cavity. Both live candidates find its wall at about <strong>0.0001953</strong> world units. The fresh ordinary cache fills the cavity; the ray instead reaches the outer body near <strong>0.1996</strong>. The normal reverses. This reproduces the earlier scalar research in the native tracer.</p>
<div class="note warning">Automatic sampled handoff fails the frozen gate. Other regular rays also shift by about 0.00009 under ordinary Cached’s looser hit policy, above the predeclared source tolerance. These numerical shifts and the missing cavity are separate effects. A smaller interpolation error alone cannot guarantee preserved topology near zero.</div>
<h3>Whole-image handoff comparisons at 320×180</h3><p>Both images’ native validation flags are excluded. Depth compares matched hits; normals compare hits within two voxels. The limits and excluded pixels are explicit. Fresh scenes are independently rebuilt in separate native runs; no concurrent cache publication or in-process retirement is claimed. The existing source diagnostic guard admits explicit combined controls; the source geometry reference here is that control, separately labeled from public Analytical timing/images.</p><p id="bridge-note" class="muted"></p><div class="scroll"><table><thead><tr><th>Subject / live → fresh</th><th>Unscored</th><th>Lost / gained pixels</th><th>Depth p95</th><th>Normal p95 °</th></tr></thead><tbody id="handoff-table"></tbody></table></div>
<p id="build-time"></p><p id="build-cost"></p></section>
<section class="card" id="next"><h2>What to try next, and what must pass</h2><ol>
<li><strong>Choose the display contract first.</strong> For an explicitly approximate editing profile, author in sampled geometry consistently and expose its minimum-feature policy. For source-faithful editing, keep source evaluation or a qualified analytic patch after baking. Do not silently retire a visible tiny cut into a cache that cannot represent it.</li>
<li><strong>Make the bounded renderer competitive before a long-tail editor.</strong> The zero-tail controls expose prototype overhead; the tail sweep exposes repeated pending work across primary, normal and shadow queries. Investigate specialization and spatial tail filtering under the same geometry gates. A brush’s solid AABB is not an exact scalar invalidation region.</li>
<li><strong>Then select an updater.</strong> Compare canonical pruned full construction against stable local patches, measuring CPU preparation, all GPU work, uploads, setup, memory and work-unit time. Keep the current full builder as the recovery control. A full build’s elapsed time is not a schedulable per-frame budget.</li>
<li><strong>Only then qualify version publication and input.</strong> Preserve exact suffix accounting through new edits, undo/redo/branch changes and stale build completion. Measure service/drain rate, oldest edit age, tail high-water and input-to-completed-frame latency while rendering. A finite 600-leaf stream needs Robot or the qualified Guardian history prefix; baking does not free canonical leaf capacity.</li>
</ol><p>The study justifies retaining the frozen-base/ordered-tail hypothesis. It does not select a production renderer, certify a positive sustainable tail cap, or validate continuous VR editing. Public Cached/Balanced/Analytical/Hybrid behavior is unchanged.</p></section>
<section class="card" id="evidence"><h2>Reproducible evidence and limits</h2><p>Implementation source <code>@@SOURCE</code>; @@DEVICE. The original 194 SPIR-V files and 567 protected source/content files are byte unchanged. Private binaries, all eight private shader hashes, exact commands, complete frame samples, admission rejections and native receipts are retained.</p>
<ul><li><a href="summary.json">Decision data</a> · <a href="timing.json">108 raw timing run receipts</a> · <a href="geometry.json">Geometry scores</a></li><li><a href="admission.json">Admission and negative cases</a> · <a href="qualification.json">Camera/shadow replay</a> · <a href="postqualification.json">Native pixel validation and handoff comparisons</a></li><li><a href="pending-input.json">Pending known-answer inputs</a> · <a href="pending-source.json">Source live results</a> · <a href="fresh-sampled.json">Fresh sampled results</a> · <a href="raw-manifest.json">Raw artifact hashes and local locations</a></li></ul>
<p>Failed pilots remain separate from the final set. Boundary GPU load observations are recorded; they do not establish continuous external exclusivity. No background builder, allocator, publication state machine, brush UI, input timing, durable 600-leaf stream, stereo compositor, headset or human motion acceptance is claimed. Browser discovery had no connected browser: static interaction checks and HTTP delivery are distinct from rendered browser acceptance.</p>
<p>Earlier context: <a href="http://starship:8787/reports/0738ac47582410f1eb1387c0a5868b599675d18c170d7b06b78a59dfad1316eb/">Continuous-editing research and plan</a> · <a href="http://starship:8787/reports/b37a56f83a63404aba37029a521824414e0e5c5cecb50423d7b2a60f706fb184/">Selectable modes and Balanced default</a></p></section>
<footer>Private exploration · local commits only · report snapshots are immutable · native images are lossless WebP exports of the recorded PNGs.</footer>
</main><script>
const data=@@DATA;
const names={cached:'A · sampled base',source:'B · source defined','fresh-cached':'Fresh Cached','fresh-balanced':'Fresh Balanced','fresh-analytic':'Fresh Analytical','fresh-source-control':'Fresh source control','live-cached':'A · sampled base','live-source':'B · source defined'};
const colors={cached:'#bb621c',source:'#16736d','fresh-cached':'#4285b0','fresh-balanced':'#744c9b','fresh-analytic':'#677580'};
const el=id=>document.getElementById(id),fmt=(v,n=2)=>Number(v).toFixed(n);
function updateTiming(){
 const subject=el('subject').value,filter=el('rows').value;
 const rows=data.timing.filter(r=>r.subject===subject&&(filter==='all'||(filter==='live')!==r.policy.startsWith('fresh-')));
 const maxX=Math.max(...rows.map(r=>r.count),1),maxY=Math.max(18,...rows.map(r=>r.p95_range[1]))*1.08;
 const x=n=>75+n/maxX*880,y=n=>330-n/maxY*280;
 let svg='';
 for(let i=0;i<=4;i++){let v=maxY*i/4;svg+=`<line x1="75" y1="${y(v)}" x2="955" y2="${y(v)}" stroke="#dae4e4"/><text x="63" y="${y(v)+5}" text-anchor="end" font-size="13">${fmt(v,1)}</text>`;}
 for(const v of [11.11,16.67])svg+=`<line x1="75" y1="${y(v)}" x2="955" y2="${y(v)}" stroke="#a56b34" stroke-dasharray="5 5"/><text x="80" y="${y(v)-5}" font-size="12">${v} ms</text>`;
 let legend=0;
 for(const policy of Object.keys(colors)){
  const line=rows.filter(r=>r.policy===policy).sort((a,b)=>a.count-b.count);if(!line.length)continue;
  svg+=`<text x="${75+legend++*180}" y="20" fill="${colors[policy]}" font-size="13">${names[policy]}</text><polyline points="${line.map(r=>`${x(r.count)},${y(r.p95_ms)}`).join(' ')}" fill="none" stroke="${colors[policy]}" stroke-width="2.5"/>`;
  for(const r of line)svg+=`<circle cx="${x(r.count)}" cy="${y(r.p95_ms)}" r="4" fill="${colors[policy]}"><title>${names[policy]} +${r.count}: ${fmt(r.p95_ms)} ms</title></circle>`;
 }
 for(const n of [...new Set(rows.map(r=>r.count))])svg+=`<text x="${x(n)}" y="353" text-anchor="middle" font-size="13">${n}</text>`;
 svg+='<text x="500" y="382" text-anchor="middle" font-size="14">Pending leaves / freshly baked equivalent</text>';
 el('chart').innerHTML=svg;
 el('timing-table').innerHTML=rows.sort((a,b)=>a.count-b.count||a.policy.localeCompare(b.policy)).map(r=>`<tr><td>${names[r.policy]} +${r.count}</td><td>${fmt(r.p50_ms)}</td><td>${fmt(r.p95_ms)} [${r.p95_range.map(v=>fmt(v)).join('–')}]</td><td>${fmt(r.p99_ms)}</td><td>${fmt(r.max_ms)}</td><td>${r.over_11_11}/450</td><td>${r.over_16_67}/450</td></tr>`).join('');
 el('timing-caption').textContent=`${subject==='robot'?'Survey Robot':'Full Guardian'} · each row has 450 measured frames; p99 is the worst run p99, max is the worst recorded frame. Fresh controls include the same zero/eight stamps in the newly baked base.`;
 return rows;
}
function updateImages(){const c=el('case').value;for(const side of ['left','right']){const mode=el(side+'-mode').value;el(side+'-image').src=data.images[c][mode];el(side+'-caption').textContent=names[mode]+' · '+c;}return c;}
function updateMotion(){const s=el('motion-subject').value,n=Number(el('frame').value);el('motion-a').src=data.motion[s].cached[n];el('motion-b').src=data.motion[s].source[n];el('frame-label').textContent=`${n+1}/16 · ${1+Math.floor(n/2)} pending leaves`;return n;}
function init(){
 el('case').innerHTML=Object.keys(data.images).map(c=>`<option value="${c}">${c}</option>`).join('');el('case').value='guardian-8';
 for(const side of ['left','right'])el(side+'-mode').innerHTML=Object.keys(data.images['guardian-8']).map(m=>`<option value="${m}">${names[m]}</option>`).join('');
 el('left-mode').value='live-cached';el('right-mode').value='fresh-analytic';
 for(const id of ['subject','rows'])el(id).addEventListener('change',updateTiming);
 for(const id of ['case','left-mode','right-mode'])el(id).addEventListener('change',updateImages);
 el('motion-subject').addEventListener('change',updateMotion);el('frame').addEventListener('input',updateMotion);
 el('reset').addEventListener('click',()=>{el('subject').value='robot';el('rows').value='all';updateTiming();});
 let timer=null;el('play').addEventListener('click',()=>{if(timer){clearInterval(timer);timer=null;el('play').textContent='Play';}else{timer=setInterval(()=>{el('frame').value=(Number(el('frame').value)+1)%16;updateMotion();},180);el('play').textContent='Pause';}});
 const camera=data.qualification.reduce((n,r)=>n+r.camera_rays,0),shadow=data.qualification.reduce((n,r)=>n+r.shadow_rays,0);
 el('qualification-text').textContent=`Additional deterministic replay: ${camera.toLocaleString()} camera rays, ${shadow.toLocaleString()} shadow rays and ${data.qualification.reduce((n,r)=>n+r.points,0).toLocaleString()} spatial point queries across the two subjects/policies. No replay exhaustion; B's cached intervals enclose full-source values at the tested points.`;
 el('validation-table').innerHTML=data.native_validation.map(r=>`<tr><td>${r.subject} / ${names[r.mode]}</td><td>${r.pixels}</td><td>${r.unscored_pixels}</td><td>${r.ordinary_exhaustion_pixels}</td></tr>`).join('');
 el('handoff-table').innerHTML=data.handoff.map(r=>`<tr><td>${r.subject} / ${names[r.live]} → ${names['fresh-'+r.fresh]}</td><td>${r.unscored_pixels}</td><td>${r.false_background} / ${r.false_foreground}</td><td>${fmt(r.depth_world.p95||0,7)}</td><td>${fmt(r.compatible_normal_degrees.p95||0)}</td></tr>`).join('');
 el('bridge-note').textContent=data.source_diagnostic_relation.map(r=>`${r.subject}: explicit combined versus public Analytical ordinary images differ at ${r.different_pixels} pixels, maximum channel difference ${r.max_channel_difference}/255.`).join(' ')+' The diagnostic control is not relabeled as public-mode equivalence.';
 el('build-cost').textContent='The synchronous baseline still constructs a whole field. Each frozen candidate allocates 912,997,552 bytes of geometry storage, plus source regions and rendering resources. Two such fields alone request about 1.70 GiB before overlapping-region, staging and renderer costs. Actual overlapping-version residency and updater service rate remain unmeasured.';
 el('build-time').textContent=data.construction.map(r=>`${r.subject}: median full base-field construction ${fmt(r.field_build_median_ms,1)} ms, region construction/upload ${fmt(r.region_build_median_ms,1)} ms.`).join(' ')+' These are synchronous initialization costs, excluded from the warmed rendering table; they are not schedulable work-unit budgets.';
 updateTiming();updateImages();updateMotion();
}
init();window.liveEditReport={data,updateTiming,updateImages,updateMotion};
</script></body></html>'''


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--raw',type=Path,required=True);p.add_argument('--post',type=Path,required=True);p.add_argument('--export',type=Path,required=True)
    a=p.parse_args();build(a.raw.resolve(),a.post.resolve(),a.export.resolve(),Path(__file__).parent)


if __name__=='__main__':main()
