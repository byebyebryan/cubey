# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy", "Pillow"]
# ///
"""Native forward transmission controls. GPU work is serialized; output is immutable."""
from __future__ import annotations
import argparse, hashlib, json, math, os, platform, subprocess
from pathlib import Path
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[3]
WIDTH,HEIGHT=1920,1080
RADIUS,DISTANCE=.25,1.05

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def unit(v): return v/np.linalg.norm(v,axis=-1,keepdims=True)
def refract(i,n,eta):
    c=np.sum(i*n,axis=-1,keepdims=True)
    k=1-eta*eta*(1-c*c)
    assert np.min(k)>-1e-8
    return unit(eta*i-(eta*c+np.sqrt(np.maximum(k,0)))*n)
def angle(a,b):
    # atan2 retains precision at the null control.
    a,b=unit(a),unit(b)
    return np.degrees(np.arctan2(np.linalg.norm(np.cross(a,b),axis=-1),np.sum(a*b,axis=-1)))
def stats(a): return dict(mean=float(np.mean(a)),p95=float(np.percentile(a,95)),p99=float(np.percentile(a,99)),maximum=float(np.max(a)))
def geometry():
    tan=math.tan(math.radians(45)/2)
    x=(2*(np.arange(WIDTH)+.5)/WIDTH-1)*tan*WIDTH/HEIGHT
    y=(1-2*(np.arange(HEIGHT)+.5)/HEIGHT)*tan
    xx,yy=np.meshgrid(x,y)
    incident=unit(np.stack([xx,yy,-np.ones_like(xx)],-1))
    camera=np.array([0,0,DISTANCE]);b=incident@camera
    disc=b*b-(DISTANCE*DISTANCE-RADIUS*RADIUS)
    hit=disc>0
    # Exact perspective silhouette radius in pixels, with a two-pixel band.
    rp=RADIUS/np.sqrt(DISTANCE*DISTANCE-RADIUS*RADIUS)*HEIGHT/(2*tan)
    xxp,yyp=np.meshgrid(np.arange(WIDTH)+.5-WIDTH/2,np.arange(HEIGHT)+.5-HEIGHT/2)
    interior=hit&(xxp*xxp+yyp*yyp<(rp-2)**2)
    point=camera+incident*(-b-np.sqrt(np.maximum(disc,0)))[...,None]
    return incident,point,hit,interior

def write_hdr(path,constant=False,width=1024,height=512):
    u=(np.arange(width)+.5)/width;v=(np.arange(height)+.5)/height
    phi=(u-.5)*2*np.pi;theta=v*np.pi
    x=np.sin(theta)[:,None]*np.sin(phi)[None,:]
    y=np.broadcast_to(np.cos(theta)[:,None],x.shape)
    z=np.sin(theta)[:,None]*np.cos(phi)[None,:]
    rgb=np.full((*x.shape,3),.5) if constant else .5+.45*np.stack([x,y,z],-1)
    _,e=np.frexp(np.max(rgb,axis=-1));scale=np.ldexp(np.ones_like(e,dtype=float),8-e)
    data=np.concatenate([np.minimum(255,np.floor(rgb*scale[...,None])).astype(np.uint8),(e+128)[...,None].astype(np.uint8)],-1)
    path.write_bytes(f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode()+data.tobytes())

class Runs:
    def __init__(self,binary,output):
        self.binary=binary.resolve();self.output=output.resolve();self.output.mkdir(parents=True,exist_ok=False)
        self.rows=[]
        self.identity=dict(source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
            source_diff_sha256=hashlib.sha256(subprocess.check_output(['git','diff','HEAD'],cwd=ROOT)).hexdigest(),
            binary_sha256=sha(self.binary),shader_sha256={p.name:sha(p) for p in sorted((self.binary.parent/'shaders').glob('*.spv'))},
            recipes={p.name:sha(p) for p in sorted((ROOT/'assets/materials').glob('*.json'))},python=platform.python_version(),numpy=np.__version__)
    def run(self,name,extra=(),hdr=False,size=(WIDTH,HEIGHT)):
        png=self.output/(name+'.png');raw=self.output/(name+'.f32')
        cmd=[str(self.binary),'--headless','--frames','1','--width',str(size[0]),'--height',str(size[1]),'--require-validation','--output',str(png),*map(str,extra)]
        if hdr:cmd+=['--hdr-output',str(raw)]
        completed=subprocess.run(cmd,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env={**os.environ,'VK_LAYER_VALIDATE_SYNC':'1'})
        (self.output/(name+'.log')).write_text(completed.stdout)
        assert completed.returncode==0 and 'validation error' not in completed.stdout.lower(), (name,completed.stdout[-3000:])
        self.rows.append(dict(name=name,command=cmd,png_sha256=sha(png),hdr_sha256=sha(raw) if hdr else None))
        (self.output/'runs.json').write_text(json.dumps(dict(identity=self.identity,runs=self.rows),indent=2)+'\n')
        print(name,flush=True)
        return np.fromfile(raw,dtype='<f4').reshape(size[1],size[0],4).astype(float) if hdr else png

