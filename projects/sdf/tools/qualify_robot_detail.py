#!/usr/bin/env python3
"""Qualify the robot detail variant and capture matched baseline/detail views."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from qualify import distribution


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--video', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    out = args.output.resolve()
    if out.exists():
        raise RuntimeError('Use a fresh output directory')
    for directory in ('captures', 'logs', 'configs', 'profiles', 'scenes'):
        (out / directory).mkdir(parents=True)
    binary = root / 'build/dev/projects/sdf/sdf'
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = dict(source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        source_dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip()),
        binary_sha256=sha(binary),
        shader_sha256={p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))},
        source_scene_sha256={name:sha(root/f'projects/sdf/scenes/{file}.json')
            for name,file in (('baseline','survey-robot'),('detail','survey-robot-detail'))}, runs={}, views={})
    def save():
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    command=['python3',str(root/'projects/sdf/tools/qualify_history.py'),
        '--config',str(root/'projects/sdf/presets/survey-robot-detail-build.json'),
        '--output',str(out/'history')]
    if args.video:command.append('--video')
    for name in ('left-shoulder-slot','backpack-vent-cut-0','backpack-vent-cut-1','backpack-vent-cut-2'):
        command+=['--hidden-preview-node',name]
    manifest['history_command']=command
    save()
    subprocess.run(command,cwd=root,check=True)
    def run(name,variant,options=(),capture=True):
        preset=root/f'projects/sdf/presets/survey-robot{"-detail" if variant=="detail" else ""}.json'
        command=[str(binary),'--headless','--require-validation','--config',str(preset),
            '--width','960','--height','960',*map(str,options)]
        output=out/f'captures/{name}.png'
        if capture:command+=['--output',str(output)]
        subprocess.run([*command,'--write-config-template',str(out/f'configs/{name}.json')],
            cwd=root,check=True,stdout=subprocess.DEVNULL)
        result=subprocess.run(command,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/f'logs/{name}.log').write_text(result.stdout)
        if result.returncode or 'validation error' in result.stdout.lower():
            raise RuntimeError(name+': '+result.stdout)
        row=dict(command=command,stats=[line for line in result.stdout.splitlines() if line.startswith('sdf:')])
        if capture:row.update(capture=str(output.relative_to(out)),capture_sha256=sha(output))
        manifest['runs'][name]=row
        save()
        print(name,'passed',flush=True)
        return row
    views={
        'hero':('Full model',[]),
        'upper':('Display, joints and grippers',['--width',1200,'--height',960,'--distance',.68,'--target-y',-.03,'--pitch',.08,'--yaw',.20]),
        'face':('Head fasteners',['--distance',.87,'--target-y',.22]),
        'feet':('Toe bumper seams',['--distance',.68,'--target-y',-.27,'--pitch',.07,'--yaw',.35]),
        'rear':('Rear inspection',['--yaw',3.49,'--pitch',.17,'--sun-azimuth',-20]),
        'backpack':('Backpack vents',['--yaw',3.49,'--pitch',.09,'--sun-azimuth',-20,'--distance',.82,'--target-y',-.015]),
        'side':('Side silhouette',['--yaw',1.5707963,'--pitch',.10]),
    }
    for view,(label,options) in views.items():
        manifest['views'][view]=dict(label=label,options=options)
        for variant in ('baseline','detail'):
            run(f'{variant}-{view}',variant,options+(['--validate-field'] if view=='hero' else []))
    run('detail-reloaded','detail',['--authored-scene',out/'history/scenes/robot.json',
        '--export-authored',out/'scenes/robot-reloaded.json','--validate-field'])
    assert (out/'history/scenes/robot.json').read_bytes()==(out/'scenes/robot-reloaded.json').read_bytes()
    manifest['replay_scene_bytes_identical']=True
    def pixels(path):
        return subprocess.check_output(['ffmpeg','-v','error','-i',str(path),'-f','rawvideo','-pix_fmt','rgb24','-'])
    canonical=pixels(out/'captures/detail-hero.png')
    manifest['atlas_relocation_checks']={}
    for axis in (80,92):
        name=f'detail-atlas-{axis}'
        run(name,'detail',['--atlas-axis',axis,'--validate-field'])
        candidate=pixels(out/f'captures/{name}.png')
        assert canonical==candidate, f'Brick relocation changed the rendered image at axis {axis}'
        manifest['atlas_relocation_checks'][str(axis)]=dict(passed=True,max_abs=0)
        save()
    for variant in ('baseline','detail'):
        prefix=out/f'profiles/{variant}'
        row=run(f'profile-{variant}',variant,['--headless-render-only','--frames',180,
            '--profile-warmup-frames',30,'--profile-output',prefix,'--width',1920,'--height',1080,
            '--supersample',1,'--shadows',1],False)
        frames={}
        with Path(f'{prefix}.passes.csv').open() as stream:
            for item in csv.DictReader(stream):
                if item['kind']=='gpu':
                    index=int(item['frame_index'])
                    frames[index]=frames.get(index,0)+float(item['duration_ms'])
        row['gpu_frame']=distribution(list(frames.values()))
        save()
    shutil.copy2(root/'projects/sdf/tools/author_robot.py',out/'scenes/author_robot.py')
    shutil.copy2(root/'projects/sdf/tools/sdf_author.py',out/'scenes/sdf_author.py')
    print(out/'manifest.json',flush=True)


if __name__=='__main__':main()
