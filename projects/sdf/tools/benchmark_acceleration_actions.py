#!/usr/bin/env python3
"""Bounded existing content-history transitions for the selected E3 structures."""
import argparse
import json
from pathlib import Path
from sdf_paths import resolve_sdf_path
from acceleration_loop import PRESETS, base, flags, gpu_state, save, study_metadata
from query_study import ROOT, invoke, digest
from benchmark_tracing import percentile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--binary',type=Path,default=ROOT/'build/release/projects/sdf/sdf')
    args=p.parse_args();args.binary=args.binary.resolve();args.output=args.output.resolve()
    args.output.mkdir(parents=True,exist_ok=False)
    receipt=dict(study_metadata(args.binary),action_runner_sha256=digest(Path(__file__)),runs=[])
    inputs={}
    for scene,preset in PRESETS.items():
        source=json.loads((ROOT/str(resolve_sdf_path(f'projects/sdf/scenes/{preset}.json'))).read_text())
        total=len(source['history'])
        selected=[1,total//4,total//2,3*total//4,total-1,total,total-1,total,total//2,1]
        actions=[dict(label=f'{"backward" if i and n<selected[i-1] else "forward"} prefix {n}/{total}',
            scene=source,visible_scene=source,history_prefix=n,visible_history_prefix=n) for i,n in enumerate(selected)]
        inputs[scene]=args.output/f'{scene}-input.json'
        save(inputs[scene],dict(format='cubey.sdf.action-study',version=1,actions=actions))
    cases=[(s,n,v) for s in PRESETS for n,v in [('A64','control'),('A64-far','far'),('A64-root-far','root-far')]]
    for repeat in range(1,4):
        for scene,name,variant in cases if repeat%2 else reversed(cases):
            prefix=args.output/f'{scene}-{name}-r{repeat}'
            command=base(args)+['--config',str(resolve_sdf_path(f'projects/sdf/presets/{PRESETS[scene]}.json')),
                '--headless-render-only','--frames','10','--width','320','--height','180',
                *flags('analytic',variant),'--comparison-actions',str(inputs[scene]),
                '--comparison-output',str(prefix.with_suffix('.json'))]
            before=gpu_state();invoke(command,prefix);after=gpu_state()
            data=json.loads(prefix.with_suffix('.json').read_text())
            if len(data['actions'])!=10:raise RuntimeError('incomplete action replay')
            values=[r['request_to_waited_gpu_completion_ms'] for r in data['actions']]
            # A separate pair isolates total-1 -> total from partial histories.
            full=[data['actions'][i]['request_to_waited_gpu_completion_ms'] for i in (5,7)]
            receipt['runs'].append(dict(scene=scene,name=name,repeat=repeat,command=command,
                gpu_before=before,gpu_after=after,p95_ms=percentile(values,.95),max_ms=max(values),
                full_prefix_completion_ms=full,actions=data['actions']))
            save(args.output/'actions.json',receipt)
            print(scene,name,repeat,'p95',round(percentile(values,.95),2),'full',full,flush=True)
    receipt['complete']=True;save(args.output/'actions.json',receipt)


if __name__=='__main__':main()
