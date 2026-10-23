#!/usr/bin/env python3
"""Capture matched sphere/sculpt materials and record bounded glass admission."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
from qualify_authored import check_video


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();root=Path(__file__).resolve().parents[3];out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=False);binary=root/'build/release/projects/sdf/sdf'
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    read=lambda p:json.loads(p.read_text())
    receipt=dict(binary_sha256=sha(binary),shader_sha256={p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))},
        source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        source_dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip()),
        device=subprocess.check_output(['nvidia-smi','--query-gpu=name,driver_version','--format=csv,noheader'],text=True).strip(),
        materials=['plain','limestone','marble','clay','ceramic','bronze','glass'],cases={},glass_diagnostics={})
    def save():(out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')
    for subject in ['sphere','guardian']:
        for kind in receipt['materials']:
            key=f'{subject}-{kind}';directory=out/key
            for f in ['captures','configs','source','logs']:(directory/f).mkdir(parents=True)
            cfg=root/f'projects/sdf/presets/material-study/{key}.json';scene=root/read(cfg)['authored-scene']
            shutil.copy2(cfg,directory/'source/preset.json');shutil.copy2(scene,directory/'source/scene.json')
            row=dict(scene_sha256=sha(scene),config_sha256=sha(cfg),runs={});receipt['cases'][key]=row
            for view in ['hero','detail']:
                extra=[] if view=='hero' else ['--distance',.72 if subject=='sphere' else .85,'--target-y',0 if subject=='sphere' else .09]
                cmd=[str(binary),'--headless','--require-validation','--config',str(cfg),'--width','960','--height','960',*map(str,extra),'--output',str(directory/f'captures/{view}.png')]
                if view=='hero' and kind in ['plain','glass']:cmd.append('--validate-field')
                r=subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                (directory/f'logs/{view}.log').write_text(r.stdout);assert r.returncode==0 and 'validation error' not in r.stdout.lower(),r.stdout
                subprocess.run([*cmd,'--write-config-template',str(directory/f'configs/{view}.json')],cwd=root,check=True,stdout=subprocess.DEVNULL)
                row['runs'][view]=dict(command=cmd,capture=f'{key}/captures/{view}.png',sha256=sha(directory/f'captures/{view}.png'),stats=[s for s in r.stdout.splitlines() if s.startswith('sdf:')])
                if kind=='plain':row['geometry']=re.search(r'authored geometry=(\w+)',r.stdout)[1]
                save();print(key,view,'passed',flush=True)
    for subject in ['sphere','guardian']:
        options=[('hero',[]),('detail',['--distance',.72 if subject=='sphere' else .85,'--target-y',0 if subject=='sphere' else .09])]
        if subject=='guardian':options += [(f'orbit-{i}',['--yaw',.4+i*6.28318530718/8]) for i in range(8)]
        for view,extra in options:
            image=out/f'{subject}-glass/captures/{view}-diagnostics.png'
            cmd=[str(binary),'--headless','--require-validation','--config',str(root/f'projects/sdf/presets/material-study/{subject}-glass.json'),'--width','960','--height','960','--debug','9',*map(str,extra),'--output',str(image)]
            r=subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);assert r.returncode==0,r.stdout
            (out/f'{subject}-glass/logs/{view}-diagnostics.log').write_text(r.stdout)
            rgb=subprocess.check_output(['ffmpeg','-v','error','-i',str(image),'-f','rawvideo','-pix_fmt','rgb24','-'])
            pixels=list(zip(rgb[::3],rgb[1::3],rgb[2::3]));mask=[p for p in pixels if 45<p[2]<180]
            cap=sum(r>180 for r,g,b in mask);tir=sum(g>180 for r,g,b in mask);errors=sum(r>180 and g<90 and b>180 for r,g,b in pixels)
            receipt['glass_diagnostics'][f'{subject}-{view}']=dict(command=cmd,capture=str(image.relative_to(out)),sha256=sha(image),pixels=len(mask),cap=cap,tir=tir,cap_fraction=cap/max(1,len(mask)),exhausted=errors)
            assert errors==0,(subject,view,errors);save()
    worst=max(r['cap_fraction'] for k,r in receipt['glass_diagnostics'].items() if k.startswith('guardian-'))
    receipt['glass_verdict']=dict(sphere='qualified',guardian='qualified' if worst<=.01 else 'experimental',worst_guardian_cap_fraction=worst,guardian_cap_limit=.01)
    for kind in receipt['materials']:
        subject='sphere' if kind=='glass' and worst>.01 else 'guardian';key=f'{subject}-{kind}';directory=out/key;video=directory/'orbit.mp4'
        cmd=[str(binary),'--headless','--require-validation','--config',str(root/f'projects/sdf/presets/material-study/{key}.json'),'--motion','orbit','--duration','8','--construction-seconds','4','--capture','video','--fps','30','--frames','240','--width','1280','--height','960','--output',str(video)]
        r=subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(directory/'logs/orbit.log').write_text(r.stdout)
        assert r.returncode==0 and r.stdout.count('sdf: authored instructions=')==1,r.stdout
        check_video(video,expected_frames=240)
        receipt['cases'][key]['orbit']=dict(command=cmd,capture=str(video.relative_to(out)),sha256=sha(video),frames=240,field_builds=1)
        save();print(key,'orbit passed',flush=True)
    for kind in receipt['materials']:
        a=read(out/f'sphere-{kind}/source/scene.json');b=read(out/f'guardian-{kind}/source/scene.json')
        assert a['materials']==b['materials']
    receipt['passed']=True;save()


if __name__=='__main__':main()
