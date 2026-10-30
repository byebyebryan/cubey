#!/usr/bin/env python3
"""Serial, source-pinned cached/source quality comparison. Fresh phase outputs."""
import argparse
import copy
import csv
import json
import math
from pathlib import Path
import statistics
import subprocess
import time

from acceleration_loop import gpu_state, save
from benchmark_tracing import percentile
from investigate_guardian import ROOT, PRESETS, digest, invoke, metadata
from tracing_comparison import check

VIEWS = {'preset': 0, 'side': math.pi/2, 'rear': math.pi}
ROUTES = {
    'sampled': ['--surface-trace','sampled','--csg-evaluation','full'],
    'source': ['--surface-trace','analytic','--csg-evaluation','pruned',
               '--region-axis','64','--acceleration-variant','root-far',
               '--source-optimization','combined'],
    'adaptive': ['--surface-trace','analytic','--csg-evaluation','pruned',
                 '--region-axis','64','--acceleration-variant','root-far',
                 '--source-optimization','combined','--query-stepping','adaptive-0.5'],
    'hybrid': ['--surface-trace','hybrid','--csg-evaluation','pruned',
               '--region-axis','64','--acceleration-variant','unclamped'],
}
for _name,_mode in [('refine05','refine-0.05'),('refine03125','refine-0.03125'),
                    ('normal','source-normal'),('normal-zero','source-normal-zero'),('combined','refine-normal')]:
    ROUTES[_name]=['--surface-trace','sampled','--csg-evaluation','full',
                  '--region-axis','64','--cached-quality',_mode]


def command(args, scene, view, route, width=1920, height=1080):
    config=json.loads((ROOT/f'projects/sdf/presets/{PRESETS[scene]}.json').read_text())
    yaw=config['yaw']+VIEWS.get(view,0)
    distance={'close':.8,'distant':2.4}.get(view,config['distance'])
    return [str(args.binary),'--config',f'projects/sdf/presets/{PRESETS[scene]}.json',
        '--headless','--require-validation','--motion','static','--supersample','1',
        '--width',str(width),'--height',str(height),'--yaw',str(yaw),'--distance',str(distance),
        '--shadows',str(args.shadows),'--no-ao','--no-ground','--no-clouds','--no-occluder',*ROUTES[route]]


def profile(prefix, frames, warmup):
    rows=list(csv.DictReader(prefix.with_suffix('.passes.csv').open()))
    groups={}
    for row in rows:
        if row['kind']=='gpu':groups.setdefault(row['label'],[]).append(row)
    result={}
    for name,group in groups.items():
        indices=[int(row['frame_index']) for row in group]
        if sorted(indices)!=list(range(warmup,frames)):
            raise RuntimeError(f'{prefix}: incomplete/duplicate frame indices for {name}')
        values=[float(row['duration_ms']) for row in group]
        result[name]=dict(samples=len(values),median_ms=statistics.median(values),
            p95_ms=percentile(values,.95),values_ms=values,frame_indices=indices)
    if 'sdf.whole_scene' not in result:raise RuntimeError('missing whole-scene samples')
    metrics=list(csv.DictReader(prefix.with_suffix('.metrics.csv').open()))
    categories={}
    for row in metrics:
        categories.setdefault(row['category'],{})[row['name']]=float(row['value'])
    return result,categories


