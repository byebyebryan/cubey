#!/usr/bin/env python3
"""Bounded selected-shader workload qualification; GPU work stays serial."""
import argparse
import json
import math
from pathlib import Path
import statistics
import struct
from acceleration_loop import save
from benchmark_tracing import percentile
from investigate_guardian import ROOT, digest, invoke, metadata
from check_tracing_images import pixels


def summarize(path):
    data = path.read_bytes()
    width, height, stride, count = struct.unpack_from('<4I', data)
    if len(data) != 16+count*256 or len(data)+width*height*8>32*1024**2 or stride!=8:
        raise RuntimeError('workload layout/budget mismatch')
    totals=[[0]*4 for _ in range(3)];walk=[[0]*4 for _ in range(3)]
    hist=[0]*8;stats=[0]*4;hits=exhausted=0;tails=[[] for _ in range(3)]
    maxrun=[];bad=0
    for i in range(count):
        offset=16+i*256;u=struct.unpack_from('<64I',data,offset)
        f=struct.unpack_from('<16f',data,offset+36*4)
        if u[54]!=i or u[55]!=0x51575231 or not all(math.isfinite(v) for v in f):
            raise RuntimeError('incomplete/nonfinite workload row')
        hits+=f[1]==1;exhausted+=f[3]!=0
        bad+=f[1] not in (0,1,2)
        for phase in range(3):
            for k in range(4):
                if k==2:totals[phase][k]=max(totals[phase][k],u[phase*4+k])
                else:totals[phase][k]+=u[phase*4+k]
                walk[phase][k]+=u[12+phase*4+k]
            tails[phase].append(u[phase*4])
        for k in range(8):hist[k]+=u[24+k]
        for k in (0,1,3):stats[k]+=u[32+k]
        stats[2]=max(stats[2],u[34]);maxrun.append(u[34])
    if bad or exhausted or totals[1][0]!=hits*6:
        raise RuntimeError('hit/status/normal-query workload invariant')
    return dict(width=width,height=height,sampled_camera_rays=count,stride=stride,
        storage_bytes=len(data),raster_uv_bytes=width*height*8,sha256=digest(path),hits=hits,exhausted=exhausted,
        phases={name:dict(exact_queries=t[0],csg_instructions=t[1],max_program=t[2],
            root_fallback_queries=t[3],iterations=w[0],bound_queries=w[1],far_advances=w[2],
            mean_program=t[1]/t[0] if t[0] else 0,
            queries_per_camera_ray_p95=percentile(tail,.95),queries_per_camera_ray_max=max(tail))
            for name,t,w,tail in zip(('primary','normal','shadow'),totals,walk,tails)},
        primary_exact_run_histogram=dict(zip(('1','2','3-4','5-8','9-16','17-32','33-64','65+'),hist)),
        required_shadow_rays=stats[0],skipped_shadow_calls=stats[1],max_primary_exact_run=stats[2],
        primary_exact_runs=stats[3],primary_max_run_per_camera_ray_p95=percentile(maxrun,.95),
        mean_primary_run_length=totals[0][0]/stats[3] if stats[3] else 0,
        scope='Actual shared shader functions; sampled query counts, not GPU time attribution')


def workload(args):
    receipt=dict(metadata(args.binary),runner_sha256=digest(Path(__file__)),runs=[])
    for scene,preset in [('guardian','sculpture-guardian-limestone'),('robot','survey-robot-fieldkit')]:
        config=json.loads((ROOT/f'projects/sdf/presets/{preset}.json').read_text())
        for view,offset in [('preset',0),('side',math.pi/2),('rear',math.pi)]:
            for mode in ['shadow','combined']:
                prefix=args.output/f'{scene}-{view}-{mode}'
                base=[str(args.binary),'--headless','--require-validation','--supersample','1',
                    '--config',f'projects/sdf/presets/{preset}.json','--width','1920','--height','1080',
                    '--surface-trace','analytic','--csg-evaluation','pruned','--region-axis','64',
                    '--acceleration-variant','root-far','--source-optimization',mode,
                    '--yaw',str(config['yaw']+offset),'--frames','1']
                native=prefix.with_suffix('.native.png');replay=prefix.with_suffix('.replay.png')
                invoke(base+['--output',str(native)],Path(str(prefix)+'-native'),timeout=180)
                raw=prefix.with_suffix('.bin')
                command=base+['--output',str(replay),'--query-workload-output',str(raw)]
                invoke(command,Path(str(prefix)+'-replay'),timeout=180)
                identical=digest(native)==digest(replay)
                difference={}
                if not identical:
                    _,_,ca,a=pixels(native);_,_,cb,b=pixels(replay)
                    differences=[abs(x-y) for ra,rb in zip(a,b) for x,y in zip(ra,rb)]
                    difference=dict(changed_channels=sum(d!=0 for d in differences),max_channel=max(differences))
                row=dict(scene=scene,view=view,mode=mode,command=command,
                    native_image_sha256=digest(native),validated_image_sha256=digest(replay),
                    native_image_identical=identical,difference=difference,workload=summarize(raw))
                receipt['runs'].append(row);save(args.output/'workload.json',receipt)
                print(prefix.name,'parity',identical,'queries',row['workload']['phases']['primary']['exact_queries'],flush=True)
                if not identical:raise RuntimeError('native replay parity failed; inspect before continuing')
    receipt['complete']=True;save(args.output/'workload.json',receipt)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase',choices=['workload'])
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--binary',type=Path,default=ROOT/'build/release/projects/sdf/sdf')
    args=p.parse_args();args.binary=args.binary.resolve();args.output=args.output.resolve()
    args.output.mkdir(parents=True,exist_ok=False);workload(args)


if __name__=='__main__':main()
