#!/usr/bin/env python3
"""Matched first-pass/refined native views and baseline costs, with receipts."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from qualify import distribution


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();root=Path(__file__).resolve().parents[3]
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    binary=root/'build/dev/projects/sdf/sdf'
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    metadata=json.loads((root/'projects/sdf/sculpture_refinements.json').read_text())
    manifest=dict(source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        binary_sha256=sha(binary),shader_sha256={p.name:sha(p) for p in (binary.parent/'shaders').glob('*.spv')},
        subjects={})
    for subject,meta in metadata.items():
        rows={};manifest['subjects'][subject]=rows
        for version in ('baseline','refined'):
            cfg=root/(f'projects/sdf/presets/sculpture-{subject}.json' if version=='baseline' else meta['config'])
            scene=root/json.loads(cfg.read_text())['authored-scene']
            folder=out/subject/version;folder.mkdir(parents=True)
            shutil.copy2(cfg,folder/'preset.json');shutil.copy2(scene,folder/'source.json')
            row=dict(config_sha256=sha(cfg),scene_sha256=sha(scene),runs={});rows[version]=row
            for view in meta['views']:
                for clay in (False,True):
                    name=('clay-' if clay else '')+view['name']
                    command=[str(binary),'--headless','--require-validation','--config',str(cfg),
                        '--width','960','--height','960',*map(str,view['options']),
                        *(['--clay'] if clay else []),'--output',str(folder/f'{name}.png')]
                    result=subprocess.run(command,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                    (folder/f'{name}.log').write_text(result.stdout)
                    if result.returncode:raise RuntimeError(result.stdout)
                    row['runs'][name]=dict(command=command,image_sha256=sha(folder/f'{name}.png'),
                        stats=[l for l in result.stdout.splitlines() if l.startswith('sdf:')])
                    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
            if version=='baseline':
                prefix=folder/'profile'
                command=[str(binary),'--headless','--headless-render-only','--require-validation',
                    '--config',str(cfg),'--width','1920','--height','1080','--supersample','1','--shadows','1',
                    '--frames','180','--profile-warmup-frames','30','--profile-output',str(prefix)]
                result=subprocess.run(command,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                (folder/'profile.log').write_text(result.stdout)
                if result.returncode:raise RuntimeError(result.stdout)
                frames={}
                with Path(f'{prefix}.passes.csv').open() as stream:
                    for r in csv.DictReader(stream):
                        if r['kind']=='gpu':
                            f=int(r['frame_index']);frames[f]=frames.get(f,0)+float(r['duration_ms'])
                row['gpu_frame']=distribution(list(frames.values()));row['profile_command']=command
            print(subject,version,'matched captures passed',flush=True)
            (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    # The accepted mechanical baseline must keep its visible image after the tie fix.
    robot=out/'robot.png'
    subprocess.run([str(binary),'--headless','--require-validation','--config',
        'projects/sdf/presets/survey-robot-fieldkit.json','--width','960','--height','960','--output',str(robot)],
        check=True,cwd=root,stdout=subprocess.DEVNULL)
    old=root/'outputs/sdf/sculpture-foundation/robot-fieldkit.png'
    pixels=lambda p:subprocess.check_output(['ffmpeg','-v','error','-i',str(p),'-f','rawvideo','-pix_fmt','rgb24','-'])
    assert pixels(robot)==pixels(old)
    manifest['robot_pixels_identical']=True
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')


if __name__=='__main__':main()