def timing(args):
    receipt=dict(metadata(args.binary),tool_sha256=digest(Path(__file__)),runs=[],complete=False)
    frames,warmup=(12,2) if args.pilot else (180,30)
    cases=[(s,v,r) for s in args.scenes for v in args.views for r in args.routes]
    for repeat in range(1,2 if args.pilot else 4):
        for scene,view,route in cases if repeat%2 else reversed(cases):
            prefix=args.output/f'{scene}-{view}-{route}-r{repeat}'
            flags=command(args,scene,view,route)+['--headless-render-only','--frames',str(frames),
                '--profile-warmup-frames',str(warmup),'--profile-output',str(prefix)]
            before=gpu_state();wall=invoke(flags,prefix,timeout=180);after=gpu_state()
            passes,metrics=profile(prefix,frames,warmup)
            receipt['runs'].append(dict(scene=scene,view=view,route=route,repeat=repeat,
                command=flags,wall_seconds=wall,gpu_before=before,gpu_after=after,
                passes=passes,metrics=metrics,passes_sha256=digest(prefix.with_suffix('.passes.csv'))))
            save(args.output/'timing.json',receipt)
            print(prefix.name,round(passes['sdf.whole_scene']['p95_ms'],3),flush=True)
    receipt['complete']=True;save(args.output/'timing.json',receipt)


def capture(args):
    receipt=dict(metadata(args.binary),tool_sha256=digest(Path(__file__)),runs=[],complete=False)
    if args.resume:
        receipt=json.loads((args.output/'captures.json').read_text())
        if receipt['binary_sha256']!=digest(args.binary):raise RuntimeError('resume binary differs')
        receipt.setdefault('resume_tool_hashes',[]).append(digest(Path(__file__)))
    for scene in args.scenes:
        for view in args.views:
            for route in args.routes:
                prefix=args.output/f'{scene}-{view}-{route}'
                previous=next((r for r in receipt['runs'] if (r['scene'],r['view'],r['route'])==(scene,view,route)),None)
                if previous:
                    if digest(prefix.with_suffix('.png'))!=previous['image_sha256']:raise RuntimeError('resume image differs')
                    if args.probe:
                        for suffix,key in [('.bin','geometry_sha256'),('.valid.png','validity_sha256')]:
                            if digest(prefix.with_suffix(suffix))!=previous[key]:raise RuntimeError('resume diagnostic differs')
                    continue
                flags=command(args,scene,view,route)+['--frames','1','--output',str(prefix.with_suffix('.png'))]
                wall=invoke(flags,Path(str(prefix)+'-native') if args.probe else prefix,timeout=180)
                row=dict(scene=scene,view=view,route=route,command=flags,wall_seconds=wall,
                    image_sha256=digest(prefix.with_suffix('.png')))
                if args.probe:
                    import numpy as np
                    from PIL import Image
                    validated=Path(str(prefix)+'-validated.png')
                    probe=flags.copy();probe[probe.index('--output')+1]=str(validated)
                    probe+=['--quality-output',str(prefix.with_suffix('.bin'))]
                    invoke(probe,prefix,timeout=180)
                    a=np.array(Image.open(prefix.with_suffix('.png')).convert('RGB'))
                    b=np.array(Image.open(validated).convert('RGB'))
                    different=np.any(a!=b,axis=-1)
                    markers=different&np.any(b>250,axis=-1)&(np.sum(b<5,axis=-1)>=2)
                    invalid=different
                    if int(invalid.sum())>64:raise RuntimeError('native diagnostic differs at over 64 pixels; investigate')
                    Image.fromarray(np.where(invalid,0,255).astype(np.uint8)).save(prefix.with_suffix('.valid.png'))
                    ys,xs=np.nonzero(invalid)
                    row.update(probe_command=probe,geometry_sha256=digest(prefix.with_suffix('.bin')),
                        validity_sha256=digest(prefix.with_suffix('.valid.png')),
                        validated_image_sha256=digest(validated),
                        unscored_pixels=int(invalid.sum()),
                        validation_marker_pixels=int(markers.sum()),
                        image_rounding_unscored_pixels=int((different&~markers).sum()),
                        validation_flags=[dict(x=int(x),y=int(y),rgb=b[y,x].tolist()) for y,x in zip(ys,xs)],
                        regular_depth_limit=4/(256*7*32),normal_degrees_limit=1,
                        native_image_parity_on_scored_pixels=True,
                        qualification_scope='Only pixels passing native depth/status/normal checks; flagged pixels remain unscored')
                receipt['runs'].append(row);save(args.output/'captures.json',receipt)
                print(prefix.name,'captured','unscored',row.get('unscored_pixels',0),flush=True)
    receipt['complete']=True;save(args.output/'captures.json',receipt)


