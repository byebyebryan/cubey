#!/usr/bin/env python3
"""Bounded E0-E3 study. Serial native GPU jobs; fresh output per phase."""
import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics
import struct
import subprocess
from investigate_guardian import PRESETS, ROOT, camera_queries, digest, invoke, metadata
from benchmark_tracing import percentile
from tracing_comparison import EPS, check, dot
from check_tracing_images import pixels

VARIANTS = [('sampled', 'sampled', 'off'), ('A64', 'analytic', 'control'),
            ('H64', 'hybrid', 'control'), ('H64-unclamped', 'hybrid', 'unclamped'),
            ('A64-root', 'analytic', 'root'), ('A64-far', 'analytic', 'far'),
            ('A64-root-far', 'analytic', 'root-far')]
VIEWS = [('preset', 0), ('side', math.pi/2), ('rear', math.pi)]


def flags(mode, variant):
    return ['--surface-trace', mode, '--csg-evaluation', 'full' if mode == 'sampled' else 'pruned',
            '--region-axis', '64', '--acceleration-variant', variant]


def save(path, data):
    path.write_text(json.dumps(data, indent=2)+'\n')


def study_metadata(binary):
    return dict(metadata(binary), acceleration_runner_sha256=digest(Path(__file__)))


def gpu_state():
    return subprocess.check_output(['nvidia-smi',
        '--query-gpu=utilization.gpu,utilization.encoder,clocks.sm,clocks.mem,temperature.gpu,power.draw,memory.used',
        '--format=csv,noheader'], text=True).strip()


def base(args):
    return [str(args.binary), '--headless', '--require-validation', '--supersample', '1']


