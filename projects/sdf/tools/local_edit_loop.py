#!/usr/bin/env python3
"""Serial private local-cache qualification and measurement, with pinned controls."""
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

RAW=ROOT/'outputs/sdf/local-edit-loop-20261030'
PREPARE=ROOT/'outputs/sdf/cached-commit-loop-20261030/qualified-v2/prepare/prepare.json'
MUTABLE={'projects/sdf/CMakeLists.txt','projects/sdf/sdf_cached_commit.h',
    'projects/sdf/sdf_cached_commit.cpp','projects/sdf/tools/cached_commit_study.cpp',
    'projects/sdf/sdf_renderer.cpp','projects/sdf/shaders/cached_edit_trace.glsl'}


def save(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')


def identity(args):
    base=json.loads((RAW/'baseline.json').read_text());shaders=args.binary.parent/'shaders'
    assert all(digest(shaders/n)==h for n,h in base['shaders'].items()),'original shader changed'
    assert all(digest(ROOT/n)==h for n,h in base['protected'].items() if n not in MUTABLE),'protected source changed'
    assert digest(args.binary.parent/'sdf')==base['binaries']['sdf'],'public executable changed'
    return dict(source_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        source_status=subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True),
        binary_sha256=digest(args.binary),harness_sha256=digest(Path(__file__)),
        shaders={p.name:digest(p) for p in sorted(shaders.glob('local_*.spv'))},
        original_shaders_unchanged=len(base['shaders']),public_executable_unchanged=True)


