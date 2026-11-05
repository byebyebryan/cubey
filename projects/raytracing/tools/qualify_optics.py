# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Qualify native RT optical math and geometry with fixed independent tolerances."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import numpy as np
from optics_reference import Scene, camera, interface, normalize, offset

DIRECTION_LIMIT = 1e-5
POSITION_LIMIT = 1e-4
FRESNEL_LIMIT = 1e-5
ABSORPTION_LIMIT = 1e-4


def native(binary, out, name, arguments, width=128, height=96):
    directory = out/name
    directory.mkdir()
    cmd = [str(binary),'--headless','--frames','1','--width',str(width),'--height',str(height),
           '--require-validation','--optics-output',str(directory/'rays.bin'),
           '--scene-output',str(directory/'scene.json'),'--output',str(directory/'frame.png'),
           '--metrics',str(directory/'metrics.json'), *arguments]
    run = subprocess.run(cmd,capture_output=True,text=True,timeout=120,env=dict(os.environ,VK_LAYER_VALIDATE_SYNC='1'))
    log = run.stdout+run.stderr
    (directory/'native.log').write_text(log)
    assert run.returncode == 0 and not any(t in log.lower() for t in ['vuid-','hazard detected','vulkan validation error']),log
    return np.fromfile(directory/'rays.bin',dtype='<f4').reshape(-1,16,4).astype(float), json.loads((directory/'scene.json').read_text()), cmd


def contract(binary,out,ior):
    actual,data,cmd = native(binary,out,'interface-'+str(ior),['--optics-contract','--ior',str(ior)],256,16)
    maximum = dict(direction=0.,fresnel=0.,absorption=0.)
    encoded_ior=data["config"]["ior"]
    tir_count = 0
    scored_inputs=0
    for row in actual:
        incident=normalize(row[4,:3]);inside=bool(row[4,3])
        reflected,transmitted,fresnel,tir=interface(incident,np.array([0.,0.,1.]),inside,encoded_ior)
        # Fixed critical-angle guard band; branch classifications outside it are scored.
        sin2=(encoded_ior if inside else 1/encoded_ior)**2*max(0,1-incident[2]**2)
        if abs(sin2-1)<1e-4:
            continue
        scored_inputs+=1
        assert bool(row[6,3])==tir, (ior,incident,row[6],tir)
        tir_count+=tir
        maximum['direction']=max(maximum['direction'],float(np.linalg.norm(reflected-row[5,:3])),float(np.linalg.norm(transmitted-row[6,:3])))
        maximum['fresnel']=max(maximum['fresnel'],abs(fresnel-row[5,3]))
        c=data['config'];reference=np.array([c['attenuation-red'],c['attenuation-green'],c['attenuation-blue']])**(row[9,3]/c['attenuation-distance'])
        maximum['absorption']=max(maximum['absorption'],float(np.max(np.abs(reference-row[9,:3])/reference)))
    assert maximum['direction']<=DIRECTION_LIMIT,maximum
    assert maximum['fresnel']<=FRESNEL_LIMIT,maximum
    assert maximum['absorption']<=ABSORPTION_LIMIT,maximum
    return dict(ior=ior,inputs=len(actual),scored_inputs=scored_inputs,guarded_inputs=len(actual)-scored_inputs,tir_inputs=tir_count,maximum=maximum,command=cmd)


