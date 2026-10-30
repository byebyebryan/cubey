#!/usr/bin/env python3
"""Serial, source-pinned cache-commit qualification and measurement."""
import argparse
import copy
import filecmp
import hashlib
import json
from pathlib import Path
import subprocess
import time

import cached_edit_loop as edit
from acceleration_loop import gpu_state
from investigate_guardian import ROOT, digest
from tracing_comparison import document, sphere


def save(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n')


def identity(args):
    shader_dir=args.binary.parent/'shaders'
    baseline=json.loads(args.baseline.read_text())
    changed=[name for name,h in baseline['shader_hashes'].items() if digest(shader_dir/name)!=h]
    if changed:raise RuntimeError(f'original shaders changed: {changed}')
    allowed={'projects/sdf/CMakeLists.txt','projects/sdf/sdf_field.h','projects/sdf/tools/cached_edit_study.cpp'}
    changed=[name for name,h in baseline['protected'].items() if name not in allowed and digest(ROOT/name)!=h]
    if changed:raise RuntimeError(f'protected source/content changed: {changed}')
    return dict(source_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        source_status=subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True),
        binary_sha256=digest(args.binary),edit_binary_sha256=digest(args.edit_binary),
        harness_sha256=digest(Path(__file__)),
        shaders={p.name:digest(p) for p in sorted(shader_dir.glob('commit_*.spv'))},
        original_shader_count=len(baseline['shader_hashes']),
        protected_unchanged=len(baseline['protected'])-len(allowed),mutable_existing=sorted(allowed))


