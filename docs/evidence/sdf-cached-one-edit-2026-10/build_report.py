#!/usr/bin/env python3
"""Post-timing Cached/one-edit analysis and dedicated HTML export. No GPU work."""
import argparse
import json
from pathlib import Path
import shutil
import statistics
import sys

import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'projects/sdf/tools'))
from investigate_guardian import digest, PRESETS, camera_queries
from quality_speed_analysis import compare, geometry


def save(path,value):path.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n')


def validation(prefix):
    a=np.asarray(Image.open(prefix.with_suffix('.png')).convert('RGB'))
    b=np.asarray(Image.open(prefix.with_suffix('.validated.png')).convert('RGB'))
    invalid=np.any(a!=b,axis=-1);ys,xs=np.nonzero(invalid)
    Image.fromarray(np.where(invalid,0,255).astype('uint8')).save(prefix.with_suffix('.valid.png'))
    return dict(pixels=int(invalid.size),unscored_pixels=int(invalid.sum()),
        exhaustion_color_pixels=int(((a[:,:,0]>250)&(a[:,:,1]<5)&(a[:,:,2]>250)).sum()),
        flags=[dict(x=int(x),y=int(y),rgb=b[y,x].tolist()) for y,x in zip(ys,xs)])


def build(raw,export):
    evidence=Path(__file__).parent
    export.mkdir(parents=True,exist_ok=False);(export/'images').mkdir()
    packet={name:json.loads((raw/path).read_text()) for name,path in dict(admission='prepare/admission.json',
        geometry='geometry/geometry.json',defaults='defaults/defaults.json',replay='replay/replay.json',
        captures='captures/captures.json',sequences='sequences/sequences.json',timing='timing/timing.json').items()}
    timing=packet['timing']
    if any(r.get('changed_cached_rays',0)<10 for r in packet['replay']['runs']):
        raise RuntimeError('visible edit replay coverage is missing')
    for name,value in packet.items():
        if not value['complete']:raise RuntimeError(f'incomplete {name}')
        for key in ('source_head','source_diff_sha256','binary_sha256','current_public_sha256','public_binary_sha256','shaders'):
            if value[key]!=timing[key]:raise RuntimeError(f'{name} identity differs: {key}')
    private=ROOT/'build/release/projects/sdf/cubey_project_sdf_cached_edit_study'
    if digest(private)!=timing['binary_sha256']:raise RuntimeError('current private executable differs')
    for name,expected in timing['shaders'].items():
        if digest(private.parent/'shaders'/name)!=expected:raise RuntimeError('private shader differs')
    baseline=json.loads((raw.parent/'baseline.json').read_text())
    if any(digest(private.parent/'shaders'/name)!=h for name,h in baseline['shader_hashes'].items()):raise RuntimeError('original shader changed')
    if any(digest(ROOT/name)!=h for name,h in baseline['protected'].items()):raise RuntimeError('protected source/content changed')
    groups={}
    for r in timing['runs']:groups.setdefault((r['subject'],r['view'],r['operation'],r['phase']),[]).append(r)
    if len(groups)!=24 or len(timing['runs'])!=72:raise RuntimeError('incomplete timing matrix')
    stats=[]
    for (subject,view,operation,phase),runs in groups.items():
        if sorted(r['repeat'] for r in runs)!=[1,2,3]:raise RuntimeError('missing repeat')
        if any(len(r['passes']['sdf.whole_scene']['values_ms'])!=150 for r in runs):raise RuntimeError('missing frame samples')
        stats.append(dict(subject=subject,view=view,operation=operation,phase=phase,
            p50_ms=statistics.median(r['stats']['p50_ms'] for r in runs),p95_ms=statistics.median(r['stats']['p95_ms'] for r in runs),
            p95_range=[min(r['stats']['p95_ms'] for r in runs),max(r['stats']['p95_ms'] for r in runs)],
            p99_ms=max(r['stats']['p99_ms'] for r in runs),max_ms=max(r['stats']['max_ms'] for r in runs),
            over_11_11=sum(r['stats']['over_11_11'] for r in runs),over_16_67=sum(r['stats']['over_16_67'] for r in runs)))
    zero_checks=[]
    for subject in ('guardian','robot'):
        for view in ('preset','close'):
            select=lambda phase:next(r for r in stats if r['subject']==subject and r['view']==view and r['phase']==phase)
            original,zero=select('original'),select('zero');limit=max(.2,.05*original['p95_ms'])
            zero_checks.append(dict(subject=subject,view=view,original_p95_ms=original['p95_ms'],zero_p95_ms=zero['p95_ms'],
                delta_ms=zero['p95_ms']-original['p95_ms'],allowance_ms=limit,passed=zero['p95_ms']<=original['p95_ms']+limit))
    native_validation=[];handoff=[]
    for r in packet['captures']['runs']:
        if r['phase'].endswith('-probe'):
            native_validation.append(dict(subject=r['subject'],operation=r['operation'],phase=r['phase'],**validation(Path(r['prefix']))))
    for subject in ('guardian','robot'):
        for operation in ('add','cut'):
            preview=raw/'captures'/f'{subject}-{operation}-preview-probe';committed=raw/'captures'/f'{subject}-{operation}-committed-probe'
            row=compare(preview,committed,evidence/f'{subject}-{operation}-handoff')
            row.update(subject=subject,operation=operation);handoff.append(row)
    grazing=[]
    for subject in ('guardian','robot'):
        a,_=geometry(raw/'captures'/f'{subject}-add-preview-probe.bin')
        b,_=geometry(raw/'captures'/f'{subject}-add-committed-probe.bin')
        delta=np.where((a>=0)&(b>=0),np.abs(a-b),0)
        cfg=json.loads((ROOT/f'projects/sdf/presets/{PRESETS[subject]}.json').read_text())
        rays=camera_queries(cfg,320,180)
        tool=json.loads((raw/'prepare'/f'{subject}-add.json').read_text())['cases'][0]['scene']['root']['children'][1]
        for index in np.argsort(delta.ravel())[-5:][::-1]:
            y,x=divmod(int(index),320);q=rays[int(index)];offset=np.asarray(q['origin'])-np.asarray(tool['position']);direction=np.asarray(q['direction'])
            clearance=float(np.linalg.norm(offset-direction*np.dot(offset,direction))-tool['radius'])
            grazing.append(dict(subject=subject,pixel=[x,y],preview_t=float(a[y,x]),committed_t=float(b[y,x]),
                depth_change=float(delta[y,x]),sphere_ray_clearance=clearance,cached_hit_epsilon=1/(256*7)*.2))
    save(evidence/'grazing-depth-diagnostics.json',dict(scope='Independent ray/sphere clearance for largest matched-hit add depth changes; supports tolerance-band interpretation, not a hardware timing attribution',cases=grazing))
    image_checks=[];images={};sequences=[]
    def convert(source,name):
        image=Image.open(source).convert('RGB');target=export/'images'/(name+'.webp')
        image.save(target,'WEBP',lossless=True)
        a=np.asarray(image);b=np.asarray(Image.open(target).convert('RGB'))
        if not np.array_equal(a,b):raise RuntimeError('lossless export differs')
        image_checks.append(dict(source=str(source),export=str(target.relative_to(export)),
            exhaustion_color_pixels=int(((a[:,:,0]>250)&(a[:,:,1]<5)&(a[:,:,2]>250)).sum())))
        return str(target.relative_to(export))
    for r in packet['captures']['runs']:
        if r['phase'] not in ('base','preview','committed') or 'image' not in r:continue
        if r['operation']=='none':continue
        key=f"{r['subject']}-{r.get('view','preset')}-{r['operation']}"
        images.setdefault(key,{})[r['phase']]=convert(raw/'captures'/r['image'],Path(r['image']).stem)
    for r in packet['sequences']['runs']:
        row=dict(subject=r['subject'],operation=r['operation'],fault=r['fault'],frames=[],commits=r['native']['commits'],
                 retired_after_frame=r['native']['pending_retired_after_completed_frame'],
                 prewarm_ms=r['native']['candidate_renderer_prewarm_ms'],identity=r['native']['programs'][0])
        for frame in r['native']['frames']:
            name=f"{r['subject']}-{r['operation']}-{'failure-retry' if r['fault'] else 'commit'}-{frame['frame']}"
            row['frames'].append(dict(frame,image=convert(Path(frame['image']),name)))
        sequences.append(row)
    windows=[]
    for label in ('robot','workshop'):
        source=raw.parent/f'window-{label}';value=json.loads((source/'window.json').read_text())
        if not value['passed'] or value['mode_rows'][0]['effective']!='cached':raise RuntimeError('native default window failed')
        windows.append(dict(subject=label,receipt=value,image=convert(source/'initial.png',f'window-{label}')))
        save(evidence/f'window-{label}.json',value)
    commits=[]
    for subject in ('guardian','robot'):
        rows=[c for r in sequences if r['subject']==subject for c in r['commits'] if c['published']]
        commits.append(dict(subject=subject,samples=len(rows),ready_median_ms=statistics.median(c['ready_to_render_ms'] for c in rows),
            completed_median_ms=statistics.median(c['to_completed_replacement_frame_ms'] for c in rows),
            ready_range_ms=[min(c['ready_to_render_ms'] for c in rows),max(c['ready_to_render_ms'] for c in rows)],
            allocation_median_ms=statistics.median(c['allocation_ms'] for c in rows),field_build_median_ms=statistics.median(c['field_build_ms'] for c in rows),
            peak_field_payload_bytes=max(c['peak_field_payload_bytes'] for c in rows)))
    summary=dict(source_head=timing['source_head'],binary_sha256=timing['binary_sha256'],device=timing['device'],
        shaders=timing['shaders'],default='Cached',timing=stats,zero_checks=zero_checks,images=images,sequences=sequences,
        windows=windows,commits=commits,native_validation=native_validation,handoff=handoff,
        geometry=packet['geometry'],replay=packet['replay'],
        preservation=dict(original_shaders=len(baseline['shader_hashes']),protected_files=len(baseline['protected'])))
    for name,value in packet.items():save(evidence/(name+'.json'),value)
    placement=json.loads((raw.parent/'visible-placement.json').read_text())
    save(evidence/'visible-placement.json',dict(controls=placement,source_replay_root=str(raw.parent/'final/replay'),
        reference_hashes={p.name:digest(p) for subject in ('guardian','robot') for p in
            (raw.parent/'final/replay'/f'{subject}-preset-cut.json',raw.parent/'final/replay'/f'{subject}-preset-cut.input.json')}))
    save(evidence/'summary.json',summary);save(evidence/'image-checks.json',dict(passed=True,images=len(image_checks),checks=image_checks))
    for name in ('fixtures.json','preview.json','fresh-cached.json'):shutil.copy2(raw/'geometry'/name,evidence/name)
    files={str(p.relative_to(raw)):dict(bytes=p.stat().st_size,sha256=digest(p)) for p in sorted(raw.rglob('*')) if p.is_file()}
    save(evidence/'raw-manifest.json',dict(root=str(raw),files=files))
    for name in ('summary.json','timing.json','geometry.json','defaults.json','sequences.json','replay.json','admission.json','raw-manifest.json','grazing-depth-diagnostics.json'):
        shutil.copy2(evidence/name,export/name)
    page=PAGE.replace('@@DATA',json.dumps(summary,separators=(',',':')).replace('</','<\\/')).replace('@@HEAD',timing['source_head'][:8])
    (export/'index.html').write_text(page)
    save(evidence/'report-manifest.json',dict(export=str(export),assets={str(p.relative_to(export)):dict(bytes=p.stat().st_size,sha256=digest(p)) for p in sorted(export.rglob('*')) if p.is_file()}))
    print(json.dumps(dict(zero_checks=zero_checks,commits=commits,native_validation=native_validation,
        image_exports=len(image_checks),timing_groups=len(stats)),indent=2))


