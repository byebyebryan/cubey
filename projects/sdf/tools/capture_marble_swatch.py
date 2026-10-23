#!/usr/bin/env python3
"""Capture a flat native SDF marble slab before inspecting it on the guardian."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from sdf_author import box, write_scene


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--binary',type=Path,default=Path('build/release/projects/sdf/sdf'))
    args=parser.parse_args();root=Path(__file__).resolve().parents[3]
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    binary=(root/args.binary).resolve();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    source=root/'projects/sdf/scenes/sculpture-guardian-marble.json'
    material=json.loads(source.read_text())['materials'][0]
    base=json.loads((root/'projects/sdf/presets/sculpture-guardian-marble.json').read_text())
    views=[dict(name='unlit',label='Flat base color',options=['--debug',6]),
           dict(name='lit',label='Shaded slab',options=['--debug',0,'--yaw',.16,'--pitch',.12]),
           dict(name='close',label='Close base color',options=['--debug',6,'--distance',.72]),
           dict(name='distant',label='Distant base color',options=['--debug',6,'--distance',2.6])]
    receipt=dict(binary_sha256=sha(binary),shader_sha256={p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))},
        source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        source_dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip()),
        device=subprocess.check_output(['nvidia-smi','--query-gpu=name,driver_version','--format=csv,noheader'],text=True).strip(),
        source_material_sha256=sha(source),views=views,versions={})
    def save():(out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')
    for name,amount in [('before',0),('subtle',.08),('after',.16)]:
        directory=out/name
        for folder in ('captures','configs','logs','source'):(directory/folder).mkdir(parents=True)
        appearance=copy.deepcopy(material);appearance['procedural']['color_variation']=amount
        scene=directory/'source/scene.json'
        write_scene(scene,'Marble material slab',[appearance],
                    box('slab',(0,0,.105),(.3,.3,.025),.002,appearance['name']))
        config=copy.deepcopy(base)
        config.update({'authored-scene':str(scene),'yaw':0,'pitch':0,'distance':1.2,'target-y':0})
        preset=directory/'source/preset.json';preset.write_text(json.dumps(config,indent=2)+'\n')
        row=dict(amount=amount,scene_sha256=sha(scene),config_sha256=sha(preset),runs={})
        receipt['versions'][name]=row
        for view in views:
            stem=view['name'];image=directory/f'captures/{stem}.png'
            cmd=[str(binary),'--headless','--require-validation','--config',str(preset),
                 '--width','960','--height','960',*map(str,view['options']),'--output',str(image)]
            if name=='after' and stem=='unlit':cmd.append('--validate-field')
            subprocess.run([*cmd,'--write-config-template',str(directory/f'configs/{stem}.json')],
                           cwd=root,check=True,stdout=subprocess.DEVNULL)
            result=subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (directory/f'logs/{stem}.log').write_text(result.stdout)
            assert result.returncode==0 and 'validation error' not in result.stdout.lower(),result.stdout
            row['runs'][stem]=dict(command=cmd,capture=f'{name}/captures/{stem}.png',sha256=sha(image),
                stats=[s for s in result.stdout.splitlines() if s.startswith('sdf:')])
            save();print(name,stem,'passed',flush=True)
    for view in views:
        settings=[]
        for name in receipt['versions']:
            config=json.loads((out/name/f'configs/{view["name"]}.json').read_text())
            for key in ('authored-scene','output','validate-field'):config.pop(key)
            settings.append(config)
        assert all(config==settings[0] for config in settings)
    sources=out/'authoring';sources.mkdir()
    for name in ('capture_marble_swatch.py','sdf_author.py'):shutil.copy2(Path(__file__).parent/name,sources/name)
    receipt['authoring_sha256']={p.name:sha(p) for p in sorted(sources.iterdir())}
    receipt['paired_settings_match']=True;save();print(out/'manifest.json',flush=True)


if __name__=='__main__':main()
