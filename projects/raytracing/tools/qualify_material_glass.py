# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Closed-body glass energy, budget, mesh-density and lifetime controls."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import numpy as np
from profile_first_pass import sha


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--mode',choices=['controls','density','lifecycle'],required=True)
    p.add_argument('--binary',type=Path,default=Path('build/dev/projects/raytracing/raytracing'))
    args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    rows=[]
    def run(name,material,extra=(),samples=256,raw=True,width=128,height=96):
        command=[str(args.binary.resolve()),'--scene','materials','--material',material,
                 '--headless','--width',str(width),'--height',str(height),'--samples',str(samples),
                 '--require-validation','--output',str(out/(name+'.png')),'--metrics',str(out/(name+'.json')),*extra]
        if raw:command+=['--optics-output' if args.mode=='controls' else '--hdr-output',str(out/(name+'.bin'))]
        result=subprocess.run(command,capture_output=True,text=True,timeout=600,
                              env=dict(os.environ,VK_LAYER_VALIDATE_SYNC='1'))
        log=result.stdout+result.stderr;(out/(name+'.log')).write_text(log)
        assert result.returncode==0 and not any(t in log.lower() for t in
            ['vuid-','hazard detected','vulkan validation error']),log
        data=json.loads((out/(name+'.json')).read_text())
        rows.append(dict(name=name,command=command,binary_sha256=sha(args.binary)))
        (out/'runs.json').write_text(json.dumps(rows,indent=2)+'\n')
        print(name,'captured',flush=True)
        if raw:
            shape=(height,width,16,4) if args.mode=='controls' else (height,width,4)
            values=np.fromfile(out/(name+'.bin'),dtype='<f4').reshape(shape).astype(float)
            assert np.isfinite(values).all()
            return data,values
        return data,None
    results=[]
    y,x=np.mgrid[:96,:128]
    roi=(x+.5-64)**2+(y+.5-48)**2<(28.407974-1)**2
    if args.mode=='controls':
        neutral={}
        for material in ['glass','frosted-glass']:
            recipe=json.loads(Path(f'assets/materials/{material}.json').read_text())
            recipe['surface']['optics']['attenuation_color']=[1,1,1]
            path=out/(material+'-neutral.json');path.write_text(json.dumps(recipe,indent=2)+'\n')
            m,q=run(material+'-furnace',material,['--recipe-input',str(path),'--workshop-lighting','furnace'],1024)
            image=q[:,:,10,:3]
            assert np.min(image)>=0 and np.max(image[roi])<=1.02
            assert np.all(q[:,:,15,2]==1024) and np.all(q[:,:,14,2]>=1024)
            assert np.all(q[:,:,15,0]<=32*1024) and np.all(q[:,:,10,3]<=128*1024)
            assert np.all(q[:,:,10,3]==q[:,:,15,0]+q[:,:,15,3])
            statuses=q[:,:,11,3].astype(int)
            assert not np.any(statuses&(16|64)), 'invalid medium or stack overflow'
            neutral[material]=image
            results.append(dict(name=material+'-furnace',mean=image[roi].mean(axis=0).tolist(),
                maximum=float(np.max(image[roi])),limit=1.02,
                mean_radiance_calls_per_primary=float(np.mean(q[:,:,15,0][roi])/1024),
                mean_discarded_throughput=(q[:,:,11,:3][roi].mean(axis=0)/1024).tolist(),
                terminated_branches=float(q[:,:,2,3][roi].sum()),
                primary_samples_with_status=float(q[:,:,14,3][roi].sum()),
                primary_samples=int(np.count_nonzero(roi))*1024))
        rough=json.loads((out/'frosted-glass-neutral.json').read_text())
        rough['surface']['roughness']=.05
        near_delta=out/'frosted-near-delta.json';near_delta.write_text(json.dumps(rough,indent=2)+'\n')
        _,q=run('near-delta','frosted-glass',['--recipe-input',str(near_delta),'--workshop-lighting','furnace'],1024)
        limit_error=float(np.mean(np.abs(q[:,:,10,:3][roi]-neutral['glass'][roi])))
        assert limit_error<=.02,limit_error
        results.append(dict(name='complete-body-near-delta',mean_error=limit_error,limit=.02))
        index=json.loads((out/'glass-neutral.json').read_text());index['surface']['optics']['ior']=1
        index_path=out/'index-matched-neutral.json';index_path.write_text(json.dumps(index,indent=2)+'\n')
        _,q=run('budget-one','glass',['--recipe-input',str(index_path),'--workshop-lighting','furnace','--max-total-rays','1'],1)
        assert np.max(np.abs(q[:,:,10,:3][roi]))==0,'exhaustion acquired fallback sky'
        assert np.all((q[:,:,11,3][roi].astype(int)&1)!=0)
        assert np.min(q[:,:,11,:3][roi])>0
        results.append(dict(name='budget-one',fallback_sky=False,all_hit_pixels_report_exhaustion=True))
        budget={}
        for name,flags in [('default',[]),('rays-control',['--max-path-rays','64','--max-total-rays','256']),
                           ('deep-control',['--max-events','12','--max-path-rays','64','--max-total-rays','256'])]:
            _,q=run(name,'frosted-glass',flags)
            budget[name]=q[:,:,10,:3]
            results.append(dict(name=name,mean_discarded_throughput=(q[:,:,11,:3][roi].mean(axis=0)/256).tolist(),
                terminated_branches=float(q[:,:,2,3][roi].sum()),
                primary_samples_with_status=float(q[:,:,14,3][roi].sum()),
                primary_samples=int(np.count_nonzero(roi))*256,
                mean_radiance_calls_per_primary=float(np.mean(q[:,:,15,0][roi])/256)))
        for name in ['rays-control','deep-control']:
            normalizer=np.maximum(1,budget[name]@np.array([.2126,.7152,.0722]))
            difference=np.abs(budget['default']-budget[name])/normalizer[:,:,None]
            results.append(dict(name='default-versus-'+name,mean=float(difference[roi].mean()),
                                p95=float(np.quantile(difference[roi].max(axis=-1),.95)),
                                scope='measured bounded-transport bias; not the same-budget SPP convergence screen'))
        components={}
        for component in ['combined','sky','sun']:
            _,q=run('component-'+component,'frosted-glass',['--workshop-lighting',component])
            components[component]=q[:,:,10,:3]
        error=float(np.max(np.abs(components['combined'][roi]-components['sky'][roi]-components['sun'][roi])))
        assert error<=5e-4,error
        results.append(dict(name='sun-sky-accounting',maximum_error=error,limit=5e-4))
    if args.mode=='density':
        for material in ['glass','frosted-glass']:
            captures={}
            for density in ['standard','dense']:
                m,q=run(material+'-'+density,material,['--sphere-density',density],1024,True,640,480)
                captures[density]=q
                results.append(dict(name=material+'-'+density,active_triangles=m['active_triangles'],
                                    geometry=m['geometry_admission'][0],as_bytes=m['as_bytes']))
            a,b=captures['standard'],captures['dense']
            yy,xx=np.mgrid[:480,:640];mask=(xx+.5-320)**2+(yy+.5-240)**2<(142.03987-1)**2
            difference=np.abs(a[:,:,:3]-b[:,:,:3])/np.maximum(1,b[:,:,:3]@np.array([.2126,.7152,.0722]))[:,:,None]
            results.append(dict(name=material+'-density-difference',mean=float(difference[mask].mean()),
                                p95=float(np.quantile(difference[mask].max(axis=-1),.95)),
                                scope='geometry approximation plus finite-sample noise, not a material equality claim'))
    if args.mode=='lifecycle':
        for material in ['glass','frosted-glass']:
            m,_=run('edits-'+material,material,['--headless-render-only','--frames','1000','--stress-edits','1000'],8192,False)
            journal=m['sampling_frames']
            assert m['history_epochs']==1000 and m['as_builds']==10 and m['target_generations']==1
            assert len({r['resident_requested_bytes'] for r in journal})==1
            assert all(r['new_samples']==1 for r in journal)
            results.append(dict(name=material+'-1000-edits',epochs=m['history_epochs'],as_builds=m['as_builds'],
                                resident_requested_bytes=journal[0]['resident_requested_bytes']))
        m,_=run('moving-sun','bronze',['--headless-render-only','--frames','90','--sun-speed','.2'],8192,False)
        assert m['environment_generation']>=4 and m['history_epochs']==90
        assert all(r['new_samples']==1 for r in m['sampling_frames'])
        results.append(dict(name='moving-sun',generations=m['environment_generation'],epochs=m['history_epochs']))
    (out/'receipt.json').write_text(json.dumps(dict(passed=True,mode=args.mode,results=results,
        source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),binary_sha256=sha(args.binary),
        shader_sha256=sha(args.binary.parent/'shaders/trace.rgen.spv'),scope=__doc__),indent=2)+'\n')


if __name__=='__main__':
    main()
