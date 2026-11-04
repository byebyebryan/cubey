#!/usr/bin/env python3
"""Bounded source-query ablations for retained Guardian/Robot controls. Serial GPU jobs."""
import argparse
import csv
import hashlib
import json
import math
import re
from pathlib import Path
from sdf_paths import resolve_sdf_path
import statistics
import struct
import subprocess
import time
from benchmark_tracing import percentile

PRESETS = {'guardian': 'sculpture-guardian-limestone', 'robot': 'survey-robot-fieldkit'}
ROOT = Path(__file__).resolve().parents[3]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def invoke(command, prefix, timeout=120):
    start = time.monotonic()
    run = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         cwd=ROOT, timeout=timeout)
    prefix.with_suffix('.log').write_text(run.stdout)
    if run.returncode or 'validation error' in run.stdout.lower():
        raise RuntimeError(f'{prefix}: {run.stdout[-3000:]}')
    return time.monotonic()-start


def metadata(binary):
    return dict(binary_sha256=digest(binary), runner_sha256=digest(Path(__file__)),
                source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                source_diff_sha256=hashlib.sha256(subprocess.check_output(['git', 'diff'])).hexdigest(),
                device=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version',
                                                '--format=csv,noheader'], text=True).strip())


def timing(args):
    output = args.output/('residency' if args.residency_run else 'timing-tight' if args.tight_root else 'timing'); output.mkdir(parents=True, exist_ok=False)
    frames,warmup=(180,30) if args.residency_run else (80,16)
    manifest = dict(metadata(args.binary), width=320, height=180, frames=frames, warmup=warmup, runs=[])
    destination = output/'timing.json'
    cases = []
    for scene in ('guardian', 'robot'):
        for mode, axis in [('sampled', 16), ('analytic', 64), ('hybrid', 64)]:
            for stage in ('off', 'full', 'primary', 'normal', 'material', 'shadow', 'no-shadow', 'no-pattern'):
                cases.append((scene, mode, axis, stage))
    # Coarse regions diagnose whether the 16->64 win is reduced primary or secondary work.
    for mode in ('analytic', 'hybrid'):
        for stage in ('off', 'primary', 'normal', 'shadow'):
            cases.append(('guardian', mode, 16, stage))
    if args.smoke:
        cases = [('guardian', 'analytic', 64, stage) for stage in ('off', 'full', 'primary', 'normal')]
    if args.tight_root:
        cases = [(scene, mode, 64, stage) for scene in ('guardian','robot')
                 for mode in ('analytic','hybrid') for stage in ('root-control','full','primary','normal','shadow')]
    if args.residency_run:
        cases = [(scene, mode, 64, f'{placement}-{root}') for scene in ('guardian','robot')
                 for mode in ('analytic','hybrid') for placement in ('host','regions','program','all')
                 for root in (('inner',) if args.inner_only else ('outer','inner'))]
    for repeat in range(1, (1 if args.smoke else 3)+1):
        for scene, mode, axis, stage in cases if repeat % 2 else reversed(cases):
            prefix = output/f'{scene}-{mode}-{axis}-{stage}-r{repeat}'
            csg = 'full' if mode == 'sampled' else 'pruned'
            command = [str(args.binary), '--config', str(resolve_sdf_path(f'projects/sdf/presets/{PRESETS[scene]}.json')),
                '--headless', '--headless-render-only', '--require-validation', '--frames', str(frames),
                '--profile-warmup-frames', str(warmup), '--profile-output', str(prefix), '--width', '320',
                '--height', '180', '--supersample', '1', '--shadows', '0' if stage == 'no-shadow' else '1',
                '--surface-trace', mode, '--csg-evaluation', csg, '--region-axis', str(axis),
                '--comparison-stage', stage if stage in ('full','primary','normal','material','shadow') else 'off']
            if stage == 'no-pattern':
                command += ['--no-procedural', '--no-bump']
            if args.residency_run:
                placement, root = stage.split('-')
                command[command.index('--comparison-stage')+1] = 'full'
                command += ['--comparison-residency', placement]
                if root == 'inner':
                    command += ['--comparison-tight-root']
            elif args.tight_root and stage!='root-control':
                command += ['--comparison-tight-root']
            wall = invoke(command, prefix)
            rows = list(csv.DictReader(prefix.with_suffix('.passes.csv').open()))
            groups = {}
            for row in rows:
                if row['kind'] == 'gpu':
                    groups.setdefault(row['label'], []).append(float(row['duration_ms']))
            if len(groups.get('sdf.whole_scene', [])) != frames-warmup:
                raise RuntimeError(f'incomplete tail samples: {prefix}')
            metrics = list(csv.DictReader(prefix.with_suffix('.metrics.csv').open()))
            resources = {r['name']:float(r['value']) for r in metrics if r['category']=='sdf.resources'}
            run = dict(scene=scene, mode=mode, axis=axis, stage=stage, repeat=repeat, command=command,
                       wall_seconds=wall, resources=resources,
                       passes={k:dict(samples=len(v),median_ms=statistics.median(v),p95_ms=percentile(v,.95))
                               for k,v in groups.items()})
            run['memory'] = [dict(name=m[0],type=int(m[1]),heap=int(m[2]),flags=int(m[3]))
                             for m in re.findall(r'sdf: study memory ([\w-]+) type=(\d+) heap=(\d+) flags=(\d+)', prefix.with_suffix('.log').read_text())]
            manifest['runs'].append(run)
            destination.write_text(json.dumps(manifest, indent=2)+'\n')
            print(prefix.name, round(run['passes']['sdf.whole_scene']['p95_ms'],4), flush=True)