def contracts(args):
    original=json.loads((ROOT/'projects/sdf/comparison/fixtures.json').read_text())
    names=['sphere-control','subgrid-sphere','aligned-small-sphere','box-23-0.00027901786',
           'box-45-0.00027901786','thin-stick-0.5h','thin-stick-1.5h','thin-stick-4h',
           'subgrid-cavity','crossed-thin-sticks','workspace-clip','sphere-tangent']
    selected=dict(original,cases=[c for c in original['cases'] if c['name'] in names])
    save(args.output/'fixtures.json',selected)
    receipt=dict(metadata(args.binary),tool_sha256=digest(Path(__file__)),runs=[],complete=False)
    for route in args.routes:
        manifest=ROOT/'projects/sdf/comparison/fixtures.json' if route=='source' else args.output/'fixtures.json'
        prefix=args.output/route
        flags=[str(args.binary),'--headless','--headless-render-only','--require-validation','--motion','static',
               '--frames','1','--width','32','--height','32',*ROUTES[route],
               '--comparison-input',str(manifest),'--comparison-output',str(prefix.with_suffix('.json'))]
        wall=invoke(flags,prefix,timeout=180)
        result=json.loads(prefix.with_suffix('.json').read_text());fixture=json.loads(manifest.read_text())
        score=check(fixture,result) if route=='source' else approximate_score(fixture,result)
        row=dict(route=route,command=flags,wall_seconds=wall,source_geometry_score=score,
                 result_sha256=digest(prefix.with_suffix('.json')),input_sha256=digest(manifest))
        if route=='source' and not score['passed']:raise RuntimeError('source oracle contracts failed')
        if route!='source':
            ray_rows=[r for c in result['cases'] for r in c['results'] if 'status' in r]
            if any(r['status']==2 for r in ray_rows):raise RuntimeError('candidate exhausted in fixture')
        receipt['runs'].append(row);save(args.output/'contracts.json',receipt)
        print(route,'source score', {k:score[k] for k in ('passed','scored','rays','matched_hits','missed_source_hits','outside_source_depth_contract') if k in score},flush=True)
    receipt['complete']=True;save(args.output/'contracts.json',receipt)


def approximate_score(fixture,result):
    errors=[];missed=[];false=[];angles=[];outside=[];rays=0
    for c,actual in zip(fixture['cases'],result['cases']):
        if c['name']!=actual['name']:raise RuntimeError('fixture identity differs')
        for i,(q,r) in enumerate(zip(c['queries'],actual['results'])):
            if q['kind']!='ray':continue
            rays+=1;key=[c['name'],i];expected=q['expected_t']
            if expected is None:
                if r['status']==1:false.append(key)
            elif r['status']!=1:missed.append(key)
            else:
                error=abs(r['t']-expected);errors.append(error)
                if error>q.get('depth_tolerance',fixture['contracts']['regular_depth_tolerance']):outside.append(key)
                n=q.get('expected_normal')
                if n is not None:
                    angles.append(math.degrees(math.acos(max(-1,min(1,sum(a*b for a,b in zip(n,r['normal'])))))))
    return dict(scope='Approximation measured against source answers; frozen exact-source scorer does not score sampled modes',
        rays=rays,matched_hits=len(errors),missed_source_hits=missed,false_source_hits=false,
        outside_source_depth_contract=outside,depth_errors_world=errors,normal_errors_degrees=angles)


