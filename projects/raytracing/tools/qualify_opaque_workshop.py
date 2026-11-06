# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Exercise opaque specimen routes, filtering, components and retained forward bytes."""
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
    p.add_argument('--resume',action='store_true')
    args=p.parse_args()
    out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=args.resume)
    binary=Path('build/dev/projects/raytracing/raytracing').resolve()
    identity=dict(binary_sha256=sha(binary),shader_sha256=sha(binary.parent/'shaders/trace.rgen.spv'),
                  source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip())
    identity_path=out/'identity.json'
    if args.resume:
        assert json.loads(identity_path.read_text())==identity,'Resume needs the identical source/binary/shader'
    else:
        identity_path.write_text(json.dumps(identity,indent=2)+'\n')
    catalog=json.loads(Path('assets/materials/catalog.json').read_text())['entries']
    opaque=[e['id'] for e in catalog if e['family']!='glass']
    results=[]
    def capture(name,material,extra=()):
        command=[str(binary),'--scene','materials','--material',material,'--headless','--frames','1',
                 '--width','160','--height','120','--samples','64','--require-validation',
                 '--hdr-output',str(out/(name+'.bin')),'--output',str(out/(name+'.png')),
                 '--metrics',str(out/(name+'.json')),*extra]
        receipt=out/(name+'.capture.json')
        if args.resume and receipt.exists():
            assert json.loads(receipt.read_text())['command']==command
        else:
            r=subprocess.run(command,capture_output=True,text=True,timeout=180,
                             env=dict(os.environ,VK_LAYER_VALIDATE_SYNC='1'))
            log=r.stdout+r.stderr
            (out/(name+'.log')).write_text(log)
            assert r.returncode==0 and not any(t in log.lower() for t in
                ['vuid-','hazard detected','vulkan validation error']),log
            receipt.write_text(json.dumps(dict(command=command,exit=0,validation_clean=True),indent=2)+'\n')
        raw=np.fromfile(out/(name+'.bin'),dtype='<f4').reshape(120,160,4)
        assert np.isfinite(raw).all() and np.min(raw[:,:,:3])>=0
        results.append(dict(name=name,command=command,image_sha256=sha(out/(name+'.png')),
                            status_counts={str(int(v)):int(n) for v,n in zip(*np.unique(raw[:,:,3],return_counts=True))}))
        (out/'partial.json').write_text(json.dumps(results,indent=2)+'\n')
        return raw[:,:,:3]
    for material in opaque:
        for shape in ['sphere','block','swatch','cut-block']:
            capture(f'{material}-{shape}',material,['--specimen',shape])
        for toggle in ['--no-pattern','--no-bump']:
            capture(f'{material}-{toggle[2:]}',material,[toggle])
        capture(material+'-grazing',material,['--specimen','swatch','--yaw','1.2'])
        capture(material+'-far',material,['--distance','3'])
        parts={v:capture(material+'-'+v,material,['--view',v,'--reflection-source','scene'])
               for v in ['final','diffuse','specular','sheen']}
        error=float(np.max(np.abs(parts['final']-parts['diffuse']-parts['specular']-parts['sheen'])))
        assert error<=5e-4,error
        a=capture(material+'-scale1',material,['--no-bump','--view','base-color'])
        b=capture(material+'-scale2',material,['--no-bump','--view','base-color',
                  '--object-scale','2','--distance','2.1'])
        # Match object coordinates and object-space footprint at double scale.
        error_scale=float(np.max(np.abs(a-b)))
        assert error_scale<=5e-4,(material,error_scale)
        results.append(dict(material=material,component_additivity_max_error=error,
                            scaled_base_max_error=error_scale,component_limit=5e-4))
        print(material,'routes/toggles/components/scale passed',error,error_scale,flush=True)
    footprint=capture('footprint-near','marble',['--view','footprint'])
    far=capture('footprint-far','marble',['--view','footprint','--distance','2.1'])
    center=np.s_[58:62,78:82,:]
    ratio=float(np.mean(far[center])/np.mean(footprint[center]))
    assert 1.9<ratio<2.5,ratio
    results.append(dict(footprint_center_distance_ratio=ratio))
    receipt=dict(passed=True,source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
                 binary_sha256=sha(binary),shader_sha256=sha(binary.parent/'shaders/trace.rgen.spv'),
                 results=results,scope=__doc__)
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')


if __name__=='__main__':
    main()
