# /// script
# dependencies = ["numpy==2.4.6"]
# ///
"""Read-only diagnostic replay of two previously measured bright samples.

Uses the existing opt-in journal build, without changing the ordinary renderer
or implementing any candidate noise treatment.
"""
import argparse
import json
import os
import subprocess
from pathlib import Path
import numpy as np

root=Path('outputs/raytracing/dragon-glass-solution-research-20261116').resolve()
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--analyze-only',action='store_true')
args=parser.parse_args()
out=root/'paths'
out.mkdir(exist_ok=True)
previous=Path('outputs/raytracing/dragon-glass-speckle-20261116/sample-probes')
results=[]
for name,x,y,sample in [('unstable',818,372,873),('persistent',738,527,1793)]:
    command=[str(root/'journal-build/raytracing'),'--config','projects/raytracing/presets/optics/dragon-glass.json','--headless','--frames','1','--require-validation','--width','1920','--height','1080','--samples','4096','--seed','19','--journal-x',str(x),'--journal-y',str(y),'--journal-sample',str(sample),'--journal-isolate','--ray-journal',str(out/(name+'.json')),'--output',str(out/(name+'.png'))]
    if not args.analyze_only:
        assert not (out/(name+'.json')).exists()
        run=subprocess.run(command,capture_output=True,text=True,env=dict(os.environ,VK_LAYER_VALIDATE_SYNC='1'),timeout=90)
        log=run.stdout+run.stderr
        (out/(name+'.log')).write_text(log)
        assert run.returncode==0,log
    log=(out/(name+'.log')).read_text()
    assert not any(t in log.lower() for t in ('vuid-','hazard detected','vulkan validation error')),log
    journal=json.loads((out/(name+'.json')).read_text())
    packets=np.array(journal['records'],dtype=np.uint32)
    assert journal['overflow']==0 and len(packets)==journal['attempted_records']
    final=packets[packets[:,0,0]==2]
    assert len(final)==1
    color=final[0,1,:3].copy().view(np.float32)
    old=json.loads((previous/(name+'-s19.journal.json')).read_text())
    rows=np.array(old['records'],dtype=np.uint32)
    target=rows[(rows[:,0,0]==2)&(rows[:,7,0]==sample)]
    assert len(target)==1
    reference=target[0,1,:3].copy().view(np.float32)
    assert np.allclose(color,reference,rtol=2e-5,atol=1e-5),(color,reference)
    queries=packets[packets[:,0,0]==10]
    hit=queries[:,3,3].copy().view(np.float32)>0
    ids=sorted(set(map(int,queries[hit,4,0])))
    branches=[]
    for branch in np.unique(packets[:,0,1]):
        rows=packets[packets[:,0,1]==branch]
        scatters=rows[rows[:,0,0]==41]
        if not len(scatters):
            continue
        path=[]
        for row in scatters:
            kind='T' if row[1,3].copy().view(np.float32)>.5 else 'R'
            path.append(dict(depth=int(row[0,2]),kind=kind,
                             total_internal_reflection=bool(row[4,3].copy().view(np.float32)>.5),
                             fresnel_selection_pdf=float(row[4,0].copy().view(np.float32)),
                             continuation_weight=float(row[4,1].copy().view(np.float32))))
        branches.append(dict(branch=int(branch),scatters=path))
    result=dict(name=name,pixel=[x,y],sample_index=sample,
                actual_rgb=color.tolist(),prior_rgb=reference.tolist(),
                status=int(final[0,1,3].copy().view(np.float32)),
                positive_hit_instance_ids=ids,
                only_certified_glass_hit=ids==[0],branches=branches,
                validation_clean=True,command=command)
    results.append(result)
    print(json.dumps({k:v for k,v in result.items() if k not in ('branches','command')},indent=2),flush=True)
Path(__file__).with_name('path-classification.json').write_text(json.dumps(dict(scope='two illustrative bright samples, not all scene paths',samples=results),indent=2)+'\n')
