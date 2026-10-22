#!/usr/bin/env python3
"""Package the parameter-only marble frequency review from native receipts."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('comparison','history','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    read=lambda p:json.loads(p.read_text())
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    c=read(args.comparison/'manifest.json');h=read(args.history/'manifest.json')
    assert c['paired_settings_match'] and read(args.comparison/'intent-audit.json')['passed']
    for key in ('binary_sha256','shader_sha256','device'):
        assert c[key]==h[key],key
    assert h['validated_states']==[*range(66),32,0,65]
    assert h['final_matches_static_fingerprints'] and h['final_image_comparison']['changed']==0
    scenes={name:read(args.comparison/name/'source/scene.json') for name in c['versions']}
    assert set(scenes)=={'before','intermediate','after'}
    original=scenes['before']
    for name,scene in scenes.items():
        assert scene['root']==original['root'] and scene['history']==original['history']
        assert sha(args.comparison/name/'source/scene.json')==c['versions'][name]['scene_sha256']
        for run in c['versions'][name]['runs'].values():
            assert sha(args.comparison/run['capture'])==run['sha256']
        orbit=c['versions'][name]['orbit']
        assert orbit['frames']==240 and orbit['field_builds']==1
        assert sha(args.comparison/orbit['capture'])==orbit['sha256']
        assert read(args.comparison/name/'video-check.json')['passed']
    assert h['source_scene_sha256']==c['versions']['after']['scene_sha256']
    for run in h['runs'].values():
        assert sha(args.history/run['capture'])==run['capture_sha256']
    log=(args.history/'logs/history-validation.log').read_text()
    assert h['runs']['history-validation']['stats']==[s for s in log.splitlines() if s.startswith('sdf:')]
    for key in ('mismatches','exhausted','sampling_ambiguous','part_mismatches'):
        values=list(map(int,re.findall(key+r'=(\d+)',log)))
        assert values and not any(values),key
    stats=dict(states=66,checkpoints=len(h['validated_states']),
        rays=sum(map(int,re.findall(r'oracle rays=(\d+)',log))),
        points=sum(map(int,re.findall(r'CPU/GPU points=(\d+)',log))),
        max_color_error=max(map(float,re.findall(r'max_material_error=([\deE.+-]+)',log))))
    assert stats['max_color_error']<=.0002
    data=dict(views=c['views'],stats=stats,parameters={name:s['materials'][0]['procedural'] for name,s in scenes.items()},
        binary_sha256=c['binary_sha256'])
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    shutil.copytree(args.comparison,out/'comparison');shutil.copytree(args.history,out/'history')
    shutil.copy2(__file__,out/'build_report.py')
    (out/'review.json').write_text(json.dumps(data,indent=2)+'\n')
    html='''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cubey SDF — finer marble</title><link rel="icon" href="data:,">
<style>
:root{color-scheme:dark;--bg:#11191e;--panel:#1d2a30;--ink:#f0eee7;--muted:#bac6ca;--accent:#d9c796}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.65 system-ui,sans-serif}main{max-width:1500px;margin:auto;padding:40px 24px}h1,h2{font-family:Georgia,serif;line-height:1.1;letter-spacing:-.025em}h1{font-size:clamp(2.8rem,6vw,5rem);margin:8px 0 20px}h2{font-size:2rem}p{max-width:85ch}a{color:var(--accent);text-underline-offset:3px}.eyebrow{font-size:.8rem;text-transform:uppercase;letter-spacing:.15em;color:var(--accent)}.lead{font-size:1.12rem;color:var(--muted)}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:16px}figure{margin:0;background:var(--panel);border-radius:12px;overflow:hidden}img{width:100%;aspect-ratio:1;display:block}figcaption{padding:14px 16px}small,.note{color:var(--muted)}select{font:inherit;padding:9px 14px;background:var(--panel);color:var(--ink);border:1px solid #49606c;border-radius:8px;margin:8px 0 20px}section{border-top:1px solid #344b57;margin-top:36px;padding-top:14px}video{display:block;width:100%;aspect-ratio:4/3;background:var(--panel)}details{background:var(--panel);padding:18px;border-radius:10px}summary{cursor:pointer}table{border-collapse:collapse}td,th{text-align:left;padding:8px 18px 8px 0;border-bottom:1px solid #344b57}code{overflow-wrap:anywhere}a:focus-visible,select:focus-visible,summary:focus-visible{outline:3px solid var(--accent);outline-offset:4px}@media(max-width:850px){.grid{grid-template-columns:1fr}main{padding:24px 16px}}
</style><main>
<p class="eyebrow">Cubey / SDF / material detail</p><h1>Finer marble.</h1>
<p class="lead">More veins, smaller bends and finer grain on the same carved guardian. The revised preset uses the finest treatment below. The pale base and honed finish retain the original contrast; the geometry, lighting and camera match.</p>
<label for="view">Inspection view</label><br><select id="view"></select>
<div class="grid">
<figure><a id="before-link" href="comparison/before/captures/detail.png"><img id="before" src="comparison/before/captures/detail.png" alt="Original broad marble, face detail"></a><figcaption>Original<small><br>Broad veins · scale 4.5 / frequency 5.5</small></figcaption></figure>
<figure><a id="intermediate-link" href="comparison/intermediate/captures/detail.png"><img id="intermediate" src="comparison/intermediate/captures/detail.png" alt="Denser intermediate marble, face detail"></a><figcaption>Denser trial<small><br>4× axial vein frequency · scale 9 / frequency 11</small></figcaption></figure>
<figure><a id="after-link" href="comparison/after/captures/detail.png"><img id="after" src="comparison/after/captures/detail.png" alt="Revised finer marble, face detail"></a><figcaption>Revised preset<small><br>5.8× axial vein frequency · scale 12 / frequency 12</small></figcaption></figure>
</div>
<p class="note">Frequency ratios compare scale × vein frequency along the volume's vein direction. Turbulence adds local variation, so these are not uniform line-count multipliers. Click any image for its native 960-pixel capture.</p>
<section><h2>Turn it in the light.</h2><p>Three native eight-second orbits, with the same camera motion and shared sky. Each builds the distance field once. Fine grain is filtered as the surface recedes.</p>
<div class="grid">
<figure><video controls muted playsinline preload="metadata" poster="comparison/before/captures/hero.png" src="comparison/before/native.mp4"></video><figcaption>Original · <a href="comparison/before/native.mp4">Download</a></figcaption></figure>
<figure><video controls muted playsinline preload="metadata" poster="comparison/intermediate/captures/hero.png" src="comparison/intermediate/native.mp4"></video><figcaption>Denser trial · <a href="comparison/intermediate/native.mp4">Download</a></figcaption></figure>
<figure><video controls muted playsinline preload="metadata" poster="comparison/after/captures/hero.png" src="comparison/after/native.mp4"></video><figcaption>Revised preset · <a href="comparison/after/native.mp4">Download</a></figcaption></figure>
</div></section>
<section><h2>A surface revision.</h2><p>The material's overall scale rises from 4.5 to 12 and vein frequency from 5.5 to 12. Narrower veins and stronger warping add small bends; grain scale rises from 220 to 360 while bump amplitude stays at 0.00008 model units. Base colors, roughness amounts, seed, vein direction and offset stay the same. Limestone is unchanged.</p>
<p>No shader or renderer changes were needed. All 1,011 primitives and 65 authoring operations are identical. Cuts expose the same fixed model-space pattern volume.</p>
<details><summary>Qualification and source evidence</summary>
<p id="qualification"></p><p>All three variants share the same native binary, shaders and GPU; matching render settings are checked by the capture driver. All 720 orbit frames decode and pass screening. Static/replay PNGs of the revised material match exactly. This pass requalifies appearance and history; it does not claim a new performance benchmark or another full construction film.</p>
<p>Native images and extracted orbit frames were visually inspected. Browser interaction review was unavailable because no browser was connected. The material remains a stylized procedural approximation.</p>
<p><a href="comparison/manifest.json">Comparison receipt</a> · <a href="history/manifest.json">Full native history receipt</a> · <a href="comparison/intent-audit.json">Structural probes</a> · <a href="comparison/after/source/scene.json">Revised scene</a> · <a href="comparison/authoring/author_guardian_materials.py">Generator</a> · <a href="review.json">Review data</a> · <a href="build_report.py">Report builder</a></p>
</details></section>
<p><a href="http://starship:8787/reports/65b3ef76be120d14f259412977d3e50fa6e59e435298cd415ece6edb84d1ec87/">Previous limestone and marble study, including the original carving film</a> · <a href="http://starship:8787/reports/#project=cubey">Cubey reports</a></p>
</main><script>
const data=__DATA__,$=id=>document.getElementById(id);
$('view').replaceChildren(...data.views.map(v=>new Option(v.label,v.name)));$('view').value='detail';
function show(){const name=$('view').value,label=data.views.find(v=>v.name===name).label;for(const version of ['before','intermediate','after']){const path=`comparison/${version}/captures/${name}.png`;$(version).src=path;$(version).alt=`${version} marble: ${label}`;$(version+'-link').href=path;}}
$('view').addEventListener('change',show);show();
$('qualification').textContent=`All ${data.stats.states} revised-material states / ${data.stats.checkpoints} checkpoints pass: ${data.stats.rays.toLocaleString()} sampled-field rays and ${data.stats.points.toLocaleString()} direct CPU/GPU points, with zero mismatches, exhausted rays, sampling ambiguities or part disagreements. Maximum CPU/GPU color error is ${data.stats.max_color_error}, within the existing 0.0002 linear-value procedural bound.`;
</script></html>'''
    (out/'index.html').write_text(html.replace('__DATA__',json.dumps(data).replace('</','<\\/')))
    assets={str(p.relative_to(out)):dict(bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()}
    (out/'assets.json').write_text(json.dumps(assets,indent=2)+'\n')
    size=sum(p.stat().st_size for p in out.rglob('*') if p.is_file());assert size<=100*1024*1024
    print(json.dumps(dict(output=str(out),bytes=size,files=len(assets)+1,qualification=stats)))


if __name__=='__main__':main()
