#!/usr/bin/env python3
"""Compare bounded glass interface budgets on fixed inputs, cameras and light."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def diagnostic(image):
    rgb=subprocess.check_output(['ffmpeg','-v','error','-i',str(image),'-f','rawvideo','-pix_fmt','rgb24','-'])
    pixels=cap=tir=errors=0
    for r,g,b in zip(rgb[::3],rgb[1::3],rgb[2::3]):
        if 45<b<180:
            pixels+=1;cap+=r>180;tir+=g>180
        errors+=r>180 and g<90 and b>180
    return dict(pixels=pixels,cap=cap,tir=tir,cap_fraction=cap/max(1,pixels),errors=errors)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--budgets',nargs='+',type=int,choices=[4,8,16],default=[4,8,16])
    a=p.parse_args();root=Path(__file__).resolve().parents[3];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    binary=root/'build/release/projects/sdf/sdf';read=lambda p:json.loads(p.read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    receipt=dict(binary_sha256=sha(binary),shader_sha256={p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))},runs={},budgets={})
    def run(name,config,extra=(),debug=False):
        cfg=out/(name+'.json');cfg.write_text(json.dumps(config,indent=2)+'\n');image=out/(name+'.png')
        cmd=[str(binary),'--headless','--require-validation','--config',str(cfg),'--width','960','--height','960','--output',str(image),*map(str,extra)]
        r=subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/(name+'.log')).write_text(r.stdout)
        assert r.returncode==0 and 'validation error' not in r.stdout.lower(),r.stdout
        row=dict(command=cmd,capture=image.name,sha256=sha(image))
        if debug:row.update(diagnostic(image));assert row['errors']==0,row
        receipt['runs'][name]=row;return image,row
    for budget in sorted(set(a.budgets)):
        for subject in ['sphere','guardian']:
            cfg=read(root/f'projects/sdf/presets/material-study/{subject}-glass.json');cfg['glass-crossings']=budget
            for view,extra in [('hero',[]),('detail',['--distance',.72 if subject=='sphere' else .85,'--target-y',0 if subject=='sphere' else .09])]:
                image,_=run(f'{subject}-{budget}-{view}',cfg,extra+(['--validate-field'] if view=='hero' else []))
                if subject=='sphere' and budget>4 and 4 in a.budgets:assert image.read_bytes()==(out/f'sphere-4-{view}.png').read_bytes()
            if subject=='guardian':
                for i in range(24):
                    run(f'guardian-{budget}-angle-{i}',cfg,['--yaw',.4+i*6.28318530718/24,'--debug',9],True)
        for fixture in ['separated','cavity']:
            cfg=read(root/'projects/sdf/presets/material-study/sphere-glass.json');cfg['glass-crossings']=budget
            cfg['authored-scene']=str(root/f'outputs/sdf/material-contrast-foundation/glass-{fixture}-scene.json')
            run(f'{fixture}-{budget}',cfg,['--validate-field'])
        rows=[v for k,v in receipt['runs'].items() if k.startswith(f'guardian-{budget}-angle-')]
        receipt['budgets'][str(budget)]=dict(worst_cap_fraction=max(r['cap_fraction'] for r in rows),errors=sum(r['errors'] for r in rows),views=len(rows))
        (out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n');print('budget',budget,receipt['budgets'][str(budget)],flush=True)
    for lo,hi in zip(sorted(set(a.budgets)),sorted(set(a.budgets))[1:]):
        for i in range(24):
            assert receipt['runs'][f'guardian-{hi}-angle-{i}']['cap_fraction']<=receipt['runs'][f'guardian-{lo}-angle-{i}']['cap_fraction']
    receipt['passed']=True
    receipt['candidate']=next((n for n in sorted(set(a.budgets)) if receipt['budgets'][str(n)]['worst_cap_fraction']<=.01),max(a.budgets))
    (out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')


if __name__=='__main__':main()
