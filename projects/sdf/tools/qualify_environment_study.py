#!/usr/bin/env python3
"""Qualify frozen environment reuse, cloud foreground masking and compatibility."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[3];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    binary=root/'build/release/projects/sdf/sdf';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();read=lambda p:json.loads(p.read_text())
    receipt=dict(binary_sha256=sha(binary),shader_sha256={p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))},runs={},checks={})
    def save():(out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')
    def run(name,preset,extra=(),size=480):
        image=out/(name+'.png');cmd=[str(binary),'--headless','--require-validation','--config',str(preset),'--width',str(size),'--height',str(size),'--supersample','1' if size==480 else '2','--output',str(image),*map(str,extra)]
        r=subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/(name+'.log')).write_text(r.stdout)
        assert r.returncode==0 and 'validation error' not in r.stdout.lower(),r.stdout
        receipt['runs'][name]=dict(command=cmd,capture=image.name,sha256=sha(image))
        if 'material-atmosphere' in str(preset) or preset.name.endswith(('-sunset.json','-clouds.json')):
            assert 'environment finish sky_generation=1' in r.stdout,r.stdout
            if preset.name.endswith('-clouds.json'):assert r.stdout.count('cloud probe capture generation=')==1,r.stdout
        save();print(name,'passed',flush=True);return image
    raw=lambda p:subprocess.check_output(['ffmpeg','-v','error','-i',str(p),'-f','rawvideo','-pix_fmt','rgb24','-'])
    for environment in ['reference','sunset','clouds']:
        for subject in ['sphere','guardian']:
            for material in ['clay','glass']:
                preset=root/f'projects/sdf/presets/environment-study/{subject}-{material}-{environment}.json'
                run(f'{subject}-{material}-{environment}',preset,['--validate-field'])
    for material in ['bronze','glass']:
        preset=root/f'projects/sdf/presets/environment-study/guardian-{material}-clouds.json'
        on=run(material+'-composite',preset)
        off=run(material+'-no-composite',preset,['--no-cloud-backdrop'])
        again=run(material+'-repeat',preset,['--frames',3])
        assert on.read_bytes()==again.read_bytes(),('frozen cloud frame changed',material)
        depth=run(material+'-mask',preset,['--debug',2])
        rgb=raw(depth);mask=[v>80 for v in rgb[::3]];left,right=raw(on),raw(off);errors=[]
        for y in range(2,478):
            for x in range(2,478):
                i=y*480+x
                if all(mask[i+dy*480+dx] for dy,dx in [(0,0),(-2,0),(2,0),(0,-2),(0,2)]):
                    errors.extend(abs(left[i*3+c]-right[i*3+c]) for c in range(3))
        assert errors and max(errors)<=1,(material,max(errors))
        receipt['checks'][material+'-foreground']=dict(max_channel_error=max(errors),tested_channels=len(errors),frozen_repeat_png_exact=True)
    baseline=read(root/'outputs/sdf/environment-baseline/manifest.json')
    assert all(sha(root/k)==v for k,v in baseline['catalog_sha256'].items())
    receipt['checks']['accepted_catalog_28_files_exact']=True
    legacy=read(root/'outputs/sdf/material-study-final-legacy/manifest.json')
    for name,row in legacy['runs'].items():
        config=Path(row['command'][row['command'].index('--config')+1])
        image=run('legacy-'+name,config,['--clay'] if name=='inspection' else [],960)
        assert image.read_bytes()==(root/f'outputs/sdf/material-study-final-legacy/{name}.png').read_bytes(),name
    for subject in ['sphere','guardian']:
        for material in ['clay','glass','bronze']:
            image=run('fixed-'+subject+'-'+material,root/f'projects/sdf/presets/material-study/{subject}-{material}.json',size=960)
            old=root/f'outputs/sdf/material-contrast-comparison/{subject}-{material}/captures/hero.png'
            assert image.read_bytes()==old.read_bytes(),(subject,material)
    preset=root/'projects/sdf/presets/environment-study/sphere-glass-clouds.json'
    for name,extra in [('budget',['--glass-crossings',7]),('extent',['--environment-extent',32]),('route',['--lighting','material-study']),('coverage',['--cloud-coverage',-1]),('time',['--cloud-time',-1])]:
        r=subprocess.run([str(binary),'--headless','--config',str(preset),*map(str,extra)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        assert r.returncode,(name,r.stdout)
    cfg=out/'config-roundtrip.json';subprocess.run([str(binary),'--config',str(preset),'--write-config-template',str(cfg)],check=True,stdout=subprocess.DEVNULL)
    c=read(cfg);assert c['glass-crossings']==8 and c['clouds'] and c['environment-extent']==128 and c['cloud-time']==300
    receipt['checks']['admission_and_config_roundtrip']=True;receipt['passed']=True;save()


if __name__=='__main__':main()
