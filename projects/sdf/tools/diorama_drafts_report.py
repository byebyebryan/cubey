#!/usr/bin/env python3
"""Package the selected native diorama views as a visual draft sheet."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[3]
RAW = ROOT/'outputs/sdf/diorama-drafts-20261103/round6'
COPY = {
    'canal-mill': dict(character='A little working place',
        composition='The tall, warm-roofed mill balances the large wheel and low bridge. The canal gives the base a purpose and leads the eye across the scene.',
        next='Explore a small loft or side extension, richer wheel joinery, individual roof tiles and more believable delivery clutter under the awning.',
        rough='The water and current marks are static shapes. Wheel, bridge and roof detailing are still simplified.'),
    'hill-observatory': dict(character='A compact scientific outpost',
        composition='The split dome is the main landmark. A smaller keeper annex, the stair and the instruments create a route from the base up to the telescope.',
        next='Replace the blocky cliff with more irregular rock and retaining walls; develop dome segmentation, the keeper room and the telescope mounting.',
        rough='The rock base is especially schematic. The dome and instruments are posed geometry, with no moving mechanism yet.'),
    'terraced-courtyard': dict(character='An architectural miniature',
        composition='The arcade forms a sheltered back edge; the fountain anchors the open square. The external stair carries the composition upward to the tower.',
        next='Extend the side wing and terraces, add garden alcoves, vary the masonry and roof tiles, and develop a more atmospheric lighting setup.',
        rough='Masonry, paving and planting are regular and clean. The fountain water is a static graphic treatment.'),
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    for name in ('assets', 'scenes', 'presets'):
        (output/name).mkdir()
    receipt = json.loads((RAW/'drafts.json').read_text())
    assert receipt['complete'] and receipt['runtime_unchanged'] and len(receipt['runs']) == 6
    data = dict(scope='Three native architectural drafts; static stills, awaiting visual selection.',
                capture_receipt_sha256=sha(RAW/'drafts.json'), report_generator_sha256=sha(__file__),
                binary_sha256=receipt['binary_sha256'], shaders=receipt['shaders'], subjects={})
    e = html.escape
    for run in receipt['runs']:
        subject = run['subject']
        assert sha(run['image']) == run['image_sha256']
        assert sha(run['scene_input']) == run['scene_sha256'] == sha(ROOT/run['scene'])
        assert sha(run['config_input']) == run['config_sha256']
        asset = 'assets/'+Path(run['image']).name
        scene = 'scenes/'+Path(run['scene']).name
        preset = 'presets/'+Path(run['preset']).name
        shutil.copy2(run['image'], output/asset)
        shutil.copy2(run['scene_input'], output/scene)
        shutil.copy2(ROOT/run['preset'], output/preset)
        entry = data['subjects'].setdefault(subject, {**receipt['concepts'][subject], **COPY[subject], 'views': {}})
        launch = 'rtk proxy build/release/projects/sdf/sdf --config '+run['preset']+' --render-mode balanced --supersample 2'
        if run['view'] == 'alternate':
            launch += ' --yaw -.62 --pitch .48'
        entry['views'][run['view']] = dict(**run, asset=asset, scene_asset=scene, preset_asset=preset, launch=launch)
    nav, sections = [], []
    for number, (subject, entry) in enumerate(data['subjects'].items(), 1):
        first, alternate = entry['views']['hero'], entry['views']['alternate']
        title = e(entry['title'])
        nav.append(f'<a href="#{subject}"><img src="{first["asset"]}" width="1280" height="960" alt=""><span><b>0{number} / {title}</b><small>{e(entry["character"])}</small></span></a>')
        sections.append(f'''<section class="draft" id="{subject}">
<div class="section-head"><div><p class="eyebrow">Draft 0{number} / {e(entry['character'])}</p><h2>{title}</h2></div><p class="idea">{e(entry['idea'])}</p></div>
<figure><a class="full-image" href="{first['asset']}" aria-label="Open full-size {title}"><img class="main-image" src="{first['asset']}" width="1280" height="960" alt="{title}, native three-quarter view" loading="lazy"></a><figcaption><div class="views" role="group" aria-label="{title} views"><button type="button" data-subject="{subject}" data-view="hero" aria-pressed="true">Main view</button><button type="button" data-subject="{subject}" data-view="alternate" aria-pressed="false">Other side</button></div><span class="view-label">Native render · main view</span><a class="raw-link" href="{alternate['asset']}">Open other view</a></figcaption></figure>
<div class="notes"><div><h3>Composition</h3><p>{e(entry['composition'])}</p><h3>Details in this draft</h3><p>{e(entry['details'])}</p></div><div><h3>Where to take it</h3><p>{e(entry['next'])}</p><p class="rough">{e(entry['rough'])}</p></div></div>
<details><summary>Native scene and reproduction</summary><p>Existing Balanced renderer, 2× supersampling, 1280×960. {first['leaves']} primitive leaves. Named grouped history is saved, but construction playback was not captured.</p><p><a href="{first['scene_asset']}" download>Scene JSON</a> · <a href="{first['preset_asset']}" download>Native preset</a></p><code class="launch">{e(first['launch'])}</code></details>
</section>''')
    template = (Path(__file__).with_suffix('.html')).read_text()
    encoded = json.dumps(data, separators=(',', ':'), allow_nan=False).replace('</', '<\\/')
    page = template.replace('__NAV__', ''.join(nav)).replace('__DRAFTS__', ''.join(sections)).replace('__DATA__', encoded)
    (output/'index.html').write_text(page)
    (output/'draft-data.json').write_text(json.dumps(data, indent=2)+'\n')
    files = [dict(path=str(p.relative_to(output)), sha256=sha(p), bytes=p.stat().st_size)
             for p in sorted(output.rglob('*')) if p.is_file()]
    (output/'source-manifest.json').write_text(json.dumps(dict(files=files, complete=True), indent=2)+'\n')
    print(json.dumps(dict(output=str(output),subjects=len(data['subjects']),native_views=len(receipt['runs']),files=len(files)+1)))


if __name__ == '__main__':
    main()
