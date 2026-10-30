#!/usr/bin/env python3
"""Serial native frozen-base study. Never edits canonical scenes or public modes.

geometry uses independent closed-form contracts; timing is a separate process
phase. Each phase requires a fresh destination so failed pilots remain visible.
"""
import argparse
import copy
import json
import math
from pathlib import Path
import statistics
import subprocess

from benchmark_tracing import percentile
from investigate_guardian import ROOT, PRESETS, digest, invoke, metadata, camera_queries
from quality_speed_loop import profile
from acceleration_loop import gpu_state
from tracing_comparison import fixtures, document, sphere, ray, H, EPS, check


def save(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n')


def study_metadata(args):
    return dict(metadata(args.binary), tool_sha256=digest(Path(__file__)),
        public_binary_sha256=digest(args.public_binary),
        private_shader_sha256={p.name:digest(p) for p in sorted((args.binary.parent/'shaders').glob('*live_edit*.spv'))})


def manifest(cases):
    return dict(format='cubey.sdf.live-edit-study', version=1,
                contracts=fixtures()['contracts'], cases=cases)


def base_case(c, root=None):
    c=copy.deepcopy(c)
    c['base']=copy.deepcopy(c['scene'])
    if root is not None: c['base']['root']=copy.deepcopy(root)
    return c


def pending_cases():
    cases=[]
    for c in fixtures()['cases']:
        if c['name'].startswith(('smooth-identical-', 'smooth-subtract-', 'cut-owner-')) or c['name']=='subgrid-cavity':
            cases.append(base_case(c, c['scene']['root']['children'][0]))
    def add(name, base, root, queries):
        cases.append(dict(name=name, base=document(base), scene=document(root), queries=queries))
    a=sphere('frozen', .08, [-.25,0,0]); b=sphere('pending', .08, [.25,0,0])
    add('outside-old-bounds',a,dict(name='union',type='union',children=[a,b]),[
        ray([.25,0,.7],[0,0,-1],.62,[0,0,1]),
        ray([.25,0,0],[0,0,1],.08,[0,0,1]),
        dict(kind='point',position=[.25,0,0],expected_distance=-.08)])
    box=dict(name='frozen-box',type='box',half_extent=[.15]*3)
    add('uniform-solid-cavity',box,dict(name='cut',type='subtract',cut_material='blue',
        children=[box,sphere('cutter',.06)]),[
        ray([0,0,0],[0,0,1],.06,[0,0,-1]),
        ray([0,0,.12],[0,0,-1],.06,[0,0,-1]),
        dict(kind='point',position=[0,0,0],expected_distance=.06)])
    cylinder=dict(name='through',type='cylinder',radius=.06,half_length=.5,
                  rotation_xyzw=[math.sqrt(.5),0,0,math.sqrt(.5)])
    add('through-hole',box,dict(name='cut',type='subtract',cut_material='blue',children=[box,cylinder]),[
        ray([0,0,.7],[0,0,-1],None),ray([0,0,0],[1,0,0],.06,[-1,0,0])])
    big=sphere('crosses-workspace',.1,[.45,0,0])
    add('clip-before-smooth-cut',big,dict(name='smooth-cut',type='smooth-subtract',radius=.04,
        children=[big,sphere('cutter',.02,[.47,0,0])]),[
        dict(kind='point',position=[.498,0,0],expected_distance=-.001),
        ray([.498,0,0],[-1,0,0],.008,[-1,0,0])])
    # Independent group transform control, with a pending addition outside it.
    group=dict(name='group',type='group',scale=2,position=[-.2,0,0],children=[sphere('child',.04)])
    add('group-frozen-prefix',group,dict(name='append',type='union',children=[group,b]),[
        ray([-.2,0,.7],[0,0,-1],.62,[0,0,1]),ray([.25,0,.7],[0,0,-1],.62,[0,0,1])])
    add('empty-base-addition',None,sphere('first',.1),[
        ray([0,0,.7],[0,0,-1],.6,[0,0,1]),dict(kind='point',position=[0,0,0],expected_distance=-.1)])
    add('empty-base-stays-empty',None,None,[ray([0,0,.7],[0,0,-1],None)])
    return cases


def native(args, inp, out, policy, extra=(), size=(16,16), preset=None):
    flags=[str(args.binary)]
    if preset: flags+=['--config',str(ROOT/f'projects/sdf/presets/{PRESETS[preset]}.json')]
    flags+=['--live-input',str(inp),'--live-output',str(out),'--live-policy',policy,
        '--headless','--require-validation','--motion','static','--supersample','1',
        '--width',str(size[0]),'--height',str(size[1]),'--no-ao','--no-ground','--no-clouds','--no-occluder']
    return flags+list(extra)


def query(args, data, label, policy):
    inp=args.output/f'{label}-input.json';save(inp,data)
    out=args.output/f'{label}-{policy}.json'
    flags=native(args,inp,out,policy,['--live-queries'])
    wall=invoke(flags,out.with_suffix(''),timeout=300)
    result=json.loads(out.read_text())
    return dict(command=flags,wall_seconds=wall,output=out.name,sha256=digest(out)),result['queries']


def geometry(args):
    receipt=dict(study_metadata(args),runs=[],checks={},complete=False)
    zero=manifest([base_case(c) for c in fixtures()['cases']])
    pending=manifest(pending_cases())
    for label,data in [('zero',zero),('pending',pending)]:
        for policy in ('source','cached'):
            run,result=query(args,data,label,policy);receipt['runs'].append(run)
            score=check(data,result)
            residuals=[abs(r['cpu_live_residual']) for c in result['cases'] for r in c['results'] if 'status'in r and r['status']==1]
            errors=[r['cpu_point_error'] for c in result['cases'] for r in c['results'] if 'distance'in r]
            score['cpu_provider_point_max']=max(errors,default=0)
            score['cpu_provider_hit_residual_max']=max(residuals,default=0)
            score['exhausted']=sum(r.get('status')==2 for c in result['cases'] for r in c['results'])
            receipt['checks'][f'{label}-{policy}']=score
            save(args.output/'geometry.json',receipt)
            print(label,policy,json.dumps(score),flush=True)
    # Fresh canonical bakes: original renderer, original sampled/source policies.
    inp=args.output/'fresh-input.json'
    fresh=copy.deepcopy(pending);fresh['format']='cubey.sdf.tracing-study'
    for c in fresh['cases']:del c['base']
    save(inp,fresh)
    for mode in ('analytic','sampled'):
        out=args.output/f'fresh-{mode}.json'
        flags=[str(args.public_binary),'--headless','--require-validation','--headless-render-only',
            '--frames','1','--width','16','--height','16','--surface-trace',mode,
            '--csg-evaluation','pruned' if mode=='analytic' else 'full','--region-axis','64',
            '--comparison-input',str(inp),'--comparison-output',str(out)]
        wall=invoke(flags,out.with_suffix(''),timeout=300)
        result=json.loads(out.read_text())
        scored=copy.deepcopy(result)
        for c in scored['cases']:c['mode']='fresh-'+mode
        receipt['checks']['fresh-'+mode]=check(fresh,scored)
        receipt['runs'].append(dict(command=flags,wall_seconds=wall,output=out.name,sha256=digest(out)))
        print('fresh',mode,receipt['checks']['fresh-'+mode],flush=True)
    receipt['complete']=True
    receipt['source_gate']=all(receipt['checks'][k]['passed'] and receipt['checks'][k].get('exhausted',0)==0 for k in ('zero-source','pending-source','fresh-analytic'))
    receipt['automatic_sampled_handoff_gate']=receipt['checks']['fresh-sampled']['passed']
    save(args.output/'geometry.json',receipt)


def clean_scene(scene):
    scene=copy.deepcopy(scene)
    for key in ('geometry_hash','appearance_hash','history'):scene.pop(key,None)
    return scene


def subject(name):
    cfg=json.loads((ROOT/f'projects/sdf/presets/{PRESETS[name]}.json').read_text())
    return clean_scene(json.loads((ROOT/cfg['authored-scene']).read_text()))


def appended(base, count, operation='union'):
    result=copy.deepcopy(base)
    # Visible front-side stamps distributed over a compact patch, stable prefix.
    stamps=[sphere(f'pending-{i}',.022,[.085*math.sin(i*2.39996),
            .08*math.cos(i*2.39996),.245+.001*(i%3)]) for i in range(count)]
    if count:result['root']=dict(name='live-tail',type=operation,children=[base['root'],*stamps])
    return result


def prepare(args):
    receipt=dict(study_metadata(args),admitted=[],rejected=[],negative=[],complete=False)
    for name,counts in [('robot',[0,1,4,8,16,32,64]),('guardian',[0,1,4,8,13])]:
        base=subject(name)
        for count in counts:
            data=manifest([dict(name=f'{name}-{count}',base=base,scene=appended(base,count))])
            inp=args.output/f'{name}-{count}.json';save(inp,data)
            out=args.output/f'{name}-{count}-admission.json'
            flags=native(args,inp,out,'source',['--live-validate-only'])
            invoke(flags,out.with_suffix(''))
            receipt['admitted'].append(dict(name=name,count=count,input=str(inp),sha256=digest(inp),identity=json.loads(out.read_text())['programs'][0]))
    # A canonical 65th pending leaf and full Guardian's 14th leaf must reject.
    for label,name,count in [('tail-limit','robot',65),('source-capacity','guardian',14)]:
        base=subject(name);data=manifest([dict(name=label,base=base,scene=appended(base,count))])
        inp=args.output/f'{label}.json';save(inp,data)
        out=args.output/f'{label}-rejected.json'
        flags=native(args,inp,out,'source',['--live-validate-only'])
        p=subprocess.run(flags,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if p.returncode==0 or out.exists():raise RuntimeError(f'{label} unexpectedly admitted')
        receipt['rejected'].append(dict(name=label,returncode=p.returncode,reason=p.stdout.strip(),command=flags))
    base=document(sphere('base',.2));scene=appended(base,1)
    negatives={
        'prefix':lambda d:d['cases'][0]['scene']['root']['children'][0].update(radius=.21),
        'palette':lambda d:d['cases'][0]['scene']['materials'][0].update(roughness=.3),
        'non-unit-ray':lambda d:d['cases'][0].update(queries=[dict(kind='ray',origin=[0,0,1],direction=[0,0,-2])]),
        'bad-query':lambda d:d['cases'][0].update(queries=[dict(kind='triangle',position=[0,0,0])]),
    }
    for label,mutate in negatives.items():
        data=manifest([dict(name=label,base=copy.deepcopy(base),scene=copy.deepcopy(scene),queries=[dict(kind='point',position=[0,0,0])])]);mutate(data)
        inp=args.output/f'negative-{label}.json';save(inp,data)
        out=args.output/f'negative-{label}-output.json'
        flags=native(args,inp,out,'source',['--live-validate-only','--live-queries'])
        p=subprocess.run(flags,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if p.returncode==0 or out.exists():raise RuntimeError(f'{label} unexpectedly admitted')
        receipt['negative'].append(dict(name=label,returncode=p.returncode,reason=p.stdout.strip(),command=flags))
    receipt['complete']=True;save(args.output/'admission.json',receipt)


def timing(args):
    prepared=json.loads(args.inputs.read_text())
    receipt=dict(study_metadata(args),runs=[],complete=False,protocol=dict(width=1920,height=1080,frames=180,warmup=30,repeats=3))
    cases=[(r,policy) for r in prepared['admitted'] for policy in ('cached','source')]
    controls=[(r,mode) for r in prepared['admitted'] if r['count'] in (0,8)
              for mode in ('fresh-cached','fresh-balanced','fresh-analytic')]
    cases+=controls
    for repeat in range(1,4):
        for row,policy in cases if repeat%2 else reversed(cases):
            prefix=args.output/f"{row['name']}-{row['count']}-{policy}-r{repeat}"
            if policy.startswith('fresh-'):
                scene_path=args.output/f"{row['name']}-{row['count']}-scene.json"
                save(scene_path,json.loads(Path(row['input']).read_text())['cases'][0]['scene'])
                flags=[str(args.public_binary),'--config',str(ROOT/f"projects/sdf/presets/{PRESETS[row['name']]}.json"),
                    '--authored-scene',str(scene_path),'--headless','--headless-render-only','--require-validation',
                    '--motion','static','--supersample','1','--width','1920','--height','1080',
                    '--no-ao','--no-ground','--no-clouds','--no-occluder','--frames','180',
                    '--profile-warmup-frames','30','--profile-output',str(prefix),'--render-mode',policy[6:]]
            else:
                flags=native(args,Path(row['input']),prefix.with_suffix('.json'),policy,
                    ['--headless-render-only','--frames','180','--profile-warmup-frames','30',
                     '--profile-output',str(prefix)],(1920,1080),row['name'])
            before=gpu_state()
            wall=invoke(flags,prefix,timeout=300)
            after=gpu_state()
            passes,metrics=profile(prefix,180,30)
            values=passes['sdf.whole_scene']['values_ms']
            stats=dict(p50_ms=statistics.median(values),p95_ms=percentile(values,.95),
                p99_ms=percentile(values,.99),max_ms=max(values),over_11_11=sum(v>11.11 for v in values),
                over_16_67=sum(v>16.67 for v in values))
            receipt['runs'].append(dict(subject=row['name'],count=row['count'],policy=policy,repeat=repeat,
                command=flags,input_sha256=row['sha256'],wall_seconds=wall,passes=passes,metrics=metrics,stats=stats,
                gpu_before=before,gpu_after=after,
                native_receipt=None if policy.startswith('fresh-') else json.loads(prefix.with_suffix('.json').read_text()),passes_sha256=digest(prefix.with_suffix('.passes.csv'))))
            save(args.output/'timing.json',receipt)
            print(prefix.name,round(stats['p95_ms'],3),flush=True)
    receipt['complete']=True;save(args.output/'timing.json',receipt)


def captures(args):
    receipt=dict(study_metadata(args),runs=[],complete=False)
    # Two material cut, smooth clipping, subgrid cut, and both authored subjects.
    cases=pending_cases()
    selected=[c for c in cases if c['name'] in ('uniform-solid-cavity','through-hole','subgrid-cavity','clip-before-smooth-cut')]
    selected +=[dict(name=name+'-8',base=subject(name),scene=appended(subject(name),8)) for name in PRESETS]
    for c in selected:
        name=c['name'];preset=next((s for s in PRESETS if name.startswith(s)),None)
        inp=args.output/f'{name}-input.json';save(inp,manifest([c]))
        scene_path=args.output/f'{name}-scene.json';save(scene_path,c['scene'])
        for mode in ('live-cached','live-source','fresh-cached','fresh-analytic','fresh-balanced'):
            prefix=args.output/f'{name}-{mode}'
            if mode.startswith('live'):
                flags=native(args,inp,prefix.with_suffix('.json'),mode[5:],
                    ['--frames','1','--output',str(prefix.with_suffix('.png'))],(960,540),preset)
            else:
                flags=[str(args.public_binary)]
                if preset:flags+=['--config',str(ROOT/f'projects/sdf/presets/{PRESETS[preset]}.json')]
                flags+=['--authored-scene',str(scene_path),'--headless','--require-validation',
                    '--motion','static','--supersample','1','--width','960','--height','540',
                    '--no-ao','--no-ground','--no-clouds','--no-occluder','--frames','1',
                    '--render-mode',mode[6:],'--output',str(prefix.with_suffix('.png'))]
            wall=invoke(flags,prefix,timeout=300)
            receipt['runs'].append(dict(name=name,mode=mode,command=flags,wall_seconds=wall,image=prefix.with_suffix('.png').name,sha256=digest(prefix.with_suffix('.png'))))
            save(args.output/'captures.json',receipt);print(prefix.name,flush=True)
    # Frozen field/regions while poses AND pending source change. Serial captures.
    for name in PRESETS:
        base=subject(name)
        cfg=json.loads((ROOT/f'projects/sdf/presets/{PRESETS[name]}.json').read_text())
        data=manifest([dict(name=name,base=base,scene=appended(base,1))])
        data['states']=[dict(scene=appended(base,1+i//2),yaw=cfg['yaw']+.12*i,pitch=cfg['pitch'],distance=cfg['distance']) for i in range(16)]
        inp=args.output/f'{name}-motion-input.json';save(inp,data)
        for policy in ('cached','source'):
            prefix=args.output/f'{name}-motion-{policy}'
            flags=native(args,inp,prefix.with_suffix('.json'),policy,['--headless-render-only','--frames','16'],(640,360),name)
            wall=invoke(flags,prefix,timeout=300)
            native_result=json.loads(prefix.with_suffix('.json').read_text())
            if len(native_result['poses'])!=16 or native_result['field_builds']!=1 or native_result['region_builds']!=1:raise RuntimeError('motion rebuild/count violation')
            receipt['runs'].append(dict(name=name,mode='motion-'+policy,command=flags,wall_seconds=wall,native_receipt=native_result))
            save(args.output/'captures.json',receipt);print(prefix.name,flush=True)
    receipt['complete']=True;save(args.output/'captures.json',receipt)


def qualify(args):
    """Native-derived camera/shadow rays and a deterministic spatial point grid."""
    receipt=dict(study_metadata(args),runs=[],complete=False)
    for name in PRESETS:
        base=subject(name);scene=appended(base,8)
        cfg=json.loads((ROOT/f'projects/sdf/presets/{PRESETS[name]}.json').read_text())
        rays=camera_queries(cfg,160,90)
        points=[dict(kind='point',position=[-.499+.998*((i*f)%509)/508 for f in (1,137,311)]) for i in range(509)]
        case=dict(name=name,base=base,scene=scene,queries=rays+points)
        for policy in ('cached','source'):
            run,result=query(args,manifest([case]),name+'-camera',policy)
            rows=result['cases'][0]['results'];hits=[(q,r) for q,r in zip(rays,rows) if r.get('status')==1]
            exhausted=sum(r.get('status')==2 for r in rows)
            point_error=max(r['cpu_point_error'] for r in rows[len(rays):])
            interval_failures=sum(not(r['interval'][0]-2e-6<=r['gpu_full_source']<=r['interval'][1]+2e-6) for r in rows[len(rays):]) if policy=='source' else None
            if exhausted or point_error>2e-6 or interval_failures:raise RuntimeError(f'{name}/{policy}: camera/point gate failed {exhausted} {point_error} {interval_failures}')
            elevation=math.radians(cfg['sun-elevation']);azimuth=math.radians(cfg['sun-azimuth'])
            light=[math.cos(elevation)*math.sin(azimuth),math.sin(elevation),-math.cos(elevation)*math.cos(azimuth)]
            shadows=[dict(kind='ray',origin=[q['origin'][i]+q['direction'][i]*r['t']+r['normal'][i]*H*2 for i in range(3)],direction=light) for q,r in hits]
            shadow=dict(case,queries=shadows)
            shadow_run,shadow_result=query(args,manifest([shadow]),name+'-shadow-'+policy,policy)
            shadow_rows=shadow_result['cases'][0]['results'];shadow_exhausted=sum(r['status']==2 for r in shadow_rows)
            if shadow_exhausted:raise RuntimeError('shadow traversal exhausted')
            inp=args.output/f'{name}-camera-input.json'
            prefix=args.output/f'{name}-{policy}-native'
            # Same image with/without the compute geometry payload; all shaders
            # use the frozen base and the same complete source for materials.
            flags=native(args,inp,prefix.with_suffix('.json'),policy,['--frames','1','--output',str(prefix.with_suffix('.png'))],(320,180),name)
            invoke(flags,prefix,timeout=300)
            probe=flags.copy();probe[probe.index('--output')+1]=str(prefix.with_suffix('.validated.png'))
            probe+=['--quality-output',str(prefix.with_suffix('.bin'))]
            invoke(probe,Path(str(prefix)+'-probe'),timeout=300)
            receipt['runs'].append(dict(subject=name,policy=policy,camera=run,shadow=shadow_run,
                camera_rays=len(rays),camera_hits=len(hits),points=len(points),cpu_point_max_error=point_error,
                interval_failures=interval_failures,exhausted=exhausted,shadow_rays=len(shadows),shadow_exhausted=shadow_exhausted,
                native_command=flags,probe_command=probe,image=prefix.with_suffix('.png').name,
                validated_image=prefix.with_suffix('.validated.png').name,geometry=prefix.with_suffix('.bin').name))
            save(args.output/'qualification.json',receipt);print(name,policy,'qualified traversal',flush=True)
    receipt['complete']=True;save(args.output/'qualification.json',receipt)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase',choices=['geometry','prepare','timing','captures','qualify'])
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--inputs',type=Path)
    p.add_argument('--binary',type=Path,default=ROOT/'build/release/projects/sdf/cubey_project_sdf_live_edit_study')
    p.add_argument('--public-binary',type=Path,default=ROOT/'outputs/sdf/live-edit-loop-20261030/baseline-sdf')
    args=p.parse_args();args.output=args.output.resolve();args.binary=args.binary.resolve();args.public_binary=args.public_binary.resolve()
    args.output.mkdir(parents=True,exist_ok=False)
    globals()[args.phase](args)


if __name__=='__main__':main()
