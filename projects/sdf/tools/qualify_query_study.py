#!/usr/bin/env python3
"""Qualify diagnostic root/residency ablations without changing fixture tolerances."""
import argparse
import json
import math
from pathlib import Path
from sdf_paths import resolve_sdf_path
import struct
from query_study import metadata, invoke, PRESETS, ROOT
from tracing_comparison import check, EPS, dot


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--binary',type=Path,default=Path('build/release/projects/sdf/sdf'))
    p.add_argument('--workload-root',type=Path,required=True)
    args=p.parse_args();args.binary=args.binary.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    receipt=dict(metadata(args.binary),fixtures=[],replays=[],captures=[])
    manifest=ROOT/'projects/sdf/comparison/fixtures.json';source=json.loads(manifest.read_text())
    base=[str(args.binary),'--headless','--headless-render-only','--require-validation','--frames','1',
          '--width','32','--height','32','--csg-evaluation','pruned']
    def save(): (out/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    for mode in ('analytic','hybrid'):
        for axis in (16,64):
            prefix=out/f'fixtures-{mode}-{axis}'
            cmd=base+['--surface-trace',mode,'--region-axis',str(axis),'--comparison-tight-root',
                '--comparison-input',str(manifest),'--comparison-output',str(prefix.with_suffix('.json'))]
            invoke(cmd,prefix)
            result=json.loads(prefix.with_suffix('.json').read_text());score=check(source,result)
            receipt['fixtures'].append(dict(mode=mode,axis=axis,command=cmd,score=score,
                derived_probe_count=sum(c['derived_probe_count'] for c in result['cases']),
                derived_probe_max_error=max(c['derived_probe_max_error'] for c in result['cases'])))
            save();print('fixtures',mode,axis,score['passed'],score['scored'],flush=True)
            if not score['passed']:raise RuntimeError('unchanged independent fixture gate failed')
    # Sparse uninstrumented replay establishes that counting did not change the geometry.
    # Compare both instrumented variants too, with the prior declared incidence-scaled contract.
    for scene in PRESETS:
        inp=json.loads((args.workload_root/'workload'/f'{scene}-input.json').read_text())
        all_queries=inp['cases'][0]['queries'];indices=list(range(0,len(all_queries),16))
        inp['workload']=False;inp['cases'][0]['queries']=[all_queries[i] for i in indices]
        input_path=out/f'{scene}-sparse.json';input_path.write_text(json.dumps(inp,separators=(',',':'))+'\n')
        for mode in ('analytic','hybrid'):
            paired=[]
            for tight,folder in [(False,'workload'),(True,'workload-tight')]:
                original=args.workload_root/folder/f'{scene}-{mode}-64.json'
                c=json.loads(original.read_text())['cases'][0];payload=c['workload']
                rays=list(struct.iter_unpack('<8f',Path(payload['rays_file']).read_bytes()))
                paired.append(rays)
                prefix=out/f'{scene}-{mode}-{"tight" if tight else "outer"}'
                cmd=base+['--surface-trace',mode,'--region-axis','64','--comparison-input',str(input_path),
                          '--comparison-output',str(prefix.with_suffix('.json'))]
                if tight:cmd+=['--comparison-tight-root']
                invoke(cmd,prefix);actual=json.loads(prefix.with_suffix('.json').read_text())['cases'][0]['results']
                failures=[];max_depth=0
                for i,a in zip(indices,actual):
                    r=rays[i];max_depth=max(max_depth,abs(r[0]-a['t']))
                    if int(r[1])!=a['status'] or abs(r[0]-a['t'])>4*EPS:
                        failures.append(i)
                row=dict(scene=scene,mode=mode,tight=tight,command=cmd,queries=len(indices),
                         counting_geometry_failures=failures,max_depth_difference=max_depth)
                receipt['replays'].append(row);save()
                if failures:raise RuntimeError('counter/ordinary trace equivalence failed')
            failures=[];max_regular=0;max_grazing=0;regular=0;grazing=0
            for i,(a,b,q) in enumerate(zip(*paired,all_queries)):
                if int(a[1])!=int(b[1]) or int(b[1])==2:
                    failures.append([i,'status']);continue
                if int(a[1])!=1:continue
                incidence=abs(dot(a[4:7],q['direction']));err=abs(a[0]-b[0])
                if incidence>=.25:
                    regular+=1;max_regular=max(max_regular,err);limit=4*EPS
                else:
                    grazing+=1;max_grazing=max(max_grazing,err);limit=4*EPS/max(.01,incidence)
                if err>limit or abs(b[3])>EPS+2e-6:
                    failures.append([i,'depth/residual',err])
            receipt['replays'].append(dict(scene=scene,mode=mode,comparison='outer/inner dense',
                queries=len(all_queries),regular_hits=regular,grazing_hits=grazing,
                max_regular_depth_difference=max_regular,max_grazing_depth_difference=max_grazing,failures=failures));save()
            print('replay',scene,mode,len(all_queries),'failures',len(failures),flush=True)
            if failures:raise RuntimeError('dense root differential gate failed')
    for scene in ('guardian','robot'):
        for mode in ('analytic','hybrid'):
            for label,flags in [('host-outer',[]),('host-inner',['--comparison-tight-root']),
                                ('local-outer',['--comparison-residency','all']),
                                ('local-inner',['--comparison-tight-root','--comparison-residency','all'])]:
                prefix=out/f'{scene}-{mode}-{label}';image=prefix.with_suffix('.png')
                cmd=[str(args.binary),'--headless','--require-validation','--frames','1','--supersample','1',
                    '--width','320','--height','180','--config',str(resolve_sdf_path(f'projects/sdf/presets/{PRESETS[scene]}.json')),
                    '--surface-trace',mode,'--csg-evaluation','pruned','--region-axis','64',
                    '--comparison-stage','full','--output',str(image),*flags]
                invoke(cmd,prefix)
                receipt['captures'].append(dict(scene=scene,mode=mode,label=label,command=cmd,image=str(image)))
                save()
    receipt['complete']=True;save()


if __name__=='__main__':main()
