#!/usr/bin/env python3
"""CPU admission and hard-Boolean occupancy probes, not renderer qualification."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT / 'projects/sdf/tools'))
import cached_edit_loop as edit


def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.work.mkdir(parents=True,exist_ok=False)
    binary=ROOT/'build/release/projects/sdf/cubey_project_sdf_cached_commit_study'
    runs=[]
    for subject,length in [('guardian',12),('robot',16)]:
        for cut in [False,True]:
            previous=edit.subject_scene(subject);cases=[];center=edit.CUT_CENTERS[subject]
            for i in range(length):
                point=[center[j]+(.002*i if j==0 else 0) for j in range(3)]
                c=edit.case(f'{subject}-stroke-{i}',previous,
                    dict(name=f'stroke-tool-{i}',type='sphere',radius=.025,position=point),[],cut)
                c['scene']['root']['name']=f'stroke-root-{i}'
                cases.append(c);previous=c['scene']
            name=subject+('-cut' if cut else '-add');inp=args.work/(name+'.json');out=args.work/(name+'-result.json')
            inp.write_text(json.dumps(edit.manifest(cases),indent=2)+'\n')
            cmd=[str(binary),'--commit-input',str(inp),'--commit-output',str(out),'--commit-validate-only']
            run=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
            result=dict(subject=subject,operation='cut' if cut else 'add',requested=length,
                command=cmd,exit_code=run.returncode,stderr=run.stderr,input_sha256=sha(inp),
                native=json.loads(out.read_text()) if out.exists() else None)
            assert run.returncode==0 and result['native']['validated_without_gpu'],result
            runs.append(result)
    # Exhaust every base occupancy, tool occupancy mask and operation mask for
    # N=0..8. An active hard-union sets inside; active hard-subtract sets outside.
    # If no tool contains the point, the old cache supplies occupancy.
    checks=0
    for n in range(9):
        for active in range(1<<n):
            for adds in range(1<<n):
                for base in (False,True):
                    folded=base
                    for i in range(n):
                        contains=bool(active & (1<<i));add=bool(adds & (1<<i))
                        folded=(folded or contains) if add else (folded and not contains)
                    latest=active.bit_length()-1
                    expected=bool(adds & (1<<latest)) if latest>=0 else base
                    assert folded==expected
                    checks+=1
    # Independent 1D interval sample model, avoiding tangency/epsilon claims.
    rng=random.Random(20261030);interval_points=0
    for _ in range(1000):
        tools=[sorted([rng.uniform(-1,1),rng.uniform(-1,1)]) for _ in range(8)]
        modes=[rng.choice([False,True]) for _ in tools]
        for _ in range(64):
            t=rng.uniform(-1,1);base=rng.choice([False,True]);value=base;active=[]
            for i,((a,b),add) in enumerate(zip(tools,modes)):
                inside=a<t<b
                value=(value or inside) if add else (value and not inside)
                if inside:active.append(i)
            assert value==(modes[active[-1]] if active else base)
            interval_points+=1
    result=dict(scope='CPU-only source admission and Boolean identities; no native eight-edit interval rendering or update performance',
        source_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        script_sha256=sha(__file__),binary_sha256=sha(binary),edit_harness_sha256=sha(Path(edit.__file__)),
        admission_runs=runs,exhaustive_occupancy_cases=checks,random_interval_points=interval_points,
        queue_example=dict(total_uncommitted_cap=8,frozen_prefix=4,remaining_input_slots=4,
            arrival_stamps_per_second=60,maximum_steady_batch_seconds=4/60,
            gpu_work_target_ms_if_one_ms_per_frame=4,
            note='Arithmetic service constraint, not measured throughput'),
        passed=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(passed=True,admitted_streams=len(runs),
        exhaustive_occupancy_cases=checks,random_interval_points=interval_points)))


if __name__=='__main__':main()