def geometry(binary,out,name,scene_name,extra):
    actual,data,cmd=native(binary,out,name,['--scene',scene_name,'--target-y','.55','--distance','3',*extra])
    scene=Scene(data);c=data['config'];eye,directions=camera(c,128,96)
    rng=np.random.default_rng(20261105)
    selected=set(rng.choice(len(actual),128,replace=False).tolist())
    optical=np.flatnonzero(actual[:,1,3]>.5)
    selected.update(rng.choice(optical,min(len(optical),256),replace=False).tolist())
    counts=dict(primary=0,mirror=0,glass=0,secondary=0,shadow=0,transparent_shadow=0,through_glass_to_opaque=0,virtual_camera=0)
    errors=dict(direction=0.,position=0.,fresnel=0.,absorption=0.,shadow=0.)
    def score_hit(expected,observed):
        if expected is not None and expected['bary'].min()<1e-4:
            return
        assert int(observed[3])==(-1 if expected is None else expected['instance']),(expected,observed)
        if expected is not None:
            errors['position']=max(errors['position'],float(np.linalg.norm(observed[:3]-expected['point'])))
        counts['secondary']+=1
    light=np.array([np.cos(c['sun-elevation'])*np.sin(c['sun-azimuth']),np.sin(c['sun-elevation']),np.cos(c['sun-elevation'])*np.cos(c['sun-azimuth'])])
    for pixel in sorted(selected):
        row,direction=actual[pixel],directions[pixel]
        surface=scene.intersect(eye,direction)
        assert (surface is not None)==(row[0,0]>=0),(name,pixel,'mask')
        if surface is None or surface['bary'].min()<1e-4:
            continue
        assert surface['instance']==int(row[0,1])
        errors['position']=max(errors['position'],float(np.linalg.norm(surface['point']-(eye+direction*row[0,0]))))
        counts['primary']+=1
        kind=scene.materials[surface['material']]['kind']
        if kind==1:
            reflected=normalize(direction-2*np.dot(direction,surface['normal'])*surface['normal'])
            geometric=surface['outward'] if np.dot(direction,surface['outward'])<0 else -surface['outward']
            if np.dot(reflected,geometric)<=0:
                reflected=direction-2*np.dot(direction,geometric)*geometric
            errors['direction']=max(errors['direction'],float(np.linalg.norm(reflected-row[5,:3])))
            score_hit(scene.intersect(offset(surface,reflected),reflected),row[8])
            if surface['instance']==1:
                virtual_eye=eye.copy();virtual_eye[1]=-eye[1]+max(.00002,2e-6*np.max(np.abs(surface["point"])))
                virtual_direction=direction.copy();virtual_direction[1]=-direction[1]
                score_hit(scene.intersect(virtual_eye,virtual_direction,skip_instance=1),row[8])
                counts['virtual_camera']+=1
            counts['mirror']+=1
        elif kind==2:
            reflected,transmitted,fresnel,tir=interface(direction,surface['outward'],False,c['ior'])
            assert not tir
            errors['direction']=max(errors['direction'],float(np.linalg.norm(reflected-row[5,:3])),float(np.linalg.norm(transmitted-row[6,:3])))
            errors['fresnel']=max(errors['fresnel'],abs(fresnel-row[5,3]))
            first=scene.intersect(offset(surface,transmitted),transmitted)
            score_hit(first,row[7])
            if fresnel>c['throughput-cutoff']:
                score_hit(scene.intersect(offset(surface,reflected),reflected),row[8])
            if first is not None and first['instance']==surface['instance'] and first['bary'].min()>1e-4:
                absorption=np.array([c['attenuation-red'],c['attenuation-green'],c['attenuation-blue']])**(first['t']/c['attenuation-distance'])
                errors['absorption']=max(errors['absorption'],float(np.max(np.abs(absorption-row[9,:3])/absorption)))
                _,outgoing,_,internal_tir=interface(transmitted,first['outward'],True,c['ior'])
                if not internal_tir:
                    # Follow the direct transmitted trunk; reflected branches are separate.
                    after=scene.intersect(offset(first,outgoing),outgoing)
                    if after is None or scene.materials[after['material']]['kind']==0:
                        errors['direction']=max(errors['direction'],float(np.linalg.norm(outgoing-row[14,:3])))
                        assert int(row[14,3])==(-1 if after is None else after['instance']),(pixel,after,row[14])
                        counts['through_glass_to_opaque']+=after is not None
                        if after is not None:
                            geometric=after['outward'] if np.dot(outgoing,after['outward'])<0 else -after['outward']
                            visibility=scene.shadow(after,light,c) if np.dot(after['normal'],light)>0 and np.dot(geometric,light)>0 else np.ones(3)
                            errors['shadow']=max(errors['shadow'],float(np.max(np.abs(visibility-row[12,:3]))))
                            counts['transparent_shadow']+=bool(np.any((visibility>1e-4)&(visibility<.9999)))
            counts['glass']+=1
        else:
            normal=surface['normal'];geometric=surface['outward'] if np.dot(direction,surface['outward'])<0 else -surface['outward']
            if np.dot(normal,light)>0 and np.dot(geometric,light)>0:
                visibility=scene.shadow(surface,light,c)
                errors['shadow']=max(errors['shadow'],float(np.max(np.abs(visibility-row[12,:3]))))
                counts['shadow']+=1
                counts['transparent_shadow']+=bool(np.any((visibility>1e-4)&(visibility<.9999)))
    assert counts['mirror']+counts['glass']>30,counts
    assert errors['direction']<=DIRECTION_LIMIT,errors
    assert errors['position']<=POSITION_LIMIT,errors
    assert errors['fresnel']<=FRESNEL_LIMIT,errors
    assert errors['absorption']<=ABSORPTION_LIMIT,errors
    assert errors['shadow']<=ABSORPTION_LIMIT,errors
    counts['pixels_with_tir']=int(np.sum(actual[:,2,3]>0))
    if name=='offscreen':
        counts['offscreen_primary_pixels']=int(np.sum(actual[:,0,1]==5))
        counts['offscreen_reflected_pixels']=int(np.sum((actual[:,8,3]==5)&(actual[:,0,1]==2)))
        assert counts['offscreen_primary_pixels']==0 and counts['offscreen_reflected_pixels']>0,counts
    if name=='transparent-shadow':assert counts['transparent_shadow']>0,counts
    return dict(name=name,counts=counts,maximum=errors,command=cmd)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,default=Path('build/dev/projects/raytracing/raytracing'))
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();args.binary=args.binary.resolve();args.output=args.output.resolve()
    args.output.mkdir(parents=True,exist_ok=False)
    result=dict(format='cubey.raytracing.optics-qualification',tolerances=dict(direction=DIRECTION_LIMIT,position=POSITION_LIMIT,fresnel=FRESNEL_LIMIT,absorption=ABSORPTION_LIMIT),interfaces=[],geometry=[])
    for ior in [1.,1.33,1.5,2.5]:
        result['interfaces'].append(contract(args.binary,args.output,ior))
        print('interface',ior,'passed',flush=True)
    for name,scene,extra in [('mirror','reflection-fixture',[]),('mirror-side','reflection-fixture',['--yaw','-.45']),('offscreen','reflection-fixture',['--yaw','0','--pitch','.12','--target-x','.65','--target-y','.38','--target-z','.1','--distance','1.6']),('prism','refraction-fixture',[]),('slab','slab',['--target-y','.8','--yaw','.15','--pitch','0']),('transparent-shadow','slab',['--target-y','.8','--yaw','.15','--pitch','0','--sun-elevation','.1','--sun-azimuth','0']),('index-matched','slab',['--target-y','.8','--yaw','.15','--pitch','0','--ior','1','--attenuation-red','1','--attenuation-green','1','--attenuation-blue','1','--throughput-cutoff','0'])]:
        result['geometry'].append(geometry(args.binary,args.output,name,scene,extra))
        print(name,'passed',flush=True)
    furnace,data,command=native(args.binary,args.output,'lossless-environment',['--scene','slab','--target-y','.8','--yaw','0','--pitch','0','--distance','3','--fov','10','--reflection-source','environment','--transmission-source','environment','--attenuation-red','1','--attenuation-green','1','--attenuation-blue','1','--max-events','12','--max-path-rays','64','--max-total-rays','256','--throughput-cutoff','0'],64,48)
    energy_error=float(np.max(np.abs(furnace[:,10,:3]-[.035,.045,.06])))
    assert energy_error<=1e-5,energy_error
    result['lossless_environment']=dict(pixels=len(furnace),maximum_absolute_error=energy_error,command=command)
    result['passed']=True
    (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
