# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Serial optics convergence, native captures and fixed-view GPU timing packet."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import numpy as np
from profile_first_pass import gpu_state, quantile, sha

VIEWS=['reflection','curved-mirror','refraction']


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,default=Path('build/dev/projects/raytracing/raytracing'))
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--mode',choices=['all','quality','timing'],default='all')
    args=p.parse_args();args.binary=args.binary.resolve();args.output=args.output.resolve()
    args.output.mkdir(parents=True,exist_ok=False)
    patch=subprocess.check_output(['git','diff','HEAD','--binary'])
    (args.output/'source.patch').write_bytes(patch)
    identity=dict(source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
                  patch_sha256=hashlib.sha256(patch).hexdigest(),binary_sha256=sha(args.binary),
                  shaders={p.name:sha(p) for p in sorted((args.binary.parent/'shaders').glob('*.spv'))},
                  model_sha256=sha(Path('cache/raytracing/stanford-dragon/dragon.glb')),gpu=gpu_state(),
                  presets={v:sha(Path(f'projects/raytracing/presets/optics/{v}.json')) for v in VIEWS},
                  host_scope='shared host; boundary observations do not prove continuous exclusivity')
    (args.output/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
    def run(name,view,extra,validated=False):
        command=[str(args.binary),'--config',f'projects/raytracing/presets/optics/{view}.json',
                 '--headless','--metrics',str(args.output/(name+'.json')),
                 '--require-validation' if validated else '--no-validation',*extra]
        before,start=gpu_state(),time.monotonic()
        result=subprocess.run(command,capture_output=True,text=True,timeout=180,
                              env=dict(os.environ,VK_LAYER_VALIDATE_SYNC='1') if validated else os.environ)
        wall=time.monotonic()-start;after=gpu_state();log=result.stdout+result.stderr
        (args.output/(name+'.log')).write_text(log)
        assert result.returncode==0 and not any(t in log.lower() for t in ['vuid-','hazard detected','vulkan validation error']),log
        return dict(name=name,view=view,command=command,wall_seconds=wall,before=before,after=after)
    if args.mode in ['all','timing']:
        timing=[]
        for repeat in range(3):
            for view in VIEWS:
                for source in ['environment','scene']:
                    name=f'{view}-{source}-{repeat}'
                    receipt=run(name,view,['--headless-render-only','--frames','180','--reflection-source',source,'--transmission-source',source])
                    data=json.loads((args.output/(name+'.json')).read_text())
                    assert data['dropped_gpu_samples']==0
                    receipt.update(repeat=repeat,source=source,gpu={})
                    for label in ['raytracing.trace','raytracing.whole_scene','raytracing.environment_update','raytracing.total_frame']:
                        rows=[r for r in data['gpu_samples'] if r['pass']==label and r['frame']>=30]
                        assert len(rows)==150 and len({r['frame'] for r in rows})==150
                        values=[r['milliseconds'] for r in rows]
                        receipt['gpu'][label]=dict(p50_ms=quantile(values,.5),p95_ms=quantile(values,.95),samples=150)
                    timing.append(receipt)
                    (args.output/'timing.json').write_text(json.dumps(timing,indent=2)+'\n')
                    print(name,receipt['gpu']['raytracing.total_frame'],flush=True)
        refresh=[]
        for repeat in range(3):
            name=f'refresh-{repeat}'
            receipt=run(name,'reflection',['--headless-render-only','--frames','180','--sun-speed','.2'])
            data=json.loads((args.output/(name+'.json')).read_text());receipt['sky_generations']=data['environment_generation']
            receipt['gpu']={}
            for label in ['raytracing.environment_update','raytracing.total_frame']:
                values=[r['milliseconds'] for r in data['gpu_samples'] if r['pass']==label and r['frame']>=30]
                assert len(values)==150
                receipt['gpu'][label]=dict(p50_ms=quantile(values,.5),p95_ms=quantile(values,.95))
            refresh.append(receipt)
        (args.output/'refresh.json').write_text(json.dumps(refresh,indent=2)+'\n')
    if args.mode in ['all','quality']:
        quality=[];captures=[]
        for view in VIEWS:
            controls={}
            for mode in ['default','control']:
                name=view+'-'+mode
                extras=['--frames','1','--hdr-output',str(args.output/(name+'.bin')),
                        '--output',str(args.output/(name+'.png'))]
                if mode=='control':extras+=['--max-events','12','--max-path-rays','64','--max-total-rays','256','--throughput-cutoff','0']
                controls[mode]=run(name,view,extras,True)
            image=np.fromfile(args.output/(view+'-default.bin'),dtype='<f4').reshape(-1,4).astype(float)
            control=np.fromfile(args.output/(view+'-control.bin'),dtype='<f4').reshape(-1,4).astype(float)
            assert np.isfinite(image).all() and np.isfinite(control).all()
            normalizer=np.maximum(1,control[:,:3]@np.array([.2126,.7152,.0722]))
            difference=np.abs(image[:,:3]-control[:,:3])/normalizer[:,None]
            row=dict(view=view,pixels=len(image),edge_exclusions=0,normalized_mean_absolute_error=float(difference.mean()),
                     normalized_pixel_error_p99=float(np.quantile(difference.max(axis=1),.99)),
                     maximum_normalized_error=float(difference.max()),
                     default_status_counts={str(int(s)):int(np.sum(image[:,3]==s)) for s in np.unique(image[:,3])},
                     control_status_counts={str(int(s)):int(np.sum(control[:,3]==s)) for s in np.unique(control[:,3])},commands=controls)
            row['passed']=row['normalized_mean_absolute_error']<=.001 and row['normalized_pixel_error_p99']<=.01
            quality.append(row)
            (args.output/'convergence.json').write_text(json.dumps(quality,indent=2)+'\n')
            print('convergence',view,row['passed'],row['normalized_mean_absolute_error'],row['normalized_pixel_error_p99'],flush=True)
            for mode in ['environment','scene','secondary-id','transport-limit','path-class']:
                name=view+'-'+mode
                extras=['--frames','1','--output',str(args.output/(name+'.png'))]
                if mode=='environment':extras+=['--reflection-source','environment','--transmission-source','environment']
                elif mode!='scene':extras+=['--view',mode]
                receipt=run(name,view,extras,True);receipt['sha256']=sha(args.output/(name+'.png'))
                captures.append(receipt)
        (args.output/'captures.json').write_text(json.dumps(captures,indent=2)+'\n')
        assert all(row['passed'] for row in quality),'Optical convergence screen failed; inspect retained results'


if __name__=='__main__':main()
