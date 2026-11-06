# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Serial per-material gallery convergence, bounded-path statistics and GPU cost."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import numpy as np
from profile_first_pass import sha, gpu_state, quantile

PRESET = 'projects/raytracing/presets/material-workshop/gallery-clear.json'


def masks(width,height):
    y,x=np.mgrid[:height,:width]
    tangent=np.tan(np.deg2rad(22.5))
    rays=np.stack([(2*(x+.5)/width-1)*width/height*tangent,
                   -(2*(y+.5)/height-1)*tangent,-np.ones_like(x)],axis=-1)
    rays/=np.linalg.norm(rays,axis=-1,keepdims=True)
    result=[]
    for i in range(12):
        center=np.array([((i%4)-1.5)*.252,(1-i//4)*.195,-.855])
        projection=rays@center
        silhouette=projection**2-np.dot(center,center)+.075**2>=0
        roi=silhouette.copy()
        # One grid-pixel interior guard, fixed before scoring. No background
        # pixels or other materials can dilute this specimen's error.
        roi[1:]&=silhouette[:-1];roi[:-1]&=silhouette[1:]
        roi[:,1:]&=silhouette[:,:-1];roi[:,:-1]&=silhouette[:,1:]
        roi[[0,-1],:]=False;roi[:,[0,-1]]=False
        result.append((silhouette,roi))
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--mode',choices=['quality','cost'],required=True)
    p.add_argument('--binary',type=Path,default=Path('build/dev/projects/raytracing/raytracing'))
    p.add_argument('--resume',action='store_true')
    a=p.parse_args();a.binary=a.binary.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=a.resume)
    identity=dict(source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        status=subprocess.check_output(['git','status','--porcelain'],text=True),binary_sha256=sha(a.binary),
        shaders={f.name:sha(f) for f in sorted((a.binary.parent/'shaders').glob('*.spv'))},
        inputs={str(f):sha(f) for f in [Path(PRESET),Path(PRESET).with_name('manifest.json'),*sorted(Path('assets/materials').glob('*.json'))]},gpu=gpu_state())
    if a.resume:
        prior=json.loads((out/'identity.json').read_text())
        assert all(prior[k]==identity[k] for k in ['source','binary_sha256','shaders','inputs'])
    else:(out/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
    runs=json.loads((out/'runs.json').read_text()) if a.resume and (out/'runs.json').exists() else []
    ids=[e['id'] for e in json.loads(Path('assets/materials/catalog.json').read_text())['entries']]

    def run(name,flags,validated=True,width=960,height=540):
        command=[str(a.binary),'--config',PRESET,'--headless','--width',str(width),'--height',str(height),
            '--require-validation' if validated else '--no-validation','--metrics',str(out/(name+'.json')),*flags]
        previous=next((r for r in runs if r['name']==name),None)
        if previous:
            assert previous['command']==command and all(sha(Path(f))==h for f,h in previous['artifacts'].items())
            return json.loads((out/(name+'.json')).read_text())
        start=time.monotonic();before=gpu_state()
        r=subprocess.run(command,capture_output=True,text=True,timeout=900,
                         env=dict(os.environ,VK_LAYER_VALIDATE_SYNC='1') if validated else os.environ)
        log=r.stdout+r.stderr;(out/(name+'.log')).write_text(log)
        assert r.returncode==0 and not any(t in log.lower() for t in ['vuid-','hazard detected','vulkan validation error']),log
        runs.append(dict(name=name,command=command,wall_seconds=time.monotonic()-start,before=before,after=gpu_state(),
            artifacts={command[i+1]:sha(Path(command[i+1])) for i,f in enumerate(command) if f in ['--metrics','--output','--hdr-output','--optics-output']}))
        (out/'runs.json').write_text(json.dumps(runs,indent=2)+'\n');print(name,'captured',flush=True)
        return json.loads((out/(name+'.json')).read_text())

    results=[]
    if a.mode=='quality':
        rois=masks(960,540)
        for route in ['environment','scene']:
            captures={}
            for spp,seed in [(256,7),(256,31),(4096,19),(8192,43)]:
                name=f'{route}-{spp}-{seed}'
                meta=run(name,['--frames','1','--samples',str(spp),'--seed',str(seed),
                    '--reflection-source',route,'--transmission-source',route,
                    '--hdr-output',str(out/(name+'.bin')),'--output',str(out/(name+'.png'))])
                assert meta['history_samples']==spp and meta['history_epochs']==1
                q=np.fromfile(out/(name+'.bin'),dtype='<f4').reshape(540,960,4).astype(float)
                assert np.isfinite(q).all() and np.min(q[:,:,:3])>=0
                captures[spp,seed]=q
            reference=captures[4096,19]
            normalizer=np.maximum(1,reference[:,:,:3]@np.array([.2126,.7152,.0722]))
            for spp,seed in [(256,7),(256,31),(8192,43)]:
                q=captures[spp,seed];difference=np.abs(q[:,:,:3]-reference[:,:,:3])/normalizer[:,:,None]
                for material,(silhouette,roi) in zip(ids,rois):
                    mean=float(difference[roi].mean());p95=float(np.quantile(difference[roi].max(axis=-1),.95))
                    statuses,counts=np.unique(q[:,:,3][silhouette],return_counts=True)
                    result=dict(material=material,route=route,samples=spp,seed=seed,mean=mean,p95=p95,passed=mean<=.005 and p95<=.02,
                        mean_limit=.005,p95_limit=.02,roi_pixels=int(roi.sum()),guarded_pixels=int((silhouette&~roi).sum()),
                        guard='one pixel four-neighbor erosion of analytic primary-hit silhouette',
                        unguarded_material_mean=float(difference[silhouette].mean()),maximum_error=float(difference[silhouette].max()),
                        all_pixel_mean=float(difference.mean()),all_pixel_p95=float(np.quantile(difference.max(axis=-1),.95)),
                        status_counts={str(int(s)):int(n) for s,n in zip(statuses,counts)})
                    results.append(result)
                    print(material,route,spp,seed,result['passed'],round(mean,6),round(p95,6),flush=True)
            (out/'quality.json').write_text(json.dumps(results,indent=2)+'\n')
            name=route+'-budget-statistics'
            run(name,['--frames','1','--samples','256','--reflection-source',route,'--transmission-source',route,
                '--optics-output',str(out/(name+'.bin')),'--output',str(out/(name+'.png'))],True,480,270)
            q=np.fromfile(out/(name+'.bin'),dtype='<f4').reshape(270,480,16,4).astype(float)
            assert np.isfinite(q).all() and np.all(q[:,:,15,2]==256)
            assert np.all(q[:,:,15,0]<=32*256) and np.all(q[:,:,10,3]<=128*256)
            assert np.all(q[:,:,10,3]==q[:,:,15,0]+q[:,:,15,3])
            assert not np.any(q[:,:,11,3].astype(int)&(16|64)), 'invalid medium or stack overflow'
            stats=[]
            for i,material in enumerate(ids):
                mask=q[:,:,0,1]==i+4
                assert mask.sum()>100
                stats.append(dict(material=material,pixels=int(mask.sum()),
                    mean_radiance_calls_per_primary=float(q[:,:,15,0][mask].mean()/256),
                    mean_visibility_calls_per_primary=float(q[:,:,15,3][mask].mean()/256),
                    mean_discarded_throughput=(q[:,:,11,:3][mask].mean(axis=0)/256).tolist(),
                    terminated_branches=int(q[:,:,2,3][mask].sum()),
                    rejected_continuations=int(q[:,:,3,3][mask].sum()),
                    primary_samples_with_status=int(q[:,:,14,3][mask].sum()),primary_samples=int(mask.sum())*256))
            (out/(route+'-budget-statistics-summary.json')).write_text(json.dumps(stats,indent=2)+'\n')
        assert all(r['passed'] for r in results),'Original per-material convergence limits failed'
    else:
        for route in ['environment','scene']:
            name=route+'-settled-256'
            d=run(name,['--frames','1','--samples','256','--reflection-source',route,'--transmission-source',route,
                '--output',str(out/(name+'.png'))],False,1920,1080)
            assert d['history_samples']==256 and d['dropped_gpu_samples']==0
            gpu={label:sum(r['milliseconds'] for r in d['gpu_samples'] if r['pass']==label)
                 for label in ['raytracing.trace','raytracing.environment_update','raytracing.whole_scene','raytracing.total_frame']}
            assert gpu['raytracing.total_frame']>0 and gpu['raytracing.total_frame']<=2000,gpu
            results.append(dict(name=name,gpu_ms=gpu,limit_ms=2000,passed=True,scope='256-SPP GPU project work; launch/setup/readback are separate',
                total_app_wall_ms=d['total_app_wall_ms'],scene_prepare_ms=d['cpu_scene_prepare_ms'],pipeline_ms=d['pipeline_setup_ms'],
                requested_bytes=d['resident_requested_bytes'] if 'resident_requested_bytes' in d else d['sampling_frames'][-1]['resident_requested_bytes']))
        for repetition in range(3):
            name=f'refresh-{repetition}'
            d=run(name,['--frames','180','--samples','8192','--sun-speed','.2','--headless-render-only'],False,1920,1080)
            assert d['environment_generation']>=4 and all(j['new_samples']==1 for j in d['sampling_frames'])
            values=[r['milliseconds'] for r in d['gpu_samples'] if r['pass']=='raytracing.total_frame' and r['frame']>=30]
            assert len(values)==150 and quantile(values,.95)<=16.7
            results.append(dict(name=name,p95_ms=quantile(values,.95),samples=150,generations=d['environment_generation'],passed=True))
        (out/'cost.json').write_text(json.dumps(results,indent=2)+'\n')
    (out/'receipt.json').write_text(json.dumps(dict(passed=True,mode=a.mode,identity=identity,results=results),indent=2)+'\n')


if __name__=='__main__':main()
