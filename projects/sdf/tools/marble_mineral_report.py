#!/usr/bin/env python3
"""Package a coherent marble color review from matched native receipts."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('comparison','previous','foundation','profile','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--swatch',type=Path,help='Current native material-swatch receipt')
    parser.add_argument('--previous-swatch',type=Path,help='Swatch captured before changing evaluation')
    args=parser.parse_args();root=Path(__file__).resolve().parents[3]
    if bool(args.swatch)!=bool(args.previous_swatch):
        parser.error('--swatch and --previous-swatch must be provided together')
    regions=bool(args.swatch)
    read=lambda p:json.loads(p.read_text())
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    c=read(args.comparison/'manifest.json');old=read(args.previous/'manifest.json')
    f=read(args.foundation/'manifest.json');p=read(args.profile/'manifest.json')
    assert f['passed'] and c['paired_settings_match'] and old['paired_settings_match']
    assert c['device']==old['device']==p['device']
    for key in ('binary_sha256','shader_sha256'):assert c[key]==f[key]==p[key]
    assert read(args.comparison/'intent-audit.json')['passed']
    names={'before','subtle','after'};assert set(c['versions'])==set(old['versions'])==names
    common=[v for v in c['views'] if v in old['views']]
    assert {v['name'] for v in common}=={v['name'] for v in old['views']}
    def settings(path):
        d=read(path)
        for key in ('authored-scene','output','validate-field'):d.pop(key,None)
        return d
    original=read(args.comparison/'subtle/source/scene.json')
    for directory,receipt in ((args.comparison,c),(args.previous,old)):
        for name,row in receipt['versions'].items():
            scene=read(directory/name/'source/scene.json')
            assert scene['root']==original['root'] and scene['history']==original['history']
            assert sha(directory/name/'source/scene.json')==row['scene_sha256']
            for run in row['runs'].values():assert sha(directory/run['capture'])==run['sha256']
            orbit=row['orbit'];assert orbit['frames']==240 and orbit['field_builds']==1
            assert sha(directory/orbit['capture'])==orbit['sha256']
            assert read(directory/name/'video-check.json')['passed']
    for name in names:
        assert c['versions'][name]['scene_sha256']==old['versions'][name]['scene_sha256']
        assert p['runs'][name]['scene_sha256']==c['versions'][name]['scene_sha256']
        for view in common:
            stem=view['name']
            assert settings(args.comparison/name/f'configs/{stem}.json')==settings(args.previous/name/f'configs/{stem}.json')
            if name=='before':
                assert (args.comparison/name/f'captures/{stem}.png').read_bytes()==(args.previous/name/f'captures/{stem}.png').read_bytes()
        assert (args.comparison/name/'captures/clay.png').read_bytes()==(args.comparison/'before/captures/clay.png').read_bytes()
    assert c['versions']['subtle']['scene_sha256']==f['source_scene_sha256']['marble']
    for run in f['runs'].values():assert sha(args.foundation/run['capture'])==run['sha256']
    assert f['checks']['marble_colored_details_max_amount_passed']==['original','balanced','denser','fine']
    max_color=max(float(value) for run in f['runs'].values() for line in run['stats']
                  for value in re.findall(r'max_material_error=([\deE.+-]+)',line))
    assert max_color<=.0002
    data=dict(views=common,current_views=c['views'],binary_sha256=c['binary_sha256'],
        shader_sha256=c['shader_sha256'],foundation_runs=len(f['runs']),max_color_error=max_color,
        checks=f['checks'],performance=p['runs'],source_commit=c['source_commit'])
    if regions:
        s=read(args.swatch/'manifest.json');previous_s=read(args.previous_swatch/'manifest.json')
        for key in ('binary_sha256','shader_sha256','device'):
            assert s[key]==c[key] and previous_s[key]==old[key]
        assert s['paired_settings_match'] and previous_s['paired_settings_match']
        assert s['views']==previous_s['views']
        for directory,receipt in ((args.swatch,s),(args.previous_swatch,previous_s)):
            assert set(receipt['versions'])==names
            for name,row in receipt['versions'].items():
                assert sha(directory/name/'source/scene.json')==row['scene_sha256']
                for run in row['runs'].values():assert sha(directory/run['capture'])==run['sha256']
        for name in names:
            assert s['versions'][name]['scene_sha256']==previous_s['versions'][name]['scene_sha256']
            for view in s['views']:
                stem=view['name']
                assert settings(args.swatch/name/f'configs/{stem}.json')==settings(args.previous_swatch/name/f'configs/{stem}.json')
                if name=='before':
                    assert (args.swatch/name/f'captures/{stem}.png').read_bytes()==(args.previous_swatch/name/f'captures/{stem}.png').read_bytes()
        assert s['versions']['after']['runs']['unlit']['sha256']!=previous_s['versions']['after']['runs']['unlit']['sha256']
        data['swatch_views']=s['views']
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    for source,target in ((args.comparison,'comparison'),(args.previous,'previous'),
                          (args.foundation,'foundation'),(args.profile,'profile')):
        shutil.copytree(source,out/target)
    if regions:
        shutil.copytree(args.swatch,out/'swatch');shutil.copytree(args.previous_swatch,out/'previous-swatch')
    implementation=out/'implementation';implementation.mkdir()
    for source in ('projects/sdf/sdf_material.cpp','projects/sdf/sdf_material_tests.cpp',
                   'projects/sdf/shaders/surface_pattern.glsl','projects/sdf/tools/qualify_materials.py'):
        shutil.copy2(root/source,implementation/Path(source).name)
    (implementation/'hashes.json').write_text(json.dumps({f.name:sha(f) for f in sorted(implementation.iterdir())},indent=2)+'\n')
    plan='sdf-marble-regions-plan.md' if regions else 'sdf-marble-minerals-plan.md'
    shutil.copy2(root/'docs/notes'/plan,out/'plan.md')
    shutil.copy2(__file__,out/'build_report.py')
    (out/'review.json').write_text(json.dumps(data,indent=2)+'\n')
    neutral=p['runs']['before']['gpu_frame'];colored=p['runs']['subtle']['gpu_frame']
    timings=f'{neutral["median_ms"]:.3f} / {colored["median_ms"]:.3f}'
    figures=''.join(f'<figure><a href="comparison/{name}/captures/detail-base-color.png"><img src="comparison/{name}/captures/detail-base-color.png" alt="{label}, current unlit face material"></a><figcaption>{label}</figcaption></figure>'
        for name,label in [('before','Original pale material · 0'),('subtle','Current default · 0.08'),('after','Current stronger trial · 0.16')])
    html='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cubey SDF — coherent marble color</title><link rel="icon" href="data:,"><style>
:root{color-scheme:dark;--bg:#11191e;--panel:#1d2a30;--ink:#f0eee7;--muted:#bac6ca;--accent:#d9c796}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.65 system-ui,sans-serif}main{max-width:1400px;margin:auto;padding:40px 24px}h1,h2{font-family:Georgia,serif;line-height:1.1;letter-spacing:-.025em}h1{font-size:clamp(2.8rem,6vw,5rem);margin:8px 0 20px}h2{font-size:2rem}p{max-width:85ch}a{color:var(--accent);text-underline-offset:3px}.eyebrow{font-size:.8rem;text-transform:uppercase;letter-spacing:.15em;color:var(--accent)}.lead,.note{color:var(--muted)}.lead{font-size:1.12rem}.pair,.trio{display:grid;gap:16px}.pair{grid-template-columns:repeat(2,minmax(0,1fr))}.trio{grid-template-columns:repeat(3,minmax(0,1fr))}figure{margin:0;background:var(--panel);border-radius:12px;overflow:hidden}img{width:100%;aspect-ratio:1;display:block}figcaption{padding:14px 16px}.controls{display:flex;flex-wrap:wrap;gap:16px;align-items:end;margin-bottom:20px}label{display:block}select,button{font:inherit;padding:9px 14px;background:var(--panel);color:var(--ink);border:1px solid #49606c;border-radius:8px}button{cursor:pointer}section{border-top:1px solid #344b57;margin-top:36px;padding-top:14px}video{display:block;width:100%;aspect-ratio:4/3;background:var(--panel)}details{background:var(--panel);padding:18px;border-radius:10px;margin-top:32px}summary{cursor:pointer}a:focus-visible,select:focus-visible,button:focus-visible,summary:focus-visible{outline:3px solid var(--accent);outline-offset:4px}@media(max-width:800px){.pair,.trio{grid-template-columns:1fr}main{padding:24px 16px}}
</style><main><p class="eyebrow">Cubey / SDF / marble mineral structure</p><h1>Color follows the veins.</h1>
<p class="lead">Warm cores, cream edges and occasional wider pockets now come from the existing marbling. The material keeps pale intervals and gray-only veins. Noise selects deposits and breaks up their edges.</p>
<div class="controls"><div><label for="amount">Matching color amount</label><select id="amount"><option value="subtle">Default · 0.08</option><option value="after" selected>Stronger trial · 0.16</option><option value="before">Neutral · 0</option></select></div><div><label for="view">Matching native view</label><select id="view"></select></div></div>
<div class="pair"><figure><a id="old-link" href="previous/after/captures/detail.png"><img id="old" src="previous/after/captures/detail.png" alt="Previous independent cream pockets, stronger face detail"></a><figcaption>Previous · pockets biased toward veins</figcaption></figure><figure><a id="new-link" href="comparison/after/captures/detail.png"><img id="new" src="comparison/after/captures/detail.png" alt="Current connected mineral color, stronger face detail"></a><figcaption>Current · colored cores and attached cream edges</figcaption></figure></div>
<p class="note">Same scene parameters, amount, camera, lighting and geometry. The placement algorithm changes. Base color removes lighting; Clay checks the sculpt. Zero variation is pixel-identical across revisions. Click either image for the native capture.</p>
<section><h2>One mineral structure.</h2><p>The main warped vein coordinate places both colored cores and surrounding cream. Noise selects warm segments, widens some edges into pockets and varies their boundaries. Existing thin secondary veins carry some warmth too. Dark cores can take color, without the former suppression factor.</p>
<p>The original pale/cream multipliers, Balanced vein paths, detail presets and 0–0.25 amount control retain their values. The saved default is 0.08. No additional material parameter or UI control is introduced.</p>
<p>Reference direction: <a href="https://www.antolini.com/en/c6/natural-stone-collection/m1556/calacatta-cremo">Calacatta Cremo</a> shows warm broken streaks; <a href="https://www.antolini.com/en/c3/exclusive-collection/m1559/calacatta-macchia-vecchia-original">Macchia Vecchia Original</a> shows vein edges widening into irregular pockets. These are observations of selected natural-marble photographs. Our interpretation shares structure between color and veins; it is a stylized approximation, not a geological simulation or a measured reproduction.</p></section>
<section><h2>The current material, without lighting.</h2><div class="trio">__FIGURES__</div><p class="note">Current revision only, with the same close-up camera at all three amounts. This extra face-material view was added for this study; the old/new selector above uses views available in both receipts.</p></section>
<section><h2>In motion.</h2><p>These eight-second native orbits follow the amount selection above. Each builds the field once. The paired play button resets both videos; each video also has its own playback controls.</p><button id="play">Play both from the start</button><span id="play-status" role="status"></span>
<div class="pair"><figure><video id="old-film" controls muted playsinline preload="metadata" poster="previous/after/captures/hero.png" src="previous/after/native.mp4"></video><figcaption>Previous · <a id="old-download" href="previous/after/native.mp4">Download</a></figcaption></figure><figure><video id="new-film" controls muted playsinline preload="metadata" poster="comparison/after/captures/hero.png" src="comparison/after/native.mp4"></video><figcaption>Current · <a id="new-download" href="comparison/after/native.mp4">Download</a></figcaption></figure></div></section>
<section><h2>Review and limits.</h2><p>The first native trial established the color/vein relationship but its warm edges were too smooth. A finer, filtered noise scale now varies those edges, and the existing secondary wisps carry more warmth. Default and stronger shaded/unlit views and the rear were inspected. The underlying marbling remains smooth and contour-like in places; this pass preserves the user's accepted density and flow. Human artistic acceptance remains pending.</p>
<p>Color remains a continuous fixed model-space volume through the sculpt and cuts. Geometry, all 65 operations, roughness, bump, shadows and sky retain their settings. Analytic gradients broaden narrow color boundaries with pixel footprint; unresolved warm structure fades away. This pass adds no appearance volume or distance-field rebuild.</p></section>
<details><summary>Native qualification and measured scope</summary><p>Five focused CPU suites pass. A controlled unwarped field checks that warm and gray-only vein cores both occur and that samples well between both vein families retain the pale material. Filtering tests retain finite, bounded, cream-only changes without affecting roughness or height gradients.</p>
<p>__RUNS__ native foundation render/validation runs pass. They cover CPU/GPU agreement, deterministic seeds, neutral/plain preservation, shortcut/full equality, persistence, ownership, short cut histories and one-build appearance edits. All four named detail presets also pass at maximum color amount with another seed. Maximum observed CPU/GPU color error is __ERROR__, within the unchanged 0.0002 bound; geometry/gradient/normal bounds remain 0.00002.</p>
<p>Eighteen current stills, eight structural probes and all 720 current orbit frames pass. Old captures keep their own binary receipt; equal settings and scene hashes are checked across revisions. Clay images are exact across current amounts. The complete guardian history and unchanged desktop controls were not rerun. Browser discovery found no connected browser, so browser interaction review was not performed.</p>
<p>A fresh matched RTX 5070 Ti profile uses 1920×1080, supersample 1, hard shadows, 180 submitted frames / 30 warmup and 148 completed shading-plus-post samples per case. Variation 0 / 0.08 medians are __TIMINGS__ ms. Field builds, readback and encoding are excluded. This compares tint off/on within the revised build, not old/new shader performance.</p>
<p><a href="comparison/manifest.json">Current views and films</a> · <a href="previous/manifest.json">Previous views and films</a> · <a href="foundation/manifest.json">Native foundation</a> · <a href="profile/manifest.json">GPU measurements</a> · <a href="implementation/hashes.json">Implementation snapshots</a> · <a href="comparison/subtle/source/scene.json">Current scene</a> · <a href="plan.md">Plan and acceptance</a> · <a href="review.json">Review data</a> · <a href="build_report.py">Builder</a></p></details>
<p><a href="http://starship:8787/reports/2fe7ca71500f4f1a4318deafb4429b80d10cb192f38a5776a1e1414520b4eb96/">Previous mineral-pocket review</a> · <a href="http://starship:8787/reports/#project=cubey">Cubey reports</a></p>
</main><script>const data=__DATA__,$=id=>document.getElementById(id);
$('view').replaceChildren(...data.views.map(v=>new Option(v.label,v.name)));$('view').value='detail';
function showImages(){const version=$('amount').value,view=$('view').value,label=data.views.find(v=>v.name===view).label;for(const [id,folder] of [['old','previous'],['new','comparison']]){const path=`${folder}/${version}/captures/${view}.png`;$(id).src=path;$(id).alt=`${id==='old'?'Previous':'Current'} marble, ${version}, ${label}`;$(id+'-link').href=path;}}
function showFilms(){for(const [id,folder] of [['old','previous'],['new','comparison']]){const film=$(id+'-film'),version=$('amount').value,path=`${folder}/${version}/native.mp4`;film.pause();film.poster=`${folder}/${version}/captures/hero.png`;film.src=path;$(id+'-download').href=path;}$('play-status').textContent='';}
$('view').addEventListener('change',showImages);$('amount').addEventListener('change',()=>{showImages();showFilms();});
$('play').addEventListener('click',async()=>{try{const films=[$('old-film'),$('new-film')];for(const film of films){film.pause();film.currentTime=0;}await Promise.all(films.map(film=>film.play()));$('play-status').textContent='';}catch{$('play-status').textContent=' Use the individual video controls to play.';}});showImages();
</script></html>'''
    for key,value in dict(FIGURES=figures,RUNS=str(len(f['runs'])),ERROR=str(max_color),
                          TIMINGS=timings,DATA=json.dumps(data).replace('</','<\\/')).items():
        html=html.replace('__'+key+'__',value)
    if regions:
        changes={
            'Cubey SDF — coherent marble color':'Cubey SDF — marble mineral regions',
            'Color follows the veins.':'Color beyond the veins.',
            'Warm cores, cream edges and occasional wider pockets now come from the existing marbling. The material keeps pale intervals and gray-only veins. Noise selects deposits and breaks up their edges.':
                'Cream regions now have their own boundaries and internal streaks, sharing a broad flow with the marbling. Narrow veins can cross pale and cream stone. The study starts with a flat native SDF slab, then checks the same material on the guardian.',
            'Previous · pockets biased toward veins':'Previous · warmth confined to vein paths',
            'Current · colored cores and attached cream edges':'Current · partially correlated mineral regions',
            'Previous independent cream pockets, stronger face detail':'Previous vein-bound color, stronger face detail',
            'Current connected mineral color, stronger face detail':'Current mineral regions, stronger face detail',
            'One mineral structure.':'Shared flow, different boundaries.',
            'The main warped vein coordinate places both colored cores and surrounding cream. Noise selects warm segments, widens some edges into pockets and varies their boundaries. Existing thin secondary veins carry some warmth too. Dark cores can take color, without the former suppression factor.':
                'Independent seeded fields place the cream regions and their finer directional streaks. They share the existing large-scale deformation with the veins. Some warm seams remain, while color also occurs away from either narrow vein family. Region gradients include the deformation and anisotropic scaling; pixel footprints filter the spatial scales and boundaries.',
            'The first native trial established the color/vein relationship but its warm edges were too smooth. A finer, filtered noise scale now varies those edges, and the existing secondary wisps carry more warmth. Default and stronger shaded/unlit views and the rear were inspected. The underlying marbling remains smooth and contour-like in places; this pass preserves the user\'s accepted density and flow. Human artistic acceptance remains pending.':
                'The first swatch trial was too broad and soft. The second used smaller, more defined regions; the selected third trial adds finer directional interior streaks. Swatch views, default/stronger guardian views and motion frames were inspected. The smooth, closed vein topology remains a visible limitation. This is a stylized trial based on reference relationships, not a measured reproduction; human artistic acceptance is pending.',
            'A controlled unwarped field checks that warm and gray-only vein cores both occur and that samples well between both vein families retain the pale material.':
                'The revised controlled field requires both warm and pale samples away from the narrow main and secondary vein paths. Warm and gray-only core samples still occur. The previous requirement that every inter-vein sample stay pale has been removed.',
            'Eighteen current stills, eight structural probes and all 720 current orbit frames pass.':
                'Eighteen current guardian stills, twelve current swatch stills, eight structural probes and all 720 current orbit frames pass. All ten neutral guardian/swatch images match their older counterparts exactly.',
            'shows vein edges widening into irregular pockets':'shows broad cream regions and isolated flecks alongside narrow veins',
            'http://starship:8787/reports/2fe7ca71500f4f1a4318deafb4429b80d10cb192f38a5776a1e1414520b4eb96/':
                'http://starship:8787/reports/0feff6f5a8207ee1f3ed7b6933cbb434b55ff191e8365f248e414699972978ec/',
            'Previous mineral-pocket review':'Previous vein-bound color review',
        }
        for before,after in changes.items():
            assert before in html,before
            html=html.replace(before,after)
        swatches='''<section><h2>Start with a flat material.</h2>
<figure style="max-width:800px;margin-bottom:24px"><a href="https://www.antolini.com/en/c3/exclusive-collection/m1559/calacatta-macchia-vecchia-original"><img style="aspect-ratio:3/2;object-fit:contain" src="https://www.antolini.com/image/imgfull1-calacatta-macchia-vecchia-originalcu263.jpg" alt="Antolini Macchia Vecchia close-up with thin dark lines, broader pale-gold regions and isolated flecks"></a><figcaption>Selected reference close-up · photograph hosted by <a href="https://www.antolini.com/en/c3/exclusive-collection/m1559/calacatta-macchia-vecchia-original">Antolini</a>. Compare the wider cream regions, thin lines and small flecks. This guides shape relationships; it is not a calibrated color or physical-scale fit.</figcaption></figure>
<label for="swatch-view">Native swatch view</label><select id="swatch-view"></select>
<div class="pair"><figure><a id="old-swatch-link" href="previous-swatch/after/captures/unlit.png"><img id="old-swatch" src="previous-swatch/after/captures/unlit.png" alt="Previous vein-bound marble on a flat slab"></a><figcaption>Before · native SDF slab</figcaption></figure><figure><a id="new-swatch-link" href="swatch/after/captures/unlit.png"><img id="new-swatch" src="swatch/after/captures/unlit.png" alt="Current mineral regions on the same flat slab"></a><figcaption>Current · same coordinates, camera and amount</figcaption></figure></div>
<p class="note">The amount selector above controls both comparisons. Unlit removes lighting; close and distant views inspect the same volume at different footprints. All four zero-amount swatch PNGs match the old renderer exactly. These are native slab captures, with no image maps.</p>
<p><a href="swatch/manifest.json">Current swatch receipt</a> · <a href="previous-swatch/manifest.json">Before-change swatch receipt</a> · <a href="swatch/authoring/capture_marble_swatch.py">Swatch capture driver</a></p></section><h2>On the guardian.</h2>'''
        html=html.replace('<select id="view"></select></div></div>', '<select id="view"></select></div></div>'+swatches)
        extra="""function showSwatches(){const version=$('amount').value,view=$('swatch-view').value;for(const [id,folder] of [['old','previous-swatch'],['new','swatch']]){const path=`${folder}/${version}/captures/${view}.png`;$(id+'-swatch').src=path;$(id+'-swatch').alt=`${id} marble swatch, ${version}, ${view}`;$(id+'-swatch-link').href=path;}}
$('swatch-view').replaceChildren(...data.swatch_views.map(v=>new Option(v.label,v.name)));$('swatch-view').value='unlit';$('swatch-view').addEventListener('change',showSwatches);$('amount').addEventListener('change',showSwatches);showSwatches();"""
        html=html.replace('</script>',extra+'</script>')
    (out/'index.html').write_text(html)
    assets={str(f.relative_to(out)):dict(bytes=f.stat().st_size,sha256=sha(f)) for f in sorted(out.rglob('*')) if f.is_file()}
    (out/'assets.json').write_text(json.dumps(assets,indent=2)+'\n')
    size=sum(f.stat().st_size for f in out.rglob('*') if f.is_file());assert size<=100*1024*1024
    print(json.dumps(dict(output=str(out),files=len(assets)+1,bytes=size)))


if __name__=='__main__':main()
