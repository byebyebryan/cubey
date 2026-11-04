#!/usr/bin/env python3
"""Build the visual reference shortlist without generating native SDF content."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[3]
REFERENCES = ROOT / 'docs/evidence/sdf-sculptural-references-2026-11/references.json'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = json.loads(REFERENCES.read_text())
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out / 'assets').mkdir()
    e = html.escape
    cards, panels = [], []
    for row in data['references']:
        for name in row['images']:
            shutil.copy2(args.assets / name, out / 'assets' / name)
        key = row['id']
        first = 'assets/' + row['images'][0]
        cards.append(f'<a class="card" href="#{key}"><img src="{first}" alt="{e(row["title"])} reference" loading="lazy"><span>{e(row["title"])}</span><small>{e(row["role"])}</small></a>')
        buttons = ''.join(f'<button type="button" data-target="{key}" data-index="{i}" aria-pressed="{str(i == 0).lower()}">{e(label)}</button>' for i, label in enumerate(row['image_labels']))
        follow = ''.join('<li>' + e(text) + '</li>' for text in row['follow'])
        license_link = f' · <a href="{row["license_url"]}">License</a>' if 'license_url' in row else ''
        extra = f'<p><a href="{row["corroborating_source"]}">Primary research corroboration, section 8</a></p>' if 'corroborating_source' in row else ''
        panels.append(f'''<section id="{key}"><p class="eyebrow">{e(row['role'])}</p><h2>{e(row['title'])}</h2><p class="byline">{e(row['creator'])} · {e(row['identity'])}</p>
<div class="study"><figure><a class="fullsize" href="{first}"><img src="{first}" alt="{e(row['title'])}: {e(row['image_labels'][0])}" loading="lazy"></a><figcaption><div class="views">{buttons}</div><span class="caption">{e(row['image_labels'][0])}</span><p class="credit">{e(row['image_credit'])} <a href="{row['rights_url']}">Image source / credit</a>{license_link}</p></figcaption></figure>
<div class="analysis"><h3>What gives it a defined shape</h3><p>{e(row['form'])}</p><h3>Why it belongs in this shortlist</h3><p>{e(row['why'])}</p><h3>Forms to follow</h3><ul>{follow}</ul><p><strong>Leave for later:</strong> {e(row['defer'])}</p></div></div>
<div class="limits"><p><strong>Modeling scope:</strong> {e(row['native_scope'])}</p><p><strong>Limits:</strong> {e(row['risk'])}</p><p><a href="{row['source']}">Open original artwork record</a></p>{extra}</div></section>''')
    methods = ''.join(f'<li><a href="{row["url"]}">{e(row["title"])}</a><p>{e(row["takeaway"])}</p></li>' for row in data['method_sources'])
    stages = ''.join('<li>' + e(text) + '</li>' for text in data['proposed_study']['stages'])
    acceptance = ''.join('<li>' + e(text) + '</li>' for text in data['proposed_study']['acceptance'])
    direct = ''.join(f'<a href="assets/{name}">{e(row["title"])} / {e(label)}</a>' for row in data['references'] for name, label in zip(row['images'], row['image_labels']))
    encoded = json.dumps(data, separators=(',', ':'), allow_nan=False).replace('</', '<\\/')
    template = Path(__file__).with_suffix('.html').read_text()
    for token, value in dict(CARDS=''.join(cards), PANELS=''.join(panels), METHODS=methods, STAGES=stages, ACCEPTANCE=acceptance, DIRECT=direct, DATA=encoded).items():
        template = template.replace('__' + token + '__', value)
    (out / 'index.html').write_text(template)
    shutil.copy2(REFERENCES, out / 'references.json')
    files = [dict(path=str(p.relative_to(out)), bytes=p.stat().st_size, sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()]
    manifest = dict(format='cubey.reference-report-manifest', files=files, references_sha256=sha(REFERENCES), generator_sha256=sha(Path(__file__)), template_sha256=sha(Path(__file__).with_suffix('.html')))
    (out / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(dict(output=str(out), references=len(data['references']), images=sum(len(r['images']) for r in data['references']), files=len(files) + 1)))


if __name__ == '__main__':
    main()
