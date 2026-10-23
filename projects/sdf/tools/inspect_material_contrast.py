#!/usr/bin/env python3
"""Capture a bounded material audition, optionally using the first report's inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--baseline', type=Path, help='Original comparison capture directory')
    p.add_argument('--materials', nargs='+', default=['plain', 'ceramic', 'clay'])
    p.add_argument('--components', action='store_true', help='Also inspect clay lobes and roughness')
    a = p.parse_args()
    root = Path(__file__).resolve().parents[3]
    binary = root/'build/release/projects/sdf/sdf'
    out = a.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    read = lambda p: json.loads(p.read_text())
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    receipt = dict(binary_sha256=sha(binary), shader_sha256={p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))},
                   source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
                   baseline=str(a.baseline) if a.baseline else None, runs={})
    for subject in ['sphere', 'guardian']:
        for kind in a.materials:
            case = f'{subject}-{kind}'
            source = a.baseline/case/'source' if a.baseline else root/'projects/sdf'
            cfg = read(source/'preset.json' if a.baseline else source/f'presets/material-study/{case}.json')
            scene = source/'scene.json' if a.baseline else root/cfg['authored-scene']
            directory = out/case; directory.mkdir()
            shutil.copy2(scene, directory/'scene.json')
            cfg['authored-scene'] = str(directory/'scene.json')
            for view in ['hero', 'detail']:
                config = dict(cfg)
                if view == 'detail':
                    config.update({'distance':.72 if subject=='sphere' else .85, 'target-y':0 if subject=='sphere' else .09})
                preset = directory/f'{view}-preset.json'; preset.write_text(json.dumps(config,indent=2)+'\n')
                modes = {'shaded':0, 'roughness':7, 'surface-normal':8, 'diffuse':10, 'reflection':11} if a.components and kind=='clay' else {'shaded':0}
                for name, mode in modes.items():
                    image = directory/f'{view}-{name}.png'
                    cmd = [str(binary),'--headless','--require-validation','--config',str(preset),'--width','960','--height','960','--debug',str(mode),'--output',str(image)]
                    r = subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                    (directory/f'{view}-{name}.log').write_text(r.stdout)
                    assert r.returncode==0 and 'validation error' not in r.stdout.lower(),r.stdout
                    receipt['runs'][f'{case}-{view}-{name}'] = dict(command=cmd,capture=str(image.relative_to(out)),sha256=sha(image))
                    (out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')
                    print(case,view,name,'passed',flush=True)
    receipt['passed']=True
    (out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')


if __name__=='__main__':main()
