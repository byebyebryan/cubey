#!/usr/bin/env python3
"""Capture the two native form treatments; retain exact scene/config inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();root=Path(__file__).resolve().parents[3]
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    binary=root/'build/dev/projects/sdf/sdf'
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    manifest=dict(source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        dirty=bool(subprocess.check_output(['git','status','--porcelain'],text=True)),
        binary_sha256=sha(binary),shader_sha256={p.name:sha(p) for p in (binary.parent/'shaders').glob('*.spv')},runs={})
    for subject in ('guardian','driftwood'):
        for variant in 'ab':
            config=root/f'projects/sdf/presets/refinement-auditions/{subject}-form-{variant}.json'
            scene=root/json.loads(config.read_text())['authored-scene']
            shutil.copy2(config,out/config.name);shutil.copy2(scene,out/scene.name)
            for view,opts in dict(hero=[],front=['--yaw',0,'--pitch',.02],
                profile=['--yaw',1.5707963,'--pitch',.1],
                rear=['--yaw',3.49,'--pitch',.19,'--sun-azimuth',-30]).items():
                name=f'{subject}-{variant}-{view}'
                command=[str(binary),'--headless','--require-validation','--config',str(config),
                    '--width','800','--height','800','--clay',*map(str,opts),'--output',str(out/f'{name}.png')]
                result=subprocess.run(command,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                (out/f'{name}.log').write_text(result.stdout)
                if result.returncode:raise RuntimeError(result.stdout)
                manifest['runs'][name]=dict(command=command,image_sha256=sha(out/f'{name}.png'),
                    scene_sha256=sha(scene),config_sha256=sha(config),
                    stats=[s for s in result.stdout.splitlines() if s.startswith('sdf:')])
                (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
                print(name,'passed',flush=True)


if __name__=='__main__':main()
