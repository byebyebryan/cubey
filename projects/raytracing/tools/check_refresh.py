#!/usr/bin/env python3
"""Exercise moving shared sky generations through real RT submissions."""
import argparse
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
args=p.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
command=[str(args.binary.resolve()),'--headless','--headless-render-only','--frames','90',
         '--width','192','--height','128','--require-validation','--scene','reflection-fixture',
         '--environment','atmosphere','--target-y','.55','--sun-speed','.2',
         '--metrics',str((args.output/'metrics.json').resolve())]
run=subprocess.run(command,capture_output=True,text=True,timeout=120,env=dict(os.environ,VK_LAYER_VALIDATE_SYNC='1'))
log=run.stdout+run.stderr
(args.output/'native.log').write_text(log)
assert run.returncode==0 and not any(x in log.lower() for x in ['vuid-','hazard detected','vulkan validation error']),log
metrics=json.loads((args.output/'metrics.json').read_text())
assert metrics['environment_generation']>=4,metrics['environment_generation']
assert metrics['as_builds']==7,metrics['as_builds']
assert metrics['target_generations']==1,metrics['target_generations']
assert len([r for r in metrics['gpu_samples'] if r['pass']=='raytracing.total_frame'])==90
(args.output/'result.json').write_text(json.dumps(dict(passed=True,command=command,
    sky_generations=metrics['environment_generation'],as_builds=metrics['as_builds'],
    target_generations=metrics['target_generations'],completed_frames=90),indent=2)+'\n')
print((args.output/'result.json').read_text())