def run(cmd, output, *, allow_failure=False):
    before=gpu_state(); start=time.monotonic()
    with output.with_suffix('.log').open('w') as log:
        p=subprocess.run([str(x) for x in cmd],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    wall=time.monotonic()-start;after=gpu_state();log=output.with_suffix('.log').read_text()
    if 'VUID-' in log or 'Validation Error' in log:raise RuntimeError(f'validation failure: {output}')
    if p.returncode and not allow_failure:raise RuntimeError(f'command failed ({p.returncode}): {output}\n{log[-2500:]}')
    return dict(command=[str(x) for x in cmd],exit_code=p.returncode,wall_seconds=wall,
                gpu_before=before,gpu_after=after,log_sha256=digest(output.with_suffix('.log')),
                native=json.loads(output.read_text()) if output.exists() else None)


def native(args, inp, out, mode=None):
    return [args.binary,'--commit-input',inp,'--commit-output',out,
            '--commit-mode',mode or args.mode,'--commit-regions',str(args.regions)]


def prepare(args):
    receipt=dict(identity(args),fixtures=[],subjects=[],chains=[],admission=[],complete=False)
    cases=edit.fixtures()
    box=document(dict(name='body',type='box',half_extent=[.125]*3))
    smooth=document(dict(name='smooth-base',type='smooth-union',radius=.035,
        children=[sphere('a',.14,[-.07,0,0]),sphere('b',.14,[.07,0,0])]))
    cases += [
        edit.case('lattice-box-add',box,sphere('pending',.0625,[.1875,0,0]),[]),
        edit.case('lattice-box-cut',box,dict(name='pending',type='box',half_extent=[.0625,.0625,.25]),[],True),
        edit.case('smooth-base-hard-add',smooth,sphere('pending',.06,[0,.16,0]),[]),
        edit.case('smooth-base-hard-cut',smooth,sphere('pending',.06),[],True),
        edit.case('scaled-tool',box,dict(name='pending',type='box',half_extent=[.04,.03,.06],
            scale=1.25,position=[.15,0,0],rotation_xyzw=[0,.1986693308,0,.9800665778]),[]),
        edit.case('thin-base',document(dict(name='body',type='box',half_extent=[.00001,.125,.125])),
            sphere('pending',.04,[0,.1,0]),[]),
    ]
    for c in cases:
        inp=args.output/(c['name']+'.json');save(inp,edit.manifest([c]))
        receipt['fixtures'].append(dict(name=c['name'],input=str(inp)))
    for subject in ('guardian','robot'):
        base=edit.subject_scene(subject)
        for op in ('add','cut'):
            center=edit.CUT_CENTERS[subject] if op=='cut' else ([.03,.075,.28] if subject=='guardian' else [0,.1,.15])
            for radius in (.02,.065,.15):
                name=f'{subject}-{op}-{radius:g}'
                c=edit.case(name,base,sphere('pending-brush',radius,center),[],op=='cut')
                inp=args.output/(name+'.json');save(inp,edit.manifest([c]))
                row=dict(name=name,input=str(inp),subject=subject,operation=op,radius=radius)
                receipt['subjects'].append(row)
        limit=8 if subject=='guardian' else 16
        chain=[];previous=copy.deepcopy(base)
        for i in range(limit):
            primitive=sphere(f'chain-tool-{i}',.025,[center[j]+(.004*i if j==0 else 0) for j in range(3)])
            c=edit.case(f'{subject}-chain-{i}',previous,primitive,[],bool(i%2))
            c['scene']['root']['name']=f'chain-root-{i}'
            chain.append(c);previous=c['scene']
        while chain:
            inp=args.output/(subject+'-chain.json');save(inp,edit.manifest(chain))
            out=args.output/(subject+f'-chain-admit-{len(chain)}.json')
            record=run(native(args,inp,out)+['--commit-validate-only'],out,allow_failure=True)
            receipt['admission'].append(dict(subject=subject,requested=len(chain),**record))
            if record['exit_code']==0:break
            chain.pop()
        if not chain:raise RuntimeError('no admitted chain')
        receipt['chains'].append(dict(name=subject+'-chain',subject=subject,length=len(chain),input=str(inp)))
    all_cases=[json.loads(Path(r['input']).read_text())['cases'][0]
               for r in receipt['fixtures']+receipt['subjects']]
    all_input=args.output/'all-admission-input.json';save(all_input,edit.manifest(all_cases))
    all_output=args.output/'all-admission.json'
    receipt['all_admission']=run(native(args,all_input,all_output)+['--commit-validate-only'],all_output)
    receipt['complete']=True;save(args.output/'prepare.json',receipt)
    print(json.dumps(dict(fixtures=len(receipt['fixtures']),subjects=len(receipt['subjects']),
        chains=[(r['subject'],r['length']) for r in receipt['chains']])),flush=True)


def independent_files(row):
    a,b=row['candidate'],row['canonical']
    if 'files' not in a:return None
    checks=[]
    for x,y in zip(a['files'],b['files']):
        checks.append(dict(candidate=x,canonical=y,byte_identical=filecmp.cmp(x,y,shallow=False)))
    if not all(c['byte_identical'] for c in checks):raise RuntimeError('independent raw snapshot comparison failed')
    return checks


def qualify(args):
    inputs=json.loads(args.inputs.read_text())
    receipt=dict(identity(args),mode=args.mode,regions=args.regions,runs=[],complete=False,passed=True)
    rows=inputs['chains'] if args.chains else inputs['fixtures']+inputs['subjects']
    inherited={}
    if args.resume:
        old=json.loads(args.resume.read_text())
        for key in ('binary_sha256','edit_binary_sha256','shaders'):
            if old[key]!=receipt[key]:raise RuntimeError('resume runtime identity changed')
        if old['mode']!=args.mode or old['regions']!=args.regions:raise RuntimeError('resume mode changed')
        inherited={r['case']['name']:r for r in old['runs']}
        receipt['inherited_from']=dict(path=str(args.resume.resolve()),sha256=digest(args.resume),
            source_head=old['source_head'],harness_sha256=old['harness_sha256'])
    for i,row in enumerate(rows):
        if row['name'] in inherited:
            previous=inherited[row['name']]
            if digest(Path(previous['case']['input']))!=digest(Path(row['input'])):
                raise RuntimeError('resume input changed')
            if previous['exit_code'] or not previous['native']['all_comparisons_passed']:
                raise RuntimeError('cannot inherit failed qualification')
            if args.dump and not all(previous['independent_files']):raise RuntimeError('resume lacks raw dumps')
            receipt['runs'].append(dict(previous,inherited=True))
            print(f"{i+1}/{len(rows)} {row['name']}: inherited exact-runtime PASS",flush=True)
            continue
        out=args.output/(row['name']+'.json')
        cmd=native(args,row['input'],out)+['--commit-qualify']
        if args.chains:cmd+=['--commit-chain']
        if args.dump:cmd+=['--commit-dump']
        record=run(cmd,out,allow_failure=True)
        if record['native'] is None:raise RuntimeError(f'missing native result: {out}')
        comparisons=record['native']['all_comparisons_passed']
        files=[independent_files(r) for r in record['native']['runs']] if comparisons else []
        receipt['passed']=receipt['passed'] and record['exit_code']==0 and comparisons
        receipt['runs'].append(dict(case=row,**record,independent_files=files))
        save(args.output/'qualification.json',receipt)
        print(f"{i+1}/{len(rows)} {row['name']}: {'PASS' if comparisons else 'FAIL'}",flush=True)
        if not comparisons and args.mode=='reuse':raise RuntimeError('reuse correctness failure')
    receipt['complete']=True;save(args.output/'qualification.json',receipt)


def timing(args):
    inputs=json.loads(args.inputs.read_text())
    rows=[r for r in inputs['subjects'] if r['radius']==.065]
    modes=[('pruned',4),('pruned',16)] if args.diagnostic_pruning else [('full',4),('reuse',4)]
    repeats,warmup,trials=(1,1,3) if args.diagnostic_pruning else (3,3,10)
    cold_options=(False,) if args.diagnostic_pruning else (False,True)
    total=repeats*len(cold_options)*len(rows)*len(modes)
    receipt=dict(identity(args),runs=[],complete=False,warmup=warmup,measured_per_run=trials,repeats=repeats,
        diagnostic_unqualified_pruning=args.diagnostic_pruning)
    # Cold and preallocated results remain separate. Interleave order by repeat.
    for repeat in range(repeats):
        order=modes[repeat%len(modes):]+modes[:repeat%len(modes)]
        for cold in cold_options:
            for row in rows:
                for mode,axis in order:
                    out=args.output/f"{row['name']}-{mode}-{axis}-{'cold' if cold else 'warm'}-{repeat}.json"
                    cmd=[args.binary,'--commit-input',row['input'],'--commit-output',out,'--commit-mode',mode,
                        '--commit-regions',str(axis),'--commit-warmup',str(warmup),'--commit-trials',str(trials)]
                    if cold:cmd+=['--commit-cold']
                    record=run(cmd,out)
                    samples=record['native']['runs'][0]['trials']
                    if sum(not t['warmup'] for t in samples)!=receipt['measured_per_run']:raise RuntimeError('missing timed commits')
                    receipt['runs'].append(dict(case=row,repeat=repeat,cold=cold,mode=mode,regions=axis,**record))
                    save(args.output/'timing.json',receipt)
                    print(f"{len(receipt['runs'])}/{total} {out.stem}",flush=True)
    receipt['complete']=True;save(args.output/'timing.json',receipt)


def sequences(args):
    inputs=json.loads(args.inputs.read_text())
    receipt=dict(identity(args),runs=[],complete=False)
    for row in (r for r in inputs['subjects'] if r['radius']==.065):
        failures=['none']+(['allocation','cancel-before-submit','stale','discard-after-submit','build-capacity'] if row['operation']=='cut' else [])
        original=json.loads(Path(row['input']).read_text())
        for failure in failures:
            inp=args.output/f"{row['name']}-{failure}-input.json"
            data=copy.deepcopy(original);data['fault_before_publish']=failure!='none'
            if failure!='none':data['candidate_failure']=failure
            save(inp,data);out=inp.with_name(inp.stem.replace('-input','-result')+'.json')
            cmd=edit.native(args,inp,out,phase='sequence',subject=row['subject'],size=(960,540),
                extra=['--edit-commit-builder','reuse','--frames','4' if failure=='none' else '5','--headless-render-only'])
            cmd[0]=str(args.edit_binary)
            record=run(cmd,out);native_result=record['native'];frames=native_result['frames']
            if frames[0]['render_pending_operations']!=0 or frames[1]['render_pending_operations']!=1:
                raise RuntimeError('invalid pending activation')
            if failure!='none':
                if not filecmp.cmp(frames[1]['image'],frames[2]['image'],shallow=False):raise RuntimeError('failed candidate changed display')
                if frames[2]['pending_after_completed_frame']!=1:raise RuntimeError('failed candidate lost pending')
            last=native_result['pending_retired_after_completed_frame']
            if frames[last]['display']!='committed' or frames[last]['pending_after_completed_frame']!=0:
                raise RuntimeError('early/late pending retirement')
            scene=args.output/f"{row['name']}-canonical-scene.json";save(scene,original['cases'][0]['scene'])
            image=args.output/f"{row['name']}-original-cached.png"
            if not image.exists():
                control=edit.public_command(args.public_binary,row['subject'],scene,image)
                run(control,image.with_suffix('.control.json'))
            if not filecmp.cmp(frames[-1]['image'],image,shallow=False):raise RuntimeError('settled image differs from original Cached')
            retained=json.loads(Path(native_result['retained_source_file']).read_text())
            if len(retained.get('history',[]))!=len(original['cases'][0]['scene'].get('history',[])):
                raise RuntimeError('retained source/history mismatch')
            receipt['runs'].append(dict(case=row,failure=failure,**record,settled_byte_identical=True))
            save(args.output/'sequences.json',receipt);print(f"sequence {row['name']} {failure}: PASS",flush=True)
    receipt['complete']=True;save(args.output/'sequences.json',receipt)


def faults(args):
    inputs=json.loads(args.inputs.read_text())
    receipt=dict(identity(args),overflow=[],negative=[],complete=False)
    for name in ('lattice-box-cut','guardian-cut-0.065'):
        row=next(r for r in inputs['fixtures']+inputs['subjects'] if r['name']==name)
        out=args.output/(name+'-overflow.json')
        record=run(native(args,row['input'],out,'reuse')+['--commit-qualify','--commit-dump','--commit-fallback-limit','1'],out)
        r=record['native']['runs'][0]
        if not r['comparison']['passed'] or not r['trials'][0]['full_fallback'] or r['trials'][0]['work_counts'][7]!=1:
            raise RuntimeError('overflow did not produce a canonical result')
        receipt['overflow'].append(dict(**record,independent_files=independent_files(r)))
    base=document(sphere('body',.15));single=edit.edit_scene(base,sphere('pending',.05))
    changed_prefix=copy.deepcopy(single);changed_prefix['root']['children'][0]['radius']=.16
    changed_palette=copy.deepcopy(single);changed_palette['materials'][0]['roughness']=.9
    negatives={
        'multiple-pending':edit.edit_scene(single,sphere('second',.02)),
        'smoothing':dict(single,root=dict(single['root'],type='smooth-union',radius=.01)),
        'unsupported-capsule':edit.edit_scene(base,dict(name='pending',type='capsule',radius=.03,half_length=.04)),
        'rounded-tool':edit.edit_scene(base,dict(name='pending',type='box',half_extent=[.03]*3,rounding=.01)),
        'changed-prefix':changed_prefix,'changed-palette':changed_palette,
        'below-source-size-limit':dict(single,root=dict(name='tiny',type='box',half_extent=[2**-20,.125,.125])),
    }
    for name,scene in negatives.items():
        inp=args.output/(name+'-input.json');save(inp,edit.manifest([dict(name=name,base=base,scene=scene)]))
        out=args.output/(name+'-result.json')
        record=run(native(args,inp,out)+['--commit-validate-only'],out,allow_failure=True)
        if record['exit_code']==0 or record['native'] is not None:raise RuntimeError('invalid request admitted')
        receipt['negative'].append(dict(name=name,**record))
    chain=json.loads(Path(inputs['chains'][1]['input']).read_text())
    chain['cases']=chain['cases'][:2]
    chain['cases'][1]=edit.case('wrong-base',base,sphere('pending',.02),[])
    inp=args.output/'wrong-base-input.json';save(inp,chain);out=args.output/'wrong-base-result.json'
    record=run(native(args,inp,out)+['--commit-qualify','--commit-chain'],out,allow_failure=True)
    if record['exit_code']==0 or 'matching canonical fields' not in out.with_suffix('.log').read_text():
        raise RuntimeError('stale base identity was not rejected')
    receipt['negative'].append(dict(name='wrong-retained-base',**record))
    receipt['complete']=True;save(args.output/'faults.json',receipt)


def prune_probe(args):
    inputs=json.loads(args.inputs.read_text())
    root=args.inputs.parent.parent
    a4=json.loads((root/'pruned4/qualification.json').read_text())
    a16=json.loads((root/'pruned16/qualification.json').read_text())
    receipt=dict(identity(args),runs=[],complete=False)
    binary=args.binary.parent/'cubey_project_sdf_cached_commit_probe'
    receipt['probe_binary_sha256']=digest(binary)
    for name,reference in [('guardian-add-0.065',a4),('robot-cut-0.065',a16)]:
        row=next(r for r in reference['runs'] if r['case']['name']==name)
        source=json.loads(Path(row['case']['input']).read_text())['cases'][0]['scene']
        samples=row['native']['runs'][0]['comparison']['examples']
        if not samples:raise RuntimeError('missing diagnostic sample locations')
        inp=args.output/(name+'-input.json');save(inp,dict(scene=source,samples=samples))
        for axis in (4,16):
            out=args.output/f'{name}-A{axis}.json'
            record=run([binary,inp,out,str(axis)],out)
            receipt['runs'].append(dict(name=name,axis=axis,reference_axis=reference['regions'],**record))
    receipt['complete']=True;save(args.output/'prune-probe.json',receipt)


def retention(args):
    source=json.loads(args.inputs.read_text())
    receipt=dict(identity(args),runs=[],complete=False)
    for row in source['runs']:
        original=json.loads(Path(row['case']['input']).read_text())['cases'][0]['scene']
        retained=json.loads(Path(row['native']['retained_source_file']).read_text())
        if original['root']!=retained['root'] or original.get('history')!=retained.get('history'):
            raise RuntimeError('source tree or history changed while retaining the edit')
        if int(retained['geometry_hash'],16)!=row['native']['programs'][0]['edited_geometry_hash']:
            raise RuntimeError('retained geometry identity changed')
        if row['failure']!='none':continue
        image=args.output/(row['case']['name']+'-retained.png')
        cmd=edit.public_command(args.public_binary,row['case']['subject'],
            row['native']['retained_source_file'],image)
        record=run(cmd,image.with_suffix('.control.json'))
        if not filecmp.cmp(row['native']['frames'][-1]['image'],image,shallow=False):
            raise RuntimeError('retained source replay changed the canonical result')
        receipt['runs'].append(dict(case=row['case'],**record,byte_identical=True,
            image=str(image),retained_source=row['native']['retained_source_file']))
    if len(receipt['runs'])!=4:raise RuntimeError('missing retained-source consumers')
    receipt['all_sequence_source_trees_and_histories_equal']=True
    receipt['complete']=True;save(args.output/'retention.json',receipt)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase',choices=['prepare','qualify','timing','sequences','faults','prune-probe','retention'])
    p.add_argument('--output',type=Path,required=True);p.add_argument('--inputs',type=Path)
    p.add_argument('--mode',choices=['full','reuse','pruned'],default='reuse');p.add_argument('--regions',type=int,choices=[4,16],default=4)
    p.add_argument('--chains',action='store_true');p.add_argument('--dump',action='store_true')
    p.add_argument('--resume',type=Path)
    p.add_argument('--diagnostic-pruning',action='store_true')
    p.add_argument('--binary',type=Path,default=ROOT/'build/release/projects/sdf/cubey_project_sdf_cached_commit_study')
    p.add_argument('--edit-binary',type=Path,default=ROOT/'build/release/projects/sdf/cubey_project_sdf_cached_edit_study')
    p.add_argument('--baseline',type=Path,default=ROOT/'outputs/sdf/cached-commit-loop-20261030/baseline.json')
    p.add_argument('--public-binary',type=Path,default=ROOT/'outputs/sdf/cached-commit-loop-20261030/baseline-sdf')
    args=p.parse_args();args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=False)
    if args.phase!='prepare' and not args.inputs:p.error('--inputs is required')
    globals()[args.phase.replace('-','_')](args)


if __name__=='__main__':main()
