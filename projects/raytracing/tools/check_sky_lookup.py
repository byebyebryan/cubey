# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Compare environment-reflected radiance with independently aimed primary sky views."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import numpy as np
from qualify_optics import native
from optics_reference import camera, normalize

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,default=Path('build/dev/projects/raytracing/raytracing'))
p.add_argument('--output',type=Path,required=True)
args=p.parse_args();args.binary=args.binary.resolve();args.output=args.output.resolve()
args.output.mkdir(parents=True,exist_ok=False)
source,data,command=native(args.binary,args.output,'reflection',['--scene','reflection-fixture','--environment','atmosphere',
    '--reflection-source','environment','--target-y','.55','--distance','3'])
eye,_=camera(data['config'],128,96)
eligible=np.flatnonzero((source[:,1,3]==1)&(source[:,5,1]>.15)&(source[:,5,1]<.75))
assert len(eligible)>5
rows=[]
for index,pixel in enumerate(eligible[np.linspace(0,len(eligible)-1,5).astype(int)]):
    row=source[pixel];direction=normalize(row[5,:3]);target=eye+3*direction
    yaw=np.arctan2(-direction[0],-direction[2]);pitch=np.arcsin(-direction[1])
    out=args.output/str(index);out.mkdir()
    cmd=[str(args.binary),'--scene','fixture','--environment','atmosphere','--view','environment','--headless','--frames','1',
         '--width','1','--height','1','--distance','3','--yaw',str(yaw),'--pitch',str(pitch),
         '--target-x',str(target[0]),'--target-y',str(target[1]),'--target-z',str(target[2]),
         '--require-validation','--hdr-output',str(out/'hdr.bin'),'--output',str(out/'frame.png')]
    run=subprocess.run(cmd,capture_output=True,text=True,timeout=90,env=dict(os.environ,VK_LAYER_VALIDATE_SYNC='1'))
    log=run.stdout+run.stderr;(out/'native.log').write_text(log)
    assert run.returncode==0 and not any(t in log.lower() for t in ['vuid-','hazard detected','vulkan validation error']),log
    primary=np.fromfile(out/'hdr.bin',dtype='<f4')[:3].astype(float)
    instance=data['instances'][int(row[0,1])]
    reflectance=np.array(data['materials'][instance['material']]['reflectance'])
    reflected=row[10,:3]/reflectance
    error=float(np.max(np.abs(primary-reflected))/max(1,float(np.max(primary))))
    assert error<=.001,(primary,reflected,error)
    rows.append(dict(pixel=int(pixel),direction=direction.tolist(),primary=primary.tolist(),reflected=reflected.tolist(),normalized_error=error,command=cmd))
result=dict(passed=True,tolerance=.001,source_command=command,rows=rows)
(args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
