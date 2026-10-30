#!/usr/bin/env python3
"""Serial Cached baseline / one convex edit / canonical commit qualification."""
import argparse
import copy
import json
import math
from pathlib import Path
import statistics
import subprocess

from acceleration_loop import gpu_state
from benchmark_tracing import percentile
from investigate_guardian import ROOT, PRESETS, digest, invoke, metadata, camera_queries
from quality_speed_loop import profile
from tracing_comparison import document, sphere, ray, H, unit, dot

# Frozen from visible, non-pending Cached hits in the original placement replay
# at 160x90: Guardian pixel (75,53), Robot (76,53), moved inward by .01 normal.
CUT_CENTERS={
    'guardian':[.00979279792920114,-.053725909893786866,.15428923866744446],
    'robot':[.007054936181693949,-.05578828362564217,.09989852874040706],
}


def save(path,value):path.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n')


def info(args):
    return dict(metadata(args.binary),tool_sha256=digest(Path(__file__)),
        public_binary_sha256=digest(args.public_binary),current_public_sha256=digest(args.current_public),
        shaders={p.name:digest(p) for p in sorted((args.binary.parent/'shaders').glob('*cached_edit*.spv'))})


def edit_scene(base,primitive,subtract=False):
    result=copy.deepcopy(base)
    for key in ('geometry_hash','appearance_hash'):result.pop(key,None)
    if base['root'] is None:
        if subtract:raise ValueError('empty-body subtraction is outside this trial')
        result['root']=primitive
    else:result['root']=dict(name='single-pending-operation',type='subtract' if subtract else 'union',children=[base['root'],primitive])
    if result.get('history'):
        result['history'].append(dict(node=primitive['name'],label='One pending '+('cut' if subtract else 'add'),seconds=.65))
    return result


def case(name,base,primitive,queries,subtract=False,diagnostic=False):
    return dict(name=name,base=base,scene=edit_scene(base,primitive,subtract) if primitive else copy.deepcopy(base),queries=queries,diagnostic=diagnostic)