def run(cmd,out,allow_failure=False):
    before=gpu_state();start=time.monotonic()
    with out.with_suffix('.log').open('w') as f:r=subprocess.run([str(x) for x in cmd],cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
    after=gpu_state();log=out.with_suffix('.log').read_text()
    if 'VUID-' in log or 'Validation Error' in log:raise RuntimeError(log[-3000:])
    native=json.loads(out.read_text()) if out.exists() else None
    result=dict(command=[str(x) for x in cmd],exit_code=r.returncode,wall_seconds=time.monotonic()-start,
        gpu_before=before,gpu_after=after,log_sha256=digest(out.with_suffix('.log')),native=native)
    if r.returncode and not allow_failure:raise RuntimeError(log[-3000:])
    return result


def command(args,inp,out,mode=None):
    return [args.binary,'--commit-input',inp,'--commit-output',out,'--commit-mode',mode or args.mode]


def inputs(args):
    original=json.loads(PREPARE.read_text());result={k:original[k] for k in ('fixtures','subjects','chains')}
    for key,rows in result.items():
        for row in rows:
            src=Path(row['input']);dst=args.output/src.name;dst.write_bytes(src.read_bytes());row['input']=str(dst)
    extra=[]
    for name,base,tool,cut in [
        ('erase-whole',document(sphere('body',.16)),sphere('tool',.35),True),
        ('disconnected-add',document(sphere('body',.04,[-.25,0,0])),sphere('tool',.02,[.25,0,0]),False),
        ('face-contact-add',document(dict(name='body',type='box',half_extent=[.125]*3)),sphere('tool',1/256,[.125,0,0]),False),
        ('workspace-wide-cut',document(sphere('body',.1,[.45,0,0])),sphere('tool',.04,[.499,0,0]),True),
    ]:
        c=edit.case(name,base,tool,[],cut);path=args.output/(name+'.json');save(path,edit.manifest([c]));extra.append(dict(name=name,input=str(path)))
    result['fixtures']+=extra
    previous=document(sphere('body',.04));chain=[]
    for i in range(8):
        c=edit.case(f'recreate-{i}',previous,sphere(f'tool-{i}',.12 if i%2==0 else .04),[],i%2==0)
        c['scene']['root']['name']=f'recreate-root-{i}';previous=c['scene'];chain.append(c)
    path=args.output/'recreate-chain.json';save(path,edit.manifest(chain))
    result['chains'].append(dict(name='recreate-chain',subject='fixture',length=8,input=str(path)))
    result.update(identity(args));result['complete']=True;save(args.output/'inputs.json',result)
    print(json.dumps(dict(fixtures=len(result['fixtures']),subjects=len(result['subjects']),chains=len(result['chains']))))


def qualify(args):
    source=json.loads(args.inputs.read_text());rows=source['chains'] if args.chains else source['fixtures']+source['subjects']
    receipt=dict(identity(args),mode=args.mode,runs=[],complete=False)
    for row in rows:
        out=args.output/(row['name']+'.json');cmd=command(args,row['input'],out)+['--commit-qualify']
        if args.chains:cmd+=['--commit-chain']
        if args.dump:cmd+=['--commit-dump']
        record=run(cmd,out);native=record['native'];assert native['complete'] and native['all_comparisons_passed']
        checks=[]
        for case in native['runs']:
            assert case['comparison']['passed']
            if args.mode.startswith('local'):assert case['retained_slots_moved']==0
            if args.dump:
                for a,b in zip(case['candidate']['files'],case['canonical']['files']):
                    identical=filecmp.cmp(a,b,shallow=False);assert identical
                    checks.append(dict(candidate=a,canonical=b,byte_identical=identical))
        receipt['runs'].append(dict(case=row,independent_files=checks,**record));save(args.output/'qualification.json',receipt)
        print(row['name'],'PASS',flush=True)
    receipt['complete']=True;receipt['passed']=True;save(args.output/'qualification.json',receipt)


def copies(args):
    source=json.loads(args.inputs.read_text());rows=[r for r in source['subjects'] if r['radius']==.065 and r['operation']=='add']
    receipt=dict(identity(args),runs=[],complete=False)
    for repeat in range(3):
        for row in rows:
            for slab in ([0,4,16] if repeat%2==0 else [16,4,0]):
                out=args.output/f"{row['subject']}-{slab}-{repeat}.json"
                cmd=command(args,row['input'],out)+['--commit-copy-only','--commit-copy-slab-mib',str(slab),'--commit-warmup','3','--commit-trials','10']
                receipt['runs'].append(dict(case=row,repeat=repeat,slab_mib=slab,**run(cmd,out)))
                save(args.output/'copies.json',receipt);print(row['subject'],slab,repeat,flush=True)
    receipt['complete']=True;save(args.output/'copies.json',receipt)


def timing(args):
    source=json.loads(args.inputs.read_text());rows=[r for r in source['subjects'] if r['radius']==args.radius]
    receipt=dict(identity(args),runs=[],complete=False)
    for repeat in range(3):
        for row in rows:
            for mode in (['reuse','local'] if repeat%2==0 else ['local','reuse']):
                out=args.output/f"{row['name']}-{mode}-{repeat}.json"
                cmd=command(args,row['input'],out,mode)+['--commit-warmup','3','--commit-trials','10']
                receipt['runs'].append(dict(case=row,repeat=repeat,mode=mode,**run(cmd,out)))
                save(args.output/'timing.json',receipt);print(row['name'],mode,repeat,flush=True)
    receipt['complete']=True;save(args.output/'timing.json',receipt)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase',choices=['inputs','qualify','copies','timing'])
    p.add_argument('--output',type=Path,required=True);p.add_argument('--inputs',type=Path)
    p.add_argument('--mode',choices=['local','local-dom','reuse','full'],default='local')
    p.add_argument('--chains',action='store_true');p.add_argument('--dump',action='store_true')
    p.add_argument('--radius',type=float,default=.065)
    p.add_argument('--binary',type=Path,default=ROOT/'build/release/projects/sdf/cubey_project_sdf_cached_commit_study')
    args=p.parse_args();args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=False)
    if args.phase!='inputs' and not args.inputs:p.error('--inputs required')
    globals()[args.phase](args)


if __name__=='__main__':main()
