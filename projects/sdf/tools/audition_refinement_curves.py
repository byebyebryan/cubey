#!/usr/bin/env python3
"""Controlled native curve comparison at fixed segment count and field settings."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from author_sculptures import Sculpt, preset
from refinement_author import Refinement, samples
from sdf_author import material, write_scene
from sdf_probe import distance


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[3]
    out=args.output.resolve(); out.mkdir(parents=True,exist_ok=False)
    binary=root/'build/dev/projects/sdf/sdf'
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    manifest=dict(source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
                  binary_sha256=sha(binary),shader_sha256={p.name:sha(p) for p in (binary.parent/'shaders').glob('*.spv')},runs={})
    fixtures={
        'horn':([(-.12,-.16,0),(-.20,-.09,-.015),(-.235,0,-.04),(-.22,.09,-.08)],
                [.05,.034,.014,.001]),
        'trunk':([(-.15,-.265,-.005),(-.085,-.13,.005),(0,.015,.015),(.065,.17,-.025),(.025,.305,-.06)],
                 [.10,.095,.08,.055,.015])}
    for subject,(points,radii) in fixtures.items():
        baseline=Sculpt(True); base=baseline.stroke(subject,points,radii,'Original')
        trees={}
        for mode in ('original','width','joined','soft','denser'):
            s=Refinement()
            tree=base if mode=='original' else s.segments(subject,samples(points,radii,7 if mode=='denser' else 5),mode,
                                                         {'joined':.0012,'soft':.006}.get(mode,0))
            trees[mode]=tree
            scene=out/f'{subject}-{mode}.json'
            write_scene(scene,f'{subject} / {mode}',[material('body','AA7955',.75)],tree)
            cfg=preset(scene,subject);cfg.update(distance=.95 if subject=='horn' else 1.45,yaw=3.49,pitch=.19)
            cfg['target-y']=-.02
            config=out/f'{subject}-{mode}-config.json';config.write_text(json.dumps(cfg))
            for field in ('default',):
                name=f'{subject}-{mode}-{field}'
                command=[str(binary),'--headless','--require-validation','--config',str(config),
                         '--width','720','--height','720','--clay','--sun-azimuth','-30',
                         '--validate-field','--output',str(out/f'{name}.png')]
                result=subprocess.run(command,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                (out/f'{name}.log').write_text(result.stdout)
                if result.returncode:raise RuntimeError(result.stdout)
                manifest['runs'][name]=dict(command=command,scene_sha256=sha(scene),image_sha256=sha(out/f'{name}.png'),
                                            stats=[l for l in result.stdout.splitlines() if l.startswith('sdf:')])
                print(name,'passed',flush=True)
                (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        # Compare direct-expression curvature along a centerline-normal strip.
        # This diagnostic is independent of the cached native field resolution.
        center=samples(points,radii,60)
        manifest[subject+'_direct_strip']={mode:[distance(tree,(p[0],p[1],p[2]+r)) for p,r in center]
            for mode,tree in trees.items()}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')


if __name__=='__main__':main()
