#!/usr/bin/env python3
"""Matched native inspection of the legacy, organic form and detailed guardian."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,default=Path('build/release/projects/sdf/sdf'))
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--metadata',type=Path,default=Path('projects/sdf/guardian_organic.json'))
    p.add_argument('--version',nargs=2,action='append',metavar=('NAME','PRESET'),
                   help='Override comparison versions; repeat for matched views')
    a=p.parse_args();root=Path(__file__).resolve().parents[3];out=a.output.resolve()
    out.mkdir(exist_ok=False,parents=True)
    binary=(root/a.binary).resolve();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    metadata=root/a.metadata;meta=json.loads(metadata.read_text())['guardian']
    if a.version and (len({name for name,_ in a.version})!=len(a.version) or
                      any(Path(name).name!=name for name,_ in a.version)):
        p.error('Version names must be distinct basenames')
    configs=({name:root/path for name,path in a.version} if a.version else
             dict(before=root/meta['blockout_config'],form=root/'projects/sdf/presets/sculpture-guardian-organic-form.json',after=root/meta['config']))
    receipt=dict(binary_sha256=sha(binary),shader_sha256={f.name:sha(f) for f in sorted((binary.parent/'shaders').glob('*.spv'))},
        device=subprocess.check_output(['nvidia-smi','--query-gpu=name,driver_version','--format=csv,noheader'],text=True).strip(),
        source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        source_dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip()),
        metadata_sha256=sha(metadata),versions={},views=meta['views']+[dict(name='clay',label='Clay / hero',options=['--clay'])])
    for name,config in configs.items():
        directory=out/name
        for folder in ('captures','configs','logs','source'):(directory/folder).mkdir(parents=True)
        document=json.loads(config.read_text());scene_path=root/document['authored-scene'];scene=json.loads(scene_path.read_text())
        row=dict(scene_sha256=sha(scene_path),config_sha256=sha(config),edits=len(scene['history']),runs={})
        receipt['versions'][name]=row
        shutil.copy2(scene_path,directory/'source/scene.json');shutil.copy2(config,directory/'source/preset.json')
        for view in receipt['views']:
            stem=view['name'];image=directory/f'captures/{stem}.png'
            cmd=[str(binary),'--headless','--require-validation','--config',str(config),'--width','960','--height','960',
                 *map(str,view['options']),'--output',str(image)]
            if name=='after' and stem=='hero':cmd.append('--validate-field')
            subprocess.run([*cmd,'--write-config-template',str(directory/f'configs/{stem}.json')],cwd=root,check=True,stdout=subprocess.DEVNULL)
            run=subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (directory/f'logs/{stem}.log').write_text(run.stdout)
            assert run.returncode==0 and 'validation error' not in run.stdout.lower(),run.stdout
            row['runs'][stem]=dict(command=cmd,capture=f'{name}/captures/{stem}.png',sha256=sha(image),stats=[s for s in run.stdout.splitlines() if s.startswith('sdf:')])
            (out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')
            print(name,stem,'passed',flush=True)
        assert sha(scene_path)==row['scene_sha256'] and sha(config)==row['config_sha256']
    for view in receipt['views']:
        configs_for_view=[]
        for name in configs:
            config=json.loads((out/name/f'configs/{view["name"]}.json').read_text())
            for key in ('authored-scene','output','validate-field'):config.pop(key)
            configs_for_view.append(config)
        assert all(config==configs_for_view[0] for config in configs_for_view)
    subprocess.run(['python3',str(root/'projects/sdf/tools/check_sculptures.py'),'--subject','guardian',
        '--metadata',str(metadata),'--output',str(out/'intent-audit.json')],cwd=root,check=True)
    shutil.copy2(metadata,out/'metadata.json')
    assert sha(out/'metadata.json')==receipt['metadata_sha256']
    sources=out/'authoring';sources.mkdir()
    for name in meta['authoring_sources']:shutil.copy2(root/'projects/sdf/tools'/name,sources/name)
    receipt['authoring_sha256']={f.name:sha(f) for f in sorted(sources.glob('*.py'))}
    receipt['paired_settings_match']=True
    (out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(out/'manifest.json',flush=True)


if __name__=='__main__':main()
