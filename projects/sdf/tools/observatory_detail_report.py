#!/usr/bin/env python3
"""Visual review of the selected observatory's three native detail passes."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[3]
RAW = ROOT/'outputs/sdf/observatory-detail-20261103/final-v2'
BASELINE = ROOT/'outputs/sdf/diorama-drafts-20261103/round6'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    for folder in ('assets', 'scenes', 'presets'):
        (out/folder).mkdir()
    capture = json.loads((RAW/'captures.json').read_text())
    assert capture['complete'] and capture['runtime_unchanged'] and len(capture['runs'])==8
    data = dict(scope='Selected observatory; three authored detail passes and a static local CSG section.',
                capture_receipt_sha256=sha(RAW/'captures.json'), generator_sha256=sha(__file__), views={})
    labels = {'site-hero': 'Pass 1 — site and architecture',
              'instrument-hero': 'Pass 2 — dome and telescope',
              'furnished-hero': 'Pass 3 — furnished exterior',
              'furnished-alternate': 'Exterior — other side',
              'furnished-dome-close': 'Dome and telescope — close-up',
              'section-hero': 'Architectural cutaway',
              'section-alternate': 'Cutaway — other side',
              'section-interior-close': 'Interior — close-up'}
    for run in capture['runs']:
        key = run['variant']+'-'+run['view']
        assert sha(run['image'])==run['image_sha256']
        assert sha(run['scene_input'])==run['scene_sha256']==sha(ROOT/run['scene'])
        assert sha(run['config_input'])==run['config_sha256']
        asset = 'assets/'+Path(run['image']).name
        scene = 'scenes/'+Path(run['scene']).name
        preset = 'presets/'+Path(run['preset']).name
        shutil.copy2(run['image'], out/asset)
        shutil.copy2(run['scene_input'], out/scene)
        shutil.copy2(ROOT/run['preset'], out/preset)
        data['views'][key] = dict(**run, label=labels[key], asset=asset, scene_asset=scene, preset_asset=preset)
    previous = json.loads((BASELINE/'drafts.json').read_text())
    baseline = next(r for r in previous['runs'] if r['subject']=='hill-observatory' and r['view']=='hero')
    assert sha(baseline['image'])==baseline['image_sha256']
    shutil.copy2(baseline['image'], out/'assets/original-draft.png')
    data['views']['original'] = dict(**baseline, label='Original selected draft', asset='assets/original-draft.png')
    e = html.escape
    sections = []
    groups = [
        ('exterior', 'A more developed little observatory.', 'The selected silhouette remains: a split dome, a keeper annex and a stepped approach. The detail now carries through the masonry, entrances, dome mechanism and scientific instruments.',
         ['furnished-hero', 'furnished-alternate', 'furnished-dome-close']),
        ('cutaway', 'A solid with a room inside.', 'The architectural section removes the front of the shell, frames and railings. The telescope, fork mount, books, cabinet and chart desk retain their complete authored geometry. This is a native CSG scene variant.',
         ['section-hero', 'section-alternate', 'section-interior-close']),
    ]
    for section, title, intro, keys in groups:
        first = data['views'][keys[0]]
        buttons = ''.join(f'<button type="button" data-target="{section}" data-view="{key}" aria-pressed="{str(i==0).lower()}">{e(data["views"][key]["label"])}</button>' for i, key in enumerate(keys))
        sections.append(f'''<section class="feature" id="{section}"><h2>{e(title)}</h2><p class="lead">{e(intro)}</p><figure><a class="fullsize" href="{first['asset']}"><img class="feature-image" src="{first['asset']}" width="1600" height="1200" alt="{e(first['label'])}" loading="lazy"></a><figcaption><div class="view-buttons" role="group" aria-label="{e(title)} captured views">{buttons}</div><span class="caption">{e(first['label'])}</span></figcaption></figure></section>''')
    progress = ''.join(f'<button type="button" data-target="progress" data-view="{key}" aria-pressed="{str(key=="furnished-hero").lower()}">{e(data["views"][key]["label"])}</button>' for key in ('original','site-hero','instrument-hero','furnished-hero'))
    downloads = ''.join(f'<li>{e(data["views"][key]["label"])}: <a href="{data["views"][key]["scene_asset"]}" download>scene</a> · <a href="{data["views"][key]["preset_asset"]}" download>preset</a></li>' for key in ('site-hero','instrument-hero','furnished-hero','section-hero'))
    gallery = ''.join(f'<a href="{item["asset"]}">{e(item["label"])}</a>' for item in data['views'].values())
    encoded = json.dumps(data, separators=(',', ':'), allow_nan=False).replace('</', '<\\/')
    template = Path(__file__).with_suffix('.html').read_text()
    page = template.replace('__FEATURES__',''.join(sections)).replace('__PROGRESS_BUTTONS__',progress)
    page = page.replace('__DOWNLOADS__',downloads).replace('__DIRECT_IMAGES__',gallery).replace('__DATA__',encoded)
    (out/'index.html').write_text(page)
    (out/'review-data.json').write_text(json.dumps(data, indent=2)+'\n')
    manifest = [dict(path=str(p.relative_to(out)), bytes=p.stat().st_size, sha256=sha(p))
                for p in sorted(out.rglob('*')) if p.is_file()]
    (out/'source-manifest.json').write_text(json.dumps(dict(files=manifest, complete=True), indent=2)+'\n')
    print(json.dumps(dict(output=str(out),new_native_views=8,baseline_views=1,files=len(manifest)+1)))


if __name__ == '__main__':
    main()
