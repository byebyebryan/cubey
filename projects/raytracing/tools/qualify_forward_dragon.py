# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.5", "pillow==13.0.0"]
# ///
"""Audit actual forward dragon material, entry geometry and volume ray inputs.

This qualifies the chosen forward approximation, not equivalence to RT optics.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import numpy as np


def unit(v):
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def angle(a, b):
    a, b = unit(a), unit(b)
    return np.degrees(np.arctan2(np.linalg.norm(np.cross(a,b), axis=-1), np.sum(a*b, axis=-1)))


def stats(a):
    return {"mean":float(np.mean(a)), "p99":float(np.percentile(a,99)), "max":float(np.max(a))}


def snell(incident, normal, eta):
    # Independent tangent/normal decomposition, rather than the shader's
    # refract expression. Normals face the incident medium.
    tangent = eta * (incident - np.sum(incident*normal, axis=-1, keepdims=True)*normal)
    discriminant = 1 - np.sum(tangent*tangent, axis=-1, keepdims=True)
    assert np.min(discriminant) > -1e-6
    return unit(tangent - normal*np.sqrt(np.maximum(discriminant,0)))


def glb_mesh(path):
    data=path.read_bytes()
    length,kind=struct.unpack_from("<II",data,12)
    assert kind==0x4E4F534A
    doc=json.loads(data[20:20+length])
    binary_offset=20+length+8
    def accessor(index):
        a=doc["accessors"][index];v=doc["bufferViews"][a["bufferView"]]
        dtype=np.dtype({5126:"<f4",5125:"<u4",5123:"<u2"}[a["componentType"]])
        channels={"VEC3":3,"SCALAR":1}[a["type"]]
        offset=binary_offset+v.get("byteOffset",0)+a.get("byteOffset",0)
        return np.ndarray((a["count"],channels),dtype=dtype,buffer=data,offset=offset,
            strides=(v.get("byteStride",dtype.itemsize*channels),dtype.itemsize)).copy()
    primitive=doc["meshes"][0]["primitives"][0]
    return (accessor(primitive["attributes"]["POSITION"]).astype(float),
            accessor(primitive["attributes"]["NORMAL"]).astype(float),
            accessor(primitive["indices"]).reshape(-1,3).astype(np.int64))


def primary_rays(config,width,height):
    target=np.array([config.get("target-x",0),config["target-y"],config.get("target-z",0)])
    yaw,pitch,distance=(config[k] for k in ("yaw","pitch","distance"))
    eye=target+distance*np.array([np.cos(pitch)*np.sin(yaw),np.sin(pitch),np.cos(pitch)*np.cos(yaw)])
    forward=unit(target-eye);right=unit(np.cross(forward,[0,1,0]));up=np.cross(right,forward)
    tan=np.tan(np.radians(config["fov"])/2)
    x=(2*(np.arange(width)+.5)/width-1)*tan*width/height
    y=(1-2*(np.arange(height)+.5)/height)*tan
    return eye,unit(forward+x[None,:,None]*right+y[:,None,None]*up)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary",type=Path,default=Path("build/dev/projects/raytracing/raytracing"))
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    width,height=320,180
    preset=Path("projects/raytracing/presets/optics/dragon-forward.json")
    config=json.loads(preset.read_text());eye,rays=primary_rays(config,width,height)
    binary_hash=hashlib.sha256(args.binary.read_bytes()).hexdigest()
    calls=[]

    def capture(name,view="final",extra=()):
        prefix=args.output/name.replace(".","p")
        command=[str(args.binary),"--config",str(preset),"--headless","--require-validation",
            "--width",str(width),"--height",str(height),"--view",view,
            "--output",str(prefix.with_suffix(".png")),"--hdr-output",str(prefix.with_suffix(".bin")),
            "--metrics",str(prefix.with_suffix(".json")),*map(str,extra)]
        result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        prefix.with_suffix(".log").write_text(result.stdout)
        assert result.returncode==0 and not any(s in result.stdout.lower() for s in ("validation error","vuid-","hazard detected")),result.stdout
        raw=np.fromfile(prefix.with_suffix(".bin"),dtype="<f4").reshape(height,width,4).astype(float)
        assert np.isfinite(raw).all(),name
        metrics=json.loads(prefix.with_suffix(".json").read_text())
        assert not metrics["ray_tracing_enabled"] and metrics["as_bytes"]==0
        calls.append({"name":name,"command":command,"validation_clean":True,"finite":True})
        print(name,flush=True)
        return raw,metrics

    actual,_=capture("ray","transmission-ray")
    exit_data,_=capture("exit","transmission-exit")
    shading,_=capture("normal","shading-normal")
    geometric,_=capture("input-normal","geometric-normal")
    # Background/opaque pixels have alpha 1/0 respectively. The glass Beer
    # distance is positive and bounded by the 0.20-unit solid approximation.
    mask=(actual[:,:,3]>1e-7)&(actual[:,:,3]<config.get("forward-thickness",.2)*1.001)
    normals=unit(shading[:,:,:3]*2-1)
    interior=mask&(np.sum(normals*(-rays),axis=-1)>.05)
    assert np.count_nonzero(interior)>5000
    inputs={}
    for view,expected in [("base-color",1),("roughness",.05),("metallic",0)]:
        raw,metrics=capture(view,view)
        error=float(np.max(abs(raw[interior,:3]-expected)))
        assert error<1e-5,(view,error)
        inputs[view]={"expected":expected,"max_abs_error":error}
    assert metrics["materials"][0]["ior"]==1.5
    assert metrics["materials"][0]["transmission_factor"]==1

    optical_cases=[];inferred_entry=None
    for ior in (1.,1.33,1.5,2.5):
        recipe=json.loads(Path("assets/materials/glass.json").read_text())
        recipe["surface"]["optics"]["ior"]=ior
        recipe["surface"]["optics"]["attenuation_color"]=[1,1,1]
        recipe_path=args.output/f"neutral-{ior}.json"
        recipe_path.write_text(json.dumps(recipe,indent=2)+"\n")
        data,_=capture(f"ior-{ior}","transmission-ray",["--recipe-input",recipe_path])
        n=normals[interior];incident=rays[interior]
        internal=snell(incident,n,1/ior)
        # A sphere of radius thickness/2 tangent to the entry surface has its
        # second intersection at -2R dot(n, internal). Solve that construction.
        length=-config.get("forward-thickness",.2)*np.sum(n*internal,axis=-1)
        outward=unit(n+internal*(length/(config.get("forward-thickness",.2)/2))[:,None])
        outgoing=snell(internal,-outward,ior)
        direction_error=stats(angle(data[interior,:3],outgoing))
        length_error=stats(abs(data[interior,3]-length))
        assert direction_error["max"]<.01,direction_error
        # Allow 10 micrometres for raster/float32 camera interpolation. This is
        # 0.005% of the selected 0.20 m approximation; report the actual error.
        assert length_error["max"]<1e-5,length_error
        optical_cases.append({"ior":ior,"direction_error_degrees":direction_error,"length_error_m":length_error})
        if ior==1.5:
            inferred_entry=exit_data[interior,:3]-internal*length[:,None]

    # Verify the actual raster entry against independent nearest front-facing
    # triangle intersections, including the preserved model translation.
    positions,vertex_normals,triangles=glb_mesh(Path(config["model"]))
    positions[:,1]+=-positions[:,1].min()+.001
    a=positions[triangles[:,0]];e1=positions[triangles[:,1]]-a;e2=positions[triangles[:,2]]-a
    tvec=eye-a;q=np.cross(tvec,e1);numerator=np.sum(e2*q,axis=-1)
    selected=np.linspace(0,np.count_nonzero(interior)-1,32,dtype=int)
    geometric_normals=unit(geometric[interior,:3]*2-1)
    entry_errors=[];normal_errors=[]
    for slot in selected:
        ray=rays[interior][slot];p=np.cross(ray,e2);det=np.sum(e1*p,axis=-1)
        inv=np.divide(1,det,out=np.zeros_like(det),where=det>1e-12)
        u=np.sum(tvec*p,axis=-1)*inv;v=np.sum(ray*q,axis=-1)*inv;t=numerator*inv
        valid=(det>1e-12)&(u>=0)&(v>=0)&(u+v<=1)&(t>0)
        nearest=np.argmin(np.where(valid,t,np.inf));assert valid[nearest]
        point=eye+t[nearest]*ray
        bary=np.array([1-u[nearest]-v[nearest],u[nearest],v[nearest]])
        normal=unit(np.sum(vertex_normals[triangles[nearest]]*bary[:,None],axis=0))
        entry_errors.append(np.linalg.norm(point-inferred_entry[slot]))
        normal_errors.append(angle(normal,geometric_normals[slot]))
    assert max(entry_errors)<2e-5,stats(entry_errors)
    # This is a wrong-surface/wrong-normal binding check on very small scanned
    # triangles, not an ideal-normal optical-accuracy gate. Retain measured
    # differences rather than promising exact CPU/raster interpolation parity.
    assert max(normal_errors)<.5,stats(normal_errors)

    thin,_=capture("thin-ray","transmission-ray",["--forward-thickness",".1"])
    assert np.max(abs(thin[interior,:3]-actual[interior,:3]))<1e-6
    assert np.max(abs(2*thin[interior,3]-actual[interior,3]))<1e-6
    routes={}
    null_routes={}
    hidden_ray,_=capture("hidden-ray","transmission-ray",["--hide-dragon"])
    # The environment contains sky, while the screen image also contains the
    # marble. A source null comparison must exclude those local opaque pixels.
    sky_interior=interior&(hidden_ray[:,:,3]>.5)
    assert np.count_nonzero(sky_interior)>4000
    for source in ("screen","environment"):
        routes[source],_=capture("final-"+source,extra=["--forward-transmission-source",source])
        null_routes[source],_=capture("null-"+source,"transmission-radiance",
            ["--forward-transmission-source",source,"--recipe-input",args.output/"neutral-1.0.json"])
    null_difference=stats(np.linalg.norm(null_routes["screen"][sky_interior,:3]-null_routes["environment"][sky_interior,:3],axis=-1))
    assert null_difference["p99"]<.02,null_difference
    # Isolate actual final-frame transmission with a fully absorbing control.
    # This also checks that the glass is not merely an opaque reflective layer.
    neutral,_=capture("neutral-final",extra=["--forward-transmission-source","environment",
        "--recipe-input",args.output/"neutral-1.5.json"])
    absorbing=json.loads((args.output/"neutral-1.5.json").read_text())
    absorbing["surface"]["optics"]["attenuation_color"]=[.01,.01,.01]
    absorbing["surface"]["optics"]["attenuation_distance"]=.0001
    absorbing_path=args.output/"absorbing.json"
    absorbing_path.write_text(json.dumps(absorbing,indent=2)+"\n")
    reflected,_=capture("absorbing-final",extra=["--forward-transmission-source","environment",
        "--recipe-input",absorbing_path])
    transmitted=neutral[interior,:3]-reflected[interior,:3]
    assert np.median(np.linalg.norm(transmitted,axis=-1))>.1
    canonical=json.loads(Path("assets/materials/glass.json").read_text())["surface"]["optics"]
    beer=np.power(np.array(canonical["attenuation_color"]),actual[interior,3,None]/canonical["attenuation_distance"])
    expected=reflected[interior,:3]+transmitted*beer
    beer_error=stats(abs(routes["environment"][interior,:3]-expected))
    assert beer_error["max"]<1e-5,beer_error
    result={"passed":True,"binary_sha256":binary_hash,"calls":calls,"material_inputs":inputs,
        "covered_glass_pixels":int(mask.sum()),"interior_pixels":int(interior.sum()),
        "optical_cases":optical_cases,"cpu_entry_sample_count":32,
        "cpu_entry_position_error_m":stats(entry_errors),"cpu_vertex_normal_error_degrees":stats(normal_errors),
        "half_thickness_scales_path_without_changing_direction":True,
        "ior1_source_null_sky_pixels":int(np.count_nonzero(sky_interior)),
        "ior1_source_radiance_difference":null_difference,
        "final_frame_beer_composition_error":beer_error,
        "median_final_transmitted_rgb_norm":float(np.median(np.linalg.norm(transmitted,axis=-1))),
        "scope":"Production forward shader, observed shading normals, existing tangent-sphere approximation; no claim of exact arbitrary-mesh refraction"}
    assert hashlib.sha256(args.binary.read_bytes()).hexdigest()==binary_hash
    (args.output/"receipt.json").write_text(json.dumps(result,indent=2)+"\n")
    print("Forward dragon audit passed",flush=True)


if __name__=="__main__":
    main()
