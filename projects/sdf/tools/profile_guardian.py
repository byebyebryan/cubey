#!/usr/bin/env python3
"""Measure guardian steady shading separately from synchronous field builds."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
from qualify import distribution


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--binary',type=Path,default=Path('build/release/projects/sdf/sdf'))
    p.add_argument('--case',nargs=2,action='append',metavar=('NAME','PRESET'),
                   help='Override measured cases; repeat for a matched comparison')
    a=p.parse_args();root=Path(__file__).resolve().parents[3];binary=(root/a.binary).resolve();out=a.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    receipt=dict(binary_sha256=sha(binary),shader_sha256={f.name:sha(f) for f in sorted((binary.parent/'shaders').glob('*.spv'))},
        device=subprocess.check_output(['nvidia-smi','--query-gpu=name,driver_version','--format=csv,noheader'],text=True).strip(),
        settings=dict(width=1920,height=1080,supersample=1,shadows=1,frames=180,warmup_frames=30),runs={})
    cases=a.case or [(name,f'projects/sdf/presets/sculpture-guardian-{suffix}.json')
                     for name,suffix in (('before','refined'),('form','organic-form'),('after','organic'))]
    if len({name for name,_ in cases})!=len(cases) or any(Path(name).name!=name for name,_ in cases):
        p.error('Case names must be distinct basenames')
    for name,preset in cases:
        config=root/preset
        scene=root/json.loads(config.read_text())['authored-scene'];prefix=out/name
        cmd=[str(binary),'--headless','--require-validation','--config',str(config),'--headless-render-only',
            '--frames','180','--profile-warmup-frames','30','--profile-output',str(prefix),
            '--width','1920','--height','1080','--supersample','1','--shadows','1']
        result=subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/f'{name}.log').write_text(result.stdout)
        assert result.returncode==0 and 'validation error' not in result.stdout.lower(),result.stdout
        frames={}
        with Path(f'{prefix}.passes.csv').open() as stream:
            for row in csv.DictReader(stream):
                if row['kind']=='gpu':
                    index=int(row['frame_index']);frames[index]=frames.get(index,0)+float(row['duration_ms'])
        match=re.search(r'bricks=(\d+)/(\d+) allocated=(\d+) build_ms=([\d.]+)',result.stdout);assert match
        receipt['runs'][name]=dict(scene_sha256=sha(scene),command=cmd,gpu_frame=distribution(list(frames.values())),
            active_bricks=int(match[1]),capacity=int(match[2]),field_bytes=int(match[3]),cold_build_ms=float(match[4]))
        setup=re.search(r'study IBL setup_ms=([\d.]+)',result.stdout)
        if setup:receipt['runs'][name]['ibl_setup_ms']=float(setup[1])
        print(name,json.dumps(receipt['runs'][name]['gpu_frame']),flush=True)
    (out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(out/'manifest.json',flush=True)


if __name__=='__main__':main()
