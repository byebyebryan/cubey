#!/usr/bin/env python3
"""Audit saved sculpture volumes against explicit, separately chosen design probes."""
import argparse
import hashlib
import json
from pathlib import Path

from sdf_probe import distance


def audit(scene_path, checks):
    scene=json.loads(scene_path.read_text())
    rows=[]
    for check in checks:
        points=check['points']
        samples=[]
        if check.get('path'):
            for a,b in zip(points,points[1:]):
                samples.extend([[x*(1-i/20)+y*i/20 for x,y in zip(a,b)] for i in range(20)])
            samples.append(points[-1])
        else: samples=points
        values=[distance(scene['root'],p) for p in samples]
        margin=check.get('margin',.001)
        passed=all(v<=-margin for v in values) if check['expect']=='solid' else all(v>=margin for v in values)
        rows.append(dict(**check,passed=passed,samples=len(samples),min_distance=min(values),max_distance=max(values)))
    return dict(passed=all(r['passed'] for r in rows),
                scene_sha256=hashlib.sha256(scene_path.read_bytes()).hexdigest(),checks=rows,
                scope='Double-precision saved-tree sign probes for design intent; native field/rendering oracles and visual review remain separate.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--subject',required=True)
    parser.add_argument('--metadata',type=Path,default=Path('projects/sdf/sculptures.json'))
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[3]
    meta=json.loads(args.metadata.read_text())[args.subject]
    config=json.loads((root/meta['config']).read_text())
    result=audit(root/config['authored-scene'],meta['intent_checks'])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    for row in result['checks']:
        print(args.subject,row['name'],'passed' if row['passed'] else 'FAILED',
              row['min_distance'],row['max_distance'])
    if not result['passed']: raise SystemExit(1)


if __name__=='__main__': main()