def sequence(args):
    receipt=dict(metadata(args.binary),tool_sha256=digest(Path(__file__)),runs=[],complete=False)
    original=json.loads((args.input/'sequences.json').read_text()) if args.probe else None
    for scene in args.scenes:
        config=json.loads((ROOT/f'projects/sdf/presets/{PRESETS[scene]}.json').read_text())
        document=json.loads((ROOT/config['authored-scene']).read_text())
        for movement in ('arc','dolly'):
            poses=[]
            for i in range(24):
                phase=i/23*2-1
                poses.append(dict(yaw=config['yaw']+(.006*phase if movement=='arc' else 0),
                    pitch=config['pitch'],distance=config['distance'] if movement=='arc' else .8+.02*phase))
            path=args.output/f'{scene}-{movement}-actions.json'
            actions=[dict(label=f'{movement}-{i}',scene=document,visible_scene=document,
                          geometry_unchanged=True,render=pose) for i,pose in enumerate(poses)]
            save(path,dict(format='cubey.sdf.action-study',version=1,capture_frames=True,actions=actions))
            for route in args.routes:
                prefix=args.output/f'{scene}-{movement}-{route}'
                flags=command(args,scene,'preset',route)+['--frames','24','--headless-render-only',
                    '--comparison-actions',str(path),'--comparison-output',str(prefix.with_suffix('.json'))]
                if args.probe:flags+=['--quality-output',str(prefix.with_suffix('.bin'))]
                invoke(flags,prefix,timeout=180)
                actual=json.loads(prefix.with_suffix('.json').read_text())['actions']
                if len(actual)!=24:raise RuntimeError('incomplete pose sequence')
                for i,row in enumerate(actual):
                    if row['frame']!=i or row['failed'] or row['field_builds'] or row['region_builds'] or row['cone_build_delta']:
                        raise RuntimeError('pose sequence changed geometry or cache')
                    for k,v in poses[i].items():
                        if abs(row['camera'][k]-v)>2e-7:raise RuntimeError('camera pose receipt differs')
                    row['image_sha256']=digest(Path(row['frame_png']))
                    if args.probe:
                        import numpy as np
                        from PIL import Image
                        native=next(r for r in original['runs'] if (r['scene'],r['movement'],r['route'])==(scene,movement,route))['frames'][i]
                        a=np.array(Image.open(native['frame_png']).convert('RGB'));b=np.array(Image.open(row['frame_png']).convert('RGB'))
                        invalid=np.any(a!=b,axis=-1)
                        if int(invalid.sum())>64:raise RuntimeError('pose diagnostic differs at over 64 pixels')
                        geometry_file=Path(row['quality_file'])
                        valid_file=geometry_file.with_suffix('.valid.png')
                        Image.fromarray(np.where(invalid,0,255).astype(np.uint8)).save(valid_file)
                        row.update(validated_png=row['frame_png'],frame_png=native['frame_png'],
                            native_image_sha256=digest(Path(native['frame_png'])),geometry_sha256=digest(geometry_file),
                            validity_file=str(valid_file),unscored_pixels=int(invalid.sum()))
                        row['validated_image_sha256']=row['image_sha256']
                        row['image_sha256']=row['native_image_sha256']
                receipt['runs'].append(dict(scene=scene,movement=movement,route=route,command=flags,
                    poses=poses,frames=actual,scope='Native static camera poses with retained caches; not interactive timing'))
                save(args.output/'sequences.json',receipt)
                print(scene,movement,route,'24 native frames; no geometry rebuilds',flush=True)
    receipt['complete']=True;save(args.output/'sequences.json',receipt)