def qualify(args):
    receipt = dict(study_metadata(args.binary), fixtures=[], actions=[], optics=[])
    manifest = ROOT/'projects/sdf/comparison/fixtures.json'
    for name, mode, variant in VARIANTS[1:]:
        prefix = args.output/(name+'-fixtures')
        command = base(args)+['--headless-render-only', '--frames', '1', '--width', '32', '--height', '32',
            *flags(mode, variant), '--comparison-input', str(manifest), '--comparison-output', str(prefix.with_suffix('.json'))]
        invoke(command, prefix)
        result = json.loads(prefix.with_suffix('.json').read_text())
        score = check(json.loads(manifest.read_text()), result)
        receipt['fixtures'].append(dict(name=name, command=command, score=score,
            derived_points=sum(c['derived_probe_count'] for c in result['cases']),
            max_derived_error=max(c['derived_probe_max_error'] for c in result['cases'])))
        save(args.output/'qualification.json', receipt)
        if not score['passed']:
            raise RuntimeError(f'{name}: unchanged independent fixtures failed')
        prefix = args.output/(name+'-actions')
        command = base(args)+['--headless-render-only', '--frames', '20', '--width', '96', '--height', '64',
            *flags(mode, variant), '--authored-scene', 'projects/sdf/comparison/initial.json',
            '--comparison-actions', 'projects/sdf/comparison/actions.json', '--comparison-output', str(prefix.with_suffix('.json'))]
        invoke(command, prefix)
        result = json.loads(prefix.with_suffix('.json').read_text())
        if len(result['actions']) != 20:
            raise RuntimeError('incomplete semantic replay')
        receipt['actions'].append(dict(name=name, command=command, actions=20, receipt_sha256=digest(prefix.with_suffix('.json'))))
        for debug in (0, 9):
            prefix = args.output/(name+f'-glass-{debug}')
            command = base(args)+['--config', 'projects/sdf/presets/material-study/sphere-glass.json',
                '--frames', '1', '--width', '320', '--height', '180', *flags(mode, variant),
                '--debug', str(debug), '--output', str(prefix.with_suffix('.png'))]
            invoke(command, prefix)
            row = dict(name=name, debug=debug, command=command, image_sha256=digest(prefix.with_suffix('.png')))
            if debug == 9:
                w, h, channels, rows = pixels(prefix.with_suffix('.png'))
                center = list(rows[h//2][w//2*channels:w//2*channels+3])
                magenta = sum(r[i] > 245 and r[i+1] < 10 and r[i+2] > 245 for r in rows for i in range(0, len(r), channels))
                row.update(center_rgb=center, exhaustion_pixels=magenta)
                if center[2] < 32 or center[0] >= 10 or center[1] >= 10 or magenta:
                    raise RuntimeError(f'{name}: bounded glass transport failed')
            receipt['optics'].append(row)
        save(args.output/'qualification.json', receipt)
        print(name, '155 independent checks; 20 actions; glass pass', flush=True)
    receipt['complete'] = True
    save(args.output/'qualification.json', receipt)


def timing(args):
    qualification = json.loads((args.qualification/'qualification.json').read_text())
    if not qualification.get('complete') or qualification['binary_sha256'] != digest(args.binary):
        raise RuntimeError('timing requires completed qualification of this binary')
    receipt = dict(study_metadata(args.binary), frames=180, warmup=30, runs=[], skipped=[])
    for width, height, threshold in [(320, 180, 12), (960, 540, 50), (1920, 1080, None)]:
        cases = []
        for scene, preset in PRESETS.items():
            config = json.loads((ROOT/f'projects/sdf/presets/{preset}.json').read_text())
            for view, offset in VIEWS:
                for name, mode, variant in VARIANTS:
                    previous = [r for r in receipt['runs'] if r['scene'] == scene and r['view'] == view and r['name'] == name and r['width'] == (320 if width == 960 else 960)]
                    limit = 12 if width == 960 else 50
                    if width != 320 and (len(previous) != 3 or max(r['passes']['sdf.whole_scene']['p95_ms'] for r in previous) > limit):
                        receipt['skipped'].append(dict(scene=scene, view=view, name=name, width=width, height=height, reason='previous resolution guard', status='Not measured'))
                        continue
                    cases.append((scene, preset, view, config['yaw']+offset, name, mode, variant))
        for repeat in range(1, 4):
            for scene, preset, view, yaw, name, mode, variant in cases if repeat % 2 else reversed(cases):
                prefix = args.output/f'{scene}-{view}-{name}-{width}-r{repeat}'
                command = base(args)+['--config', f'projects/sdf/presets/{preset}.json', '--headless-render-only',
                    '--frames', '180', '--profile-warmup-frames', '30', '--profile-output', str(prefix),
                    '--width', str(width), '--height', str(height), '--yaw', str(yaw), *flags(mode, variant)]
                environment_before = gpu_state()
                wall = invoke(command, prefix)
                environment_after = gpu_state()
                groups = {}
                for row in csv.DictReader(prefix.with_suffix('.passes.csv').open()):
                    if row['kind'] == 'gpu':
                        groups.setdefault(row['label'], []).append(float(row['duration_ms']))
                if len(groups.get('sdf.whole_scene', [])) != 150:
                    raise RuntimeError('incomplete GPU samples')
                metrics = list(csv.DictReader(prefix.with_suffix('.metrics.csv').open()))
                resources = {r['name']:float(r['value']) for r in metrics if r['category'] == 'sdf.resources'}
                row = dict(scene=scene, view=view, name=name, repeat=repeat, width=width, height=height,
                    command=command, wall_seconds=wall, resources=resources,
                    gpu_state_fields='gpu_pct,encoder_pct,sm_MHz,mem_MHz,temp_C,power_W,used_MiB',
                    gpu_before=environment_before, gpu_after=environment_after,
                    passes={k:dict(samples=len(v), median_ms=statistics.median(v), p95_ms=percentile(v,.95)) for k,v in groups.items()})
                row['memory'] = [dict(name=m[0],type=int(m[1]),heap=int(m[2]),flags=int(m[3])) for m in re.findall(
                    r'sdf: acceleration memory ([\w-]+) type=(\d+) heap=(\d+) flags=(\d+)', prefix.with_suffix('.log').read_text())]
                receipt['runs'].append(row)
                save(args.output/'timing.json', receipt)
                print(prefix.name, round(row['passes']['sdf.whole_scene']['p95_ms'],4), flush=True)
        save(args.output/'timing.json', receipt)
    receipt['complete'] = True
    save(args.output/'timing.json', receipt)


def analyze(case):
    payload = case['workload']
    rays = list(struct.iter_unpack('<8f', Path(payload['rays_file']).read_bytes()))
    stats = list(struct.iter_unpack('<64I', Path(payload['stats_file']).read_bytes()))
    if len(rays) != payload['query_count'] or len(stats) != len(rays):
        raise RuntimeError('counter payload length mismatch')
    hits = [w for w in stats if w[48] == 1]
    if any(w[48] == 2 or w[50] == 2 for w in stats):
        raise RuntimeError('exhausted camera/secondary ray')
    phases = {}
    for phase, name in enumerate(('primary', 'normal', 'shadow')):
        queries = sum(w[phase*4] for w in stats)
        visits = sum(w[phase*4+1] for w in stats)
        histogram = sum(sum(w[12+phase*4:16+phase*4])+sum(w[24+phase*4:28+phase*4]) for w in stats)
        walk = [sum(w[36+phase*4+j] for w in stats) for j in range(4)]
        aid = [sum(w[52+phase*4+j] for w in stats) for j in range(4)]
        if histogram != queries or (phase == 1 and queries != 6*len(hits)) or (phase != 1 and walk[0] != queries+walk[2]):
            raise RuntimeError('counter invariants failed')
        phases[name] = dict(exact_queries=queries, csg_visits=visits, mean_program=visits/queries if queries else 0,
            fallback_queries=sum(w[phase*4+3] for w in stats), iterations=walk[0], bound_lookups=walk[1],
            bound_advances=walk[2], uniform_rejections=walk[3], far_lookups=aid[0], far_advances=aid[1],
            far_exact_fallbacks=aid[2], root_rejected=aid[3],
            hit_ray_visits=sum(w[phase*4+1] for w in stats if w[48] == 1),
            miss_ray_visits=sum(w[phase*4+1] for w in stats if w[48] == 0))
    lengths = [w[49] for w in hits]
    return dict(query_count=len(rays), hits=len(hits), exhausted=0, phases=phases,
        primary_steps=dict(mean=statistics.mean(r[2] for r in rays), p95=percentile([r[2] for r in rays],.95), max=max(r[2] for r in rays)),
        surface_program=dict(mean=statistics.mean(lengths), p95=percentile(lengths,.95), max=max(lengths)),
        region_bytes=case['region_bytes'], far_cells=case['region_far_cells'], fallback_cells=case['region_fallback_cells'],
        region_build_upload_ms=case['region_build_upload_ms'], geometry_hash=case['geometry_hash'], payload=payload)


def workload(args):
    receipt = dict(study_metadata(args.binary), runs=[], differential=[])
    for scene, preset in PRESETS.items():
        config = json.loads((ROOT/f'projects/sdf/presets/{preset}.json').read_text())
        elevation, azimuth = math.radians(config['sun-elevation']), math.radians(config['sun-azimuth'])
        light = [math.cos(elevation)*math.sin(azimuth), math.sin(elevation), -math.cos(elevation)*math.cos(azimuth)]
        for view, offset in VIEWS:
            view_config = dict(config, yaw=config['yaw']+offset)
            queries = camera_queries(view_config)
            data = dict(format='cubey.sdf.tracing-study',version=1,workload=True,sunlight=light,
                cases=[dict(name=scene,scene=json.loads((ROOT/config['authored-scene']).read_text()),queries=queries)])
            input_path = args.output/f'{scene}-{view}-input.json';save(input_path, data)
            control = None
            for name, mode, variant in VARIANTS[1:]:
                prefix = args.output/f'{scene}-{view}-{name}'
                command = base(args)+['--headless-render-only', '--frames', '1', '--width', '32', '--height', '32',
                    *flags(mode, variant), '--comparison-input', str(input_path), '--comparison-output', str(prefix.with_suffix('.json'))]
                invoke(command, prefix)
                case = json.loads(prefix.with_suffix('.json').read_text())['cases'][0]
                row = dict(scene=scene,view=view,name=name,command=command,**analyze(case))
                actual = list(struct.iter_unpack('<8f', Path(row['payload']['rays_file']).read_bytes()))
                if control is None:
                    control = actual
                failures = []
                for i,(a,b,q) in enumerate(zip(control, actual, queries)):
                    if int(a[1]) != int(b[1]): failures.append([i,'status']);continue
                    if int(a[1]) != 1: continue
                    incidence = abs(dot(a[4:7],q['direction']))
                    limit = 4*EPS if incidence >= .25 else 4*EPS/max(.01,incidence)
                    if abs(a[0]-b[0]) > limit or abs(b[3]) > EPS+2e-6:
                        failures.append([i,'depth/residual'])
                row['dense_failures'] = failures
                receipt['runs'].append(row);save(args.output/'workload.json',receipt)
                if failures: raise RuntimeError(f'{scene}/{view}/{name}: dense geometry gate')
                print(scene,view,name,'visits',sum(p['csg_visits'] for p in row['phases'].values()),flush=True)
            # Full source sparse reference and each ordinary candidate. Counter
            # readback is independent from this uninstrumented trace replay.
            indices = list(range(0,len(queries),64))
            data['workload'] = False;data['cases'][0]['queries'] = [queries[i] for i in indices]
            sparse_path = args.output/f'{scene}-{view}-sparse.json';save(sparse_path,data)
            reference = None
            for name,mode,variant in [('source','analytic','off'),*VARIANTS[1:]]:
                prefix = args.output/f'{scene}-{view}-{name}-sparse'
                selection = ['--surface-trace','analytic','--csg-evaluation','full'] if name == 'source' else flags(mode,variant)
                command = base(args)+['--headless-render-only','--frames','1','--width','32','--height','32',*selection,
                    '--comparison-input',str(sparse_path),'--comparison-output',str(prefix.with_suffix('.json'))]
                invoke(command,prefix)
                actual = json.loads(prefix.with_suffix('.json').read_text())['cases'][0]['results']
                if reference is None: reference = actual
                failures = []
                dense_rays = None
                if name != 'source':
                    dense = receipt['runs'][-6+next(j for j,v in enumerate(VARIANTS[1:]) if v[0] == name)]
                    dense_rays = Path(dense['payload']['rays_file']).read_bytes()
                for i,a,b in zip(indices,reference,actual):
                    if a['status'] != b['status'] or b['status'] == 2: failures.append([i,'status']);continue
                    if a['status'] != 1: continue
                    incidence = abs(dot(a['normal'],queries[i]['direction']))
                    limit = 4*EPS if incidence >= .25 else 4*EPS/max(.01,incidence)
                    if abs(a['t']-b['t']) > limit or abs(b['residual']) > EPS+2e-6: failures.append([i,'depth/residual'])
                    if name != 'source':
                        r = struct.unpack_from('<8f',dense_rays,i*32)
                        if int(r[1]) != b['status'] or abs(r[0]-b['t']) > 4*EPS: failures.append([i,'instrumentation'])
                receipt['differential'].append(dict(scene=scene,view=view,name=name,command=command,queries=len(indices),failures=failures))
                save(args.output/'workload.json',receipt)
                if failures: raise RuntimeError(f'{scene}/{view}/{name}: sparse full-source gate')
    receipt['complete'] = True;save(args.output/'workload.json',receipt)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase',choices=['qualify','timing','workload'])
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--binary',type=Path,default=ROOT/'build/release/projects/sdf/sdf')
    p.add_argument('--qualification',type=Path)
    args = p.parse_args();args.binary=args.binary.resolve();args.output=args.output.resolve()
    args.output.mkdir(parents=True,exist_ok=False)
    globals()[args.phase](args)


if __name__ == '__main__': main()
