#!/usr/bin/env python3
"""Native bounded sphere-stroke preview qualification; jobs remain serial."""
import argparse
import copy
import filecmp
import json
import math
from pathlib import Path
import subprocess
import statistics

import cached_edit_loop as e
import local_edit_loop as local
from investigate_guardian import ROOT, camera_queries, PRESETS, digest
from quality_speed_loop import profile
from benchmark_tracing import percentile
from tracing_comparison import document, sphere, ray, dot, unit, H

RAW=ROOT/'outputs/sdf/local-edit-loop-20261030'


def save(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')


def identity(args):
    result=local.identity(args)
    result['stroke_shaders']={p.name:digest(p) for p in sorted((args.binary.parent/'shaders').glob('*cached_stroke*.spv'))}
    result['stroke_harness_sha256']=digest(Path(__file__))
    return result


def stroke_case(name,base,tools,cut=False,qs=()):
    previous=base
    for i,tool in enumerate(tools):
        c=e.case(name,previous,tool,[],cut)
        c['scene']['root']['name']=f'{name}-root-{i}'
        previous=c['scene']
    return dict(name=name,base=base,scene=previous,queries=list(qs))


def interval(origin,direction,shape):
    if shape['type']=='sphere':
        delta=[origin[i]-shape.get('position',[0,0,0])[i] for i in range(3)]
        radius=shape['radius']*shape.get('scale',1);b=dot(delta,direction)
        discriminant=b*b-dot(delta,delta)+radius*radius
        if discriminant<0:return None
        h=math.sqrt(discriminant);return -b-h,-b+h
    low=[-x for x in shape['half_extent']];high=shape['half_extent'];a=-math.inf;b=math.inf
    for i in range(3):
        if abs(direction[i])<1e-20:
            if not low[i]<=origin[i]<=high[i]:return None
        else:
            p=(low[i]-origin[i])/direction[i];q=(high[i]-origin[i])/direction[i]
            a=max(a,min(p,q));b=min(b,max(p,q))
    return (a,b) if a<=b else None


def oracle(origin,direction,bodies,tools,cut):
    parts=[]
    for kind,shapes in [('base',bodies),('tool',tools),('clip',[dict(type='box',half_extent=[.499]*3)])]:
        for shape in shapes:
            value=interval(origin,direction,shape)
            if value:parts.append((kind,shape,value))
    def inside(t):
        states={k:any(a<t<b for kind,_,(a,b) in parts if kind==k) for k in ('base','tool','clip')}
        return states['clip'] and (states['base'] and not states['tool'] if cut else states['base'] or states['tool'])
    for t in sorted(set(t for _,_,values in parts for t in values if 0<=t<=3)):
        delta=1e-8
        if inside(t-delta)==inside(t+delta):continue
        hit=[o+d*t for o,d in zip(origin,direction)]
        boundaries=[(kind,shape) for kind,shape,values in parts if min(abs(t-x) for x in values)<1e-10]
        q=ray(origin,direction,t)
        if len(boundaries)==1:
            kind,shape=boundaries[0]
            if shape['type']=='sphere':
                n=unit([hit[i]-shape.get('position',[0,0,0])[i] for i in range(3)])
            else:
                axis=max(range(3),key=lambda i:abs(hit[i])/shape['half_extent'][i]);n=[0,0,0];n[axis]=1 if hit[axis]>0 else -1
            if cut and kind=='tool':n=[-x for x in n]
            q['expected_normal']=n
            if kind!='clip':q['expected_pending']=kind=='tool'
        return q
    return ray(origin,direction,None)


def inputs(args):
    geometry=[]
    for case in e.fixtures():
        if case['name'] in ('through-cut','recessed-front','rotated-box'):continue
        geometry.append(case)
    for n in (2,4,8):
        ball=sphere('body',.2);base=document(ball)
        for cut in (False,True):
            tools=[dict(name=f'tool-{i}',type='sphere',radius=.025,
                position=[(i-(n-1)/2)*.055,0,0],material='blue') for i in range(n)]
            qs=[oracle(o,d,[ball],tools,cut) for o,d in [([0,0,.7],[0,0,-1]),
                ([0,0,0],[1,0,0]),([-.7,0,0],[1,0,0]),([.15,0,.7],[0,0,-1])]]
            geometry.append(stroke_case(f'{n}-disjoint-'+('cut' if cut else 'add'),base,tools,cut,qs))
            tools=[dict(t,position=[(i-(n-1)/2)*.012,0,.18]) for i,t in enumerate(tools)]
            qs=[oracle(o,d,[ball],tools,cut) for o,d in [([.006,0,.7],[0,0,-1]),
                ([.006,0,.18],[0,1,0]),([-.1,0,.7],[0,0,-1])]]
            geometry.append(stroke_case(f'{n}-overlap-'+('cut' if cut else 'add'),base,tools,cut,qs))
    bodies=[sphere(f'body-{i}',.02,[0,0,(i-3.5)*.08]) for i in range(8)]
    tools=[dict(name=f'cutter-{i}',type='sphere',radius=.025,position=b['position']) for i,b in enumerate(bodies)]
    base=document(dict(name='eight-bodies',type='union',children=bodies))
    qs=[oracle([0,0,.7],[0,0,-1],bodies,tools,True),oracle([0,0,0],[0,0,1],bodies,tools,True)]
    geometry.append(stroke_case('eight-hidden-base-boundaries',base,tools,True,qs))
    for cut in (False,True):
        body=sphere('body',.1,[0,0,.45]) if cut else sphere('body',.04,[-.3,0,0])
        tools=[dict(name=f'edge-{i}',type='sphere',radius=.025,position=[(i-3.5)*.008,0,.49]) for i in range(8)]
        qs=[oracle(o,d,[body],tools,cut) for o,d in [([.004,0,.7],[0,0,-1]),([.004,0,.498],[0,0,1]),
            ([.004,0,.4989],[1,0,0])]]
        geometry.append(stroke_case('eight-workspace-'+('cut' if cut else 'add'),document(body),tools,cut,qs))
    body=sphere('body',.06,[-.25,0,0])
    for label,centers in [('touching',[[0,0,0],[.05,0,0]]),('equal-endpoints',[[0,0,0]]*8)]:
        tools=[dict(name=f'equal-{i}',type='sphere',radius=.025,position=center) for i,center in enumerate(centers)]
        qs=[oracle(o,d,[body],tools,False) for o,d in [([0,0,0],[1,0,0]),([.05,0,.7],[0,0,-1]),([-.1,0,0],[1,0,0])]]
        geometry.append(stroke_case(label,document(body),tools,False,qs))
    tool=dict(name='grazing-tool',type='sphere',radius=.025,position=[0,0,0])
    qs=[oracle([0,.025*.999,.7],[0,0,-1],[body],[tool],False)]
    geometry.append(stroke_case('regular-grazing',document(body),[tool],False,qs))
    geom=args.output/'geometry-input.json';save(geom,e.manifest(geometry))
    rows=[]
    for subject in ('guardian','robot'):
        base=e.subject_scene(subject);basefile=args.output/f'{subject}-base.json';save(basefile,base)
        for op in ('add','cut'):
            center=e.CUT_CENTERS[subject] if op=='cut' else ([.03,.075,.28] if subject=='guardian' else [0,.1,.15])
            for n in (0,1,2,4,8):
                tools=[dict(name=f'stamp-{i}',type='sphere',radius=.025,
                    position=[center[j]+((i-(n-1)/2)*.008 if j==0 else 0) for j in range(3)]) for i in range(n)]
                case=stroke_case(f'{subject}-{op}-{n}',base,tools,op=='cut')
                inp=args.output/(case['name']+'.json');scene=args.output/(case['name']+'-scene.json')
                save(inp,e.manifest([case]));save(scene,case['scene'])
                rows.append(dict(subject=subject,operation=op if n else 'none',count=n,input=str(inp),scene=str(scene),base=str(basefile)))
    capture=[r for r in rows if r['count']==8 or r['count']==0 and '-add-' in r['input']]
    receipt=dict(identity(args),rows=rows,cases=capture,geometry_input=str(geom),complete=True)
    save(args.output/'inputs.json',receipt);print(len(geometry),'geometry cases',len(rows),'subject inputs')


def geometry(args):
    source=json.loads(args.inputs.read_text());inp=Path(source['geometry_input']);out=args.output/'queries.json'
    cmd=e.native(args,inp,out,extra=['--edit-queries'])
    record=local.run(cmd,out);data=json.loads(inp.read_text());actual=record['native']['queries']['cases']
    score=e.score(data,actual)
    by_name={r['name']:r for r in actual};owners=[]
    for c in data['cases']:
        for i,q in enumerate(c['queries']):
            if 'expected_pending' in q:
                got=by_name[c['name']]['results'][i]['pending_surface'];owners.append(got==q['expected_pending'])
    score['pending_owner_checks']=len(owners);score['pending_owner_checks_passed']=all(owners)
    save(args.output/'geometry.json',dict(identity(args),native=record,score=score,complete=True))
    print(json.dumps(score,indent=2));assert score['passed'] and all(owners)


def replay(args):
    # Existing replay gate: visible Cached change, complete camera rays and
    # corresponding shadow rays, with no tracer exhaustion.
    e.replay(args)
    receipt=json.loads((args.output/'replay.json').read_text());receipt.update(identity(args));save(args.output/'replay.json',receipt)


def admission(args):
    base=document(sphere('body',.15))
    tools=[dict(name=f'tool-{i}',type='sphere',radius=.025,position=[i*.008,0,.15],material='blue') for i in range(9)]
    positives=[stroke_case(f'count-{n}',base,tools[:n]) for n in (0,1,2,4,8)]
    negatives={'nine':stroke_case('nine',base,tools)}
    good=stroke_case('two',base,tools[:2])
    for label in ('mixed','radius','material','scale','prefix','palette','smoothing','box','cut-material'):
        c=copy.deepcopy(good);c['base']=copy.deepcopy(c['base']);c['name']=label
        if label=='mixed':c['scene']['root']['type']='subtract'
        elif label=='radius':c['scene']['root']['children'][1]['radius']=.026
        elif label=='scale':c['scene']['root']['children'][1]['scale']=1.1
        elif label=='material':c['scene']['root']['children'][1]['material']='red'
        elif label=='prefix':c['scene']['root']['children'][0]['children'][0]['radius']=.16
        elif label=='palette':c['scene']['materials'][0]['roughness']=.99
        elif label=='smoothing':c['scene']['root'].update(type='smooth-union',radius=.01)
        elif label=='box':c['scene']['root']['children'][1]=dict(name='box-tool',type='box',half_extent=[.02]*3)
        elif label=='cut-material':
            c=stroke_case(label,base,tools[:2],True)
            c['scene']['root']['cut_material']='red'
        negatives[label]=c
    receipt=dict(identity(args),positive=[],negative=[],complete=False)
    for label,cases,expected in [('positive',positives,True),('negative',list(negatives.values()),False)]:
        for c in cases:
            inp=args.output/(c['name']+'-input.json');out=args.output/(c['name']+'-result.json');save(inp,e.manifest([c]))
            r=local.run(e.native(args,inp,out,extra=['--edit-validate-only']),out,allow_failure=True)
            assert (r['exit_code']==0)==expected
            assert out.exists()==expected
            receipt[label].append(dict(name=c['name'],**r))
    receipt['complete']=True;save(args.output/'admission.json',receipt)


def caches(args):
    source=json.loads(args.inputs.read_text())
    rows=[r for r in source['rows'] if r['count'] in (1,2,4,8)]
    receipt=dict(identity(args),runs=[],complete=False,mode=args.mode)
    for row in rows:
        out=args.output/(Path(row['input']).stem+'.json')
        cmd=e.native(args,Path(row['input']),out,extra=['--edit-cache-qualify','--edit-commit-builder',args.mode,
            '--edit-cache-trials',str(10 if args.measure else 1),'--edit-cache-warmup',str(3 if args.measure else 0),
            '--edit-slice-groups',str(args.groups)])
        r=local.run(cmd,out);assert r['native']['cache_complete'] and r['native']['cache_passed']
        for case in r['native']['cache_runs']:
            assert case['comparison']['passed']
            assert all(filecmp.cmp(a,b,shallow=False) for a,b in zip(case['candidate']['files'],case['canonical']['files']))
        receipt['runs'].append(dict(case=row,**r));save(args.output/'caches.json',receipt)
        print(Path(row['input']).stem,'PASS',flush=True)
    receipt['complete']=True;save(args.output/'caches.json',receipt)


def captures(args):
    source=json.loads(args.inputs.read_text());receipt=dict(identity(args),runs=[],complete=False)
    for row in source['cases']:
        for view in ('preset','close'):
            for phase in ('preview',) if row['operation']=='none' else ('base','preview','committed'):
                prefix=args.output/f"{row['subject']}-{view}-{row['operation']}-{phase}"
                cmd=e.native(args,Path(row['input']),prefix.with_suffix('.json'),phase,
                    ['--frames','1','--output',str(prefix.with_suffix('.png'))],row['subject'],view,(960,540))
                r=local.run(cmd,prefix.with_suffix('.json'))
                receipt['runs'].append(dict(case=row,view=view,phase=phase,image=str(prefix.with_suffix('.png')),image_sha256=digest(prefix.with_suffix('.png')),**r))
            reference=args.output/f"{row['subject']}-{view}-{row['operation']}-original.png"
            scene=row['base'] if row['operation']=='none' else row['scene']
            cmd=e.public_command(args.public_binary,row['subject'],scene,reference,view=view)
            local.run(cmd,reference.with_suffix('.public.json'))
            expected='preview' if row['operation']=='none' else 'committed'
            match=next(r for r in receipt['runs'] if r['case']==row and r['view']==view and r['phase']==expected)
            assert match['image_sha256']==digest(reference),'settled or zero differs from original Cached'
            match.update(original_cached=str(reference),original_cached_byte_identical=True)
            save(args.output/'captures.json',receipt)
    receipt['complete']=True;save(args.output/'captures.json',receipt)


def prefixes(args):
    source=json.loads(args.inputs.read_text());receipt=dict(identity(args),runs=[],complete=False)
    for row in source['rows']:
        if row['count']!=8:continue
        data=json.loads(Path(row['input']).read_text());case=data['cases'][0]
        prefix=copy.deepcopy(case['scene'])
        for i in range(4):prefix['root']=prefix['root']['children'][0]
        if prefix.get('history'):prefix['history']=prefix['history'][:len(case['base'].get('history',[]))+4]
        case['prefix_scene']=prefix
        # Independent original Cached build of prefix four, with only the tail
        # supplied to the interval tracer, proves intermediate display binding.
        ref_input=args.output/(case['name']+'-tail-reference-input.json')
        save(ref_input,e.manifest([dict(name='tail-reference',base=prefix,scene=case['scene'])]))
        tail_ref=args.output/(case['name']+'-tail-reference.png')
        local.run(e.native(args,ref_input,tail_ref.with_suffix('.json'),'preview',
            ['--frames','1','--output',str(tail_ref)],row['subject'],size=(320,180)),tail_ref.with_suffix('.json'))
        settled=args.output/(case['name']+'-original-cached.png')
        local.run(e.public_command(args.public_binary,row['subject'],row['scene'],settled,size=(320,180)),settled.with_suffix('.public.json'))
        for fault in ('none','allocation','cancel-before-submit','stale','discard-after-submit','build-capacity','append-capacity'):
            inp=args.output/(case['name']+'-'+fault+'-input.json');data['prefix_fault']=fault;save(inp,data)
            out=args.output/(case['name']+'-'+fault+'.json')
            r=local.run(e.native(args,inp,out,'prefix',['--edit-commit-builder','local-dom',
                '--headless-render-only','--frames',str(4 if fault=='none' else 5)],row['subject'],size=(320,180)),out)
            native=r['native'];assert native['complete'] and native['drained']
            for frame in native['frames'][1:]:
                assert frame['baked_ids']+frame['render_pending_ids']==list(range(1,9))
                assert frame['total_uncommitted_before_completion']<=8
                assert frame['retired_after_completed_frame']==frame['baked_prefix']
            middle=next(f for f in native['frames'] if f['baked_prefix']==4)
            assert digest(Path(middle['image']))==digest(tail_ref)
            assert digest(Path(native['frames'][-1]['image']))==digest(settled)
            if fault!='none':
                assert digest(Path(native['frames'][1]['image']))==digest(Path(native['frames'][2]['image']))
                assert not native['attempts'][0]['published']
            retained=json.loads(Path(native['retained_source_file']).read_text())
            assert retained['root']==case['scene']['root']
            assert retained.get('history',[])==case['scene'].get('history',[])
            receipt['runs'].append(dict(case=row,fault=fault,intermediate_byte_identical=True,
                settled_byte_identical=True,retained_source_matches=True,**r))
            save(args.output/'prefixes.json',receipt);print(case['name'],fault,'PASS',flush=True)
    receipt['complete']=True;save(args.output/'prefixes.json',receipt)


def calibrate(args):
    source=json.loads(args.inputs.read_text());rows=[r for r in source['rows'] if r['count']==4]
    receipt=dict(identity(args),runs=[],complete=False)
    for repeat in range(3):
        for row in rows:
            for groups in (0,32,128,512,2048) if repeat%2==0 else (2048,512,128,32,0):
                out=args.output/f"{Path(row['input']).stem}-{groups}-{repeat}.json"
                cmd=e.native(args,Path(row['input']),out,extra=['--edit-cache-qualify','--edit-commit-builder','local-dom',
                    '--edit-cache-trials','10','--edit-cache-warmup','3','--edit-slice-groups',str(groups)])
                r=local.run(cmd,out);assert r['native']['cache_complete'] and r['native']['cache_passed']
                receipt['runs'].append(dict(case=row,groups=groups,repeat=repeat,**r));save(args.output/'calibrate.json',receipt)
                print(Path(row['input']).stem,groups,repeat,flush=True)
    receipt['complete']=True;save(args.output/'calibrate.json',receipt)


def timing(args):
    source=json.loads(args.inputs.read_text());rows=source['rows'];cases=[]
    for subject in ('guardian','robot'):
        for view in ('preset','close'):
            zero=next(r for r in rows if r['subject']==subject and r['count']==0)
            cases.extend([(zero,view,'original'),(zero,view,'stroke')])
            for op in ('add','cut'):
                for n in (1,2,4,8):
                    row=next(r for r in rows if r['subject']==subject and r['operation']==op and r['count']==n)
                    cases.append((row,view,'stroke'))
                    if n==1:cases.append((row,view,'one-control'))
    frames,warmup=(42,12) if args.pilot else (180,30)
    receipt=dict(identity(args),runs=[],complete=False,pilot=args.pilot)
    for repeat in (0,) if args.pilot else range(3):
        for row,view,route in cases if repeat%2==0 else reversed(cases):
            prefix=args.output/f"{row['subject']}-{view}-{row['operation']}-{row['count']}-{route}-{repeat}"
            extra=['--headless-render-only','--frames',str(frames),'--profile-warmup-frames',str(warmup),'--profile-output',str(prefix)]
            if route=='original':cmd=e.public_command(args.public_binary,row['subject'],row['base'],None,size=(1920,1080),view=view)+extra
            else:
                current=args.binary
                if route=='one-control':args.binary=RAW/'baseline-cubey_project_sdf_cached_edit_study'
                cmd=e.native(args,Path(row['input']),prefix.with_suffix('.json'),'preview',extra,row['subject'],view,(1920,1080));args.binary=current
            record=local.run(cmd,prefix.with_suffix('.json'))
            passes,metrics=profile(prefix,frames,warmup);values=passes['sdf.whole_scene']['values_ms']
            receipt['runs'].append(dict(case=row,view=view,route=route,repeat=repeat,**record,passes=passes,metrics=metrics,
                stats=dict(median_ms=statistics.median(values),p95_ms=percentile(values,.95),p99_ms=percentile(values,.99),
                    max_ms=max(values),over_16_67=sum(v>16.67 for v in values),over_11_11=sum(v>11.11 for v in values))))
            save(args.output/'timing.json',receipt);print(prefix.name,round(percentile(values,.95),3),flush=True)
    receipt['complete']=True;save(args.output/'timing.json',receipt)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase',choices=['inputs','geometry','replay','admission','caches','captures','prefixes','calibrate','timing'])
    p.add_argument('--inputs',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--pilot',action='store_true')
    p.add_argument('--binary',type=Path,default=ROOT/'build/release/projects/sdf/cubey_project_sdf_cached_stroke_study')
    p.add_argument('--public-binary',type=Path,default=RAW/'baseline-sdf')
    p.add_argument('--current-public',type=Path,default=ROOT/'build/release/projects/sdf/sdf')
    p.add_argument('--mode',choices=['local','local-dom'],default='local-dom')
    p.add_argument('--measure',action='store_true')
    p.add_argument('--groups',type=int,default=0)
    args=p.parse_args();args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=False)
    if args.phase not in ('inputs','admission') and not args.inputs:p.error('--inputs required')
    globals()[args.phase](args)


if __name__=='__main__':main()
