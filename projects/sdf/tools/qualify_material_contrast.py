#!/usr/bin/env python3
"""Check preserved material inputs/images and legacy atmosphere controls."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();root=Path(__file__).resolve().parents[3];base=root/'outputs/sdf'
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    read=lambda p:json.loads(p.read_text())
    before=base/'material-study-comparison';after=base/'material-contrast-comparison'
    b,c=[read(d/'manifest.json') for d in [before,after]]
    binary=root/'build/release/projects/sdf/sdf'
    assert c['passed'] and sha(binary)==c['binary_sha256']
    receipt=dict(binary_sha256=c['binary_sha256'],shader_sha256=c['shader_sha256'],
                 baseline_binary_sha256=b['binary_sha256'],pixel_bound=dict(max_channel_error=1,max_changed_fraction=.0001),runs={})
    def compare(name,left,right):
        rgb=lambda p:subprocess.check_output(['ffmpeg','-v','error','-i',str(p),'-f','rawvideo','-pix_fmt','rgb24','-'])
        l,r=rgb(left),rgb(right);assert len(l)==len(r)
        d=[abs(x-y) for x,y in zip(l,r)];changed=sum(any(d[i:i+3]) for i in range(0,len(d),3))
        row=dict(before_sha256=sha(left),sha256=sha(right),png_exact=left.read_bytes()==right.read_bytes(),max_channel_error=max(d),changed_pixels=changed,pixels=len(d)//3)
        assert row['max_channel_error']<=1 and changed/(len(d)//3)<=.0001,(name,row)
        receipt['runs'][name]=row
    for subject in ['sphere','guardian']:
        for kind in c['materials']:
            case=f'{subject}-{kind}'
            old,new=[read(d/case/'source/scene.json') for d in [before,after]]
            assert old['root']==new['root'] and old.get('history')==new.get('history'),case
            assert b['cases'][case]['config_sha256']==c['cases'][case]['config_sha256'],case
            if kind not in ['plain','ceramic','clay']:
                assert old==new and b['cases'][case]['scene_sha256']==c['cases'][case]['scene_sha256'],case
                for view in ['hero','detail']:
                    compare(case+'-'+view,before/b['cases'][case]['runs'][view]['capture'],after/c['cases'][case]['runs'][view]['capture'])
    legacy=base/'material-study-final-legacy';m=read(legacy/'manifest.json')
    for name,row in m['runs'].items():
        image=out/f'{name}.png';cmd=list(row['command']);cmd[0]=str(binary);cmd[cmd.index('--output')+1]=str(image)
        r=subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/f'{name}.log').write_text(r.stdout)
        assert r.returncode==0 and 'validation error' not in r.stdout.lower(),r.stdout
        compare('atmosphere-'+name,legacy/f'{name}.png',image)
        receipt['runs']['atmosphere-'+name]['command']=cmd
    receipt['passed']=True
    (out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(dict(passed=True,cases=len(receipt['runs']),png_exact=sum(r['png_exact'] for r in receipt['runs'].values()),max_channel_error=max(r['max_channel_error'] for r in receipt['runs'].values()),max_changed_pixels=max(r['changed_pixels'] for r in receipt['runs'].values()))))


if __name__=='__main__':main()