def unit(a):
    length=math.sqrt(sum(x*x for x in a));return [x/length for x in a]


def cross(a,b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def camera_queries(config, width=320, height=180):
    yaw,pitch=config['yaw'],config['pitch'];target=[0,config['target-y'],0]
    offset=[math.cos(pitch)*math.sin(yaw),math.sin(pitch),math.cos(pitch)*math.cos(yaw)]
    eye=[target[i]+config['distance']*offset[i] for i in range(3)]
    forward=unit([-v for v in offset]);right=unit(cross(forward,[0,1,0]));up=cross(right,forward)
    queries=[];tan=math.tan(math.pi/8)
    for y in range(height):
        for x in range(width):
            dx=(2*(x+.5)/width-1)*width/height*tan
            dy=(1-2*(y+.5)/height)*tan
            direction=unit([forward[i]+right[i]*dx+up[i]*dy for i in range(3)])
            queries.append(dict(kind='ray',origin=eye,direction=direction))
    return queries


def analyze_workload(case):
    payload=case['workload'];rays=list(struct.iter_unpack('<8f',Path(payload['rays_file']).read_bytes()))
    stats=list(struct.iter_unpack('<52I',Path(payload['stats_file']).read_bytes()))
    if len(rays)!=payload['query_count'] or len(stats)!=len(rays):
        raise RuntimeError('workload payload length mismatch')
    hits=[w for w in stats if w[48]==1]
    if any(w[48]==2 or w[50]==2 for w in stats):
        raise RuntimeError('exhausted replay rays')
    phases={}
    for phase,name in enumerate(('primary','normal','shadow')):
        queries=sum(w[phase*4] for w in stats)
        instructions=sum(w[phase*4+1] for w in stats)
        bins=[sum(w[12+phase*4+j] for w in stats) for j in range(4)]
        bins += [sum(w[24+phase*4+j] for w in stats) for j in range(4)]
        walk=[sum(w[36+phase*4+j] for w in stats) for j in range(4)]
        if sum(bins)!=queries:
            raise RuntimeError('histogram/query count mismatch')
        if phase==1 and queries!=6*len(hits):
            raise RuntimeError('normal stencil count mismatch')
        if phase!=1 and walk[0]!=queries+walk[2]:
            raise RuntimeError('trace step != source queries + accepted cache steps')
        phases[name]=dict(source_queries=queries,instructions=instructions,
                         mean_instructions=instructions/queries if queries else 0,
                         max_instructions=max(w[phase*4+2] for w in stats),
                         full_fallback_queries=sum(w[phase*4+3] for w in stats),
                         instruction_bins=bins, trace_iterations=walk[0],cache_attempts=walk[1],
                         cache_accepted=walk[2],uniform_cell_cache_rejections=walk[3])
    lengths=[w[49] for w in hits]
    steps=[r[2] for r in rays]
    return dict(query_count=len(rays),hits=len(hits),misses=len(rays)-len(hits),exhausted=0,
                max_boundary_excess=max(struct.unpack('<f',struct.pack('<I',w[51]))[0] for w in stats),
                surface_program=dict(mean=statistics.mean(lengths),p50=percentile(lengths,.5),
                                     p95=percentile(lengths,.95),max=max(lengths)),
                primary_steps=dict(mean=statistics.mean(steps),p95=percentile(steps,.95),max=max(steps)),
                phases=phases, geometry_hash=case['geometry_hash'],instructions=case['instructions'],
                region_bytes=case['region_bytes'],region_build_upload_ms=case['region_build_upload_ms'],
                region_fallback_cells=case['region_fallback_cells'],
                bin_upper_bounds=[1,4,8,16,32,64,128,None],payload=payload)


def workload(args):
    output=args.output/('workload-tight' if args.tight_root else 'workload');output.mkdir(parents=True,exist_ok=False)
    manifest=dict(metadata(args.binary),runs=[])
    for scene,name in PRESETS.items():
        config=json.loads((ROOT/str(resolve_sdf_path(f'projects/sdf/presets/{name}.json'))).read_text())
        elevation=math.radians(config['sun-elevation']);azimuth=math.radians(config['sun-azimuth'])
        light=[math.cos(elevation)*math.sin(azimuth),math.sin(elevation),-math.cos(elevation)*math.cos(azimuth)]
        data=dict(format='cubey.sdf.tracing-study',version=1,workload=True,sunlight=light,
            cases=[dict(name=scene,scene=json.loads((ROOT/config['authored-scene']).read_text()),
                        queries=camera_queries(config))])
        input_path=output/f'{scene}-input.json';input_path.write_text(json.dumps(data,separators=(',',':'))+'\n')
        for mode,axis in [('analytic',16),('analytic',64),('hybrid',16),('hybrid',64)]:
            if args.tight_root and axis!=64:
                continue
            prefix=output/f'{scene}-{mode}-{axis}';receipt=prefix.with_suffix('.json')
            command=[str(args.binary),'--headless','--headless-render-only','--require-validation',
                '--frames','1','--width','32','--height','32','--surface-trace',mode,
                '--csg-evaluation','pruned','--region-axis',str(axis),'--comparison-input',str(input_path),
                '--comparison-output',str(receipt)]
            if args.tight_root:
                command += ['--comparison-tight-root']
            invoke(command,prefix)
            case=json.loads(receipt.read_text())['cases'][0]
            result=dict(scene=scene,mode=mode,axis=axis,command=command,**analyze_workload(case))
            manifest['runs'].append(result)
            (output/'workload.json').write_text(json.dumps(manifest,indent=2)+'\n')
            print(scene,mode,axis,'surface',result['surface_program'],'phases',
                  {k:round(v['mean_instructions'],2) for k,v in result['phases'].items()},flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode',choices=['timing','workload'])
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--binary',type=Path,default=Path('build/release/projects/sdf/sdf'))
    p.add_argument('--smoke',action='store_true')
    p.add_argument('--residency-run',action='store_true',help='Matched host/device-local buffer ablations')
    p.add_argument('--inner-only',action='store_true',help='Remeasure only the changed padded-bound variant')
    p.add_argument('--tight-root',action='store_true',help='Diagnostic inner-clip entry ablation')
    args=p.parse_args();args.output=args.output.resolve();args.binary=args.binary.resolve()
    args.output.mkdir(parents=True,exist_ok=True)
    globals()[args.mode](args)


if __name__=='__main__':
    main()