def fixtures():
    ball=document(sphere('body',.2));q=[]
    p=sphere('pending',.08,[.25,0,0])
    q.append(case('add-outside-base',ball,p,[ray([.25,0,.7],[0,0,-1],.62,[0,0,1]),ray([0,0,.7],[0,0,-1],.5,[0,0,1]),ray([.25,0,.08],[0,0,-1],0,[0,0,1])]))
    q.append(case('union-inside-exits',ball,p,[ray([.12,0,0],[1,0,0],.21,[1,0,0]),ray([.25,0,0],[-1,0,0],.45,[-1,0,0])]))
    q.append(case('cavity',ball,sphere('pending',.06),[ray([0,0,0],[0,0,1],.06,[0,0,-1]),ray([0,0,.12],[0,0,-1],.06,[0,0,-1]),ray([0,0,.7],[0,0,-1],.5,[0,0,1])],True))
    box=document(dict(name='body',type='box',half_extent=[.15]*3))
    q.append(case('through-cut',box,dict(name='pending',type='box',half_extent=[.04,.04,.3]),[ray([0,0,.7],[0,0,-1],None),ray([0,0,0],[1,0,0],.04,[-1,0,0])],True))
    q[-1]['scene']['root']['cut_material']='blue'
    q.append(case('recessed-front',box,dict(name='pending',type='box',half_extent=[.06,.06,.1],position=[0,0,.12]),[ray([0,0,.7],[0,0,-1],.68,[0,0,1])],True))
    q.append(case('cut-covers-body',ball,sphere('pending',.3),[ray([0,0,.7],[0,0,-1],None),ray([0,0,0],[0,0,1],None)],True))
    q.append(case('union-covers-body',ball,sphere('pending',.3),[ray([0,0,.7],[0,0,-1],.4,[0,0,1]),ray([0,0,0],[0,0,1],.3,[0,0,1])]))
    q.append(case('invisible-inner-add',ball,sphere('pending',.04),[ray([0,0,.7],[0,0,-1],.5,[0,0,1]),ray([0,0,0],[0,0,1],.2,[0,0,1])]))
    q.append(case('outside-cut',ball,sphere('pending',.04,[.35,0,0]),[ray([0,0,.7],[0,0,-1],.5,[0,0,1]),ray([.35,0,.7],[0,0,-1],None)],True))
    a=.4;n=[math.sin(a),0,math.cos(a)];center=[.25,.04,0]
    q.append(case('rotated-box',document(sphere('body',.06,[-.2,0,0])),dict(name='pending',type='box',half_extent=[.08,.03,.05],position=center,rotation_xyzw=[0,math.sin(a/2),0,math.cos(a/2)]),[
        ray([center[i]+n[i]*.7 for i in range(3)],[-v for v in n],.65,n)]))
    q.append(case('workspace-add',document(sphere('body',.05,[-.2,0,0])),sphere('pending',.06,[0,0,.48]),[
        ray([0,0,.7],[0,0,-1],.201,[0,0,1]),ray([0,0,.498],[0,0,1],.001,[0,0,1])]))
    q.append(case('workspace-cut',document(sphere('body',.1,[0,0,.45])),sphere('pending',.08,[0,0,.48]),[
        ray([0,0,.7],[0,0,-1],.3,[0,0,1]),ray([0,0,.498],[0,0,1],None)],True))
    two=document(dict(name='two',type='union',children=[sphere('front',.06,[0,0,.15]),sphere('rear',.06,[0,0,-.15])]))
    q.append(case('resume-to-second-body',two,sphere('pending',.1,[0,0,.15]),[ray([0,0,.7],[0,0,-1],.79,[0,0,1])],True))
    q.append(case('empty-base-add',document(None),sphere('pending',.1),[ray([0,0,.7],[0,0,-1],.6,[0,0,1]),ray([0,0,0],[0,0,1],.1,[0,0,1])]))
    q.append(case('zero-edit',ball,None,[ray([0,0,.7],[0,0,-1],.5,[0,0,1]),ray([0,0,0],[0,0,1],.2,[0,0,1])]))
    q.append(case('subgrid-cavity',ball,sphere('pending',.35*H,[.5*H]*3),[ray([.5*H]*3,[0,0,1],.35*H,[0,0,-1])],True,True))
    q.append(case('tool-tangent',document(None),sphere('pending',.1),[ray([.1,0,.7],[0,0,-1],.7),ray([.1+8*H/5,0,.7],[0,0,-1],None)],diagnostic=True))
    return q


def manifest(cases):
    return dict(format='cubey.sdf.cached-edit-study',version=1,cases=cases,
        contracts=dict(regular_depth=H/2,regular_normal_degrees=2,profile='existing Cached quality; source differences remain separate'))


def native(args,inp,out,phase='preview',extra=(),subject=None,view='preset',size=(16,16)):
    flags=[str(args.binary)]
    if subject:flags+=['--config',str(ROOT/f'projects/sdf/presets/{PRESETS[subject]}.json')]
    flags+=['--edit-input',str(inp),'--edit-output',str(out),'--edit-phase',phase,'--headless','--require-validation',
        '--motion','static','--supersample','1','--width',str(size[0]),'--height',str(size[1]),
        '--shadows','1','--no-ao','--no-ground','--no-clouds','--no-occluder']
    if view=='close':flags+=['--distance','.8']
    return flags+list(extra)


def subject_scene(subject):
    cfg=json.loads((ROOT/f'projects/sdf/presets/{PRESETS[subject]}.json').read_text())
    data=json.loads((ROOT/cfg['authored-scene']).read_text())
    for key in ('geometry_hash','appearance_hash'):data.pop(key,None)
    return data


