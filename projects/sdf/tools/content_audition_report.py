#!/usr/bin/env python3
"""Bundle the selected native content blockouts for visual review."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[3]
RAW=ROOT/'outputs/sdf/content-audition-20261103'
COPY={
 'spiral-shell':dict(tag='Visual first pick',promise='A recognizable coiled silhouette and a hollow channel that stays continuous around the spiral.',
    next='Refine the mouth and wall profile, then choreograph coil growth and the section reveal.',
    caveat='Early stylized shell form. Small sweep facets remain visible; anatomical realism is not the brief.'),
 'oculus-pavilion':dict(tag='Architectural contrast',promise='Arched passages, a roof oculus and a narrow tower make a compact spatial composition with only a few primitives.',
    next='Refine the proportions and give the interior a stronger focal element before developing a construction film.',
    caveat='The first full-scale version exceeded the cache frontier capacity. The selected version uses 0.82 workspace scale.'),
 'cutaway-manifold':dict(tag='Technical-demo first pick',promise='Bores and a quarter-section expose a simple shaft and rotor while leaving those internal groups intact.',
    next='Develop a clear casting → boring → section → assembly sequence, using the existing named history.',
    caveat='A fictional mechanical blockout, not an engineering simulation. The oversized chamber cut was corrected after first inspection.'),
}


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


TEMPLATE='''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Cubey SDF — Three authored content directions</title><style>
:root{--bg:#f1f2ec;--paper:#fffef9;--ink:#20382f;--muted:#5c6e63;--line:#d6ddd2;--green:#2b685b;--soft:#e0eadd}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.65 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1440px;width:calc(100% - 56px);margin:auto}header{padding:46px 0 32px}.eyebrow{font-size:.73rem;text-transform:uppercase;letter-spacing:.15em;font-weight:750;color:var(--muted)}h1{font-size:clamp(2.3rem,4vw,4rem);letter-spacing:-.045em;line-height:1.08;margin:14px 0 20px}h2{font-size:1.6rem;letter-spacing:-.025em;margin:14px 0 8px}h3{font-size:1rem;margin:22px 0 6px}p{margin:0 0 16px}.lead{max-width:900px;font-size:1.13rem;color:var(--muted)}.chips{display:flex;gap:9px;flex-wrap:wrap;margin-top:20px}.chip{font-size:.73rem;border:1px solid var(--line);border-radius:30px;padding:4px 11px;background:var(--paper)}.gallery{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:20px;align-items:start}.card{border:1px solid var(--line);border-radius:20px;background:var(--paper);overflow:hidden}.image-link{display:block;background:#c4dbdf}.card img{display:block;width:100%;aspect-ratio:4/3;object-fit:contain}.content{padding:22px}.tag{font-size:.73rem;text-transform:uppercase;letter-spacing:.08em;color:var(--green);font-weight:750}.views{display:flex;gap:6px;flex-wrap:wrap;margin:15px 0 10px}button{font:inherit;font-size:.8rem;border:1px solid var(--line);padding:6px 10px;border-radius:8px;background:var(--paper);color:var(--ink);cursor:pointer}button[aria-pressed=true]{background:var(--green);color:white;border-color:var(--green)}button:hover{border-color:var(--green)}:focus-visible{outline:3px solid #b67e2d;outline-offset:3px}.meta{font-size:.78rem;color:var(--muted)}.card p{font-size:.9rem}.small{font-size:.8rem;color:var(--muted)}a{color:var(--green);text-underline-offset:4px}.downloads{display:flex;gap:16px;font-size:.8rem;margin-top:18px}.command{display:block;background:#edf0e7;border-radius:9px;padding:12px;font-size:.72rem;line-height:1.5;overflow-wrap:anywhere;margin:10px 0}.discussion{display:grid;grid-template-columns:1fr 1fr;gap:32px;border-top:1px solid var(--line);margin-top:36px;padding:30px 0}.discussion h2{margin-top:0;font-size:1.4rem}.note{padding:20px 24px;background:var(--soft);border-radius:15px;margin:25px 0 35px}.note p:last-child{margin:0}footer{padding:24px 0 40px;border-top:1px solid var(--line);font-size:.78rem;color:var(--muted)}.proof{display:flex;gap:14px;flex-wrap:wrap;margin:20px 0}.proof a{font-size:.8rem}.view-label{min-height:23px;font-size:.75rem;color:var(--muted)}
@media(max-width:1050px){.gallery{grid-template-columns:1fr}.card{display:grid;grid-template-columns:1fr 1fr;align-items:start}.image-link{position:sticky;top:0}.content{padding:24px}.discussion{gap:22px}}@media(max-width:650px){.wrap{width:calc(100% - 28px)}header{padding-top:28px}.card{display:block}.image-link{position:static}.gallery{gap:24px}.content{padding:20px}.discussion{grid-template-columns:1fr}.lead{font-size:1rem}h1{font-size:2.5rem}}
@media print{button{display:none}.gallery{display:block}.card{display:grid;grid-template-columns:1fr 1fr;break-inside:avoid;margin-bottom:20px}.image-link{position:static}body{font-size:12px;background:white}}
</style></head><body><main class="wrap"><header><div class="eyebrow">Cubey / SDF · content audition · 3 November 2026</div><h1>Three new forms to try.</h1><p class="lead">Native blockouts made with the existing authored SDF vocabulary. The goal is a few distinctive graphics-demo subjects: organic curvature, architectural negative space and a mechanical cutaway.</p><div class="chips"><span class="chip">Existing Cached renderer · SS1</span><span class="chip">Real CSG cutaway views</span><span class="chip">Named construction history</span><span class="chip">Early visual review</span></div></header><section class="gallery" aria-label="Authored content candidates">__CARDS__</section><div class="note"><strong>My recommendation</strong><p>Take the <strong>spiral shell</strong> forward for the next sculptural hero image, or the <strong>manifold</strong> for the clearest scripted construction and cutaway experiment. The pavilion adds a useful architectural direction with a very small source graph. These are candidates for your visual judgment, not promoted final assets.</p></div><section class="discussion"><div><h2>What this audition establishes</h2><p>The six scene variants load in the current native application. Nine selected stills cover each intact object, its cutaway, and another angle on the cutaway. The renderer accepted their names, materials and grouped history with Vulkan validation required.</p><p class="small">The final shapes use 20–259 leaves. Rendering uses the existing field allocation and shader set. The existing robot, Guardian, vessel, arch and driftwood assets are untouched. All source and capture commands are retained.</p></div><div><h2>What I would try after selection</h2><p>Refine the chosen silhouette in a few fixed views, then develop its saved construction sequence or a deliberate section reveal. Keep playback, orbit, parameters and diagnostic controls focused on that graphics question.</p><p class="small">These are static content captures. The saved history is authored for later playback review; no construction film, incremental-edit performance, mechanical physics or general authoring UI was qualified here. View buttons switch captured images.</p></div></section><div class="proof"><a href="audition-data.json">Selected native capture receipts</a><a href="source-manifest.json">Source and asset fingerprints</a><a href="http://starship:8787/reports/86a38771f62a2ea2a958ddca9e1880265017826405a58c66c14d900ac5cd9136/">Previous SDF capability overview</a><a href="http://starship:8787/reports/#project=cubey">Cubey report catalog</a></div></main><footer class="wrap">Cubey graphics workshop · Authored scene inputs and native captures only. Small geometry refinements happened during the audition; runtime code and shaders were preserved.</footer><script type="application/json" id="data">__DATA__</script><script>
const data=JSON.parse(document.getElementById('data').textContent);
function show(subject,key){const record=data.subjects[subject].views[key],card=document.getElementById(subject);card.querySelector('img').src=record.asset;card.querySelector('img').alt=data.subjects[subject].title+' / '+record.caption;card.querySelector('.image-link').href=record.asset;card.querySelector('.meta').textContent=record.leaves+' primitive leaves · '+record.history_actions+' saved construction actions';card.querySelector('.view-label').textContent=record.caption;card.querySelector('.scene-download').href=record.scene_asset;card.querySelector('.preset-download').href=record.preset_asset;card.querySelector('.command').textContent=record.launch_command;card.querySelectorAll('button').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.view===key)));}
document.querySelectorAll('[data-subject]').forEach(b=>b.addEventListener('click',()=>show(b.dataset.subject,b.dataset.view)));
</script></body></html>'''


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    for name in ('assets','scenes','presets'): (output/name).mkdir()
    r2=json.loads((RAW/'round2/audition.json').read_text());r3=json.loads((RAW/'round3/audition.json').read_text())
    assert r2['complete'] and r3['complete'] and r2['runtime_unchanged'] and r3['runtime_unchanged']
    assert r2['binary_sha256']==r3['binary_sha256'] and r2['shaders']==r3['shaders']
    records=[r for r in r2['runs'] if r['subject']!='cutaway-manifold']+r3['runs'];assert len(records)==9
    data=dict(subjects={},binary_sha256=r2['binary_sha256'],generator_sha256=sha(__file__),
              shaders=r2['shaders'],runs=records,scope='Content audition; static native captures, not performance or visual acceptance.')
    assets=[]
    for row in records:
        subject=row['subject'];key='intact' if row['variant']=='intact' else ('side' if row['view']=='side' else 'section')
        caption=dict(intact='Intact object · native hero view',section='Authored cutaway · native hero view',side='Authored cutaway · alternate angle')[key]
        filename=Path(row['image']).name;dest='assets/'+filename;shutil.copy2(row['image'],output/dest)
        scene_asset='scenes/'+Path(row['scene']).name;preset_asset='presets/'+Path(row['preset']).name
        assert sha(row['scene_input'])==row['scene_sha256'] and sha(ROOT/row['scene'])==row['scene_sha256']
        shutil.copy2(row['scene_input'],output/scene_asset);shutil.copy2(ROOT/row['preset'],output/preset_asset)
        launch='rtk proxy build/release/projects/sdf/sdf --config '+row['preset']+' --render-mode cached'
        if key=='side':launch+=' --yaw 1.02 --pitch .28'
        item={**row,'asset':dest,'scene_asset':scene_asset,'preset_asset':preset_asset,'caption':caption,'launch_command':launch}
        data['subjects'].setdefault(subject,{**r2['subjects'][subject],**COPY[subject],'views':{}})['views'][key]=item
    cards=[]
    for subject,item in data['subjects'].items():
        first=item['views']['intact'];e=html.escape
        buttons=''.join(f'<button type="button" data-subject="{subject}" data-view="{key}" aria-pressed="{str(key=="intact").lower()}">{title}</button>' for key,title in [('intact','Intact'),('section','Cutaway'),('side','Another angle')])
        cards.append(f'''<article class="card" id="{subject}"><a class="image-link" href="{first['asset']}" aria-label="Open full-size {e(item['title'])} image"><img src="{first['asset']}" width="960" height="720" alt="{e(item['title'])} / intact object"></a><div class="content"><div class="tag">{e(item['tag'])}</div><h2>{e(item['title'])}</h2><p>{e(item['promise'])}</p><div class="views" role="group" aria-label="{e(item['title'])} captured views">{buttons}</div><div class="view-label">{first['caption']}</div><div class="meta">{first['leaves']} primitive leaves · {first['history_actions']} saved construction actions</div><h3>Graphics question</h3><p>{e(item['question'])}</p><h3>Possible demo</h3><p>{e(item['demo'])}</p><h3>Next refinement</h3><p>{e(item['next'])}</p><p class="small">{e(item['caveat'])}</p><div class="downloads"><a class="scene-download" href="{first['scene_asset']}" download>Scene JSON</a><a class="preset-download" href="{first['preset_asset']}" download>Native preset</a></div><code class="command">{e(first['launch_command'])}</code></div></article>''')
    encoded=json.dumps(data,separators=(',',':'),allow_nan=False).replace('</','<\\/')
    (output/'index.html').write_text(TEMPLATE.replace('__CARDS__',''.join(cards)).replace('__DATA__',encoded))
    (output/'audition-data.json').write_text(json.dumps(data,indent=2)+'\n')
    for f in sorted(output.rglob('*')):
        if f.is_file():assets.append(dict(path=str(f.relative_to(output)),sha256=sha(f),bytes=f.stat().st_size))
    (output/'source-manifest.json').write_text(json.dumps(dict(inputs=['round2/audition.json','round3/audition.json'],files=assets,complete=True),indent=2)+'\n')
    print(json.dumps(dict(output=str(output),subjects=len(data['subjects']),native_views=len(records),files=len(assets)+1)))


if __name__=='__main__':main()
