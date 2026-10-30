#!/usr/bin/env python3
"""Independent polynomial first-zero/gradient oracle for exported R16 cells."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from numpy.polynomial import Polynomial as Poly

H=1/1792


def polynomial(values,start,end):
    axes=[Poly([start[i],end[i]-start[i]]) for i in range(3)]
    p=Poly([0.])
    for k,v in enumerate(values):
        term=Poly([float(v)])
        for axis in range(3):term*=axes[axis] if k&(1<<axis) else 1-axes[axis]
        p+=term
    return p


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('observations',type=Path);p.add_argument('receipt',type=Path)
    args=p.parse_args();raw=args.observations.read_bytes()
    rows=np.frombuffer(raw,'<f4').reshape(22,24).astype(np.float64)
    cases=[];gradient_checks=0
    rng=np.random.default_rng(0x534446)
    for row in rows:
        t,status,steps,gpu_l=row[:4];o=row[4:7];d=row[8:11];values=row[12:20]
        start,limit,kind,epsilon=row[20:];kind=int(kind)
        item=dict(kind=kind,epsilon=float(epsilon),status=int(status),steps=int(steps),t=float(t))
        if kind==9:
            passed=status==2 and np.isfinite(t);item['expected']='nonfinite field diagnosed'
        elif kind in (5,6,7,8):
            expected=1 if kind in (5,8) else 0
            passed=status==expected
            if kind==5:passed=passed and t==start and steps==64
            if kind==8:passed=passed and t==start
            item['expected']={5:'bounded fallback',6:'covered endpoint miss',7:'uniform empty miss',8:'uniform filled hit'}[kind]
        else:
            f0=(o+d*start)/H
            exits=np.where(d>1e-15,(H-o)/np.where(d>1e-15,d,1),np.inf)
            end=float(np.min(exits));f1=(o+d*end)/H
            poly=polynomial(values,f0,f1)
            roots=poly.roots()
            roots=sorted(float(x.real) for x in roots if abs(x.imag)<1e-7 and -1e-7<=x.real<=1+1e-7)
            hit_s=(t-start)/(end-start)
            residual=abs(float(poly(hit_s)))
            first=start+roots[0]*(end-start) if roots else None
            passed=status==1 and start-3e-7<=t<=end+3e-7 and residual<=epsilon*H+4e-7
            if first is not None:passed=passed and t<=first+3e-7
            item.update(first_reconstructed_zero=first,residual=residual,field_band=epsilon*H,
                expected='hit band without advancing beyond first reconstructed zero',
                corners=values.tolist())
        if np.isfinite(values).all():
            table=values.reshape(2,2,2)
            edges=[np.max(np.abs(np.diff(table,axis=i)))/H for i in range(3)]
            cpu_l=max(float(np.linalg.norm(edges)),1e-5)
            passed=passed and abs(gpu_l-cpu_l)<=max(1e-5,1e-5*cpu_l)
            # Derivatives of the independent trilinear polynomial, sampled away
            # from cell faces. The bound is also compared directly to GPU output.
            for point in rng.uniform(.001,.999,size=(128,3)):
                g=[]
                for axis in range(3):
                    lo=point.copy();hi=point.copy();lo[axis]=0;hi[axis]=1
                    g.append(float(polynomial(values,lo,hi)(1)-polynomial(values,lo,hi)(0))/H)
                passed=passed and np.linalg.norm(g)<=cpu_l*(1+1e-12)+1e-12
                gradient_checks+=1
            item['gpu_lipschitz']=float(gpu_l);item['cpu_lipschitz']=cpu_l
        item['passed']=bool(passed);cases.append(item)
    result=dict(observations_sha256=hashlib.sha256(raw).hexdigest(),cases=cases,
        independent_gradient_checks=gradient_checks,passed=all(c['passed'] for c in cases),
        scope='Actual refinement loop with controlled quantized cells; not a global source-geometry equivalence proof')
    args.receipt.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(passed=result['passed'],cases=len(cases),gradient_checks=gradient_checks,
        failed=[(c['kind'],c['epsilon']) for c in cases if not c['passed']])))
    if not result['passed']:raise SystemExit(1)


if __name__=='__main__':main()
