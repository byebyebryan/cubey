#!/usr/bin/env python3
"""Capture the two native clay blockouts of each sculpture; preserve run receipts."""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import subprocess

from author_sculptures import SUBJECTS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binary = root/'build/dev/projects/sdf/sdf'
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    manifest = dict(captured_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
        source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        source_dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip()),
        binary_sha256=sha(binary), shader_sha256={p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))},
        runs={})
    for subject in SUBJECTS:
        for variant in 'ab':
            config = Path(f'projects/sdf/presets/sculpture-blockouts/{subject}-{variant}.json')
            for view, options in {'hero': [], 'front':['--yaw',0,'--pitch',.02],
                                  'profile':['--yaw',1.5707963,'--pitch',.06],
                                  'before-carve':[]}.items():
                # Prefix selection is an actual saved CSG state, not a rendered mockup.
                if view == 'before-carve':
                    scene = json.loads((root/json.loads((root/config).read_text())['authored-scene']).read_text())
                    cutters = set()
                    def walk(node, negative=False):
                        if 'children' not in node:
                            if negative: cutters.add(node['name'])
                        else:
                            for i, child in enumerate(node['children']):
                                walk(child, negative != (node['type'] in ('subtract','smooth-subtract') and i == 1))
                    walk(scene['root'])
                    first_cut = next(i for i,s in enumerate(scene['history']) if s['node'] in cutters)
                    total = .65+sum(s['seconds'] for s in scene['history'])
                    cursor = .65+sum(s['seconds'] for s in scene['history'][:first_cut-1])
                    time = (cursor+.7*scene['history'][first_cut-1]['seconds'])/total*40
                    options = ['--motion','presentation','--construction-seconds',40,'--duration',50,'--time',time]
                name = f'{subject}-{variant}-{view}'
                command = [str(binary),'--headless','--require-validation','--config',str(root/config),
                           '--width','720','--height','720','--clay',*map(str,options),
                           '--output',str(out/f'{name}.png')]
                result = subprocess.run(command,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                (out/f'{name}.log').write_text(result.stdout)
                if result.returncode or 'validation error' in result.stdout.lower():
                    raise RuntimeError(name+': '+result.stdout)
                manifest['runs'][name]=dict(command=command,image=f'{name}.png',image_sha256=sha(out/f'{name}.png'),
                    config_sha256=sha(root/config),
                    scene_sha256=sha(root/json.loads((root/config).read_text())['authored-scene']),
                    stats=[s for s in result.stdout.splitlines() if s.startswith('sdf:')])
                (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
                print(name, 'passed', flush=True)


if __name__ == '__main__':
    main()
