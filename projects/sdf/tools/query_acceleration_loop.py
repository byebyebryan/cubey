#!/usr/bin/env python3
"""Bounded selected-shader workload qualification; GPU work stays serial."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import struct
import subprocess
from acceleration_loop import save, gpu_state
from benchmark_tracing import percentile
from investigate_guardian import ROOT, digest, invoke, metadata
from check_tracing_images import pixels
from tracing_comparison import check, document, ray, unit


def summarize(path):
    data = path.read_bytes()
    width, height, stride, count = struct.unpack_from('<4I', data)
    if len(data) != 16+count*256 or len(data)+width*height*8>32*1024**2 or stride!=8:
        raise RuntimeError('workload layout/budget mismatch')
    totals=[[0]*4 for _ in range(3)];walk=[[0]*4 for _ in range(3)]
    hist=[0]*8;stats=[0]*4;hits=exhausted=0;tails=[[] for _ in range(3)]
    maxrun=[];bad=0;guarded=[0]*4
    for i in range(count):
        offset=16+i*256;u=struct.unpack_from('<64I',data,offset)
        f=struct.unpack_from('<16f',data,offset+36*4)
        if u[54]!=i or u[55]!=0x51575231 or not all(math.isfinite(v) for v in f):
            raise RuntimeError('incomplete/nonfinite workload row')
        hits+=f[1]==1;exhausted+=f[3]!=0
        bad+=f[1] not in (0,1,2)
        for phase in range(3):
            for k in range(4):
                if k==2:totals[phase][k]=max(totals[phase][k],u[phase*4+k])
                else:totals[phase][k]+=u[phase*4+k]
                walk[phase][k]+=u[12+phase*4+k]
            tails[phase].append(u[phase*4])
        for k in range(8):hist[k]+=u[24+k]
        for k in (0,1,3):stats[k]+=u[32+k]
        stats[2]=max(stats[2],u[34]);maxrun.append(u[34])
        for k in range(4):guarded[k]+=u[56+k]
    if bad or exhausted or totals[1][0]!=hits*6:
        raise RuntimeError('hit/status/normal-query workload invariant')
    return dict(width=width,height=height,sampled_camera_rays=count,stride=stride,
        storage_bytes=len(data),raster_uv_bytes=width*height*8,sha256=digest(path),hits=hits,exhausted=exhausted,
        phases={name:dict(exact_queries=t[0],csg_instructions=t[1],max_program=t[2],
            root_fallback_queries=t[3],iterations=w[0],bound_queries=w[1],far_advances=w[2],
            mean_program=t[1]/t[0] if t[0] else 0,
            queries_per_camera_ray_p95=percentile(tail,.95),queries_per_camera_ray_max=max(tail))
            for name,t,w,tail in zip(('primary','normal','shadow'),totals,walk,tails)},
        primary_exact_run_histogram=dict(zip(('1','2','3-4','5-8','9-16','17-32','33-64','65+'),hist)),
        required_shadow_rays=stats[0],skipped_shadow_calls=stats[1],max_primary_exact_run=stats[2],
        primary_exact_runs=stats[3],primary_max_run_per_camera_ray_p95=percentile(maxrun,.95),
        guarded=dict(zip(('proposed','accepted','rejected','accepted_cached'),guarded)),
        mean_primary_run_length=totals[0][0]/stats[3] if stats[3] else 0,
        scope='Actual shared shader functions; sampled query counts, not GPU time attribution')


def workload(args):
    receipt=dict(metadata(args.binary),runner_sha256=digest(Path(__file__)),runs=[])
    for scene,preset in [('guardian','sculpture-guardian-limestone'),('robot','survey-robot-fieldkit')]:
        config=json.loads((ROOT/f'projects/sdf/presets/{preset}.json').read_text())
        for view,offset in [('preset',0),('side',math.pi/2),('rear',math.pi)]:
            for mode in ['shadow','combined']:
                prefix=args.output/f'{scene}-{view}-{mode}'
                base=[str(args.binary),'--headless','--require-validation','--supersample','1',
                    '--config',f'projects/sdf/presets/{preset}.json','--width','1920','--height','1080',
                    '--surface-trace','analytic','--csg-evaluation','pruned','--region-axis','64',
                    '--acceleration-variant','root-far','--source-optimization',mode,
                    '--yaw',str(config['yaw']+offset),'--frames','1','--query-stepping',args.query_stepping]
                native=prefix.with_suffix('.native.png');replay=prefix.with_suffix('.replay.png')
                invoke(base+['--output',str(native)],Path(str(prefix)+'-native'),timeout=180)
                raw=prefix.with_suffix('.bin')
                command=base+['--output',str(replay),'--query-workload-output',str(raw)]
                invoke(command,Path(str(prefix)+'-replay'),timeout=180)
                identical=digest(native)==digest(replay)
                difference={}
                if not identical:
                    _,_,ca,a=pixels(native);_,_,cb,b=pixels(replay)
                    differences=[abs(x-y) for ra,rb in zip(a,b) for x,y in zip(ra,rb)]
                    difference=dict(changed_channels=sum(d!=0 for d in differences),max_channel=max(differences))
                row=dict(scene=scene,view=view,mode=mode,command=command,
                    native_image_sha256=digest(native),validated_image_sha256=digest(replay),
                    native_image_identical=identical,difference=difference,workload=summarize(raw))
                receipt['runs'].append(row);save(args.output/'workload.json',receipt)
                print(prefix.name,'parity',identical,'queries',row['workload']['phases']['primary']['exact_queries'],flush=True)
                if not identical:raise RuntimeError('native replay parity failed; inspect before continuing')
    receipt['complete']=True;save(args.output/'workload.json',receipt)


def step_contracts(args):
    frozen_path=ROOT/'projects/sdf/comparison/fixtures.json'
    frozen=json.loads(frozen_path.read_text())
    cases=[]
    theta=.7;n=[math.sin(theta),0,math.cos(theta)]
    rotation=[0,math.sin(theta/2),0,math.cos(theta/2)]
    for thickness in [.00008,.0004,.002]:
        root=dict(name='paired-oblique-slabs',type='union',children=[
            dict(name='front',type='box',half_extent=[.2,.15,thickness],rotation_xyzw=rotation,
                 position=[v*.12 for v in n]),
            dict(name='back',type='box',half_extent=[.2,.15,thickness],rotation_xyzw=rotation,
                 position=[-v*.12 for v in n])])
        queries=[ray([v*.7 for v in n],[-v for v in n],.58-thickness,n),
                 ray([v*.12 for v in n],n,thickness,n)]
        for lateral in [-.95,.95]:
            local=unit([lateral,0,-.25])
            direction=[math.cos(theta)*local[0]+math.sin(theta)*local[2],0,
                       -math.sin(theta)*local[0]+math.cos(theta)*local[2]]
            target=[v*(.12+thickness) for v in n]
            queries.append(ray([x-.2*d for x,d in zip(target,direction)],direction,.2,n))
        cases.append(dict(name='paired-thin-'+str(thickness),scene=document(root),queries=queries))
    thin=dict(format=frozen['format'],version=frozen['version'],contracts=frozen['contracts'],cases=cases)
    thin_path=args.output/'thin-contracts.json';save(thin_path,thin)
    inputs=[('frozen',frozen_path),('thin',thin_path)]
    if args.supplemental:inputs.append(('supplemental',args.supplemental.resolve()))
    receipt=dict(metadata(args.binary),runner_sha256=digest(Path(__file__)),runs=[])
    for mode in ['shadow','combined']:
        for name,path in inputs:
            result=args.output/f'{mode}-{name}.json'
            command=[str(args.binary),'--headless','--headless-render-only','--require-validation',
                '--width','32','--height','32','--frames','1','--surface-trace','analytic',
                '--csg-evaluation','pruned','--region-axis','64','--acceleration-variant','root-far',
                '--source-optimization',mode,'--query-stepping',args.query_stepping,
                '--comparison-input',str(path),'--comparison-output',str(result)]
            invoke(command,args.output/f'{mode}-{name}',timeout=180)
            score=check(json.loads(path.read_text()),json.loads(result.read_text()))
            receipt['runs'].append(dict(mode=mode,input=str(path),input_sha256=digest(path),
                command=command,result_sha256=digest(result),score=score))
            save(args.output/'contracts.json',receipt)
            print(mode,name,args.query_stepping,score,flush=True)
            if not score['passed']:raise RuntimeError('guarded numerical contract failed')
    receipt['complete']=True;save(args.output/'contracts.json',receipt)


STEPS=['ordinary','relaxed-1.2','relaxed-1.5','adaptive-0.5']


def native_command(args,scene,view,mode,step):
    preset='sculpture-guardian-limestone' if scene=='guardian' else 'survey-robot-fieldkit'
    config=json.loads((ROOT/f'projects/sdf/presets/{preset}.json').read_text())
    offset=dict(preset=0,side=math.pi/2,rear=math.pi)[view]
    return [str(args.binary),'--headless','--require-validation','--supersample','1',
        '--config',f'projects/sdf/presets/{preset}.json','--width','1920','--height','1080',
        '--surface-trace','analytic','--csg-evaluation','pruned','--region-axis','64',
        '--acceleration-variant','root-far','--source-optimization',mode,'--query-stepping',step,
        '--yaw',str(config['yaw']+offset)]


def image_difference(a,b):
    # Decode exact 8-bit channels with the installed native image tool.
    raw=[subprocess.check_output(['magick',str(p),'-depth','8','RGB:-']) for p in [a,b]]
    aa,bb=raw
    if len(aa)!=len(bb):raise RuntimeError('image extent mismatch')
    diffs=[abs(x-y) for x,y in zip(aa,bb)]
    return dict(changed_pixels=sum(aa[i:i+3]!=bb[i:i+3] for i in range(0,len(aa),3)),
        max_channel=max(diffs),mean_channel=sum(diffs)/len(diffs),
        magenta_pixels=sum(aa[i:i+3]==bytes([241,0,241]) for i in range(0,len(aa),3)),
        candidate_magenta_pixels=sum(bb[i:i+3]==bytes([241,0,241]) for i in range(0,len(bb),3)))


def capture(args):
    receipt=dict(metadata(args.binary),runner_sha256=digest(Path(__file__)),runs=[],
        scope='Full-quality study captures; differences retained, no golden refresh or visual adoption')
    for scene in ['guardian','robot']:
        for view in ['preset','side','rear']:
            for mode in ['shadow','combined']:
                control=None
                for step in STEPS:
                    label=step.replace('.', '_')
                    prefix=args.output/f'{scene}-{view}-{mode}-{label}'
                    image=Path(str(prefix)+'.png')
                    command=native_command(args,scene,view,mode,step)+['--frames','1','--output',str(image)]
                    invoke(command,prefix,timeout=180)
                    if step=='ordinary':control=image
                    diff=image_difference(control,image)
                    row=dict(scene=scene,view=view,mode=mode,step=step,command=command,
                        image_sha256=digest(image),difference=diff)
                    receipt['runs'].append(row);save(args.output/'captures.json',receipt)
                    print(prefix.name,diff['changed_pixels'],diff['max_channel'],flush=True)
                    if diff['candidate_magenta_pixels'] or (scene=='robot' and diff['changed_pixels']):
                        raise RuntimeError('capture exhaustion or multi-material fallback changed')
    receipt['complete']=True;save(args.output/'captures.json',receipt)


def timing(args):
    q=json.loads((args.qualification/'captures.json').read_text())
    if not q.get('complete') or q['binary_sha256']!=digest(args.binary):
        raise RuntimeError('timing requires completed captures from this binary')
    covered=set()
    for path in args.numerical or []:
        numerical=json.loads(path.read_text())
        if not numerical.get('complete'):raise RuntimeError('incomplete numerical receipt')
        for row in numerical['runs']:
            if not row['score']['passed']:raise RuntimeError('failed numerical receipt')
            command=row['command'];covered.add(command[command.index('--query-stepping')+1])
            output=Path(command[command.index('--comparison-output')+1])
            result=json.loads(output.read_text())
            if digest(args.binary.parent/'shaders'/result['query_shader'])!=result['query_shader_sha256']:
                raise RuntimeError('numerical kernel changed since qualification')
    if covered!=set(STEPS[1:]):raise RuntimeError('all three numerical candidates required')
    controlled=json.loads(args.controlled.read_text()) if args.controlled else {}
    if not controlled.get('passed') or len(controlled['cases'])!=21:
        raise RuntimeError('controlled prefix/endpoint/budget receipt required')
    receipt=dict(metadata(args.binary),runner_sha256=digest(Path(__file__)),runs=[],frames=180,warmup=30,
        scope='Research timings; numerical qualification and image adoption are distinct')
    cases=[(scene,view,mode,step) for scene in ['guardian','robot'] for view in ['preset','side','rear']
        for mode in ['shadow','combined'] for step in STEPS]
    for repeat in [1,2,3]:
        for scene,view,mode,step in cases if repeat%2 else reversed(cases):
            label=step.replace('.', '_')
            prefix=args.output/f'{scene}-{view}-{mode}-{label}-r{repeat}'
            command=native_command(args,scene,view,mode,step)+['--headless-render-only','--frames','180',
                '--profile-warmup-frames','30','--profile-output',str(prefix)]
            before=gpu_state();wall=invoke(command,prefix,timeout=180);after=gpu_state()
            groups={};indices={}
            for row in csv.DictReader(Path(str(prefix)+'.passes.csv').open()):
                if row['kind']=='gpu':
                    groups.setdefault(row['label'],[]).append(float(row['duration_ms']))
                    indices.setdefault(row['label'],[]).append(int(row['frame_index']))
            if len(groups.get('sdf.whole_scene',[]))!=150 or any(sorted(v)!=list(range(30,180)) for v in indices.values()):
                raise RuntimeError('incomplete GPU timestamp coverage')
            metrics=list(csv.DictReader(Path(str(prefix)+'.metrics.csv').open()))
            selection={r['name']:float(r['value']) for r in metrics if r['category']=='sdf.shader_selection'}
            expected=0 if scene=='robot' or step=='ordinary' else dict(zip(STEPS[1:],[12,15,50]))[step]
            if selection['guarded_stepping']!=expected or selection['pipeline_count']!=3 or selection['route']!=(2 if scene=='guardian' else 1):
                raise RuntimeError('timing did not bind requested candidate')
            receipt['runs'].append(dict(scene=scene,view=view,mode=mode,step=step,repeat=repeat,command=command,
                gpu_before=before,gpu_after=after,wall_seconds=wall,selection=selection,
                passes={k:dict(samples=len(v),median_ms=statistics.median(v),p95_ms=percentile(v,.95)) for k,v in groups.items()}))
            save(args.output/'timing.json',receipt)
            print(prefix.name,round(receipt['runs'][-1]['passes']['sdf.whole_scene']['p95_ms'],4),flush=True)
    receipt['complete']=True;save(args.output/'timing.json',receipt)


def publication(args):
    from qualify_source_query import history
    args.resume=False
    history(args)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase',choices=['workload','contracts','capture','timing','publication'])
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--binary',type=Path,default=ROOT/'build/release/projects/sdf/sdf')
    p.add_argument('--query-stepping',default='ordinary',choices=['ordinary','relaxed-1.2','relaxed-1.5','adaptive-0.5'])
    p.add_argument('--supplemental',type=Path)
    p.add_argument('--qualification',type=Path)
    p.add_argument('--numerical',type=Path,nargs='+')
    p.add_argument('--controlled',type=Path)
    args=p.parse_args();args.binary=args.binary.resolve();args.output=args.output.resolve()
    args.output.mkdir(parents=True,exist_ok=False)
    dict(workload=workload,contracts=step_contracts,capture=capture,timing=timing,publication=publication)[args.phase](args)


if __name__=='__main__':main()
