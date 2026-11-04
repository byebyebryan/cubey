#!/usr/bin/env python3
"""Archive completed query-acceleration evidence. Does not launch native jobs."""
import argparse
import csv
import hashlib
import json
import re
from pathlib import Path
import statistics
import subprocess
from benchmark_tracing import percentile
from query_study import ROOT, digest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();raw=args.raw.resolve();out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=True)
    def read(relative):return json.loads((raw/relative).read_text())
    def save(name,value):
        body=json.dumps(value,indent=2)
        if name in {'gpu-samples.json','pruning-pilot.json'}:
            # Keep machine numeric vectors on one line; preserve every value.
            body=re.sub(r'\[\n\s*([-+0-9eE.,\s]+)\n\s*\]',
                lambda m:'['+re.sub(r'\s+','',m.group(1)).replace(',',', ')+']',body)
            assert json.loads(body)==value
        (out/name).write_text(body+'\n')
    def copy(source,target):save(target,read(source))
    timing=read('final-timing/timing.json')
    assert timing['complete'] and len(timing['runs'])==144
    assert timing['binary_sha256']==digest(ROOT/'build/release/projects/sdf/sdf')
    cases=set();samples=[];resources=[]
    for run in timing['runs']:
        eligible=run['scene']=='guardian'
        assert run['selection']['shadow_optimized']==int(eligible)
        assert run['selection']['cones_optimized']==int(eligible and run['mode']=='combined')
        key=tuple(run[k] for k in ['scene','view','mode','step','repeat'])
        assert key not in cases;cases.add(key)
        command=run['command'];prefix=Path(command[command.index('--profile-output')+1])
        csv_path=Path(str(prefix)+'.passes.csv');metrics_path=Path(str(prefix)+'.metrics.csv')
        groups={};frames={}
        for row in csv.DictReader(csv_path.open()):
            if row['kind']=='gpu':
                groups.setdefault(row['label'],[]).append(float(row['duration_ms']))
                frames.setdefault(row['label'],[]).append(int(row['frame_index']))
        assert set(groups)=={'sdf.trace','sdf.post','sdf.whole_scene'}
        assert all(sorted(v)==list(range(30,180)) for v in frames.values())
        for label,values in groups.items():
            assert abs(percentile(values,.95)-run['passes'][label]['p95_ms'])<1e-9
        identity={k:run[k] for k in ['scene','view','mode','step','repeat']}
        samples.append(dict(**identity,raw_passes_sha256=digest(csv_path),
            raw_metrics_sha256=digest(metrics_path),frame_indices=frames,passes_ms=groups))
        metrics=list(csv.DictReader(metrics_path.open()))
        groups_of_interest={category:{r['name']:float(r['value']) for r in metrics if r['category']==category}
            for category in ['sdf.resources','sdf.prepared_cones']}
        resources.append(dict(**identity,metrics=groups_of_interest))
    save('timing.json',timing);save('gpu-samples.json',samples);save('resources.json',resources)
    summary=[]
    steps=['ordinary','relaxed-1.2','relaxed-1.5','adaptive-0.5']
    for scene in ['guardian','robot']:
        for view in ['preset','side','rear']:
            for mode in ['shadow','combined']:
                results={}
                for step in steps:
                    runs=[r for r in timing['runs'] if (r['scene'],r['view'],r['mode'],r['step'])==(scene,view,mode,step)]
                    assert len(runs)==3
                    p95=[r['passes']['sdf.whole_scene']['p95_ms'] for r in runs]
                    results[step]=dict(p95_ms=p95,min_p95_ms=min(p95),max_p95_ms=max(p95),
                        mean_of_three_p95_ms=statistics.mean(p95))
                baseline=results['ordinary']['mean_of_three_p95_ms']
                for step,value in results.items():
                    value['time_saved_ms']=baseline-value['mean_of_three_p95_ms']
                    value['fractional_time_reduction']=value['time_saved_ms']/baseline
                    value['useful_gain_in_view']=value['time_saved_ms']>=.5 and value['fractional_time_reduction']>=.05
                    value['regression_gate_failed']=value['time_saved_ms']<-.5 and value['fractional_time_reduction']<-.03
                summary.append(dict(scene=scene,view=view,mode=mode,results=results))
    encoder={}
    for step in steps:
        values=[float(r[k].split(',')[1].strip().removesuffix('%').strip())
            for r in timing['runs'] if r['step']==step for k in ['gpu_before','gpu_after']]
        encoder[step]=dict(min_percent=min(values),max_percent=max(values),mean_percent=statistics.mean(values),
            zero_observations=values.count(0),boundary_observations=len(values))
    save('summary.json',dict(implementation_head=timing['source_head'],binary_sha256=timing['binary_sha256'],
        completed_runs=144,whole_scene_frame_samples=21600,all_gpu_pass_samples=64800,
        decision='Retain adaptive-0.5 as explicit headless study; automatic remains ordinary. Fixed controls fail useful-gain gate; finer full level deferred by CPU construction forecast.',
        load_boundary='All boundaries have shared encoder activity; no idle-GPU or interactive/windowed root-far qualification.',
        encoder=encoder,views=summary))
    workload={}
    for step,folder in zip(steps,['workload-qualified','workload-relaxed12','workload-relaxed15','workload-adaptive50']):
        receipt=read(folder+'/workload.json');assert receipt['complete'] and len(receipt['runs'])==12
        assert all(r['native_image_identical'] and r['workload']['exhausted']==0 for r in receipt['runs'])
        workload[step]=receipt
    save('workload.json',workload)
    copy('guardian-pruning-pilot.json','pruning-pilot.json')
    copy('final-captures/captures.json','captures.json')
    captures=read('final-captures/captures.json')
    initial={(r['scene'],r['view'],r['mode']):r['native_image_sha256'] for r in workload['ordinary']['runs']}
    ordinary=[r for r in captures['runs'] if r['step']=='ordinary']
    assert len(ordinary)==12 and all(initial[r['scene'],r['view'],r['mode']]==r['image_sha256'] for r in ordinary)
    copy('shadow-review/shadow-review.json','shadow-review.json')
    assert read('shadow-review/shadow-review.json')['complete']
    numerical=[]
    for step,folder in [('relaxed-1.2','final-contracts-relaxed12'),('relaxed-1.5','final-contracts-relaxed15'),('adaptive-0.5','contracts-adaptive50')]:
        receipt=read(folder+'/contracts.json');assert receipt['complete']
        for row in receipt['runs']:
            assert row['score']['passed']
            command=row['command'];result_path=Path(command[command.index('--comparison-output')+1])
            result=json.loads(result_path.read_text())
            assert digest(ROOT/'build/release/projects/sdf/shaders'/result['query_shader'])==result['query_shader_sha256']
            row['kernel']=dict(name=result['query_shader'],sha256=result['query_shader_sha256'])
        numerical.append(dict(step=step,receipt=receipt))
    save('numerical.json',numerical)
    copy('guarded-controlled.json','controlled.json');assert read('guarded-controlled.json')['passed']
    copy('final-contracts-relaxed12/thin-contracts.json','thin-contracts.json')
    cone_path=ROOT/'outputs/sdf/source-query-optimization-20261029/supplemental-contracts/supplemental-contracts.json'
    save('cone-contracts.json',json.loads(cone_path.read_text()))
    publication=[]
    for step,folder in [('relaxed-1.2','history-relaxed12'),('relaxed-1.5','history-relaxed15'),('adaptive-0.5','history-adaptive50')]:
        receipt=read(folder+'/qualification.json');assert receipt['complete']
        for mode in receipt['history']:
            rows=mode['actions'];assert len(rows)==9 and rows[7]['failed'] and not rows[8]['failed']
            for a,b in [(1,3),(2,4),(6,7)]:assert digest(Path(rows[a]['frame_png']))==digest(Path(rows[b]['frame_png']))
            for row in rows:row['frame_png_sha256']=digest(Path(row['frame_png']))
        publication.append(dict(step=step,receipt=receipt))
    save('publication.json',publication);copy('negative.json','negative.json')
    compiler=read('compiler/statistics.json');assert len(compiler['pipelines'])==8
    assert 'VUID-' not in (raw/'compiler.log').read_text()
    compiler['diagnostic_tool_sha256']=digest(ROOT/'build/release/projects/sdf/cubey_project_sdf_shader_stats')
    save('compiler.json',compiler)
    base=read('baseline.json')
    legacy=json.loads((ROOT/'docs/evidence/sdf-source-query-optimization-2026-10/baseline.json').read_text())
    assert all(digest(ROOT/p)==h for p,h in base['shaders'].items())
    assert all(digest(ROOT/p)==h for p,h in legacy['content'].items())
    canonical=['sdf_authored.cpp','sdf_authored.h','sdf_history.cpp','sdf_history.h','sdf_field.cpp','sdf_field.h',
        'sdf_regions.cpp','sdf_regions.h','sdf_bounds.cpp','sdf_prepared_cones.cpp','sdf_prepared_cones.h',
        'sdf_recipe.cpp','sdf_recipe.h','sdf_timeline.cpp','sdf_timeline.h']
    canonical_hashes={p:digest(ROOT/'projects/sdf'/p) for p in canonical}
    for p,h in canonical_hashes.items():
        original=subprocess.check_output(['git','show',base['head']+':projects/sdf/'+p],cwd=ROOT)
        assert hashlib.sha256(original).hexdigest()==h
    assert not subprocess.check_output(['git','diff',base['head'],'--name-only','--','src/cubey','include/cubey'],cwd=ROOT).strip()
    save('preservation.json',dict(baseline=base,protected_content=legacy['content'],
        retained_shader_count=len(base['shaders']),protected_content_count=len(legacy['content']),
        ordinary_native_capture_parity_cases=12,
        all_final_shader_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted((ROOT/'build/release/projects/sdf/shaders').glob('*.spv'))},
        canonical_source_baseline_and_current_sha256=canonical_hashes,
        shared_engine_source_and_API_diff_empty=True))
    checks=read('checks.json');assert checks['status']=='passed' and checks['groups']==12
    save('checks.json',checks)
    print('Archived 144 runs, 64800 GPU samples, current-workload receipts and qualification gates.')


if __name__=='__main__':main()