def history(args):
    scene=dict(format='cubey.sdf.scene',version=2,name='Cached quality history',
        materials=[dict(name='body',base_color=[.5,.4,.3],roughness=.6)],
        root=dict(type='union',name='root',children=[
            dict(type='round-cone',name='left',bottom_radius=.08,top_radius=.03,half_length=.15,position=[-.12,0,0],material='body'),
            dict(type='round-cone',name='right',bottom_radius=.06,top_radius=.02,half_length=.12,position=[.12,0,0],material='body')]),
        history=[dict(node='left',label='Left',seconds=1),dict(node='right',label='Right',seconds=1)])
    branch=copy.deepcopy(scene);branch['root']['children'][0]['bottom_radius']=.09
    appearance=copy.deepcopy(branch);appearance['materials'][0]['base_color']=[.2,.5,.4]
    states=[('empty',scene,0),('first',scene,1),('second',scene,2),('undo',scene,1),('redo',scene,2),
            ('branch',branch,1),('appearance',appearance,1),('failed',appearance,2),('retry',appearance,2)]
    save(args.output/'initial.json',scene)
    receipt=dict(metadata(args.binary),tool_sha256=digest(Path(__file__)),runs=[],complete=False)
    for route in args.routes:
        actions=[]
        for label,document,prefix in states:
            action=dict(label=label,scene=document,history_prefix=prefix,visible_scene=document,
                visible_history_prefix=prefix,geometry_unchanged=label in ('appearance','failed'))
            if label=='failed':action.update(visible_history_prefix=1,expect_failure=True,fault='region',
                render=dict(yaw=.9,pitch=.4,distance=1.2))
            actions.append(action)
        path=args.output/(route+'-actions.json');save(path,dict(format='cubey.sdf.action-study',version=1,actions=actions,capture_frames=True))
        prefix=args.output/route
        flags=[str(args.binary),'--headless','--headless-render-only','--require-validation','--motion','static',
               '--width','320','--height','180','--supersample','1','--frames','9',*ROUTES[route],
               '--authored-scene',str(args.output/'initial.json'),'--comparison-actions',str(path),
               '--comparison-output',str(prefix.with_suffix('.json'))]
        invoke(flags,prefix,timeout=180)
        rows=json.loads(prefix.with_suffix('.json').read_text())['actions']
        for a,b in [(1,3),(2,4),(6,7)]:
            if digest(Path(rows[a]['frame_png']))!=digest(Path(rows[b]['frame_png'])):
                raise RuntimeError('undo/redo/failed-camera retention differs')
        if any(r['camera']!=rows[0]['camera'] for r in rows):raise RuntimeError('failed camera edit was not restored')
        expected=[1,1,1,1,1,1,0,0,1]
        normal=route in ('normal','normal-zero','combined')
        if [r['region_builds'] for r in rows]!=(expected if normal or route=='source' else [0]*9):
            raise RuntimeError('region generations did not follow geometry')
        if [r['field_builds'] for r in rows]!=(expected if route!='source' else [0]*9):
            raise RuntimeError('field generations did not follow geometry')
        receipt['runs'].append(dict(route=route,command=flags,actions=rows,undo_redo_failure_pixels_identical=True))
        save(args.output/'history.json',receipt);print(route,'nine history/camera-recovery frames pass',flush=True)
    receipt['complete']=True;save(args.output/'history.json',receipt)


