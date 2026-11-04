#!/usr/bin/env python3
"""Curate the completed current-candidate investigation, retaining invalid stats raw."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "projects/sdf/tools"))
import shutil
import subprocess
from acceleration_loop import save


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--raw',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();raw=args.raw.resolve();out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    timing=json.loads((raw/'timing-final/results.json').read_text())
    qualification=json.loads((raw/'qualification/qualification.json').read_text())
    compiler=json.loads((raw/'compiler-final/statistics.json').read_text())
    baseline=json.loads((raw/'baseline.json').read_text())
    assert timing.get('complete') and timing['binary_sha256']==qualification['binary_sha256']
    changed=[name for name,h in baseline['files'].items() if sha(Path(name))!=h]
    assert not changed,changed
    summary=dict(date='2026-10-28',scope='Guardian rear, 1920x1080, root-far analytic axis64',
        ranges=[],compiler=[],qualification=qualification,source_identity=timing['binary_sha256'],
        notes=['All stage rows are whole-scene ablations, not additive component timestamps.',
               'Register and binary-size statistics are compiler resource evidence, not live occupancy or cache counters.',
               'Local-memory statistic is excluded: trivial vertex shader reports an implausible value even after explicit zero initialization.',
               'Shader specialization is diagnostic and guarded to one material, existing fast path, debug 0 and static authored headless atmosphere.',
               'No default change or new acceleration structure.'])
    for name in dict.fromkeys(r['name'] for r in timing['runs']):
        rows=[r for r in timing['runs'] if r['name']==name];assert len(rows)==3
        v=[r['passes']['sdf.whole_scene']['p95_ms'] for r in rows]
        summary['ranges'].append(dict(name=name,p95_ms_min=min(v),p95_ms_max=max(v),
            repetitions=3,samples_per_run=150,shader=rows[0]['shader'],shader_sha256=rows[0]['shader_sha256']))
    for row in compiler['pipelines']:
        fragment=next(e for e in row['executables'] if e['stages']==16)
        usable={s['name']:s['value'] for s in fragment['statistics'] if s['name'] in ('Register Count','Binary Size','Stack Size')}
        summary['compiler'].append(dict(shader=row['shader'],statistics=usable,internal_representations=len(fragment['representations'])))
    (out/'images').mkdir()
    captures=[]
    for folder in ('capture-hd','capture-side-hd','capture-preset'):
        data=json.loads((raw/folder/'results.json').read_text())
        assert data['binary_sha256']==timing['binary_sha256']
        h=data['runs'][0]['image_sha256'];assert all(r['image_sha256']==h for r in data['runs'])
        captures.append(dict(folder=folder,checks=len(data['runs']),all_byte_identical=True,sha256=h))
        for name in ('ordinary','specialized'):
            source=raw/folder/(name+'-r1.png');shutil.copy2(source,out/'images'/(folder+'-'+name+'.png'))
    summary['native_captures']=captures
    for source,target in [('timing-final/results.json','timing.json'),
                          ('compiler-final/statistics.json','compiler.json'),
                          ('qualification/qualification.json','qualification.json'),
                          ('baseline.json','baseline.json'),('runtime-failure.json','rejected-runtime-setup.json')]:
        shutil.copy2(raw/source,out/target)
    context=dict(source_head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        binary_sha256=timing['binary_sha256'],baseline_head=baseline['head'],
        protected_files=len(baseline['files']),changed_protected_files=changed,
        accepted_timing_runs=len(timing['runs']),completed_gpu_samples=150*len(timing['runs']),
        source_sha256={name:sha(Path(name)) for name in subprocess.check_output(['git','ls-files','projects/sdf'],text=True).splitlines()},
        raw_root=str(raw),raw_sha256={str(p.relative_to(raw)):sha(p) for p in raw.rglob('*.json')})
    save(out/'summary.json',summary);save(out/'context.json',context)
    print(json.dumps(dict(ranges=summary['ranges'],native_captures=captures),indent=2))


if __name__=='__main__':main()
