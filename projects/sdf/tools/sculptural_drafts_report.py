#!/usr/bin/env python3
"""Export a native clay comparison for the organic SDF direction."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[3]
RAW = ROOT / 'outputs/sdf/sculptural-drafts-20261103/final'
SUBJECTS = {
    'coiled-sleeper': dict(title='Coiled sleeper', number='01',
        gesture='A curled body, a lowered head, and a tail that completes the gesture.',
        read='The most immediately readable subject. Broad back, belly and folded limbs flow into one resting mass; small carved eyelids establish the expression.',
        unresolved='The forepaw and lower jaw still merge visually from the main angle. The next form pass would clarify the weight and contact between them.'),
    'forest-spirit': dict(title='Forest spirit', number='02',
        gesture='A face emerging from a leaning trunk, carried into unequal roots and boughs.',
        read='The closest relative of Guardian in this set. The face, roots and broken crown share one continuous body; the unequal branches give it a direction.',
        unresolved='The face is still a stylized mask, and some cheek and branch joins are abrupt. Expression and integration with the trunk need another sculpting pass.'),
    'eroded-refuge': dict(title='Eroded refuge', number='03',
        gesture='A sweeping outcrop hollowed into a chamber, entrance and upper lightwell.',
        read='A way to retain the architectural idea through carved negative space. Unequal supports and a broad canopy replace the repeated mechanical details of the dioramas.',
        unresolved='The most abstract draft. It reads as a carved rock before it reads as a place to inhabit; the approach and inner floor need a clearer relationship.'),
}
LABELS = {'carved-hero': 'Carved form', 'carved-profile': 'Other side', 'mass-hero': 'Before carving'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    for folder in ('assets', 'scenes', 'presets'):
        (out / folder).mkdir()
    receipt = json.loads((RAW / 'captures.json').read_text())
    assert receipt['complete'] and receipt['runtime_unchanged'] and len(receipt['runs']) == 9
    assert len(receipt['shaders']) == 220
    data = dict(scope='Organic SDF clay drafts; static native images for art direction.',
                capture_receipt_sha256=sha(RAW / 'captures.json'),
                report_generator_sha256=sha(__file__), subjects=SUBJECTS, views={})
    for row in receipt['runs']:
        assert sha(row['image']) == row['image_sha256']
        assert sha(row['scene_input']) == row['scene_sha256'] == sha(ROOT / row['scene'])
        assert sha(row['config_input']) == row['config_sha256']
        key = f"{row['subject']}-{row['state']}-{row['view']}"
        label = LABELS[f"{row['state']}-{row['view']}"]
        asset = 'assets/' + Path(row['image']).name
        scene = 'scenes/' + Path(row['scene']).name
        preset = 'presets/' + Path(row['preset']).name
        shutil.copy2(row['image'], out / asset)
        shutil.copy2(row['scene_input'], out / scene)
        shutil.copy2(ROOT / row['preset'], out / preset)
        data['views'][key] = dict(**row, label=label, asset=asset, scene_asset=scene, preset_asset=preset)
    e = html.escape
    sections, downloads = [], []
    for subject, info in SUBJECTS.items():
        keys = [subject + '-' + view for view in LABELS]
        first = data['views'][keys[0]]
        buttons = ''.join(f'<button type="button" data-target="{subject}" data-view="{key}" aria-pressed="{str(i == 0).lower()}">{e(data["views"][key]["label"])}</button>' for i, key in enumerate(keys))
        sections.append(f'''<section id="{subject}"><div class="section-heading"><span class="number">{info['number']}</span><div><h2>{e(info['title'])}</h2><p class="lead">{e(info['gesture'])}</p></div></div>
<figure><a class="fullsize" href="{first['asset']}"><img src="{first['asset']}" width="1440" height="1080" alt="{e(info['title'])}: {e(first['label'])}" loading="lazy"></a><figcaption><div class="views" role="group" aria-label="{e(info['title'])} captured views">{buttons}</div><span class="caption">{e(info['title'])}: {e(first['label'])}</span></figcaption></figure>
<div class="reading"><p>{e(info['read'])}</p><p class="muted"><strong>Still rough:</strong> {e(info['unresolved'])}</p></div></section>''')
        for state in ('mass', 'carved'):
            view = data['views'][subject + '-' + state + '-hero']
            downloads.append(f'<li>{e(info["title"])} / {state}: <a href="{view["scene_asset"]}" download>scene</a> · <a href="{view["preset_asset"]}" download>preset</a></li>')
    direct = ''.join(f'<a href="{v["asset"]}">{e(SUBJECTS[v["subject"]]["title"])} / {e(v["label"])}</a>' for v in data['views'].values())
    encoded = json.dumps(data, separators=(',', ':'), allow_nan=False).replace('</', '<\\/')
    template = Path(__file__).with_suffix('.html').read_text()
    (out / 'index.html').write_text(template.replace('__PANELS__', ''.join(sections)).replace('__DOWNLOADS__', ''.join(downloads)).replace('__DIRECT__', direct).replace('__DATA__', encoded))
    (out / 'review-data.json').write_text(json.dumps(data, indent=2) + '\n')
    files = [dict(path=str(f.relative_to(out)), bytes=f.stat().st_size, sha256=sha(f)) for f in sorted(out.rglob('*')) if f.is_file()]
    (out / 'source-manifest.json').write_text(json.dumps(dict(files=files, complete=True), indent=2) + '\n')
    print(json.dumps(dict(output=str(out), native_views=9, files=len(files) + 1)))


if __name__ == '__main__':
    main()