def f0(runs):
    inc,point,hit,interior=geometry()
    base=['--material','glass','--sphere-density','dense','--pitch','0','--distance',str(DISTANCE),'--no-pattern','--no-bump']
    recipe=json.loads((ROOT/'assets/materials/glass.json').read_text())
    recipe['surface']['optics']['attenuation_color']=[1,1,1]
    normals=runs.run('input-normal',base+['--lighting','clear','--debug-view','normal'],hdr=True)
    normal=unit(normals[interior,:3]*2-1)
    geometric=runs.run('input-geometric-normal',base+['--lighting','clear','--debug-view','geometric-normal'],hdr=True)
    nideal=unit(point[interior]);camera=np.array([0,0,DISTANCE]);i=inc[interior]
    cases=[];rays={};exits={}
    for ior in [1.,1.33,1.5,2.5]:
        recipe['surface']['optics']['ior']=ior
        path=runs.output/f'neutral-{ior}.json';path.write_text(json.dumps(recipe,indent=2)+'\n')
        extra=base+['--lighting','clear','--recipe-input',str(path)]
        actual=runs.run(f'ray-{ior}',extra+['--debug-view','transmission-ray'],hdr=True)
        exit_data=runs.run(f'exit-{ior}',extra+['--debug-view','transmission-exit'],hdr=True)
        assert np.all(actual[interior,3]>0), 'missing raster coverage outside silhouette band'
        # Independent Snell tangent form and second intersection of the local
        # tangent sphere, using the observed production shading normal.
        t=refract(i,normal,1/ior)
        length=-2*RADIUS*np.sum(normal*t,axis=-1)
        outward=unit(normal+t*length[:,None]/RADIUS)
        tangent=ior*(t-np.sum(t*outward,axis=-1,keepdims=True)*outward)
        direction=unit(tangent+outward*np.sqrt(np.maximum(0,1-np.sum(tangent*tangent,axis=-1,keepdims=True))))
        ideal_t=refract(i,nideal,1/ior)
        ideal_length=-2*RADIUS*np.sum(nideal*ideal_t,axis=-1)
        ideal_outward=unit(nideal+ideal_t*ideal_length[:,None]/RADIUS)
        ideal_direction=refract(ideal_t,-ideal_outward,ior)
        ray=actual[interior,:3];gpu_length=actual[interior,3]
        row=dict(ior=ior,observed_normal_direction_error_degrees=stats(angle(ray,direction)),
                 ideal_sphere_direction_error_degrees=stats(angle(ray,ideal_direction)),
                 observed_normal_path_error_fraction_diameter=stats(abs(gpu_length-length)/(2*RADIUS)),
                 ideal_path_error_fraction_diameter=stats(abs(gpu_length-ideal_length)/(2*RADIUS)),
                 exit_position_error_m=stats(np.linalg.norm(exit_data[interior,:3]-(point[interior]+t*length[:,None]),axis=-1)))
        assert row['observed_normal_direction_error_degrees']['p99']<.1,row
        assert row['observed_normal_path_error_fraction_diameter']['p99']<.005,row
        row['ideal_sphere_initial_gate_passed'] = (
            row['ideal_sphere_direction_error_degrees']['p99'] < .1 and
            row['ideal_path_error_fraction_diameter']['p99'] < .005)
        cases.append(row);rays[ior]=actual;exits[ior]=exit_data
    gradient=runs.output/'direction-gradient.hdr';write_hdr(gradient)
    constant=runs.output/'constant.hdr';write_hdr(constant,True)
    routing=[]
    for label,environment,ior in [('gradient',gradient,1.5),('gradient-null',gradient,1.),('constant',constant,1.5)]:
        values={}
        extra=base+['--lighting','studio','--environment',str(environment),'--recipe-input',str(runs.output/f'neutral-{ior}.json'),'--debug-view','transmission-radiance']
        for source in ['screen','environment']:
            values[source]=runs.run(f'{label}-{source}',extra+['--transmission-source',source],hdr=True)[interior,:3]
        target=np.full_like(values['screen'],.5) if label=='constant' else .5+.45*unit(rays[ior][interior,:3])
        row=dict(control=label,screen_error=stats(np.linalg.norm(values['screen']-target,axis=-1)),
                 environment_error=stats(np.linalg.norm(values['environment']-target,axis=-1)),
                 source_difference=stats(np.linalg.norm(values['screen']-values['environment'],axis=-1)))
        assert row['environment_error']['p99']<.012,row
        if label=='gradient': assert row['environment_error']['mean']<row['screen_error']['mean']/10,row
        else: assert row['source_difference']['p99']<.005,row
        routing.append(row)
    for source in ['screen','environment']:
        runs.run('atmosphere-'+source,base+['--lighting','clear','--transmission-source',source])
    result=dict(passed=True,stage='F0',identity=runs.identity,cases=cases,routing=routing,
        precision='Native production fragment shader with opt-in RGBA32F scene/readback; ordinary rendering retains RGBA16F',
        coverage=dict(ideal_hit_pixels=int(hit.sum()),interior_pixels=int(interior.sum()),excluded_fraction=float(1-interior.sum()/hit.sum()),silhouette_band_pixels=2),
        shading_normal_offset_degrees=stats(angle(normal,unit(geometric[interior,:3]*2-1))),
        geometric_normal_vs_ideal_degrees=stats(angle(unit(geometric[interior,:3]*2-1),nideal)),
        scope='Conditional geometry oracle uses observed production shading normals; ideal-sphere differences retained separately. No rough-BTDF/GI truth claim.')
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,default=ROOT/'build/dev/projects/pbr_materials/pbr_materials');p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();runs=Runs(a.binary,a.output);result=f0(runs)
    (runs.output/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
