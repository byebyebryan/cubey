# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Serial, source-identified workshop captures and completed-sample GPU costs.

Run with no other owned GPU workload. Boundary observations describe the shared
host; they cannot establish uninterrupted exclusive hardware access.
"""
import argparse
import json
import os
import shlex
from pathlib import Path
import subprocess
import time
import numpy as np
from compare_material_samples import compare
from profile_first_pass import gpu_state, quantile, sha


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, default=Path('build/dev/projects/raytracing/raytracing'))
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--material', action='append')
    p.add_argument('--mode', choices=['quality', 'timing', 'matrix'], required=True)
    p.add_argument('--resume',action='store_true')
    p.add_argument('--sphere-density',choices=['standard','dense'],default='standard')
    args = p.parse_args()
    args.binary = args.binary.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=args.resume)
    materials = args.material or ['bronze']
    patch = subprocess.check_output(['git', 'diff', 'HEAD', '--binary'])
    (out/'source.patch').write_bytes(patch)
    compilation=json.loads(Path('build/dev/compile_commands.json').read_text())
    entry=next(c for c in compilation if c['file'].endswith('raytracing_app.cpp'))
    tokens=shlex.split(entry['command'])
    build=dict(compiler=tokens[0],flags=[t for t in tokens if t.startswith(('-O','-g','-std=','-DNDEBUG','-DGLM'))],compiler_version=subprocess.check_output([tokens[0],'--version'],text=True).splitlines()[0])
    identity = dict(source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
                    status=subprocess.check_output(['git','status','--porcelain'],text=True),
                    binary_sha256=sha(args.binary),
                    shaders={f.name:sha(f) for f in sorted((args.binary.parent/'shaders').glob('*.spv'))},
                    recipes={f.name:sha(f) for f in sorted(Path('assets/materials').glob('*.json'))},
                    gpu=gpu_state(), build=build, scope=__doc__)
    if args.resume:
        previous=json.loads((out/'identity.json').read_text())
        assert all(previous[k]==identity[k] for k in ['source','binary_sha256','shaders','recipes']), 'Resume requires identical inputs'
    else:
        (out/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
    runs = json.loads((out/'runs.json').read_text()) if args.resume and (out/'runs.json').exists() else []

    def run(name, material, extra, validated=True, width=640, height=480):
        command = [str(args.binary),'--scene','materials','--material',material,
                   '--sphere-density',args.sphere_density,
                   '--headless','--width',str(width),'--height',str(height),
                   '--require-validation' if validated else '--no-validation',
                   '--metrics',str(out/(name+'.json')),*extra]
        prior=next((r for r in runs if r['name']==name),None)
        if prior:
            assert prior['command']==command
            assert all(sha(Path(f))==digest for f,digest in prior['artifacts'].items())
            return json.loads((out/(name+'.json')).read_text()),prior
        start = time.monotonic()
        before = gpu_state()
        native = subprocess.run(command,capture_output=True,text=True,timeout=900,
                                env=dict(os.environ,VK_LAYER_VALIDATE_SYNC='1') if validated else os.environ)
        log = native.stdout+native.stderr
        (out/(name+'.log')).write_text(log)
        assert native.returncode==0 and not any(t in log.lower() for t in
            ['vuid-','hazard detected','vulkan validation error']),log
        record = dict(name=name,command=command,before=before,after=gpu_state(),
                      wall_seconds=time.monotonic()-start)
        record['artifacts']={command[i+1]:sha(Path(command[i+1])) for i,flag in enumerate(command)
                             if flag in ['--output','--hdr-output','--metrics']}
        runs.append(record)
        (out/'runs.json').write_text(json.dumps(runs,indent=2)+'\n')
        return json.loads((out/(name+'.json')).read_text()),record

    results = []
    if args.mode=='quality':
        for material in materials:
            for source in ['environment','scene']:
                for samples,seed in [(256,7),(256,31),(4096,19),(8192,43)]:
                    name=f'{material}-{source}-{samples}-seed{seed}'
                    data,_=run(name,material,['--frames','1','--samples',str(samples),'--seed',str(seed),
                        '--reflection-source',source,'--transmission-source',source,
                        '--hdr-output',str(out/(name+'.bin')),'--output',str(out/(name+'.png'))])
                    assert data['history_samples']==samples and data['history_epochs']==1
                    print(name,'captured',flush=True)
                reference=out/f'{material}-{source}-4096-seed19.bin'
                for samples,seed in [(256,7),(256,31),(8192,43)]:
                    name=f'{material}-{source}-{samples}-seed{seed}'
                    scored=compare(out/(name+'.bin'),reference,640,480,.25,1.05,45)
                    scored.update(name=name,material=material,source=source,samples=samples,seed=seed)
                    results.append(scored)
                    print(name,scored['passed'],scored['roi_mean'],scored['roi_p95'],flush=True)
                    (out/'quality.json').write_text(json.dumps(results,indent=2)+'\n')
        assert all(r['passed'] for r in results),'Fixed convergence screen failed; inspect retained captures'
    if args.mode=='timing':
        for material in materials:
            for source in ['environment','scene']:
                for repetition in range(3):
                    name=f'{material}-{source}-{repetition}'
                    data,record=run(name,material,['--headless-render-only','--frames','180',
                        '--samples','8192','--reflection-source',source,'--transmission-source',source],
                        False,1920,1080)
                    assert data['dropped_gpu_samples']==0 and data['history_samples']==180
                    assert all(j['new_samples']==1 for j in data['sampling_frames'])
                    record.update(material=material,source=source,repetition=repetition,gpu={})
                    for label in ['raytracing.trace','raytracing.whole_scene',
                                  'raytracing.environment_update','raytracing.total_frame']:
                        rows=[r for r in data['gpu_samples'] if r['pass']==label and r['frame']>=30]
                        assert len(rows)==150 and len({r['frame'] for r in rows})==150
                        values=[r['milliseconds'] for r in rows]
                        record['gpu'][label]=dict(p50_ms=quantile(values,.5),p95_ms=quantile(values,.95),samples=150)
                    record['passed']=record['gpu']['raytracing.total_frame']['p95_ms']<=16.7
                    results.append(record)
                    (out/'timing.json').write_text(json.dumps(results,indent=2)+'\n')
                    print(name,record['gpu']['raytracing.total_frame'],flush=True)
        assert all(r['passed'] for r in results),'Fixed frame-cost screen failed'
    if args.mode=='matrix':
        for material in materials:
            for roughness in [.05,.1,.2,.32,.5,.7,.9,1]:
                for source in ['environment','scene']:
                    name=f'{material}-furnace-{roughness}-{source}'
                    data,_=run(name,material,['--frames','1','--samples','4096','--no-pattern','--no-bump',
                        '--workshop-lighting','furnace','--material-roughness',str(roughness),
                        '--reflection-source',source,'--transmission-source',source,
                        '--hdr-output',str(out/(name+'.bin')),'--output',str(out/(name+'.png'))],True,128,96)
                    raw=np.fromfile(out/(name+'.bin'),dtype='<f4').reshape(96,128,4)
                    center=raw[46:50,62:66,:3].mean(axis=(0,1))
                    assert np.isfinite(raw).all() and np.min(raw[:,:,:3])>=0 and np.max(center)<=1.02
                    results.append(dict(name=name,center=center.tolist(),finite_nonnegative=True,energy_limit=1.02))
                    (out/'matrix.json').write_text(json.dumps(results,indent=2)+'\n')
                    print(name,center,flush=True)
        # Linearity uses identical samples, proposal PDFs and hit footprints.
        for material in materials:
            products={}
            for component in ['combined','sky','sun']:
                name=f'{material}-component-{component}'
                run(name,material,['--frames','1','--samples','256','--workshop-lighting',component,
                    '--reflection-source','scene','--hdr-output',str(out/(name+'.bin')),
                    '--output',str(out/(name+'.png'))],True,128,96)
                products[component]=np.fromfile(out/(name+'.bin'),dtype='<f4').reshape(96,128,4)[:,:,:3]
            # Primary misses display the accepted sky in every component mode.
            y,x=np.mgrid[:96,:128]
            roi=(x+.5-64)**2+(y+.5-48)**2<25**2
            error=float(np.max(np.abs(products['combined'][roi]-products['sky'][roi]-products['sun'][roi])))
            assert error<=5e-4,error
            results.append(dict(material=material,component_additivity_max_error=error,limit=5e-4))
            (out/'matrix.json').write_text(json.dumps(results,indent=2)+'\n')
    (out/'receipt.json').write_text(json.dumps(dict(passed=True,mode=args.mode,materials=materials,
                                                 results=results),indent=2)+'\n')


if __name__=='__main__':
    main()
