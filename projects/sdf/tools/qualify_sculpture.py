#!/usr/bin/env python3
"""Qualify one sculpture using explicit inspection metadata and native history."""
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
    parser.add_argument('--subject', required=True)
    parser.add_argument('--metadata', type=Path, default=Path('projects/sdf/sculptures.json'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--video', action='store_true')
    parser.add_argument('--reuse-history', type=Path,
                        help='Prior history directory; reuse requires identical scene/config/native artifacts')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    meta_path = args.metadata.resolve()
    meta = json.loads(meta_path.read_text())[args.subject]
    preset = root/meta['config']
    build = root/meta['build_config']
    scene_path = root/json.loads(preset.read_text())['authored-scene']
    assert scene_path == root/json.loads(build.read_text())['authored-scene']
    scene = json.loads(scene_path.read_text())
    binary = root/'build/dev/projects/sdf/sdf'
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    for name in ('captures', 'logs', 'configs', 'scenes', 'profiles'):
        (out/name).mkdir()
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    manifest = dict(subject=args.subject, title=scene['name'], metadata=meta,
        source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        source_dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip()),
        binary_sha256=sha(binary),shader_sha256={p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))},
        device=subprocess.check_output(['nvidia-smi','--query-gpu=name,driver_version','--format=csv,noheader'],text=True).strip(),
        source_scene_sha256=sha(scene_path), config_sha256=sha(preset),
        build_config_sha256=sha(build), metadata_sha256=sha(meta_path),
        edits=len(scene['history']),runs={})
    generators=meta.get('authoring_sources',('author_sculptures.py','sdf_author.py','sdf_probe.py'))
    assert generators and all(Path(name).name==name and name.endswith('.py') for name in generators)
    manifest['authoring_sha256']={name:sha(root/'projects/sdf/tools'/name) for name in generators}
    def save():
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    def run(name, options=(), config=preset, capture=True):
        command=[str(binary),'--headless','--require-validation','--config',str(config),
                 '--width','960','--height','960',*map(str,options)]
        image=out/f'captures/{name}.png'
        if capture:
            command += ['--output',str(image)]
        subprocess.run([*command,'--write-config-template',str(out/f'configs/{name}.json')],
                       cwd=root,check=True,stdout=subprocess.DEVNULL)
        result=subprocess.run(command,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/f'logs/{name}.log').write_text(result.stdout)
        if result.returncode or 'validation error' in result.stdout.lower():
            raise RuntimeError(name+': '+result.stdout)
        row=dict(command=command,exit_code=result.returncode,
                 stats=[s for s in result.stdout.splitlines() if s.startswith('sdf:')])
        if capture:
            row.update(capture=str(image.relative_to(out)),capture_sha256=sha(image))
        manifest['runs'][name]=row
        save()
        print(args.subject,name,'passed',flush=True)
        return row
    save()
    audit_command=['python3',str(root/'projects/sdf/tools/check_sculptures.py'),
                   '--subject',args.subject,'--metadata',str(meta_path),'--output',str(out/'intent-audit.json')]
    subprocess.run(audit_command,cwd=root,check=True)
    manifest['intent_audit_command']=audit_command
    assert json.loads((out/'intent-audit.json').read_text())['scene_sha256']==manifest['source_scene_sha256']
    save()
    history_command=['python3',str(root/'projects/sdf/tools/qualify_history.py'),
                     '--config',str(build),'--export-name','scene.json','--output',str(out/'history')]
    for preview in meta.get('hidden_previews',[]):
        assert preview['reason'].strip()
        history_command += ['--hidden-preview-node',preview['node']]
    if args.video:
        history_command.append('--video')
    if args.reuse_history:
        history_command += ['--reuse-captures',str(args.reuse_history.resolve())]
    manifest['history_command']=history_command
    save()
    subprocess.run(history_command,cwd=root,check=True)
    hist=json.loads((out/'history/manifest.json').read_text())
    assert hist['binary_sha256']==manifest['binary_sha256']
    assert hist['shader_sha256']==manifest['shader_sha256']
    assert hist['source_scene_sha256']==manifest['source_scene_sha256']
    for view in meta['views']:
        run(view['name'],view['options']+(['--validate-field'] if view['name']=='hero' else []))
    run('clay',['--clay'])
    run('blockout-clay',['--clay'],root/meta['blockout_config'])
    run('reloaded',['--authored-scene',out/'history/scenes/scene.json',
                    '--export-authored',out/'scenes/reloaded.json','--validate-field'])
    assert (out/'history/scenes/scene.json').read_bytes()==(out/'scenes/reloaded.json').read_bytes()
    def pixels(path):
        return subprocess.check_output(['ffmpeg','-v','error','-i',str(path),'-f','rawvideo','-pix_fmt','rgb24','-'])
    assert pixels(out/'captures/hero.png')==pixels(out/'captures/reloaded.png')
    manifest['reloaded_scene_and_image_identical']=True
    run('atlas-80',['--atlas-axis',80,'--validate-field'])
    assert pixels(out/'captures/hero.png')==pixels(out/'captures/atlas-80.png')
    manifest['atlas_80_image_identical']=True
    manifest['supplemental_previews']=[]
    for preview in meta.get('supplemental_previews',[]):
        index=next(i for i,h in enumerate(scene['history']) if h['node']==preview['node'])
        total=.65+sum(h['seconds'] for h in scene['history'])
        duration=json.loads(build.read_text())['construction-seconds']
        time=(.65+sum(h['seconds'] for h in scene['history'][:index])
              +.175*scene['history'][index]['seconds'])/total*duration
        options=['--time',time,*preview['options']]
        on=run(preview['name'],options,build)
        off=run(preview['name']+'-off',[*options,'--no-cutter-cue'],build)
        changed=sum(a!=b for a,b in zip(pixels(out/on['capture']),pixels(out/off['capture'])))
        assert changed>100,(preview['node'],changed)
        manifest['supplemental_previews'].append(dict(**preview,time=time,changed_channels=changed,
                                                     on=on['capture'],off=off['capture']))
        save()
    for name, config in [('subject',preset),('robot',root/'projects/sdf/presets/survey-robot-fieldkit.json')]:
        prefix=out/f'profiles/{name}'
        row=run('profile-'+name,['--headless-render-only','--frames',180,'--profile-warmup-frames',30,
                '--profile-output',prefix,'--width',1920,'--height',1080,'--supersample',1,'--shadows',1],config,False)
        samples={}
        with Path(f'{prefix}.passes.csv').open() as stream:
            for entry in csv.DictReader(stream):
                if entry['kind']=='gpu':
                    frame=int(entry['frame_index'])
                    samples[frame]=samples.get(frame,0)+float(entry['duration_ms'])
        row['gpu_frame']=distribution(list(samples.values()))
        save()
    shutil.copy2(scene_path,out/'scenes/source.json')
    shutil.copy2(meta_path,out/'scenes/sculptures.json')
    for filename in generators:
        shutil.copy2(root/'projects/sdf/tools'/filename,out/'scenes'/filename)
        assert sha(out/'scenes'/filename)==manifest['authoring_sha256'][filename]
    # Geometry evidence is immutable even if unrelated work continues later.
    assert sha(scene_path)==manifest['source_scene_sha256']
    assert sha(preset)==manifest['config_sha256'] and sha(build)==manifest['build_config_sha256']
    assert sha(meta_path)==manifest['metadata_sha256']
    save()
    print(out/'manifest.json',flush=True)


if __name__=='__main__':
    main()
