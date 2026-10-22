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
    parser.add_argument('--finish-existing',action='store_true',
                        help='Verify existing matched captures and complete the robot regression without recapturing profiles')
    args=parser.parse_args();root=Path(__file__).resolve().parents[3]
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=args.finish_existing)
    binary=root/'build/dev/projects/sdf/sdf'
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    metadata=json.loads((root/'projects/sdf/sculpture_refinements.json').read_text())
    manifest=dict(source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        binary_sha256=sha(binary),shader_sha256={p.name:sha(p) for p in (binary.parent/'shaders').glob('*.spv')},
        subjects={})
    if args.finish_existing:
        previous=json.loads((out/'manifest.json').read_text())
        for key in ('binary_sha256','shader_sha256'):
            assert previous[key]==manifest[key],key
        manifest=previous
        manifest['completion_source']=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
        for subject,versions in manifest['subjects'].items():
            for version,row in versions.items():
                folder=out/subject/version
                cfg=root/(f'projects/sdf/presets/sculpture-{subject}.json' if version=='baseline' else metadata[subject]['config'])
                scene=root/json.loads(cfg.read_text())['authored-scene']
                assert sha(cfg)==row['config_sha256'] and sha(scene)==row['scene_sha256']
                assert sha(folder/'preset.json')==row['config_sha256'] and sha(folder/'source.json')==row['scene_sha256']
                for name,run in row['runs'].items():
                    assert sha(folder/f'{name}.png')==run['image_sha256']
    for subject,meta in metadata.items():
        if args.finish_existing:continue
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
    # Preserve tiny cross-shader differences under the established image gate.
    robot=out/'robot.png'
    command=[str(binary),'--headless','--require-validation','--config',
        'projects/sdf/presets/survey-robot-fieldkit.json','--width','960','--height','960',
        '--validate-field','--output',str(robot)]
    result=subprocess.run(command,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/'robot.log').write_text(result.stdout)
    if result.returncode:raise RuntimeError(result.stdout)
    old=root/'outputs/sdf/sculpture-foundation/robot-fieldkit.png'
    pixels=lambda p:subprocess.check_output(['ffmpeg','-v','error','-i',str(p),'-f','rawvideo','-pix_fmt','rgb24','-'])
    a,b=pixels(robot),pixels(old);assert len(a)==len(b)
    delta=[abs(x-y) for x,y in zip(a,b)]
    metrics=dict(max_abs=max(delta),mean_abs=sum(delta)/len(delta),changed_channels=sum(d>0 for d in delta),
                 changed_pixels=len({i//3 for i,d in enumerate(delta) if d}),
                 fraction_above_1=sum(d>1 for d in delta)/len(delta),
                 limits=dict(max_abs=16,mean_abs=.002,fraction_above_1=.0001))
    assert all(metrics[k]<=v for k,v in metrics['limits'].items()),metrics
    manifest['robot_pixels_identical']=not metrics['changed_channels']
    manifest['robot_image_regression']=dict(passed=True,**metrics,command=command,
        stats=[s for s in result.stdout.splitlines() if s.startswith('sdf:')],
        baseline_rgb_sha256=hashlib.sha256(b).hexdigest(),candidate_rgb_sha256=hashlib.sha256(a).hexdigest())
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')


if __name__=='__main__':main()
