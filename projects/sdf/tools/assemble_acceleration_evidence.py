#!/usr/bin/env python3
"""Curate a completed acceleration-loop receipt; never synthesize skipped timing."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from acceleration_loop import VARIANTS, VIEWS, PRESETS, save
from benchmark_tracing import percentile


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--raw',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();raw=args.raw.resolve();out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    inputs={'qualification':'qualification-final/qualification.json',
            'workload':'workload-final/workload.json','timing':'timing-shared/timing.json',
            'actions':'content-actions/actions.json','images':'native/images.json'}
    data={key:json.loads((raw/path).read_text()) for key,path in inputs.items()}
    for key in ('qualification','workload','timing','actions'):
        if not data[key].get('complete'):raise RuntimeError(f'incomplete {key}')
    identities={value['binary_sha256'] for value in data.values()}
    if len(identities)!=1:raise RuntimeError('mixed binary identities')
    summary=dict(date='2026-10-28',baseline='81e6216',binary_sha256=identities.pop(),
        environment='Shared RTX 5070 Ti with an existing NVENC encode; per-run load retained. No idle-GPU acceptance claim.',
        frame_ranges=[],action_ranges=[],workload=[],qualification={},images=[])
    for scene in PRESETS:
        for view,_ in VIEWS:
            for name,_,_ in VARIANTS:
                for width,height in [(320,180),(960,540),(1920,1080)]:
                    rows=[r for r in data['timing']['runs'] if (r['scene'],r['view'],r['name'],r['width'])==(scene,view,name,width)]
                    row=dict(scene=scene,view=view,name=name,width=width,height=height)
                    if rows:
                        if len(rows)!=3:raise RuntimeError('incomplete timing repetition group')
                        values=[r['passes']['sdf.whole_scene']['p95_ms'] for r in rows]
                        row.update(status='measured',p95_ms_min=min(values),p95_ms_max=max(values),runs=3,
                                   samples_per_run=150,resources=rows[-1]['resources'])
                    else:row.update(status='Not measured',reason='previous resolution guard')
                    summary['frame_ranges'].append(row)
        for name in ('A64','A64-far','A64-root-far'):
            rows=[r for r in data['actions']['runs'] if r['scene']==scene and r['name']==name]
            if len(rows)!=3:raise RuntimeError('incomplete content action repetitions')
            full=[v for r in rows for v in r['full_prefix_completion_ms']]
            summary['action_ranges'].append(dict(scene=scene,name=name,p95_ms_min=min(r['p95_ms'] for r in rows),
                p95_ms_max=max(r['p95_ms'] for r in rows),full_prefix_p95_ms=percentile(full,.95),
                full_prefix_min_ms=min(full),full_prefix_max_ms=max(full),actions=30,
                note='Ten existing-history transitions per run, three runs; two transitions to complete source per run. Queue-idle completion.'))
    for row in data['workload']['runs']:
        summary['workload'].append({k:v for k,v in row.items() if k not in ('command','payload')})
    q=data['qualification'];w=data['workload']
    summary['qualification']=dict(independent_checks=sum(r['score']['scored'] for r in q['fixtures']),
        derived_gpu_points=sum(r['derived_points'] for r in q['fixtures']),
        semantic_actions=sum(r['actions'] for r in q['actions']),optical_captures=len(q['optics']),
        camera_queries=sum(r['query_count'] for r in w['runs']),
        sparse_checks=sum(r['queries'] for r in w['differential']),
        exhausted=0,dense_failures=sum(len(r['dense_failures']) for r in w['runs']),
        sparse_failures=sum(len(r['failures']) for r in w['differential']))
    (out/'images').mkdir()
    for row in data['images']['images']:
        source=Path(row['image']);destination=out/'images'/source.name;shutil.copy2(source,destination)
        summary['images'].append(dict(row,image='images/'+source.name,sha256=sha(destination)))
    (out/'optics').mkdir()
    for row in q['optics']:
        source=raw/'qualification-final'/(row['name']+f'-glass-{row["debug"]}.png')
        shutil.copy2(source,out/'optics'/source.name)
    for key,path in inputs.items():
        shutil.copy2(raw/path,out/(key+'.json'))
    for name in ('memory-envelope.json','cpu-fixture-correction.txt'):
        shutil.copy2(raw/name,out/name)
    shutil.copy2(raw/'native/material-boundary.json',out/'material-boundary.json')
    (out/'boundary').mkdir()
    for source in (raw/'native').glob('robot-boundary-*.png'):
        shutil.copy2(source,out/'boundary'/source.name)
    for name in ('A64','H64','H64-unclamped','A64-root','A64-far','A64-root-far'):
        shutil.copy2(raw/'qualification-final'/(name+'-actions.json'),out/(name+'-semantic-actions.json'))
    baseline=json.loads(Path('outputs/sdf/guardian-investigation-20261028/baseline.json').read_text())
    changed=[name for name,h in baseline['files'].items() if sha(Path(name))!=h]
    if changed:raise RuntimeError(f'protected content changed: {changed}')
    previous=json.loads(Path('docs/evidence/sdf-guardian-investigation-2026-10/provenance.json').read_text())
    old_shaders=[name for name,h in previous['shader_sha256'].items() if sha(Path('build/release/projects/sdf/shaders')/name)!=h]
    if old_shaders:raise RuntimeError(f'historical shaders changed: {old_shaders}')
    source_files=subprocess.check_output(['git','ls-files','projects/sdf'],text=True).splitlines()
    context=dict(source_head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        baseline='81e6216',binary_sha256=summary['binary_sha256'],
        source_sha256={name:sha(Path(name)) for name in source_files},
        shader_sha256={p.name:sha(p) for p in Path('build/release/projects/sdf/shaders').glob('*.spv')},
        raw_receipt_sha256={path:sha(raw/path) for path in inputs.values()},
        protected_files=len(baseline['files']),historical_shaders=len(previous['shader_sha256']),
        changed_protected_files=changed,changed_historical_shaders=old_shaders,
        timing_runs=len(data['timing']['runs']),completed_gpu_samples=150*len(data['timing']['runs']),
        skipped_groups=len(data['timing']['skipped']),raw_root=str(raw))
    save(out/'summary.json',summary);save(out/'context.json',context)
    print(json.dumps(dict(qualification=summary['qualification'],timing_runs=context['timing_runs'],skipped_groups=context['skipped_groups']),indent=2))


if __name__=='__main__':main()
