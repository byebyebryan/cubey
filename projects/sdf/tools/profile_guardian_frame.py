#!/usr/bin/env python3
"""Serial ordinary/stage/compiler controls for Guardian's current rear-view candidate."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
from acceleration_loop import gpu_state, save
from investigate_guardian import ROOT, metadata, digest, invoke
from benchmark_tracing import percentile

CASES = [('ordinary', [], 'root_far_sdf_analytic_pruned.frag.spv')]
for style in ('runtime', 'fixed'):
    for stage in ('primary','normal','material','shadow','full'):
        shader = 'root_far_investigate_sdf_analytic_pruned.frag.spv' if style == 'runtime' else f'profile_fixed_{stage}.frag.spv'
        CASES.append((f'{style}-{stage}', ['--frame-profile',style,'--comparison-stage',stage], shader))
for variant in ('nodebug','single-material','specialized','no-sky'):
    CASES.append((variant,['--frame-profile',variant],f'profile_{variant}.frag.spv'))
CASES += [('no-shadow',['--shadows','0'],'root_far_sdf_analytic_pruned.frag.spv'),
          ('no-procedural',['--no-procedural','--no-bump'],'root_far_sdf_analytic_pruned.frag.spv')]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase',choices=['pilot','timing','capture','compiler'])
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--binary',type=Path,default=ROOT/'build/release/projects/sdf/sdf')
    p.add_argument('--width',type=int)
    p.add_argument('--height',type=int)
    p.add_argument('--view',choices=['rear','preset','side'],default='rear')
    args=p.parse_args();args.output=args.output.resolve();args.binary=args.binary.resolve()
    args.output.mkdir(parents=True,exist_ok=False)
    receipt=dict(metadata(args.binary),profile_runner_sha256=digest(Path(__file__)),runs=[])
    width,height=(320,180) if args.phase=='capture' else (1920,1080)
    width,height=args.width or width,args.height or height
    base=[str(args.binary),'--headless','--require-validation','--supersample','1',
          '--config','projects/sdf/presets/sculpture-guardian-limestone.json',
          '--width',str(width),'--height',str(height),'--yaw',str(.4+{'rear':math.pi,'side':math.pi/2,'preset':0}[args.view]),
          '--surface-trace','analytic','--csg-evaluation','pruned','--region-axis','64',
          '--acceleration-variant','root-far']
    if args.phase=='compiler':
        command=[str(ROOT/'build/release/projects/sdf/cubey_project_sdf_shader_stats'),
            str(ROOT/'build/release/projects/sdf/shaders'),str(args.output),
            *sorted({c[2] for c in CASES})]
        invoke(command,args.output/'compiler',timeout=180)
        stats=json.loads((args.output/'statistics.json').read_text())
        for row in stats['pipelines']:
            fragment=next(e for e in row['executables'] if e['stages']==16)
            print(row['shader'],{s['name']:s['value'] for s in fragment['statistics']},flush=True)
        receipt['command']=command;save(args.output/'provenance.json',receipt);return
    repeats=3 if args.phase=='timing' else 1
    frames,warmup=(180,30) if args.phase=='timing' else (12,2)
    cases=CASES if args.phase!='capture' else [c for c in CASES if c[0] in ('ordinary','runtime-full','fixed-full','nodebug','single-material','specialized')]
    for repeat in range(1,repeats+1):
        for name,flags,shader in cases if repeat%2 else reversed(cases):
            prefix=args.output/f'{name}-r{repeat}'
            command=base+['--frames',str(1 if args.phase=='capture' else frames),*flags]
            if args.phase=='capture':command+=['--output',str(prefix.with_suffix('.png'))]
            else:command+=['--headless-render-only','--profile-warmup-frames',str(warmup),'--profile-output',str(prefix)]
            before=gpu_state();wall=invoke(command,prefix,timeout=180);after=gpu_state()
            row=dict(name=name,repeat=repeat,command=command,shader=shader,
                shader_sha256=digest(ROOT/'build/release/projects/sdf/shaders'/shader),
                gpu_before=before,gpu_after=after,wall_seconds=wall)
            if args.phase=='capture':row['image_sha256']=digest(prefix.with_suffix('.png'))
            else:
                groups={}
                for r in csv.DictReader(prefix.with_suffix('.passes.csv').open()):
                    if r['kind']=='gpu':groups.setdefault(r['label'],[]).append(float(r['duration_ms']))
                if len(groups.get('sdf.whole_scene',[]))!=frames-warmup:raise RuntimeError('incomplete GPU tail')
                row['passes']={k:dict(samples=len(v),median_ms=statistics.median(v),p95_ms=percentile(v,.95)) for k,v in groups.items()}
            receipt['runs'].append(row);save(args.output/'results.json',receipt)
            print(name,repeat,row.get('passes',{}).get('sdf.whole_scene',{}).get('p95_ms',row.get('image_sha256')),flush=True)
    receipt['complete']=True;save(args.output/'results.json',receipt)


if __name__=='__main__':main()
