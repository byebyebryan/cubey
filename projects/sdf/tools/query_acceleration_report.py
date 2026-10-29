#!/usr/bin/env python3
"""Build a portable HTML report from completed query-acceleration receipts."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil
import struct

ROOT=Path(__file__).resolve().parents[3]
EVIDENCE=['summary','timing','gpu-samples','resources','workload','pruning-pilot',
          'numerical','controlled','thin-contracts','cone-contracts','captures',
          'shadow-review','publication','negative','compiler','preservation','checks']


def build(output,raw,archive):
    output.mkdir(parents=True,exist_ok=False)
    (output/'images').mkdir();(output/'evidence').mkdir()
    read=lambda name:json.loads((archive/(name+'.json')).read_text())
    summary=read('summary');captures=read('captures');review=read('shadow-review')
    assert summary['completed_runs']==144 and captures['complete'] and review['complete']
    manifest=[]
    def copy(source,relative,expected=None):
        data=source.read_bytes();digest=hashlib.sha256(data).hexdigest()
        if expected is not None:assert digest==expected,source
        target=output/relative;target.write_bytes(data)
        manifest.append(dict(path=relative,sha256=digest,bytes=len(data),source=str(source.relative_to(ROOT))))
    images={}
    for view in ['preset','side','rear']:
        images[view]={}
        for state in ['shadows','unshadowed']:
            images[view][state]={}
            for algorithm,step in [('ordinary','ordinary'),('adaptive','adaptive-0.5')]:
                stem=step.replace('.','_')
                if state=='shadows':
                    source=raw/'final-captures'/f'guardian-{view}-combined-{stem}.png'
                    record=next(r for r in captures['runs'] if (r['scene'],r['view'],r['mode'],r['step'])==('guardian',view,'combined',step))
                else:
                    source=raw/'shadow-review'/f'{view}-{stem}.png'
                    record=next(r for r in review['runs'] if (r['view'],r['step'])==(view,step))
                assert struct.unpack('>II',source.read_bytes()[16:24])==(1920,1080)
                relative=f'images/{view}-{state}-{algorithm}.png'
                copy(source,relative,record['image_sha256']);images[view][state][algorithm]=relative
    for name in EVIDENCE:copy(archive/(name+'.json'),'evidence/'+name+'.json')
    for source,name in [(ROOT/'docs/notes/sdf-query-acceleration-results.md','results.md'),
                        (ROOT/'docs/notes/sdf-query-acceleration-research-plan.md','plan.md'),
                        (ROOT/'docs/notes/sdf-query-acceleration-progress.md','progress.md'),
                        (archive/'README.md','README.md'),
                        (ROOT/'docs/evidence/sdf-query-acceleration-research-2026-10/sources.json','sources.json')]:
        copy(source,'evidence/'+name)
    workload={step:[dict(scene=r['scene'],view=r['view'],mode=r['mode'],**r['workload']) for r in receipt['runs']]
              for step,receipt in read('workload').items()}
    cameras={scene:json.loads((ROOT/'projects/sdf/presets'/f'{preset}.json').read_text())['yaw']
             for scene,preset in [('guardian','sculpture-guardian-limestone'),('robot','survey-robot-fieldkit')]}
    data=dict(summary=summary,workload=workload,pilot=read('pruning-pilot'),images=images,cameras=cameras,
              review=review['runs'],implementation='603f073a',closeout='f63e32df')
    initial=[]
    for view in ['preset','side','rear']:
        r=next(r for r in summary['views'] if (r['scene'],r['view'],r['mode'])==('guardian',view,'combined'))['results']
        fmt=lambda v:f"{v['min_p95_ms']:.2f}–{v['max_p95_ms']:.2f}"
        initial.append(f'<tr><th>{html.escape(view.title())}</th><td>{fmt(r["ordinary"])} ms</td><td>{fmt(r["adaptive-0.5"])} ms</td></tr>')
    template=Path(__file__).with_name('query_acceleration_report_template.html').read_text()
    source=template.replace('@@DATA@@',json.dumps(data,separators=(',',':')).replace('<','\\u003c'))
    source=source.replace('@@INITIAL_ROWS@@',''.join(initial))
    (output/'index.html').write_text(source)
    (output/'asset-manifest.json').write_text(json.dumps(dict(files=manifest),indent=2)+'\n')
    print(json.dumps(dict(output=str(output),files=len(manifest)+2,bytes=sum(p.stat().st_size for p in output.rglob('*') if p.is_file())),indent=2))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--raw',type=Path,default=ROOT/'outputs/sdf/query-acceleration-loop-20261029')
    p.add_argument('--archive',type=Path,default=ROOT/'docs/evidence/sdf-query-acceleration-loop-2026-10')
    a=p.parse_args();build(a.output.resolve(),a.raw.resolve(),a.archive.resolve())


if __name__=='__main__':main()