def publication(args):
    receipt=dict(metadata(args.binary),tool_sha256=digest(Path(__file__)),runs=[],complete=False)
    for scene in args.scenes:
        config=json.loads((ROOT/f'projects/sdf/presets/{PRESETS[scene]}.json').read_text())
        original=json.loads((ROOT/config['authored-scene']).read_text());edited=copy.deepcopy(original)
        def alter(node):
            if node.get('type') in ('round-cone','box','sphere'):
                key={'round-cone':'bottom_radius','box':'half_extent','sphere':'radius'}[node['type']]
                if key=='half_extent':node[key][0]*=1.01
                else:node[key]*=1.01
                return True
            return any(alter(c) for c in node.get('children',[]))
        if not alter(edited['root']):raise RuntimeError('no editable primitive')
        appearance=copy.deepcopy(original);appearance['materials'][0]['base_color']=[.25,.4,.35]
        actions=[dict(label='initial',scene=original,visible_scene=original,geometry_unchanged=True),
            dict(label='edit',scene=edited,visible_scene=edited),dict(label='undo',scene=original,visible_scene=original),
            dict(label='redo',scene=edited,visible_scene=edited),
            dict(label='failed',scene=original,visible_scene=edited,fault='region',expect_failure=True,geometry_unchanged=True),
            dict(label='retry',scene=original,visible_scene=original),
            dict(label='appearance',scene=appearance,visible_scene=appearance,geometry_unchanged=True)]
        path=args.output/(scene+'-actions.json');save(path,dict(format='cubey.sdf.action-study',version=1,actions=actions))
        for repeat in range(1,4):
            routes=args.routes if repeat%2 else list(reversed(args.routes))
            for route in routes:
                prefix=args.output/f'{scene}-{route}-r{repeat}'
                flags=command(args,scene,'preset',route)+['--headless-render-only','--frames','7',
                    '--comparison-actions',str(path),'--comparison-output',str(prefix.with_suffix('.json'))]
                before=gpu_state();started=time.time()
                invoke(flags,prefix,timeout=180)
                ended=time.time();after=gpu_state()
                rows=json.loads(prefix.with_suffix('.json').read_text())['actions']
                if len(rows)!=7 or any(r['field_builds'] or r['region_builds'] for r in rows if r['label'] in ('initial','failed','appearance')):
                    raise RuntimeError('publication reuse/retention differs')
                receipt['runs'].append(dict(scene=scene,route=route,repeat=repeat,command=flags,actions=rows,
                    started_unix=started,ended_unix=ended,gpu_before=before,gpu_after=after))
                save(args.output/'publication.json',receipt);print(scene,route,repeat,'publication complete',flush=True)
    receipt['complete']=True;save(args.output/'publication.json',receipt)


def negative(args):
    base=[str(args.binary),'--headless','--headless-render-only','--require-validation','--frames','1',
        '--width','32','--height','32','--motion','static','--surface-trace','sampled',
        '--csg-evaluation','full','--region-axis','64','--cached-quality','refine-normal',
        '--authored-scene','projects/sdf/comparison/initial.json']
    cases=[]
    for name,key,value in [('analytic','--surface-trace','analytic'),('axis16','--region-axis','16'),
                            ('orbit','--motion','orbit')]:
        flags=base.copy();flags[flags.index(key)+1]=value;cases.append((name,flags))
    cases.append(('window',[f for f in base if f not in ('--headless','--headless-render-only')]))
    cases.append(('optical',base+['--lighting','material-study']))
    cases.append(('ao',base+['--ao']))
    flags=base.copy();flags[flags.index('--frames')+1]='2'
    cases.append(('multi-probe',flags+['--quality-output',str(args.output/'invalid.bin')]))
    flags=base.copy();flags[flags.index('--width')+1]='4096';flags[flags.index('--height')+1]='4096'
    cases.append(('probe-cap',flags+['--quality-output',str(args.output/'oversize.bin')]))
    cases.append(('mixed-step',base+['--query-stepping','adaptive-0.5']))
    initial=json.loads((ROOT/'projects/sdf/comparison/initial.json').read_text())
    action=dict(label='pose',scene=initial,visible_scene=initial,geometry_unchanged=True)
    path=args.output/'pose-actions.json';save(path,dict(format='cubey.sdf.action-study',version=1,
        capture_frames=True,actions=[action,action]))
    flags=base.copy();flags[flags.index('--frames')+1]='2'
    cases.append(('pose-profile',flags+['--quality-output',str(args.output/'invalid-pose.bin'),
        '--comparison-actions',str(path),'--comparison-output',str(args.output/'invalid-pose.json'),
        '--profile-output',str(args.output/'invalid-profile')]))
    receipt=dict(metadata(args.binary),runs=[],complete=False)
    for name,flags in cases:
        r=subprocess.run(flags,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20)
        (args.output/(name+'.log')).write_text(r.stdout)
        if not r.returncode or not ('require' in r.stdout or 'exceeds' in r.stdout):
            raise RuntimeError('private admission unexpectedly accepted: '+name)
        receipt['runs'].append(dict(name=name,command=flags,exit_code=r.returncode,output=r.stdout))
    receipt['complete']=True;save(args.output/'negative.json',receipt)
    print(len(cases),'actual CLI rejection checks pass',flush=True)


