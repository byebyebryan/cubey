#!/usr/bin/env python3
"""Compare the observatory's corrected layouts and inspect its observing slit."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[3]
RAW=ROOT/'outputs/sdf/observatory-cleanup-20261103/final'
LABELS={'before-hero':'Previous layout','round-hero':'Larger round base','square-hero':'Larger square base',
        'square-alternate':'Square — other side','square-plan':'Square — plan view',
        'square-slit':'Observing slit — front','square-slit_rear':'Observing slit — rear',
        'square-section-hero':'Architectural section','square-section-interior':'Interior close-up'}


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    out=p.parse_args().output.resolve();out.mkdir(parents=True,exist_ok=False)
    for folder in ('assets','scenes','presets'):(out/folder).mkdir()
    r=json.loads((RAW/'captures.json').read_text());assert r['complete'] and r['runtime_unchanged'] and len(r['runs'])==9
    data=dict(scope='Observatory spatial cleanup and dome-slit correction; static native comparison.',
              capture_receipt_sha256=sha(RAW/'captures.json'),generator_sha256=sha(__file__),views={})
    for row in r['runs']:
        key=row['variant']+'-'+row['view'];assert key in LABELS
        assert sha(row['image'])==row['image_sha256'] and sha(row['scene_input'])==row['scene_sha256']==sha(ROOT/row['scene'])
        assert sha(row['config_input'])==row['config_sha256']
        asset='assets/'+Path(row['image']).name;scene='scenes/'+Path(row['scene']).name;preset='presets/'+Path(row['preset']).name
        shutil.copy2(row['image'],out/asset);shutil.copy2(row['scene_input'],out/scene);shutil.copy2(ROOT/row['preset'],out/preset)
        data['views'][key]=dict(**row,label=LABELS[key],asset=asset,scene_asset=scene,preset_asset=preset)
    e=html.escape;sections=[]
    panels=[('layout','Room for the approach and instruments.',
             'The staircase is pulled back onto a supported landing. The loose survey telescope has its own observing pad, and the armillary and planting have been moved clear of the approach. The building keeps its original scale.',
             ['square-hero','before-hero','round-hero','square-alternate','square-plan']),
            ('slit','One opening, with trim that follows it.',
             'The brass side guides and rear header are exact circular arcs on the dome. Meridian seams terminate beneath that trim, and rivets stay outside the aperture margin. The annex roof seams also sit against their panels.',
             ['square-slit','square-slit_rear']),
            ('section','The furnished cutaway carries over.',
             'The architecture remains a local CSG group. The telescope, timber floors, logs and keeper’s desk retain their complete geometry when the front of the shell is cut away.',
             ['square-section-hero','square-section-interior'])]
    for target,title,intro,keys in panels:
        first=data['views'][keys[0]]
        buttons=''.join(f'<button type="button" data-target="{target}" data-view="{key}" aria-pressed="{str(i==0).lower()}">{e(data["views"][key]["label"])}</button>' for i,key in enumerate(keys))
        sections.append(f'''<section id="{target}"><h2>{e(title)}</h2><p class="lead">{e(intro)}</p><figure><a class="fullsize" href="{first['asset']}"><img src="{first['asset']}" width="1600" height="1200" alt="{e(first['label'])}" loading="lazy"></a><figcaption><div class="views" role="group" aria-label="{e(title)} captured views">{buttons}</div><span class="caption">{e(first['label'])}</span></figcaption></figure></section>''')
    downloads=''.join(f'<li>{e(data["views"][key]["label"])}: <a href="{data["views"][key]["scene_asset"]}" download>scene</a> · <a href="{data["views"][key]["preset_asset"]}" download>preset</a></li>' for key in ('round-hero','square-hero','square-section-hero'))
    direct=''.join(f'<a href="{v["asset"]}">{e(v["label"])}</a>' for v in data['views'].values())
    encoded=json.dumps(data,separators=(',',':'),allow_nan=False).replace('</','<\\/')
    template=Path(__file__).with_suffix('.html').read_text()
    (out/'index.html').write_text(template.replace('__PANELS__',''.join(sections)).replace('__DOWNLOADS__',downloads).replace('__DIRECT__',direct).replace('__DATA__',encoded))
    (out/'review-data.json').write_text(json.dumps(data,indent=2)+'\n')
    files=[dict(path=str(f.relative_to(out)),bytes=f.stat().st_size,sha256=sha(f)) for f in sorted(out.rglob('*')) if f.is_file()]
    (out/'source-manifest.json').write_text(json.dumps(dict(files=files,complete=True),indent=2)+'\n')
    print(json.dumps(dict(output=str(out),native_views=9,files=len(files)+1)))


if __name__=='__main__':main()
