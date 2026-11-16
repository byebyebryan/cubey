# /// script
# dependencies = ["numpy==2.4.6"]
# ///
import json, sys
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path('projects/raytracing/tools').resolve()))
from measure_dragon_glass import capture
root=Path('outputs/raytracing/dragon-glass-q0-20261108')
binary=root/'baseline-raytracing'
out=root/'failure-diagnostics'
def run(name,samples,extra=()):
    r=capture(binary,out,name,1920,1080,samples,7,extra,diagnostic_continue=True)
    pixel=np.memmap(out/(name+'.bin'),dtype='<f4',mode='r',shape=(1080,1920,4))[344,908]
    return r,int(pixel[3])
repro,flags=run('reproduce-2048',2048)
assert flags&16
assert repro['artifacts']['.bin']==__import__('hashlib').sha256((root/'fullhd/canonical-2048-s7.bin').read_bytes()).hexdigest()
lo,hi=512,2048
search=[]
while hi-lo>1:
    mid=(lo+hi)//2
    receipt,flags=run('prefix-'+str(mid),mid)
    search.append(dict(samples=mid,flags=flags))
    if flags&16: hi=mid
    else: lo=mid
_,deep=run('deep-'+str(hi),hi,['--max-events','256','--max-path-rays','2048','--max-total-rays','8192'])
_,unshadowed=run('no-shadows-'+str(hi),hi,['--no-shadows'])
result=dict(pixel=[908,344],first_failing_sample_count=hi,last_clean_sample_count=lo,
            zero_based_failing_sample=hi-1,reproduced_byte_exact=True,search=search,
            deep_budget_flags=deep,no_shadows_flags=unshadowed,
            optical_policy_changed=False,
            scope='Full-HD seed-7 production-policy diagnostic. Failures are retained, never accepted as quality references.')
(out/'diagnosis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2),flush=True)