def hybrid(args):
    receipt=dict(metadata(args.binary),runs=[],skipped=[],complete=False)
    for w,h,guard in [(320,180,12),(960,540,50),(1920,1080,None)]:
        prefix=args.output/f'guardian-hybrid-{w}'
        flags=command(args,'guardian','preset','hybrid',w,h)+['--headless-render-only','--frames','12',
            '--profile-warmup-frames','2','--profile-output',str(prefix)]
        before=gpu_state();invoke(flags,prefix,timeout=180);after=gpu_state()
        passes,metrics=profile(prefix,12,2)
        receipt['runs'].append(dict(width=w,height=h,command=flags,passes=passes,metrics=metrics,
            gpu_before=before,gpu_after=after,scope='Short context pilot, not three-repeat final timing'))
        if guard is not None and passes['sdf.whole_scene']['p95_ms']>guard:
            receipt['skipped'].append(dict(larger_resolutions='Not measured',reason='previous-resolution cost guard'))
            break
        if w==1920:
            flags=command(args,'guardian','preset','hybrid')+['--frames','1','--output',str(args.output/'guardian-hybrid.png')]
            invoke(flags,args.output/'capture',timeout=180)
    receipt['complete']=True;save(args.output/'hybrid.json',receipt)
    print('Hybrid context',[(r['width'],round(r['passes']['sdf.whole_scene']['p95_ms'],3)) for r in receipt['runs']],flush=True)


def roi(args):
    import numpy as np
    from PIL import Image
    frozen=json.loads((ROOT/'projects/sdf/comparison/fixtures.json').read_text())
    cases=[];labels=[]
    for fixture in ('subgrid-sphere','thin-stick-0.5h'):
        source=next(c for c in frozen['cases'] if c['name']==fixture)
        centre=source['scene']['root'].get('position',[0,0,0])
        for pixels in (.5,1,2,4):
            for phase in (0,.5):
                width=64;step=(.5/(256*7))/pixels
                queries=[dict(kind='ray',origin=[centre[0]+(x-32+phase)*step,
                    centre[1]+(32-y-phase)*step,.7],direction=[0,0,-1]) for y in range(width) for x in range(width)]
                name=f'{fixture}-{pixels}-{phase}'
                cases.append(dict(name=name,scene=source['scene'],queries=queries))
                labels.append(dict(name=name,fixture=fixture,nominal_width_pixels=pixels,pixel_phase=phase,
                    query_extent=[64,64],viewport_world=64*step))
    manifest=dict(format='cubey.sdf.tracing-study',version=1,cases=cases)
    path=args.output/'input.json';save(path,manifest)
    receipt=dict(metadata(args.binary),runs=[],labels=labels,complete=False,
        scope='Orthographic geometry-query ROI, not ordinary shaded native camera output; nominal size and pixel phase are explicit')
    for route in args.routes:
        prefix=args.output/route
        flags=[str(args.binary),'--headless','--headless-render-only','--require-validation','--motion','static',
               '--frames','1','--width','32','--height','32',*ROUTES[route],
               '--comparison-input',str(path),'--comparison-output',str(prefix.with_suffix('.json'))]
        invoke(flags,prefix,timeout=180)
        actual=json.loads(prefix.with_suffix('.json').read_text())
        summaries=[]
        for case,label in zip(actual['cases'],labels):
            data=case['results'];status=np.array([r['status'] for r in data]).reshape(64,64)
            if np.any(status==2):raise RuntimeError('ROI exhausted')
            normals=np.array([r['normal'] for r in data]).reshape(64,64,3)
            rgb=np.full((64,64,3),20,dtype=np.uint8)
            rgb[status==1]=np.clip((normals[status==1]*.5+.5)*255,0,255).astype(np.uint8)
            image=args.output/(route+'-'+label['name']+'.png');Image.fromarray(rgb).save(image)
            summaries.append(dict(label,hits=int((status==1).sum()),file=str(image),sha256=digest(image)))
        receipt['runs'].append(dict(route=route,command=flags,cases=summaries,
            result_sha256=digest(prefix.with_suffix('.json')),query_shader_sha256=actual['query_shader_sha256']))
        save(args.output/'roi.json',receipt);print(route,'16 phased feature views complete',flush=True)
    receipt['complete']=True;save(args.output/'roi.json',receipt)