PAGE=r'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Cubey SDF — Cached plus one edit</title><style>
:root{font:16px/1.6 system-ui,sans-serif;color:#152f3a;background:#f2f5f3}*{box-sizing:border-box}body{margin:0}main{max-width:1160px;margin:auto;padding:48px 24px 70px}header{padding:30px 0}h1{font-size:clamp(2.4rem,5vw,4.1rem);line-height:1.08;letter-spacing:-.045em;max-width:950px}h2{font-size:1.7rem;line-height:1.2;margin-top:0}p{max-width:940px}.eyebrow{font-size:.8rem;letter-spacing:.13em;text-transform:uppercase;color:#52726e}.lead{font-size:1.2rem}.card{background:white;border:1px solid #d4dfda;border-radius:14px;padding:26px;margin:24px 0}.note{padding:17px 20px;border-radius:8px;background:#dfefe7;border-left:4px solid #20745e}.warning{background:#fff0d6;border-color:#aa671c}.grid{display:grid;grid-template-columns:1fr 1fr;gap:22px}.metrics{display:grid;grid-template-columns:repeat(4,1fr);gap:16px}.number{font-size:2.1rem;font-weight:700;line-height:1.2}.muted,small,figcaption{color:#506c73}.controls{display:flex;gap:18px;align-items:center;flex-wrap:wrap;margin:18px 0}select,button{font:inherit;color:inherit;background:#fff;border:1px solid #abbeb7;border-radius:6px;padding:6px 10px}button{cursor:pointer}label{font-size:.9rem}input{vertical-align:middle}svg,img{max-width:100%;display:block}figure{margin:0}figcaption{font-size:.86rem;margin-top:7px}.scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:.87rem;font-variant-numeric:tabular-nums}th,td{text-align:right;padding:9px 10px;border-bottom:1px solid #dce5df}th:first-child,td:first-child{text-align:left}thead{background:#f3f6f4}a{color:#076c80}nav a{margin-right:20px}code{background:#edf2ef;padding:2px 4px;font-size:.88em}.state{font-family:ui-monospace,monospace;font-size:.85rem;white-space:pre-line}summary{cursor:pointer}li{margin:8px 0}footer{font-size:.85rem;margin-top:35px;padding-top:20px;border-top:1px solid #ccd9d1}@media(max-width:740px){main{padding:20px 14px 50px}.grid,.metrics{grid-template-columns:1fr}.card{padding:18px}.metrics{grid-template-columns:1fr 1fr}h1{font-size:2.5rem}}
</style></head><body><main><header><div class="eyebrow">Cubey · 30 October 2026 · narrowed native editing trial</div><h1>Cached plus one edit.<br>Rendering is cheap again.</h1><p class="lead">Cached is now the default. Zero pending edits uses the original Cached pipeline. One live add or cut stays close to that rendering cost in the measured scenes. The expensive part is now the synchronous cache commit.</p><nav><a href="#cost">Rendering cost</a><a href="#preview">Native images</a><a href="#commit">Commit and retry</a><a href="#quality">Quality</a><a href="#evidence">Evidence</a></nav></header>
<div class="metrics"><div class="card"><div class="eyebrow">Guardian · one preview</div><div class="number" id="guardian-metric"></div><small>GPU p95 · preset · SS1</small></div><div class="card"><div class="eyebrow">Robot · one preview</div><div class="number" id="robot-metric"></div><small>GPU p95 · preset · SS1</small></div><div class="card"><div class="eyebrow">Guardian · commit</div><div class="number" id="guardian-commit"></div><small>Median ready-to-render stall</small></div><div class="card"><div class="eyebrow">Robot · commit</div><div class="number" id="robot-commit"></div><small>Median ready-to-render stall</small></div></div>
<div class="note"><strong>The narrow hypothesis works in this trial:</strong> render the cache, compose one convex operation, then retire that operation after a replacement Cached frame completes. Undo history stays in retained canonical source; it is not a pending GPU render list.</div>
<section class="card" id="cost"><h2>The zero-edit path is the actual Cached path</h2><p>The old general prototype routed even zero edits through an expensive new tracer. This version selects the existing Cached shader, without an edit control buffer, when no operation is pending. Zero-edit images match the original executable’s explicit Cached output byte for byte.</p><p>For an active sphere or unrounded box, a ray interval tells us which cached boundaries the hard union/cut hides. The cached walker is reused; a second pass is needed only after a hidden boundary. Base normals keep the old stencil, brush boundaries use the tool normal, and shadows use the same one-operation composition. There is no analytical evaluation of the base geometry.</p>
<div class="controls"><label>Subject <select id="subject"><option value="guardian">Guardian</option><option value="robot">Survey Robot</option></select></label><label>View <select id="view"><option value="preset">Preset</option><option value="close">Close</option></select></label><button id="reset">Reset view</button></div><svg id="chart" viewBox="0 0 960 330" role="img" aria-label="Matched Cached and one-operation GPU times"></svg><div class="scroll"><table><thead><tr><th>Route</th><th>p50 ms</th><th>p95 ms [range]</th><th>worst p99</th><th>max</th><th>&gt;11.11</th><th>&gt;16.67</th></tr></thead><tbody id="timing-table"></tbody></table></div><p class="muted">1080p SS1, fixed cameras, opaque atmosphere and hard shadows. Three serial interleaved repeats per row, each 30 warmup + 150 measured frames. p95 is the median run p95, with its repeat range; p99 is the worst run p99. Every row contains 450 measured frames. Brush geometry changes the workload, so a cheaper preview is not a general speedup claim.</p><p id="zero-text"></p><details><summary>The earlier high one-edit cost</summary><p>The first one-edit shader still had multiple call sites for the nested Cached walker and could trace again unnecessarily after the tool. Its short pilot measured roughly 5.4 ms Guardian and 19 ms Robot preset. Reusing one call site and reusing a valid first hit preserves the regular query controls and removes that avoidable cost. No hardware register/spill breakdown is claimed. The old 27 ms general-tracer number was not an inherent cost of cached editing.</p></details></section>
<section class="card" id="preview"><h2>Before, one pending edit, freshly committed cache</h2><div class="controls"><label>Image case <select id="image-case"></select></label><label>Compare <select id="compare"><option value="before">Base → preview</option><option value="handoff">Preview → committed</option></select></label></div><div class="grid"><figure><img id="left-image" alt="Native base or one-edit preview"><figcaption id="left-caption"></figcaption></figure><figure><img id="right-image" alt="Native one-edit preview or committed cache"><figcaption id="right-caption"></figcaption></figure></div><p>These are native 960×540 SS1 frames. The control cases include a through-cut with blue cut ownership, a recessed front wall and a cut across the workspace boundary. The settled Guardian/Robot images also match independently rebuilt Cached images from the original executable.</p></section>
<section class="card" id="commit"><h2>Committing remains a blocking operation</h2><p>The trial finishes the preview frame, allocates/builds a separate candidate field from canonical source, then switches its already-prepared ordinary Cached renderer to that field. The pending operation is retired only after the replacement frame completes. A candidate failure keeps the old cache and the same preview; the recorded next step explicitly retries.</p><div class="controls"><label>Sequence subject <select id="seq-subject"><option value="guardian">Guardian</option><option value="robot">Survey Robot</option></select></label><label>Operation <select id="seq-op"><option value="add">Add</option><option value="cut">Cut</option></select></label><label>Sequence <select id="fault"><option value="false">Commit normally</option><option value="true">Failure then retry</option></select></label><label>Frame <input id="frame" type="range" min="0" max="3" value="0"></label></div><div class="grid"><img id="sequence-image" alt="Recorded native cache commit sequence"><div><div class="state" id="sequence-state"></div><p id="sequence-cost"></p><p class="muted">Sequence frames are 960×540 SS1; the cache grid is unchanged. This is serialized headless work, not concurrent input or physical display timing. Completion includes readback synchronization but excludes PNG encoding. Renderer preparation is prewarmed and separately recorded.</p></div></div><div class="scroll"><table><thead><tr><th>Subject</th><th>Candidate allocation</th><th>Field build</th><th>Ready to render</th><th>Completed replacement</th></tr></thead><tbody id="commit-table"></tbody></table></div><p class="note warning">One pending operation simplifies rendering; it does not make the full builder instantaneous. During this serialized commit the GPU cannot keep presenting an updated brush. Continuous editing needs a separate measured update strategy.</p><p>Two fields request about 1.70 GiB of geometry payload before renderer/staging costs. The old field stays alive through publication. This is not an allocator residency measurement. Full source/history is retained, so the canonical 1,024-leaf capacity still applies.</p></section>
<section class="card" id="quality"><h2>Cached quality is the baseline</h2><p>The 29 regular sphere/box controls pass their frozen cached-profile limits: half a voxel in depth and two degrees away from seams. This does not widen or replace the earlier analytical-source contract. Dense camera and shadow replays have no exhausted rays. Every measured add/cut changes at least ten Cached control rays at 160×90, by classification or more than one voxel in depth. Initial non-visible cut placements were excluded and replaced with tools centered on measured visible Cached surfaces.</p><p class="note warning">The pending tool is analytical. A sub-grid cut can still disappear in the sampled cache. Grazing rays can also switch between a brush and the body behind it: four Guardian and five Robot add pixels exceed two voxels of depth change here, with maxima of 0.236 and 0.037 world units. Independent ray/sphere clearance calculations are consistent with Cached’s wider hit band causing the largest outline cases. Preview/cache tolerance matching remains unresolved; see <a href="grazing-depth-diagnostics.json">the diagnostics</a>.</p><p>Full-image comparisons below use native compute geometry payloads. Pixels flagged by each image’s compute-to-fragment validation are excluded and counted. Depth includes matched hits; normal differences include hits within two voxels. Maxima keep isolated edge errors visible even when p95 is small. These images have no scored hit/miss changes, but that does not mean identical visible depth layers.</p><div class="scroll"><table><thead><tr><th>Subject / operation</th><th>Unscored</th><th>Lost / gained</th><th>Depth &gt;2h</th><th>Depth p95 / max</th><th>Normal p95 / max °</th></tr></thead><tbody id="handoff-table"></tbody></table></div><p id="native-flags" class="muted"></p></section>
<section class="card"><h2>Cached is now the public default</h2><p>Authored scenes, material workshops and box recipes resolve Default to Cached. Balanced, refined Cached, Analytical and Hybrid remain selectable. Existing preset SS1/SS2 settings are independent and retain their values.</p><div class="grid"><figure><img id="window-robot" alt="Actual native SDF window with Default resolving to Cached"><figcaption>Owned Xvfb native scene window · preset SS2 retained · all modes/sample controls exercised.</figcaption></figure><figure><img id="window-workshop" alt="Actual native material workshop with Cached default"><figcaption>Owned native material workshop · Default resolves to Cached · material/specimen changes checked.</figcaption></figure></div><p>The preview remains a private native study executable. No brush UI, background updater, continuous-input service rate, undo UI or VR readiness is claimed.</p></section>
<section class="card" id="evidence"><h2>What this establishes</h2><ol><li><strong>Keep the Cached baseline.</strong> The zero-edit path has the original output and measured cost.</li><li><strong>One live hard add/cut is viable at that rendering scale in these tests.</strong> It needs no growing pending GPU list and no general source evaluation of the base.</li><li><strong>Optimize the commit next.</strong> Compare canonical pruned/local construction against the current full-field control. Include work-unit size, all preparation, memory and rendering interference before adding continuous input.</li><li><strong>Keep quality and history explicit.</strong> Preserve source/history separately, and decide whether preview should share the cache’s feature limits.</li></ol><p>Source <code>@@HEAD</code>; original 202 SPIR-V files and 567 protected source/content files unchanged. Final evidence: 72 timing runs / 10,800 measured frames, native images and 36 sequence frames, fault/retry publication, retained source and independent old-executable settled controls.</p><p><a href="summary.json">Decision data</a> · <a href="timing.json">Complete timing samples</a> · <a href="geometry.json">Geometry controls</a> · <a href="defaults.json">Default identity</a> · <a href="sequences.json">Commit/retry receipts</a> · <a href="replay.json">Camera/shadow replay</a> · <a href="admission.json">Admission rejections</a> · <a href="raw-manifest.json">Raw artifact hashes</a></p><p>Earlier: <a href="http://starship:8787/reports/41999c42e4ba06abdfad59d6ef6b005ca14d98c5c63a9a377123eaa00cc60e51/">General live-CSG prototype</a> · <a href="http://starship:8787/reports/0738ac47582410f1eb1387c0a5868b599675d18c170d7b06b78a59dfad1316eb/">Continuous-editing research plan</a>. Those earlier snapshots preserve their historical defaults and measurements.</p></section><footer>Local source/evidence commits · no push or project-site deployment · lossless native image exports · finite desktop GPU tests, not headset acceptance.</footer></main><script>
const data=@@DATA,el=id=>document.getElementById(id),fmt=(v,n=2)=>Number(v).toFixed(n);
const route=r=>r.phase==='original'?'Original Cached':r.phase==='zero'?'Zero pending · Cached':`${r.operation==='add'?'Add':'Cut'} · ${r.phase==='preview'?'one preview':'fresh Cached'}`;
function updateCost(){const rows=data.timing.filter(r=>r.subject===el('subject').value&&r.view===el('view').value);const max=Math.max(18,...rows.map(r=>r.p95_range[1]*1.1));const y=v=>245-v/max*190;let svg='';
 for(const v of [0,5,10,11.11,16.67])svg+=`<line x1="50" x2="940" y1="${y(v)}" y2="${y(v)}" stroke="${v===11.11||v===16.67?'#b27c3c':'#dce5df'}" stroke-dasharray="${v===11.11||v===16.67?'4 4':'0'}"/><text x="43" y="${y(v)+5}" text-anchor="end" font-size="12">${v}</text>`;
 rows.forEach((r,i)=>{let x=75+i*147,color=r.phase==='preview'?'#227e68':r.phase==='original'?'#526f89':'#96afa3';svg+=`<rect x="${x}" y="${y(r.p95_ms)}" width="100" height="${245-y(r.p95_ms)}" fill="${color}" rx="3"/><text x="${x+50}" y="${y(r.p95_ms)-9}" text-anchor="middle" font-size="15">${fmt(r.p95_ms)}</text><text x="${x+50}" y="269" text-anchor="middle" font-size="12">${route(r).split(' · ')[0]}</text><text x="${x+50}" y="287" text-anchor="middle" font-size="12">${route(r).split(' · ')[1]||''}</text>`;});el('chart').innerHTML=svg;
 el('timing-table').innerHTML=rows.map(r=>`<tr><td>${route(r)}</td><td>${fmt(r.p50_ms)}</td><td>${fmt(r.p95_ms)} [${r.p95_range.map(v=>fmt(v)).join('–')}]</td><td>${fmt(r.p99_ms)}</td><td>${fmt(r.max_ms)}</td><td>${r.over_11_11}/450</td><td>${r.over_16_67}/450</td></tr>`).join('');return rows;}
function updateImages(){const key=el('image-case').value,images=data.images[key],before=el('compare').value==='before';el('left-image').src=images[before?'base':'preview'];el('right-image').src=images[before?'preview':'committed'];el('left-caption').textContent=key+' · '+(before?'Cached base':'one pending operation');el('right-caption').textContent=key+' · '+(before?'one pending operation':'freshly committed Cached');return key;}
function updateSequence(reset=false){const row=data.sequences.find(r=>r.subject===el('seq-subject').value&&r.operation===el('seq-op').value&&String(r.fault)===el('fault').value);el('frame').max=row.frames.length-1;if(reset)el('frame').value=0;const i=Math.min(Number(el('frame').value),row.frames.length-1),f=row.frames[i];el('sequence-image').src=f.image;el('sequence-state').textContent=`Frame ${i+1}/${row.frames.length}: ${f.display}\nPending before frame: ${f.pending_before_frame}\nPending rendered: ${f.render_pending_operations}\nPending after completion: ${f.pending_after_completed_frame}\nRetirement after completed frame: ${row.retired_after_frame}`;let c=row.commits.filter(c=>c.frame<=f.frame).at(-1);el('sequence-cost').textContent=c?(c.published?`Replacement ready in ${fmt(c.ready_to_render_ms,1)} ms; completed frame in ${fmt(c.to_completed_replacement_frame_ms,1)} ms.`:`Candidate rejected after ${fmt(c.candidate_ready_ms,1)} ms. Old cache and pending preview retained.`):'The old cache remains available while one operation is previewed.';return f;}
function init(){for(const subject of ['guardian','robot']){const values=data.timing.filter(r=>r.subject===subject&&r.view==='preset'&&r.phase==='preview').map(r=>r.p95_ms);el(subject+'-metric').textContent=values.map(v=>fmt(v,1)).join(' / ')+' ms';const c=data.commits.find(r=>r.subject===subject);el(subject+'-commit').textContent=c.ready_median_ms>500?fmt(c.ready_median_ms/1000,2)+' s':fmt(c.ready_median_ms,0)+' ms';}
 el('image-case').innerHTML=Object.keys(data.images).map(k=>`<option value="${k}">${k}</option>`).join('');el('image-case').value='guardian-preset-cut';
 for(const id of ['subject','view'])el(id).addEventListener('change',updateCost);el('reset').addEventListener('click',()=>{el('subject').value='guardian';el('view').value='preset';updateCost();});
 for(const id of ['image-case','compare'])el(id).addEventListener('change',updateImages);for(const id of ['seq-subject','seq-op','fault'])el(id).addEventListener('change',()=>updateSequence(true));el('frame').addEventListener('input',()=>updateSequence());
 el('zero-text').textContent=data.zero_checks.map(r=>`${r.subject}/${r.view}: zero-edit delta ${fmt(r.delta_ms,3)} ms (${r.passed?'passes':'fails'} frozen allowance).`).join(' ');
 el('commit-table').innerHTML=data.commits.map(r=>`<tr><td>${r.subject} · ${r.samples} successful attempts</td><td>${fmt(r.allocation_median_ms,1)} ms</td><td>${fmt(r.field_build_median_ms,1)} ms</td><td>${fmt(r.ready_median_ms,1)} ms</td><td>${fmt(r.completed_median_ms,1)} ms</td></tr>`).join('');
 el('handoff-table').innerHTML=data.handoff.map(r=>`<tr><td>${r.subject} / ${r.operation}</td><td>${r.unscored_pixels}</td><td>${r.false_background} / ${r.false_foreground}</td><td>${r.incompatible_hit_pairs}</td><td>${fmt(r.depth_world.p95||0,7)} / ${fmt(r.depth_world.max||0,4)}</td><td>${fmt(r.compatible_normal_degrees.p95||0)} / ${fmt(r.compatible_normal_degrees.max||0)}</td></tr>`).join('');
 el('native-flags').textContent=data.native_validation.map(r=>`${r.subject}/${r.operation}/${r.phase}: ${r.unscored_pixels} flagged pixels`).join('; ')+'. These are not scored as passed geometry pixels.';
 for(const r of data.windows)el('window-'+r.subject).src=r.image;updateCost();updateImages();updateSequence();}
init();window.cachedEditReport={data,updateCost,updateImages,updateSequence};
</script></body></html>'''


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--raw',type=Path,required=True);p.add_argument('--export',type=Path,required=True)
    a=p.parse_args();build(a.raw.resolve(),a.export.resolve())


if __name__=='__main__':main()
