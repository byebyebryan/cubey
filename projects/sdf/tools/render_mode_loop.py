#!/usr/bin/env python3
"""Serial native render-mode promotion evidence. Fresh output directories."""
import argparse
import json
from pathlib import Path

from acceleration_loop import gpu_state, save
from investigate_guardian import digest, invoke, metadata
from quality_speed_loop import command, profile


def settings(args, scene, view, route, samples):
    flags=command(args,scene,view,route)
    flags[flags.index('--supersample')+1]=str(samples)
    return flags


def sampling(args):
    receipt=dict(metadata(args.binary),tool_sha256=digest(Path(__file__)),runs=[],captures=[],complete=False)
    cases=[(scene,view,route,ss) for scene in ('guardian','robot') for view in ('preset','close')
           for route in ('sampled','refine03125','combined','source') for ss in (1,2)]
    for repeat in (1,2,3):
        for scene,view,route,ss in cases if repeat%2 else reversed(cases):
            prefix=args.output/f'{scene}-{view}-{route}-ss{ss}-r{repeat}'
            flags=settings(args,scene,view,route,ss)+['--headless-render-only','--frames','180',
                '--profile-warmup-frames','30','--profile-output',str(prefix)]
            before=gpu_state();wall=invoke(flags,prefix,timeout=240);after=gpu_state()
            passes,metrics=profile(prefix,180,30)
            receipt['runs'].append(dict(scene=scene,view=view,route=route,supersample=ss,repeat=repeat,
                command=flags,wall_seconds=wall,gpu_before=before,gpu_after=after,passes=passes,metrics=metrics))
            save(args.output/'sampling.json',receipt)
            print(prefix.name,round(passes['sdf.whole_scene']['p95_ms'],3),flush=True)
    # Native captures follow the timed batch, never overlap it.
    for scene,view,route,ss in cases:
        prefix=args.output/f'{scene}-{view}-{route}-ss{ss}'
        flags=settings(args,scene,view,route,ss)+['--frames','1','--output',str(prefix.with_suffix('.png'))]
        invoke(flags,prefix,timeout=240)
        receipt['captures'].append(dict(scene=scene,view=view,route=route,supersample=ss,command=flags,
            image=str(prefix.with_suffix('.png')),image_sha256=digest(prefix.with_suffix('.png'))))
        save(args.output/'sampling.json',receipt)
    receipt['complete']=True;save(args.output/'sampling.json',receipt)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase',choices=['sampling'])
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--binary',type=Path,default=Path('build/release/projects/sdf/sdf'))
    args=p.parse_args();args.shadows=1;args.binary=args.binary.resolve();args.output=args.output.resolve()
    args.output.mkdir(parents=True,exist_ok=False)
    globals()[args.phase](args)


if __name__=='__main__':main()