def backgrounds(args):
    sequence_receipt=json.loads((args.input/'sequences.json').read_text())
    receipt=dict(metadata(args.binary),runs=[],complete=False)
    for scene in args.scenes:
        config=json.loads((ROOT/f'projects/sdf/presets/{PRESETS[scene]}.json').read_text())
        document=json.loads((ROOT/config['authored-scene']).read_text())
        groups={r['movement']:r['poses'] for r in sequence_receipt['runs'] if r['scene']==scene}
        groups['calibration']=[dict(yaw=config['yaw']+VIEWS.get(v,0),pitch=config['pitch'],
            distance={'close':.8,'distant':2.4}.get(v,config['distance'])) for v in [*VIEWS,'close','distant']]
        for movement,poses in groups.items():
            actions=[dict(label=f'{movement}-{i}',scene=document,history_prefix=0,visible_scene=document,
                visible_history_prefix=0,geometry_unchanged=i>0,render=pose) for i,pose in enumerate(poses)]
            path=args.output/f'{scene}-{movement}-actions.json'
            save(path,dict(format='cubey.sdf.action-study',version=1,capture_frames=True,actions=actions))
            prefix=args.output/f'{scene}-{movement}'
            flags=command(args,scene,'preset','source')+['--headless-render-only','--frames',str(len(poses)),
                '--comparison-actions',str(path),'--comparison-output',str(prefix.with_suffix('.json'))]
            invoke(flags,prefix,timeout=180)
            rows=json.loads(prefix.with_suffix('.json').read_text())['actions']
            for row in rows:row['image_sha256']=digest(Path(row['frame_png']))
            receipt['runs'].append(dict(scene=scene,movement=movement,poses=poses,frames=rows,command=flags))
            save(args.output/'backgrounds.json',receipt);print(scene,movement,'empty-scene sky captured',flush=True)
    receipt['complete']=True;save(args.output/'backgrounds.json',receipt)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase',choices=['timing','capture','contracts','sequence','history','publication','negative','hybrid','roi','backgrounds'])
    p.add_argument('--binary',type=Path,default=Path('build/release/projects/sdf/sdf'))
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--input',type=Path)
    p.add_argument('--routes',nargs='+',choices=list(ROUTES),default=['sampled','source','adaptive'])
    p.add_argument('--views',nargs='+',choices=[*VIEWS,'close','distant'],default=list(VIEWS))
    p.add_argument('--probe',action='store_true')
    p.add_argument('--resume',action='store_true')
    p.add_argument('--pilot',action='store_true')
    p.add_argument('--shadows',type=int,choices=[0,1],default=1)
    p.add_argument('--scenes',nargs='+',choices=['guardian','robot'],default=['guardian','robot'])
    args=p.parse_args();args.binary=args.binary.resolve();args.output=args.output.resolve()
    args.output.mkdir(parents=True,exist_ok=args.resume)
    globals()[args.phase](args)


if __name__=='__main__':main()