def prepare(args):
    receipt=dict(info(args),cases=[],negative=[],complete=False)
    for s in PRESETS:
        base=subject_scene(s);base_path=args.output/f'{s}-base-scene.json';save(base_path,base)
        for op in ('none','add','cut'):
            position=[.03,.075,.28] if s=='guardian' else [0,.1,.15]
            if op=='cut':position=CUT_CENTERS[s]
            edited=base if op=='none' else edit_scene(base,sphere('pending-brush',.065,position),op=='cut')
            inp=args.output/f'{s}-{op}.json';save(inp,manifest([dict(name=s+'-'+op,base=base,scene=edited)]))
            scene=args.output/f'{s}-{op}-scene.json';save(scene,edited)
            out=args.output/f'{s}-{op}-admitted.json'
            flags=native(args,inp,out,extra=['--edit-validate-only']);invoke(flags,out.with_suffix(''))
            receipt['cases'].append(dict(subject=s,operation=op,input=str(inp),scene=str(scene),base=str(base_path),identity=json.loads(out.read_text())['programs'][0]))
    base=document(sphere('body',.15));single=edit_scene(base,sphere('pending',.05))
    negatives={
        'multiple':edit_scene(single,sphere('second',.02)),
        'smoothing':dict(single,root=dict(single['root'],type='smooth-union',radius=.01)),
        'capsule':edit_scene(base,dict(name='pending',type='capsule',radius=.03,half_length=.04)),
        'rounded-box':edit_scene(base,dict(name='pending',type='box',half_extent=[.03]*3,rounding=.01)),
    }
    prefix=copy.deepcopy(single);prefix['root']['children'][0]['radius']=.16;negatives['prefix']=prefix
    palette=copy.deepcopy(single);palette['materials'][0]['roughness']=.9;negatives['palette']=palette
    for label,edited in negatives.items():
        inp=args.output/f'negative-{label}.json';save(inp,manifest([dict(name=label,base=base,scene=edited)]))
        out=args.output/f'negative-{label}-result.json';flags=native(args,inp,out,extra=['--edit-validate-only'])
        p=subprocess.run(flags,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if p.returncode==0 or out.exists():raise RuntimeError(f'{label} unexpectedly admitted')
        receipt['negative'].append(dict(name=label,command=flags,reason=p.stdout.strip()))
    receipt['complete']=True;save(args.output/'admission.json',receipt)


def score(data,actual):
    wanted={c['name']:c for c in data['cases']};failures=[];diagnostics=[];count=0;max_depth=0;max_normal=0
    if sorted(c['name'] for c in actual)!=sorted(wanted):raise RuntimeError('missing query case')
    for c in actual:
        source=wanted[c['name']]
        if len(c['results'])!=len(source['queries']):raise RuntimeError('incomplete rays')
        for i,(q,r) in enumerate(zip(source['queries'],c['results'])):
            issue=[]
            if r['status']==2:failures.append([c['name'],i,'exhausted']);continue
            if r['status']!=(0 if q['expected_t'] is None else 1):issue.append('classification')
            elif q['expected_t'] is not None:
                error=abs(r['t']-q['expected_t'])
                if not source.get('diagnostic'):max_depth=max(max_depth,error)
                if error>data['contracts']['regular_depth']:issue.append(['depth',error])
                if 'expected_normal'in q:
                    angle=math.degrees(math.acos(max(-1,min(1,dot(unit(r['normal']),q['expected_normal'])))))
                    if not source.get('diagnostic'):max_normal=max(max_normal,angle)
                    if angle>data['contracts']['regular_normal_degrees']:issue.append(['normal',angle])
            if source.get('diagnostic'):diagnostics.append(dict(case=c['name'],query=i,expected_t=q['expected_t'],result=r,differences=issue))
            else:
                count+=1
                if issue:failures.append([c['name'],i,issue])
    return dict(passed=not failures,regular_rays=count,max_regular_depth=max_depth,max_regular_normal_degrees=max_normal,failures=failures,diagnostics=diagnostics)


def geometry(args):
    data=manifest(fixtures());inp=args.output/'fixtures.json';save(inp,data)
    out=args.output/'preview.json';flags=native(args,inp,out,extra=['--edit-queries']);invoke(flags,out.with_suffix(''),timeout=300)
    result=json.loads(out.read_text())['queries']['cases'];preview=score(data,result)
    fresh=dict(data,format='cubey.sdf.tracing-study');fresh['cases']=[{k:v for k,v in c.items() if k!='base'} for c in data['cases']]
    fresh_input=args.output/'fresh-input.json';save(fresh_input,fresh)
    fresh_output=args.output/'fresh-cached.json'
    flags2=[str(args.public_binary),'--headless','--headless-render-only','--require-validation','--frames','1','--width','16','--height','16',
        '--surface-trace','sampled','--csg-evaluation','full','--comparison-input',str(fresh_input),'--comparison-output',str(fresh_output)]
    invoke(flags2,fresh_output.with_suffix(''),timeout=300)
    fresh_score=score(data,json.loads(fresh_output.read_text())['cases'])
    receipt=dict(info(args),preview=preview,fresh_cached=fresh_score,command=flags,fresh_command=flags2,complete=True)
    save(args.output/'geometry.json',receipt);print(json.dumps(receipt['preview'],indent=2),flush=True)
    if not preview['passed']:raise RuntimeError('one-operation preview geometry gate failed')


def public_command(binary,subject,scene,out,size=(960,540),view='preset',samples=1,mode='cached'):
    flags=[str(binary)]
    if subject:flags+=['--config',str(ROOT/f'projects/sdf/presets/{PRESETS[subject]}.json')]
    flags+=['--authored-scene',str(scene),
        '--headless','--require-validation','--motion','static','--supersample',str(samples),'--width',str(size[0]),'--height',str(size[1]),
        '--shadows','1','--no-ao','--no-ground','--no-clouds','--no-occluder']
    if view=='close':flags+=['--distance','.8']
    if mode:flags+=['--render-mode',mode]
    if out:flags+=['--frames','1','--output',str(out)]
    return flags


def defaults(args):
    receipt=dict(info(args),runs=[],complete=False)
    for subject in PRESETS:
        scene=next(r['base'] for r in json.loads(args.inputs.read_text())['cases'] if r['subject']==subject)
        for samples in (1,2):
            images=[]
            for label,binary,mode in [('default',args.current_public,None),('cached',args.public_binary,'cached')]:
                prefix=args.output/f'{subject}-ss{samples}-{label}'
                flags=public_command(binary,subject,scene,prefix.with_suffix('.png'),size=(320,180),samples=samples,mode=mode)
                invoke(flags,prefix,timeout=300);images.append(digest(prefix.with_suffix('.png')))
                receipt['runs'].append(dict(subject=subject,samples=samples,kind=label,command=flags,image_sha256=images[-1]))
            if images[0]!=images[1]:raise RuntimeError('new default differs from original Cached')
    receipt['complete']=True;save(args.output/'defaults.json',receipt)


def timing(args):
    rows=json.loads(args.inputs.read_text())['cases'];cases=[]
    for subject in PRESETS:
        none=next(r for r in rows if r['subject']==subject and r['operation']=='none')
        for view in ('preset','close'):
            cases +=[(none,view,'original'),(none,view,'zero')]
            for op in ('add','cut'):
                row=next(r for r in rows if r['subject']==subject and r['operation']==op)
                cases +=[(row,view,'preview'),(row,view,'committed')]
    frames,warmup=(12,2) if args.pilot else (180,30)
    receipt=dict(info(args),runs=[],complete=False,pilot=args.pilot,frames=frames,warmup=warmup)
    for repeat in (1,) if args.pilot else (1,2,3):
        for row,view,phase in cases if repeat%2 else reversed(cases):
            prefix=args.output/f"{row['subject']}-{view}-{row['operation']}-{phase}-r{repeat}"
            extra=['--headless-render-only','--frames',str(frames),'--profile-warmup-frames',str(warmup),'--profile-output',str(prefix)]
            if phase=='original':flags=public_command(args.public_binary,row['subject'],row['base'],None,size=(1920,1080),view=view)+extra
            else:flags=native(args,Path(row['input']),prefix.with_suffix('.json'),'preview' if phase=='zero' else phase,extra,row['subject'],view,(1920,1080))
            before=gpu_state();wall=invoke(flags,prefix,timeout=300);after=gpu_state()
            passes,metrics=profile(prefix,frames,warmup);values=passes['sdf.whole_scene']['values_ms']
            stats=dict(p50_ms=statistics.median(values),p95_ms=percentile(values,.95),p99_ms=percentile(values,.99),max_ms=max(values),
                over_11_11=sum(v>11.11 for v in values),over_16_67=sum(v>16.67 for v in values))
            receipt['runs'].append(dict(subject=row['subject'],view=view,operation=row['operation'],phase=phase,repeat=repeat,
                command=flags,wall_seconds=wall,gpu_before=before,gpu_after=after,stats=stats,passes=passes,metrics=metrics))
            save(args.output/'timing.json',receipt);print(prefix.name,round(stats['p95_ms'],3),flush=True)
    receipt['complete']=True;save(args.output/'timing.json',receipt)


def captures(args):
    rows=json.loads(args.inputs.read_text())['cases'];receipt=dict(info(args),runs=[],complete=False)
    for row in rows:
        for view in ('preset','close'):
            phases=('preview',) if row['operation']=='none' else ('base','preview','committed')
            for phase in phases:
                prefix=args.output/f"{row['subject']}-{view}-{row['operation']}-{phase}"
                flags=native(args,Path(row['input']),prefix.with_suffix('.json'),phase,['--frames','1','--output',str(prefix.with_suffix('.png'))],row['subject'],view,(960,540))
                invoke(flags,prefix,timeout=300)
                receipt['runs'].append(dict(subject=row['subject'],view=view,operation=row['operation'],phase=phase,command=flags,image=prefix.with_suffix('.png').name,image_sha256=digest(prefix.with_suffix('.png'))))
                print(prefix.name,flush=True)
    # Matched quality payloads and independent original-renderer settled images.
    for row in rows:
        for phase in (('preview',) if row['operation']=='none' else ('preview','committed')):
            prefix=args.output/f"{row['subject']}-{row['operation']}-{phase}-probe"
            flags=native(args,Path(row['input']),prefix.with_suffix('.json'),phase,['--frames','1','--output',str(prefix.with_suffix('.png'))],row['subject'],size=(320,180))
            invoke(flags,prefix,timeout=300)
            probe=flags.copy();probe[probe.index('--output')+1]=str(prefix.with_suffix('.validated.png'));probe+=['--quality-output',str(prefix.with_suffix('.bin'))]
            invoke(probe,Path(str(prefix)+'-quality'),timeout=300)
            receipt['runs'].append(dict(subject=row['subject'],operation=row['operation'],phase=phase+'-probe',command=flags,probe_command=probe,prefix=str(prefix)))
        if row['operation']=='none':
            for view in ('preset','close'):
                prefix=args.output/f"{row['subject']}-{view}-original-zero"
                flags=public_command(args.public_binary,row['subject'],row['base'],prefix.with_suffix('.png'),view=view)
                invoke(flags,prefix,timeout=300)
                current=next(r for r in receipt['runs'] if r.get('view')==view and r['subject']==row['subject'] and r['operation']=='none' and r['phase']=='preview')
                if digest(prefix.with_suffix('.png'))!=current['image_sha256']:raise RuntimeError('zero-edit output differs from original Cached')
                receipt['runs'].append(dict(subject=row['subject'],operation='none',view=view,phase='original-zero',command=flags,image=prefix.with_suffix('.png').name,zero_byte_identical=True))
            continue
        prefix=args.output/f"{row['subject']}-{row['operation']}-original-committed"
        flags=public_command(args.public_binary,row['subject'],row['scene'],prefix.with_suffix('.png'),size=(960,540))
        invoke(flags,prefix,timeout=300)
        current=next(r for r in receipt['runs'] if r.get('view')=='preset' and r['subject']==row['subject'] and r['operation']==row['operation'] and r['phase']=='committed')
        if digest(prefix.with_suffix('.png'))!=current['image_sha256']:raise RuntimeError('committed image differs from original Cached')
        receipt['runs'].append(dict(subject=row['subject'],operation=row['operation'],phase='original-committed',command=flags,image=prefix.with_suffix('.png').name,image_sha256=digest(prefix.with_suffix('.png')),settled_byte_identical=True))
    for c in fixtures():
        if c['name'] not in ('through-cut','recessed-front','workspace-cut'):continue
        inp=args.output/(c['name']+'-input.json');save(inp,manifest([c]))
        scene=args.output/(c['name']+'-scene.json');save(scene,c['scene'])
        for phase in ('base','preview','committed'):
            prefix=args.output/(c['name']+'-'+phase)
            flags=native(args,inp,prefix.with_suffix('.json'),phase,['--frames','1','--output',str(prefix.with_suffix('.png'))],size=(960,540))
            invoke(flags,prefix,timeout=300)
            receipt['runs'].append(dict(subject=c['name'],operation='cut',view='control',phase=phase,command=flags,image=prefix.with_suffix('.png').name,image_sha256=digest(prefix.with_suffix('.png'))))
    receipt['complete']=True;save(args.output/'captures.json',receipt)


def replay(args):
    receipt=dict(info(args),runs=[],complete=False)
    for row in json.loads(args.inputs.read_text())['cases']:
        if row['operation']=='none':continue
        cfg=json.loads((ROOT/f"projects/sdf/presets/{PRESETS[row['subject']]}.json").read_text())
        for view in ('preset','close'):
            camera=dict(cfg)
            if view=='close':camera['distance']=.8
            data=json.loads(Path(row['input']).read_text());data['cases'][0]['queries']=camera_queries(camera,160,90)
            control=copy.deepcopy(data['cases'][0]);control['name']+='-cached-control';control['scene']=copy.deepcopy(control['base']);data['cases'].append(control)
            prefix=args.output/f"{row['subject']}-{view}-{row['operation']}"
            inp=prefix.with_suffix('.input.json');save(inp,data)
            out=prefix.with_suffix('.json');flags=native(args,inp,out,extra=['--edit-queries']);invoke(flags,prefix,timeout=300)
            output=json.loads(out.read_text())['queries']['cases'];results=output[0]['results'];control_results=output[1]['results']
            exhausted=sum(r['status']==2 for r in results)
            if exhausted:raise RuntimeError('camera replay exhausted')
            changed=sum(a['status']!=b['status'] or (a['status']==1 and abs(a['t']-b['t'])>H) for a,b in zip(results,control_results))
            if changed<10:raise RuntimeError(f"{row['subject']}/{view}/{row['operation']}: fewer than ten visibly changed Cached rays ({changed})")
            e=math.radians(cfg['sun-elevation']);a=math.radians(cfg['sun-azimuth'])
            light=[math.cos(e)*math.sin(a),math.sin(e),-math.cos(e)*math.cos(a)]
            shadows=[dict(kind='ray',origin=[q['origin'][i]+q['direction'][i]*r['t']+r['normal'][i]*2*H for i in range(3)],direction=light)
                     for q,r in zip(data['cases'][0]['queries'],results) if r['status']==1]
            data['cases'][0]['queries']=shadows
            data['cases']=data['cases'][:1]
            shadow_input=Path(str(prefix)+'-shadow-input.json');save(shadow_input,data)
            shadow_output=Path(str(prefix)+'-shadow.json');shadow_flags=native(args,shadow_input,shadow_output,extra=['--edit-queries']);invoke(shadow_flags,Path(str(prefix)+'-shadow'),timeout=300)
            shadow_rows=json.loads(shadow_output.read_text())['queries']['cases'][0]['results']
            if any(r['status']==2 for r in shadow_rows):raise RuntimeError('shadow replay exhausted')
            receipt['runs'].append(dict(subject=row['subject'],view=view,operation=row['operation'],camera_rays=len(results),camera_exhausted=exhausted,
                camera_hits=sum(r['status']==1 for r in results),pending_hits=sum(r['status']==1 and r['pending_surface'] for r in results),
                changed_cached_rays=changed,visibility_gate_minimum=10,shadow_rays=len(shadow_rows),shadow_exhausted=0,command=flags,shadow_command=shadow_flags))
            save(args.output/'replay.json',receipt)
    receipt['complete']=True;save(args.output/'replay.json',receipt)


def sequences(args):
    rows=json.loads(args.inputs.read_text())['cases'];receipt=dict(info(args),runs=[],complete=False)
    for row in rows:
        if row['operation']=='none':continue
        for fault in (False,True):
            label=f"{row['subject']}-{row['operation']}-{'failure-retry' if fault else 'commit'}"
            inp=args.output/f'{label}-input.json';data=json.loads(Path(row['input']).read_text());data['fault_before_publish']=fault;save(inp,data)
            prefix=args.output/label;frames=5 if fault else 4
            flags=native(args,inp,prefix.with_suffix('.json'),'sequence',['--headless-render-only','--frames',str(frames)],row['subject'],size=(960,540))
            invoke(flags,prefix,timeout=300);native_result=json.loads(prefix.with_suffix('.json').read_text())
            actual=native_result['frames'];expected=['base','preview','preview','committed','committed'] if fault else ['base','preview','committed','committed']
            if [r['display'] for r in actual]!=expected:raise RuntimeError('sequence publication order differs')
            if actual[-1]['pending_after_completed_frame']!=0:raise RuntimeError('pending edit not retired')
            if fault and digest(Path(actual[1]['image']))!=digest(Path(actual[2]['image'])):raise RuntimeError('failed commit changed preview display')
            reference=args.inputs.parent.parent/'captures'/f"{row['subject']}-preset-{row['operation']}-committed.png"
            if digest(Path(actual[-1]['image']))!=digest(reference):raise RuntimeError('settled sequence differs from fresh Cached')
            retained=json.loads(Path(native_result['retained_source_file']).read_text())
            if retained['root']!=json.loads(Path(row['scene']).read_text())['root']:raise RuntimeError('canonical retained source differs')
            receipt['runs'].append(dict(subject=row['subject'],operation=row['operation'],fault=fault,command=flags,native=native_result,settled_byte_identical=True))
            save(args.output/'sequences.json',receipt);print(label,'passed',flush=True)
    receipt['complete']=True;save(args.output/'sequences.json',receipt)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('phase',choices=['prepare','geometry','defaults','timing','captures','sequences','replay']);p.add_argument('--output',type=Path,required=True);p.add_argument('--inputs',type=Path)
    p.add_argument('--binary',type=Path,default=ROOT/'build/release/projects/sdf/cubey_project_sdf_cached_edit_study')
    p.add_argument('--public-binary',type=Path,default=ROOT/'outputs/sdf/cached-one-edit-loop-20261030/baseline-sdf')
    p.add_argument('--current-public',type=Path,default=ROOT/'build/release/projects/sdf/sdf')
    p.add_argument('--pilot',action='store_true')
    a=p.parse_args();a.output=a.output.resolve();a.binary=a.binary.resolve();a.public_binary=a.public_binary.resolve();a.current_public=a.current_public.resolve()
    a.output.mkdir(parents=True,exist_ok=False);globals()[a.phase](a)


if __name__=='__main__':main()
