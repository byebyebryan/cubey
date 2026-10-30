#!/usr/bin/env python3
"""Run only after timing: fresh native payloads and full-image handoff analysis."""
import argparse
import json
import math
from pathlib import Path
import sys
from types import SimpleNamespace

import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'projects/sdf/tools'))
from investigate_guardian import PRESETS, invoke, metadata, digest, camera_queries
from live_edit_loop import query
from quality_speed_analysis import compare


def save(path,data):path.write_text(json.dumps(data,indent=2)+'\n')


def validity(prefix):
    a=np.asarray(Image.open(prefix.with_suffix('.png')).convert('RGB'))
    b=np.asarray(Image.open(prefix.with_suffix('.validated.png')).convert('RGB'))
    different=np.any(a!=b,axis=-1)
    Image.fromarray(np.where(different,0,255).astype('uint8')).save(prefix.with_suffix('.valid.png'))
    ys,xs=np.nonzero(different)
    magenta=(a[:,:,0]>250)&(a[:,:,1]<5)&(a[:,:,2]>250)
    return dict(pixels=int(a.shape[0]*a.shape[1]),unscored_pixels=int(different.sum()),
                ordinary_exhaustion_pixels=int(magenta.sum()),
                flags=[dict(x=int(x),y=int(y),ordinary=a[y,x].tolist(),validation=b[y,x].tolist()) for y,x in zip(ys,xs)])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--raw',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();args.raw=args.raw.resolve();args.output=args.output.resolve();args.output.mkdir(exist_ok=False)
    binary=ROOT/'outputs/sdf/live-edit-loop-20261030/baseline-sdf'
    receipt=dict(metadata(binary),tool_sha256=digest(Path(__file__)),native_validation=[],runs=[],comparisons=[],max_tail_replay=[],source_diagnostic_relation=[],complete=False)
    for subject in PRESETS:
        for policy in ('cached','source'):
            prefix=args.raw/'qualify'/f'{subject}-{policy}-native'
            receipt['native_validation'].append(dict(subject=subject,mode='live-'+policy,**validity(prefix)))
        for mode in ('cached','balanced','analytic'):
            prefix=args.output/f'{subject}-fresh-{mode}'
            flags=[str(binary),'--config',str(ROOT/f'projects/sdf/presets/{PRESETS[subject]}.json'),
                '--authored-scene',str(args.raw/'captures'/f'{subject}-8-scene.json'),
                '--headless','--require-validation','--motion','static','--supersample','1',
                '--width','320','--height','180','--no-ao','--no-ground','--no-clouds','--no-occluder',
                '--frames','1','--render-mode',mode,'--output',str(prefix.with_suffix('.png'))]
            public_flags=None
            if mode=='analytic':
                # The existing payload guard admits explicit combined controls,
                # whereas named Analytical resolves automatic specialization.
                # Quantify their ordinary image differences and keep the source
                # diagnostic control separately labeled; preserve the public guard.
                public_flags=flags.copy();public_flags[public_flags.index('--output')+1]=str(prefix.with_suffix('.public.png'))
                invoke(public_flags,Path(str(prefix)+'-public'),timeout=300)
                at=flags.index('--render-mode')
                flags[at:at+2]=['--surface-trace','analytic','--csg-evaluation','pruned','--region-axis','64',
                    '--acceleration-variant','root-far','--source-optimization','combined']
            invoke(flags,prefix,timeout=300)
            if public_flags:
                a=np.asarray(Image.open(prefix.with_suffix('.public.png'))).astype('int16')
                b=np.asarray(Image.open(prefix.with_suffix('.png'))).astype('int16')
                changed=int(np.any(a!=b,axis=-1).sum())
                receipt['source_diagnostic_relation'].append(dict(subject=subject,byte_identical_png=digest(prefix.with_suffix('.public.png'))==digest(prefix.with_suffix('.png')),pixel_identical=changed==0,different_pixels=changed,max_channel_difference=int(np.abs(a-b).max()),public_command=public_flags,diagnostic_command=flags,
                    scope='Explicit combined source diagnostic control; no claimed public Analytical equivalence where pixels differ'))
            probe=flags.copy();probe[probe.index('--output')+1]=str(prefix.with_suffix('.validated.png'))
            probe+=['--quality-output',str(prefix.with_suffix('.bin'))]
            invoke(probe,Path(str(prefix)+'-probe'),timeout=300)
            validation=validity(prefix)
            receipt['native_validation'].append(dict(subject=subject,mode='fresh-'+('source-control' if mode=='analytic' else mode),**validation))
            receipt['runs'].append(dict(command=flags,probe_command=probe,image_sha256=digest(prefix.with_suffix('.png')),geometry_sha256=digest(prefix.with_suffix('.bin'))))
        for live in ('cached','source'):
            for fresh in ('cached','balanced','analytic'):
                a=args.raw/'qualify'/f'{subject}-{live}-native';b=args.output/f'{subject}-fresh-{fresh}'
                result=compare(a,b,args.output/f'{subject}-{live}-to-{fresh}')
                result.update(subject=subject,live=live,fresh='source-control' if fresh=='analytic' else fresh)
                receipt['comparisons'].append(result)
        count=64 if subject=='robot' else 13
        data=json.loads((args.raw/'prepare'/f'{subject}-{count}.json').read_text())
        cfg=json.loads((ROOT/f'projects/sdf/presets/{PRESETS[subject]}.json').read_text())
        data['cases'][0]['queries']=camera_queries(cfg,160,90)
        private=SimpleNamespace(binary=ROOT/'build/release/projects/sdf/cubey_project_sdf_live_edit_study',output=args.output)
        for policy in ('cached','source'):
            run,result=query(private,data,f'{subject}-max-tail',policy)
            rows=result['cases'][0]['results'];hits=[r for r in rows if r['status']==1]
            exhausted=sum(r['status']==2 for r in rows)
            residual=max(abs(r['cpu_live_residual']) for r in hits)
            if exhausted or residual>1/(256*7*32)+2e-6:raise RuntimeError('maximum tail traversal/residual failed')
            e=math.radians(cfg['sun-elevation']);a=math.radians(cfg['sun-azimuth'])
            light=[math.cos(e)*math.sin(a),math.sin(e),-math.cos(e)*math.cos(a)]
            shadows=[dict(kind='ray',origin=[ray['origin'][i]+ray['direction'][i]*r['t']+r['normal'][i]*2/(256*7) for i in range(3)],direction=light)
                     for ray,r in zip(data['cases'][0]['queries'],rows) if r['status']==1]
            shadow_data=json.loads(json.dumps(data));shadow_data['cases'][0]['queries']=shadows
            shadow_run,shadow_result=query(private,shadow_data,f'{subject}-max-shadow-{policy}',policy)
            shadow_exhausted=sum(r['status']==2 for r in shadow_result['cases'][0]['results'])
            if shadow_exhausted:raise RuntimeError('maximum tail shadow traversal exhausted')
            receipt['max_tail_replay'].append(dict(subject=subject,count=count,policy=policy,
                rays=len(rows),hits=len(hits),exhausted=exhausted,cpu_hit_residual_max=residual,run=run,
                shadow_rays=len(shadows),shadow_exhausted=shadow_exhausted,shadow_run=shadow_run))
        save(args.output/'postqualification.json',receipt)
        print(subject,'full-image handoff compared',flush=True)
    receipt['complete']=True;save(args.output/'postqualification.json',receipt)


if __name__=='__main__':main()
