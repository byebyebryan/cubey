# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Independent double-precision rough dielectric arithmetic and angular integrals.

The integral traverses solid angle of microfacet normals using Gauss quadrature;
it does not call the GPU's visible-normal mapping or renormalize rejected events.
Single interior-interface radiance may exceed one by eta scaling; the complete
body's exterior white-furnace screen is a separate integration gate.
"""
import argparse
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import subprocess
import numpy as np


def normalize(a):
    return a/np.maximum(np.linalg.norm(a,axis=-1,keepdims=True),1e-150)


def fresnel(c,eta):
    c=np.clip(c,0,1)
    sine2=(1-c*c)/(eta*eta)
    t=np.sqrt(np.maximum(0,1-sine2))
    rs=(c-eta*t)/np.maximum(c+eta*t,1e-150)
    rp=(eta*c-t)/np.maximum(eta*c+t,1e-150)
    return np.where(eta==1,0,np.where(sine2>=1,1,.5*(rs*rs+rp*rp)))


def lambda_ggx(v,alpha):
    cosine=np.abs(v[...,2])
    tangent2=np.sum(v[...,:2]**2,axis=-1)/np.maximum(cosine*cosine,1e-150)
    return (np.sqrt(1+alpha*alpha*tangent2)-1)/2


def response(v,l,r,eta):
    # Audit the actual float operands, as in the opaque probe. Normalizing
    # again in double precision changes a sharp lobe's input direction.
    reflection=l[...,2]>0
    h=normalize(v+np.where(reflection,1,eta)[...,None]*l)
    h=np.where((h[...,2]<0)[...,None],-h,h)
    vh=np.sum(v*h,axis=-1)
    lh=np.sum(l*h,axis=-1)
    a=r*r
    d=a*a/(np.pi*np.maximum(np.sum(h[...,:2]**2,axis=-1)+a*a*h[...,2]**2,1e-150)**2)
    g1=1/(1+lambda_ggx(v,a))
    g=1/(1+lambda_ggx(v,a)+lambda_ggx(l,a))
    f=fresnel(vh,eta)
    normal_pdf=d*g1*vh/np.maximum(v[...,2],1e-150)
    denominator=np.maximum((lh+vh/eta)**2,1e-150)
    reflected=d*g*f/np.maximum(4*v[...,2]*l[...,2],1e-150)
    # Both cosines are known for transmission. The independent oracle uses
    # the two polarized reflection coefficients, rather than the GPU's
    # cancellation-free transmittance expression.
    ct=np.abs(lh)
    rs=(vh-eta*ct)/np.maximum(vh+eta*ct,1e-150)
    rp=(eta*vh-ct)/np.maximum(eta*vh+ct,1e-150)
    transmission=np.clip(1-.5*(rs*rs+rp*rp),0,1)
    transmitted=d*transmission*g*np.abs(lh*vh/(np.where(l[...,2]!=0,l[...,2],1e-150)*v[...,2]*denominator))/(eta*eta)
    value=np.where(reflection,reflected,transmitted)
    pdf=np.where(reflection,normal_pdf*f/np.maximum(4*vh,1e-150),normal_pdf*transmission*np.abs(lh)/denominator)
    valid=(v[...,2]>0)&(l[...,2]!=0)&(vh>0)&(lh*l[...,2]>0)&(h[...,2]>0)&(r>0)&(eta!=1)
    return np.where(valid,value,0),np.where(valid,pdf,0)


def component_pdf(v,l,r,eta):
    reflection=l[...,2]>0
    h=normalize(v+np.where(reflection,1,eta)[...,None]*l)
    h=np.where((h[...,2]<0)[...,None],-h,h)
    vh=np.sum(v*h,axis=-1);lh=np.sum(l*h,axis=-1);a=r*r
    d=a*a/(np.pi*np.maximum(np.sum(h[...,:2]**2,axis=-1)+a*a*h[...,2]**2,1e-150)**2)
    normal=d*vh/np.maximum(v[...,2]*(1+lambda_ggx(v,a)),1e-150)
    pdf=np.where(reflection,normal/np.maximum(4*vh,1e-150),normal*np.abs(lh)/np.maximum((lh+vh/eta)**2,1e-150))
    valid=(v[...,2]>0)&(l[...,2]!=0)&(vh>0)&(lh*l[...,2]>0)&(h[...,2]>0)&(r>0)&(eta!=1)
    return np.where(valid,pdf,0)


@lru_cache(maxsize=3)
def normal_quadrature(order):
    x,w=np.polynomial.legendre.leggauss(order)
    z,wz=(x+1)/2,w/2
    phi,wp=(x+1)*np.pi,w*np.pi
    h=np.stack(np.broadcast_arrays(np.sqrt(1-z[:,None]**2)*np.cos(phi),
                 np.sqrt(1-z[:,None]**2)*np.sin(phi),z[:,None]),-1)
    return h,wz[:,None]*wp[None,:]


def integral(v,r,eta,order=512):
    if r==0 or eta==1:
        f=float(fresnel(v[2],eta))
        reflected=np.array([-v[0],-v[1],v[2]])
        sun=3*f if np.dot(reflected,normalize(np.array([.3,.4,.85])))>=.95 else 0
        return dict(energy=f+(1-f)/(eta*eta),mass=1.0,normal_mass=1.0,sun=sun)
    h,measure=normal_quadrature(order)
    vh=np.sum(v*h,axis=-1)
    a=r*r
    d=a*a/(np.pi*(np.sum(h[...,:2]**2,axis=-1)+a*a*h[...,2]**2)**2)
    g1=1/(1+lambda_ggx(v,a))
    p=d*g1*np.maximum(vh,0)/v[2]
    f=fresnel(vh,eta)
    reflected=normalize(2*vh[...,None]*h-v)
    sine2=(1-vh*vh)/(eta*eta)
    transmitted=normalize(-v/eta+(vh/eta-np.sqrt(np.maximum(0,1-sine2)))[...,None]*h)
    reflection_valid=(vh>0)&(reflected[...,2]>0)
    transmission_valid=(vh>0)&(sine2<1)&(transmitted[...,2]<0)
    r_mass=np.where(reflection_valid,f,0)
    t_mass=np.where(transmission_valid,1-f,0)
    gr=1/(1+lambda_ggx(v,a)+lambda_ggx(reflected,a))
    gt=1/(1+lambda_ggx(v,a)+lambda_ggx(transmitted,a))
    sun_mask=np.sum(reflected*normalize(np.array([.3,.4,.85])),axis=-1)>=.95
    return dict(energy=float(np.sum(measure*p*(r_mass*gr/g1+t_mass*gt/g1/(eta*eta)))),
                mass=float(np.sum(measure*p*(r_mass+t_mass))),normal_mass=float(np.sum(measure*p)),
                sun=float(np.sum(measure*p*r_mass*gr/g1*sun_mask*3)))


def score(path,integrals=True):
    raw=np.fromfile(path,dtype='<f4').reshape(256,512,16,4).astype(float)
    assert np.isfinite(raw).all(),'nonfinite GPU dielectric observations'
    for row in range(256):
        patterns=raw[row,:,10,0].astype(int)
        for start in [0,256]:
            assert np.array_equal(np.sort(patterns[start:start+256]),np.arange(256)),('joint proposal coverage',row,start)
        indices=raw[row,:,10,1].astype(int)
        for bit in [0,1]:
            selected=((patterns>>(row%8))&1)==bit
            assert np.array_equal(np.sort(indices[selected]),np.arange(256)),('conditional proposal indices',row,bit)
    v,r=raw[...,0,:3],raw[...,0,3]
    h=raw[...,1,:3]
    l=raw[...,2,:3]
    xi=raw[...,4,:3]
    eta=raw[...,3,3]
    flags=raw[...,4,3].astype(int)
    delta=(flags&8)!=0
    accepted=(flags&2)!=0
    cosine=np.sum(v*h,axis=-1)
    f=fresnel(cosine,eta)
    branch=xi[...,2]>=f
    sine2=(1-cosine*cosine)/(eta*eta)
    expected=normalize(np.where(branch[...,None],-v/eta[...,None]+(
        cosine/eta-np.sqrt(np.maximum(0,1-sine2)))[...,None]*h,2*cosine[...,None]*h-v))
    valid=np.where(branch,expected[...,2]<0,expected[...,2]>0)&np.where(branch,sine2<1,True)
    # Explicit critical/grazing bands; report them and retain their estimator
    # mass. They are not retries or acceptance renormalization.
    regular=(np.abs(sine2-1)>1e-5)&(np.abs(expected[...,2])>1e-4)
    direction_error=float(np.max(np.linalg.norm(l[regular]-expected[regular],axis=-1)))
    scalar_fresnel=fresnel(raw[...,5,2],eta)
    fresnel_error=float(np.max(np.abs(raw[...,3,2]-scalar_fresnel)))
    classification=int(np.count_nonzero(regular&((branch!=((flags&1)!=0))|(valid!=accepted))))
    ev,pdf=response(v,l,r,eta)
    ev=np.where(delta,np.where(branch,1-f,f)*np.where(branch,1/(eta*eta),1)/np.maximum(np.abs(l[...,2]),1e-150),ev)
    pdf=np.where(delta,np.where(branch,1-f,f),pdf)
    ev=np.where(valid,ev,0);pdf=np.where(valid,pdf,0)
    actual=np.stack([raw[...,3,0],raw[...,2,3]],-1)
    expected_values=np.stack([ev,pdf],-1)
    ratio=np.abs(actual-expected_values)/(1e-6+1e-4*np.abs(expected_values))
    arithmetic_regular=regular&valid
    maximum_ratio=float(np.max(ratio[arithmetic_regular]))
    arbitrary=raw[...,8,:3]
    av,ap=response(v,arbitrary,r,eta)
    arbitrary_expected=np.stack([av,ap],-1)
    arbitrary_actual=np.stack([raw[...,8,3],raw[...,9,0]],-1)
    arbitrary_ratio=np.abs(arbitrary_actual-arbitrary_expected)/(1e-6+1e-4*np.abs(arbitrary_expected))
    arbitrary_h=normalize(v+np.where(arbitrary[...,2]>0,1,eta)[...,None]*arbitrary)
    arbitrary_cosine=np.abs(np.sum(v*arbitrary_h,axis=-1))
    arbitrary_regular=(np.abs(1-(1-arbitrary_cosine**2)/(eta*eta))>1e-5)&(np.abs(arbitrary[...,2])>1e-4)
    arbitrary_maximum=float(np.max(arbitrary_ratio[arbitrary_regular]))
    components=np.stack([component_pdf(v,l,r,eta),component_pdf(v,arbitrary,r,eta)],-1)
    component_actual=raw[...,11,:2]
    component_ratio=np.abs(component_actual-components)/(1e-6+1e-4*np.abs(components))
    component_maximum=float(max(np.max(component_ratio[...,0][arithmetic_regular]),np.max(component_ratio[...,1][arbitrary_regular])))
    worst=np.unravel_index(np.argmax(np.where(arithmetic_regular[...,None],ratio,0)),ratio.shape)
    result=dict(passed=direction_error<=1e-5 and fresnel_error<=1e-5 and classification==0 and maximum_ratio<=1 and arbitrary_maximum<=1 and component_maximum<=1,
                direction_error=direction_error,fresnel_error=fresnel_error,classification_mismatches=classification,
                arithmetic_tolerance_ratio=maximum_ratio,arbitrary_tolerance_ratio=arbitrary_maximum,
                component_pdf_tolerance_ratio=component_maximum,
                critical_or_grazing_observations=int(np.count_nonzero(~regular)),
                arbitrary_critical_or_grazing_observations=int(np.count_nonzero(~arbitrary_regular)),
                arithmetic_observations=256*512,integral_samples_per_row=512*64,
                joint_proposal_blocks=512,conditional_proposal_index_cases=512,
                all_direction_error=float(np.max(np.linalg.norm(l-expected,axis=-1))),
                direction_limit=1e-5,arithmetic_absolute_limit=1e-6,arithmetic_relative_limit=1e-4,
                energy_integral_limit=.02,mass_integral_limit=.025,
                worst=dict(index=[int(i) for i in worst],input=raw[worst[0],worst[1],:5].tolist(),
                           expected=expected_values[worst[:2]].tolist(),ratio=ratio[worst[:2]].tolist()),cases=[])
    if not result['passed'] or not integrals:
        return result
    for row in range(256):
        roughness=float(r[row,0]);relative_eta=float(eta[row,0]);view=v[row,0]
        # Very sharp NDF peaks use the already qualified direction/PDF arithmetic;
        # solid-angle integrals start at r=.2, without a poorly resolved oracle.
        if 0<roughness<.19 and relative_eta!=1:
            continue
        control=integral(view,roughness,relative_eta)
        observed=np.mean(raw[row,:,12],axis=0)
        estimates=np.mean(raw[row,:,14],axis=0)
        energy_error=abs(float(observed[0])-control['energy'])
        mass_error=abs(float(observed[1])-control['mass'])
        passed=energy_error<=.02 and mass_error<=.025 and abs(control['normal_mass']-1)<=.005
        nee_error=max(abs(float(estimates[0])-control['sun']),abs(float(estimates[1])-control['sun']))
        split_error=abs(float(estimates[2])-control['energy'])
        passed=passed and nee_error<=.02 and split_error<=.02
        if relative_eta>=1:
            passed=passed and float(observed[0])<=1.02 and control['energy']<=1.02
        case=dict(row=row,roughness=roughness,eta=relative_eta,cosine=float(view[2]),
                  observed_energy=float(observed[0]),observed_mass=float(observed[1]),
                  control=control,energy_error=energy_error,mass_error=mass_error,passed=bool(passed))
        case.update(sun_estimates=estimates[:2].tolist(),split_energy=float(estimates[2]),sun_error=nee_error,split_energy_error=split_error)
        if row in [163,164,167,227,228,231,235,236,239]:
            finer=integral(view,roughness,relative_eta,768)
            stability=max(abs(control[k]-finer[k]) for k in ['energy','mass','normal_mass','sun'])
            case.update(finer_control=finer,quadrature_stability=stability)
            passed=passed and stability<=.005
            case['passed']=bool(passed)
        result['cases'].append(case)
        result['passed']=result['passed'] and bool(passed)
    rough_limits=[]
    for row in range(1,256,8):
        view=v[row,0];relative_eta=float(eta[row,0])
        control=integral(view,0,relative_eta)
        observed=float(np.mean(raw[row,:,12,0]))
        error=abs(observed-control['energy'])
        passed=error<=.02
        rough_limits.append(dict(row=row,roughness=float(r[row,0]),eta=relative_eta,
                                observed=observed,delta_limit=control['energy'],error=error,passed=passed))
        result['passed']=result['passed'] and passed
    result['rough_limit_cases']=rough_limits
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,default=Path('build/dev/projects/raytracing/raytracing'))
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--input',type=Path)
    p.add_argument('--arithmetic-only',action='store_true')
    args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    command=None
    probe=args.input
    if probe is None:
        probe=out/'probe.bin'
        command=[str(args.binary.resolve()),'--headless','--frames','1','--width','512','--height','256',
                 '--require-validation','--dielectric-contract','--optics-output',str(probe),
                 '--output',str(out/'probe.png'),'--metrics',str(out/'metrics.json')]
        run=subprocess.run(command,capture_output=True,text=True,timeout=180,
                           env=dict(os.environ,VK_LAYER_VALIDATE_SYNC='1'))
        log=run.stdout+run.stderr;(out/'native.log').write_text(log)
        assert run.returncode==0 and not any(t in log.lower() for t in
            ['vuid-','hazard detected','vulkan validation error']),log
    with np.errstate(over='ignore',invalid='ignore',divide='ignore'):
        result=score(probe,not args.arithmetic_only)
    result.update(command=command,probe_sha256=hashlib.sha256(probe.read_bytes()).hexdigest(),
                  binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                  shader_sha256=hashlib.sha256((args.binary.parent/'shaders/trace.rgen.spv').read_bytes()).hexdigest(),
                  source_base=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),scope=__doc__)
    result['source_files_sha256']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in
        [Path('projects/raytracing/shaders/material_dielectric.glsl'),
         Path('projects/raytracing/shaders/material_bsdf.glsl'),
         Path('projects/raytracing/shaders/material_workshop.glsl'),
         Path('projects/raytracing/shaders/trace.rgen'),Path(__file__)]}
    (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ['cases','rough_limit_cases','command','scope']},indent=2))
    if result['cases']:
        print('Integral cases',len(result['cases']),'failed',sum(not c['passed'] for c in result['cases']))
    raise SystemExit(0 if result['passed'] else 1)


if __name__=='__main__':
    main()
