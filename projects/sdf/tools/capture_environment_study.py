#!/usr/bin/env python3
"""Capture fixed/environment pairs with frozen weather and explicit artifact receipts."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from qualify_authored import check_video


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--audition',action='store_true',help='Only guardian metal/glass and sphere clay, no films')
    a=p.parse_args();root=Path(__file__).resolve().parents[3];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    binary=root/'build/release/projects/sdf/sdf';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();read=lambda p:json.loads(p.read_text())
    receipt=dict(binary_sha256=sha(binary),shader_sha256={p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))},source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),cases={},backgrounds={},glass_budget=8)
    def save():(out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')
    def run(name,preset,extra=()):
        image=out/(name+'.png');cmd=[str(binary),'--headless','--require-validation','--config',str(preset),'--width','960','--height','960','--output',str(image),*map(str,extra)]
        r=subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/(name+'.log')).write_text(r.stdout)
        assert r.returncode==0 and 'validation error' not in r.stdout.lower(),r.stdout
        if read(preset).get('lighting')=='material-atmosphere':
            assert 'sdf: environment ready' in r.stdout and 'environment finish sky_generation=1' in r.stdout,r.stdout
            if read(preset).get('clouds'):assert r.stdout.count('cloud probe capture generation=')==1,r.stdout
        return dict(command=cmd,capture=image.name,sha256=sha(image))
    for environment in ['reference','sunset','clouds']:
        for subject in ['sphere','guardian']:
            for material in ['bronze','glass','marble','clay']:
                if a.audition and (subject,material) not in [('guardian','bronze'),('guardian','glass'),('sphere','clay')]:continue
                key=f'{subject}-{material}-{environment}';preset=root/f'projects/sdf/presets/environment-study/{key}.json'
                local=out/(key+'.json');shutil.copy2(preset,local);scene=root/read(preset)['authored-scene'];shutil.copy2(scene,out/(key+'-scene.json'))
                row=dict(preset=local.name,scene=key+'-scene.json',scene_sha256=sha(scene),views={})
                for view in ['hero'] if a.audition else ['hero','detail']:
                    extra=[] if view=='hero' else ['--distance',.72 if subject=='sphere' else .85,'--target-y',0 if subject=='sphere' else .09]
                    if view=='hero' and material in ['glass','clay']:extra+=['--validate-field']
                    row['views'][view]=run(key+'-'+view,preset,extra)
                if not a.audition and subject=='guardian' and material in ['bronze','glass']:
                    video=out/(key+'.mp4');cmd=[str(binary),'--headless','--require-validation','--config',str(preset),'--motion','orbit','--duration','8','--frames','240','--capture','video','--fps','30','--width','1280','--height','960','--output',str(video)]
                    r=subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/(key+'-orbit.log')).write_text(r.stdout)
                    assert r.returncode==0 and r.stdout.count('sdf: authored instructions=')==1,r.stdout
                    if environment!='reference':assert 'environment finish sky_generation=1' in r.stdout
                    if environment=='clouds':assert r.stdout.count('cloud probe capture generation=')==1
                    check_video(video,expected_frames=240)
                    row['orbit']=dict(command=cmd,capture=video.name,sha256=sha(video),frames=240)
                receipt['cases'][key]=row;save();print(key,'passed',flush=True)
        if not a.audition:
            preset=root/f'projects/sdf/presets/environment-study/sphere-plain-{environment}.json'
            receipt['backgrounds'][environment]=run('background-'+environment,preset,['--motion','construction','--time',0])
            receipt['backgrounds'][environment]['calibration']=run('calibration-'+environment,preset,['--exposure',.6])
    receipt['passed']=True;save()


if __name__=='__main__':main()
