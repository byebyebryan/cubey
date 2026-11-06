# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Qualify two disjoint workshop bodies against independent plane/Snell/Beer controls."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import numpy as np
from profile_first_pass import sha


def normalize(v):
    return v/np.linalg.norm(v,axis=-1,keepdims=True)


def refract(incident,ratio):
    cosine=-incident[:,2]
    k=1-ratio*ratio*(1-cosine*cosine)
    assert np.min(k)>0
    return normalize(incident*ratio+np.array([0,0,1])*(ratio*cosine-np.sqrt(k))[:,None])


def plane(point,direction,z):
    return point+direction*((z-point[:,2])/direction[:,2])[:,None]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--binary',type=Path,default=Path('build/dev/projects/raytracing/raytracing'))
    args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    results=[]
    for case in ['index-matched','unequal-ior']:
        command=[str(args.binary.resolve()),'--scene','materials','--optical-pair',case,
                 '--headless','--frames','1','--width','192','--height','144','--samples','1',
                 '--workshop-lighting','furnace','--ior','2.2','--attenuation-red','.2',
                 '--attenuation-green','.3','--attenuation-blue','.4','--attenuation-distance','.01',
                 '--require-validation','--output',str(out/(case+'.png')),
                 '--optics-output',str(out/(case+'.bin')),'--metrics',str(out/(case+'.json'))]
        run=subprocess.run(command,capture_output=True,text=True,timeout=180,
                           env=dict(os.environ,VK_LAYER_VALIDATE_SYNC='1'))
        log=run.stdout+run.stderr;(out/(case+'.log')).write_text(log)
        assert run.returncode==0 and not any(t in log.lower() for t in
            ['vuid-','hazard detected','vulkan validation error']),log
        m=json.loads((out/(case+'.json')).read_text())
        raw=np.fromfile(out/(case+'.bin'),dtype='<f4').reshape(-1,16,4).astype(float)
        assert np.isfinite(raw).all()
        config=m['config'];eye=np.array([0,0,config['distance']])
        point=eye+raw[:,4,:3]*raw[:,0,0,None]
        regular=(raw[:,0,1]==16)&(np.max(np.abs(point[:,:2]),axis=-1)<.05)
        regular&=(raw[:,1,3]==4)&(raw[:,12,0]==16)&(raw[:,12,1]==17)&(raw[:,12,2]==2)
        regular&=(raw[:,7,3]==17)&(np.abs(raw[:,7,2]+.125)<1e-5)&(raw[:,11,3]==0)
        q=raw[regular];assert len(q)>=64,(case,len(q))
        incident=q[:,4,:3]
        ia,ib=(1,1) if case=='index-matched' else (1.33,1.8)
        a_in=plane(np.broadcast_to(eye,incident.shape),incident,.125)
        in_a=refract(incident,1/ia)
        a_out=plane(a_in,in_a,.075)
        air=refract(in_a,ia)
        b_in=plane(a_out,air,-.075)
        in_b=refract(air,1/ib)
        b_out=plane(b_in,in_b,-.125)
        da=np.linalg.norm(a_out-a_in,axis=-1);db=np.linalg.norm(b_out-b_in,axis=-1)
        reflected=incident*np.array([1,1,-1])
        cosine=-incident[:,2];transmitted_cosine=-in_a[:,2]
        rs=(cosine-ia*transmitted_cosine)/(cosine+ia*transmitted_cosine)
        rp=(ia*cosine-transmitted_cosine)/(ia*cosine+transmitted_cosine)
        fresnel=(rs*rs+rp*rp)/2
        position=float(np.max(np.linalg.norm(q[:,7,:3]-b_out,axis=-1)))
        direction=float(max(np.max(np.linalg.norm(q[:,5,:3]-reflected,axis=-1)),np.max(np.linalg.norm(q[:,6,:3]-in_a,axis=-1))))
        fresnel_error=float(np.max(np.abs(q[:,5,3]-fresnel)))
        distance_error=float(np.max(np.abs(q[:,9,3]-db)))
        expected_b=np.exp(np.log([.94,.7,.85])*db[:,None]/.2) if case=='index-matched' else np.ones((len(q),3))
        absorption=float(np.max(np.abs(q[:,9,:3]-expected_b)/expected_b))
        color_error=None
        if case=='index-matched':
            expected=expected_b*np.exp(np.log([.8,.9,.98])*da[:,None]/.3)
            color_error=float(np.max(np.abs(q[:,10,:3]-expected)/expected))
            assert color_error<=1e-4,color_error
        assert position<=1e-4 and distance_error<=1e-4 and direction<=1e-5 and fresnel_error<=1e-5 and absorption<=1e-4
        assert m['as_builds']==12 and m['history_samples']==1
        results.append(dict(case=case,command=command,regular_pixels=len(q),excluded_pixels=len(raw)-len(q),
                            position_error=position,direction_error=direction,fresnel_error=fresnel_error,
                            optical_distance_error=distance_error,relative_absorption_error=absorption,
                            relative_two_body_color_error=color_error,body_ids=[16,17],iors=[ia,ib]))
        print(case,'passed',len(q),'regular two-body paths',position,direction,absorption,flush=True)
    receipt=dict(passed=True,source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
                 binary_sha256=sha(args.binary),shader_sha256=sha(args.binary.parent/'shaders/trace.rgen.spv'),
                 scope=__doc__,cases=results)
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')


if __name__=='__main__':
    main()
